/*
 * Health numbers in, the city's coarse body, heart and sleep levels out.
 *
 * The watch reads a few plain numbers and nothing else reaches the city: how
 * many activity rings are closed today, steps today, the latest and resting
 * heart rate, and how long it slept last night. This turns them into the
 * three moods Semantics.swift carries. It keeps nothing between calls and
 * imports no HealthKit, so city-check holds every cut-off on Linux.
 *
 * The cut-offs are the owner's (NF10a, 2026-10-01). Rings win over steps;
 * steps are only read when there is no ring reading. Missing data is `none`,
 * never a guess, and no level is meant as a medical judgement.
 */

/// One read of the watch's health numbers. Each is nil when it was not
/// recorded; a negative value counts as not recorded.
public struct HealthReading: Equatable, Sendable {
    /// Activity rings closed today, 0 to 3.
    public var ringsClosed: Int?
    public var steps: Int?
    /// Beats per minute.
    public var heartRate: Double?
    public var restingHeartRate: Double?
    /// Last night's time asleep.
    public var secondsAsleep: Double?

    public init(
        ringsClosed: Int? = nil, steps: Int? = nil, heartRate: Double? = nil,
        restingHeartRate: Double? = nil, secondsAsleep: Double? = nil
    ) {
        self.ringsClosed = ringsClosed
        self.steps = steps
        self.heartRate = heartRate
        self.restingHeartRate = restingHeartRate
        self.secondsAsleep = secondsAsleep
    }
}

/// The three levels a reading reduces to.
public struct HealthBuckets: Equatable, Sendable {
    public var body: BodyActivity
    public var heart: HeartMood
    public var sleep: SleepMood

    public init(body: BodyActivity, heart: HeartMood, sleep: SleepMood) {
        self.body = body
        self.heart = heart
        self.sleep = sleep
    }

    public init(_ reading: HealthReading) {
        self.init(
            body: Self.body(rings: reading.ringsClosed, steps: reading.steps),
            heart: Self.heart(latest: reading.heartRate, resting: reading.restingHeartRate),
            sleep: Self.sleep(seconds: reading.secondsAsleep))
    }

    static func body(rings: Int?, steps: Int?) -> BodyActivity {
        if let rings, rings >= 0 {
            switch rings {
            case 0: return .resting
            case 1: return .stirring
            case 2: return .moving
            default: return .full
            }
        }
        guard let steps, steps >= 0 else { return .none }
        switch steps {
        case ..<2_000: return .resting
        case ..<6_000: return .stirring
        case ..<10_000: return .moving
        default: return .full
        }
    }

    /// Lively at 20 bpm or more above resting.
    static func heart(latest: Double?, resting: Double?) -> HeartMood {
        guard let latest, let resting, latest >= 0, resting >= 0 else { return .none }
        return latest >= resting + 20 ? .lively : .still
    }

    /// Rested at seven hours or more.
    static func sleep(seconds: Double?) -> SleepMood {
        guard let seconds, seconds >= 0 else { return .none }
        return seconds >= 7 * 3_600 ? .rested : .tired
    }
}
