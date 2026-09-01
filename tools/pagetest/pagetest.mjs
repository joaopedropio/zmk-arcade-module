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
 *
 * The dongle is always on a profile, and being on one means the live settings
 * are that profile - so the slot it is on and `live` are the same object here,
 * exactly as `pacman set` and `profile show` present them.  Slot 1 is a
 * profile it is not on, which is the only way to tell the two readings apart.
 */
const live = Object.fromEntries(
  fs.readFileSync(new URL("schema.txt", import.meta.url), "latin1")
    .split(/\r?\n/).filter((line) => line.includes("\t"))
    .map((line) => line.split("\t")).map(([name, , value]) => [name, value]));

const dongle = {
  profiles: [{ name: "Desk", values: live },
             { name: "Night", values: { "game-wall": "2121de", "theme": "0" } },
             null, null, null],
  current: 0,
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
  const here = dongle.profiles[dongle.current];

  switch (argv[2]) {
  case "list":
    return term(dongle.profiles
      .map((p, i) => `${i}\t${p ? p.name : "-"}\t${p ? Object.keys(p.values).length : 0}`)
      .concat("end"));
  case "current":
    return term([`${dongle.current}\t${here.name}`, "end"]);
  case "show":
    if (!held) return term([`profile ${slot} is empty`]);
    return term(Object.entries(held.values).map(([k, v]) => `${k}\t${v}`).concat("end"));
  case "save":
    /* a snapshot of the live settings, and the dongle goes on from the copy */
    dongle.profiles[slot] = { name: argv[4], values: Object.assign({}, here.values) };
    dongle.current = slot;
    return term([`saved 86 settings to profile ${slot} as "${argv[4]}"; the dongle is on it now`]);
  case "load":
    if (!held) return term([`profile ${slot} is empty`]);
    if (slot === dongle.current) {
      return term([`loaded profile ${slot}; the dongle was already on it`]);
    }
    dongle.current = slot;
    return term([`loaded profile ${slot}; 2 settings moved`]);
  case "rename":
    if (!held) return term([`profile ${slot} is empty`]);
    held.name = argv[4];
    return term([`renamed profile ${slot} to "${argv[4]}"`]);
  case "delete":
    if (slot === 0) {
      return term([`profile ${slot} cannot be forgotten; it is the one the dongle falls back to`]);
    }
    if (slot === dongle.current) {
      return term([`profile ${slot} cannot be forgotten; it is the one the dongle is on`]);
    }
    dongle.profiles[slot] = null;
    return term([`deleted profile ${slot}`]);
  case "stage":
    if (argv.length === 3) { dongle.staged = null; return term(["cleared what was staged"]); }
    dongle.staged = dongle.staged || {};
    dongle.staged[argv[3]] = argv[4];
    return term([`staged ${argv[3]} ${argv[4]}`]);
  case "commit":
    if (!dongle.staged) return term(["nothing has been staged to write"]);
    if (slot === dongle.current) {
      return term([`profile ${slot} is the one the dongle is on; write to another slot`]);
    }
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
    dongle.profiles[dongle.current].values[argv[2]] = argv[3];
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
  for (const s of settings.filter((x) => !UNOFFERED.test(x.name))) {
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
const offered = vm.runInContext(
  "settings.filter((s) => !UNOFFERED.test(s.name)).length", ctx);
check(Object.values(partition.counts).reduce((a, b) => a + b, 0) === offered,
      `all ${offered} offered settings reachable: ` + JSON.stringify(partition.counts));

/*
 * The theme number is written by every preset and stored like anything else,
 * but nothing on the page picks it any more: the dongle's button steps between
 * profiles now, so a spinner for it led nowhere.  Checked as rows that were
 * actually rendered, because a setting dropped from the array instead would
 * take the presets' `theme: 0` down with it - and theme 0 is what makes the
 * individual colours count at all.
 */
const themeRows = vm.runInContext(`(() => {
  const seen = [];
  for (let sec = 0; sec < SECTIONS.length; sec++) {
    for (let scr = 0; scr < SCREENS.length; scr++) {
      setNav(sec);
      if (SECTIONS[sec].panel === "pick") setScreen(scr);
      for (const section of document.getElementById("panels").children) {
        for (const row of section.children[1].children) seen.push(row.children[0].textContent);
      }
    }
  }
  return seen.filter((name) => name === "theme");
})()`, ctx);
check(themeRows.length === 0, `no tab offers a theme control (${themeRows.length} found)`);
check(vm.runInContext('settings.some((s) => s.name === "theme")', ctx) &&
      vm.runInContext('PRESETS.every((p) => p.values.theme === "0")', ctx),
      "but the setting is still there for the presets to write");

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

/*
 * Which game the panel plays is a setting like any other, and the fixture
 * predates it as well, so the module is driven directly here too.  The colours
 * come out of a preset rather than being written in this file: a preset that
 * stopped at the maze would leave the others drawing black on black, and this
 * is where that shows up rather than on somebody's desk.
 */
const games = await (async () => {
  const shot = async (which) => {
    const m = await PacmanPreview();
    const values = vm.runInContext("presetValues(PRESETS[0])", ctx);
    for (const [name, word] of Object.entries(values)) {
      const p = m.stringToNewUTF8(name);
      m._preview_set(p, parseInt(word, 16) || 0);
      m._free(p);
    }
    const p = m.stringToNewUTF8("game");
    const known = m._preview_set(p, which);
    m._free(p);
    m._preview_set_screen(0);
    m._preview_apply_all();
    m._preview_reset(1);
    for (let i = 0; i < 400; i++) m._preview_step();
    const b = m._preview_framebuffer() >> 1;
    return { known, frame: Array.from(m.HEAPU16.subarray(b, b + m._preview_panel() ** 2)).join(",") };
  };
  const drawn = [await shot(0), await shot(1), await shot(2)];
  const names = ["the maze", "the shooter", "the brick field"];
  return {
    known: drawn.every((g) => g.known === 1),
    /* every game draws a different panel, and none of them draws a flat one */
    same: drawn.map((g, i) => [i, drawn.findIndex((o) => o.frame === g.frame)])
               .filter(([i, first]) => first !== i)
               .map(([i, first]) => `${names[i]} drew ${names[first]}`),
    colours: drawn.map((g) => new Set(g.frame.split(",")).size),
  };
})();
check(games.known, "preview.js knows all three games");
check(games.same.length === 0 && games.colours.every((n) => n >= 4),
      `and each of them draws its own panel, in ${games.colours.join("/")} colours` +
      (games.same.length ? ` (${games.same[0]})` : ""));

/*
 * A mode only has so many slots, and it drops them from the top: 2-slot is
 * slot5 and slot6, because set_slot_1() gives slot1 SLOT_NUMBER_NONE in every
 * mode but 6-slot. Showing a row for a slot the panel has not got is offering
 * a choice that changes nothing, so the rows are counted as rendered rather
 * than as the predicate that picks them.
 */
const perMode = await vm.runInContext(`(async () => {
  setNav(1);
  const rendered = () => {
    const names = [];
    for (const section of document.getElementById("panels").children) {
      for (const row of section.children[1].children) names.push(row.children[0].textContent);
    }
    return names.filter((n) => /^slot[1-6]$/.test(n));
  };
  const mode = settings.find((s) => s.name === "slot-mode");
  const seen = {};
  for (const word of mode.labels) {
    await setValue(mode, word);
    render();
    seen[word] = rendered().join(",");
  }
  return seen;
})()`, ctx);
const wantPerMode = {
  "2-slot": "slot5,slot6",
  "4-slot": "slot3,slot4,slot5,slot6",
  "5-slot": "slot2,slot3,slot4,slot5,slot6",
  "6-slot": "slot1,slot2,slot3,slot4,slot5,slot6",
};
const modeWrong = Object.keys(wantPerMode).filter((m) => perMode[m] !== wantPerMode[m]);
check(modeWrong.length === 0,
      "each mode shows only the slots it has" +
      (modeWrong.length ? ": " + modeWrong.map((m) => `${m} gave ${perMode[m]}`) : ""));

/*
 * A widget can only be in one slot.  The firmware does not refuse the second
 * one - get_slot_by_name() returns the first slot holding it, so the other is
 * never drawn into - which makes it the page's job to keep that state out of
 * reach.  Choosing a widget that is elsewhere has to move it, not copy it,
 * and the slot it left has to take what the destination was showing.
 */
const oneSlot = await vm.runInContext(`(async () => {
  setNav(1);
  const slot = (n) => settings.find((s) => s.name === "slot" + n);
  const widgets = slot(1).labels.filter((w) => w !== "empty");

  /* set up through setSlot too - the fixture already has a widget in slot5,
     and parking one with setValue would make the duplicate this is about */
  await setSlot(slot(1), widgets[0]);
  await setSlot(slot(2), widgets[1]);
  const legalSetup = duplicateSlots().length === 0;

  /* now ask slot2 for what slot1 is holding */
  const freed = slot(2).value;
  await setSlot(slot(2), widgets[0]);
  const moved = slot(2).value === widgets[0] && slot(1).value === freed;
  const stillLegal = duplicateSlots().length === 0;

  /* empty is a blank slot, not a widget: it may repeat */
  await setSlot(slot(1), "empty");
  await setSlot(slot(2), "empty");
  const emptiesAllowed = slot(1).value === "empty" && slot(2).value === "empty" &&
                         duplicateSlots().length === 0;

  /* and a dongle already holding a duplicate has to be named rather than drawn */
  for (let i = 1; i <= 6; i++) slot(i).value = "empty";
  slot(1).value = widgets[0];
  slot(2).value = widgets[0];
  const spotted = duplicateSlots();
  return { legalSetup, moved, stillLegal, emptiesAllowed, spotted, freed };
})()`, ctx);
check(oneSlot.legalSetup && oneSlot.moved && oneSlot.stillLegal,
      "picking a widget another slot has moves it rather than duplicating it" +
      (oneSlot.moved ? "" : " (it did not move)"));
check(oneSlot.emptiesAllowed, "but any number of slots may be empty");
check(oneSlot.spotted.length === 1 && /slot1 and slot2/.test(oneSlot.spotted[0]),
      `a dongle already holding a duplicate is named: ${oneSlot.spotted}`);


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

/*
 * A preset naming a setting the connected dongle has not got is skipped rather
 * than written, which is what lets one page serve several firmwares - so the
 * only mistake to catch here is a name no firmware has.  settings_list.h is
 * the second opinion, because the fixture is as old as the dongle that sent it
 * and a setting added since is in the list and not in the schema.
 */
const presets = vm.runInContext(`(() => {
  const listed = ${JSON.stringify(listed)};
  const unknown = [], wrong = [];
  for (const preset of PRESETS) {
    for (const [name, word] of Object.entries(preset.values)) {
      const setting = settings.find((s) => s.name === name);
      if (!setting) {
        if (!listed.includes(name)) unknown.push(preset.name + "/" + name);
        continue;
      }
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

/*
 * Apply is the one button that replaces a whole saved look rather than editing
 * a corner of one: the live settings are the profile the dongle is on, so
 * sixty-odd colours land in it at once.  Declining has to leave it untouched.
 */
dialogs.confirm = false;
dongle.written = [];
await vm.runInContext("applyPreset(PRESETS[1])", ctx);
check(dongle.written.length === 0 && /left as it was/.test(
        document.getElementById("status").textContent),
      `declining the warning writes nothing (${dongle.written.length} written)`);
dialogs.confirm = true;

dongle.written = [];
await vm.runInContext("applyPreset(PRESETS[1])", ctx);
const applied = new Set(dongle.written.map(([name]) => name));
/* everything in the preset that this firmware actually reported, and nothing
 * else: the ones it did not report are the ones writeValues() skips */
const writable = vm.runInContext(
  `Object.keys(presetValues(PRESETS[1])).filter((n) => settings.some((s) => s.name === n)).length`,
  ctx);
check(applied.size === writable,
      `applying a preset wrote all ${applied.size} of its ${writable} writable settings ` +
      `and nothing else`);

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

/*
 * Which profile the dongle is on is true of every tab, not only the one about
 * profiles - somebody recolouring a maze is exactly the person who needs to
 * know which look they are recolouring - so it is checked on the strip.
 */
check(vm.runInContext("onSlot === 0 && onProfileName() === \"Desk\"", ctx),
      "the page read which profile the dongle is on");
const chip = document.getElementById("nav").children.find((b) => b.id === "onprofile");
check(chip && chip.children.some((b) => b.textContent === "Desk"),
      "and names it beside the tabs");

vm.runInContext('setNav(SECTIONS.findIndex((s) => s.view === "profiles"))', ctx);
check(document.getElementById("panels").children.length === 2 &&
      document.getElementById("previewpanel").classList.contains("hidden") &&
      document.getElementById("work").classList.contains("solo"),
      "the Profiles tab draws its two lists and no preview beside them");

/*
 * Two rows can never be deleted - the one the dongle is on, because it would
 * be left on nothing, and the first, because that is what it falls back to -
 * and the row it is on has nowhere to load from.  The buttons are read off the
 * rendered rows rather than off the predicate that picks them, so a rule that
 * is only in the firmware shows up here as a button the page still offers.
 */
const rowActs = () => {
  const saved = document.getElementById("panels").children[1];
  return saved.children[2].children.map((row) => ({
    name: row.children[0].children[0].textContent,
    acts: row.children[2].children.map((b) => b.textContent + (b.disabled ? "(off)" : "")),
  }));
};
const rows = rowActs();
check(rows[0] && rows[0].acts.join(",") === "Duplicate,Export,Rename,Delete(off)",
      `the row the dongle is on offers no Load and no Delete: ${rows[0] && rows[0].acts}`);
check(rows[1] && rows[1].acts.join(",") === "Load,Duplicate,Export,Rename,Delete",
      `another saved profile offers all five: ${rows[1] && rows[1].acts}`);

/*
 * The round trip is the whole point of a file: what one dongle exported has to
 * arrive on another as the same profile.  It also has to arrive without
 * touching the settings that are on the panel right now, which is what the
 * staging is for - so the writes are counted on the way through.
 */
dialogs.prompt = () => "2";
dongle.written = [];
await vm.runInContext("guardAction(() => exportProfile(profiles[1]))", ctx);
const file = saved.text ? JSON.parse(saved.text) : {};
check(file["pacman-dongle-profile"] === 1 && file.name === "Night" &&
      file.settings && file.settings["game-wall"] === "2121de",
      "an exported profile is a file another dongle could read");

await vm.runInContext(`importProfile(${JSON.stringify(saved.text || "{}")})`, ctx);
check(dongle.profiles[2] && dongle.profiles[2].name === "Night" &&
      JSON.stringify(dongle.profiles[2].values) === JSON.stringify(dongle.profiles[1].values),
      "importing it puts the same profile in another slot");
check(dongle.written.length === 0 && dongle.current === 0,
      `an import leaves the live settings alone and the dongle where it was ` +
      `(${dongle.written.length} written, on ${dongle.current})`);

/*
 * A staged profile written into the slot the dongle is on would be dropped
 * unread - that slot answers out of the live settings - so the page has to
 * refuse the slot rather than hand the firmware a write it will reject.
 */
dialogs.prompt = () => String(dongle.current);
const untouched = JSON.stringify(dongle.profiles[dongle.current]);
await vm.runInContext(`guardAction(() => importProfile(${JSON.stringify(saved.text || "{}")}))`, ctx);
check(JSON.stringify(dongle.profiles[dongle.current]) === untouched &&
      /would not survive/.test(document.getElementById("status").textContent),
      "an import cannot be aimed at the profile the dongle is on");

/*
 * Duplicating is what there is instead of unsaved work: everything typed at
 * this page reaches flash as it is typed, so keeping a look before changing it
 * means copying it and carrying on from the copy.  The copy is where the
 * dongle ends up, and the original has to come out of it untouched.
 */
dialogs.prompt = () => "3";
const before = JSON.stringify(dongle.profiles[0].values);
await vm.runInContext("guardAction(() => duplicateProfile(profiles[0]))", ctx);
check(dongle.profiles[3] && dongle.current === 3 &&
      JSON.stringify(dongle.profiles[3].values) === before,
      `copying the profile the dongle is on moves it onto the copy (on ${dongle.current})`);
check(JSON.stringify(dongle.profiles[0].values) === before &&
      dongle.profiles[0].name === "Desk",
      "and leaves the one it came from exactly as it was");
check(vm.runInContext("onSlot === 3", ctx) &&
      document.getElementById("nav").children.some(
        (b) => b.id === "onprofile" && b.children.some((n) => n.textContent === "3")),
      "the strip follows it onto the copy");

/*
 * The first slot survives a page that has moved off it: the dongle is on the
 * copy now, so the only thing keeping slot 0 is the rule that it is the
 * fallback.
 */
await vm.runInContext("guardAction(() => deleteProfile(profiles[0]))", ctx);
check(dongle.profiles[0] !== null && /falls back/.test(
        document.getElementById("status").textContent),
      "the first profile still cannot be deleted once the dongle has left it");

await vm.runInContext("guardAction(() => deleteProfile(profiles[3]))", ctx);
check(dongle.profiles[3] !== null && /is on/.test(
        document.getElementById("status").textContent),
      "and neither can the one it is on");

await vm.runInContext("guardAction(() => renameProfile(profiles[2]))", ctx);
check(dongle.profiles[2].name === "3", "renaming reaches the dongle");

await vm.runInContext("guardAction(() => deleteProfile(profiles[2]))", ctx);
check(dongle.profiles[2] === null, "deleting a slot it is not on frees it");

/* and moving back writes down the one being left, rather than dropping it */
await vm.runInContext("guardAction(() => loadProfile(profiles[0]))", ctx);
check(dongle.current === 0 && dongle.profiles[3] !== null,
      `loading another profile moves the dongle and keeps the one it left ` +
      `(on ${dongle.current})`);

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
check(!document.getElementById("nav").children.some((b) => b.textContent === "Profiles") &&
      !document.getElementById("nav").children.some((b) => b.id === "onprofile"),
      "and gets neither a button for it nor a profile to be on");

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
