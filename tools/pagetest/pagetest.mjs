/*
 * Host check for the configurator page.
 *
 * The page is the one part of this that a compiler never sees: a stale call
 * left behind by an edit parses perfectly and only fails when somebody plugs
 * a dongle in.  So the real script is run here against a stub DOM and against
 * schema.txt - bytes a dongle actually sent - which is enough to catch a
 * missing function, a broken parse, or a preview that takes the connection
 * down with it.
 *
 *   node tools/pagetest/pagetest.mjs [path/to/index.html]
 *
 * Refresh the fixture by capturing `pacman schema` from a real dongle; the
 * point of it is that it is not something this repo made up.
 *
 * SPDX-License-Identifier: MIT
 */
import fs from "fs";
import vm from "vm";
import PacmanPreview from "../../docs/configurator/preview.js";

const html = fs.readFileSync(process.argv[2] || new URL("../../docs/configurator/index.html", import.meta.url).pathname, "utf8");
const src = html.split('<script>\n"use strict";')[1].split("\n</script>")[0];

const make = (id = "") => {
  const el = {
    id, textContent: "", value: "", type: "", className: "", dataset: {}, style: {},
    children: [], hidden: false, spellcheck: false,
    classes: new Set(),
    classList: {
      add(c){ el.classes.add(c); }, remove(c){ el.classes.delete(c); },
      contains: (c) => el.classes.has(c),
      toggle(c, on){ const want = on === undefined ? !el.classes.has(c) : !!on;
                     if (want) el.classes.add(c); else el.classes.delete(c); },
    },
    addEventListener(){}, append(...k){ el.children.push(...k); },
    scrollIntoView(){}, getBoundingClientRect: () => ({left:0, top:0, width:300, height:300}),
    querySelector: () => make(), querySelectorAll: () => [],
    closest: () => make(), get offsetWidth(){ return 1; },
    getContext: () => ({
      createImageData: (w,h) => ({ data: new Uint8ClampedArray(w*h*4) }),
      putImageData(){},
    }),
  };
  return el;
};
const byId = new Map();
const document = {
  getElementById: (id) => { if (!byId.has(id)) byId.set(id, make(id)); return byId.get(id); },
  createElement: make, querySelectorAll: () => [],
};

const ctx = {
  document, console, setTimeout, setInterval, clearInterval, Math, JSON, Date, Number,
  String, Object, Array, Set, Map, RegExp, Promise, Error, parseInt, isNaN, performance,
  navigator: {}, window: {}, PacmanPreview, confirm: () => true,
  TextEncoderStream: class {}, TextDecoderStream: class {}, MouseEvent: class {},
};
vm.createContext(ctx);
vm.runInContext('"use strict";' + src, ctx);
await new Promise(r => setTimeout(r, 400));            // let the module load

// replay what the dongle really sent
const raw = fs.readFileSync(new URL("schema.txt", import.meta.url), "latin1");
vm.runInContext(`
  prompt = "uart:~$";
  writer = { write: async (cmd) => {
    if (cmd.startsWith("pacman schema")) incoming = ${JSON.stringify(raw)};
    else incoming = "\\r\\nuart:~$ ";
  }};
`, ctx);

let failures = 0;
const check = (ok, what) => { console.log(`${ok ? "ok  " : "FAIL"}  ${what}`); if (!ok) failures++; };

/*
 * The stub has no CSS, so it cannot see the one way hiding silently fails: a
 * later rule of the same weight setting display on the same element.  That is
 * what let the transport show on every screen once .previewbar was flexed.
 */
check(/\.hidden\s*\{[^}]*display:\s*none\s*!important/.test(html),
      ".hidden outranks the display rules around it");

try {
  await vm.runInContext("loadSchema()", ctx);
} catch (err) {
  console.log("FAIL  loadSchema threw:", err.message);
  process.exit(1);
}

const n = vm.runInContext("settings.length", ctx);
check(n > 0, `loadSchema parsed ${n} settings`);
check(vm.runInContext("wasm !== null", ctx), "the preview survived loading");
check(/^Connected\./.test(vm.runInContext('document.getElementById("status").textContent', ctx)),
      "the page says it connected");

/* every setting must be reachable: one section, and one screen within Screen */
const partition = vm.runInContext(`(() => {
  const counts = {}, ambiguous = [];
  for (const s of settings) {
    const sections = SECTIONS.filter((x) => x.match && x.match(s.name));
    if (sections.length > 1) ambiguous.push(s.name + " (sections)");
    const section = sectionOf(s.name);
    let key = section.name;
    if (section === SECTIONS[0]) {
      const screens = SCREENS.filter((x) => x.match && x.match(s.name));
      if (screens.length > 1) ambiguous.push(s.name + " (screens)");
      key += "/" + screenOf(s.name).name;
    }
    counts[key] = (counts[key] || 0) + 1;
  }
  const want = SECTIONS.flatMap((x) => x === SECTIONS[0] ? SCREENS.map((y) => "Screen/" + y.name) : [x.name]);
  return { counts, ambiguous, empty: want.filter((k) => !counts[k]) };
})()`, ctx);
check(partition.ambiguous.length === 0, `nothing matches two places${partition.ambiguous.length ? ": " + partition.ambiguous : ""}`);
check(partition.empty.length === 0, `no empty section${partition.empty.length ? ": " + partition.empty : ""}`);
check(Object.values(partition.counts).reduce((a, b) => a + b, 0) === n,
      `all ${n} settings reachable: ` + JSON.stringify(partition.counts));

/*
 * Switching away from a screen and back used to trap the renderer - the splash
 * freed its buffers every time the dashboard was drawn - and a trapped module
 * leaves the last good picture on screen, which looks like nothing happening.
 */
let switching;
try {
  switching = vm.runInContext(`(() => {
  const frame = () => { const b = wasm._preview_framebuffer() >> 1;
    return Array.from(wasm.HEAPU16.subarray(b, b + wasm._preview_panel() ** 2)); };
  const same = (a, b) => a.length === b.length && a.every((v, i) => v === b[i]);
  const ref = {};
  for (let i = 0; i < SCREENS.length; i++) { setNav(0); setScreen(i); ref[i] = frame(); }

  const wrong = [];
  const states = [];
  for (let sec = 0; sec < SECTIONS.length; sec++) {
    if (sec === 0) { for (let i = 0; i < SCREENS.length; i++) states.push([0, i]); }
    else states.push([sec, null]);
  }
  for (const from of states) for (const to of states) for (const rep of [1, 2, 3]) {
    setNav(from[0]); if (from[1] !== null) setScreen(from[1]);
    for (let i = 0; i < rep; i++) { setNav(to[0]); if (to[1] !== null) setScreen(to[1]); }
    const where = JSON.stringify(from) + " -> " + JSON.stringify(to) + " x" + rep;

    /* Play and Rewind belong to the game and nowhere else */
    const playShown = !document.getElementById("playbar").classList.contains("hidden");
    const wantPlay = to[0] === 0 && to[1] === 0;
    if (playShown !== wantPlay) wrong.push(where + (wantPlay ? " hid" : " showed") + " the transport");

    /* the two-column frame collapses when there is no panel beside the list */
    const panelUp = !document.getElementById("previewpanel").classList.contains("hidden");
    const solo = document.getElementById("work").classList.contains("solo");
    if (panelUp === solo) wrong.push(where + " left the layout and the panel disagreeing");

    if (to[1] === null) {
      if (panelUp) wrong.push(where + " left the panel up off the Screen section");
      continue;
    }
    const got = frame();
    for (const other of Object.keys(ref)) {
      if (Number(other) !== to[1] && same(got, ref[other])) wrong.push(where + " showed another screen");
    }
    if (new Set(got).size < 3) wrong.push(where + " drew a blank frame");
    if (to[1] === 1 && !same(got, ref[1])) wrong.push(where + " drew the splash differently");
  }
  return { wrong, tried: states.length * states.length * 3 };
})()`, ctx);
} catch (err) {
  switching = { wrong: ["the renderer trapped: " + err.message], tried: 0 };
}
check(switching.wrong.length === 0,
      `${switching.tried} navigations drew the right thing` +
      (switching.wrong.length ? ": " + [...new Set(switching.wrong)].slice(0, 3).join("; ") : ""));

console.log(failures ? `\n${failures} failed` : "\nall checks passed");
process.exit(failures ? 1 : 0);
