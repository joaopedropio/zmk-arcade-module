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
    id, value: "", type: "", className: "", dataset: {}, style: {},
    children: [], hidden: false, spellcheck: false, title: "", href: "", download: "",
    files: null, text: "",
    /* the page empties a container by assigning textContent, so the stub has to
     * drop the children with it - otherwise a redrawn panel keeps every row it
     * has ever had and nothing that counts them can fail */
    set textContent(value) { el.text = String(value); el.children.length = 0; },
    get textContent() { return el.text; },
    classes: new Set(),
    classList: {
      add(c){ el.classes.add(c); }, remove(c){ el.classes.delete(c); },
      contains: (c) => el.classes.has(c),
      toggle(c, on){ const want = on === undefined ? !el.classes.has(c) : !!on;
                     if (want) el.classes.add(c); else el.classes.delete(c); },
    },
    listeners: {},
    addEventListener(name, fn){ (el.listeners[name] ||= []).push(fn); },
    append(...k){ el.children.push(...k); },
    click(){ for (const fn of el.listeners.click || []) fn({ target: el }); },
    remove(){},
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
  documentElement: make("html"),
  getElementById: (id) => { if (!byId.has(id)) byId.set(id, make(id)); return byId.get(id); },
  createElement: make, querySelectorAll: () => [],
};

/*
 * What the page reaches for beyond the DOM.  They are answered rather than
 * left undefined because the page's own guards would otherwise swallow a real
 * mistake: each of these calls sits inside a try/catch, so a typo in the theme
 * switch or the export would look exactly like a browser that has not got the
 * feature, and pass.
 */
const store = new Map();
const localStorage = {
  getItem: (k) => (store.has(k) ? store.get(k) : null),
  setItem: (k, v) => store.set(k, String(v)),
  removeItem: (k) => store.delete(k),
};

const media = { light: false };
const matchMedia = (query) => ({
  matches: /light/.test(query) ? media.light : !media.light,
  addEventListener(){},
});

/* what the page last handed the browser to save */
const saved = { text: null };
class Blob { constructor(parts) { this.text = parts.join(""); } }
/* named apart from the global URL this file uses to find its own fixtures */
const blobUrls = { createObjectURL: (b) => { saved.text = b.text; return "blob:x"; },
                   revokeObjectURL(){} };

/* the other tabs a BroadcastChannel would reach, as a list of listeners */
const channels = [];
class BroadcastChannel {
  constructor(name) { this.name = name; this.onmessage = null; channels.push(this); }
  postMessage(data) {
    for (const other of channels) {
      if (other !== this && other.onmessage) other.onmessage({ data });
    }
  }
}

const dialogs = { confirm: true, prompt: () => "" };

const serial = { asked: 0, addEventListener(){}, requestPort() {
  serial.asked++;
  throw Object.assign(new Error("no port here"), { name: "NotFoundError" });
} };

const ctx = {
  document, console, setTimeout, setInterval, clearInterval, Math, JSON, Date, Number,
  String, Object, Array, Set, Map, RegExp, Promise, Error, parseInt, isNaN, performance,
  navigator: { serial }, window: { prompt: (q, v) => dialogs.prompt(q, v) }, PacmanPreview,
  confirm: () => dialogs.confirm,
  localStorage, matchMedia, Blob, URL: blobUrls, BroadcastChannel,
  TextEncoderStream: class {}, TextDecoderStream: class {}, MouseEvent: class {},
};
vm.createContext(ctx);
vm.runInContext('"use strict";' + src, ctx);
await new Promise(r => setTimeout(r, 400));            // let the module load

// replay what the dongle really sent
const raw = fs.readFileSync(new URL("schema.txt", import.meta.url), "latin1");

/*
 * Enough of a dongle to answer back.  The schema is the real thing, off real
 * hardware; the profile side is modelled here because those replies are a wire
 * format of their own - a page that misreads them, or that writes an import in
 * the wrong order, would otherwise only be found out with a dongle in hand.
 */
const dongle = {
  profiles: [{ name: "Desk", values: { "game-wall": "2121de", "theme": "0" } },
             null, null, null, null],
  staged: null,
  knowsProfiles: true,
  written: [],
};

const term = (lines) => "\r\n" + lines.join("\r\n") + "\r\nuart:~$ ";

/* the shell's own quoting: "a name like this" arrives as one argument */
const words = (line) => (line.match(/"[^"]*"|\S+/g) || []).map((w) => w.replace(/^"|"$/g, ""));

function profileCommand(argv) {
  const slot = Number(argv[3]);
  const held = dongle.profiles[slot];

  switch (argv[2]) {
  case "list":
    return term(dongle.profiles
      .map((p, i) => `${i}\t${p ? p.name : "-"}\t${p ? Object.keys(p.values).length : 0}`)
      .concat("end"));
  case "show":
    if (!held) return term([`profile ${slot} is empty`]);
    return term(Object.entries(held.values).map(([k, v]) => `${k}\t${v}`).concat("end"));
  case "save":
    dongle.profiles[slot] = { name: argv[4], values: { theme: "0" } };
    return term([`saved 86 settings to profile ${slot} as "${argv[4]}"`]);
  case "load":
    if (!held) return term([`profile ${slot} is empty`]);
    return term([`loaded profile ${slot}; 2 settings moved`]);
  case "rename":
    if (!held) return term([`profile ${slot} is empty`]);
    held.name = argv[4];
    return term([`renamed profile ${slot} to "${argv[4]}"`]);
  case "delete":
    dongle.profiles[slot] = null;
    return term([`deleted profile ${slot}`]);
  case "stage":
    if (argv.length === 3) { dongle.staged = null; return term(["cleared what was staged"]); }
    dongle.staged = dongle.staged || {};
    dongle.staged[argv[3]] = argv[4];
    return term([`staged ${argv[3]} ${argv[4]}`]);
  case "commit":
    if (!dongle.staged) return term(["nothing has been staged to write"]);
    dongle.profiles[slot] = { name: argv[4], values: dongle.staged };
    dongle.staged = null;
    return term([`saved profile ${slot} as "${argv[4]}"`]);
  default:
    return term([`${argv[2]}: wrong parameter count`]);
  }
}

ctx.dongleSays = (line) => {
  if (line.startsWith("pacman schema")) return raw;
  const argv = words(line.trim());
  if (argv[0] === "pacman" && argv[1] === "profile") {
    return dongle.knowsProfiles ? profileCommand(argv) : term(["profile: command not found"]);
  }
  if (argv[0] === "pacman" && argv[1] === "set") {
    dongle.written.push([argv[2], argv[3]]);
    return term([`${argv[2]} is now ${argv[3]}`]);
  }
  return term([]);
};

vm.runInContext(`
  prompt = "uart:~$";
  writer = { write: async (cmd) => { incoming = dongleSays(cmd); } };
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
  /* a tab carrying a view is about every setting at once and holds none itself */
  const want = SECTIONS.filter((x) => !x.view)
    .flatMap((x) => x === SECTIONS[0] ? SCREENS.map((y) => "Screen/" + y.name) : [x.name]);
  return { counts, ambiguous, empty: want.filter((k) => !counts[k]) };
})()`, ctx);
check(partition.ambiguous.length === 0, `nothing matches two places${partition.ambiguous.length ? ": " + partition.ambiguous : ""}`);
check(partition.empty.length === 0, `no empty section${partition.empty.length ? ": " + partition.empty : ""}`);
check(Object.values(partition.counts).reduce((a, b) => a + b, 0) === n,
      `all ${n} settings reachable: ` + JSON.stringify(partition.counts));

/*
 * The fixture is bytes a dongle sent, which means it is as old as the dongle
 * that sent them - so a setting added since is in settings_list.h and not in
 * here, and the partition above cannot see it.  The list itself is therefore
 * read directly, and the two prefixes that have an obvious home are held to
 * it.  That is what a screen-picking regex gets wrong: it names the settings
 * that existed when it was written, and the next one falls through to the
 * catch-all screen without a word.
 */
const listed = [...fs.readFileSync(
    new URL("../../boards/shields/pacman_adapter/widgets/helpers/settings_list.h",
            import.meta.url), "utf8")
  .matchAll(/^\s*X\(\w+,\s*"([^"]+)"/gm)].map((m) => m[1]);

const misfiled = vm.runInContext(`(() => {
  const want = { "game-": "Game", "splash-": "Splash" };
  const wrong = [];
  for (const name of ${JSON.stringify(listed)}) {
    const section = sectionOf(name);
    if (section !== SECTIONS[0]) continue;          /* another section claimed it */
    for (const [prefix, screen] of Object.entries(want)) {
      if (name.startsWith(prefix) && screenOf(name).name !== screen) {
        wrong.push(name + " -> " + screenOf(name).name);
      }
    }
  }
  return wrong;
})()`, ctx);
check(misfiled.length === 0,
      `all ${listed.length} settings in settings_list.h reach the right screen` +
      (misfiled.length ? ": " + misfiled : ""));

const unseen = listed.filter((name) => !vm.runInContext("settings", ctx).some((s) => s.name === name));
if (unseen.length) console.log(`      (schema.txt predates ${unseen.length}: ${unseen.join(", ")})`);

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

    /* Play, Rewind and the screen strip belong to Screen, and Play to the game */
    const shown = (id) => !document.getElementById(id).classList.contains("hidden");
    const pick = SECTIONS[to[0]].panel === "pick";
    if (shown("playbar") !== (pick && to[1] === 0)) wrong.push(where + " got the transport wrong");
    if (shown("screens") !== pick) wrong.push(where + " got the screen strip wrong");

    /* the two-column frame collapses when there is no panel beside the list */
    const want = pick ? to[1] : SECTIONS[to[0]].panel;
    if (shown("previewpanel") !== (want !== null)) wrong.push(where + " got the panel wrong");
    if (shown("previewpanel") === document.getElementById("work").classList.contains("solo")) {
      wrong.push(where + " left the layout and the panel disagreeing");
    }
    if (want === null) continue;

    const got = frame();
    for (const other of Object.keys(ref)) {
      if (Number(other) !== want && same(got, ref[other])) wrong.push(where + " showed another screen");
    }
    if (new Set(got).size < 3) wrong.push(where + " drew a blank frame");
    if (want === 1 && !same(got, ref[1])) wrong.push(where + " drew the splash differently");
  }
  return { wrong, tried: states.length * states.length * 3 };
})()`, ctx);
} catch (err) {
  switching = { wrong: ["the renderer trapped: " + err.message], tried: 0 };
}
check(switching.wrong.length === 0,
      `${switching.tried} navigations drew the right thing` +
      (switching.wrong.length ? ": " + [...new Set(switching.wrong)].slice(0, 3).join("; ") : ""));

/*
 * A layout setting is one the widgets only read as they size themselves, so
 * the page has to build the module again to show it.  Pushing the value at the
 * module it already has changes nothing on screen, which looks exactly like a
 * page that ignored the click.
 */
let layout;
try {
  layout = await vm.runInContext(`(async () => {
  const snap = (m) => { const b = m._preview_framebuffer() >> 1, n = m._preview_panel() ** 2;
    return Array.from(m.HEAPU16.subarray(b, b + n)).join(","); };

  setNav(1);
  const mode = settings.find((s) => s.name === "slot-mode");
  const other = mode.labels.find((l) => l !== mode.value);
  await setValue(mode, other);
  const got = snap(wasm);

  /* what a dongle booting into this layout draws, built with no history */
  const fresh = await PacmanPreview();
  fresh._preview_set_screen(2);
  for (const s of settings) {
    const ptr = fresh.stringToNewUTF8(s.name);
    fresh._preview_set(ptr, asNumber(s));
    fresh._free(ptr);
  }
  fresh._preview_apply_all();
  fresh._preview_render();
  return { to: other, same: got === snap(fresh), blank: new Set(got.split(",")).size < 3 };
})()`, ctx);
} catch (err) {
  layout = { same: false, blank: true, to: "?", err: err.message };
}
check(layout.same && !layout.blank,
      `slot-mode ${layout.to} drew what a dongle booted into it would` +
      (layout.err ? ": " + layout.err : ""));

/*
 * The splash has two styles and they are chosen at boot, so the preview can
 * only show the other one by building again - the same rule the slots follow.
 * The fixture predates the setting, so the module is driven directly: what
 * matters is that preview.js knows the word at all, which it stops doing the
 * moment somebody changes the splash and forgets tools/wasm/build.sh.
 */
const splash = await (async () => {
  const shot = async (style) => {
    const m = await PacmanPreview();
    const ptr = m.stringToNewUTF8("splash-style");
    const known = m._preview_set(ptr, style);
    m._free(ptr);
    m._preview_set_screen(1);
    m._preview_apply_all();
    m._preview_render();
    const b = m._preview_framebuffer() >> 1;
    return { known, frame: Array.from(m.HEAPU16.subarray(b, b + m._preview_panel() ** 2)).join(",") };
  };
  const drawn = await shot(0), image = await shot(1);
  return { known: drawn.known === 1, differ: drawn.frame !== image.frame,
           blank: new Set(image.frame.split(",")).size < 3 };
})();
check(splash.known, "preview.js knows the splash-style setting");
check(splash.differ && !splash.blank,
      `and draws a different splash for each style` +
      (splash.blank ? " (the image came out blank)" : ""));

/* ------------------------------------------------------------------ */
/* the page's own two colour schemes                                   */
/* ------------------------------------------------------------------ */

const style = html.split("<style>")[1].split("</style>")[0];
const palette = (selector) => {
  const block = style.split(selector)[1];
  return block ? new Set((block.split("}")[0].match(/--[a-z-]+(?=\s*:)/g) || [])) : null;
};
const dark = palette(':root, :root[data-theme="dark"] {');
const light = palette(':root[data-theme="light"] {');
check(dark && light && dark.size > 8 && [...dark].every((t) => light.has(t)) &&
      [...light].every((t) => dark.has(t)),
      `both themes define the same ${dark ? dark.size : 0} colours`);

/*
 * A colour written into a rule rather than into the palettes is one that stays
 * dark when the page goes light.  The panel's own black is the exception: it
 * stands for the dongle's screen, which is black whatever this page is.
 */
const strays = style.replace(/:root[^{]*\{[^}]*\}/g, "")
  .match(/#[0-9a-fA-F]{3,8}(?![0-9a-zA-Z_-])/g) || [];
const loose = [...new Set(strays)].filter((c) => c !== "#000");
check(loose.length === 0,
      `no colour outside the palettes${loose.length ? ": " + loose : ""}`);

media.light = false;
vm.runInContext('setTheme("light")', ctx);
const wentLight = document.documentElement.dataset.theme;
vm.runInContext('setTheme("dark")', ctx);
const wentDark = document.documentElement.dataset.theme;
media.light = true;
vm.runInContext('setTheme("auto")', ctx);
const wentAuto = document.documentElement.dataset.theme;
check(wentLight === "light" && wentDark === "dark" && wentAuto === "light",
      `the switch paints light, dark and auto (got ${wentLight}, ${wentDark}, ${wentAuto})`);
check(store.size === 0, "auto remembers nothing, so the browser stays in charge");

/* ------------------------------------------------------------------ */
/* presets, which the page carries and the dongle has never heard of   */
/* ------------------------------------------------------------------ */

const presets = vm.runInContext(`(() => {
  const unknown = [], wrong = [];
  for (const preset of PRESETS) {
    for (const [name, word] of Object.entries(preset.values)) {
      const setting = settings.find((s) => s.name === name);
      if (!setting) { unknown.push(preset.name + "/" + name); continue; }
      if (setting.kind === "color" && !/^[0-9a-f]{6}$/.test(word)) wrong.push(name + "=" + word);
      if (setting.kind === "number" && !(Number(word) >= setting.min && Number(word) <= setting.max)) {
        wrong.push(name + "=" + word);
      }
    }
  }
  return { unknown, wrong, count: PRESETS.length };
})()`, ctx);
check(presets.count === 5, `${presets.count} presets ship with the page`);
check(presets.unknown.length === 0,
      `every preset names settings this firmware has${presets.unknown.length ? ": " + presets.unknown.slice(0, 3) : ""}`);
check(presets.wrong.length === 0,
      `every preset value is one the setting takes${presets.wrong.length ? ": " + presets.wrong.slice(0, 3) : ""}`);

/*
 * Theme 0 draws every colour from its own setting rather than deriving it, so
 * a preset that stops at the maze leaves the dashboard in the colours of the
 * one before it.  Every colour the panel has that is not the maze's has to
 * come out of the preset - by name where it named one, by role otherwise.
 */
const roles = vm.runInContext(`(() => {
  const values = presetValues(PRESETS[1]);
  const want = settings.filter(isDashColor).map((s) => s.name);
  const missing = want.filter((n) => !values[n]);
  const twice = want.filter((n) => DASH_ROLES.filter(([, m]) => m.test(n)).length === 0);
  const counts = {};
  for (const n of want) counts[roleOf(n)] = (counts[roleOf(n)] || 0) + 1;
  return { missing, twice, counts, want: want.length, total: Object.keys(values).length };
})()`, ctx);
check(roles.missing.length === 0 && roles.twice.length === 0,
      `all ${roles.want} panel colours get a role: ` + JSON.stringify(roles.counts));
check(Object.keys(roles.counts).length === 4,
      `every role is used${Object.keys(roles.counts).length === 4 ? "" : ": " + JSON.stringify(roles.counts)}`);

/*
 * Having a role is not the same as having the right one.  Every glyph on the
 * dashboard is drawn as a colour on a background, so a background that lands
 * in a foreground role paints the cell solid and swallows the text - which is
 * what "battery-bg-1" did by ending in a digit and slipping past /-bg$/.
 */
const grounds = vm.runInContext(`(() => {
  const wrong = [];
  for (const s of settings.filter(isDashColor)) {
    const isGround = /(^|-)bg(-[12])?$/.test(s.name);
    if (isGround !== (roleOf(s.name) === "bg")) wrong.push(s.name + " -> " + roleOf(s.name));
  }
  return wrong;
})()`, ctx);
check(grounds.length === 0,
      `every background takes the background colour${grounds.length ? ": " + grounds : ""}`);

dongle.written = [];
await vm.runInContext("applyPreset(PRESETS[1])", ctx);
const applied = new Set(dongle.written.map(([name]) => name));
check(applied.size === roles.total,
      `applying a preset wrote all ${applied.size} of its settings and nothing else`);

/* and the preview shows it: a preset has to move the dashboard, not just the maze */
const moved = vm.runInContext(`(() => {
  const snap = () => { const b = wasm._preview_framebuffer() >> 1;
    return Array.from(wasm.HEAPU16.subarray(b, b + wasm._preview_panel() ** 2)).join(","); };
  setNav(0);
  const seen = {};
  for (const screen of [0, 1, 2]) { setScreen(screen); seen[screen] = snap(); }
  return seen;
})()`, ctx);
await vm.runInContext("applyPreset(PRESETS[3])", ctx);
const after = vm.runInContext(`(() => {
  const snap = () => { const b = wasm._preview_framebuffer() >> 1;
    return Array.from(wasm.HEAPU16.subarray(b, b + wasm._preview_panel() ** 2)).join(","); };
  const seen = {};
  for (const screen of [0, 1, 2]) { setScreen(screen); seen[screen] = snap(); }
  return seen;
})()`, ctx);
const still = [0, 1, 2].filter((screen) => moved[screen] === after[screen]);
check(still.length === 0,
      `the preview repaints all three screens${still.length ? "; unmoved: " + still : ""}`);

/* ------------------------------------------------------------------ */
/* profiles, which live on the dongle                                  */
/* ------------------------------------------------------------------ */

check(vm.runInContext("profilesLive === true", ctx), "the page found `pacman profile`");
const slots = vm.runInContext("JSON.stringify(profiles)", ctx);
check(JSON.parse(slots).length === 5 && JSON.parse(slots)[0].name === "Desk",
      `it read all five slots back: ${slots}`);

vm.runInContext('setNav(SECTIONS.findIndex((s) => s.view === "profiles"))', ctx);
check(document.getElementById("panels").children.length === 2 &&
      document.getElementById("previewpanel").classList.contains("hidden") &&
      document.getElementById("work").classList.contains("solo"),
      "the Profiles tab draws its two lists and no preview beside them");

/*
 * The round trip is the whole point of a file: what one dongle exported has to
 * arrive on another as the same profile.  It also has to arrive without
 * touching the settings that are on the panel right now, which is what the
 * staging is for - so the writes are counted on the way through.
 */
dialogs.prompt = () => "1";
dongle.written = [];
await vm.runInContext("guardAction(() => exportProfile(profiles[0]))", ctx);
const file = saved.text ? JSON.parse(saved.text) : {};
check(file["pacman-dongle-profile"] === 1 && file.name === "Desk" &&
      file.settings && file.settings["game-wall"] === "2121de",
      "an exported profile is a file another dongle could read");

await vm.runInContext(`importProfile(${JSON.stringify(saved.text || "{}")})`, ctx);
check(dongle.profiles[1] && dongle.profiles[1].name === "Desk" &&
      JSON.stringify(dongle.profiles[1].values) === JSON.stringify(dongle.profiles[0].values),
      "importing it puts the same profile in another slot");
check(dongle.written.length === 0,
      `an import leaves the live settings alone (${dongle.written.length} written)`);

await vm.runInContext("guardAction(() => renameProfile(profiles[1]))", ctx);
check(dongle.profiles[1].name === "1", "renaming reaches the dongle");

await vm.runInContext("guardAction(() => deleteProfile(profiles[1]))", ctx);
check(dongle.profiles[1] === null, "deleting frees the slot");

/*
 * A dongle whose firmware predates all this answers the command with a
 * complaint and no `end`.  The tab has to go, the page has to stay up, and
 * whichever tab was open has to stop being the one on screen.
 */
dongle.knowsProfiles = false;
await vm.runInContext("loadSchema()", ctx);
check(vm.runInContext("profilesLive === false && navIndex === 0", ctx) &&
      vm.runInContext("wasm !== null", ctx) &&
      /^Connected\./.test(document.getElementById("status").textContent),
      "an older firmware loses the tab and keeps the connection");
check(!document.getElementById("nav").children.some((b) => b.textContent === "Profiles"),
      "and gets no button for it");

/* ------------------------------------------------------------------ */
/* one connection at a time                                            */
/* ------------------------------------------------------------------ */

check(/already open somewhere else/.test(vm.runInContext(
        'openFailure(Object.assign(new Error("Failed to open serial port."), ' +
        '{ name: "NetworkError" }))', ctx)),
      "a port the browser will not open is blamed on whoever has it");

/* a second tab of this page, already holding a dongle */
const otherTab = new BroadcastChannel("pacman-configurator");
otherTab.onmessage = (event) => {
  if (event.data.what === "who") otherTab.postMessage({ what: "mine", from: "other" });
};
dialogs.confirm = false;
const asked = serial.asked;
await vm.runInContext("connect()", ctx);
check(serial.asked === asked && /another tab/i.test(document.getElementById("status").textContent),
      "a tab that already has the dongle stops the next one before the port picker");

otherTab.onmessage = null;
dialogs.confirm = true;
await vm.runInContext("connect()", ctx);
check(serial.asked === asked + 1 &&
      /No port was picked/.test(document.getElementById("status").textContent),
      "with no other tab it goes on to the picker");

console.log(failures ? `\n${failures} failed` : "\nall checks passed");
process.exit(failures ? 1 : 0);
