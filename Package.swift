// swift-tools-version:5.9
//
// The Apple shell, over the same C the firmware, the desktop window and the
// browser compile.
//
// The C target's sources are the two directories the other two shells read
// through firmware/sim_sources.mk: every native build compiles all of
// firmware/sim, so a directory and that list are the same thing, and neither
// can drift from the other without a file appearing in one and not the other.
//
// No unsafeFlags anywhere. An Xcode project cannot depend on a package that
// uses them, and the app and the widget are meant to depend on this one.

import PackageDescription

let package = Package(
    name: "CorneArcane",
    // The floors the app and the widget stand on. Nothing here reaches for a
    // recent API for its own sake; iOS 17 is what containerBackground and the
    // current widget families need.
    platforms: [.iOS(.v17), .macOS(.v14), .watchOS(.v10)],
    products: [
        .library(name: "CityKit", targets: ["CityKit"]),
    ],
    targets: [
        // The renderer itself. Swift's C interop is native, so there is no
        // FFI layer here and nothing corresponding to the desktop shell's
        // ctypes binding -- duel_city.h is the module.
        .target(
            name: "CCorneArcaneCity",
            path: ".",
            sources: [
                "firmware/sim",
                "desktop/duel_city.c",
                "desktop/duel_ambient.c",
                "desktop/duel_town_draw.c",
            ],
            publicHeadersPath: "apple/include",
            cSettings: [
                .headerSearchPath("firmware/sim"),
                .headerSearchPath("desktop"),
            ]
        ),
        .target(
            name: "CityKit",
            dependencies: ["CCorneArcaneCity"],
            path: "apple/Sources/CityKit"
        ),
        // The third parity leg and the invariant checks, as a program rather
        // than as an XCTest target. The other two legs are programs --
        // parity_native.py and parity_wasm.mjs -- and so is the firmware's own
        // acceptance rig; a leg that needs a test framework to run is a leg
        // that does not run everywhere the shell does.
        .executableTarget(
            name: "city-check",
            dependencies: ["CityKit"],
            path: "apple/Sources/city-check"
        ),
    ]
)
