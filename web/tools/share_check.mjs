/*
 * The share link's own check: arriving at a moment must equal living to it.
 *
 * A link carries a seed and a number of milliseconds, and the recipient's
 * browser replays the world to that point with City.seek(). A browser that has
 * simply been left open reaches the same point through City.advance(), once a
 * frame. Those are two different paths through duel-city.js, and if they ever
 * disagree the link quietly stops being a promise -- the recipient sees a
 * plausible world that is not the one that was sent.
 *
 * So this renders both ways and compares them to each other, and then compares
 * both to the hashes the native library produced in parity_native.py. Three
 * ways of reaching one frame, one answer, no browser required.
 */
import { readFileSync } from "node:fs";
import { createHash } from "node:crypto";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

import { City } from "../duel-city.js";

const here = dirname(fileURLToPath(import.meta.url));
const matrix = JSON.parse(readFileSync(join(here, "parity_matrix.json"), "utf8"));

const out = process.argv[2];
if (!out) {
  console.error("usage: share_check.mjs <parity-out-dir>");
  process.exit(2);
}

/* The native run's per-frame hashes, keyed the way it wrote them. */
const native = new Map();
for (const line of readFileSync(join(out, "native.hashes"), "utf8").split("\n")) {
  const [layout, seed, frame, , digest] = line.split(" ");
  if (frame && frame !== "stats") native.set(`${layout} ${seed} ${frame}`, digest);
}

const bytes = readFileSync(join(here, "..", "duel_city.wasm"));
const digest = (pixels) => createHash("sha256").update(pixels).digest("hex");

/* The last frame of the matrix: far enough in that the world has cast and
 * landed, so agreement is about a lived history and not about frame zero. */
const frame = matrix.frames - 1;
const targetMs = frame * matrix.tick_ms;

let checked = 0;
for (const layout of matrix.layouts) {
  for (const seed of matrix.seeds) {
    /* Left open: advance and draw once a frame, all the way there, which is
     * what the page does. Drawing every frame matters as much as advancing
     * every frame -- the renderer carries the floor-transition policy between
     * calls -- and it is the reason seek() renders a run-up. reset() has
     * already taken the tick at time zero, as the page's first frame does. */
    const live = await City.fromBytes(bytes);
    live.reset(seed);
    let livePixels = live.render(layout, { ambient: true }).pixels.slice();
    for (let t = matrix.tick_ms; t <= targetMs; t += matrix.tick_ms) {
      live.advance(t);
      livePixels = live.render(layout, { ambient: true }).pixels.slice();
    }

    /* Arrived by link: reset, then replay to the same coordinate. */
    const shared = await City.fromBytes(bytes);
    shared.reset(seed);
    shared.seek(targetMs);
    const sharedPixels = shared.render(layout, { ambient: true }).pixels.slice();

    const liveDigest = digest(livePixels);
    const sharedDigest = digest(sharedPixels);
    const expected = native.get(`${layout} ${seed} ${frame}`);

    if (liveDigest !== sharedDigest) {
      console.error(
        `FAIL share: layout ${layout} seed ${seed} -- a link to ${targetMs} ms does not ` +
          `match a tab left open to it`,
      );
      process.exit(1);
    }
    if (expected === undefined) {
      console.error(`FAIL share: no native hash for layout ${layout} seed ${seed} frame ${frame}`);
      process.exit(1);
    }
    if (sharedDigest !== expected) {
      console.error(
        `FAIL share: layout ${layout} seed ${seed} -- the shared frame does not match the ` +
          `native library`,
      );
      process.exit(1);
    }
    /* The world clock must also land where the link said, or the recipient's
     * next share would name a different moment than the one they are seeing. */
    if (shared.worldMs !== targetMs) {
      console.error(`FAIL share: seek(${targetMs}) left the clock at ${shared.worldMs}`);
      process.exit(1);
    }
    checked++;
  }
}

console.log(`PASS share: ${checked} worlds reach ${targetMs} ms identically, linked or left open`);
