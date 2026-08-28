/*
 * The city on screen, at the world's own cadence.
 *
 * Apple-only, and empty everywhere else, so the Linux build of this package
 * still compiles the parts that are portable. Everything that decides what
 * the city looks like is in C and is shared; this file only decides when to
 * ask and where to put the answer.
 */

#if canImport(SwiftUI)

    import Foundation
    import SwiftUI

    /// A world running against the display's clock.
    ///
    /// The timer only wakes us. World time is computed from elapsed wall time
    /// and floored to whole ticks, which is the accumulator the port notes
    /// insist on: the world's cadence is 40 ms and the display's is whatever
    /// ProMotion is doing, and counting timer fires drifts against both.
    @MainActor
    public final class CityDriver: ObservableObject {
        @Published public private(set) var image: CGImage?

        private let city: City
        private let started = Date()
        private var timer: Timer?

        public init(seed: UInt8, layout: Layout = .town) throws {
            self.city = try City(seed: seed, layout: layout)
            step()
        }

        public func start() {
            guard timer == nil else { return }
            let interval = Double(City.frameIntervalMs) / 1000
            let timer = Timer(timeInterval: interval, repeats: true) { [weak self] _ in
                Task { @MainActor in self?.step() }
            }
            /* .common so the city keeps running while a scroll view is being
             * dragged, which is the default mode's one surprise. */
            RunLoop.main.add(timer, forMode: .common)
            self.timer = timer
        }

        public func stop() {
            timer?.invalidate()
            timer = nil
        }

        /// One step of the world, from wall time rather than from a count.
        ///
        /// A backgrounded app resynchronises rather than replaying: the
        /// catch-up cap means a long absence lands in a world that skipped its
        /// own history, and it visibly jumps. That is the keyboard's own
        /// behaviour across a USB suspend, it is deliberate, and it is not
        /// fixed here. Use `City.seek` when the exact moment matters.
        private func step() {
            let step = City.frameIntervalMs
            let elapsed = UInt32(max(0, Date().timeIntervalSince(started)) * 1000)
            let now = (elapsed / step) * step
            city.advance(to: now)
            guard let pixels = try? city.render(frame: now / step) else { return }
            image = CityFrame(
                worldMs: now, frame: now / step, pixels: pixels,
                width: city.width, height: city.height
            ).image
        }
    }

    /// The city, filling whatever it is given, at whole pixels.
    ///
    /// Rendered at scale 1 and magnified here rather than in the renderer.
    /// `.interpolation(.none)` is the exact equivalent of the browser shell's
    /// `image-rendering: pixelated`, and it is why asking the library for a
    /// large scale would only produce pixels the compositor produces free.
    public struct CityView: View {
        @StateObject private var driver: CityDriver

        public init(driver: CityDriver) {
            _driver = StateObject(wrappedValue: driver)
        }

        public var body: some View {
            ZStack {
                Color.black
                if let image = driver.image {
                    Image(decorative: image, scale: 1)
                        .interpolation(.none)
                        .resizable()
                        .aspectRatio(contentMode: .fit)
                }
            }
            .onAppear { driver.start() }
            .onDisappear { driver.stop() }
        }
    }

#endif
