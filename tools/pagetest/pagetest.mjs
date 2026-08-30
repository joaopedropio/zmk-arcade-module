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

try {
  await vm.runInContext("loadSchema()", ctx);
  const n = vm.runInContext("settings.length", ctx);
  const status = vm.runInContext('document.getElementById("status").textContent', ctx);
  const previewOn = vm.runInContext("wasm !== null", ctx);
  console.log(`loadSchema OK - ${n} settings, preview ${previewOn ? "on" : "off"}`);
  console.log(`status: ${status}`);
  process.exit(n === 86 ? 0 : 1);
} catch (err) {
  console.log("loadSchema THREW:", err.message);
  process.exit(1);
}
