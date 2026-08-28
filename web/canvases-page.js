/*
 * canvases-page.js -- one world, drawn six ways at the same instant.
 *
 * The page exists to make the core/shell split visible in a single image.
 * So the important property of this file is what it does *not* do: it holds
 * one City, advances it once per frame, and then renders it repeatedly with a
 * different layout argument. There is no second world, no second clock, and no
 * per-canvas state. If the six pictures agree, it is because they are one
 * computation.
 */
import { City, makeImage, paint } from "./duel-city.js";

const el = (id) => document.getElementById(id);

/*
 * The watch crop, in town pixels.
 *
 * Chosen by eye against the composition: the full tower including its spire,
 * a house row either side, and the road at the foot. 164x200 is 0.82, which is
 * the aspect of the watch face that prompted the question. It is a crop and
 * not a rescale, so it creates no art -- which is exactly why a viewport is
 * listed as the cheapest thing the core is missing.
 */
const WATCH = { x: 46, y: 22, width: 164, height: 200 };

/*
 * The six framings. `scaleStep` is a multiplier on one shared whole-number
 * step, so every canvas grows by whole pixels together and the row stays
 * aligned: the panels are 128 tall against the town's 256, so they take twice
 * the step and the two end up the same height on screen.
 */
const FRAMINGS = [
  { layout: 4, name: "town", scaleStep: 1, note: "256×256 · its own drawing layer" },
  { key: "watch", name: "watch", scaleStep: 1, note: "164×200 · a crop, taken in JS" },
  { layout: 1, name: "city", scaleStep: 2, note: "67×128 · both, gap unlit" },
  { layout: 0, name: "desk", scaleStep: 2, note: "67×128 · both, desk showing" },
  { layout: 2, name: "left", scaleStep: 2, note: "32×128 · one tower" },
  { layout: 3, name: "right", scaleStep: 2, note: "32×128 · one tower" },
];

let city = null;
let paused = false;
let lastFrameAt = 0;
let accumulator = 0;

function readAddress() {
  const params = new URLSearchParams(location.search);
  const seed = Number.parseInt(params.get("seed") ?? "", 10);
  const t = Number.parseInt(params.get("t") ?? "", 10);
  return {
    seed: Number.isFinite(seed) ? seed & 0xff : (Math.random() * 256) | 0,
    startMs: Number.isFinite(t) && t > 0 ? Math.min(t, 24 * 60 * 60 * 1000) : 0,
    bare: params.get("bare") === "1",
    paused: params.get("paused") === "1",
  };
}

function build() {
  const strip = el("strip");
  for (const framing of FRAMINGS) {
    const size = framing.key === "watch"
      ? { width: WATCH.width, height: WATCH.height }
      : city.geometry(framing.layout);

    const figure = document.createElement("figure");
    figure.className = "framing";

    const canvas = document.createElement("canvas");
    canvas.width = size.width;
    canvas.height = size.height;
    /* The renderer's own backdrop, so the desk framing reads as a desk here
     * exactly as it does in the window. The crop borrows the town's. */
    canvas.style.setProperty(
      "--backdrop",
      city.backdrop(framing.key === "watch" ? 4 : framing.layout),
    );

    const caption = document.createElement("figcaption");
    caption.innerHTML = `<span class="name">${framing.name}</span>${framing.note}`;

    figure.append(canvas, caption);
    strip.append(figure);

    framing.figure = figure;
    framing.canvas = canvas;
    framing.context = canvas.getContext("2d", { alpha: false });
    framing.image = makeImage(size.width, size.height);
    framing.size = size;
  }
}

/*
 * One step for the whole row, so every canvas is a whole-pixel multiple of the
 * same number and the sheet reads as one picture rather than six.
 *
 * The step is chosen by trying the largest and measuring, rather than by
 * predicting it from widths and gaps. Predicting means keeping a copy of the
 * stylesheet's padding in JavaScript and being wrong the first time either
 * changes; measuring asks the question the page actually cares about, which is
 * whether all six ended up on one row. Six layout passes at a resize is
 * nothing next to being subtly wrong about it.
 *
 * Each figure is also pinned to its canvas's width. Without that the captions
 * set the column widths, the row is far wider than the pictures in it, and it
 * wraps into a list -- the one thing this page must not become.
 */
const MAX_STEP = 4;

function applyStep(step) {
  for (const framing of FRAMINGS) {
    const scale = step * framing.scaleStep;
    const width = framing.size.width * scale;
    framing.canvas.style.width = `${width}px`;
    framing.canvas.style.height = `${framing.size.height * scale}px`;
    framing.figure.style.width = `${width}px`;
  }
}

/* The row is aligned on its baseline, so the figures have six different tops
 * and one shared bottom. It is the bottom that says whether they are on the
 * same row. */
function onOneRow() {
  const bottom = Math.round(FRAMINGS[0].figure.getBoundingClientRect().bottom);
  return FRAMINGS.every(
    (framing) => Math.round(framing.figure.getBoundingClientRect().bottom) === bottom,
  );
}

function resize() {
  for (let step = MAX_STEP; step > 1; step--) {
    applyStep(step);
    if (onOneRow()) return;
  }
  /* Nothing fits on one row -- a phone, most likely. Take the smallest step
   * and let flex-wrap make a grid of it, which is a worse diagram but a
   * perfectly good page. */
  applyStep(1);
}

function draw() {
  /* The town is rendered first and kept, because the watch crop reads from it
   * rather than costing a second render of the same pixels. */
  let town = null;
  for (const framing of FRAMINGS) {
    if (framing.key === "watch") continue;
    const { pixels } = city.render(framing.layout);
    if (framing.layout === 4) town = pixels.slice();
    framing.context.putImageData(paint(framing.image, pixels), 0, 0);
  }

  const watch = FRAMINGS.find((f) => f.key === "watch");
  if (watch && town) {
    const cropped = new Uint8Array(WATCH.width * WATCH.height);
    for (let y = 0; y < WATCH.height; y++) {
      const from = (WATCH.y + y) * 256 + WATCH.x;
      cropped.set(town.subarray(from, from + WATCH.width), y * WATCH.width);
    }
    watch.context.putImageData(paint(watch.image, cropped), 0, 0);
  }
}

function showStats() {
  const stats = city.stats();
  el("stat-seed").textContent = String(city.seed);
  const total = Math.floor(city.worldMs / 1000);
  el("stat-time").textContent = `${Math.floor(total / 60)}:${String(total % 60).padStart(2, "0")}`;
  el("stat-ticks").textContent = stats.ticks.toLocaleString();
  el("stat-casts").textContent = stats.casts.toLocaleString();
  el("stat-impacts").textContent = stats.impacts.toLocaleString();
  el("stat-knockdowns").textContent = stats.knockdowns.toLocaleString();
}

function loop(now) {
  requestAnimationFrame(loop);
  if (paused) { lastFrameAt = now; return; }
  if (lastFrameAt === 0) lastFrameAt = now;
  accumulator += now - lastFrameAt;
  lastFrameAt = now;

  const steps = Math.floor(accumulator / city.frameIntervalMs);
  if (steps < 1) return;
  accumulator -= steps * city.frameIntervalMs;

  /* One advance for the whole row. Six renders, one world: this single call is
   * the entire reason the six pictures agree. */
  city.advance(city.worldMs + steps * city.frameIntervalMs);
  draw();
  showStats();
}

async function share(button) {
  const url = new URL(location.href);
  url.search = new URLSearchParams({
    seed: String(city.seed),
    t: String(city.simulatedMs()),
  }).toString();
  const label = button.textContent;
  try {
    await navigator.clipboard.writeText(url.toString());
    button.textContent = "copied — this exact sheet";
  } catch {
    button.textContent = "in the address bar";
  }
  history.replaceState(null, "", url);
  setTimeout(() => { button.textContent = label; }, 2400);
}

async function main() {
  const address = readAddress();
  city = await City.load();
  city.reset(address.seed);
  if (address.startMs > 0) city.seek(address.startMs);

  build();
  resize();
  draw();
  showStats();

  if (address.bare) document.body.classList.add("bare");
  if (address.paused) {
    paused = true;
    el("pause").textContent = "resume";
    el("pause").setAttribute("aria-pressed", "true");
  }

  el("pause").addEventListener("click", (event) => {
    paused = !paused;
    event.target.textContent = paused ? "resume" : "pause";
    event.target.setAttribute("aria-pressed", String(paused));
  });
  el("reseed").addEventListener("click", () => {
    city.reset((Math.random() * 256) | 0);
    draw();
    showStats();
  });
  el("share").addEventListener("click", (event) => share(event.target));
  el("bare").addEventListener("click", (event) => {
    const bare = document.body.classList.toggle("bare");
    event.target.setAttribute("aria-pressed", String(bare));
    event.target.textContent = bare ? "show the rest" : "hide everything else";
    resize();
  });
  window.addEventListener("resize", resize);

  requestAnimationFrame(loop);
}

main().catch((error) => {
  el("strip").innerHTML = `<p class="error">The city could not start: ${error.message}</p>`;
  console.error(error);
});
