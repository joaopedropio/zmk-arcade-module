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
    classList: { add(){}, remove(){}, toggle(){}, contains: () => false },
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

/* every setting must land in exactly one tab, or it is unreachable in the UI */
const partition = vm.runInContext(`(() => {
  const counts = {}, ambiguous = [];
  for (const s of settings) {
    const hits = TABS.filter((t) => t.match && t.match(s.name));
    if (hits.length > 1) ambiguous.push(s.name);
    const name = tabOf(s.name).name;
    counts[name] = (counts[name] || 0) + 1;
  }
  return { counts, ambiguous, empty: TABS.filter((t) => !counts[t.name]).map((t) => t.name) };
})()`, ctx);
check(partition.ambiguous.length === 0, `no setting matches two tabs${partition.ambiguous.length ? ": " + partition.ambiguous : ""}`);
check(partition.empty.length === 0, `no empty tabs${partition.empty.length ? ": " + partition.empty : ""}`);
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
  for (const t of TABS) if (t.screen !== null && !(t.screen in ref)) {
    setScreen(t.screen); ref[t.screen] = frame();
  }
  const wrong = [];
  for (let a = 0; a < TABS.length; a++) for (let b = 0; b < TABS.length; b++) for (const rep of [1, 2, 3]) {
    setTab(a);
    for (let i = 0; i < rep; i++) setTab(b);
    const want = TABS[b].screen;
    if (want === null) continue;
    const got = frame();
    const where = TABS[a].name + " -> " + TABS[b].name + " x" + rep;
    for (const other of Object.keys(ref)) {
      if (Number(other) !== want && same(got, ref[other])) {
        wrong.push(where + " showed another screen");
      }
    }
    /* a trapped or half-drawn renderer leaves a flat frame */
    if (new Set(got).size < 3) wrong.push(where + " drew a blank frame");
    /* the splash is deterministic, so it must come back exactly as it went */
    if (want === 1 && !same(got, ref[1])) wrong.push(where + " drew the splash differently");
  }
  return { wrong, tried: TABS.length * TABS.length * 3 };
})()`, ctx);
} catch (err) {
  /* a trapped renderer leaves the last good picture up, which is the bug
     this check exists for - report it rather than dying with a stack */
  switching = { wrong: ["the renderer trapped: " + err.message], tried: 0 };
}
check(switching.wrong.length === 0,
      `${switching.tried} tab switches each drew their own screen` +
      (switching.wrong.length ? ": " + switching.wrong.slice(0, 3).join("; ") : ""));

console.log(failures ? `\n${failures} failed` : "\nall checks passed");
process.exit(failures ? 1 : 0);
