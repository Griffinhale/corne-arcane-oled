/*
 * The widget's timeline.
 *
 * This is the reason the port is worth doing on this platform rather than
 * another. The world is a pure function of (seed, elapsed_ms); WidgetKit's
 * timeline is "give me the frame at time T"; those are the same shape. The
 * determinism the parity gate protects is the property the platform's core
 * abstraction wants.
 *
 * The thing an ambient surface takes away is the animated vocabulary --
 * drifting smoke, the rippling pennant, a motion trail. What carries a still
 * frame is the persistent channels, and they are well populated: sampled a
 * quarter of an hour apart, this world has residue standing in about 94% of
 * stills, a spell in the air in 29%, a ward up in 36%, and a non-idle stance
 * in 20%. The city is never dead, which was the fear.
 */

#if canImport(WidgetKit)

    import Foundation
    import SwiftUI
    import WidgetKit

    public struct CityEntry: TimelineEntry {
        public let date: Date
        public let worldMs: UInt32
        public let image: CGImage?
    }

    public struct CityTimelineProvider: TimelineProvider {
        public let seed: UInt8
        public let layout: Layout
        /// A real wide composition for the one wide widget family. When it is
        /// absent, every family uses `layout` as before.
        public let landscapeLayout: Layout?
        /// How far apart the stills are. A quarter of an hour is what the
        /// system will actually honour for a widget that is not the one being
        /// looked at.
        public let spacing: TimeInterval
        /// How many to hand over at once. Every entry is a rendered frame held
        /// in memory, so this is the trade: a longer timeline is fewer wake-ups
        /// and more resident bytes, 64 kB for a square entry or 96 kB for a
        /// landscape one.
        public let count: Int

        public init(
            seed: UInt8, layout: Layout = .town, landscapeLayout: Layout? = nil,
            spacing: TimeInterval = 15 * 60, count: Int = 24
        ) {
            self.seed = seed
            self.layout = layout
            self.landscapeLayout = landscapeLayout
            self.spacing = spacing
            self.count = count
        }

        private func layout(for context: Context) -> Layout {
            context.family == .systemMedium ? landscapeLayout ?? layout : layout
        }

        /*
         * Where the world's clock comes from.
         *
         * A widget has no session to have been running for, so world time is
         * measured from the start of the current day: the city begins its
         * morning at midnight and a given seed shows the same city to two
         * people looking at the same minute. The alternative -- an epoch
         * persisted in an App Group, so the world runs continuously across
         * days -- is a different and equally defensible answer, and it is the
         * only line here that has to change to get it.
         */
        private func anchor(for date: Date) -> Date {
            Calendar.current.startOfDay(for: date)
        }

        public func placeholder(in context: Context) -> CityEntry {
            CityEntry(date: Date(), worldMs: 0, image: nil)
        }

        public func getSnapshot(in context: Context, completion: @escaping (CityEntry) -> Void) {
            completion(
                entries(from: Date(), count: 1, layout: layout(for: context)).first
                    ?? placeholder(in: context))
        }

        public func getTimeline(
            in context: Context, completion: @escaping (Timeline<CityEntry>) -> Void
        ) {
            let now = Date()
            let made = entries(from: now, count: count, layout: layout(for: context))
            completion(.init(entries: made, policy: .after(made.last?.date ?? now)))
        }

        /// Every entry in ONE forward pass.
        ///
        /// Seeking independently from zero for each of N entries multiplies a
        /// replay that costs about 22 ms an hour by N, inside an extension
        /// that will kill you for it. `City.timeline` advances continuously
        /// and snapshots at each boundary instead, so a day costs one day's
        /// ticks rather than ninety-six partial replays of one.
        private func entries(from date: Date, count: Int, layout: Layout) -> [CityEntry] {
            let anchor = anchor(for: date)
            let step = UInt32(City.frameIntervalMs)
            let elapsed = UInt32(max(0, date.timeIntervalSince(anchor)) * 1000)
            let start = (elapsed / step) * step
            guard let city = try? City(seed: seed, layout: layout) else { return [] }
            /* The first entry is now, and the rest follow at the spacing, so
             * the widget shows the current world rather than one starting a
             * quarter of an hour from now. */
            let boundaries = (0..<max(count, 1)).map { index -> UInt32 in
                let offset = UInt32(Double(index) * spacing * 1000)
                return start + (offset / step) * step
            }
            guard let frames = try? city.timeline(at: boundaries) else { return [] }
            return frames.map { frame in
                CityEntry(
                    date: anchor.addingTimeInterval(Double(frame.worldMs) / 1000),
                    worldMs: frame.worldMs, image: frame.image)
            }
        }
    }

    /// One entry, drawn. Whole pixels, and the monochrome treatment a Lock
    /// Screen widget applies is native here rather than something to fight:
    /// the renderer only ever emits 0 and 255.
    public struct CityEntryView: View {
        public let entry: CityEntry

        public init(entry: CityEntry) { self.entry = entry }

        public var body: some View {
            ZStack {
                Color.black
                if let image = entry.image {
                    Image(decorative: image, scale: 1)
                        .interpolation(.none)
                        .resizable()
                        .aspectRatio(contentMode: .fit)
                }
            }
        }
    }

#endif
