/*
 * city-check — the Apple shell's own leg of the acceptance harness.
 *
 *     city-check parity <out-dir>   render the matrix, write swift.hashes
 *     city-check invariants         the things a third shell has to hold
 *
 * The determinism claim is the product: a seed in a URL is a promise that
 * your world and mine are the same world, spell for spell. web/tools/parity.sh
 * checks that promise across two renderers. A third shell without a third leg
 * quietly turns "a seed names one world everywhere" into a claim about two of
 * three shells -- and the one most likely to break it is the one furthest
 * from the others, which is this one.
 *
 * So this walks the same web/tools/parity_matrix.json the other two legs
 * walk, in the same order, and emits the same `layout seed frame length
 * digest` lines that parity_native.py and parity_wasm.mjs emit, for the same
 * diff to compare.
 */

import CityKit
import Foundation

#if canImport(CoreGraphics) && canImport(ImageIO)
    import CoreGraphics
    import ImageIO
#endif

struct Matrix: Decodable {
    let layouts: [Int32]
    let seeds: [UInt8]
    let frames: UInt32
    let tick_ms: UInt32
}

/// The repository, found from this file rather than from the working
/// directory, so the leg runs the same from a terminal and from Xcode.
let repoRoot = URL(fileURLWithPath: #filePath)
    .deletingLastPathComponent()  // city-check
    .deletingLastPathComponent()  // Sources
    .deletingLastPathComponent()  // apple
    .deletingLastPathComponent()  // the repository

func fail(_ message: String) -> Never {
    FileHandle.standardError.write(Data("FAIL \(message)\n".utf8))
    exit(1)
}

func loadMatrix() -> Matrix {
    let url = repoRoot.appendingPathComponent("web/tools/parity_matrix.json")
    guard let data = try? Data(contentsOf: url),
        let matrix = try? JSONDecoder().decode(Matrix.self, from: data)
    else { fail("cannot read the parity matrix at \(url.path)") }
    guard matrix.tick_ms == City.frameIntervalMs else {
        fail("the matrix says \(matrix.tick_ms) ms and the renderer says \(City.frameIntervalMs)")
    }
    return matrix
}

func parityLines() -> [String] {
    let matrix = loadMatrix()
    var lines: [String] = []
    for layout in matrix.layouts {
        guard let which = Layout(rawValue: layout) else {
            fail("the matrix names layout \(layout), which this ABI has no name for")
        }
        for seed in matrix.seeds {
            /* A fresh city per case, because the floor policy carries between
             * frames and the other two legs re-init per case too. */
            guard let city = try? City(seed: seed, layout: which) else {
                fail("layout \(layout) seed \(seed) would not start")
            }
            for frame in 0..<matrix.frames {
                let now = frame * matrix.tick_ms
                city.advance(to: now)
                guard let pixels = try? city.render(frame: frame) else {
                    fail("layout \(layout) seed \(seed) frame \(frame) would not render")
                }
                lines.append(
                    "\(layout) \(seed) \(frame) \(pixels.count) \(SHA256.hexDigest(pixels))")
            }
            let stats = city.stats
            lines.append(
                "\(layout) \(seed) stats \(stats.ticks) \(stats.casts) "
                    + "\(stats.impacts) \(stats.knockdowns)")
        }
    }
    return lines
}

func runParity(outDirectory: String) {
    // A known vector first, so a divergence in the run below is read as a
    // divergence in the renderer rather than in this program's arithmetic.
    guard
        SHA256.hexDigest(Array("abc".utf8))
            == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        SHA256.hexDigest([])
            == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
    else { fail("this build's SHA-256 does not agree with the standard's own vectors") }

    let out = URL(fileURLWithPath: outDirectory)
    try? FileManager.default.createDirectory(at: out, withIntermediateDirectories: true)
    let lines = parityLines()
    let text = lines.joined(separator: "\n") + "\n"
    do {
        try text.write(
            to: out.appendingPathComponent("swift.hashes"), atomically: true, encoding: .utf8)
    } catch { fail("cannot write swift.hashes: \(error)") }
    FileHandle.standardError.write(Data("swift: \(lines.count) lines\n".utf8))
}

#if canImport(CoreGraphics) && canImport(ImageIO)
    private func renderedCrop(
        layout: Layout, rect: CityPixelRect, worldMs: UInt32
    ) -> CityFrame {
        guard let city = try? City(seed: 0x5A, layout: layout),
            (try? city.seek(to: worldMs)) != nil,
            let pixels = try? city.render(frame: worldMs / City.frameIntervalMs),
            let cropped = CityFrame(
                worldMs: worldMs, frame: worldMs / City.frameIntervalMs,
                pixels: pixels, width: city.width, height: city.height
            ).cropped(to: rect)
        else { fail("the watch preview crop would not render") }
        return cropped
    }

    private func writeWatchPNG(
        frame: CityFrame, width: Int, height: Int, ink: (UInt8, UInt8, UInt8),
        circular: Bool, to url: URL
    ) {
        var rgba = [UInt8](repeating: 0, count: width * height * 4)
        let scale = min(Double(width) / Double(frame.width), Double(height) / Double(frame.height))
        let drawnWidth = max(1, Int(Double(frame.width) * scale))
        let drawnHeight = max(1, Int(Double(frame.height) * scale))
        let originX = (width - drawnWidth) / 2
        let originY = (height - drawnHeight) / 2
        let radius = Double(min(width, height)) / 2
        let centreX = Double(width - 1) / 2
        let centreY = Double(height - 1) / 2

        for y in 0..<height {
            for x in 0..<width {
                let offset = (y * width + x) * 4
                rgba[offset + 3] = 255
                if circular {
                    let dx = Double(x) - centreX
                    let dy = Double(y) - centreY
                    if dx * dx + dy * dy > radius * radius { continue }
                }
                guard x >= originX, x < originX + drawnWidth,
                    y >= originY, y < originY + drawnHeight
                else { continue }
                let sourceX = (x - originX) * frame.width / drawnWidth
                let sourceY = (y - originY) * frame.height / drawnHeight
                guard frame.pixels[sourceY * frame.width + sourceX] == 255 else { continue }
                rgba[offset] = ink.0
                rgba[offset + 1] = ink.1
                rgba[offset + 2] = ink.2
            }
        }

        let data = Data(rgba)
        guard let provider = CGDataProvider(data: data as CFData),
            let image = CGImage(
                width: width, height: height,
                bitsPerComponent: 8, bitsPerPixel: 32, bytesPerRow: width * 4,
                space: CGColorSpaceCreateDeviceRGB(),
                bitmapInfo: CGBitmapInfo.byteOrder32Big.union(
                    CGBitmapInfo(rawValue: CGImageAlphaInfo.premultipliedLast.rawValue)),
                provider: provider, decode: nil, shouldInterpolate: false,
                intent: .defaultIntent),
            let destination = CGImageDestinationCreateWithURL(
                url as CFURL, "public.png" as CFString, 1, nil)
        else { fail("cannot create watch capture at \(url.path)") }
        CGImageDestinationAddImage(destination, image, nil)
        guard CGImageDestinationFinalize(destination) else {
            fail("cannot write watch capture at \(url.path)")
        }
    }

    func writeWatchCaptures(outDirectory: String) {
        let output = URL(fileURLWithPath: outDirectory)
        do {
            try FileManager.default.createDirectory(
                at: output, withIntermediateDirectories: true)
        } catch { fail("cannot create watch capture directory: \(error)") }

        let worldMs: UInt32 = 7_200_000
        let rectangular = renderedCrop(
            layout: .landscape,
            rect: CityPixelRect(x: 126, y: 38, width: 148, height: 72),
            worldMs: worldMs)
        let circular = renderedCrop(
            layout: .town,
            rect: CityPixelRect(x: 96, y: 42, width: 64, height: 64),
            worldMs: worldMs)
        let white: (UInt8, UInt8, UInt8) = (255, 255, 255)
        let accent: (UInt8, UInt8, UInt8) = (183, 112, 255)

        /* Representative 150x64 and 58x58 point surfaces at @2x. */
        writeWatchPNG(
            frame: rectangular, width: 300, height: 128, ink: white, circular: false,
            to: output.appendingPathComponent("rectangular-full-color-150x64@2x.png"))
        writeWatchPNG(
            frame: rectangular, width: 300, height: 128, ink: accent, circular: false,
            to: output.appendingPathComponent("rectangular-accented-150x64@2x.png"))
        writeWatchPNG(
            frame: circular, width: 116, height: 116, ink: white, circular: true,
            to: output.appendingPathComponent("circular-full-color-58x58@2x.png"))
        writeWatchPNG(
            frame: circular, width: 116, height: 116, ink: accent, circular: true,
            to: output.appendingPathComponent("circular-accented-58x58@2x.png"))
        print("PASS wrote deterministic watch captures to \(output.path)")
    }

    func writeWatchAppCandidates(outDirectory: String) {
        let output = URL(fileURLWithPath: outDirectory)
        do {
            try FileManager.default.createDirectory(
                at: output, withIntermediateDirectories: true)
        } catch { fail("cannot create watch app capture directory: \(error)") }

        let worldMs: UInt32 = 7_200_000
        let candidates: [(String, CityPixelRect)] = [
            /* Exactly one source pixel per 40 mm logical display point. */
            ("point-matched", CityPixelRect(x: 47, y: 13, width: 162, height: 197)),
            /* Slightly tighter alternatives for comparing arm's-length detail. */
            ("balanced", CityPixelRect(x: 52, y: 18, width: 152, height: 185)),
            ("close", CityPixelRect(x: 56, y: 22, width: 144, height: 175)),
        ]

        for (name, rect) in candidates {
            let frame = renderedCrop(layout: .town, rect: rect, worldMs: worldMs)
            writeWatchPNG(
                frame: frame, width: 324, height: 394, ink: (255, 255, 255),
                circular: false,
                to: output.appendingPathComponent("\(name)-40mm@2x.png"))
        }

        guard let city = try? City(seed: 0x5A, layout: .town),
            (try? city.seek(to: worldMs)) != nil
        else { fail("cannot seek the watch app spell sample") }
        let casts = city.stats.casts
        var castMs = worldMs
        let searchEnd = worldMs + 30_000
        while castMs < searchEnd, city.stats.casts == casts {
            castMs += City.frameIntervalMs
            city.advance(to: castMs)
        }
        guard city.stats.casts > casts else {
            fail("cannot find a watch app spell sample within 30 seconds")
        }

        let selected = candidates[0].1
        for offsetMs in stride(from: -800, through: 1_600, by: 400) {
            let sampleMs = UInt32(Int(castMs) + offsetMs)
            let frame = renderedCrop(layout: .town, rect: selected, worldMs: sampleMs)
            writeWatchPNG(
                frame: frame, width: 324, height: 394, ink: (255, 255, 255),
                circular: false,
                to: output.appendingPathComponent(
                    "point-matched-spell-\(offsetMs >= 0 ? "+" : "")\(offsetMs)ms-40mm@2x.png"))
        }
        print("PASS wrote deterministic watch app candidates to \(output.path)")
    }
#endif

/*
 * The invariants. Every one of them is a way a shared link stops meaning what
 * it says, which is why they are checks and not comments.
 */
var failures = 0

private enum ExpectedSeekCancellation: Error {
    case requested
}

func check(_ name: String, _ passed: Bool, _ detail: @autoclosure () -> String = "") {
    if passed {
        print("PASS \(name)")
    } else {
        failures += 1
        let extra = detail()
        print("FAIL \(name)\(extra.isEmpty ? "" : ": \(extra)")")
    }
}

func runInvariants() {
    check("abi_is_the_one_this_tree_compiles", City.abi == 7, "reported \(City.abi)")
    check("cadence_comes_from_the_simulation", City.frameIntervalMs == 40)
    check("the_tour_is_every_civic_floor", City.tourLength == 5)

    let expected: [Layout: (Int, Int)] = [
        .desk: (67, 128), .city: (67, 128), .left: (32, 128), .right: (32, 128),
        .town: (256, 256), .landscape: (400, 240),
    ]
    var geometryHolds = true
    for layout in Layout.allCases {
        guard let size = try? City.geometry(layout), size == expected[layout]! else {
            geometryHolds = false
            continue
        }
    }
    check("geometry_is_the_renderers_to_state", geometryHolds)

    guard let city = try? City(seed: 0x5A, layout: .town) else {
        fail("the town would not start")
    }
    /* advance(0) runs a tick, so world time zero has already lived one. The
     * desktop and the browser agree; a third shell that does not draws a world
     * one tick behind every link it is sent. */
    check("first_tick_is_taken_at_time_zero", city.stats.ticks == 1)

    city.advance(to: 400_000)
    guard let pixels = try? city.render(frame: 12) else { fail("the town would not render") }
    check("a_town_is_actually_drawn", pixels.filter { $0 == 255 }.count > 3000)
    /* Only the two values, which is why a Lock Screen widget's monochrome
     * treatment is native here rather than something to fight. */
    check("the_frame_is_one_bit_in_eight_bit_grey", Set(pixels) == Set([0, 255]))

    let watchRect = CityPixelRect(x: 54, y: 38, width: 148, height: 72)
    guard
        let watchFrame = CityFrame(
            worldMs: city.worldMs, frame: 12, pixels: pixels, width: city.width, height: city.height
        ).cropped(to: watchRect)
    else { fail("the watch crop would not compose") }
    check("a_watch_composition_is_a_source_pixel_crop", watchFrame.pixels.count == 148 * 72)
    check(
        "a_watch_crop_keeps_renderer_pixels",
        Set(watchFrame.pixels).isSubset(of: Set([UInt8(0), UInt8(255)])))
    check(
        "a_watch_crop_rejects_pixels_outside_the_frame",
        watchFrame.cropped(to: CityPixelRect(x: 0, y: 0, width: 149, height: 72)) == nil)

    guard let landscape = try? City(seed: 0x5A, layout: .landscape),
        let widePixels = try? landscape.render(frame: 12)
    else { fail("the landscape would not render") }
    let leftWing = stride(from: 0, to: widePixels.count, by: 400).flatMap {
        widePixels[$0..<($0 + 64)]
    }
    let rightWing = stride(from: 0, to: widePixels.count, by: 400).flatMap {
        widePixels[($0 + 336)..<($0 + 400)]
    }
    check(
        "the_landscape_is_composed_through_both_wings",
        widePixels.count == 400 * 240 && leftWing.filter { $0 == 255 }.count > 300
            && rightWing.filter { $0 == 255 }.count > 300)

    let target: UInt32 = 36_000
    func watched() -> [UInt8] {
        guard let city = try? City(seed: 0x5A, layout: .town) else { fail("no world") }
        var t: UInt32 = 0
        while t < target {
            t += City.frameIntervalMs
            city.advance(to: t)
            _ = try? city.render(frame: t / City.frameIntervalMs)
        }
        return (try? city.render(frame: target / City.frameIntervalMs)) ?? []
    }
    let reference = watched()

    guard let arrived = try? City(seed: 0x5A, layout: .town), (try? arrived.seek(to: target)) != nil
    else { fail("seek would not run") }
    check(
        "arriving_by_link_matches_having_watched_it_in",
        (try? arrived.render(frame: target / City.frameIntervalMs)) == reference)

    /* The watch app performs its local-midnight replay away from the main
     * actor. Backgrounding must be able to stop that replay promptly, and a
     * later request must still land in the one seed-and-time-named world. */
    let cancellationTarget: UInt32 = 80_000
    guard let interrupted = try? City(seed: 0x5A, layout: .town) else {
        fail("the cancellable world would not start")
    }
    var cancellationChecks = 0
    var didCancel = false
    do {
        try interrupted.seek(to: cancellationTarget) {
            cancellationChecks += 1
            if cancellationChecks == 2 { throw ExpectedSeekCancellation.requested }
        }
    } catch ExpectedSeekCancellation.requested {
        didCancel = true
    } catch {
        fail("the cancellable seek threw an unexpected error: \(error)")
    }
    check(
        "a_cold_seek_cooperatively_stops_before_its_target",
        didCancel && interrupted.worldMs > 0 && interrupted.worldMs < cancellationTarget)
    guard (try? interrupted.seek(to: cancellationTarget)) != nil,
        let interruptedPixels = try? interrupted.render(
            frame: cancellationTarget / City.frameIntervalMs),
        let uninterrupted = try? City(seed: 0x5A, layout: .town),
        (try? uninterrupted.seek(to: cancellationTarget)) != nil,
        let uninterruptedPixels = try? uninterrupted.render(
            frame: cancellationTarget / City.frameIntervalMs)
    else { fail("the cancelled seek would not resume") }
    check(
        "a_resumed_seek_is_the_uninterrupted_world",
        interruptedPixels == uninterruptedPixels)

    /* And the negative, so the run-up cannot be quietly dropped. Not every
     * moment diverges without it -- the two policies it settles are not always
     * mid-transition -- but this one does. */
    guard let cold = try? City(seed: 0x5A, layout: .town) else { fail("no world") }
    var t: UInt32 = 0
    while t < target {
        t += City.frameIntervalMs
        cold.advance(to: t)
    }
    check(
        "the_run_up_is_what_makes_that_true",
        (try? cold.render(frame: target / City.frameIntervalMs)) != reference)

    /* Two hundred eighty-eight entries is a day five minutes apart, matching
     * the widget's cadence, in one pass. */
    guard let ambient = try? City(seed: 0x5A, layout: .town),
        let entries = try? ambient.timeline(everyMs: 5 * 60 * 1000, count: 288)
    else { fail("a day of timeline would not generate") }
    check("a_day_of_entries_is_one_forward_pass", entries.count == 288)
    check("no_two_stills_are_the_same_still", Set(entries.map { $0.pixels }).count == 288)
    check(
        "a_still_frame_is_never_empty",
        entries.allSatisfy { $0.pixels.filter { $0 == 255 }.count > 3000 })

    let circularSamples = entries.prefix(24).compactMap {
        $0.cropped(to: CityPixelRect(x: 96, y: 42, width: 64, height: 64))
    }
    check("every_circular_watch_sample_composes", circularSamples.count == 24)
    check(
        "the_circular_motif_stays_legible",
        circularSamples.allSatisfy { $0.pixels.filter { $0 == 255 }.count > 250 })

    guard let watchWide = try? City(seed: 0x5A, layout: .landscape),
        let wideSamples = try? watchWide.timeline(everyMs: 5 * 60 * 1000, count: 24)
    else { fail("the rectangular watch timeline would not generate") }
    let rectangularSamples = wideSamples.compactMap {
        $0.cropped(to: CityPixelRect(x: 126, y: 38, width: 148, height: 72))
    }
    check("every_rectangular_watch_sample_composes", rectangularSamples.count == 24)
    check(
        "the_rectangular_vignette_stays_legible",
        rectangularSamples.allSatisfy { $0.pixels.filter { $0 == 255 }.count > 350 })

    guard let one = try? City(seed: 0x5A, layout: .town),
        (try? one.seek(to: entries[3].worldMs)) != nil
    else { fail("seek would not run") }
    check(
        "an_entry_reached_alone_is_the_entry_the_pass_produced",
        (try? one.render(frame: entries[3].frame)) == entries[3].pixels)
}

let arguments = Array(CommandLine.arguments.dropFirst())
switch arguments.first {
case "parity":
    guard arguments.count == 2 else { fail("usage: city-check parity <out-dir>") }
    runParity(outDirectory: arguments[1])
case "invariants", nil:
    runInvariants()
    if failures > 0 { fail("\(failures) invariant\(failures == 1 ? "" : "s") broken") }
    print("PASS city-check: the third shell holds what the other two hold")
#if canImport(CoreGraphics) && canImport(ImageIO)
    case "watch-captures":
        guard arguments.count == 2 else { fail("usage: city-check watch-captures <out-dir>") }
        writeWatchCaptures(outDirectory: arguments[1])
    case "watch-app-candidates":
        guard arguments.count == 2 else {
            fail("usage: city-check watch-app-candidates <out-dir>")
        }
        writeWatchAppCandidates(outDirectory: arguments[1])
#endif
default:
    fail(
        "usage: city-check [parity <out-dir> | invariants | watch-captures <out-dir> "
            + "| watch-app-candidates <out-dir>]")
}
