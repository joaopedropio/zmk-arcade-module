/*
 * Pac-Man dongle - game core (portable, no Zephyr/LVGL dependencies).
 *
 * The maze is a 12x12 tile grid of 20x20 pixel tiles, so it fills the
 * 240x240 dongle display exactly.  Nobody is holding a joystick: Pac-Man
 * picks his own way with a breadth first search towards the closest pellet
 * (avoiding tiles near the hunting ghosts, running for a power pellet when
 * he is cornered, hunting the ghosts while they are blue), and the ghosts
 * use the classic scatter/chase targeting rules.
 *
 * SPDX-License-Identifier: MIT
 */

#include "pacman_core.h"

/* '#' wall  '.' pellet  'o' power pellet  ' ' empty  'H' house wall
 * 'h' house inside  'D' house door
 *
 * 9x9 tiles of 26px.  No tile is spent on a border: the maze is walled in by a
 * line round the edge of the panel, so the whole grid is playfield and the
 * outer ring of tiles is the corridor that runs round it.
 *
 * Walls and corridors are both exactly one tile thick.  What guarantees it is
 * the lattice PM_ROWS/PM_COLS being odd sets up (see pacman_core.h): the 25
 * tiles on even row and even column are always corridor, the 16 on odd row and
 * odd column are always wall, and the 40 in between are links that may be
 * either.  Every 2x2 block therefore contains exactly one of each, so no 2x2
 * is all corridor (which would widen a corridor) and none is all wall (which
 * would thicken a wall).  Only the links are a design choice; everything else
 * is forced.
 *
 * There are no dead ends anywhere: a tile with one way in and out is a trap,
 * because Pac-Man cannot turn round ahead of a ghost, and every one of them
 * costs him a life sooner or later.  On the lattice that reduces to one rule -
 * every corridor tile on an even row and even column needs two of its four
 * links open - because a link tile always joins exactly two of them.
 *
 * The ghost house is the smallest one that can exist here: the single centre
 * tile, walled in by the ring of eight around it with the top link left as the
 * door.  Only one ghost is drawn waiting in it, the rest queue up unseen, so
 * four sprites never pile onto one tile.
 *
 * Row 4 is the tunnel: it leaves the maze at both ends (' ', no pellet) and
 * wraps round. */
static const char *const MAZE_ART[PM_ROWS] = {
    "o.......o",
    ".#.###.#.",
    ".#.....#.",
    ".#.HDH.#.",
    " ..HhH.. ",
    ".#.HHH.#.",
    ".#.....#.",
    ".#.###.#.",
    "o.......o",
};

/* the ghost house is the single tile in the middle of the maze */
#define HOUSE_EXIT_X 4
#define HOUSE_EXIT_Y 2
#define HOUSE_IN_Y   4

#define PAC_START_X  4
#define PAC_START_Y  6

#define FPS            30
#define READY_FRAMES   (FPS * 3 / 2)
#define DYING_FRAMES   (FPS * 2)
#define CLEARED_FRAMES (FPS * 2)
#define SCATTER_FRAMES (FPS * 7)
#define CHASE_FRAMES   (FPS * 20)
#define EYE_SPEED      5

/*
 * What a power pellet buys.  Pac-Man picks up a pixel a frame while the ghosts
 * are blue and they drop to half speed, the way the arcade does it - the
 * chase is only worth turning round for if catching them is easy while it
 * lasts.  Their one frame in five off comes off the halved speed rather than
 * instead of it, which is what keeps the two ratios apart: normal ghosts run
 * at 0.8 of their speed, frightened ones at about half that.
 */
#define PAC_FRIGHT_STEP(s)   ((s) + 1)
#define GHOST_FRIGHT_STEP(s) (((s) + 1) / 2)

struct ghost_start {
    uint8_t x, y;
    uint16_t release;
    pm_ghost_state state;
};

#define HOUSE_X 4
#define HOUSE_Y 4

static const struct ghost_start GHOST_START[PM_GHOSTS] = {
    {HOUSE_EXIT_X, HOUSE_EXIT_Y, 0, PM_G_OUT},   /* red   - starts on the prowl */
    {HOUSE_X, HOUSE_Y, FPS * 2, PM_G_HOUSE},     /* pink  */
    {HOUSE_X, HOUSE_Y, FPS * 5, PM_G_HOUSE},     /* cyan  */
    {HOUSE_X, HOUSE_Y, FPS * 9, PM_G_HOUSE},     /* orange */
};

/* scatter corners, one per ghost */
static const int8_t SCATTER_X[PM_GHOSTS] = {8, 0, 8, 0};
static const int8_t SCATTER_Y[PM_GHOSTS] = {0, 0, 8, 8};

/* ------------------------------------------------------------------ */
/* small helpers                                                       */
/* ------------------------------------------------------------------ */

static uint32_t rnd(pm_game *g) {
    g->rng = g->rng * 1664525u + 1013904223u;
    return g->rng >> 8;
}

static int wrap_x(int x) {
    while (x < 0) {
        x += PM_COLS;
    }
    while (x >= PM_COLS) {
        x -= PM_COLS;
    }
    return x;
}

static bool open_to_pac(uint8_t t) {
    return t == PM_T_PATH || t == PM_T_PELLET || t == PM_T_POWER;
}

static bool open_to_ghost(uint8_t t) {
    return open_to_pac(t) || t == PM_T_DOOR || t == PM_T_HOUSE;
}

static int actor_tx(const pm_actor *a) {
    int cx = a->x + PM_TILE / 2;
    return wrap_x(cx >= 0 ? cx / PM_TILE : (cx - PM_TILE + 1) / PM_TILE);
}

static int actor_ty(const pm_actor *a) {
    return (a->y + PM_TILE / 2) / PM_TILE;
}

static bool aligned(const pm_actor *a) {
    return (a->x % PM_TILE) == 0 && (a->y % PM_TILE) == 0;
}

static pm_dir opposite(pm_dir d) {
    switch (d) {
    case PM_UP: return PM_DOWN;
    case PM_DOWN: return PM_UP;
    case PM_LEFT: return PM_RIGHT;
    case PM_RIGHT: return PM_LEFT;
    default: return PM_NODIR;
    }
}

/* moves a target point, which the arcade lets wander outside the maze */
static void step_free(pm_dir d, int *x, int *y) {
    switch (d) {
    case PM_UP: (*y)--; break;
    case PM_DOWN: (*y)++; break;
    case PM_LEFT: (*x)--; break;
    case PM_RIGHT: (*x)++; break;
    default: break;
    }
    *x = wrap_x(*x);
}

/*
 * Steps one tile for anything that actually moves.  A row only wraps if it
 * runs off into a tunnel: everywhere else the edge of the maze is the outer
 * wall, which is what the border line drawn round the panel represents.
 */
static bool step_tile(const pm_game *g, pm_dir d, int *x, int *y) {
    switch (d) {
    case PM_UP: (*y)--; break;
    case PM_DOWN: (*y)++; break;
    case PM_LEFT: (*x)--; break;
    case PM_RIGHT: (*x)++; break;
    default: break;
    }
    if (*y < 0 || *y >= PM_ROWS) {
        return false;
    }
    if (*x < 0 || *x >= PM_COLS) {
        if (!((g->tunnel_rows >> *y) & 1u)) {
            return false;
        }
        *x = wrap_x(*x);
    }
    return true;
}

/* move the actor along its direction, never overshooting a tile boundary */
static void advance(pm_actor *a, int speed) {
    int pos = (a->dir == PM_LEFT || a->dir == PM_RIGHT) ? a->x : a->y;
    int m = pos % PM_TILE;
    if (m < 0) {
        m += PM_TILE;
    }
    int rem;
    if (a->dir == PM_RIGHT || a->dir == PM_DOWN) {
        rem = PM_TILE - m;
    } else {
        rem = (m == 0) ? PM_TILE : m;
    }
    int step = speed < rem ? speed : rem;

    switch (a->dir) {
    case PM_UP: a->y -= step; break;
    case PM_DOWN: a->y += step; break;
    case PM_LEFT: a->x -= step; break;
    case PM_RIGHT: a->x += step; break;
    default: break;
    }

    if (a->x <= -PM_TILE) {
        a->x += PM_WIDTH;
    } else if (a->x >= PM_WIDTH) {
        a->x -= PM_WIDTH;
    }
}

static int dist_x(int a, int b) {
    int d = a - b;
    if (d < 0) {
        d = -d;
    }
    return d > PM_COLS / 2 ? PM_COLS - d : d;
}


/* ------------------------------------------------------------------ */
/* breadth first search used by both Pac-Man and the returning eyes    */
/* ------------------------------------------------------------------ */

enum {
    BFS_PELLET = 0,   /* any remaining pellet */
    BFS_POWER,        /* power pellets only */
    BFS_BLUE_GHOST,   /* a frightened ghost */
    BFS_TILE,         /* one specific tile */
};

#define PM_CELLS (PM_ROWS * PM_COLS)
#define PM_FAR   255
#define PM_SCARY 3    /* tiles this close to a hunting ghost are off limits */

static uint8_t bfs_seen[PM_CELLS];
static uint8_t bfs_first[PM_CELLS];
static uint16_t bfs_queue[PM_CELLS];
static uint8_t ghost_dist[PM_CELLS];

/* walking distance from every tile to the closest ghost that can still bite,
 * capped so the search stays short (everything further away is PM_FAR) */
static void map_hunters(const pm_game *g) {
    for (int i = 0; i < PM_CELLS; i++) {
        ghost_dist[i] = PM_FAR;
    }
    if (g->fright > 0) {
        return;
    }

    int head = 0, tail = 0;
    for (int i = 0; i < PM_GHOSTS; i++) {
        const pm_ghost *gh = &g->ghosts[i];
        if (gh->state != PM_G_OUT) {
            continue;
        }
        int cell = actor_ty(&gh->actor) * PM_COLS + actor_tx(&gh->actor);
        if (cell < 0 || cell >= PM_CELLS || ghost_dist[cell] == 0) {
            continue;
        }
        ghost_dist[cell] = 0;
        bfs_queue[tail++] = (uint16_t)cell;
    }

    while (head < tail) {
        int cell = bfs_queue[head++];
        uint8_t d = ghost_dist[cell];
        if (d >= 10) {
            continue;
        }
        int cx = cell % PM_COLS;
        int cy = cell / PM_COLS;
        for (pm_dir dir = 0; dir < PM_DIRS; dir++) {
            int nx = cx, ny = cy;
            if (!step_tile(g, dir, &nx, &ny) || !open_to_pac(g->tiles[ny][nx])) {
                continue;
            }
            int ncell = ny * PM_COLS + nx;
            if (ghost_dist[ncell] != PM_FAR) {
                continue;
            }
            ghost_dist[ncell] = (uint8_t)(d + 1);
            bfs_queue[tail++] = (uint16_t)ncell;
        }
    }
}

static int hunter_distance(const pm_game *g, int x, int y) {
    (void)g;
    return ghost_dist[y * PM_COLS + x];
}

static bool bfs_is_target(const pm_game *g, int mode, int x, int y, int tx, int ty) {
    switch (mode) {
    case BFS_PELLET:
        return g->tiles[y][x] == PM_T_PELLET || g->tiles[y][x] == PM_T_POWER;
    case BFS_POWER:
        return g->tiles[y][x] == PM_T_POWER;
    case BFS_BLUE_GHOST:
        for (int i = 0; i < PM_GHOSTS; i++) {
            const pm_ghost *gh = &g->ghosts[i];
            if (gh->state == PM_G_OUT && actor_tx(&gh->actor) == x && actor_ty(&gh->actor) == y) {
                return true;
            }
        }
        return false;
    default:
        return x == tx && y == ty;
    }
}

/* `keep` is how many tiles of clearance from a hunting ghost the route has to
 * hold on to; 0 walks through anything. */
static pm_dir bfs_dir(const pm_game *g, int sx, int sy, int mode, int tx, int ty, int keep,
                      bool as_ghost) {
    for (int i = 0; i < PM_CELLS; i++) {
        bfs_seen[i] = 0;
    }

    int head = 0, tail = 0;
    bfs_seen[sy * PM_COLS + sx] = 1;
    bfs_first[sy * PM_COLS + sx] = PM_NODIR;
    bfs_queue[tail++] = (uint16_t)(sy * PM_COLS + sx);

    while (head < tail) {
        int cell = bfs_queue[head++];
        int cx = cell % PM_COLS;
        int cy = cell / PM_COLS;

        if (cell != sy * PM_COLS + sx && bfs_is_target(g, mode, cx, cy, tx, ty)) {
            return (pm_dir)bfs_first[cell];
        }

        for (pm_dir d = 0; d < PM_DIRS; d++) {
            int nx = cx, ny = cy;
            if (!step_tile(g, d, &nx, &ny)) {
                continue;
            }
            uint8_t t = g->tiles[ny][nx];
            if (!(as_ghost ? open_to_ghost(t) : open_to_pac(t))) {
                continue;
            }
            int ncell = ny * PM_COLS + nx;
            if (bfs_seen[ncell] || (keep > 0 && ghost_dist[ncell] <= keep)) {
                continue;
            }
            bfs_seen[ncell] = 1;
            bfs_first[ncell] = (cell == sy * PM_COLS + sx) ? (uint8_t)d : bfs_first[cell];
            bfs_queue[tail++] = (uint16_t)ncell;
        }
    }
    return PM_NODIR;
}

/* ------------------------------------------------------------------ */
/* Pac-Man                                                             */
/* ------------------------------------------------------------------ */

static bool can_go(const pm_game *g, const pm_actor *a, pm_dir d, bool as_ghost) {
    int x = actor_tx(a), y = actor_ty(a);
    if (!step_tile(g, d, &x, &y)) {
        return false;
    }
    uint8_t t = g->tiles[y][x];
    return as_ghost ? open_to_ghost(t) : open_to_pac(t);
}

static void pac_choose(pm_game *g) {
    int x = actor_tx(&g->pac);
    int y = actor_ty(&g->pac);
    pm_dir d = PM_NODIR;

    map_hunters(g);

    if (g->fright > FPS / 2) {
        d = bfs_dir(g, x, y, BFS_BLUE_GHOST, 0, 0, 0, false);
    }
    if (d == PM_NODIR && hunter_distance(g, x, y) <= 8) {
        d = bfs_dir(g, x, y, BFS_POWER, 0, 0, PM_SCARY, false);
    }
    /*
     * Walk to the closest pellet, giving up a tile of clearance at a time
     * when there is no comfortable route.  One-tile-thick walls leave the
     * maze open enough that four ghosts can cover every safe approach at
     * once, and backing off from all of them just leaves him circling empty
     * corridors, so he starts taking risks instead of stopping.  Once he has
     * gone hungry for a while he commits to the shortest route whatever is
     * standing in it - dying is recoverable, a stalemate is not.
     */
    int floor_keep = 2;
    if (g->hungry > FPS * 2) {
        floor_keep = 1;
    }
    if (g->hungry > FPS * 5) {
        floor_keep = 0;
    }
    for (int keep = PM_SCARY; d == PM_NODIR && keep >= floor_keep; keep--) {
        d = bfs_dir(g, x, y, BFS_PELLET, 0, 0, keep, false);
    }
    if (d == PM_NODIR) {
        /* boxed in: head for whichever way puts the most maze between us */
        int best = -1;
        for (pm_dir c = 0; c < PM_DIRS; c++) {
            if (!can_go(g, &g->pac, c, false)) {
                continue;
            }
            int nx = x, ny = y;
            if (!step_tile(g, c, &nx, &ny)) {
                continue;
            }
            int room = ghost_dist[ny * PM_COLS + nx];
            if (c == g->pac.dir) {
                room++; /* prefer not to dither on the spot */
            }
            if (room > best) {
                best = room;
                d = c;
            }
        }
    }
    if (d == PM_NODIR) {
        return;
    }
    g->pac.dir = d;
}

static void pac_eat(pm_game *g) {
    int x = actor_tx(&g->pac);
    int y = actor_ty(&g->pac);
    uint8_t t = g->tiles[y][x];

    if (t == PM_T_PELLET) {
        g->tiles[y][x] = PM_T_PATH;
        g->score += 10;
        g->pellets_left--;
        g->hungry = 0;
    } else if (t == PM_T_POWER) {
        g->tiles[y][x] = PM_T_PATH;
        g->score += 50;
        g->pellets_left--;
        g->hungry = 0;
        g->fright_eaten = 0;
        int f = FPS * 8 - g->level * FPS / 2;
        g->fright = (uint16_t)(f < FPS * 2 ? FPS * 2 : f);
        for (int i = 0; i < PM_GHOSTS; i++) {
            pm_ghost *gh = &g->ghosts[i];
            if (gh->state == PM_G_OUT && aligned(&gh->actor)) {
                gh->actor.dir = opposite(gh->actor.dir);
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* ghosts                                                              */
/* ------------------------------------------------------------------ */

static void ghost_target(const pm_game *g, int i, int *tx, int *ty) {
    const pm_ghost *gh = &g->ghosts[i];
    int px = actor_tx(&g->pac);
    int py = actor_ty(&g->pac);

    if (g->scatter) {
        *tx = SCATTER_X[i];
        *ty = SCATTER_Y[i];
        return;
    }

    switch (i) {
    case 0: /* straight for Pac-Man */
        *tx = px;
        *ty = py;
        break;
    case 1: { /* four tiles in front of him */
        int ax = px, ay = py;
        for (int s = 0; s < 4; s++) {
            step_free(g->pac.dir, &ax, &ay);
        }
        *tx = ax;
        *ty = ay;
        break;
    }
    case 2: { /* mirror of the red ghost around a point in front of Pac-Man */
        int ax = px, ay = py;
        for (int s = 0; s < 2; s++) {
            step_free(g->pac.dir, &ax, &ay);
        }
        *tx = 2 * ax - actor_tx(&g->ghosts[0].actor);
        *ty = 2 * ay - actor_ty(&g->ghosts[0].actor);
        break;
    }
    default: { /* shy: chases from afar, backs off when close */
        int dx = dist_x(px, actor_tx(&gh->actor));
        int dy = py - actor_ty(&gh->actor);
        if (dy < 0) {
            dy = -dy;
        }
        if (dx + dy > 8) {
            *tx = px;
            *ty = py;
        } else {
            *tx = SCATTER_X[i];
            *ty = SCATTER_Y[i];
        }
        break;
    }
    }
}

static void ghost_choose(pm_game *g, int i) {
    pm_ghost *gh = &g->ghosts[i];
    pm_dir back = opposite(gh->actor.dir);

    pm_dir opts[PM_DIRS];
    int n = 0;
    for (pm_dir d = 0; d < PM_DIRS; d++) {
        if (d != back && can_go(g, &gh->actor, d, false)) {
            opts[n++] = d;
        }
    }
    if (n == 0) {
        if (can_go(g, &gh->actor, back, false)) {
            gh->actor.dir = back;
        }
        return;
    }
    if (g->fright > 0) {
        gh->actor.dir = opts[rnd(g) % n];
        return;
    }

    int tx, ty;
    ghost_target(g, i, &tx, &ty);

    int best = 0;
    long best_d = -1;
    for (int k = 0; k < n; k++) {
        int nx = actor_tx(&gh->actor), ny = actor_ty(&gh->actor);
        if (!step_tile(g, opts[k], &nx, &ny)) {
            continue;
        }
        long dx = nx - tx, dy = ny - ty;
        long d = dx * dx + dy * dy;
        if (best_d < 0 || d < best_d) {
            best_d = d;
            best = k;
        }
    }
    gh->actor.dir = opts[best];
}

static void ghost_step(pm_game *g, int i) {
    pm_ghost *gh = &g->ghosts[i];
    pm_actor *a = &gh->actor;

    switch (gh->state) {
    case PM_G_HOUSE:
        if (gh->release > 0) {
            gh->release--;
            int base = (a->y / PM_TILE) * PM_TILE;
            a->y = (int16_t)(base + (((g->frame >> 3) & 1) ? 2 : 0));
            if (gh->release == 0) {
                a->y = (int16_t)base;
                a->dir = PM_UP;
            }
            break;
        }
        if (aligned(a)) {
            int tx = actor_tx(a);
            if (actor_ty(a) <= HOUSE_EXIT_Y) {
                gh->state = PM_G_OUT;
                a->dir = (rnd(g) & 1) ? PM_LEFT : PM_RIGHT;
                break;
            }
            a->dir = (tx == HOUSE_EXIT_X) ? PM_UP : (tx < HOUSE_EXIT_X ? PM_RIGHT : PM_LEFT);
        }
        advance(a, g->ghost_speed);
        break;

    case PM_G_OUT:
        if (aligned(a)) {
            ghost_choose(g, i);
        }
        /*
         * A hair slower than Pac-Man, so a clean run can outpace them: each
         * ghost sits out one frame in five.  A coarse grid gives Pac-Man fewer
         * decisions per second to escape with, which is what this handicap is
         * paying for, so it is a share of their speed rather than a fixed
         * number of pixels.
         */
        if ((g->frame % 5) != (unsigned)i) {
            advance(a, g->fright > 0 ? GHOST_FRIGHT_STEP(g->ghost_speed) : g->ghost_speed);
        }
        break;

    case PM_G_EYES:
        if (aligned(a)) {
            int x = actor_tx(a), y = actor_ty(a);
            if (x == HOUSE_EXIT_X && y == HOUSE_EXIT_Y) {
                gh->state = PM_G_ENTER;
                a->dir = PM_DOWN;
                break;
            }
            pm_dir d = bfs_dir(g, x, y, BFS_TILE, HOUSE_EXIT_X, HOUSE_EXIT_Y, 0, true);
            if (d != PM_NODIR) {
                a->dir = d;
            }
        }
        advance(a, EYE_SPEED);
        break;

    case PM_G_ENTER:
        a->dir = PM_DOWN;
        advance(a, g->ghost_speed);
        if (aligned(a) && actor_ty(a) >= HOUSE_IN_Y) {
            gh->state = PM_G_HOUSE;
            gh->release = FPS * 2;
        }
        break;
    }
}

/* ------------------------------------------------------------------ */
/* rounds and levels                                                   */
/* ------------------------------------------------------------------ */

static void load_maze(pm_game *g) {
    g->pellets_left = 0;
    g->tunnel_rows = 0;
    for (int y = 0; y < PM_ROWS; y++) {
        for (int x = 0; x < PM_COLS; x++) {
            uint8_t t;
            switch (MAZE_ART[y][x]) {
            case '#': t = PM_T_WALL; break;
            case '.': t = PM_T_PELLET; break;
            case 'o': t = PM_T_POWER; break;
            case 'H': t = PM_T_HWALL; break;
            case 'h': t = PM_T_HOUSE; break;
            case 'D': t = PM_T_DOOR; break;
            default: t = PM_T_PATH; break;
            }
            if (t == PM_T_PELLET || t == PM_T_POWER) {
                g->pellets_left++;
            }
            /* a blank at either end of a row is a tunnel mouth: the renderer
             * has to leave a gap in the border there, and it cannot tell one
             * from an eaten pellet once the round is under way */
            if ((x == 0 || x == PM_COLS - 1) && MAZE_ART[y][x] == ' ') {
                g->tunnel_rows |= 1u << y;
            }
            g->tiles[y][x] = t;
        }
    }
}

static void reset_round(pm_game *g) {
    g->pac.x = PAC_START_X * PM_TILE;
    g->pac.y = PAC_START_Y * PM_TILE;
    g->pac.dir = PM_LEFT;

    for (int i = 0; i < PM_GHOSTS; i++) {
        pm_ghost *gh = &g->ghosts[i];
        gh->actor.x = (int16_t)(GHOST_START[i].x * PM_TILE);
        gh->actor.y = (int16_t)(GHOST_START[i].y * PM_TILE);
        gh->actor.dir = (i & 1) ? PM_LEFT : PM_RIGHT;
        gh->state = GHOST_START[i].state;
        gh->release = GHOST_START[i].release;
        gh->bob = 0;
    }

    g->fright = 0;
    g->fright_eaten = 0;
    g->hungry = 0;
    g->death = 0;
    g->mouth = 0;
    g->scatter = true;
    g->mode_timer = SCATTER_FRAMES;
    g->hide_ghosts = false;
    g->flash = false;
    g->phase = PM_READY;
    g->phase_timer = READY_FRAMES;
    g->redraw = true;
}

static void new_game(pm_game *g) {
    g->score = 0;
    g->lives = 3;
    g->level = 1;
    load_maze(g);
    reset_round(g);
}

/* ------------------------------------------------------------------ */
/* public API                                                          */
/* ------------------------------------------------------------------ */

void pm_init(pm_game *g, uint32_t seed) {
    g->rng = seed ? seed : 1u;
    g->frame = 0;
    g->pac_speed = 4;
    g->ghost_speed = 4;
    new_game(g);
}

void pm_set_speed(pm_game *g, uint8_t pac_px, uint8_t ghost_px) {
    g->pac_speed = pac_px < 1 ? 1 : (pac_px > 5 ? 5 : pac_px);
    g->ghost_speed = ghost_px < 1 ? 1 : (ghost_px > 5 ? 5 : ghost_px);
}

/*
 * The house is one tile wide, so the ghosts still waiting in it would all be
 * drawn on top of each other.  Only the one next in line is shown - the rest
 * queue up unseen and appear as they leave.  Release timers are distinct at
 * the start of a round but two ghosts eaten together come home with the same
 * one, so the index breaks the tie.
 */
bool pm_ghost_visible(const pm_game *g, int i) {
    const pm_ghost *gh = &g->ghosts[i];
    if (gh->state != PM_G_HOUSE || gh->release == 0) {
        return true;
    }
    for (int j = 0; j < PM_GHOSTS; j++) {
        const pm_ghost *other = &g->ghosts[j];
        if (j == i || other->state != PM_G_HOUSE) {
            continue;
        }
        if (other->release < gh->release || (other->release == gh->release && j < i)) {
            return false;
        }
    }
    return true;
}

bool pm_power_visible(const pm_game *g) {
    return ((g->frame / 10) & 1) == 0;
}

bool pm_fright_flashing(const pm_game *g) {
    return g->fright > 0 && g->fright < FPS * 2 && ((g->frame >> 2) & 1);
}

static void play_step(pm_game *g) {
    if (g->fright > 0) {
        g->fright--;
    } else if (g->mode_timer > 0 && --g->mode_timer == 0) {
        g->scatter = !g->scatter;
        g->mode_timer = g->scatter ? SCATTER_FRAMES : CHASE_FRAMES;
    }

    if (aligned(&g->pac)) {
        pac_choose(g);
    }
    if (!aligned(&g->pac) || can_go(g, &g->pac, g->pac.dir, false)) {
        advance(&g->pac, g->fright > 0 ? PAC_FRIGHT_STEP(g->pac_speed) : g->pac_speed);
    }
    if (aligned(&g->pac)) {
        pac_eat(g);
    }
    if ((g->frame % 3) == 0) {
        g->mouth = (uint8_t)((g->mouth + 1) & 3);
    }

    for (int i = 0; i < PM_GHOSTS; i++) {
        ghost_step(g, i);
    }

    for (int i = 0; i < PM_GHOSTS; i++) {
        pm_ghost *gh = &g->ghosts[i];
        if (gh->state != PM_G_OUT) {
            continue;
        }
        int dx = g->pac.x - gh->actor.x;
        if (dx > PM_WIDTH / 2) {
            dx -= PM_WIDTH;
        } else if (dx < -PM_WIDTH / 2) {
            dx += PM_WIDTH;
        }
        int dy = g->pac.y - gh->actor.y;
        if (dx < 0) {
            dx = -dx;
        }
        if (dy < 0) {
            dy = -dy;
        }
        /*
         * Overlap rather than a shared tile, so a ghost passing the other way
         * counts.  The window is 13px across and the two of them close at most
         * 11 in a frame (both clamped to 5, and Pac-Man a pixel over that while
         * they are blue), so neither can step through the other unseen.
         */
        if (dx >= 7 || dy >= 7) {
            continue;
        }

        if (g->fright > 0) {
            gh->state = PM_G_EYES;
            g->fright_eaten++;
            g->score += 200u << (g->fright_eaten > 4 ? 3 : g->fright_eaten - 1);
        } else {
            g->phase = PM_DYING;
            g->phase_timer = DYING_FRAMES;
            g->death = 0;
            g->hide_ghosts = true;
            g->redraw = true;
            return;
        }
    }

    if (g->pellets_left == 0) {
        g->phase = PM_CLEARED;
        g->phase_timer = CLEARED_FRAMES;
        g->hide_ghosts = true;
        g->redraw = true;
    }
}

void pm_step(pm_game *g) {
    g->frame++;

    switch (g->phase) {
    case PM_READY:
        if (g->phase_timer > 0 && --g->phase_timer == 0) {
            g->phase = PM_PLAY;
        }
        break;

    case PM_PLAY:
        if (g->hungry < 0xFFFF) {
            g->hungry++;
        }
        play_step(g);
        break;

    case PM_DYING:
        g->death = (uint8_t)((DYING_FRAMES - g->phase_timer) / 5);
        if (g->phase_timer > 0 && --g->phase_timer == 0) {
            if (g->lives > 0) {
                g->lives--;
            }
            if (g->lives == 0) {
                new_game(g);
            } else {
                reset_round(g);
            }
        }
        break;

    case PM_CLEARED:
        if ((g->phase_timer % 10) == 0) {
            g->flash = !g->flash;
            g->redraw = true;
        }
        if (g->phase_timer > 0 && --g->phase_timer == 0) {
            if (g->level < 255) {
                g->level++;
            }
            load_maze(g);
            reset_round(g);
        }
        break;
    }
}
