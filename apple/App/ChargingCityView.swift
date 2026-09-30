/*
 * The app's one device-specific presentation.
 *
 * Portrait remains the square town. A phone resting on its side while it has
 * external power becomes an ambient display for the genuine wide city. The
 * ordinary landscape app stays square, so charging is a deliberate mode and
 * not merely an orientation change.
 */

import CityKit
import SwiftUI
import UIKit

@MainActor
private final class DevicePowerState: ObservableObject {
    @Published private(set) var hasExternalPower = false

    private let device: UIDevice
    private var observer: NSObjectProtocol?
    private let forcedForPreview: Bool

    init(
        device: UIDevice? = nil,
        forcedForPreview: Bool = ProcessInfo.processInfo.arguments.contains(
            "--charging-landscape-preview")
    ) {
        self.device = device ?? .current
        self.forcedForPreview = forcedForPreview
        start()
    }

    func start() {
        guard observer == nil else { return }
        device.isBatteryMonitoringEnabled = true
        refresh()
        observer = NotificationCenter.default.addObserver(
            forName: UIDevice.batteryStateDidChangeNotification, object: device, queue: .main
        ) { [weak self] _ in
            Task { @MainActor in self?.refresh() }
        }
    }

    deinit {
        if let observer { NotificationCenter.default.removeObserver(observer) }
    }

    func stop() {
        if let observer { NotificationCenter.default.removeObserver(observer) }
        observer = nil
        device.isBatteryMonitoringEnabled = false
    }

    private func refresh() {
        hasExternalPower = forcedForPreview || device.batteryState == .charging
            || device.batteryState == .full
    }
}

struct ChargingCityView: View {
    @StateObject private var power = DevicePowerState()

    let town: CityDriver
    let landscape: CityDriver

    var body: some View {
        GeometryReader { geometry in
            let chargingLandscape = power.hasExternalPower
                && geometry.size.width > geometry.size.height

            Group {
                if chargingLandscape {
                    CityView(driver: landscape)
                } else {
                    CityView(driver: town)
                }
            }
            .background(.black)
            .ignoresSafeArea()
            .statusBarHidden(chargingLandscape)
            .persistentSystemOverlays(chargingLandscape ? .hidden : .automatic)
            .onChange(of: chargingLandscape, initial: true) { _, active in
                UIApplication.shared.isIdleTimerDisabled = active
            }
        }
        .background(.black)
        .onAppear { power.start() }
        .onDisappear {
            power.stop()
            UIApplication.shared.isIdleTimerDisabled = false
        }
    }
}
