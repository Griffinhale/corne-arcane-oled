/*
 * The foreground watch world's clock and rendering cadence.
 *
 * A timer is only a wake-up. Every image is named by the current whole
 * renderer tick since local midnight, so skipped presentation callbacks do
 * not slow the city down. Reopening replays seed 0x5A with the current health
 * levels, and the body level sets the civic intensity, so the Observatory's
 * instrument moves through its four stages with the day (R1). The complication
 * keeps its separate self-playing input.
 */

import CityKit
import CoreGraphics
import Foundation
import Observation

private let watchCityCrop = CityPixelRect(x: 47, y: 13, width: 162, height: 197)

struct WatchWorldMoment: Sendable {
    let anchor: Date
    let worldMs: UInt32
}

struct WatchRenderedMoment: Sendable {
    let moment: WatchWorldMoment
    let frame: CityFrame
    let health: HealthBuckets
}

private enum WatchCityRenderError: Error {
    case invalidCrop
}

private enum WatchPresentationState {
    case stopped
    case paused
    case active
}

/// Own the mutable C renderer on one serial executor. A cold local-midnight
/// replay can take long enough to notice in Debug builds, so it must never
/// block SwiftUI's main actor.
actor WatchCityRenderer {
    private var city: City?
    private var worldAnchor: Date?
    private var worldMs: UInt32 = 0

    private var health: HealthBuckets?

    func render(_ moment: WatchWorldMoment, health: HealthBuckets) throws -> WatchRenderedMoment {
        do {
            let needsNewWorld =
                city == nil || worldAnchor != moment.anchor || self.health != health
                || moment.worldMs < worldMs
            if needsNewWorld {
                let city = try City(seed: 0x5A, layout: .town)
                try city.set(
                    CitySemantics(
                        floor: .special, intensity: health.body.observatoryIntensity,
                        body: health.body, heart: health.heart, sleep: health.sleep))
                try city.seek(to: moment.worldMs) { try Task.checkCancellation() }
                self.city = city
                worldAnchor = moment.anchor
                self.health = health
            } else {
                try city?.seek(to: moment.worldMs) { try Task.checkCancellation() }
            }
        } catch {
            /* A cancelled forward seek leaves its City between named moments.
             * Discard it so a later clock rewind cannot reuse partial state. */
            city = nil
            worldAnchor = nil
            worldMs = 0
            throw error
        }

        guard let city else { throw WatchCityRenderError.invalidCrop }
        let frame = moment.worldMs / City.frameIntervalMs
        let pixels = try city.render(frame: frame)
        guard
            let crop = CityFrame(
                worldMs: moment.worldMs,
                frame: frame,
                pixels: pixels,
                width: city.width,
                height: city.height
            ).cropped(to: watchCityCrop)
        else { throw WatchCityRenderError.invalidCrop }

        worldMs = moment.worldMs
        return WatchRenderedMoment(moment: moment, frame: crop, health: health)
    }
}

@MainActor
@Observable
final class WatchCityDriver {
    static let crop = watchCityCrop

    private(set) var image: CGImage?
    private(set) var worldMs: UInt32 = 0

    @ObservationIgnored private let calendar: Calendar
    @ObservationIgnored private let now: () -> Date
    @ObservationIgnored private let health: HealthReducer
    @ObservationIgnored private var healthTask: Task<Void, Never>?
    @ObservationIgnored private var healthGeneration: UInt64 = 0
    @ObservationIgnored private let renderer = WatchCityRenderer()
    @ObservationIgnored private var timer: Timer?
    @ObservationIgnored private var renderTask: Task<Void, Never>?
    @ObservationIgnored private var isRendering = false
    @ObservationIgnored private var presentationState = WatchPresentationState.stopped
    @ObservationIgnored private var pendingPausedStill = false
    @ObservationIgnored private var hasRenderedCurrentMoment = false
    @ObservationIgnored private var presentationGeneration: UInt64 = 0
    @ObservationIgnored private var activationStartedAt: TimeInterval?

    private static let seed: UInt8 = 0x5A
    private static let presentationInterval: TimeInterval = 0.1
    private static let presentationIntervalMs: UInt32 = 100

    init(calendar: Calendar = .autoupdatingCurrent, now: @escaping () -> Date = Date.init) {
        self.calendar = calendar
        #if DEBUG && targetEnvironment(simulator)
            let arguments = ProcessInfo.processInfo.arguments
            health = HealthReducer.preview(arguments: arguments) ?? .live()
            if arguments.contains("--health-fixed-time") {
                self.now = { calendar.startOfDay(for: now()).addingTimeInterval(120) }
            } else {
                self.now = now
            }
        #else
            health = .live()
            self.now = now
        #endif
        image = Self.standaloneImage(worldMs: 0)
    }

    /// Begin foreground presentation. The timer only requests work; a single
    /// in-flight render coalesces any callbacks that arrive while seeking.
    func activate() {
        guard presentationState != .active else { return }
        presentationState = .active
        pendingPausedStill = false
        presentationGeneration &+= 1
        let activationGeneration = presentationGeneration
        activationStartedAt = ProcessInfo.processInfo.systemUptime
        scheduleFrame()
        startHealthUpdates()

        let timer = Timer(
            timeInterval: Self.presentationInterval,
            repeats: true
        ) { [weak self] _ in
            Task { @MainActor [weak self] in
                guard let self,
                    self.presentationState == .active,
                    self.presentationGeneration == activationGeneration
                else { return }
                self.scheduleFrame()
            }
        }
        timer.tolerance = Self.presentationInterval / 5
        RunLoop.main.add(timer, forMode: .common)
        self.timer = timer
    }

    /// Stop repeating work for an inactive or dim scene. If the first exact
    /// wall-clock frame is already being prepared, allow that one request to
    /// finish so the launch placeholder is replaced by a current frozen still.
    func pause() {
        let wasPlaying = presentationState == .active
        presentationState = .paused
        stopHealthUpdates()
        activationStartedAt = nil
        timer?.invalidate()
        timer = nil

        if hasRenderedCurrentMoment {
            pendingPausedStill = false
            presentationGeneration &+= 1
            renderTask?.cancel()
        } else {
            pendingPausedStill = true
            if !isRendering { scheduleFrame() }
        }
        #if DEBUG
            if wasPlaying {
                print("CORNE_WATCH_PAUSED world_ms=\(worldMs)")
            }
        #endif
    }

    /// Tear down presentation when the view itself leaves the hierarchy. No
    /// pending result may publish after this point.
    func stop() {
        let wasPlaying = presentationState == .active
        presentationState = .stopped
        stopHealthUpdates()
        pendingPausedStill = false
        presentationGeneration &+= 1
        activationStartedAt = nil
        renderTask?.cancel()
        timer?.invalidate()
        timer = nil
        #if DEBUG
            if wasPlaying {
                print("CORNE_WATCH_PAUSED world_ms=\(worldMs)")
            }
        #endif
    }

    private func startHealthUpdates() {
        healthGeneration &+= 1
        let generation = healthGeneration
        healthTask = Task { @MainActor [weak self] in
            while !Task.isCancelled {
                guard let self, self.healthGeneration == generation else { return }
                let previous = self.health.buckets
                await self.health.refresh(at: self.now(), calendar: self.calendar)
                guard !Task.isCancelled, self.healthGeneration == generation else { return }
                if previous != self.health.buckets {
                    self.renderTask?.cancel()
                    self.scheduleFrame()
                }
                do { try await Task.sleep(for: .seconds(60)) } catch { return }
            }
        }
    }

    private func stopHealthUpdates() {
        healthGeneration &+= 1
        healthTask?.cancel()
        healthTask = nil
    }

    private func scheduleFrame() {
        guard !isRendering else { return }
        isRendering = true
        let moment = Self.worldMoment(at: now(), calendar: calendar)
        let renderer = self.renderer
        let health = self.health.buckets

        renderTask = Task { @MainActor [weak self] in
            do {
                let rendered = try await renderer.render(moment, health: health)
                guard let self else { return }
                self.finishRendering(rendered, wasCancelled: Task.isCancelled)
            } catch is CancellationError {
                guard let self else { return }
                self.finishRendering(nil, wasCancelled: true)
            } catch {
                guard let self else { return }
                self.finishRendering(nil, wasCancelled: Task.isCancelled)
                assertionFailure("Corne Arcane watch rendering failed: \(error)")
            }
        }
    }

    private func finishRendering(
        _ rendered: WatchRenderedMoment?, wasCancelled: Bool
    ) {
        isRendering = false
        renderTask = nil

        let mayPublish =
            !wasCancelled
            && (presentationState == .active
                || (presentationState == .paused && pendingPausedStill))
        if mayPublish, let rendered, rendered.health == health.buckets,
            let image = rendered.frame.image
        {
            worldMs = rendered.moment.worldMs
            self.image = image
            #if DEBUG
                if activationStartedAt != nil {
                    print(
                        "CORNE_WATCH_HEALTH body=\(rendered.health.body.rawValue) "
                            + "heart=\(rendered.health.heart.rawValue) sleep=\(rendered.health.sleep.rawValue)"
                    )
                }
            #endif
            hasRenderedCurrentMoment = true
            pendingPausedStill = false
            if presentationState == .active, activationStartedAt != nil {
                #if DEBUG
                    let activationStartedAt = activationStartedAt!
                    let elapsedMs =
                        (ProcessInfo.processInfo.systemUptime - activationStartedAt) * 1_000
                    let formattedElapsedMs = String(format: "%.1f", elapsedMs)
                    print(
                        "CORNE_WATCH_READY world_ms=\(rendered.moment.worldMs) "
                            + "activation_ms=\(formattedElapsedMs)"
                    )
                #endif
                self.activationStartedAt = nil
            }
        }

        if presentationState == .paused {
            /* A request cancelled by a preceding lifecycle state may still be
             * draining when the view asks for its first frozen still. Start
             * that still as soon as the cancelled request releases the actor. */
            if pendingPausedStill, wasCancelled { scheduleFrame() }
            return
        }
        guard presentationState == .active else { return }
        if wasCancelled {
            scheduleFrame()
            return
        }
        /* A persistent renderer failure retries on the next timer wake rather
         * than immediately spinning an unpaced task loop. */
        guard let rendered else { return }

        /* A cold replay may finish several presentation intervals after the
         * moment it was requested. Catch up once immediately; ordinary fast
         * renders remain paced solely by the 10 fps wake-up timer. */
        let current = Self.worldMoment(at: now(), calendar: calendar)
        let movedBackward =
            current.anchor == rendered.moment.anchor
            && current.worldMs < rendered.moment.worldMs
        let fellBehind =
            current.anchor != rendered.moment.anchor || movedBackward
            || current.worldMs - rendered.moment.worldMs >= Self.presentationIntervalMs
        if fellBehind { scheduleFrame() }
    }

    private static func worldMoment(at date: Date, calendar: Calendar) -> WatchWorldMoment {
        let anchor = calendar.startOfDay(for: date)
        let elapsedMs = UInt64(max(0, date.timeIntervalSince(anchor)) * 1_000)
        let tick = UInt64(City.frameIntervalMs)
        let maximumWorldMs = UInt64(UInt32.max) / tick * tick
        let worldMs = min(elapsedMs / tick * tick, maximumWorldMs)
        return WatchWorldMoment(anchor: anchor, worldMs: UInt32(worldMs))
    }

    private static func standaloneImage(worldMs: UInt32) -> CGImage? {
        guard let city = try? City(seed: seed, layout: .town),
            (try? city.set(CitySemantics(floor: .special))) != nil,
            (try? city.seek(to: worldMs)) != nil,
            let pixels = try? city.render(frame: worldMs / City.frameIntervalMs)
        else { return nil }

        return CityFrame(
            worldMs: worldMs,
            frame: worldMs / City.frameIntervalMs,
            pixels: pixels,
            width: city.width,
            height: city.height
        ).cropped(to: crop)?.image
    }

    #if DEBUG
        static func previewImage(worldMs: UInt32 = 7_200_000) -> CGImage? {
            standaloneImage(worldMs: worldMs)
        }
    #endif
}
