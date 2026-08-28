/*
 * The tab icon, cropped out of the world it belongs to.
 *
 * Not a drawing anyone made: it is the top of the wizard tower as the town
 * renderer draws it, at one fixed seed and one fixed tick, taken out of a real
 * frame. That means the icon cannot drift from the art -- if the tower is ever
 * redrawn, this regenerates into whatever the tower now looks like.
 *
 * Emitted as SVG rather than PNG because the art is 1-bit pixels and a browser
 * scaling a 56-pixel bitmap down to 16 makes mush of it. Runs of lit pixels
 * become <rect>s with shape-rendering: crispEdges, so it is exact at every
 * size a browser might ask for.
 *
 *   node web/tools/make_favicon.mjs > web/favicon.svg
 */
import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));

/* The frame the icon is taken from, and the window onto it. Fixed, so that
 * running this twice produces the same file. */
const SEED = 7;
const AT_MS = 12000;
const CROP = { x: 100, y: 28, size: 56 };
const TOWN = 256;

const wasm = readFileSync(join(here, "..", "duel_city.wasm"));
const { instance } = await WebAssembly.instantiate(wasm, {});
const api = instance.exports;
const tick = api.duel_wasm_frame_interval_ms();

api.duel_wasm_init(SEED);
for (let t = tick; t <= AT_MS; t += tick) api.duel_wasm_advance(t);
if (api.duel_wasm_render(AT_MS, AT_MS / tick, 4, 1) !== 0) throw new Error("render failed");
const town = new Uint8Array(api.memory.buffer, api.duel_wasm_pixels_ptr(), TOWN * TOWN);

/* Horizontal runs of lit pixels, so the file is a few hundred rects rather
 * than a few thousand. */
const rects = [];
for (let y = 0; y < CROP.size; y++) {
  let run = -1;
  for (let x = 0; x <= CROP.size; x++) {
    const lit = x < CROP.size && town[(CROP.y + y) * TOWN + (CROP.x + x)] > 127;
    if (lit && run < 0) run = x;
    if (!lit && run >= 0) {
      rects.push(`<rect x="${run}" y="${y}" width="${x - run}" height="1"/>`);
      run = -1;
    }
  }
}

process.stdout.write(
  `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 ${CROP.size} ${CROP.size}" ` +
    `shape-rendering="crispEdges">` +
    `<rect width="${CROP.size}" height="${CROP.size}" fill="#000"/>` +
    `<g fill="#fff">${rects.join("")}</g>` +
    `</svg>\n`,
);
