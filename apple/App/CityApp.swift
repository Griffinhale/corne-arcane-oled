/*
 * The app target's entry point.
 *
 * Everything else is in CityKit, which builds on Linux as well and is what the
 * parity leg checks. This file exists because @main has to live in the target
 * Xcode builds; the device-only charging presentation sits beside it.
 */

import CityKit
import SwiftUI

@main
struct CityApp: App {
    /* The seed is the session: one byte, the same one the firmware carries and
     * the same one a share link puts in a URL. */
    private static let seed: UInt8 = 0x5A

    var body: some Scene {
        WindowGroup {
            if let town = try? CityDriver(seed: Self.seed, layout: .town),
                let landscape = try? CityDriver(seed: Self.seed, layout: .landscape)
            {
                ChargingCityView(town: town, landscape: landscape)
            } else {
                Text("the renderer refused this state")
            }
        }
    }
}
