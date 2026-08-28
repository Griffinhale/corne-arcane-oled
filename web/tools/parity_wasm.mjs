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
 * files and a cmp of two buffers.
 */
import { createHash } from "node:crypto";
import { readFileSync, mkdirSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

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
const { instance } = await WebAssembly.instantiate(bytes, {});
const api = instance.exports;
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
