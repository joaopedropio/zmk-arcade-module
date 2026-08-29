# Pac-Man Dongle Module 🟡

A self-playing Pac-Man for the [snake dongle](https://github.com/joaopedropio/snake-dongle)
hardware: a ZMK module that turns the dongle's 240x240 ST7789V panel into a
little arcade cabinet. Nobody drives it — Pac-Man hunts pellets on his own,
runs for a power pellet when he is cornered, chases the blue ghosts, and the
four ghosts use the classic scatter/chase targeting rules.

<img src="docs/demo.gif" width="320" alt="Pac-Man playing itself on the dongle display"/>

Same idea (and the same hardware definition) as the
[snake module](https://github.com/joaopedropio/snake-module), just a different game.

## What it costs

The game is drawn straight to the panel — no LVGL objects, no full frame
buffer, only the tiles that actually changed are pushed over SPI.

Proportions follow the arcade rather than the tile grid. The maze is 9x9
tiles of 26px; walls and corridors are both exactly one tile thick, each wall
drawn as a 1px tube rather than a filled slab, with corners rounded to
`PM_WALL_R`. Sprites are 24px boxes centred on their tile — on a 240px panel read at arm's length, how big the characters
are matters more than how elaborate the maze is, so the grid is kept as coarse
as it can be while still holding a maze.

No tile is spent on a border: the maze is walled in by a line drawn round the
edge of the panel, with a gap wherever a row runs off into the tunnel, so the
whole grid is playfield and the outer ring of tiles is a corridor. When the
grid does not divide the panel exactly the leftover margin becomes that line;
when it divides exactly the line is drawn over the outermost pixels of the
maze instead — and because `paint()` draws it, a dirty rectangle along the
edge repaints it too. At 9x26 the grid is 234px, so the line lives in the 3px
margin.

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
cap, not a square. The inside of an elbow, where a corridor turns between two
perpendicular walls, has to be filleted from the open tile instead, because
rounding it means bulging into the corridor; corridors are a whole tile wide,
so it only bites where a sprite's circle does not reach. The outer wall is
exempt: it is the border line painted round the panel, `PM_MARGIN` thick
rather than a tile, so an arc tangent to a tile face would not meet it.

`PM_TILE` in `pacman_core.h` sets the grid; `PM_WALL_LINE`, `PM_WALL_R` and
`PM_SPRITE` in `pacman_render.h` set the drawing. Pellets,
the ghost house door, the power pellets and the sprites all scale off
`PM_TILE` rather than being hardcoded — `PM_SPRITE` only has to keep
`PM_TILE`'s parity so the sprite centres on a whole pixel.

| | |
|---|---|
| flash | ~9 KB |
| RAM | ~10 KB static (mostly a 9.6 KB blit scratch buffer and the pathfinder's arrays) |
| SPI traffic | ~1950 pixels/frame ≈ 3.8 KB/frame, about 115 KB/s at 30 fps |
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
no keymap change is needed: a press pauses the animation, another press
resumes it.

## Configuration

All options live in `Kconfig` and are prefixed with `PACMAN_`. Put them in
your `config/<shield>.conf` (or the shield's `pacman_adapter.conf`):

| Option | Default | What it does |
|---|---|---|
| `CONFIG_PACMAN_ROTATE_DISPLAY` | `0` | Panel rotation: 0, 90, 180 or 270. Only the rotation you pick is compiled in. |
| `CONFIG_PACMAN_FRAME_INTERVAL` | `33` | Milliseconds per frame (33 ≈ 30 fps). |
| `CONFIG_PACMAN_START_DELAY` | `600` | Milliseconds before the first frame, so LVGL's initial flush cannot wipe the maze. |
| `CONFIG_PACMAN_WPM_SPEED` | `y` | Speed the game up while you type. |
| `CONFIG_PACMAN_WPM_SLOW` / `_FAST` | `20` / `60` | WPM thresholds for the slow and fast gears. |
| `CONFIG_PACMAN_BG_COLOR` | `000000` | Background. |
| `CONFIG_PACMAN_WALL_COLOR` | `2121de` | Maze wall outline. |
| `CONFIG_PACMAN_WALL_FILL_COLOR` | `000000` | Inside of the wall tubes. Set it to something like `00003c` to get filled walls back. |
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

Over a two-hour soak (200k frames at 30fps) Pac-Man clears a maze about every
90 seconds and gets caught about every 50 — 73 clears against 130 deaths. The
9x9 maze is tighter than the old 12x12 one, so there is less room to dodge in
and the animation turns over faster.

## Trying it without flashing

The game core and the renderer are plain C with no Zephyr or LVGL
dependencies, so they build and run on a host. The simulator blits into a
240x240 buffer, checks the invariants every frame (nobody inside a wall, and
the incremental redraw always matches a full repaint) and can dump PPM
frames:

```sh
tools/sim/build.sh /tmp/pacman-sim
/tmp/pacman-sim 3000                        # 100 seconds, invariants only
/tmp/pacman-sim 900 2 /tmp/frames 0 2       # frames, every-nth, dir, from, speed
ffmpeg -framerate 15 -i /tmp/frames/frame_%05d.ppm /tmp/pacman.gif
```

## Layout

```
boards/shields/pacman_adapter/
├── pacman_adapter.overlay      panel, backlight and action button (same hardware as snake_adapter)
├── Kconfig.defconfig           display + LVGL defaults for the shield
├── custom_status_screen.c      hands ZMK an empty screen, starts the timer
└── widgets/
    ├── pacman.c                display device, palette, LVGL timer, WPM speed
    ├── action_button.c         pause / resume
    └── game/
        ├── pacman_core.c       maze, Pac-Man's pathfinding, ghost AI, rounds
        └── pacman_render.c     sprites, tiles and dirty-rectangle blitting
src/, include/, dts/            the zmk,behavior-dongle-action behaviour
tools/sim/                      host simulator
```

## Credits

Hardware definition, dongle action behaviour and the general shape of the
module come from [snake-module](https://github.com/joaopedropio/snake-module)
by João Pedro. Pac-Man is © Bandai Namco; this is a hobby homage running on a
keyboard dongle.

MIT licensed.
