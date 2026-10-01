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

import CCorneArcaneCity
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
    let semantic: [SemanticRow]
}

/// One case that sets host semantics instead of starting from a tour stop.
/// The browser takes no input, so only this leg and the native one run these.
struct SemanticRow: Decodable {
    struct Input: Decodable {
        let scene: UInt8
        let floor: UInt8
        let mode: UInt8
        let intensity: UInt8
        let activity: UInt8
        let count: UInt8
        let category: UInt8
        let priority: UInt8
        let age: UInt8
        let persistent: Bool
        let online: Bool
    }

    /// The off-keyboard signals; a row without them leaves every one at none.
    struct Signals: Decodable {
        let tempo: UInt8
        let spread: UInt8
        let row: UInt8
        let row_spread: UInt8
        let body: UInt8
        let heart: UInt8
        let sleep: UInt8
    }

    let name: String
    let layout: Int32
    let seed: UInt8
    let frames: UInt32
    let input: Input
    let signals: Signals?

    /// Through the public enums, so a number the row names and the enum lacks
    /// fails here rather than reaching the renderer some other way.
    var semantics: CitySemantics {
        guard let scene = HostScene(rawValue: input.scene),
            let floor = CivicFloor(rawValue: input.floor),
            let mode = CivicMode(rawValue: input.mode),
            let intensity = CivicIntensity(rawValue: input.intensity),
            let activity = SecondaryActivity(rawValue: input.activity)
        else { fail("semantic row \(name) names a value CityKit has no case for") }
        var notification: NotificationSummary?
        if input.count > 0 {
            guard let category = NotificationCategory(rawValue: input.category),
                let priority = NotificationPriority(rawValue: input.priority)
            else { fail("semantic row \(name) names a notification CityKit has no case for") }
            notification = NotificationSummary(
                count: input.count, category: category, priority: priority, age: input.age,
                persistent: input.persistent)
        }
        var semantics = CitySemantics(
            scene: scene, floor: floor, mode: mode, intensity: intensity, activity: activity,
            notification: notification, online: input.online)
        if let signals {
            guard let tempo = TypingTempo(rawValue: signals.tempo),
                let spread = TypingSpread(rawValue: signals.spread),
                let row = TypingRow(rawValue: signals.row),
                let rowSpread = TypingRowSpread(rawValue: signals.row_spread),
                let body = BodyActivity(rawValue: signals.body),
                let heart = HeartMood(rawValue: signals.heart),
                let sleep = SleepMood(rawValue: signals.sleep)
            else { fail("semantic row \(name) names a signal CityKit has no case for") }
            semantics.tempo = tempo
            semantics.spread = spread
            semantics.row = row
            semantics.rowSpread = rowSpread
            semantics.body = body
            semantics.heart = heart
            semantics.sleep = sleep
        }
        return semantics
    }
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

/// The semantic rows, in the line format above with the row's name in front.
func semanticLines() -> [String] {
    let matrix = loadMatrix()
    var lines: [String] = []
    for row in matrix.semantic {
        guard let which = Layout(rawValue: row.layout),
            let city = try? City(seed: row.seed, layout: which)
        else { fail("semantic row \(row.name) would not start") }
        do { try city.set(row.semantics) } catch {
            fail("semantic row \(row.name) was refused: \(error)")
        }
        for frame in 0..<row.frames {
            let now = frame * matrix.tick_ms
            city.advance(to: now)
            guard let pixels = try? city.render(frame: frame) else {
                fail("semantic row \(row.name) frame \(frame) would not render")
            }
            lines.append(
                "\(row.name) \(row.layout) \(row.seed) \(frame) \(pixels.count) "
                    + SHA256.hexDigest(pixels))
        }
        let stats = city.stats
        lines.append(
            "\(row.name) \(row.layout) \(row.seed) stats \(stats.ticks) \(stats.casts) "
                + "\(stats.impacts) \(stats.knockdowns)")
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
    let semantic = semanticLines()
    do {
        try (semantic.joined(separator: "\n") + "\n").write(
            to: out.appendingPathComponent("swift-semantic.hashes"), atomically: true,
            encoding: .utf8)
    } catch { fail("cannot write swift-semantic.hashes: \(error)") }
    FileHandle.standardError.write(Data("swift: \(semantic.count) semantic lines\n".utf8))
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

/// A frame of `semantics` in a fresh city, or nil when it would not start,
/// was refused, or would not render.
func frame(_ semantics: CitySemantics, layout: Layout = .left) -> [UInt8]? {
    guard let city = try? City(seed: 0x5A, layout: layout),
        (try? city.set(semantics)) != nil
    else { return nil }
    city.advance(to: 12_000)
    return try? city.render(frame: 300)
}

func runSemanticInvariants() {
    check(
        "host_enums_match_the_wire",
        HostScene.allCases.count == duel_city_wire_constant("SCENE_COUNT")
            && NotificationCategory.allCases.count + 1
                == duel_city_wire_constant("CATEGORY_COUNT")
            && NotificationPriority.allCases.count + 1
                == duel_city_wire_constant("PRIORITY_COUNT")
    )

    let base = CitySemantics()
    var variants: [(String, CitySemantics)] = [("default", base)]
    func vary(_ name: String, _ change: (inout CitySemantics) -> Void) {
        var semantics = base
        change(&semantics)
        variants.append((name, semantics))
    }
    for scene in HostScene.allCases { vary("scene \(scene)") { $0.scene = scene } }
    for floor in CivicFloor.allCases { vary("floor \(floor)") { $0.floor = floor } }
    for mode in CivicMode.allCases { vary("mode \(mode)") { $0.mode = mode } }
    for level in CivicIntensity.allCases { vary("intensity \(level)") { $0.intensity = level } }
    for activity in SecondaryActivity.allCases {
        vary("activity \(activity)") { $0.activity = activity }
    }
    let one = NotificationSummary(count: 1, category: .terminal, priority: .low)
    for count in UInt8(1)...15 {
        vary("count \(count)") {
            $0.notification = NotificationSummary(
                count: count, category: .other, priority: .normal)
        }
    }
    for category in NotificationCategory.allCases {
        vary("category \(category)") {
            $0.notification = NotificationSummary(
                count: 2, category: category, priority: .normal)
        }
    }
    for priority in NotificationPriority.allCases {
        vary("priority \(priority)") {
            $0.notification = NotificationSummary(
                count: 2, category: .calendar, priority: priority)
        }
    }
    for age in UInt8(0)...7 {
        vary("age \(age)") {
            $0.notification = NotificationSummary(
                count: 1, category: .security, priority: .critical, age: age)
        }
    }
    vary("persistent critical") {
        $0.notification = NotificationSummary(
            count: 4, category: .communication, priority: .critical, persistent: true)
    }
    vary("offline") { $0.online = false }
    for tempo in TypingTempo.allCases { vary("tempo \(tempo)") { $0.tempo = tempo } }
    for spread in TypingSpread.allCases { vary("spread \(spread)") { $0.spread = spread } }
    for row in TypingRow.allCases { vary("row \(row)") { $0.row = row } }
    for share in TypingRowSpread.allCases { vary("row spread \(share)") { $0.rowSpread = share } }
    for body in BodyActivity.allCases { vary("body \(body)") { $0.body = body } }
    for heart in HeartMood.allCases { vary("heart \(heart)") { $0.heart = heart } }
    for sleep in SleepMood.allCases { vary("sleep \(sleep)") { $0.sleep = sleep } }
    vary("everything at once") {
        $0 = CitySemantics(
            scene: .focus, floor: .special, mode: .urgent, intensity: .busy, activity: .scroll,
            notification: NotificationSummary(
                count: 3, category: .communication, priority: .critical, age: 2,
                persistent: true))
    }
    let refused = variants.filter { frame($0.1) == nil }.map(\.0)
    check(
        "semantic_input_all_values", refused.isEmpty,
        "refused or unrendered: \(refused.joined(separator: ", "))")
    let drifting = variants.filter { frame($0.1) != frame($0.1) }.map(\.0)
    check(
        "semantic_input_renders_deterministically", drifting.isEmpty,
        "two renders differ: \(drifting.joined(separator: ", "))")
    let floors = Set(
        CivicFloor.allCases.compactMap { floor -> [UInt8]? in
            var semantics = base
            semantics.floor = floor
            return frame(semantics, layout: .town)
        })
    check(
        "semantic_input_reaches_the_picture", floors.count == CivicFloor.allCases.count,
        "\(floors.count) distinct frames for \(CivicFloor.allCases.count) floors")

    check(
        "signal_enums_match_the_header",
        TypingTempo.allCases.count == Int(DUEL_CITY_TEMPO_COUNT)
            && TypingSpread.allCases.count == Int(DUEL_CITY_SPREAD_COUNT)
            && TypingRow.allCases.count == Int(DUEL_CITY_ROW_COUNT)
            && TypingRowSpread.allCases.count == Int(DUEL_CITY_ROW_SPREAD_COUNT)
            && BodyActivity.allCases.count == Int(DUEL_CITY_BODY_COUNT)
            && HeartMood.allCases.count == Int(DUEL_CITY_HEART_COUNT)
            && SleepMood.allCases.count == Int(DUEL_CITY_SLEEP_COUNT)
    )
    // The town draws every typing value, and the panels, which are the
    // keyboard's own screens, draw none. Health has no art yet, so it must
    // render the frame a city without it renders.
    let plain = frame(base, layout: .town)
    let typed = variants.filter {
        $0.1.tempo != .none || $0.1.spread != .none || $0.1.row != .none
            || $0.1.rowSpread != .none
    }
    let unseen = typed.filter { frame($0.1, layout: .town) == plain }.map(\.0)
    let panelled = typed.filter { frame($0.1) != frame(base) }.map(\.0)
    check(
        "typing_signals_draw", plain != nil && unseen.isEmpty && panelled.isEmpty,
        "town unchanged: \(unseen.joined(separator: ", ")); "
            + "panel moved: \(panelled.joined(separator: ", "))")
    let health = variants.filter {
        $0.1.body != .none || $0.1.heart != .none || $0.1.sleep != .none
    }
    let moved = health.filter { frame($0.1, layout: .town) != plain }.map(\.0)
    check(
        "health_signals_change_no_frame_yet", plain != nil && moved.isEmpty,
        "frames moved: \(moved.joined(separator: ", "))")
    // One past each signal enum, written into the C struct directly since the
    // Swift enums cannot hold it; the C check refuses it.
    var unchecked: [String] = []
    let past: [(String, WritableKeyPath<duel_city_input_t, UInt8>, Int)] = [
        ("tempo", \.tempo, Int(DUEL_CITY_TEMPO_COUNT)),
        ("spread", \.spread, Int(DUEL_CITY_SPREAD_COUNT)),
        ("row", \.row, Int(DUEL_CITY_ROW_COUNT)),
        ("row_spread", \.row_spread, Int(DUEL_CITY_ROW_SPREAD_COUNT)),
        ("body", \.body, Int(DUEL_CITY_BODY_COUNT)),
        ("heart", \.heart, Int(DUEL_CITY_HEART_COUNT)),
        ("sleep", \.sleep, Int(DUEL_CITY_SLEEP_COUNT)),
    ]
    var width: Int32 = 0
    var height: Int32 = 0
    _ = duel_city_geometry(Layout.left.rawValue, 1, &width, &height)
    var scratch = [UInt8](repeating: 0, count: Int(width * height))
    for (name, field, count) in past {
        var raw = duel_city_input_t()
        _ = duel_city_tour_stop(0, 0x5A, &raw)
        raw[keyPath: field] = UInt8(count)
        let code = scratch.withUnsafeMutableBufferPointer { buffer in
            duel_city_render(
                nil, &raw, nil, 0, 0, Layout.left.rawValue, 1, buffer.baseAddress, buffer.count)
        }
        if code != Int32(DUEL_CITY_ERR_INPUT) { unchecked.append("\(name) \(count): \(code)") }
    }
    check(
        "signals_past_their_enum_are_refused", unchecked.isEmpty,
        unchecked.joined(separator: "; "))

    // Values the enums can carry but the firmware refuses; the C check is the judge.
    let wrong: [(String, NotificationSummary)] = [
        (
            "count 0 with a category",
            NotificationSummary(count: 0, category: .system, priority: .low)
        ),
        ("count 16", NotificationSummary(count: 16, category: .system, priority: .low)),
        ("age 8", NotificationSummary(count: 1, category: .system, priority: .low, age: 8)),
        (
            "persistent below critical",
            NotificationSummary(
                count: 1, category: .system, priority: .normal, persistent: true)
        ),
    ]
    guard let city = try? City(seed: 0x5A, layout: .left) else { fail("the city would not start") }
    try? city.set(CitySemantics(notification: one))
    city.advance(to: 12_000)
    let before = try? city.render(frame: 300)
    var answers: [String] = []
    for (name, notification) in wrong {
        do {
            try city.set(CitySemantics(floor: .workshop, notification: notification))
            answers.append("\(name): accepted")
        } catch let error as CityError where error.code == Int32(DUEL_CITY_ERR_INPUT) {
        } catch {
            answers.append("\(name): \(error)")
        }
    }
    check(
        "out_of_range_input_returns_the_c_error", answers.isEmpty,
        answers.joined(separator: "; "))
    check(
        "a_refused_input_keeps_the_last_good_one",
        before != nil
            && (try? city.render(frame: 300)) == before)
}

func runInvariants() {
    check(
        "abi_is_the_one_this_tree_compiles", City.abi == expectedCityABI,
        "reported \(City.abi), CityKit expects \(expectedCityABI)")
    check(
        "layouts_come_from_the_header",
        Layout.allCases.map(\.rawValue) == Array(0..<Int32(DUEL_CITY_LAYOUT_COUNT)))
    check("cadence_comes_from_the_simulation", City.frameIntervalMs == 40)
    check("the_tour_is_every_civic_floor", City.tourLength == 5)
    runSemanticInvariants()

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
