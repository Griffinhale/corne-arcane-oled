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
    }
}
