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

/*
 * The invariants. Every one of them is a way a shared link stops meaning what
 * it says, which is why they are checks and not comments.
 */
var failures = 0

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
    check("abi_is_the_one_this_tree_compiles", City.abi == 6, "reported \(City.abi)")
    check("cadence_comes_from_the_simulation", City.frameIntervalMs == 40)
    check("the_tour_is_every_civic_floor", City.tourLength == 5)

    let expected: [Layout: (Int, Int)] = [
        .desk: (67, 128), .city: (67, 128), .left: (32, 128), .right: (32, 128),
        .town: (256, 256),
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

    /* Ninety-six entries is a day a quarter of an hour apart, which is what an
     * ambient surface actually asks for, in one pass. */
    guard let ambient = try? City(seed: 0x5A, layout: .town),
        let entries = try? ambient.timeline(everyMs: 15 * 60 * 1000, count: 96)
    else { fail("a day of timeline would not generate") }
    check("a_day_of_entries_is_one_forward_pass", entries.count == 96)
    check("no_two_stills_are_the_same_still", Set(entries.map { $0.pixels }).count == 96)
    check(
        "a_still_frame_is_never_empty",
        entries.allSatisfy { $0.pixels.filter { $0 == 255 }.count > 3000 })

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
default:
    fail("usage: city-check [parity <out-dir> | invariants]")
}
