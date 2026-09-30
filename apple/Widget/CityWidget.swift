/*
 * The widget target's whole contents. As with the app: @main has to be here,
 * and nothing else does.
 */

import CityKit
import SwiftUI
import WidgetKit

@main
struct CityWidgetBundle: WidgetBundle {
    var body: some Widget { CityWidget() }
}

struct CityWidget: Widget {
    var body: some WidgetConfiguration {
        StaticConfiguration(
            kind: "town.corne.arcane.city",
            provider: CityTimelineProvider(
                seed: 0x5A, layout: .town, landscapeLayout: .landscape)
        ) { entry in
            CityEntryView(entry: entry)
                .containerBackground(.black, for: .widget)
        }
        .configurationDisplayName("The city")
        .description("A wizard's tower at the centre of a small town, playing itself.")
        /* Square families get the square town; systemMedium gets the genuine
         * 400x240 composition. Neither is stretched or cropped into the
         * other's shape. */
        .supportedFamilies([.systemSmall, .systemMedium, .systemLarge])
        /* The city is the background artwork, not content inset into a card.
         * WidgetKit still owns the outer rounded clipping shape. */
        .contentMarginsDisabled()
    }
}

#if DEBUG
    private func previewEntry(layout: CityKit.Layout, worldMs: UInt32) -> CityEntry {
        guard let city = try? City(seed: 0x5A, layout: layout),
            (try? city.seek(to: worldMs)) != nil,
            let pixels = try? city.render(frame: worldMs / City.frameIntervalMs)
        else {
            return CityEntry(date: Date(), worldMs: worldMs, image: nil)
        }
        return CityEntry(
            date: Date(timeIntervalSince1970: Double(worldMs) / 1000), worldMs: worldMs,
            image: CityFrame(
                worldMs: worldMs, frame: worldMs / City.frameIntervalMs, pixels: pixels,
                width: city.width, height: city.height
            ).image)
    }

    #Preview("Small", as: .systemSmall) {
        CityWidget()
    } timeline: {
        previewEntry(layout: .town, worldMs: 7_200_000)
    }

    #Preview("Medium", as: .systemMedium) {
        CityWidget()
    } timeline: {
        previewEntry(layout: .landscape, worldMs: 7_200_000)
    }

    #Preview("Large", as: .systemLarge) {
        CityWidget()
    } timeline: {
        previewEntry(layout: .town, worldMs: 7_200_000)
    }
#endif
