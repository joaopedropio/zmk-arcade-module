# Working on this repo

A ZMK module for the [snake dongle](https://github.com/joaopedropio/snake-dongle):
the `pacman_adapter` shield turns the dongle's 240x240 ST7789V into one of eight
self-playing games - a Pac-Man in a maze, an Asteroids-shaped Space Shooter with
a triangle that turns and thrusts freely, a Bomberman-shaped brick field where a
bomber blows its way through soft wall and leaves by the door under one of them,
a Street Fighter-shaped ring where two fighters trade punches, sweeps and
fireballs over three rounds, a Metal Slug-shaped ridge a trooper runs right
along for ever, a Frogger-shaped crossing where a frog hops a road and rides
logs over a river, a Donkey Kong-shaped site a climber goes up six sloping
girders on while barrels roll down them, or a Tempest-shaped well a claw slides
round the rim of shooting down its lanes - chosen by the `game` setting - with
sound out of a MAX98357A. Everything is drawn straight to the
panel — there are no LVGL objects and no full frame buffer, only dirty
rectangles pushed over SPI, so the usual LVGL advice does not apply here.

## Check it on the host first

Four harnesses build the firmware's own C - and its page - on a laptop. Run whichever one covers
the change before claiming it works; flashing is the slow path.

```sh
tools/sim/build.sh /tmp/pacman-sim && /tmp/pacman-sim 3000
tools/sim/build.sh /tmp/pacman-sim && /tmp/pacman-sim shooter 3000
tools/sim/build.sh /tmp/pacman-sim && /tmp/pacman-sim bomber 3000
tools/sim/build.sh /tmp/pacman-sim && /tmp/pacman-sim fighter 3000
tools/sim/build.sh /tmp/pacman-sim && /tmp/pacman-sim commando 3000
tools/sim/build.sh /tmp/pacman-sim && /tmp/pacman-sim frogger 3000
tools/sim/build.sh /tmp/pacman-sim && /tmp/pacman-sim kong 3000
tools/sim/build.sh /tmp/pacman-sim && /tmp/pacman-sim tempest 3000
```
Any game core and its renderer, blitting into a 240x240 buffer. Every frame it
checks that nobody is inside a wall, standing on water or off the panel, and
that the incremental redraw matches a full repaint, and it prints pixels per
frame — so it catches both game bugs and drawing bugs. The game name goes in
front of the old argument list and may be left out, in which case it is the
maze. `/tmp/pacman-sim 640 2 /tmp/frames 40` dumps PPMs instead.
Balance changes want a soak: 200k frames prints clears against deaths for the
maze, meteors destroyed against restarts for the shooter, boards cleared, walls
broken and enemies destroyed against a breakdown of what killed the bomber for
the brick field, rounds and matches against whether they went to a knockout or
to the clock for the ring, ground covered and the longest run against what took
each life for the ridge, bays filled against a breakdown of what killed the
frog for the crossing, girders climbed and hammers taken against what ran the
climber over for the site, and wells cleared and enemies destroyed against a
breakdown of what killed the claw for the well. Building it with `-DFR_TRACE`,
`-DDK_TRACE` or `-DTP_TRACE` adds a line per death in the crossing, on the site
or in the well saying where and why, which is the only way to tell a pilot bug
from a board that is simply hard.
`docs/demo.gif` is those frames at 15 fps, which is the speed the dongle plays
it, with the splash held in front for the first two seconds; `docs/shooter.gif`,
`docs/bomber.gif`, `docs/fighter.gif`, `docs/commando.gif`,
`docs/frogger.gif`, `docs/kong.gif` and `docs/tempest.gif` are the same for the
other seven, without a splash:

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

That command alone builds a dongle with no shell, and so with no `pacman`
command, no profiles and nothing for the configurator to talk to: `PACMAN_SHELL`
depends on `SHELL`, so `shell.c` and `helpers/profiles.c` are simply not
compiled. Add the console snippet, and put the USB identity on the command line
rather than in a `.conf` — snippet config is merged after the keyboard's and
would win:

```sh
  ... -d build/cyg_dongle -S cdc-acm-console -- \
  ... -DCONFIG_USB_DEVICE_PRODUCT='"Cygnus"' -DCONFIG_USB_DEVICE_PID=0x615E
```

Check rather than assume — `grep ^CONFIG_PACMAN_SHELL= build/cyg_dongle/zephyr/.config`.
Without the shell the image links byte for byte the same as one built before any
shell-side change, so a clean build says nothing about code it never saw.

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
    ├── splash_image.h       the other splash, run-length, from tools/splash_art.png
    ├── battery_status.c output_status.c layer_status.c wpm.c modifier.c
    ├── helpers/display.c    the drawing engine: bitmaps, text, rects, slots
    ├── helpers/settings.c   flash-backed settings; settings_list.h is the list
    ├── helpers/profiles.c   named sets of all of them, one flash key apiece
    └── game/                panel.c (the shared band and the 5x7 font),
                             pacman_core.c, pacman_render.c, shooter_core.c,
                             shooter_render.c, bomber_core.c, bomber_render.c,
                             fighter_core.c, fighter_render.c,
                             commando_core.c, commando_render.c,
                             frogger_core.c, frogger_render.c,
                             kong_core.c, kong_render.c,
                             tempest_core.c, tempest_render.c, pacman_sfx.c
src/ include/ dts/           the zmk,behavior-dongle-action behaviour
tools/                       the four host harnesses and three generators
tools/wasm/                  the renderer built for the browser, by emscripten
docs/configurator/           the WebSerial settings page, served by GitHub Pages
```

`widgets/game/*.c` is strictly portable C — no Zephyr, no LVGL, no libc beyond
string and math. Keep it that way; the simulators are what it buys. The UI files
may include Zephyr headers only where `tools/uisim/stub/` already stubs them.
No game may include another's headers: what they share - the panel size, the
staging band, `pm_blit()`, `pm_rgb565()`, the rectangle being painted and the
5x7 font that writes on it - is in `game/panel.h`, and anything else they turn
out to have in common belongs there too. `pm_put()` is inlined in that header
on purpose: it is the inner loop of the whole shield, and a call across a
translation unit per pixel is a millisecond of every frame.

## Things that bite

- **`pacman_art.h`, `pacman_tunes.h` and `splash_image.h` are generated.** Edit
  `tools/sprites.py`, `tools/tunes.py` or `tools/splash_art.png` and regenerate;
  hand-editing the headers loses the change.
  ```sh
  python3 tools/tunes.py > boards/shields/pacman_adapter/widgets/game/pacman_tunes.h
  python3 tools/sprites.py > boards/shields/pacman_adapter/widgets/pacman_art.h
  python3 tools/splash_image.py > boards/shields/pacman_adapter/widgets/splash_image.h
  ```
  The picture is at most eight flat colours, run-length encoded three bits of
  palette index to five of length, and no run crosses a row - which is what
  lets the decoder hand the panel one finished row at a time instead of
  needing the 115KB frame buffer this shield has not got.
- **The geometry constants are load-bearing and documented where they live** —
  `PM_TILE` in `pacman_core.h`, `PM_WALL_LINE`/`PM_WALL_INSET`/`PM_WALL_R`/
  `PM_BORDER_GAP`/`PM_SPRITE` in `pacman_render.h`; `SS_SUB`, the angle units,
  `SS_NOSE`/`SS_REAR`/`SS_NOTCH`/`SS_HULL_R`/`SS_FLAME_R` and `SS_ROCKS` in
  `shooter_core.h`, `SS_HUD_*`/`SS_BANNER_*`/`SS_SHIELD_R` in
  `shooter_render.h`; `BB_CELL`/`BB_COLS`/`BB_ROWS`/`BB_OY`, `BB_FUSE`/
  `BB_FLAME`, the two caps and `BB_CLOCK` in `bomber_core.h`, `BB_HUD_*`/
  `BB_TALLY_*`/`BB_BANNER_*` in `bomber_render.h`; `FR_CELL`/`FR_ROWS`/
  `FR_LOOP`/`FR_RUNOFF`/`FR_SUB`/`FR_SPRITE_H`/`FR_FROG_W` and the lane table
  in `frogger_core.h`, `FR_CLOCK_*`/`FR_TURT_A`/`FR_TURT_B` in
  `frogger_render.h`; `DK_SUB`/`DK_TOP`/`DK_FLOORS`/`DK_BASE`/`DK_RISE`/
  `DK_DROP`, `DK_LADDER[]`, `DK_HAMMER[]`, `DK_TELL` and the jump's `DK_ARC[]`
  in `kong_core.h`, `DK_HUD_*`/`DK_CLOCK_*`/`DK_BANNER_*` in `kong_render.h`;
  `TP_SEGS`/`TP_DEPTH`/`TP_Z0`/`TP_SUB`/`TP_GRAB`/`TP_LAND`/`TP_CLAW_D`/
  `TP_SPIKE_MAX` and the `TP_SHAPE[]` table in `tempest_core.h`, `TP_BODY` and
  `TP_HUD_*`/`TP_BANNER_*` in `tempest_render.h`. Read those comments before
  changing a number. A
  `_Static_assert` catches the corner-overlap case, the board not filling the
  panel, and a turtle wider than its lane; `PM_MARGIN` and `PM_MARGIN_END`
  differ on an odd leftover, so anything painting the margin has to use the
  right one.
- **What a maze edit has to hold** is three things: no 2x2 all wall, no 2x2 all
  corridor, no dead ends. The odd lattice (even/even always corridor, odd/odd
  always wall, the rest links) gives all three for free, and most of the maze is
  built on it — but row 7 departs from it deliberately, to leave an isolated
  tile between the two L's. `python3 tools/check_maze.py` checks the art either
  way; run it after touching `MAZE_ART`.
- **The shooter's pilot is two rules and some bookkeeping.** Never break a
  meteor closer than `SS_KEEP_OFF` - the fragments land inside the distance
  needed to dodge them, and that alone was killing two runs in three - and
  dodge *sideways* to a meteor's course rather than away from it, holding the
  chosen side for `SS_EVADE_HOLD` frames. Without the hold the ship dithers on
  the spot: near the track a meteor is on, which way to step flips frame to
  frame. Anything asking "is this closing on me" has to use the relative
  velocity of the two, never the meteor's alone, or the ship coasts into rocks
  that were drifting away. `tools/sim`'s soak is how any of this is judged -
  restarts per 200k frames, not how it looks over ten seconds.
- **The bomber's pilot is two maps and one comparison.** One says how many
  frames each cell has before it burns, with the chains settled first - a bomb
  inside another's arms goes off with it, so a fuse is not what it was dropped
  with. The other says how soon an enemy could stand in each cell, walked from
  all of them at once and deliberately blind to their headings, because that is
  wrong exactly at the junctions where every catch happens. Then one
  breadth-first search of the board, timed in frames rather than in steps,
  prices every reachable cell by what a bomb there would break less what it
  costs to walk to, plus `BB_V_SAFE` for daylight. The cell underfoot is in
  that list like any other, which is what makes "drop one here" and "go
  somewhere better" one comparison rather than two rules that can disagree - and
  it is worth what a bomb would break even when there is no bomb spare, or the
  moment the last one is dropped every other wall on the board outbids the one
  being stood next to.
- **Three things in the bomber were bought with deaths, not reasoned out.** A
  bomb is never dropped without a route out, and the route has to end on a cell
  no bomb reaches rather than one that is merely quiet - but it has to be proved
  against a clock a cell's walking ahead of the real one (`drop_escape()` passes
  `fpc` as slack) and the step it found has to be the step actually taken.
  Proving it and then standing still for the frame the drop costs spends the
  only slack the route had, and working it out again next frame gives a
  different answer because every other fuse has moved on. Third, `detonate()`
  works out the whole chain before applying any of it: a blast stops at a brick,
  so a bomb walked after the brick in front of it has come down reaches a cell
  further than the map said, which put the bomber one cell past where it had
  proved the flames stop about once in a hundred bombs. Together those three are
  every time it blew itself up; `deaths: blown up=…` in the soak is the
  instrument, and it should read 0.
- **The ring is one number and one beat.** `FG_REACTION` is how long an attack
  has to have been coming before the other one may notice it, and at three - one
  more than a punch spends winding up - a punch lands on anybody who was not
  already guarding while a sweep is answerable. At one, both are, both are
  blocked about half the time, and a round is two fighters trading nothing;
  `rounds ended: on the clock=` in the soak is the instrument. `FG_POISE` is the
  other half: without a pause between one attack and the next a fighter swings
  on every frame it is able to, which means it is never in a state that could
  put a guard up, which means neither of them ever guards. And whatever a swing
  is worth has to be taken *with* the swing, before either is applied - reading
  it back afterwards charges a trade at the price of a punch, because applying
  the first one has already put the other into a flinch, and it does it to the
  same fighter every time.
- **The ring's ring is geometry, not a table.** A punch is a rectangle at
  `FG_HIGH_Y`, a sweep one at `FG_LOW_Y`, a crouch is a shorter fighter and a
  jump is a fighter further up; which of the three answers beats which of the
  three attacks falls out of whether two rectangles overlap. The two
  `_Static_assert`s in `fighter_core.h` are what hold it: move `FG_HIGH_Y` under
  `FG_CROUCH_H` and crouching stops working without a line of code changing.
  `FG_HOP_LEAD` is derived the same way, from `FG_RISE()` rather than by taste.
- **Nothing in the ridge may be drawn at a world position.** The panel carries a
  few thousand pixels a frame and a scrolling background is fifty-seven
  thousand, so `commando_render.c` keeps the outline of each of the 240 columns
  and repaints only the columns whose outline moved - and only the rows between
  where it was and where it is. A column under a flat hilltop or over flat
  ground is free. Grass, a rock on the ground, a rounded hill or a gradient in
  the sky would each give every column a new look every frame and cost twenty
  times the lot. Detail belongs to the panel (the sun, the readout) or to a
  sprite. `CM_SKIN` is how far below the surface a column stops being plain
  ground, and it is the same number in `draw_column()` and in `span_of()`
  because being a pixel out there is a run of stale pixels.
- **The ridge's generator is three rules, applied forward.** A hole is never
  next to a hole, the ground either side of a hole is the same height, and no
  two chunks differ by more than one step - so every gap can be jumped and every
  wall climbed, and the pilot never has to deal with terrain it cannot pass.
  They are applied in `make_chunk()` as the chunk is made, because a rule about
  a chunk and its neighbour is trivial when the neighbour is already decided and
  a recursion when it is not.
- **Two things in the ridge were bought with lives.** A step down is walked down
  rather than fallen off: a trooper in the air cannot jump, so a step
  immediately before a hole meant walking into the hole with no say in it. And
  an enemy round already in the air is answered by jumping it - the rifle is no
  use once it has been fired - which `CM_DUCK` and its `_Static_assert` size
  from the arc. Without the second the run ended every thirteen seconds and
  always the same way; `deaths: shot=` in the soak is the instrument. The jump
  itself is proved by winding `body_step()` forward over the ground ahead, which
  is the same function that then moves the trooper, and it is taken as late as
  it still lands rather than as early as it can.
- **The crossing's frog and the log under it are compared in eighths, never
  in pixels.** Both advance by the same number of eighths a frame, so their
  relative position is exact - but `x / 8` for each of them is not, and the
  pixel difference wobbles by one from frame to frame. Rounding first is a frog
  standing on the end of a log that gets shaken off by the arithmetic, and it
  reads as a drowning nobody can explain. `float_at()` takes eighths, and the
  pilot judges the cell it will actually land on (`f->x + dcol * FR_CELL *
  FR_SUB`) rather than a rounded version of it.
- **The frog's pilot prices danger in two currencies, and the gap between them
  is the behaviour.** A cell that is death on arrival costs `FR_FATAL` and is
  never taken; everything else costs by when it goes wrong, full price while
  the frog is still committed to the cell and less than one row of progress
  after that. `FR_COMMIT` is what that commitment is - the hop in, the pause
  before it may decide again, and the hop out - and `FR_HOLD` is the shorter
  one for staying put, which is why waiting is the cheap option. Making the
  far end of the look-ahead expensive gives a frog that waits on the bank for a
  board with nothing wrong with it anywhere and dies of the clock; making the
  near end cheap gives one that lands in front of cars. Both were measured, not
  guessed - `deaths: run over=… no time=…` in the soak is the instrument.
- **The two rules that got the frog across** are that a bank cell is worth
  more when the row above it is about to open (`FR_OPENS`, and it is a bonus
  for the good columns rather than a penalty on the bad ones, or the frog steps
  back off the bank rather than wait), and that in the river the lane outranks
  the row (`FR_STEER`) - a frog that climbs out of the current carrying it
  towards the bay it wants ends up at the top of the river on the wrong side of
  the last bay with no way back. Neither may apply while the frog is standing
  on a bank looking at where to go: nothing moves under it there, so any term
  that can make waiting beat setting off makes it wait for ever.
- **The climber and the barrels are compared one frame apart, and that is not a
  bug.** The order inside a frame is: barrels step, the climber decides, the
  climber steps, then the two are tested against each other - so the pose the
  climber will be in at `t` has to be priced against the barrel that has taken
  `t - 1` steps. `plan_cost()` passes `t - 1` to `barrel_at()` for exactly that
  reason. Getting it wrong is not a crash and not a stale pixel; it is a pilot
  that walks into things it thought it had cleared, and it took deaths from
  fourteen per sixty thousand frames to five. Two smaller ones were bought the
  same way: a barrel commits to a ladder `DK_TELL` pixels before it (so the
  decision is visible to the pilot rather than sprung on it), and the jump is
  the written-down `DK_ARC[]` rather than a parabola, because a parabola clears
  a barrel for a frame and a half and makes every jump a coin toss.
- **The well has no world in pixels; it has `tp_at()`.** Everything in it is a
  lane and a depth, and where that lands on the panel is one division -
  `TP_Z0 / (TP_Z0 + TP_DEPTH - d)` of the rim offset. A linear scale instead is
  the one change that stops it looking like a tunnel. The vanishing point is per
  shape rather than the middle of the panel, which is what makes the two open
  wells work: a closed one surrounds its own centre and vanishes into itself, a
  strip does not, and shrinking one towards the middle of the screen leaves a
  band across the bottom third with nothing above it.
- **The well's dirty rectangles need a checksum, not a look byte.** Everything
  else here is a sprite, so a box plus a byte saying what it looked like is
  enough. A flipper is a trapezoid cut out of the well: its far edge can slide a
  pixel down the tube while the box around it does not move at all, because the
  corner that moved was not the one the box was measured from. So `tp_box.look`
  is a hash of the lane, the depths and the state the shape was drawn from, and
  `span_box()` samples every spoke between a span's two ends - a claw sitting
  across a spoke bulges past the line between them by however sharp that corner
  is. Both were found by the repaint check and neither is visible any other way.
- **The claw is priced along its whole slide, not at its destination.** It is a
  lane wide and it slides rather than jumping, so it is standing in every lane
  between where it is and where it is going - including the one it is leaving.
  `path_kills()` is that; without it nearly every death in the soak was a pulsar
  the claw had stepped sideways into while its destination was perfectly safe.
  The other half of the pilot is one comparison in `lane_score()`: whether a
  shot fired from a lane reaches a climbing enemy before that enemy reaches the
  rim. If it would, the lane is where to be and the thing in it is a target; if
  it would not, the lane is death. Both halves count frames the same way -
  `travel_to()` returns a frame index rather than a count of moves, because the
  claw has already slid once by the time frame zero is over, and counting moves
  instead puts every danger a frame later than it arrives.
- **There are no waves in the shooter and nothing counts them.** Meteors are
  topped up whenever there is room, weighted by what they cost to draw rather
  than by how many there are, so the frame stays about the same price whatever
  the mix is. Adding a wave counter back would mean a number nobody watching a
  dongle can act on, which is the same reason the action button stopped
  stepping through themes.
- **The games share one staging band and one font, and nothing else.**
  `game/panel.h` holds `PM_PANEL`, `pm_band[]`, `pm_blit()`, `pm_rgb565()`, the
  band being painted (`pm_band_begin()`, `pm_put()`, `pm_fill()`) and the 5x7
  font (`pm_text()`, `pm_text_w()`, `pm_digits()`), and each renderer
  `_Static_assert`s its own widest blit against `PM_BAND_PX`. One buffer is
  safe only because one game runs at a time — never tick two.
- **The renderers work two opposite ways round.** `pacman_render.c` asks each
  pixel what is on it; the other five clear a rectangle and stamp the sprites
  reaching into it, in a fixed order. Either
  way, a rectangle has to be composed from game state alone and never from what
  is already on the panel — that is exactly what `tools/sim`'s repaint check
  compares, and it is what catches a sprite that forgot to say it moved. Three
  of them add a rule to that. Most of what changes in the brick field and on the
  ridge is the ground, so the brick field keeps one number per cell and the
  ridge one per screen column for what it looked like last frame, and only the
  ones whose number moved are repainted; and the crossing, the site and the well
  skip any sprite whose box *and* look are both what they were. So anything that
  can change without moving — a wall coming down, a pickup appearing under it, a
  fuse pulsing, a flame narrowing, the board flashing, a turtle going under, a
  bay filling, the ape winding up, a hammer being taken, a spike growing, a
  pulsar's lane lighting up — has to be in `cell_look()`, in the ridge's pair of
  column arrays, or in the `look` of the crossing, the site or the well, or it
  goes stale.
- **Switching games repaints all of them.** `pacman_set_game()` only says
  which one the timer ticks; each renderer remembers what it last put on the
  panel, and the idle ones have had the others drawing over them ever since.
  That is why `repaint_all()` and not `game.redraw`, in `pacman_start()`, the
  palette reload and the periodic full redraw alike. No game is reset, so the
  maze comes back mid-level.
- **A preset has to have an opinion about all eight games.** Theme 0 already
  makes the dashboard's colours count one at a time; across the eight palettes
  it is the same bargain, and a preset that stopped at the maze would leave a
  dongle on any of the others looking exactly as it did. `tools/pagetest`
  drives the preview from `presetValues(PRESETS[0])` and fails if any of the
  eight comes out blank or draws another's panel.
- **The sound thread runs above ZMK's display thread** (priority 3 against 5).
  Below it, a full repaint starves the amplifier. The game timer and the sound
  thread talk only through atomics.
- **Do not build `pacman_adapter` alongside `snake_adapter`** — same hardware,
  same `zmk,behavior-dongle-action`, defined twice.
- **Every game is silent by design.** Only a keyboard connecting or
  dropping makes a sound. Do not add per-event sounds back; a dongle that chirps
  at every pellet is a dongle you unplug, and the four games after the maze were
  never given any for the same reason — a bomb going off every two seconds, or a
  rifle five times a second, would have been the worst of them.
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
- **A widget goes in one slot only, and nothing enforces it.**
  `get_slot_by_name()` returns the first slot holding a widget, so a second
  slot set to the same one is never drawn into - it keeps whatever the frame
  left there. `empty` is the exception, being a blank slot rather than a
  widget. The firmware takes the duplicate silently; the configurator is where
  it is kept out of reach, by moving the widget rather than copying it.
- **Anything that draws belongs on the display queue.** The shell runs on its
  own thread, so `pacman set` submits the repaint to `zmk_display_work_q()`
  rather than painting where it stands; two threads on the same SPI bus is a
  corrupt panel. Flash writes go the other way — they stay on the calling
  thread, off the queue that has to repaint next.
- **`pacman schema` is a wire format.** The configurator page parses those
  tab-separated columns, so adding a setting is free but adding or reordering a
  column breaks the page. `pacman profile list`, `pacman profile show` and
  `pacman profile current` are three more of them, closed by the same bare
  `end`; everything else under `profile` answers with a sentence whose first
  word is what it did - saved, loaded, renamed, deleted, staged, cleared - and
  the page treats anything else as a failure, so neither end keeps a list of
  error strings. Which slot the dongle is on is `current` rather than a fourth
  column on `list`, because the column is the half that cannot grow.
- **The action button steps between profiles, and the slot widget shows
  which.** It used to step through themes and draw "SKIN nn"; a theme number
  was nothing anybody could act on from the panel, so the same hold now loads
  the next profile and the widget draws "PROF nn". The theme machinery is
  untouched underneath - theme 0 is still what makes the individual colours
  count - but nothing steps through themes any more, and the configurator
  offers no control for the number (`UNOFFERED` in the page). The threshold
  setting is still called `theme-threshold`; renaming it would drop it from
  every stored profile, which is not worth the tidier word.
- **`profiles.c` is built whether or not there is a shell**, because the button
  reaches profiles too - so `action_button.c` and `theme.c` call into it, and
  both host harnesses have to answer `pacman_profile_current()`,
  `_next()` and `_load()` themselves alongside the sound and game no-ops.
  A profile switch from the button is a flash write for the profile being left
  plus one per setting that moved, which is long enough to look like a button
  that did not take - so `widgets/progress.c` puts a modal with a bar over
  whichever screen is up, and the button is refused entirely until it is done.
  The button is answered on the display queue, so the switch itself goes to
  **ZMK's low-priority queue** (`zmk_workqueue_lowprio_work_q()`, priority 10),
  never the system one: that is cooperative at -1 and sits above the sound
  thread, so a burst of flash writes there would hold the amplifier off. Only
  the drawing comes back to the display queue, a work item at a time, and the
  game is stopped first because its timer runs on that same thread and would
  paint the maze over the box.
- **The dongle is always on a profile, and the live settings are it.**
  `pacman set` reaches flash as it is typed, so there is no draft of a profile
  and nothing to save: what it writes is the profile you are on. Keeping a look
  before changing it is `profile save <free slot>`, which snapshots the panel
  and moves the dongle onto the copy. Slot 0 is written on a dongle's first
  boot out of whatever the build left in the settings, and neither it nor the
  slot in use can be deleted - which is what makes "always on one" true. That
  is also why the current slot's record is only rewritten as the dongle leaves
  it: holding it in step would cost eight hundred bytes per colour somebody
  drags a slider over, so `name()` and `read()` answer for that slot out of the
  live settings, and `load` snapshots the profile being left before it applies
  the one being gone to. `configure()` calls `pacman_profile_init()` between
  loading the defaults and applying them, guarded by `CONFIG_PACMAN_SHELL` -
  a build with no shell has no profiles to be on.
- **A profile is one flash key, not one per setting** - the opposite of what
  the settings themselves do, because a profile is only ever written whole and
  eighty keys apiece would spend the storage partition on entries nothing reads
  separately. The blob is made safe to grow the other way: each value is
  written down beside the hash of its setting's name, so adding to or
  reordering `settings_list.h` leaves every stored profile readable. Renaming a
  setting drops it from existing profiles, which is the honest outcome. An
  import arrives a value at a time over the shell, so `stage` fills a RAM copy
  and `commit` is the single write.
- **Theme 0 is not derived.** With `CONFIG_PACMAN_USE_COMPLETE_CUSTOM_THEME`
  on - which is the default - theme 0 paints the dashboard from the individual
  colour settings, and `theme-primary` and its three companions do nothing.
  They only feed themes 1..N. So anything meaning to recolour the whole panel
  has to set the forty-six dashboard colours, which is what the configurator's
  presets do by sorting them into four roles rather than listing them five
  times over.
- **The configurator's preview is the real renderer**, compiled by
  `tools/wasm/build.sh` and committed as `docs/configurator/preview.js`. Change
  any drawing code - the game, the splash, a widget - and that file is stale
  until it is rebuilt. It walks `settings_list.h` for the name of every
  setting, so the page drives it by the same words the shell takes and there is
  no third list to keep in step; `reload_game_palette()` there does mirror
  `pacman_reload_palette()` by hand, house fill included.
- **A setting the schema calls `boot` needs the preview built again**, not
  pushed at the one that is running - the widgets sized their buffers from the
  slots at init, and `build_once()` has already happened. The page instantiates
  a fresh module and replays every value into it, which is why the values go in
  before the first render rather than after. `tools/pagetest` checks the result
  against a module built from nothing, so pushing instead of rebooting fails.
- **Both harnesses supply their own platform.** `tools/uisim/uisim.c` and
  `tools/wasm/preview.c` each define the sound, game and profile entry points
  as no-ops, because the dashboard widgets call them and neither a laptop nor
  a browser has a speaker or a flash partition to answer with. That is
  deliberate: the widgets are built exactly as the firmware builds them, with
  nothing compiled out. The two differ on the profile number - uisim makes one
  up in `uisim_state.h` like the battery levels, while the preview takes the
  real one from the page through `preview_set_profile()`, because there the
  page actually knows it.

## Style

The code explains why, not what: block comments above a constant or a function
saying what it is for and what it trades against, and no comment restating the
line under it. Match that. Same for commit messages — a sentence naming the
effect ("Stop the display starving the amplifier"), then a body explaining what
was wrong and what it cost, in prose.

The README is for someone installing this on their own dongle. Detail about how
the drawing works belongs in the headers, not there.
