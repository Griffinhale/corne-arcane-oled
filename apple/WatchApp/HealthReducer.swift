/* Read health only in the watch process. Retain levels, never samples. */
import CityKit
import Foundation

/// Query boundaries use calendar arithmetic so daylight-saving days stay local.
struct WatchHealthWindow {
    let day: DateInterval
    let sleep: DateInterval

    init(at now: Date, calendar: Calendar) {
        let midnight = calendar.startOfDay(for: now)
        day = DateInterval(start: midnight, end: now)
        let noon = calendar.date(bySettingHour: 12, minute: 0, second: 0, of: midnight)!
        let yesterdayNoon = calendar.date(byAdding: .day, value: -1, to: noon)!
        sleep = DateInterval(start: yesterdayNoon, end: min(noon, now))
    }

    /// Count overlapping sleep sources/stages once; discard time outside the night.
    static func secondsAsleep(_ intervals: [DateInterval], in window: DateInterval) -> Double? {
        let clipped = intervals.compactMap { $0.intersection(with: window) }
            .filter { $0.duration > 0 }.sorted { $0.start < $1.start }
        guard var merged = clipped.first else { return nil }
        var seconds = 0.0
        for interval in clipped.dropFirst() {
            if interval.start <= merged.end {
                merged = DateInterval(start: merged.start, end: max(merged.end, interval.end))
            } else {
                seconds += merged.duration
                merged = interval
            }
        }
        return seconds + merged.duration
    }
}

@MainActor
final class HealthReducer {
    typealias Read = (Date, Calendar) async throws -> HealthReading
    private let read: Read
    private(set) var buckets = HealthBuckets(HealthReading())

    init(read: @escaping Read) { self.read = read }

    func refresh(at date: Date, calendar: Calendar) async {
        let next: HealthBuckets
        do {
            next = HealthBuckets(try await read(date, calendar))
        } catch {
            next = HealthBuckets(HealthReading())
        }
        // A query completing after the scene stops must not publish stale levels.
        guard !Task.isCancelled else { return }
        buckets = next
    }

    static func live() -> HealthReducer {
        #if canImport(HealthKit)
            let source = WatchHealthSource()
            return HealthReducer { date, calendar in
                try await source.read(at: date, calendar: calendar)
            }
        #else
            return HealthReducer { _, _ in HealthReading() }
        #endif
    }

    #if DEBUG && targetEnvironment(simulator)
        /// Synthetic input exercises the production reducer/setter/replay path.
        /// This never seeds or writes the HealthKit store.
        static func preview(arguments: [String]) -> HealthReducer? {
            if arguments.contains("--health-sample") {
                return HealthReducer { _, _ in
                    HealthReading(
                        ringsClosed: 3, heartRate: 90,
                        restingHeartRate: 60, secondsAsleep: 28_800)
                }
            }
            if arguments.contains("--health-empty") {
                return HealthReducer { _, _ in HealthReading() }
            }
            return nil
        }
    #endif
}

#if canImport(HealthKit)
    import HealthKit

    @MainActor
    final class WatchHealthSource {
        private let store = HKHealthStore()
        private var authorization: Task<Void, Error>?
        private let steps = HKQuantityType(.stepCount)
        private let heart = HKQuantityType(.heartRate)
        private let resting = HKQuantityType(.restingHeartRate)
        private let sleep = HKCategoryType(.sleepAnalysis)

        func read(at date: Date, calendar: Calendar) async throws -> HealthReading {
            guard HKHealthStore.isHealthDataAvailable() else { return HealthReading() }
            if authorization == nil {
                authorization = Task {
                    try await store.requestAuthorization(
                        toShare: [],
                        read: [
                            steps, heart, resting, sleep, HKObjectType.activitySummaryType(),
                        ])
                }
            }
            // Keep a cancelled/failed authorization result for this launch.
            // A foreground refresh must not repeatedly reopen a dismissed sheet.
            try await authorization?.value
            try Task.checkCancellation()
            let window = WatchHealthWindow(at: date, calendar: calendar)
            // Each query can be empty independently. Authorization success does
            // not reveal read permission, and write status cannot answer it.
            let rings = try? await ringsClosed(at: date, calendar: calendar)
            let stepCount = try? await stepCount(in: window.day)
            let currentHeart = try? await latest(heart, in: window.day)
            let baseline = try? await latest(resting, in: window.day)
            let seconds = try? await secondsAsleep(in: window.sleep)
            return HealthReading(
                ringsClosed: rings, steps: stepCount, heartRate: currentHeart,
                restingHeartRate: baseline, secondsAsleep: seconds)
        }

        private func ringsClosed(at date: Date, calendar: Calendar) async throws -> Int? {
            var components = calendar.dateComponents([.era, .year, .month, .day], from: date)
            components.calendar = calendar
            let query = HKActivitySummaryQueryDescriptor(
                predicate: HKQuery.predicateForActivitySummary(with: components))
            guard let summary = try await query.result(for: store).first else { return nil }
            return Self.closedRings(summary)
        }

        static func closedRings(_ summary: HKActivitySummary) -> Int? {
            let move: Double
            let goal: Double
            if summary.activityMoveMode == .appleMoveTime {
                move = summary.appleMoveTime.doubleValue(for: .minute())
                goal = summary.appleMoveTimeGoal.doubleValue(for: .minute())
            } else {
                move = summary.activeEnergyBurned.doubleValue(for: .kilocalorie())
                goal = summary.activeEnergyBurnedGoal.doubleValue(for: .kilocalorie())
            }
            guard goal > 0,
                let exerciseGoal = summary.exerciseTimeGoal?.doubleValue(for: .minute()),
                let standGoal = summary.standHoursGoal?.doubleValue(for: .count()),
                exerciseGoal > 0, standGoal > 0
            else { return nil }
            return (move >= goal ? 1 : 0)
                + (summary.appleExerciseTime.doubleValue(for: .minute()) >= exerciseGoal ? 1 : 0)
                + (summary.appleStandHours.doubleValue(for: .count()) >= standGoal ? 1 : 0)
        }

        private func stepCount(in window: DateInterval) async throws -> Int? {
            let query = HKStatisticsQueryDescriptor(
                predicate: .quantitySample(type: steps, predicate: predicate(window)),
                options: .cumulativeSum)
            guard
                let value = try await query.result(for: store)?.sumQuantity()?.doubleValue(
                    for: .count()),
                value.isFinite, value >= 0, value < Double(Int.max)
            else { return nil }
            return Int(value)
        }

        private func latest(_ type: HKQuantityType, in window: DateInterval) async throws -> Double?
        {
            let query = HKSampleQueryDescriptor(
                predicates: [.quantitySample(type: type, predicate: predicate(window))],
                sortDescriptors: [SortDescriptor(\HKQuantitySample.endDate, order: .reverse)],
                limit: 1)
            return try await query.result(for: store).first?.quantity.doubleValue(
                for: HKUnit.count().unitDivided(by: .minute()))
        }

        private func secondsAsleep(in window: DateInterval) async throws -> Double? {
            // Overlap rather than strict start: sleep often crosses the boundary.
            let query = HKSampleQueryDescriptor(
                predicates: [
                    .categorySample(
                        type: sleep,
                        predicate:
                            HKQuery.predicateForSamples(withStart: window.start, end: window.end))
                ],
                sortDescriptors: [])
            let samples = try await query.result(for: store)
            return Self.secondsAsleep(samples, in: window)
        }

        static func secondsAsleep(_ samples: [HKCategorySample], in window: DateInterval) -> Double?
        {
            let asleep = Set([
                HKCategoryValueSleepAnalysis.asleepUnspecified.rawValue,
                HKCategoryValueSleepAnalysis.asleepCore.rawValue,
                HKCategoryValueSleepAnalysis.asleepDeep.rawValue,
                HKCategoryValueSleepAnalysis.asleepREM.rawValue,
            ])
            return WatchHealthWindow.secondsAsleep(
                samples.filter { asleep.contains($0.value) }
                    .map { DateInterval(start: $0.startDate, end: $0.endDate) }, in: window)
        }

        private func predicate(_ window: DateInterval) -> NSPredicate {
            HKQuery.predicateForSamples(
                withStart: window.start, end: window.end,
                options: [.strictStartDate, .strictEndDate])
        }
    }
#endif
