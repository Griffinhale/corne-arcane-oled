import CityKit
import Foundation
import HealthKit

@main
struct WatchHealthCheck {
    @MainActor
    static func main() async throws {
        let missing = HealthBuckets(HealthReading())
        let sample = HealthBuckets(
            HealthReading(
                ringsClosed: 3, heartRate: 90,
                restingHeartRate: 60, secondsAsleep: 28_800))
        var reading = HealthReading()
        var denied = false
        enum Denied: Error { case access }
        let reducer = HealthReducer { _, _ in
            if denied { throw Denied.access }
            return reading
        }
        await reducer.refresh(at: Date(), calendar: .current)
        precondition(reducer.buckets == missing, "missing data must stay none")
        reading = HealthReading(
            ringsClosed: 3, heartRate: 90,
            restingHeartRate: 60, secondsAsleep: 28_800)
        await reducer.refresh(at: Date(), calendar: .current)
        precondition(reducer.buckets == sample, "readings must use HealthBuckets")
        denied = true
        await reducer.refresh(at: Date(), calendar: .current)
        precondition(reducer.buckets == missing, "denial must clear old levels")
        denied = false
        reading = HealthReading(steps: 6_000)
        await reducer.refresh(at: Date(), calendar: .current)
        precondition(reducer.buckets == HealthBuckets(body: .moving, heart: .none, sleep: .none))
        reading = HealthReading()
        await reducer.refresh(at: Date(), calendar: .current)
        precondition(reducer.buckets == missing, "successful empty queries also clear levels")
        print("PASS watch_health_missing_denied_partial_revoked")

        var calendar = Calendar(identifier: .gregorian)
        calendar.timeZone = TimeZone(identifier: "America/Los_Angeles")!
        let date = calendar.date(from: DateComponents(year: 2026, month: 3, day: 8, hour: 10))!
        let window = WatchHealthWindow(at: date, calendar: calendar)
        precondition(calendar.component(.hour, from: window.sleep.start) == 12)
        precondition(calendar.component(.day, from: window.sleep.start) == 7)
        precondition(window.sleep.end == date)
        let start = window.sleep.start
        let intervals = [
            DateInterval(start: start.addingTimeInterval(-100), duration: 200),
            DateInterval(start: start.addingTimeInterval(50), duration: 150),
            DateInterval(start: start.addingTimeInterval(300), duration: 100),
        ]
        precondition(WatchHealthWindow.secondsAsleep(intervals, in: window.sleep) == 300)
        precondition(WatchHealthWindow.secondsAsleep([], in: window.sleep) == nil)
        print("PASS watch_health_sleep_union_and_calendar_window")

        let summary = HKActivitySummary()
        precondition(WatchHealthSource.closedRings(summary) == nil, "unset goals are missing")
        summary.activeEnergyBurnedGoal = HKQuantity(unit: .kilocalorie(), doubleValue: 500)
        summary.exerciseTimeGoal = HKQuantity(unit: .minute(), doubleValue: 30)
        summary.standHoursGoal = HKQuantity(unit: .count(), doubleValue: 12)
        precondition(WatchHealthSource.closedRings(summary) == 0)
        summary.activeEnergyBurned = HKQuantity(unit: .kilocalorie(), doubleValue: 500)
        summary.appleExerciseTime = HKQuantity(unit: .minute(), doubleValue: 30)
        summary.appleStandHours = HKQuantity(unit: .count(), doubleValue: 12)
        precondition(WatchHealthSource.closedRings(summary) == 3)
        summary.activityMoveMode = .appleMoveTime
        summary.appleMoveTimeGoal = HKQuantity(unit: .minute(), doubleValue: 60)
        summary.appleMoveTime = HKQuantity(unit: .minute(), doubleValue: 59)
        precondition(WatchHealthSource.closedRings(summary) == 2)
        summary.appleMoveTime = HKQuantity(unit: .minute(), doubleValue: 60)
        precondition(WatchHealthSource.closedRings(summary) == 3)
        let samples = [
            HKCategoryValueSleepAnalysis.inBed, .awake, .asleepCore,
            .asleepDeep, .asleepREM, .asleepUnspecified,
        ].enumerated().map { index, value in
            HKCategorySample(
                type: HKCategoryType(.sleepAnalysis), value: value.rawValue,
                start: start.addingTimeInterval(Double(index) * 100),
                end: start.addingTimeInterval(Double(index + 1) * 100))
        }
        precondition(WatchHealthSource.secondsAsleep(samples, in: window.sleep) == 400)
        precondition(
            WatchHealthSource.secondsAsleep(Array(samples.prefix(2)), in: window.sleep) == nil)
        print("PASS watch_health_healthkit_ring_goals_and_sleep_stages")

        let cancelled = HealthReducer { _, _ in
            withUnsafeCurrentTask { $0?.cancel() }
            return HealthReading(ringsClosed: 3)
        }
        await Task { await cancelled.refresh(at: date, calendar: calendar) }.value
        precondition(cancelled.buckets == missing, "cancelled reads must not publish")
        print("PASS watch_health_cancelled_read")

        let moment = WatchWorldMoment(anchor: calendar.startOfDay(for: date), worldMs: 120_000)
        let renderer = WatchCityRenderer()
        let fallback = try await renderer.render(moment, health: missing).frame.pixels
        let changed = try await renderer.render(moment, health: sample).frame.pixels
        precondition(changed != fallback, "sample data must visibly change the watch crop")
        let fresh = try await WatchCityRenderer().render(moment, health: sample).frame.pixels
        precondition(changed == fresh, "bucket change must replay like a fresh world")
        let restored = try await renderer.render(moment, health: missing).frame.pixels
        precondition(restored == fallback, "missing data must restore deterministic fallback")
        print("PASS watch_health_visible_reseek_and_fallback")
    }
}
