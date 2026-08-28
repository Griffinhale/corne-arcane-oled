/*
 * city-page.js -- the shell. A canvas, a clock, and a seed.
 *
 * Everything that is not one of those three is furniture. In particular there
 * is no input path: no listener here reads a key, a pointer or a gamepad into
 * the world, because the world has nowhere to put one. The controls change
 * what is drawn and how fast the page asks for it, never what happens.
 */
import { City, LAYOUT_NAMES, makeImage, paint } from "./duel-city.js";

const el = (id) => document.getElementById(id);
const canvas = el("city");
const context = canvas.getContext("2d", { alpha: false });

/*
 * The address bar is the whole persistence story.
 *
 *   seed    0..255, the world's identity
 *   t       milliseconds into that world
 *   view    which canvas
 *   floor   which stop of the district tour
 *   paused  arrive stopped on that frame rather than running on from it
 *
 * Together those are the arguments of a pure function: the same five give the
 * same pixels on any machine. `floor` is in the list because leaving it out
 * would make the link quietly wrong for anyone who had changed district before
 * copying it.
 *
 * Nothing is stored anywhere else. No cookie, no localStorage, no server: if
 * you did not put a link somewhere, this page remembers nothing about you.
 */
function readAddress() {
  const params = new URLSearchParams(location.search);
  const seed = Number.parseInt(params.get("seed") ?? "", 10);
  const t = Number.parseInt(params.get("t") ?? "", 10);
  const view = Number.parseInt(params.get("view") ?? "", 10);
  const floor = Number.parseInt(params.get("floor") ?? "", 10);
  return {
    seed: Number.isFinite(seed) ? seed & 0xff : (Math.random() * 256) | 0,
    /* Capped at a simulated day. The replay is honest -- every tick actually
     * runs -- so the cap is what stops a hostile link from spending a minute
     * of somebody's CPU before the page appears. */
    startMs: Number.isFinite(t) && t > 0 ? Math.min(t, 24 * 60 * 60 * 1000) : 0,
    layout: Number.isFinite(view) && view >= 0 && view <= 4 ? view : 4,
    /* Range-checked against the tour once the module is loaded, since only it
     * knows how long the tour is. */
    floor: Number.isFinite(floor) && floor >= 0 ? floor : 0,
    paused: params.get("paused") === "1",
  };
}

const address = readAddress();
let layout = address.layout;
let floor = address.floor;
let paused = false;
let image = null;
let geometry = null;
let city = null;

/* The clock. An accumulator against the renderer's own frame interval, not a
 * setInterval: the world's cadence is SIM_TICK_MS and the display's is
 * whatever the monitor does, and the two must not be assumed equal. */
let lastFrameAt = 0;
let accumulator = 0;

function formatElapsed(ms) {
  const total = Math.floor(ms / 1000);
  const minutes = Math.floor(total / 60);
  const seconds = total % 60;
  return `${minutes}:${String(seconds).padStart(2, "0")}`;
}

/* Whole-pixel scaling, using the renderer's own fit rule rather than a second
 * opinion about it. The canvas is always rendered at scale 1 and grown here,
 * so the bytes crossing the boundary stay at 64 kB a frame however large the
 * city is on screen. */
function resize() {
  if (!city || !geometry) return;
  const frame = el("frame");
  const available = frame.getBoundingClientRect();
  const width = Math.max(1, Math.floor(available.width - 48));
  const height = Math.max(1, Math.min(Math.floor(window.innerHeight * 0.62), 640));
  const scale = city.fitScale(layout, width, height);
  canvas.style.width = `${geometry.width * scale}px`;
  canvas.style.height = `${geometry.height * scale}px`;
  el("caption").textContent =
    `${LAYOUT_NAMES[layout]} — ${geometry.width}×${geometry.height} logical pixels, ` +
    `drawn at ${scale}× · ${(geometry.width * geometry.height / 1024).toFixed(0)} kB a frame`;
}

function selectLayout(next) {
  layout = next;
  geometry = city.geometry(layout);
  canvas.width = geometry.width;
  canvas.height = geometry.height;
  image = makeImage(geometry.width, geometry.height);
  el("frame").style.background = city.backdrop(layout);
  resize();
  draw();
}

function draw() {
  const { pixels } = city.render(layout);
  context.putImageData(paint(image, pixels), 0, 0);
}

function showStats() {
  const stats = city.stats();
  el("stat-seed").textContent = String(city.seed);
  el("stat-time").textContent = formatElapsed(city.worldMs);
  el("stat-ticks").textContent = stats.ticks.toLocaleString();
  el("stat-casts").textContent = stats.casts.toLocaleString();
  el("stat-impacts").textContent = stats.impacts.toLocaleString();
  el("stat-knockdowns").textContent = stats.knockdowns.toLocaleString();
}

function loop(now) {
  requestAnimationFrame(loop);
  if (paused) {
    lastFrameAt = now;
    return;
  }
  if (lastFrameAt === 0) lastFrameAt = now;
  accumulator += now - lastFrameAt;
  lastFrameAt = now;

  const interval = city.frameIntervalMs;
  const steps = Math.floor(accumulator / interval);
  if (steps < 1) return;
  accumulator -= steps * interval;

  /*
   * One advance for however many frames elapsed, not one per frame. After a
   * hidden tab this is a large jump, and duel_ambient_advance answers it by
   * resynchronising rather than replaying: the world moves on without having
   * lived the gap. That is the keyboard's behaviour across a USB suspend and
   * it is kept here on purpose.
   */
  city.advance(city.worldMs + steps * interval);
  draw();
  showStats();
}

/* The share link names a position in the world, not a picture of one.
 *
 * `t` comes from the world's own tick count rather than from the wall clock,
 * because those two part company the moment the tab is hidden and the world
 * resynchronises. The tick count is the number that can be replayed exactly,
 * so it is the number worth sending. */
async function share(button) {
  const url = new URL(location.href);
  url.search = new URLSearchParams({
    seed: String(city.seed),
    t: String(city.simulatedMs()),
    view: String(layout),
    floor: String(floor),
  }).toString();
  const label = button.textContent;
  try {
    await navigator.clipboard.writeText(url.toString());
    button.textContent = "copied — this exact world";
  } catch {
    /* Clipboard access can be refused, and a refusal is not a failure worth an
     * apology: put the address in the bar instead, where it can be copied by
     * hand. */
    button.textContent = "in the address bar";
  }
  history.replaceState(null, "", url);
  setTimeout(() => { button.textContent = label; }, 2400);
}

function fail(error) {
  document.querySelector(".stage").innerHTML =
    `<p class="error">The city could not start: ${error.message}</p>`;
  console.error(error);
}

async function main() {
  city = await City.load();
  city.reset(address.seed);

  const district = el("district");
  for (let index = 0; index < city.tourLength; index++) {
    const option = document.createElement("option");
    option.value = String(index);
    option.textContent = `floor ${index + 1} of ${city.tourLength}`;
    district.append(option);
  }

  /* The district is chosen before the replay rather than after it, so that the
   * run-up seek() renders happens with the district the link names. Applied
   * afterwards, the tower would arrive mid-slide from a storey the recipient
   * never saw. */
  floor = floor < city.tourLength ? floor : 0;
  district.value = String(floor);
  if (floor !== 0) city.tourStop(floor);

  /* Replay to the shared position tick by tick. A simulated hour is ~90 000
   * ticks and lands in tens of milliseconds, which is why the link can promise
   * the exact world rather than an approximation of it. */
  if (address.startMs > 0) city.seek(address.startMs);

  el("layout").value = String(layout);
  selectLayout(layout);
  showStats();

  /* `paused=1` arrives stopped on the shared frame instead of running on from
   * it, which is what someone linking to a particular moment usually means. */
  if (address.paused) {
    paused = true;
    el("pause").textContent = "resume";
    el("pause").setAttribute("aria-pressed", "true");
  }

  el("layout").addEventListener("change", (event) => selectLayout(Number(event.target.value)));
  district.addEventListener("change", (event) => {
    floor = Number(event.target.value);
    city.tourStop(floor);
    draw();
  });
  el("pause").addEventListener("click", (event) => {
    paused = !paused;
    event.target.textContent = paused ? "resume" : "pause";
    event.target.setAttribute("aria-pressed", String(paused));
  });
  el("reseed").addEventListener("click", () => {
    city.reset((Math.random() * 256) | 0);
    /* reset() puts the input back to tour stop 0, so the picker has to be put
     * back with it or the two disagree about what is on screen. */
    if (floor !== 0) city.tourStop(floor);
    draw();
    showStats();
  });
  el("share").addEventListener("click", (event) => share(event.target));
  window.addEventListener("resize", resize);

  requestAnimationFrame(loop);
}

main().catch(fail);
