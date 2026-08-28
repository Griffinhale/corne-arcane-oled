/*
 * duel-city.js -- loading the core, and the two conversions a browser needs.
 *
 * The whole of the JavaScript side of the boundary. Everything above this file
 * is page furniture; everything below it is the same C the keyboard runs.
 *
 * Three things are deliberately absent, and their absence is the product:
 *
 *   - No imports. The module is instantiated with an empty import object, so
 *     it cannot call out to the page, the network, or a clock. Check it with
 *     WebAssembly.Module.imports(): the list is empty.
 *   - No allocator. Every buffer is a static inside the module; this file
 *     reads them by offset and never asks for memory.
 *   - No input. There is no function here that samples a key, a pointer, or a
 *     gamepad, because there is no function in the module that would accept
 *     one. The world plays itself.
 */

/* The renderer expands to scale internally, which is right for a toolkit that
 * wants a finished bitmap and wrong for a browser: TOWN at scale 2 is 262 kB a
 * frame to produce pixels the compositor produces for nothing. So the browser
 * always renders at scale 1 and upscales in CSS with image-rendering:pixelated.
 * Sharp, at most 94 kB a frame, and no core change to get it. */
export const RENDER_SCALE = 1;

export const LAYOUT = Object.freeze({
  DESK: 0,
  CITY: 1,
  LEFT: 2,
  RIGHT: 3,
  TOWN: 4,
  LANDSCAPE: 5,
});

export const LAYOUT_NAMES = Object.freeze({
  0: "desk",
  1: "city",
  2: "left",
  3: "right",
  4: "town",
  5: "landscape",
});

/* duel_city.h's DUEL_CITY_ERR_*, in the words a page can show. */
const ERRORS = {
  "-1": "null pointer passed to the renderer",
  "-2": "scale outside 1..16",
  "-3": "pixel buffer shorter than the geometry",
  "-4": "an input field is outside its enum or bit width",
  "-5": "layout outside desk/city/left/right/town/landscape",
};

export class CityError extends Error {}

function check(code, what) {
  if (code < 0) throw new CityError(`${what}: ${ERRORS[String(code)] ?? `renderer returned ${code}`}`);
  return code;
}

/*
 * One loaded core. Holds a single world, because the module holds a single
 * world: the C is reentrant and would support several, but one page showing
 * one city needs one, and pretending otherwise would mean an allocator.
 */
export class City {
  constructor(instance) {
    this.exports = instance.exports;
    this.abi = this.exports.duel_wasm_abi_version();
    this.frameIntervalMs = this.exports.duel_wasm_frame_interval_ms();
    /* How much of a run-up seek() renders as well as simulates. Asked rather
     * than chosen: it is the renderer's policy, and a shell that picks its
     * own number arrives at a subtly different frame from every other. */
    this.seekWarmFrames = this.exports.duel_wasm_seek_warm_frames();
    this.tourLength = this.exports.duel_wasm_tour_length();
    this.pixelsPtr = this.exports.duel_wasm_pixels_ptr();
    this.statsPtr = this.exports.duel_wasm_stats_ptr();
    this.seed = 0;
    /* The world's own clock, in milliseconds, always a whole number of ticks.
     * Rendering and the simulation share it, so a position in this world is
     * one number rather than three that have to be kept consistent. */
    this.worldMs = 0;
  }

  static async load(url = "duel_city.wasm") {
    const response = await fetch(url);
    if (!response.ok) throw new CityError(`cannot fetch ${url}: ${response.status}`);
    /* instantiateStreaming where the server sends the right type, and a plain
     * instantiate where it does not -- opening the page from a file:// URL is
     * a reasonable thing to do and should not fail on a MIME header. */
    try {
      const { instance } = await WebAssembly.instantiateStreaming(response.clone(), {});
      return new City(instance);
    } catch {
      return City.fromBytes(await response.arrayBuffer());
    }
  }

  /* Instantiate from bytes already in hand. The page never needs this; the
   * share-link check does, so that it can exercise this file's own seek and
   * advance rather than a second copy of them written for the test. The empty
   * import object is the point: the module asks the host for nothing. */
  static async fromBytes(bytes) {
    const { instance } = await WebAssembly.instantiate(bytes, {});
    return new City(instance);
  }

  /* Pixel size of a layout at scale 1. Packed into one integer by the shim
   * because the two numbers are always wanted together. */
  geometry(layout) {
    const packed = check(this.exports.duel_wasm_geometry(layout), "geometry");
    return { width: packed >> 16, height: packed & 0xffff };
  }

  backdrop(layout) {
    const grey = check(this.exports.duel_wasm_backdrop(layout), "backdrop");
    return `rgb(${grey}, ${grey}, ${grey})`;
  }

  /* The scale to use with nobody naming one, and the largest whole-pixel scale
   * that fits a box this size. Both are asked of the renderer rather than
   * worked out here: they are exactly the decisions ABI 5 moved into C so that
   * a second shell would not reimplement them and drift from the first. */
  defaultScale(layout) {
    return check(this.exports.duel_wasm_default_scale(layout), "default scale");
  }

  fitScale(layout, width, height) {
    return check(this.exports.duel_wasm_fit_scale(layout, width, height), "fit scale");
  }

  /*
   * Start this world over from a seed, at world time zero.
   *
   * The input struct is left at tour stop 0, which the firmware's own
   * acceptance path is guaranteed to accept. A hand-zeroed struct is not
   * obviously valid, and duel_city_render rejects an invalid one outright
   * rather than drawing something approximate.
   */
  reset(seed) {
    this.seed = seed & 0xff;
    check(this.exports.duel_wasm_init(this.seed), "init");
    this.worldMs = 0;
    /* The world takes its first tick at time zero, so world time zero is a
     * world that has lived a tick rather than one that has not started. This
     * is the desktop window's behaviour too, and matching it is what lets the
     * two shells agree that "this seed at this millisecond" names one world. */
    this.exports.duel_wasm_advance(0);
  }

  /* Move the city to one stop of the no-daemon tour: every civic floor once.
   * This is the only thing on the page that changes what the world is shown
   * as doing, and it can only express a bounded enum the firmware accepts. */
  tourStop(index) {
    check(this.exports.duel_wasm_tour_stop(index, this.seed), "tour stop");
  }

  /*
   * Replay the world forward to `targetMs`, tick by tick.
   *
   * This is what makes a seed in a URL a promise rather than a hint. It is
   * deliberately not one long advance(): duel_ambient_advance resynchronises
   * across a gap longer than DUEL_TICK_CATCHUP_MAX ticks, so a single jump
   * would land in a world that had skipped its own history. Stepping runs
   * every tick, so the state at `targetMs` is a pure function of the seed and
   * the number, identical in every browser that opens the link.
   *
   * A full simulated hour is about 90 000 ticks and replays in tens of
   * milliseconds, which is why this can be the honest implementation rather
   * than an approximation.
   *
   * The last stretch is rendered as well as simulated, because the world is
   * not quite the whole state: the renderer carries the floor-transition
   * policy between frames, which is what makes the tower slide between storeys
   * instead of snapping, and the outcome flash, which arms on a sequence
   * number it has never seen before. Arriving without either draws the slide
   * from a standing start -- measured at 29 pixels of 8576 -- and bursts for
   * whatever last happened, however long ago it was. How long the run-up has
   * to be is duel_city_seek_warm_frames, asked once at load rather than
   * decided here, because every shell that seeks needs the same answer.
   * Without this, someone who follows a link sees very slightly different
   * pixels from the person who sent it, which is exactly the promise being
   * made here.
   */
  seek(targetMs) {
    const step = this.frameIntervalMs;
    const warmFrom = Math.max(0, targetMs - this.seekWarmFrames * step);
    for (let t = this.worldMs + step; t <= targetMs; t += step) {
      this.exports.duel_wasm_advance(t);
      this.worldMs = t;
      /* Warmed through the smallest layout, since the policy is composed
       * before any layout-specific drawing and so is the same whichever is
       * asked for -- and this one is 4 kB a frame rather than 64. */
      if (t >= warmFrom) {
        check(
          this.exports.duel_wasm_render(t, t / step, LAYOUT.LEFT, 1),
          "seek warm-up",
        );
      }
    }
  }

  /* Run the world up to the shell's clock. Returns the ticks actually run,
   * which is below the elapsed ticks whenever the tab has been away: the
   * catch-up cap means a long absence resynchronises instead of replaying,
   * and the world visibly jumps. That is the documented behaviour, not a
   * defect, and this shell does not paper over it. */
  advance(nowMs) {
    this.worldMs = nowMs;
    return this.exports.duel_wasm_advance(nowMs);
  }

  /*
   * Render the world as it stands into the module's static buffer, and return
   * a view of it. The view is onto WebAssembly memory, so it is valid until
   * the next call; the page turns it into an ImageData immediately.
   */
  render(layout, { elapsedMs = this.worldMs, frame = Math.floor(this.worldMs / this.frameIntervalMs), ambient = true } = {}) {
    check(this.exports.duel_wasm_render(elapsedMs, frame, layout, ambient ? 1 : 0), "render");
    const { width, height } = this.geometry(layout);
    const pixels = new Uint8Array(this.exports.memory.buffer, this.pixelsPtr, width * height);
    return { pixels, width, height };
  }

  /*
   * Where this world actually is, in its own milliseconds -- which is the
   * number worth putting in a link.
   *
   * It is derived from the tick count rather than from worldMs because those
   * two part company the moment the tab is hidden: the shell's clock jumps to
   * now, the world resynchronises instead of replaying, and worldMs then names
   * a moment this world has not lived. The tick count is what it has lived.
   * The subtraction is the tick at time zero.
   */
  simulatedMs() {
    return Math.max(0, this.stats().ticks - 1) * this.frameIntervalMs;
  }

  /* Evidence the city is alive: ticks, casts, pips of damage, champions
   * felled. The world's own count, not the shell's. */
  stats() {
    this.exports.duel_wasm_stats();
    const view = new Uint32Array(this.exports.memory.buffer, this.statsPtr, 4);
    return { ticks: view[0], casts: view[1], impacts: view[2], knockdowns: view[3] };
  }
}

/*
 * Grey to RGBA, into a reusable ImageData.
 *
 * The renderer's output is one byte a pixel and a canvas wants four, so this
 * is the only per-pixel work the shell does. The ImageData is allocated once
 * and written in place, because allocating 94 kB a frame at 25 Hz is how a
 * page that should idle at nothing instead keeps a collector busy.
 */
export function makeImage(width, height) {
  const image = new ImageData(width, height);
  /* Alpha is opaque for the life of the image; only the three colour bytes
   * are touched per frame. */
  const data = image.data;
  for (let i = 3; i < data.length; i += 4) data[i] = 255;
  return image;
}

export function paint(image, pixels) {
  const data = image.data;
  for (let i = 0, j = 0; i < pixels.length; i++, j += 4) {
    const level = pixels[i];
    data[j] = level;
    data[j + 1] = level;
    data[j + 2] = level;
  }
  return image;
}
