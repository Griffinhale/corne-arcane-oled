/*
 * The host's semantic input, for a Swift caller that has some.
 *
 * The same bounded values the daemon sends the keyboard over Raw HID v3, as
 * Swift enums: a scene, the civic floor, mode and intensity, one secondary
 * activity, and an optional notification summary. Every field is a small
 * integer. There is no string anywhere in this file, so a title, a URL or a
 * health sample has nothing to travel in.
 *
 * Nothing is accepted here. City.set(_:) hands the packed struct to the C
 * library, which assembles the greeting the keyboard would have received and
 * runs duel_host_accept on it, so a value the firmware would refuse is refused
 * with the firmware's own DUEL_CITY_ERR_INPUT.
 *
 * Swift raw values must be literals, so the numbers are restated from
 * duel_host.h, whose enums say they are never renumbered. city-check holds
 * the counts against the library's wire constants and renders every value.
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

    public init(
        scene: HostScene = .duel, floor: CivicFloor = .commons, mode: CivicMode = .normal,
        intensity: CivicIntensity = .calm, activity: SecondaryActivity = .none,
        notification: NotificationSummary? = nil, online: Bool = true
    ) {
        self.scene = scene
        self.floor = floor
        self.mode = mode
        self.intensity = intensity
        self.activity = activity
        self.notification = notification
        self.online = online
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
