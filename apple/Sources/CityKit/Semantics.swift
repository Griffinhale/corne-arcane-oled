/*
 * The host's semantic input, for a Swift caller that has some.
 *
 * The same bounded values the daemon sends the keyboard over Raw HID v3, as
 * Swift enums: a scene, the civic floor, mode and intensity, one secondary
 * activity, and an optional notification summary. Then the off-keyboard
 * signals, which no keyboard ever receives: a typing summary and reduced
 * health buckets, each `none` unless a shell has one. Every field is a small
 * integer. There is no string anywhere in this file, so a title, a URL or a
 * health sample has nothing to travel in.
 *
 * Nothing is accepted here. City.set(_:) hands the packed struct to the C
 * library, which assembles the greeting the keyboard would have received and
 * runs duel_host_accept on it, so a value the firmware would refuse is refused
 * with the firmware's own DUEL_CITY_ERR_INPUT.
 *
 * Swift raw values must be literals, so the numbers are restated from
 * duel_host.h and duel_city.h, whose enums say they are never renumbered.
 * city-check holds the counts against the library's constants and renders
 * every value.
 * The names avoid SwiftUI's Scene, ActivityKit's Activity and Foundation's
 * Notification, which the app targets import beside this module.
 */

import CCorneArcaneCity

/// DUEL_HOST_SCENE_*: the broad posture a civic floor pairs with to pick a district.
public enum HostScene: UInt8, CaseIterable, Sendable {
    case duel = 0
    case archive = 1
    case focus = 2
    case revel = 3
}

/// DUEL_CIVIC_FLOOR_*: which tower floor is occupied (civic byte bits 0-1).
public enum CivicFloor: UInt8, CaseIterable, Sendable {
    case commons = 0
    case research = 1
    case workshop = 2
    case special = 3
}

/// DUEL_CIVIC_MODE_* (civic byte bits 2-3). The fourth value is reserved and
/// has no case.
public enum CivicMode: UInt8, CaseIterable, Sendable {
    case normal = 0
    case quiet = 1
    case urgent = 2
}

/// DUEL_CIVIC_INTENSITY_*: background host workload (civic byte bits 4-5).
public enum CivicIntensity: UInt8, CaseIterable, Sendable {
    case calm = 0
    case active = 1
    case busy = 2
    case saturated = 3
}

/// DUEL_CIVIC_SECONDARY_*: one supporting object or ambience (secondary byte bits 0-2).
public enum SecondaryActivity: UInt8, CaseIterable, Sendable {
    case none = 0
    case media = 1
    case transfer = 2
    case system = 3
    case calendar = 4
    case scroll = 5
    case tab = 6
    case page = 7
}

/// DUEL_HOST_CATEGORY_* of a non-empty summary. NONE belongs to the empty
/// summary, which is a nil NotificationSummary.
public enum NotificationCategory: UInt8, CaseIterable, Sendable {
    case terminal = 1
    case communication = 2
    case transfer = 3
    case system = 4
    case calendar = 5
    case security = 6
    case other = 7
}

/// DUEL_HOST_PRIORITY_* of a non-empty summary.
public enum NotificationPriority: UInt8, CaseIterable, Sendable {
    case low = 1
    case normal = 2
    case critical = 3
}

/// DUEL_CITY_TEMPO_*: typing tempo from a desktop's opt-in typing summary.
/// Like every off-keyboard signal below, `none` is zero and is what a shell
/// without the signal sends, and the value never reaches the keyboard.
public enum TypingTempo: UInt8, CaseIterable, Sendable {
    case none = 0
    case deliberate = 1
    case flowing = 2
    case rapid = 3
    case frantic = 4
}

/// DUEL_CITY_SPREAD_*: how even the gaps inside a typing burst are.
public enum TypingSpread: UInt8, CaseIterable, Sendable {
    case none = 0
    case steady = 1
    case varied = 2
    case irregular = 3
}

/// DUEL_CITY_ROW_*: the keyboard row with the most keydowns.
public enum TypingRow: UInt8, CaseIterable, Sendable {
    case none = 0
    case top = 1
    case home = 2
    case bottom = 3
    case thumb = 4
}

/// DUEL_CITY_ROW_SPREAD_*: how much of the typing that row holds.
public enum TypingRowSpread: UInt8, CaseIterable, Sendable {
    case none = 0
    case focused = 1
    case mixed = 2
    case even = 3
}

/// DUEL_CITY_BODY_*: body activity today, reduced on the watch: activity
/// rings closed, or a step band when there are no rings.
public enum BodyActivity: UInt8, CaseIterable, Sendable {
    case none = 0
    case resting = 1
    case stirring = 2
    case moving = 3
    case full = 4
}

/// DUEL_CITY_HEART_*: heart rate as a mood, never a reading.
public enum HeartMood: UInt8, CaseIterable, Sendable {
    case none = 0
    case still = 1
    case lively = 2
}

/// DUEL_CITY_SLEEP_*: last night's sleep, as a mood for the day.
public enum SleepMood: UInt8, CaseIterable, Sendable {
    case none = 0
    case rested = 1
    case tired = 2
}

/// DUEL_CITY_SEASON_*: the season, from the shell's own calendar (ABI 9). The
/// keyboard has no date; a season is not weather. Nothing draws it yet.
public enum CitySeason: UInt8, CaseIterable, Sendable {
    case none = 0
    case spring = 1
    case summer = 2
    case autumn = 3
    case winter = 4
}

/// The day's tallies (ABI 9): spells cast, pips of health lost and champions
/// felled today, as `City.stats` counts them. The shell keeps them and passes
/// them in; the library stores nothing, so the same seed and inputs still give
/// the same world. Each saturates at 255 and any byte is a count. Zero is an
/// empty day. Nothing draws them yet.
public struct DayTallies: Equatable, Sendable {
    public var casts: UInt8
    public var impacts: UInt8
    public var knockdowns: UInt8

    public init(casts: UInt8 = 0, impacts: UInt8 = 0, knockdowns: UInt8 = 0) {
        self.casts = casts
        self.impacts = impacts
        self.knockdowns = knockdowns
    }
}

/// A non-empty notification summary. The counters are plain integers, so a
/// count above 15, an age above 7, or a persistent summary below critical
/// reaches the C check and is refused there.
public struct NotificationSummary: Equatable, Sendable {
    public var count: UInt8
    public var category: NotificationCategory
    public var priority: NotificationPriority
    public var age: UInt8
    public var persistent: Bool

    public init(
        count: UInt8, category: NotificationCategory, priority: NotificationPriority,
        age: UInt8 = 0, persistent: Bool = false
    ) {
        self.count = count
        self.category = category
        self.priority = priority
        self.age = age
        self.persistent = persistent
    }
}

/// Everything a host can say about itself, as the keyboard receives it.
/// The defaults are the resting world: the Commons, nothing pending, online.
public struct CitySemantics: Equatable, Sendable {
    public var scene: HostScene
    public var floor: CivicFloor
    public var mode: CivicMode
    public var intensity: CivicIntensity
    public var activity: SecondaryActivity
    public var notification: NotificationSummary?
    public var online: Bool
    public var tempo: TypingTempo
    public var spread: TypingSpread
    public var row: TypingRow
    public var rowSpread: TypingRowSpread
    public var body: BodyActivity
    public var heart: HeartMood
    public var sleep: SleepMood
    public var season: CitySeason
    public var tallies: DayTallies

    public init(
        scene: HostScene = .duel, floor: CivicFloor = .commons, mode: CivicMode = .normal,
        intensity: CivicIntensity = .calm, activity: SecondaryActivity = .none,
        notification: NotificationSummary? = nil, online: Bool = true,
        tempo: TypingTempo = .none, spread: TypingSpread = .none, row: TypingRow = .none,
        rowSpread: TypingRowSpread = .none, body: BodyActivity = .none,
        heart: HeartMood = .none, sleep: SleepMood = .none, season: CitySeason = .none,
        tallies: DayTallies = DayTallies()
    ) {
        self.scene = scene
        self.floor = floor
        self.mode = mode
        self.intensity = intensity
        self.activity = activity
        self.notification = notification
        self.online = online
        self.tempo = tempo
        self.spread = spread
        self.row = row
        self.rowSpread = rowSpread
        self.body = body
        self.heart = heart
        self.sleep = sleep
        self.season = season
        self.tallies = tallies
    }

    /// duel_city_input_t, packed as DUEL_CIVIC_PACK and DUEL_SECONDARY_PACK pack it.
    func input(seed: UInt8) -> duel_city_input_t {
        var input = duel_city_input_t()
        input.scene = scene.rawValue
        input.civic = floor.rawValue | (mode.rawValue << 2) | (intensity.rawValue << 4)
        input.secondary = activity.rawValue
        if let notification {
            input.notif_count = notification.count
            input.category = notification.category.rawValue
            input.priority = notification.priority.rawValue
            input.age = notification.age
            input.persistent = notification.persistent ? 1 : 0
        }
        input.online = online ? 1 : 0
        input.seed = seed
        input.tempo = tempo.rawValue
        input.spread = spread.rawValue
        input.row = row.rawValue
        input.row_spread = rowSpread.rawValue
        input.body = body.rawValue
        input.heart = heart.rawValue
        input.sleep = sleep.rawValue
        input.season = season.rawValue
        input.tally_casts = tallies.casts
        input.tally_impacts = tallies.impacts
        input.tally_knockdowns = tallies.knockdowns
        return input
    }
}

extension City {
    /// Show these semantics from the next frame on. Throws the C library's
    /// CityError (DUEL_CITY_ERR_INPUT) for anything the firmware would refuse,
    /// and then keeps showing what it showed before.
    public func set(_ semantics: CitySemantics) throws {
        try accept(semantics.input(seed: seed))
    }
}
