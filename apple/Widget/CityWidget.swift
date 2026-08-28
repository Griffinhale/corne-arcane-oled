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
            provider: CityTimelineProvider(seed: 0x5A, layout: .town)
        ) { entry in
            CityEntryView(entry: entry)
                .containerBackground(.black, for: .widget)
        }
        .configurationDisplayName("The city")
        .description("A wizard's tower at the centre of a small town, playing itself.")
        /* The town is square, so the square families are the honest ones.
         * StandBy and systemMedium are landscape and the renderer has no
         * landscape layout: see apple/README.md. */
        .supportedFamilies([.systemSmall, .systemLarge])
    }
}
