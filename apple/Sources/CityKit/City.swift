/*
 * CityKit — the city, on Apple platforms.
 *
 * The same C the firmware, the desktop window and the browser compile, with
 * Swift's own C interop in place of an FFI layer. Nothing here samples input:
 * on the keyboard key positions never leave the firmware, a phone has no
 * keyboard to read, and this file offers no way to supply one. The world
 * plays itself, exactly as it does in the browser.
 *
 * The library never allocates. A City is three caller-owned structs and a
 * pixel buffer, which is why a widget extension's memory ceiling is not a
 * consideration here.
 */

import CCorneArcaneCity

public enum Layout: Int32, CaseIterable, Sendable {
    case desk = 0
    case city = 1
    case left = 2
    case right = 3
    case town = 4
    case landscape = 5
}

public struct CityError: Error, CustomStringConvertible {
    public let code: Int32
    public let what: String

    public var description: String {
        let reason: String
        switch code {
        case -1: reason = "null pointer passed to the renderer"
        case -2: reason = "scale outside 1..16"
        case -3: reason = "pixel buffer shorter than the geometry"
        case -4: reason = "an input field is outside its enum or bit width"
        case -5: reason = "layout outside desk/city/left/right/town/landscape"
        default: reason = "renderer returned \(code)"
        }
        return "\(what): \(reason)"
    }
}

@inline(__always)
private func check(_ code: Int32, _ what: String) throws {
    if code < 0 { throw CityError(code: code, what: what) }
}

public struct WorldStats: Equatable, Sendable {
    public let ticks: UInt32
    public let casts: UInt32
    public let impacts: UInt32
    public let knockdowns: UInt32
}

public final class City {
    /* Presentation policy, asked of the renderer rather than decided again
     * here. The reason ABI 5 moved the scale rules into C, and ABI 6 the seek
     * run-up, is that a third shell would otherwise reimplement them and
     * drift -- and this is the third shell. */
    public static let abi = Int(duel_city_abi_version())
    public static let frameIntervalMs = duel_city_frame_interval_ms()
    public static let seekWarmFrames = UInt32(duel_city_seek_warm_frames())
    public static let tourLength = Int(duel_city_tour_length())

    public static func geometry(_ layout: Layout, scale: Int32 = 1) throws -> (
        width: Int, height: Int
    ) {
        var width: Int32 = 0
        var height: Int32 = 0
        try check(
            duel_city_geometry(layout.rawValue, scale, &width, &height), "geometry")
        return (Int(width), Int(height))
    }

    public static func defaultScale(_ layout: Layout) -> Int32 {
        duel_city_default_scale(layout.rawValue)
    }

    /* The grey to paint around the image. Present for completeness: a shell
     * that renders at scale 1 and magnifies in its view layer, which is what
     * every compositor-backed platform should do, paints its own ground. */
    public static func backdrop(_ layout: Layout) -> UInt8 {
        UInt8(clamping: duel_city_backdrop(layout.rawValue))
    }

    public let layout: Layout
    public let seed: UInt8
    public let width: Int
    public let height: Int

    /* The world's own clock, always a whole number of ticks. Rendering and the
     * simulation share it, so a position in this world is one number rather
     * than three that have to be kept consistent. */
    public private(set) var worldMs: UInt32 = 0

    private var state = duel_city_state_t()
    private var world = duel_ambient_t()
    private var input = duel_city_input_t()
    private var pixels: [UInt8]
    /* A second buffer for the run-up, which renders through the cheapest
     * layout rather than the one being asked for: the floor transition and
     * the outcome flash are both composed before any layout-specific drawing,
     * so any layout settles them, and LEFT is 4 kB a frame against the wide
     * town's 96. Sized once here so seeking allocates nothing. */
    private var warmPixels: [UInt8]

    public init(seed: UInt8, layout: Layout = .town, tourStop: Int = 0) throws {
        self.seed = seed
        self.layout = layout
        let size = try City.geometry(layout)
        self.width = size.width
        self.height = size.height
        self.pixels = [UInt8](repeating: 0, count: size.width * size.height)
        let warm = try City.geometry(.left)
        self.warmPixels = [UInt8](repeating: 0, count: warm.width * warm.height)
        duel_city_state_init(&state)
        duel_ambient_init(&world, seed)
        /* Start from a tour stop, which is valid by construction. The input
         * struct goes through the firmware's own acceptance path and a
         * hand-zeroed one is rejected with DUEL_CITY_ERR_INPUT. */
        try check(duel_city_tour_stop(Int32(tourStop), seed, &input), "tour stop")
        /* The world takes its first tick at time zero: advance(0) runs a tick,
         * so world time zero has already lived one. The browser and the
         * desktop agree on this, and a shared link only matches if this shell
         * agrees too. */
        _ = advance(to: 0)
    }

    /// Run the world up to `nowMs`. Returns the ticks actually run, which is
    /// below the elapsed ticks whenever the shell has been away: the catch-up
    /// cap means a long absence resynchronises instead of replaying, and the
    /// world visibly jumps. That is the keyboard's own behaviour across a USB
    /// suspend, and this shell does not paper over it.
    @discardableResult
    public func advance(to nowMs: UInt32) -> UInt8 {
        worldMs = nowMs
        return duel_ambient_advance(&world, nowMs)
    }

    /// Render the world as it stands. `frame` is the animation phase and
    /// belongs to the caller's redraw cadence; `elapsedMs` defaults to the
    /// world's own clock, which is what makes a seed and a millisecond name
    /// one world everywhere.
    @discardableResult
    public func render(frame: UInt32, elapsedMs: UInt32? = nil) throws -> [UInt8] {
        let now = elapsedMs ?? worldMs
        let code = pixels.withUnsafeMutableBufferPointer { buffer in
            duel_city_render(
                &state, &input, &world, now, frame, layout.rawValue, 1,
                buffer.baseAddress, buffer.count)
        }
        try check(code, "render")
        return pixels
    }

    func renderWarmUp(_ nowMs: UInt32, _ frame: UInt32) throws {
        let code = warmPixels.withUnsafeMutableBufferPointer { buffer in
            duel_city_render(
                &state, &input, &world, nowMs, frame, Layout.left.rawValue, 1,
                buffer.baseAddress, buffer.count)
        }
        try check(code, "seek warm-up")
    }

    /// Replay the world forward to `targetMs`, tick by tick.
    ///
    /// Deliberately not one long `advance`: `duel_ambient_advance`
    /// resynchronises across a gap longer than five ticks, so a single jump
    /// would land in a world that had skipped its own history. Stepping runs
    /// every tick, so the state at `targetMs` is a pure function of the seed
    /// and the number -- which is the whole of what a shared link promises.
    ///
    /// The last stretch is rendered as well as simulated. See
    /// `duel_city_seek_warm_frames` for why, and for how long.
    public func seek(to targetMs: UInt32) throws {
        let step = City.frameIntervalMs
        let warmFrom = targetMs > City.seekWarmFrames * step
            ? targetMs - City.seekWarmFrames * step : 0
        var t = worldMs
        while t < targetMs {
            t += step
            advance(to: t)
            if t >= warmFrom { try renderWarmUp(t, t / step) }
        }
    }

    public var stats: WorldStats {
        let s = duel_ambient_stats(&world)
        return WorldStats(
            ticks: s.ticks, casts: s.casts, impacts: s.impacts, knockdowns: s.knockdowns)
    }
}
