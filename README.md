# Pac-Man Dongle Module 🟡

A self-playing Pac-Man for the [snake dongle](https://github.com/joaopedropio/snake-dongle)
hardware: a ZMK module that turns the dongle's 240x240 ST7789V panel into a
little arcade cabinet. Nobody drives it — Pac-Man hunts pellets on his own,
runs for a power pellet when he is cornered, chases the blue ghosts, and the
four ghosts use the classic scatter/chase targeting rules.

<img src="docs/demo.gif" width="320" alt="The splash screen, then Pac-Man playing itself on the dongle display"/>

Same idea (and the same hardware definition) as the
[snake module](https://github.com/joaopedropio/snake-module), just a different game.

## What it costs

The game is drawn straight to the panel — no LVGL objects, no full frame
buffer, only the tiles that actually changed are pushed over SPI.

Proportions follow the arcade rather than the tile grid. The maze is 9x9
tiles of 24px; walls and corridors are both exactly one tile thick in the
layout, each wall drawn as a 1px tube standing off its tile rather than a slab
filling it, with corners rounded to `PM_WALL_R`. Sprites are 28px boxes centred on their tile, drawing
a 26px character — wider than the tile itself, the way the arcade runs a 16px
Pac-Man down an 8px corridor. On a 240px panel read at arm's length, how big the characters are
matters more than how elaborate the maze is, so the grid is kept as coarse as
it can be while still holding a maze.

What is drawn is lighter than the layout: `PM_WALL_INSET` stops each wall 4px
short of its tile on every side that faces open space, so a wall reads 16px
rather than 24 and the corridor beside it 32px rather than 24 — which is what
leaves room for a sprite wider than its own tile. Walls that meet
stay joined, since only open-facing sides are pulled in. That is the knob for
how heavy the walls look; shrinking `PM_TILE` instead takes the corridors and
the sprites down with them. A face looking at the ghost house door is the one
exception — it keeps its outline but takes no inset, because the door is drawn
as a line spanning its own tile and a wall standing back from it would leave
the ends of that line hanging in the gap.

`PM_WALL_INSET + PM_WALL_R` is how far a rounded corner reaches in from a tile
edge, and two opposite corners must not overlap; a `_Static_assert` fails the
build rather than letting a pixel land in two corner boxes and get drawn from
only the first. At a 24px tile it is exactly on that limit, so the walls are as
thin as this radius allows — thinner ones want a smaller `PM_WALL_R` too.

No tile is spent on a border: the maze is walled in by a line drawn round it in
the margin left over from `PM_COLS * PM_TILE`, with a gap wherever a row runs
off into the tunnel, so the whole grid is playfield and the outer ring of tiles
is a corridor. It is the same line a wall puts on a side facing open space —
`PM_WALL_LINE` thick, standing `PM_WALL_INSET` off the playfield, fill behind
it out to the edge of the panel — so the corridor round the outside comes out
the width of the ones inside, and no background shows at the edge of the
screen.

`PM_BORDER_GAP` is the clearance between the playfield and the margin. Sprites
are wider than their tile, so one running down the outermost corridor hangs
over the edge of the maze; that gap is the room it needs to be drawn in at all,
which makes it part of what gets painted rather than part of the border:
`paint()` works on a canvas of the playfield plus the ring and `bg_pixel()`
returns background for anything outside the maze, while `paint_border()` fills
the margin outside it. Clipping a sprite at the maze edge instead would flatten
its side, and letting the margin paint over the ring would rub it out — between
them the two cover the panel exactly once.

The corners are the one place the two overlap, because a border corner rounds
the other way than a wall's: a wall's arc cuts the tile corner away from the
corridor, the border's wraps the corridor it encloses, so it reaches
`PM_WALL_R` into the outermost tile — inside the canvas. `paint()` therefore
draws the border too, underneath the sprites, which also means a dirty
rectangle along the edge repaints it.

At 9x24 the grid is 216px, leaving an even 24, so the maze sits dead centre
with 12 all round. It does not always: an odd leftover splits one pixel wider
on the far side, which is why `PM_MARGIN` and `PM_MARGIN_END` are separate.
Anything painting the margin has to use the right one of the two, or it leaves
the last row and column of the panel untouched.

A row only wraps where it runs off into a tunnel. Everywhere else the edge of
the maze is that outer wall, which is what stops actors walking through the
border line on any row they like.

What keeps both walls and corridors one tile thick is the lattice an odd grid
sets up. Tiles on an even row and even column are always corridor, tiles on an
odd row and odd column are always wall, and the ones in between are links that
may be either. Every 2x2 block then holds exactly one tile of each kind, so no
2x2 can be all corridor (a corridor widening) or all wall (a wall thickening).
Only the links are a design choice. Dead ends reduce to a single rule on top
of that — every even/even corridor tile needs two of its four links open —
because a link tile always joins exactly two of them.

The ghost house is the smallest one the lattice allows: the centre tile alone,
walled in by the ring of eight around it with the top link left as the door.
Only the ghost next in line is drawn waiting in it, so four sprites never pile
onto the same tile. Corners come in two kinds and are rounded differently. Where two open sides of
a wall meet, the outline turns through a quarter circle and the tile's corner
outside it drops back to the background — so the end of a one-tile wall is a
cap, not a square. An inside corner — both neighbours wall, the diagonal open
— is measured from that open tile instead of from either side, which rounds it
off on its own as the inset grows. Nothing is drawn from the corridor's side
to do it: the inset already stands the wall off the corridor, so the whole
turn happens inside the wall tiles and the corridor keeps its full width. The
border has only the first kind, all four of them rounding inwards.

`PM_TILE` in `pacman_core.h` sets the grid; `PM_WALL_LINE`, `PM_WALL_INSET`,
`PM_WALL_R`, `PM_BORDER_GAP` and `PM_SPRITE` in `pacman_render.h` set the
drawing. Pellets,
the ghost house door, the power pellets and the sprites all scale off
`PM_TILE` rather than being hardcoded — `PM_SPRITE` only has to keep
`PM_TILE`'s parity so the sprite centres on a whole pixel.

| | |
|---|---|
| flash | ~9 KB |
| RAM | ~10 KB static (mostly a 9.6 KB blit scratch buffer and the pathfinder's arrays) |
| SPI traffic | ~3800 pixels/frame ≈ 7.4 KB/frame, about 225 KB/s at 30 fps — a fifth of what a 20 MHz link carries |
| LVGL widgets | none — the status screen is an empty `lv_obj` |

## Using it

Add the module to your zmk-config's `config/west.yml`:

```yaml
manifest:
  remotes:
    - name: zmkfirmware
      url-base: https://github.com/zmkfirmware
    - name: joaopedropio
      url-base: https://github.com/joaopedropio
  projects:
    - name: zmk
      remote: zmkfirmware
      revision: main
      import: app/west.yml
    - name: zmk-pacman
      remote: joaopedropio
      revision: main
  self:
    path: config
```

(`name:` is the GitHub repository name — change it if you publish this
under a different one.)

Then build the dongle with the `pacman_adapter` shield, next to your
keyboard's own dongle shield, in `build.yaml`:

```yaml
include:
  - board: nice_nano_v2
    shield: my_keyboard_dongle pacman_adapter
```

That is the whole setup: the shield chooses the custom status screen, so the
animation starts by itself once the dongle boots.

> The `pacman_adapter` shield describes the same panel, buzzer pins and
> action button as `snake_adapter`, and it ships the same
> `zmk,behavior-dongle-action` behaviour. Use one adapter or the other — do
> not pull both modules into the same build, or the behaviour will be
> defined twice.

### The action button

The dongle's action button (P0.31) is wired up through the sideband kscan, so
no keymap change is needed. How long it is held decides which of three things
it does:

| Held | What it does |
|---|---|
| up to 300 ms | swaps between the game and the dashboard |
| 300-600 ms | steps to the next theme |
| past 600 ms | mutes, or unmutes |

The two thresholds are `CONFIG_PACMAN_THEME_THRESHOLD` and
`CONFIG_PACMAN_MUTE_THRESHOLD`. The mute is saved, so a dongle switched off
muted comes back muted, and it silences whatever is sounding at the time.
Unmuting chirps, since a mute that says nothing either way
gives you no way to tell which state you are in. Setting
`CONFIG_PACMAN_MUTE_THRESHOLD=0` leaves the button with the first two.

## Configuration

## Three screens

The panel is shared by three things, one at a time, and none of them is an
LVGL widget - they all draw straight to the display.

**The splash** goes up first: the wordmark, Pac-Man about to run into a ghost,
and who to blame. It stays for `PACMAN_SPLASH_FRAMES` ticks of
`PACMAN_SPLASH_INTERVAL` (two and a half seconds by default), which is also
what keeps LVGL's first flush of its own empty screen off the game.

**The game** is the maze, and it owns the whole panel.

**The dashboard** is a grid of slots: `PACMAN_INFO_SLOT_MODE` picks the layout
and `PACMAN_INFO_SLOT_1` … `_6` say what goes in each one - `connectivity`,
`layer`, `theme`, `wpm`, `modifiers`, `battery` or nothing. Its header is a
lap of the maze in miniature, a ring of pellets with Pac-Man running it and a
ghost a few steps behind, which is the one thing on the dongle that animates
while the game is not running. Batteries take the strip along the bottom, one,
two or three of them per `PACMAN_BATTERY_SLOTS`.

`PACMAN_DEFAULT_SCREEN` decides which of the last two comes up after the
splash. One key on the keymap swaps between them and cycles the theme - bind
`&dongle_action_behavior` to it, then tap it to swap screens or hold it past
`PACMAN_THEME_THRESHOLD` to step to the next theme. Themes are the eleven
colour schemes in `helpers/display.c`; the first is yours to set with
`PACMAN_THEME_*`, and the choice is remembered across reboots.

Themes colour the splash and the dashboard. The maze keeps the arcade's own
colours whatever theme is up - they are set separately, by the
`PACMAN_*_COLOR` options in the table below.

The screens, their fonts and the slot machinery are ported from
[snake-module](https://github.com/joaopedropio/snake-module): same widgets,
same slot layout, same drawing helpers, with the artwork and the palette
redrawn for this one. What was left behind is the buzzer: snake's sounds are
PWM square waves, and these go out of an I2S amplifier as sampled audio
instead. The button keeps snake's three presses, mute included.

Both screens can be looked at without flashing anything:

```sh
tools/uisim/build.sh /tmp/uisim
/tmp/uisim /tmp/frames        # splash.ppm and dashboard.ppm
```

That harness stubs Zephyr out and points the drawing helpers at a plain frame
buffer, so it shows the layout and the artwork but not the slot contents,
which come from ZMK state the host has none of.

All options live in `Kconfig` and are prefixed with `PACMAN_`. Put them in
your `config/<shield>.conf` (or the shield's `pacman_adapter.conf`):

| Option | Default | What it does |
|---|---|---|
| `CONFIG_PACMAN_ROTATE_DISPLAY` | `0` | Panel rotation: 0, 90, 180 or 270. Only the rotation you pick is compiled in. |
| `CONFIG_PACMAN_FRAME_INTERVAL` | `33` | Milliseconds per frame (33 ≈ 30 fps). |
| `CONFIG_PACMAN_SOUND` | `y` | Drive the I2S amplifier at all. `n` compiles the whole sound path out. |
| `CONFIG_PACMAN_SOUND_VOLUME` | `80` | How loud, 0 to 100. 100 is unity; a limiter catches the peaks. |
| `CONFIG_PACMAN_SOUND_CONNECT` | `y` | Chirp when a keyboard connects or drops off. |
| `CONFIG_PACMAN_MUTE_THRESHOLD` | `600` | Milliseconds of hold that mute and unmute. `0` disables the mute press. |
| `CONFIG_PACMAN_SOUND_BASS_FLOOR_HZ` | `0` | Notes below this are doubled until they clear it, for a speaker that cannot reproduce them. Off by default: it changes the voicing. |
| `CONFIG_PACMAN_SOUND_SAMPLE_RATE` | `16000` | Samples per second; the nRF I2S clock picks the closest it can hit. |
| `CONFIG_PACMAN_WPM_SPEED` | `y` | Speed the game up while you type. |
| `CONFIG_PACMAN_WPM_SLOW` / `_FAST` | `20` / `60` | WPM thresholds for the 3px and 5px gears, either side of the 4px default. |
| `CONFIG_PACMAN_BG_COLOR` | `000000` | Background. |
| `CONFIG_PACMAN_WALL_COLOR` | `2121de` | Maze wall outline. |
| `CONFIG_PACMAN_WALL_FILL_COLOR` | `00003c` | Inside of the wall tubes, and the margin behind the border line. Set it to `000000` for hollow walls. |
| `CONFIG_PACMAN_WALL_FLASH_COLOR` | `f8f8f8` | Wall colour while the maze flashes at the end of a level. |
| `CONFIG_PACMAN_HOUSE_COLOR` | `6d6dff` | Ghost house outline. |
| `CONFIG_PACMAN_DOOR_COLOR` | `ffb8ff` | Ghost house door. |
| `CONFIG_PACMAN_PELLET_COLOR` | `ffb897` | Pellets and power pellets. |
| `CONFIG_PACMAN_PACMAN_COLOR` | `ffee00` | Pac-Man. |
| `CONFIG_PACMAN_GHOST_0..3_COLOR` | `ff0000`, `ffb8ff`, `00ffff`, `ffb852` | The four ghosts. |
| `CONFIG_PACMAN_FRIGHT_COLOR` | `2121de` | A frightened ghost. |

Colours are plain `rrggbb` strings (a leading `#` or `0x` is fine) and are
converted to RGB565 once at boot.

## How it plays itself

Pac-Man decides at every tile centre, with a breadth-first search over the
9x9 maze:

1. While the ghosts are blue, head for the closest one.
2. Otherwise, if a hunting ghost is within 8 tiles, run for a power pellet.
3. Otherwise, take the shortest path to the closest pellet — refusing to
   step within 3 tiles of a hunting ghost (walking distance, not
   as-the-crow-flies), giving up a tile of that clearance at a time when no
   comfortable route exists, and dropping it entirely once he has gone five
   seconds without eating. A stalemate looks worse than a death.
4. If every route is cut off, take the turn that puts the most maze between
   him and the nearest ghost.

The ghosts use the arcade rules: one chases Pac-Man, one aims four tiles
ahead of him, one mirrors the first ghost around a point in front of him, and
one only closes in from a distance; they alternate scatter and chase, reverse
when a power pellet is eaten, and go back to the house as a pair of eyes
after being eaten. Each sits out one frame in five, which is what makes a
good run possible at all.

Everyone moves 4 pixels a frame, 5 while you type fast and 3 while you are
idle — 120 pixels a second at 30 fps, twice what the arcade ran at. A
power pellet is worth turning round for: Pac-Man picks up a pixel a frame
while the ghosts are blue and they drop to half speed, so he closes on them at
better than twice their pace. It costs nothing to draw — a faster sprite
sweeps a wider dirty rectangle but fewer of them overlap, so the pixel count
per frame comes out where it was.

Over a two-hour soak (200k frames at 30fps) Pac-Man clears a maze about every
26 seconds and gets caught about every 110 — 252 clears against 60 deaths. He
used to clear one every 75 seconds and die every 55; the speed is most of the
difference, and the rest is the power pellets, which now clear the board of
ghosts long enough to matter.

## Sound

The dongle had a piezo buzzer on P0.29 that this module never used.  Nothing
drives it any more - the node, the chosen and the PWM behind it are gone, so
it can stay soldered where it is and stay quiet - and a MAX98357A does the
sound instead: an I2S amplifier with no registers to set up, so three pins
carry the sound and a fourth shuts it up.

They run down the left header in the order the breakout's own pads do, so the
module solders on as one straight run:

| MAX98357A | nRF52840 | |
|---|---|---|
| LRC | P0.02 | word select |
| BCLK | P1.15 | bit clock |
| DIN | P1.13 | the samples |
| SD | P1.11 | held high while something is playing, low the rest of the time |
| GAIN | — | floating is 9 dB; tie it straight to GND for 15 dB |
| GND, Vin | GND, VCC | 3.3 V works; 5 V is louder if the board has it |

Those pins are `i2s0_default` and the `pacman_amp` node in
`pacman_adapter.overlay`, which is the only place to change them if your
wiring differs.

The game itself is silent.  Pac-Man munching, dying and clearing the maze all
went out of the speaker once, and none of it was worth hearing on a loop for
hours at a desk - a dongle that chirps every time a pellet is eaten is a
dongle you unplug.  What is left is the one thing worth interrupting you for:
a keyboard arriving, and a keyboard dropping off.

Behind them is still a small polyphonic synth rather than a beeper, and the
two chirps use about as little of it as anything could - one instrument, two
notes.  An instrument is two oscillators and an envelope, and what separates
them is how fast they die away, whether the wave is a sine or a triangle, and
how far out of tune the second oscillator is: an octave for the marimba, 2.76
for the bell, six cents for the pad, whose two drift in and out of phase and
give it its warmth.  The chime the chirps use is the plainest of them - a sine
with a quiet octave over it, eased in rather than struck.  All integer: 16.16
phases, Q15 envelopes, one 256-entry sine table.  Four voices are mixed, and
when a fifth note arrives the quietest gives way, not the oldest - what is
least missed is whatever is contributing least.

A tune has a priority, so a disconnect takes the voice off a connect and not
the other way about: which of the two you want to hear through the other is
not a close call.

### Getting it loud on a small speaker

Most of what was keeping this quiet was not the amplifier.  The volume knob
scales the sample amplitude, so it is linear in voltage: a config carrying
`CONFIG_PACMAN_SOUND_VOLUME=25` is 12 dB down before the speaker sees anything,
and on a small driver that is most of the way to inaudible.  80 is the default
now.

The rest was headroom.  Four voices can land on the same sample, so the old
answer was to keep a third of the range in reserve - which charged every sound
10 dB to protect the loudest instant of the loudest one.  A soft limiter does
that job instead: untouched below half scale, bent into what is left above it,
so a stacked chord leans on the curve where it used to clip flat.  The two
chirps are two notes each and never come near it.

Register and timbre are the rest of it, and the chirps have been wrong in both
directions.  A 20 mm cone radiates almost nothing below its own resonance, so
anything written down at C3 arrives as silence that has still spent mixing
headroom on the way: the first pair were built like music, a rolled arpeggio
landing on a bell over a pad at 131 and 196 Hz, and the pad was inaudible on
the speaker while dragging the notes that were audible down through the
limiter.  Moving them up to a marimba at 880 and 1319 Hz fixed that and
introduced the opposite fault.  A marimba here is a triangle wave, and a
triangle carries odd harmonics falling off as 1/n squared - so a note at
1319 Hz puts real energy at 4 and 6.6 kHz, which is both the band the ear is
sharpest in and the band a small cone peaks in.  It was short, and it stung.

What the chirps use now is a sine with one quiet octave over it and nothing
else, sitting on a fifth in the middle of the speaker's range instead of the
top, and taking 110 ms to reach level rather than 2.  Measured over the whole
chirp that moves 4.5% of the energy above 2.5 kHz down to 0.003%, and turns a
2 ms onset - a step, which is a click with a note behind it - into a 290 ms
swell with no audible beginning at all.  Timbre and attack were the whole of
it; the notes never were.

`CONFIG_PACMAN_SOUND_BASS_FLOOR_HZ` doubles any note below it until it clears,
keeping the intervals - a chord stays that chord, voiced higher.  It was the
last resort for the pads, and with the pads gone it has little left to do: the
lower chirp note is D5, at 587 Hz, which most small drivers manage.  It stays
for a score of your own that does go low.

Past that it is hardware, and hardware is the cheap 6 dB: `GAIN` tied straight
to GND instead of left floating, and 5 V into `Vin` rather than 3.3 V.  Both
cost nothing in waveform.

### Telling you the keyboard dropped

`CONFIG_PACMAN_SOUND_CONNECT=y` gives the halves the only sound the dongle
makes: two soft chime notes rising a fifth when one reports in, D5 up to A5,
and the same two falling when one goes away. The same pair of pitches in
opposite orders, which is about as easy as a pair gets to learn, and a fifth is
wide enough to read as a direction from across a room rather than as a wobble.
Each note takes 110 ms to reach level and the second enters while the first is
still ringing, so the pair swells into the room rather than tapping at it -
about 1.3 seconds end to end, and it peaks at just over half full scale.

Finding out is the awkward part. ZMK raises its split connection event on the
peripheral, and the dongle is the central, so it never sees one. What the
central does raise is a peripheral battery level of 0 on disconnect, and the
next real reading is the first thing heard from a half that has come back - so
that is the signal, the same one the battery readout on screen already trusts.
A half sitting at a genuine 0% reads as gone, which is the price of using it.

A disconnect outranks a connect, so a half that drops the instant another
reconnects is still the thing you hear. Nothing sounds at power-on, when the
halves connecting is the normal state of things rather than news.

The synth is portable C in `widgets/game/pacman_sfx.c` and the tunes are
written as notes in `tools/tunes.py`.  Nothing about it is Zephyr, so the same
code renders the sounds to .wav files to listen to before flashing:

```sh
tools/sfxsim/build.sh /tmp/sfxsim
/tmp/sfxsim /tmp/sounds 16000        # connect.wav and disconnect.wav
```

The third argument, if given, is the bass floor.  Neither chirp goes low
enough for it to bite, which is the point - it is there for a score of your
own that does.

`widgets/sound.c` is the part that has to know about Zephyr: it keeps blocks
of samples going to the I2S driver while a tune is sounding and stops the
clock when it is not, which is what keeps the amplifier from hissing between
sounds.

It runs above the display thread, which ZMK puts at priority 5.  Filling a
block is well under a millisecond of the sixteen it buys, so the maze loses
nothing it can notice; underneath the display it lost whole blocks every time
the screen was busy, and repainting the whole maze is 240x240 pixels down a
20 MHz SPI.  Eight blocks are in flight and four go over before the clock
starts, so a scheduling hiccup has 64 ms to sort itself out - and when one is
missed anyway the driver stops the transfer rather than gapping it, so the
stream is dropped back to ready and picked up from wherever the synth has got
to, instead of losing the rest of the tune.  Only that thread touches the synth; the game's timer asks for a tune
and reads back what the voice is doing, both through atomics.

`CONFIG_PACMAN_SOUND=n` compiles all of it out, and so does leaving the
amplifier out of the devicetree.

## Trying it without flashing

The game core and the renderer are plain C with no Zephyr or LVGL
dependencies, so they build and run on a host. The simulator blits into a
240x240 buffer, checks the invariants every frame (nobody inside a wall, and
the incremental redraw always matches a full repaint) and can dump PPM
frames:

```sh
tools/sim/build.sh /tmp/pacman-sim
/tmp/pacman-sim 3000                        # 100 seconds, invariants only
/tmp/pacman-sim 640 2 /tmp/frames 40        # frames, every-nth, dir, from, speed
ffmpeg -framerate 15 -i /tmp/frames/frame_%05d.ppm -vf palettegen=max_colors=64 /tmp/pal.png
ffmpeg -framerate 15 -i /tmp/frames/frame_%05d.ppm -i /tmp/pal.png \
    -lavfi paletteuse=dither=none /tmp/pacman.gif
```

Every other frame at 15 fps runs the gif at the speed the dongle plays it, and
starting at 40 skips the READY pause.  `docs/demo.gif` is that, with the
splash from `tools/uisim` held in front of it for the first two seconds.

## Layout

```
boards/shields/pacman_adapter/
├── pacman_adapter.overlay      panel, backlight and action button (same hardware as snake_adapter)
├── Kconfig.defconfig           display + LVGL defaults for the shield
├── custom_status_screen.c      hands ZMK an empty screen, starts the timer
└── widgets/
    ├── pacman.c                display device, palette, LVGL timer, WPM speed
    ├── action_button.c         swaps screens, cycles themes
    ├── splash.c                the wordmark, the chase and the credit
    ├── logo.c                  the dashboard's animated header
    ├── frames.c                the boxes the slots are drawn in
    ├── configuration.c         Kconfig into runtime settings
    ├── theme.c                 the colour schemes and which one is current
    ├── battery_status.c        \
    ├── output_status.c          | the slot widgets: what ZMK knows,
    ├── layer_status.c           | drawn into whichever slot holds it
    ├── wpm.c                    |
    ├── modifier.c              /
    ├── sound.c                 the I2S amplifier, and what to play when
    ├── pacman_art.h            splash sprites (generated, see tools/sprites.py)
    ├── helpers/
    │   ├── display.c           the drawing engine: bitmaps, text, rectangles, themes, slots
    │   ├── fonts.h             six pixel fonts
    │   └── settings.c          the theme, remembered across reboots
    └── game/
        ├── pacman_core.c       maze, Pac-Man's pathfinding, ghost AI, rounds
        ├── pacman_render.c     sprites, tiles and dirty-rectangle blitting
        ├── pacman_sfx.c        one square wave and a list of tones
        └── pacman_tunes.h      the tunes (generated, see tools/tunes.py)
src/, include/, dts/            the zmk,behavior-dongle-action behaviour
tools/sim/                      host simulator for the game
tools/uisim/                    host preview for the splash and the dashboard
tools/sfxsim/                   renders the sounds to .wav on the host
tools/sprites.py                regenerates the splash artwork
tools/tunes.py                  regenerates the tunes
```

## Credits

Hardware definition, dongle action behaviour and the general shape of the
module come from [snake-module](https://github.com/joaopedropio/snake-module)
by João Pedro. Pac-Man is © Bandai Namco; this is a hobby homage running on a
keyboard dongle.

MIT licensed.
