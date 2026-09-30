/*
 * Corne Arcane on the two watch surfaces that can carry it.
 *
 * These are source-pixel compositions, not the 256x256 town squeezed into a
 * complication. Rectangular keeps the roof, lit study, balcony wizard and
 * the spell lane around them. Circular tightens to the tower-window motif.
 * Both are rendered by CityKit from the same deterministic world as every
 * other shell, then cropped without resampling.
 */

import CityKit
import SwiftUI
import WidgetKit

private enum WatchCityComposition {
    case rectangular
    case circular

    var layout: CityKit.Layout {
        switch self {
        case .rectangular: .landscape
        case .circular: .town
        }
    }

    var crop: CityPixelRect {
        switch self {
        case .rectangular:
            /* A 2.06:1 strip around the tower's upper duel stage. */
            CityPixelRect(x: 126, y: 38, width: 148, height: 72)
        case .circular:
            /* Roof, lit study and wizard remain one strong silhouette. */
            CityPixelRect(x: 96, y: 42, width: 64, height: 64)
        }
    }

    var accessibilityLabel: String {
        switch self {
        case .rectangular:
            "Corne Arcane city: a wizard duels from a tower balcony"
        case .circular:
            "Corne Arcane: a wizard in the tower window"
        }
    }

    static func forFamily(_ family: WidgetFamily) -> Self {
        family == .accessoryCircular ? .circular : .rectangular
    }
}

private struct WatchCityEntry: TimelineEntry {
    let date: Date
    let worldMs: UInt32
    let image: CGImage?
}

private struct WatchCityProvider: TimelineProvider {
    static let seed: UInt8 = 0x5A
    static let spacing: TimeInterval = 5 * 60
    static let entryCount = 24

    func placeholder(in context: Context) -> WatchCityEntry {
        Self.previewEntry(for: context.family)
    }

    func getSnapshot(in context: Context, completion: @escaping (WatchCityEntry) -> Void) {
        completion(entries(from: Date(), count: 1, family: context.family).first
            ?? Self.previewEntry(for: context.family))
    }

    func getTimeline(
        in context: Context, completion: @escaping (Timeline<WatchCityEntry>) -> Void
    ) {
        let now = Date()
        let entries = entries(from: now, count: Self.entryCount, family: context.family)
        completion(Timeline(entries: entries, policy: .after(entries.last?.date ?? now)))
    }

    private func entries(from date: Date, count: Int, family: WidgetFamily) -> [WatchCityEntry] {
        let composition = WatchCityComposition.forFamily(family)
        /* Match the existing iOS widget exactly: this experiment starts its
         * deterministic seed-0x5A world at local midnight. A shared epoch or
         * App Group is deliberately out of scope. */
        let anchor = Calendar.current.startOfDay(for: date)
        let tick = UInt32(City.frameIntervalMs)
        let elapsed = UInt32(max(0, date.timeIntervalSince(anchor)) * 1000)
        let start = (elapsed / tick) * tick
        let boundaries = (0..<max(count, 1)).map { index -> UInt32 in
            let offset = UInt32(Double(index) * Self.spacing * 1000)
            return start + (offset / tick) * tick
        }

        guard let city = try? City(seed: Self.seed, layout: composition.layout),
            let frames = try? city.timeline(at: boundaries)
        else { return [] }

        return frames.compactMap { frame in
            guard let cropped = frame.cropped(to: composition.crop) else { return nil }
            return WatchCityEntry(
                date: anchor.addingTimeInterval(Double(frame.worldMs) / 1000),
                worldMs: frame.worldMs,
                image: cropped.templateImage)
        }
    }

    static func previewEntry(
        for family: WidgetFamily, worldMs: UInt32 = 7_200_000
    ) -> WatchCityEntry {
        let composition = WatchCityComposition.forFamily(family)
        guard let city = try? City(seed: seed, layout: composition.layout),
            (try? city.seek(to: worldMs)) != nil,
            let pixels = try? city.render(frame: worldMs / City.frameIntervalMs),
            let cropped = CityFrame(
                worldMs: worldMs, frame: worldMs / City.frameIntervalMs,
                pixels: pixels, width: city.width, height: city.height
            ).cropped(to: composition.crop)
        else {
            return WatchCityEntry(
                date: Date(timeIntervalSince1970: Double(worldMs) / 1000),
                worldMs: worldMs, image: nil)
        }
        return WatchCityEntry(
            date: Date(timeIntervalSince1970: Double(worldMs) / 1000),
            worldMs: worldMs, image: cropped.templateImage)
    }
}

private struct WatchCityEntryView: View {
    @Environment(\.widgetFamily) private var environmentFamily
    @Environment(\.widgetRenderingMode) private var renderingMode

    let entry: WatchCityEntry
    private let previewFamily: WidgetFamily?

    init(entry: WatchCityEntry, previewFamily: WidgetFamily? = nil) {
        self.entry = entry
        self.previewFamily = previewFamily
    }

    var body: some View {
        let family = previewFamily ?? environmentFamily
        let composition = WatchCityComposition.forFamily(family)

        Group {
            if let image = entry.image {
                Image(decorative: image, scale: 1)
                    .renderingMode(.template)
                    .interpolation(.none)
                    .resizable()
                    .aspectRatio(contentMode: .fit)
                    .foregroundStyle(ink)
                    .widgetAccentable()
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .clipShape(family == .accessoryCircular ? AnyShape(Circle()) : AnyShape(Rectangle()))
        .accessibilityElement(children: .ignore)
        .accessibilityLabel(composition.accessibilityLabel)
    }

    private var ink: Color {
        /* The watch compositor still owns the final accent-group colours.
         * Choosing accent colour here also makes the mode-specific previews
         * truthful outside that compositor. */
        renderingMode == .accented ? .accentColor : .white
    }
}

@main
struct CorneArcaneWatchWidget: Widget {
    let kind = "town.corne.arcane.city.watch"

    var body: some WidgetConfiguration {
        StaticConfiguration(kind: kind, provider: WatchCityProvider()) { entry in
            WatchCityEntryView(entry: entry)
                .containerBackground(for: .widget) { Color.clear }
        }
        .configurationDisplayName("Corne Arcane")
        .description("A deterministic monochrome wizard duel, composed for Apple Watch.")
        .supportedFamilies([.accessoryRectangular, .accessoryCircular])
        .contentMarginsDisabled()
    }
}

#if DEBUG
    #Preview("Rectangular · full color", as: .accessoryRectangular) {
        CorneArcaneWatchWidget()
    } timeline: {
        WatchCityProvider.previewEntry(for: .accessoryRectangular)
    }

    #Preview("Circular · full color", as: .accessoryCircular) {
        CorneArcaneWatchWidget()
    } timeline: {
        WatchCityProvider.previewEntry(for: .accessoryCircular)
    }

    #Preview("Rectangular · accented") {
        WatchCityEntryView(
            entry: WatchCityProvider.previewEntry(for: .accessoryRectangular),
            previewFamily: .accessoryRectangular)
            .environment(\.widgetRenderingMode, .accented)
            .tint(.purple)
            .frame(width: 150, height: 64)
            .background(.black)
    }

    #Preview("Circular · accented") {
        WatchCityEntryView(
            entry: WatchCityProvider.previewEntry(for: .accessoryCircular),
            previewFamily: .accessoryCircular)
            .environment(\.widgetRenderingMode, .accented)
            .tint(.purple)
            .frame(width: 58, height: 58)
            .background(.black)
    }
#endif
