/*
 * A portrait, source-pixel window onto the self-playing city.
 *
 * The crop is the 40 mm display's 162x197 logical-point shape in renderer
 * pixels. It therefore fills the design target without stretching, smoothing,
 * or turning the 256x256 town into a letterboxed miniature. GeometryReader
 * keeps the same composition responsive on other watch sizes.
 */

import CoreGraphics
import SwiftUI

@MainActor
struct WatchCityView: View {
    @Environment(\.scenePhase) private var scenePhase
    @Environment(\.isLuminanceReduced) private var isLuminanceReduced
    @State private var driver = WatchCityDriver()

    var body: some View {
        WatchCitySurface(image: driver.image)
            .onAppear { updatePlayback() }
            .onDisappear { driver.stop() }
            .onChange(of: scenePhase) { _, _ in updatePlayback() }
            .onChange(of: isLuminanceReduced) { _, _ in updatePlayback() }
    }

    private func updatePlayback() {
        if scenePhase == .active, !isLuminanceReduced {
            driver.activate()
        } else {
            driver.pause()
        }
    }
}

private struct WatchCitySurface: View {
    let image: CGImage?

    var body: some View {
        GeometryReader { geometry in
            ZStack {
                Color.black

                if let image {
                    Image(decorative: image, scale: 1)
                        .resizable()
                        .interpolation(.none)
                        .aspectRatio(contentMode: .fill)
                        .frame(width: geometry.size.width, height: geometry.size.height)
                        .clipped()
                }
            }
            .frame(width: geometry.size.width, height: geometry.size.height)
        }
        .background(Color.black)
        .ignoresSafeArea()
        .accessibilityElement(children: .ignore)
        .accessibilityLabel("Corne Arcane: a monochrome wizard duel above a city")
        .accessibilityAddTraits(.isImage)
    }
}

#if DEBUG
    #Preview("40 mm · active") {
        WatchCitySurface(image: WatchCityDriver.previewImage())
            .frame(width: 162, height: 197)
    }

    #Preview("40 mm · frozen reduced-luminance still") {
        WatchCitySurface(image: WatchCityDriver.previewImage(worldMs: 7_202_400))
            .frame(width: 162, height: 197)
            .environment(\.isLuminanceReduced, true)
    }

    #Preview("44 mm · responsive") {
        WatchCitySurface(image: WatchCityDriver.previewImage())
            .frame(width: 184, height: 224)
    }
#endif
