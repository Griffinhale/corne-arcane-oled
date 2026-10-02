/*
 * Render the parity matrix with the WASM build.
 *
 * The other side of the acceptance test. It loads duel_city.wasm exactly as
 * the page does -- instantiate, read two pointers, write bounded integers,
 * call render, read pixels -- so what is compared is the path the browser
 * actually takes, not a special test entry point.
 *
 * Writes a hash per frame and the raw pixels of one frame per layout, in the
 * same format as parity_native.py, so the comparison is a plain diff of two
 * files and a cmp of two buffers. The semantic rows follow, written into the
 * module's input struct by offset, in the format of native-semantic.hashes.
 */
import { createHash } from "node:crypto";
import { readFileSync, mkdirSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

import { City, LAYOUT, LAYOUT_NAMES } from "../duel-city.js";

const here = dirname(fileURLToPath(import.meta.url));
const matrix = JSON.parse(readFileSync(join(here, "parity_matrix.json"), "utf8"));

const out = process.argv[2];
if (!out) {
  console.error("usage: parity_wasm.mjs <out-dir>");
  process.exit(2);
}
mkdirSync(out, { recursive: true });

const bytes = readFileSync(join(here, "..", "duel_city.wasm"));
// No imports: the module asks the host for nothing, not even a clock.
// Through the page's own loader first: it refuses a module whose ABI is not
// the one duel-city.js was written for.
await City.fromBytes(bytes);
const { instance } = await WebAssembly.instantiate(bytes, {});
const api = instance.exports;

// duel-city.js restates DUEL_CITY_LAYOUT_* by hand; the module is the judge.
// Every value it names renders, and the next one is DUEL_CITY_ERR_LAYOUT.
const layouts = Object.values(LAYOUT).sort((a, b) => a - b);
layouts.forEach((value, index) => {
  if (value !== index) throw new Error(`LAYOUT is not 0..n-1: ${layouts}`);
  if (api.duel_wasm_geometry(value) < 0) throw new Error(`module rejects layout ${value}`);
  if (LAYOUT_NAMES[value] === undefined) throw new Error(`LAYOUT_NAMES lacks ${value}`);
});
if (api.duel_wasm_geometry(layouts.length) !== -5) {
  throw new Error(`module accepts layout ${layouts.length}, which LAYOUT does not name`);
}
const heap = () => new Uint8Array(api.memory.buffer);

const lines = [];
for (const layout of matrix.layouts) {
  const packed = api.duel_wasm_geometry(layout);
  if (packed < 0) throw new Error(`geometry(${layout}) returned ${packed}`);
  const width = packed >> 16;
  const height = packed & 0xffff;
  const length = width * height;

  for (const seed of matrix.seeds) {
    // Re-init per case: the floor policy carries over between frames, and the
    // native side builds a fresh renderer for each case for the same reason.
    const started = api.duel_wasm_init(seed);
    if (started !== 0) throw new Error(`init(${seed}) returned ${started}`);
    const pixelsPtr = api.duel_wasm_pixels_ptr();

    let pixels = null;
    for (let frame = 0; frame < matrix.frames; frame++) {
      const now = frame * matrix.tick_ms;
      api.duel_wasm_advance(now);
      const code = api.duel_wasm_render(now, frame, layout, 1);
      if (code !== 0) throw new Error(`render(${layout}, ${seed}, ${frame}) returned ${code}`);
      pixels = heap().slice(pixelsPtr, pixelsPtr + length);
      const digest = createHash("sha256").update(pixels).digest("hex");
      lines.push(`${layout} ${seed} ${frame} ${length} ${digest}`);
    }

    api.duel_wasm_stats();
    const stats = new Uint32Array(api.memory.buffer, api.duel_wasm_stats_ptr(), 4);
    lines.push(`${layout} ${seed} stats ${stats[0]} ${stats[1]} ${stats[2]} ${stats[3]}`);
    if (seed === matrix.seeds[0]) {
      writeFileSync(join(out, `wasm-layout${layout}.raw`), pixels);
    }
  }
}

writeFileSync(join(out, "wasm.hashes"), lines.join("\n") + "\n");
console.error(`wasm: ${lines.length} lines`);

/*
 * The semantic rows. The page has no way to set these -- it takes no input --
 * but the module's input struct is in linear memory for the page's loader to
 * reach, so the harness writes the same bounded bytes the daemon's CivicState
 * and CityKit's setter pack. duel_city_render then puts them through the
 * firmware's own acceptance path, which is the point: a row the firmware would
 * reject fails here as it does on the other two legs.
 *
 * The byte order and the two pack macros are duel_city.h's and duel_host.h's,
 * restated. If either moves, this leg disagrees with the native one and the
 * diff says so, which is the same guard the matrix gives the page.
 */
const INPUT_SIZE = 17;
const SIGNAL_ORDER = ["tempo", "spread", "row", "row_spread", "body", "heart", "sleep"];

function semanticBytes(row) {
  const f = row.input;
  const s = row.signals ?? {};
  const civic = (f.floor & 3) | ((f.mode & 3) << 2) | ((f.intensity & 3) << 4);
  const secondary = f.activity & 7;
  return Uint8Array.from([
    f.scene,
    f.count,
    f.category,
    f.priority,
    f.age,
    f.persistent ? 1 : 0,
    civic,
    secondary,
    f.online ? 1 : 0,
    row.seed,
    ...SIGNAL_ORDER.map((name) => s[name] ?? 0),
  ]);
}

const semantic = [];
for (const row of matrix.semantic) {
  const packed = api.duel_wasm_geometry(row.layout);
  if (packed < 0) throw new Error(`geometry(${row.layout}) returned ${packed}`);
  const length = (packed >> 16) * (packed & 0xffff);

  const started = api.duel_wasm_init(row.seed);
  if (started !== 0) throw new Error(`init(${row.seed}) returned ${started}`);
  const bytes = semanticBytes(row);
  if (bytes.length !== INPUT_SIZE) throw new Error(`input is ${bytes.length} bytes, not ${INPUT_SIZE}`);
  heap().set(bytes, api.duel_wasm_input_ptr());
  const pixelsPtr = api.duel_wasm_pixels_ptr();

  for (let frame = 0; frame < row.frames; frame++) {
    const now = frame * matrix.tick_ms;
    api.duel_wasm_advance(now);
    const code = api.duel_wasm_render(now, frame, row.layout, 1);
    if (code !== 0) throw new Error(`semantic ${row.name} frame ${frame}: render returned ${code}`);
    const pixels = heap().slice(pixelsPtr, pixelsPtr + length);
    const digest = createHash("sha256").update(pixels).digest("hex");
    semantic.push(`${row.name} ${row.layout} ${row.seed} ${frame} ${length} ${digest}`);
  }

  api.duel_wasm_stats();
  const stats = new Uint32Array(api.memory.buffer, api.duel_wasm_stats_ptr(), 4);
  semantic.push(`${row.name} ${row.layout} ${row.seed} stats ${stats[0]} ${stats[1]} ${stats[2]} ${stats[3]}`);
}

writeFileSync(join(out, "wasm-semantic.hashes"), semantic.join("\n") + "\n");
console.error(`wasm: ${semantic.length} semantic lines`);
