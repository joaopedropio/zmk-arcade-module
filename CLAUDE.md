# Working on this repo

A ZMK module for the [snake dongle](https://github.com/joaopedropio/snake-dongle):
the `pacman_adapter` shield turns the dongle's 240x240 ST7789V into a self-playing
Pac-Man, with sound out of a MAX98357A. Everything is drawn straight to the panel —
there are no LVGL objects and no full frame buffer, only dirty rectangles pushed
over SPI, so the usual LVGL advice does not apply here.

## Check it on the host first

Four harnesses build the firmware's own C - and its page - on a laptop. Run whichever one covers
the change before claiming it works; flashing is the slow path.

```sh
tools/sim/build.sh /tmp/pacman-sim && /tmp/pacman-sim 3000
```
The game core and renderer, blitting into a 240x240 buffer. Every frame it checks
that nobody is inside a wall and that the incremental redraw matches a full
repaint, and it prints pixels per frame — so it catches both game bugs and
drawing bugs. `/tmp/pacman-sim 640 2 /tmp/frames 40` dumps PPMs instead.
Balance changes want a soak: 200k frames prints clears against deaths.
`docs/demo.gif` is those frames at 15 fps, which is the speed the dongle plays
it, with the splash held in front for the first two seconds:

```sh
ffmpeg -framerate 15 -i /tmp/frames/frame_%05d.ppm -vf palettegen=max_colors=64 /tmp/pal.png
ffmpeg -framerate 15 -i /tmp/frames/frame_%05d.ppm -i /tmp/pal.png \
    -lavfi paletteuse=dither=none /tmp/pacman.gif
```

```sh
tools/uisim/build.sh /tmp/uisim && /tmp/uisim /tmp/frames
```
The splash and the dashboard, drawn against the Zephyr/LVGL stubs in
`tools/uisim/stub/`. The widgets that ask ZMK for a layer name or a battery
level get their answers from `stub/uisim_state.h`, so the dashboard comes out
filled in - plausible, not live. Change those values to see a different one.

```sh
node tools/pagetest/pagetest.mjs
```
The configurator page, run against a stub DOM and `tools/pagetest/schema.txt` -
bytes a real dongle sent. The page is the one part of this a compiler never
sees, so a stale call left by an edit parses fine and only fails when somebody
plugs a dongle in; this catches that, a broken parse, and a preview that takes
the connection down with it. Run it after touching
`docs/configurator/index.html`.

```sh
tools/sfxsim/build.sh /tmp/sfxsim && /tmp/sfxsim /tmp/sounds 16000
```
The synth, rendered to .wav. Listen before flashing — most sound bugs (a note
that never sounds, a voice stolen, an onset that clicks) are audible here.

## Building the firmware

Not from this repo. It builds out of the `~/zmk-workspace` west workspace, with
this module and the keyboard config passed in as extra modules:

```sh
cd ~/zmk-workspace
ZEPHYR_TOOLCHAIN_VARIANT=zephyr ZEPHYR_SDK_INSTALL_DIR=$HOME/zephyr-sdk-0.17.0 \
  ./.venv/bin/west build -p always -s zmk/app -b 'nice_nano@2.0.0/nrf52840/zmk' \
  -d build/cyg_dongle -- \
  -DSHIELD="cygnus_dongle pacman_adapter" \
  -DZMK_CONFIG=$HOME/zmk-cygnus/config \
  -DZMK_EXTRA_MODULES="$HOME/zmk-pacman;$HOME/zmk-cygnus"
```

ZMK main uses hardware model v2, so the board is `nice_nano@2.0.0/nrf52840/zmk`,
never `nice_nano_v2` — the README's `build.yaml` snippet is the GitHub Actions
path, which is a different thing.

## Where things live

```
boards/shields/pacman_adapter/
├── pacman_adapter.overlay   panel, amplifier, action button
├── Kconfig.defconfig        display and LVGL defaults
├── custom_status_screen.c   hands ZMK an empty screen, starts the timer
└── widgets/
    ├── pacman.c             display device, palette, LVGL timer, WPM speed
    ├── sound.c              the I2S driver side of the synth
    ├── action_button.c      screens, themes, mute
    ├── splash.c logo.c frames.c theme.c configuration.c shell.c
    ├── battery_status.c output_status.c layer_status.c wpm.c modifier.c
    ├── helpers/display.c    the drawing engine: bitmaps, text, rects, slots
    ├── helpers/settings.c   flash-backed settings; settings_list.h is the list
    └── game/                pacman_core.c, pacman_render.c, pacman_sfx.c
src/ include/ dts/           the zmk,behavior-dongle-action behaviour
tools/                       the four host harnesses and two generators
tools/wasm/                  the renderer built for the browser, by emscripten
docs/configurator/           the WebSerial settings page, served by GitHub Pages
```

`widgets/game/*.c` is strictly portable C — no Zephyr, no LVGL, no libc beyond
string and math. Keep it that way; the simulators are what it buys. The UI files
may include Zephyr headers only where `tools/uisim/stub/` already stubs them.

## Things that bite

- **`pacman_art.h` and `pacman_tunes.h` are generated.** Edit `tools/sprites.py`
  or `tools/tunes.py` and regenerate; hand-editing the headers loses the change.
  ```sh
  python3 tools/tunes.py > boards/shields/pacman_adapter/widgets/game/pacman_tunes.h
  python3 tools/sprites.py > boards/shields/pacman_adapter/widgets/pacman_art.h
  ```
- **The geometry constants are load-bearing and documented where they live** —
  `PM_TILE` in `pacman_core.h`, `PM_WALL_LINE`/`PM_WALL_INSET`/`PM_WALL_R`/
  `PM_BORDER_GAP`/`PM_SPRITE` in `pacman_render.h`. Read those comments before
  changing a number. A `_Static_assert` catches the corner-overlap case;
  `PM_MARGIN` and `PM_MARGIN_END` differ on an odd leftover, so anything
  painting the margin has to use the right one.
- **What a maze edit has to hold** is three things: no 2x2 all wall, no 2x2 all
  corridor, no dead ends. The odd lattice (even/even always corridor, odd/odd
  always wall, the rest links) gives all three for free, and most of the maze is
  built on it — but row 7 departs from it deliberately, to leave an isolated
  tile between the two L's. `python3 tools/check_maze.py` checks the art either
  way; run it after touching `MAZE_ART`.
- **The sound thread runs above ZMK's display thread** (priority 3 against 5).
  Below it, a full repaint starves the amplifier. The game timer and the sound
  thread talk only through atomics.
- **Do not build `pacman_adapter` alongside `snake_adapter`** — same hardware,
  same `zmk,behavior-dongle-action`, defined twice.
- **The game itself is silent by design.** Only a keyboard connecting or
  dropping makes a sound. Do not add per-event sounds back; a dongle that chirps
  at every pellet is a dongle you unplug.
- **Disconnects are inferred from a peripheral battery level of 0**, because the
  central never sees the split connection event.
- **Kconfig is a default, not the value.** `configure()` is two calls:
  `pacman_settings_load_defaults()` fills in whatever flash had nothing to say,
  then `pacman_settings_apply_all()` pushes the result at everything that draws
  or sounds it. Nothing else should read `CONFIG_PACMAN_*` at its point of use —
  if it does, the shell cannot reach it.
- **A setting is one line in `helpers/settings_list.h`** and nothing else. That
  line is its flash key, its shell word, the values it takes, how it reaches
  whatever uses it, and what the build set it to; `settings.h` makes the enum
  from it and `settings.c` makes the table. Order in the list is load-bearing
  in one place: the four custom-theme colours come before the theme that derives
  the dashboard from them. Storage is one flash key per setting on purpose —
  the old single-blob format checked its own length on load, so growing it
  silently reset every dongle.
- **`apply` only runs for settings that moved.** `apply_all()` walks all of them
  every time the shell writes one, and the mute's apply makes a sound to prove
  it worked — so without the `applied[]` check, changing a colour would chirp
  the speaker and repaint the maze. A theme change is the one case that forces
  the colours through, because there the stored value has not moved but what is
  on the panel has.
- **Anything that draws belongs on the display queue.** The shell runs on its
  own thread, so `pacman set` submits the repaint to `zmk_display_work_q()`
  rather than painting where it stands; two threads on the same SPI bus is a
  corrupt panel. Flash writes go the other way — they stay on the calling
  thread, off the queue that has to repaint next.
- **`pacman schema` is a wire format.** The configurator page parses those
  tab-separated columns, so adding a setting is free but adding or reordering a
  column breaks the page.
- **The configurator's preview is the real renderer**, compiled by
  `tools/wasm/build.sh` and committed as `docs/configurator/preview.js`. Change
  any drawing code - the game, the splash, a widget - and that file is stale
  until it is rebuilt. It walks `settings_list.h` for the name of every
  setting, so the page drives it by the same words the shell takes and there is
  no third list to keep in step; `reload_game_palette()` there does mirror
  `pacman_reload_palette()` by hand, house fill included.
- **Both harnesses supply their own platform.** `tools/uisim/uisim.c` and
  `tools/wasm/preview.c` each define the sound and game entry points as no-ops,
  because the dashboard widgets call them and neither a laptop nor a browser
  has a speaker to answer with. That is deliberate: the widgets are built
  exactly as the firmware builds them, with nothing compiled out.

## Style

The code explains why, not what: block comments above a constant or a function
saying what it is for and what it trades against, and no comment restating the
line under it. Match that. Same for commit messages — a sentence naming the
effect ("Stop the display starving the amplifier"), then a body explaining what
was wrong and what it cost, in prose.

The README is for someone installing this on their own dongle. Detail about how
the drawing works belongs in the headers, not there.
