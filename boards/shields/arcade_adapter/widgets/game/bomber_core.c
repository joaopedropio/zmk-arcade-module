/*
 * Bomberman dongle - game core (portable).
 *
 * SPDX-License-Identifier: MIT
 */

#include "bomber_core.h"

/* ------------------------------------------------------------------ */
/* the numbers the pilot is made of                                    */
/* ------------------------------------------------------------------ */

/*
 * How the bomber prices what it can reach.  These are points, and BB_V_STEP
 * converts a point into frames of walking, which is the only way two unlike
 * things - a wall worth opening and an enemy worth burning - can be compared
 * at all.  At six, one brick is worth about fifteen cells of walking, so a
 * cell with three bricks on it will always beat a nearer cell with one, and a
 * tie is broken by distance.  Drop it and the bomber bombs whatever is under
 * its feet and never crosses the board; raise it and it walks past open walls
 * on its way to a marginally better one, which reads as indecision.
 */
#define BB_V_BRICK 10
#define BB_V_FOE   30
#define BB_V_NEAR  8   /* one a cell or two outside the arms, which may wander in */
#define BB_V_GOAL  120 /* the door, once the board is empty of enemies */
#define BB_V_STEP  6

/*
 * And what it pays to be somewhere an enemy is not.  This is a bonus on open
 * ground rather than a penalty near an enemy, for the same reason the
 * crossing's is: a penalty makes standing still the cheapest thing there is,
 * and a bomber that will not commit to a corridor never breaks a wall.
 *
 * The cap is what stops it being the whole plan.  Past four cells of daylight
 * one more is worth nothing - nothing at that range can reach the bomber
 * before it has walked somewhere else - and without a cap the safest cell on
 * the board outbids a wall worth opening from anywhere.
 */
#define BB_V_SAFE 1
#define BB_SAFE_CAP 32

/*
 * A blast is only survivable if there is somewhere to survive in, so nothing
 * is ever dropped without a route out being found first - and the route has to
 * end on a cell that no bomb on the board reaches, not merely one that is
 * quiet at the moment.  That check is the difference between a bomber that
 * plays and one that kills itself about twice a minute.
 *
 * The margin is what stops it cutting a corner too fine.  A cell may be
 * crossed only if the flame arrives more than a whole cell's walking after
 * the bomber would have left it; without it the bomber is in the doorway of a
 * corridor on the frame the corridor lights up, having planned it exactly.
 */
#define BB_MARGIN 2

/*
 * How much daylight the bomber insists on between itself and where an enemy
 * could be.  A cell is struck off outright when something could be standing in
 * it at about the moment the bomber would - which is a good deal stricter than
 * it sounds, because an enemy that has not decided where to turn yet counts as
 * being able to reach every way out of its junction.
 *
 * That strictness is deliberate and it is what the fallback below is for: if
 * every route out is refused, the search is run again with the enemies
 * ignored, because a bomber cornered by two of them still has to pick a side.
 */
#define BB_SHY 10

/*
 * Nothing broken for this long means the board has stopped moving: the pilot
 * is somewhere it can neither bomb nor improve, usually because the only
 * things left are enemies on the far side of a wall.  It drops a bomb
 * regardless, which is either a hole or a wasted fuse, and either way the
 * board is different afterwards.
 */
#define BB_PATIENCE 240

/* frames of blinking after a respawn, during which nothing can touch it */
#define BB_INVULN 30

/* how close the two boxes have to be for an enemy to have caught the bomber.
 * Ten of sixteen: the corners of two cell boxes brushing past each other in a
 * corridor is not a catch, and a game that ended on one would read as unfair */
#define BB_HIT 10

/* the holds between one thing and the next, in frames */
#define BB_T_READY 24
#define BB_T_DYING 30
#define BB_T_CLEAR 45
#define BB_T_OVER  60

#define BB_NEVER 255 /* a cell no live bomb reaches */

/* ------------------------------------------------------------------ */
/* small change                                                        */
/* ------------------------------------------------------------------ */

static uint32_t rnd(bb_game *g) {
    g->rng = g->rng * 1664525u + 1013904223u;
    return g->rng >> 8;
}

static int range_of(bb_game *g, int lo, int hi) {
    return lo + (int)(rnd(g) % (uint32_t)(hi - lo + 1));
}

static const int8_t DR[BB_DIRS] = {-1, 0, 1, 0};
static const int8_t DC[BB_DIRS] = {0, 1, 0, -1};

static bool on_board(int r, int c) {
    return r >= 0 && r < BB_ROWS && c >= 0 && c < BB_COLS;
}

static bool walkable(const bb_game *g, int r, int c) {
    return on_board(r, c) && g->cell[r][c] == BB_C_FLOOR;
}

static const bb_bomb *bomb_at(const bb_game *g, int r, int c) {
    for (int i = 0; i < BB_BOMBS; i++) {
        if (g->bombs[i].live && g->bombs[i].r == r && g->bombs[i].c == c) {
            return &g->bombs[i];
        }
    }
    return NULL;
}

static int bombs_out(const bb_game *g) {
    int n = 0;
    for (int i = 0; i < BB_BOMBS; i++) {
        n += g->bombs[i].live ? 1 : 0;
    }
    return n;
}

/* the cell an actor is mostly in, which is the one it is judged to be on */
static int actor_r(const bb_actor *a) { return (a->y + BB_CELL / 2) / BB_CELL; }
static int actor_c(const bb_actor *a) { return (a->x + BB_CELL / 2) / BB_CELL; }

static bool aligned(const bb_actor *a) {
    return (a->x % BB_CELL) == 0 && (a->y % BB_CELL) == 0;
}

static int bomber_speed(const bb_game *g) { return g->speed + g->boots; }

/* frames to walk one cell, which is the unit every plan below is timed in */
static int frames_per_cell(int speed) { return (BB_CELL + speed - 1) / speed; }

/*
 * Steps along whatever direction the actor is facing, never past the next cell
 * boundary - so an actor always lands exactly on a cell however badly the
 * speed divides the cell size, and aligned() is a test rather than a hope.
 */
static void advance(bb_actor *a, int speed) {
    int pos = (a->dir == BB_LEFT || a->dir == BB_RIGHT) ? a->x : a->y;
    int m = pos % BB_CELL;
    int rem;

    if (a->dir == BB_RIGHT || a->dir == BB_DOWN) {
        rem = BB_CELL - m;
    } else {
        rem = (m == 0) ? BB_CELL : m;
    }

    int step = speed < rem ? speed : rem;
    a->x += (int16_t)(DC[a->dir] * step);
    a->y += (int16_t)(DR[a->dir] * step);
    a->step = (uint8_t)(a->step + step);
}

/* ------------------------------------------------------------------ */
/* what a bomb reaches                                                 */
/* ------------------------------------------------------------------ */

/*
 * One arm of a blast, walked cell by cell.  A pillar stops it dead and is
 * untouched; a brick stops it too but burns on the way, which is the whole
 * economy of the game - one bomb opens one cell of any wall, never two.  The
 * caller is handed each cell in turn and says nothing back, because both the
 * real blast and the map the pilot plans against walk exactly the same line
 * and any difference between them would be a bomber that plans one board and
 * lives on another.
 */
#define BB_WALK_ARMS(g, r0, c0, rng, R, C, K, BODY)                                                \
    do {                                                                                           \
        for (int _d = 0; _d < BB_DIRS; _d++) {                                                     \
            for (int K = 1; K <= (rng); K++) {                                                     \
                int R = (r0) + DR[_d] * K, C = (c0) + DC[_d] * K;                                  \
                if (!on_board(R, C) || (g)->cell[R][C] == BB_C_SOLID) {                            \
                    break;                                                                         \
                }                                                                                  \
                { BODY }                                                                           \
                if ((g)->cell[R][C] == BB_C_BRICK) {                                               \
                    break;                                                                         \
                }                                                                                  \
            }                                                                                      \
        }                                                                                          \
    } while (0)

/* ------------------------------------------------------------------ */
/* the map the pilot plans against                                     */
/* ------------------------------------------------------------------ */

/*
 * How many frames each cell has before it is on fire, or BB_NEVER.  One array
 * for the whole shield, like the staging band: only one game ticks at a time,
 * and a copy per call would be two hundred bytes of stack on a dongle.
 */
static uint8_t doom[BB_ROWS][BB_COLS];

/*
 * A bomb inside another bomb's blast goes off with it, so a fuse is not what
 * the bomb was dropped with but the earliest fuse of any chain reaching it.
 * Settling that is a handful of passes over at most six bombs - cheap enough
 * to redo every time the pilot thinks, and the alternative is a bomber that
 * walks into the second half of a chain it set off itself.
 */
static void build_doom(bb_game *g, int extra_r, int extra_c, int slack) {
    uint8_t fuse[BB_BOMBS + 1];
    uint8_t br[BB_BOMBS + 1], bc[BB_BOMBS + 1], brg[BB_BOMBS + 1];
    int n = 0;

    for (int i = 0; i < BB_BOMBS; i++) {
        if (!g->bombs[i].live) {
            continue;
        }
        fuse[n] = (uint8_t)(g->bombs[i].fuse > slack ? g->bombs[i].fuse - slack : 0);
        br[n] = g->bombs[i].r;
        bc[n] = g->bombs[i].c;
        brg[n] = g->bombs[i].range;
        n++;
    }
    if (extra_r >= 0) {
        fuse[n] = (uint8_t)(BB_FUSE > slack ? BB_FUSE - slack : 0);
        br[n] = (uint8_t)extra_r;
        bc[n] = (uint8_t)extra_c;
        brg[n] = g->range;
        n++;
    }

    for (int pass = 0; pass < n; pass++) {
        bool moved = false;
        for (int i = 0; i < n; i++) {
            BB_WALK_ARMS(g, br[i], bc[i], brg[i], ar, ac, k, {
                (void)k;
                for (int j = 0; j < n; j++) {
                    if (br[j] == ar && bc[j] == ac && fuse[j] > fuse[i]) {
                        fuse[j] = fuse[i];
                        moved = true;
                    }
                }
            });
        }
        if (!moved) {
            break;
        }
    }

    for (int r = 0; r < BB_ROWS; r++) {
        for (int c = 0; c < BB_COLS; c++) {
            doom[r][c] = g->fire[r][c] > 0 ? 0 : BB_NEVER;
        }
    }
    for (int i = 0; i < n; i++) {
        if (doom[br[i]][bc[i]] > fuse[i]) {
            doom[br[i]][bc[i]] = fuse[i];
        }
        BB_WALK_ARMS(g, br[i], bc[i], brg[i], ar, ac, k, {
            (void)k;
            if (doom[ar][ac] > fuse[i]) {
                doom[ar][ac] = fuse[i];
            }
        });
    }
}

/*
 * The other half of the map: how soon an enemy could be standing in each cell,
 * walked out from all of them at once at the pace of the quickest.  It is
 * deliberately blind to which way any of them is facing, because a drifter has
 * not chosen yet and a hunter changes its mind the moment it can see down a
 * corridor - so a map drawn from headings would be exactly wrong at the
 * junctions, which is where every catch happens.
 */
static uint8_t threat[BB_ROWS][BB_COLS];

static void build_threat(const bb_game *g) {
    uint8_t q[BB_ROWS * BB_COLS];
    int head = 0, tail = 0, quickest = 0;

    for (int r = 0; r < BB_ROWS; r++) {
        for (int c = 0; c < BB_COLS; c++) {
            threat[r][c] = BB_NEVER;
        }
    }
    for (int i = 0; i < BB_FOES; i++) {
        if (!g->foes[i].alive) {
            continue;
        }
        if (g->foes[i].speed > quickest) {
            quickest = g->foes[i].speed;
        }
        int r = actor_r(&g->foes[i].a), c = actor_c(&g->foes[i].a);
        if (threat[r][c] != 0) {
            threat[r][c] = 0;
            q[tail++] = (uint8_t)(r * BB_COLS + c);
        }
    }
    if (quickest == 0) {
        return;
    }

    int fpc = frames_per_cell(quickest);
    while (head < tail) {
        int cell = q[head++];
        int r = cell / BB_COLS, c = cell % BB_COLS;
        int nd = threat[r][c] + fpc;
        if (nd >= BB_NEVER) {
            continue;
        }
        for (int d = 0; d < BB_DIRS; d++) {
            int nr = r + DR[d], nc = c + DC[d];
            if (!walkable(g, nr, nc) || threat[nr][nc] != BB_NEVER) {
                continue;
            }
            /* a bomb pens an enemy in as surely as it does the bomber */
            if (bomb_at(g, nr, nc) != NULL) {
                continue;
            }
            threat[nr][nc] = (uint8_t)nd;
            q[tail++] = (uint8_t)(nr * BB_COLS + nc);
        }
    }
}

/* ------------------------------------------------------------------ */
/* getting from here to there                                          */
/* ------------------------------------------------------------------ */

/*
 * One breadth-first walk of the board, in frames rather than in steps, so that
 * "will this cell be alight by the time I am standing in it" is a comparison
 * against the same clock the bombs are counting on.  Everything the pilot does
 * is one of these: fleeing is a search for a cell no bomb reaches, planning is
 * a search for the most valuable cell, and asking whether a bomb may be
 * dropped is the first search run against a board with that bomb already on
 * it.
 *
 * A cell may be entered only if the flame is still more than a cell's walking
 * away when the bomber would be leaving again.  That is stricter than it needs
 * to be for the cell it stops on and exactly right for the ones it passes
 * through, and the difference is a couple of frames on a route that is
 * measured in dozens.
 */
static uint16_t dist[BB_ROWS][BB_COLS];
static uint8_t first_dir[BB_ROWS][BB_COLS];
static uint8_t queue[BB_ROWS * BB_COLS];

static int bfs(bb_game *g, int sr, int sc, int fpc, bool shy) {
    int head = 0, tail = 0;

    for (int r = 0; r < BB_ROWS; r++) {
        for (int c = 0; c < BB_COLS; c++) {
            dist[r][c] = 0xFFFF;
            first_dir[r][c] = BB_NODIR;
        }
    }
    dist[sr][sc] = 0;
    queue[tail++] = (uint8_t)(sr * BB_COLS + sc);

    while (head < tail) {
        int cell = queue[head++];
        int r = cell / BB_COLS, c = cell % BB_COLS;
        uint16_t nd = (uint16_t)(dist[r][c] + fpc);

        for (int d = 0; d < BB_DIRS; d++) {
            int nr = r + DR[d], nc = c + DC[d];
            if (!walkable(g, nr, nc) || dist[nr][nc] != 0xFFFF) {
                continue;
            }
            /* a bomb is a wall to everything, including whoever dropped it */
            if (bomb_at(g, nr, nc) != NULL) {
                continue;
            }
            if (doom[nr][nc] != BB_NEVER && doom[nr][nc] <= nd + fpc + BB_MARGIN) {
                continue;
            }
            if (shy && threat[nr][nc] <= nd + BB_SHY) {
                continue;
            }
            dist[nr][nc] = nd;
            first_dir[nr][nc] = (r == sr && c == sc) ? (uint8_t)d : first_dir[r][c];
            queue[tail++] = (uint8_t)(nr * BB_COLS + nc);
        }
    }
    return tail;
}

/*
 * The nearest cell nothing on the board reaches, and the first step towards
 * it.  The enemies are not allowed to veto a route out, only to choose between
 * two equally short ones: being burnt is certain and being caught is a maybe,
 * and a search that could refuse the only way out would have to be run twice
 * and could return a different answer each frame.  That second answer is the
 * whole failure - a bomber that changes which way it is running at every cell
 * covers no ground at all and is still standing there when the fuse ends.
 */
static int flee_dir(bb_game *g, int sr, int sc, int fpc) {
    int reached = bfs(g, sr, sc, fpc, false);
    int best = BB_NODIR;
    uint16_t near = 0xFFFF;
    uint8_t clear = 0;

    for (int i = 0; i < reached; i++) {
        int r = queue[i] / BB_COLS, c = queue[i] % BB_COLS;
        if (doom[r][c] != BB_NEVER || dist[r][c] > near) {
            continue;
        }
        if (dist[r][c] < near || threat[r][c] > clear) {
            near = dist[r][c];
            clear = threat[r][c];
            best = first_dir[r][c];
        }
    }
    return best;
}

/*
 * Which way out a bomb dropped here would leave, or BB_NODIR for none.  The map
 * is rebuilt with that bomb on it - chains and all, since dropping one beside a
 * burning fuse is exactly the case that kills - and the answer is the first step
 * of the route the search finds.
 *
 * It is proved against a clock a cell's walking ahead of the real one, and the
 * step it found is the step actually taken.  Both halves of that were bought
 * with deaths.  A bomber that proved a route and then stood still for the frame
 * it took to drop the bomb had spent the only slack the route had, and a bomber
 * that worked the route out again from scratch on the next frame got a
 * different answer, because by then every other fuse on the board had moved on
 * too - and the two of them between them were every time this thing blew itself
 * up.  Proving it late and then walking it is what makes the promise good.
 */
static int drop_escape(bb_game *g, int r, int c, int fpc) {
    build_doom(g, r, c, fpc);
    int d = flee_dir(g, r, c, fpc);
    build_doom(g, -1, -1, 0);
    return d;
}

/*
 * What a bomb dropped on this cell would be worth.  Bricks are counted where
 * the blast would actually reach; enemies are counted two cells further out as
 * well, and worth much less there.  That second band is what gives the pilot a
 * gradient to walk up towards something worth burning - counting only what is
 * in the arms right now makes the value of every cell on the board flicker
 * with each step an enemy takes, and a plan built on it changes its mind every
 * junction and arrives nowhere.
 */
#define BB_REACH_OUT 2

static int blast_value(const bb_game *g, int r, int c) {
    int v = 0;

    BB_WALK_ARMS(g, r, c, g->range + BB_REACH_OUT, ar, ac, k, {
        if (g->cell[ar][ac] == BB_C_BRICK) {
            if (k <= g->range) {
                v += BB_V_BRICK;
            }
        } else {
            for (int i = 0; i < BB_FOES; i++) {
                if (g->foes[i].alive && actor_r(&g->foes[i].a) == ar &&
                    actor_c(&g->foes[i].a) == ac) {
                    v += k <= g->range ? BB_V_FOE : BB_V_NEAR;
                }
            }
        }
    });
    return v;
}

/* ------------------------------------------------------------------ */
/* bombs going off                                                     */
/* ------------------------------------------------------------------ */

static void light(bb_game *g, int r, int c) {
    g->fire[r][c] = BB_FLAME;

    if (g->cell[r][c] == BB_C_BRICK) {
        g->cell[r][c] = BB_C_FLOOR;
        g->score += 10;
        g->idle = 0;
        g->sfx |= BB_SFX_BRICK;
        if (g->item[r][c] == BB_I_DOOR) {
            g->door_open = true;
        }
    }
}

/*
 * A bomb goes off, and anything it reaches that is itself a bomb goes off in
 * the same frame.  Done as a sweep rather than by recursion: a chain is at
 * most six deep but it is also a cycle as often as not - two bombs each inside
 * the other's arms - and a sweep that stops when nothing more was lit ends
 * either way.
 *
 * The whole chain is worked out before any of it is applied, and that is not
 * tidiness.  A blast stops at a brick, so a bomb whose arm is walked after the
 * brick in front of it has already been taken down reaches a cell further than
 * the same bomb walked first - and the map the pilot planned its way out on
 * was drawn against the board as it stood.  Applying the chain as it was
 * computed meant a bomber standing exactly one cell beyond where it had proved
 * the flames would stop, about once in a hundred bombs, and no way at all to
 * tell that from a pilot bug.  The extent of a chain is the board it started
 * on.
 */
static bool lit[BB_ROWS][BB_COLS];

static void detonate(bb_game *g, int idx) {
    g->bombs[idx].fuse = 0;

    for (int r = 0; r < BB_ROWS; r++) {
        for (int c = 0; c < BB_COLS; c++) {
            lit[r][c] = false;
        }
    }

    bool more = true;
    while (more) {
        more = false;
        for (int i = 0; i < BB_BOMBS; i++) {
            if (!g->bombs[i].live || g->bombs[i].fuse != 0) {
                continue;
            }
            g->bombs[i].live = false;
            g->sfx |= BB_SFX_BLAST;

            int br = g->bombs[i].r, bc = g->bombs[i].c;
            lit[br][bc] = true;
            BB_WALK_ARMS(g, br, bc, g->bombs[i].range, ar, ac, k, {
                (void)k;
                lit[ar][ac] = true;
            });

            for (int j = 0; j < BB_BOMBS; j++) {
                if (g->bombs[j].live && lit[g->bombs[j].r][g->bombs[j].c]) {
                    g->bombs[j].fuse = 0;
                    more = true;
                }
            }
        }
    }

    for (int r = 0; r < BB_ROWS; r++) {
        for (int c = 0; c < BB_COLS; c++) {
            if (lit[r][c]) {
                light(g, r, c);
            }
        }
    }
}

static void drop_bomb(bb_game *g, int r, int c) {
    for (int i = 0; i < BB_BOMBS; i++) {
        if (g->bombs[i].live) {
            continue;
        }
        g->bombs[i].live = true;
        g->bombs[i].r = (uint8_t)r;
        g->bombs[i].c = (uint8_t)c;
        g->bombs[i].fuse = BB_FUSE;
        g->bombs[i].range = g->range;
        g->sfx |= BB_SFX_BOMB;
        return;
    }
}

/* ------------------------------------------------------------------ */
/* the bomber's own mind                                               */
/* ------------------------------------------------------------------ */

/*
 * Nobody is holding a controller, and the whole of what replaces one is three
 * questions asked at each cell boundary, in this order:
 *
 *   Am I standing somewhere that is going to burn?  Then the only thing that
 *   matters is the nearest cell that is not, and everything else waits.
 *
 *   Is there anything worth reaching?  Every cell the search got to is priced
 *   by what a bomb dropped on it would break, less what it costs to walk
 *   there, and the bomber goes to the best of them.  The cell it is already
 *   standing on is in that list too, but only when a bomb dropped here would
 *   actually leave a way out - so "bomb where I am" and "go somewhere better"
 *   are the same comparison rather than two rules that can disagree.
 *
 *   Nothing?  Then walk towards an enemy, and failing that keep walking, so
 *   that the board in front of the bomber keeps changing until something is
 *   worth doing again.
 */
static void pilot(bb_game *g) {
    bb_actor *a = &g->bomber;
    int mr = actor_r(a), mc = actor_c(a);
    int fpc = frames_per_cell(bomber_speed(g));

    build_doom(g, -1, -1, 0);
    build_threat(g);

    if (doom[mr][mc] != BB_NEVER) {
        int d = flee_dir(g, mr, mc, fpc);
        /* cornered: the least bad step is the one that burns last */
        if (d == BB_NODIR) {
            uint8_t best = doom[mr][mc];
            for (int i = 0; i < BB_DIRS; i++) {
                int nr = mr + DR[i], nc = mc + DC[i];
                if (walkable(g, nr, nc) && bomb_at(g, nr, nc) == NULL && doom[nr][nc] > best) {
                    best = doom[nr][nc];
                    d = i;
                }
            }
        }
        a->dir = (uint8_t)(d == BB_NODIR ? BB_NODIR : d);
        return;
    }

    int here = blast_value(g, mr, mc);
    bool forced = g->idle >= BB_PATIENCE;
    bool spare = bombs_out(g) < g->bombs_max;
    int escape = (spare && (here > 0 || forced)) ? drop_escape(g, mr, mc, fpc) : BB_NODIR;
    bool drop_now = escape != BB_NODIR;

    /*
     * What the cell underfoot is worth, which is the whole of the difference
     * between a bomber that works a wall and one that paces between two of
     * them.  A cell it can bomb this frame is worth what the bomb would break;
     * so is one it could bomb if it had a bomb spare, because otherwise the
     * moment the last one is dropped every other wall on the board outbids the
     * one being stood next to and the bomber walks off to it, arriving as its
     * bomb goes off behind it having broken what it was already beside.
     */
    int mine_v = 0;
    if (drop_now) {
        mine_v = forced && here == 0 ? 1 : here;
    } else if (!spare) {
        mine_v = here;
    }

    int reached = bfs(g, mr, mc, fpc, true);
    if (reached == 1) {
        reached = bfs(g, mr, mc, fpc, false);
    }

    long best = 0;
    int best_dir = BB_NODIR;
    bool may_stay = false;

    for (int i = 0; i < reached; i++) {
        int r = queue[i] / BB_COLS, c = queue[i] % BB_COLS;
        bool mine = r == mr && c == mc;
        /*
         * Only somewhere the bomber could stand is worth going to.  A cell
         * inside a live blast can be walked through on the way past - the
         * search has already checked it will be out again in time - but
         * choosing one as the place to be is choosing to arrive with a couple
         * of frames to think in, which is where it used to blow itself up.
         */
        if (!mine && doom[r][c] != BB_NEVER) {
            continue;
        }

        /*
         * Nor is standing still an option with something about to arrive.  The
         * bonus below would otherwise let a wall worth working outbid the
         * enemy walking up the corridor towards it, and a bomber that is
         * caught while deciding was never going to break that wall anyway.
         */
        if (mine && threat[r][c] <= BB_SHY) {
            continue;
        }
        may_stay = may_stay || mine;

        int v;
        if (g->foes_left == 0 && g->door_open && r == g->door_r && c == g->door_c) {
            v = BB_V_GOAL;
        } else {
            v = mine ? mine_v : blast_value(g, r, c);
        }

        long safety = threat[r][c] > BB_SAFE_CAP ? BB_SAFE_CAP : threat[r][c];
        long score = (long)v * BB_V_STEP - dist[r][c] + safety * BB_V_SAFE;
        if (mine) {
            best = score;
            best_dir = BB_NODIR;
        } else if (score > best) {
            best = score;
            best_dir = first_dir[r][c];
        }
    }

    /*
     * Told to move but with nowhere it would rather be: put the most ground
     * between itself and whatever is coming.  Without this the bomber refuses
     * to stand where it is, finds nothing better, and stands there anyway -
     * which was most of the times something walked into it.
     */
    if (!may_stay && best_dir == BB_NODIR) {
        uint8_t clear = threat[mr][mc];
        for (int i = 0; i < reached; i++) {
            int r = queue[i] / BB_COLS, c = queue[i] % BB_COLS;
            if (doom[r][c] == BB_NEVER && threat[r][c] > clear) {
                clear = threat[r][c];
                best_dir = first_dir[r][c];
            }
        }
    }

    if (best_dir != BB_NODIR) {
        a->dir = (uint8_t)best_dir;
        return;
    }
    if (drop_now) {
        drop_bomb(g, mr, mc);
        g->idle = 0;
        /* straight out along the route the drop was proved against */
        a->dir = (uint8_t)escape;
        return;
    }
    /* nowhere better to be: hold still and let the fuse or the enemy move */
    a->dir = BB_NODIR;
}

/* ------------------------------------------------------------------ */
/* the enemies                                                         */
/* ------------------------------------------------------------------ */

static bool foe_can_go(const bb_game *g, int r, int c, int d) {
    int nr = r + DR[d], nc = c + DC[d];
    return walkable(g, nr, nc) && bomb_at(g, nr, nc) == NULL;
}

/*
 * A hunter looks down the corridor it is in and turns towards the bomber if it
 * is there with nothing in between.  It has no map and does not plan; what it
 * costs the pilot is the right to stand still in a straight line, which is
 * exactly the habit the search above would otherwise fall into.
 */
static int foe_sight(const bb_game *g, int r, int c) {
    int tr = actor_r(&g->bomber), tc = actor_c(&g->bomber);

    for (int d = 0; d < BB_DIRS; d++) {
        for (int k = 1; k < BB_COLS; k++) {
            int nr = r + DR[d] * k, nc = c + DC[d] * k;
            if (!walkable(g, nr, nc) || bomb_at(g, nr, nc) != NULL) {
                break;
            }
            if (nr == tr && nc == tc) {
                return d;
            }
        }
    }
    return BB_NODIR;
}

static void turn_foe(bb_game *g, bb_foe *f) {
    int r = actor_r(&f->a), c = actor_c(&f->a);

    if (f->kind == BB_F_HUNT && g->phase == BB_PLAY) {
        int d = foe_sight(g, r, c);
        if (d != BB_NODIR) {
            f->a.dir = (uint8_t)d;
            return;
        }
    }
    if (f->a.dir < BB_DIRS && foe_can_go(g, r, c, f->a.dir) && range_of(g, 0, 15) != 0) {
        return;
    }

    /* anywhere but back the way it came, unless that is the only way left */
    int open[BB_DIRS], n = 0, back = -1;
    for (int d = 0; d < BB_DIRS; d++) {
        if (!foe_can_go(g, r, c, d)) {
            continue;
        }
        if (f->a.dir < BB_DIRS && d == ((f->a.dir + 2) & 3)) {
            back = d;
            continue;
        }
        open[n++] = d;
    }
    if (n > 0) {
        f->a.dir = (uint8_t)open[range_of(g, 0, n - 1)];
    } else {
        f->a.dir = (uint8_t)(back >= 0 ? back : BB_NODIR);
    }
}

static void move_foes(bb_game *g) {
    for (int i = 0; i < BB_FOES; i++) {
        bb_foe *f = &g->foes[i];
        if (!f->alive) {
            continue;
        }
        if (aligned(&f->a)) {
            turn_foe(g, f);
        }
        if (f->a.dir < BB_DIRS) {
            advance(&f->a, f->speed);
        }
    }
}

/* ------------------------------------------------------------------ */
/* the board                                                           */
/* ------------------------------------------------------------------ */

static void place_actor(bb_actor *a, int r, int c, int dir) {
    a->x = (int16_t)(c * BB_CELL);
    a->y = (int16_t)(r * BB_CELL);
    a->dir = (uint8_t)dir;
    a->step = 0;
}

/*
 * Where the bomber starts, and the two cells it can step into from there.
 * Something has to be kept clear or a board can begin with the bomber walled
 * in by its own bricks, which costs a bomb and half a fuse before anything
 * else happens.
 */
static bool in_pocket(int r, int c) {
    return (r == 1 && c <= 2) || (c == 1 && r <= 2);
}

static void build_board(bb_game *g) {
    for (int r = 0; r < BB_ROWS; r++) {
        for (int c = 0; c < BB_COLS; c++) {
            bool solid = r == 0 || c == 0 || r == BB_ROWS - 1 || c == BB_COLS - 1 ||
                         ((r & 1) == 0 && (c & 1) == 0);
            g->cell[r][c] = (uint8_t)(solid ? BB_C_SOLID : BB_C_FLOOR);
            g->item[r][c] = BB_I_NONE;
            g->fire[r][c] = 0;
        }
    }
    for (int i = 0; i < BB_BOMBS; i++) {
        g->bombs[i].live = false;
    }

    place_actor(&g->bomber, 1, 1, BB_RIGHT);

    /*
     * Enemies start in the far corners of the board and work inwards, so a new
     * board always begins with the whole width of it between them and the
     * bomber - long enough to break the first wall in.
     */
    static const uint8_t START_R[BB_FOES] = {11, 1, 11, 5, 7, 3};
    static const uint8_t START_C[BB_FOES] = {13, 13, 1, 13, 7, 9};
    int want = BB_FOES_0 + g->level;
    if (want > BB_FOES) {
        want = BB_FOES;
    }
    g->foes_left = 0;
    for (int i = 0; i < BB_FOES; i++) {
        g->foes[i].alive = i < want;
        if (!g->foes[i].alive) {
            continue;
        }
        place_actor(&g->foes[i].a, START_R[i], START_C[i], BB_LEFT);
        /* hunters arrive with the third enemy, and never outnumber drifters */
        g->foes[i].kind = (uint8_t)((i >= 2 && (i & 1)) ? BB_F_HUNT : BB_F_DRIFT);
        g->foes[i].speed = (uint8_t)(2 + g->level / 4);
        if (g->foes[i].speed > 3) {
            g->foes[i].speed = 3;
        }
        g->foes_left++;
    }

    /*
     * Bricks go down at a little under half of what is left, which is the
     * density that makes a board a warren rather than a field: much more and
     * the bomber spends the clock digging, much less and there is nothing to
     * hide behind when a bomb goes off.
     */
    int bricks = 0;
    for (int r = 1; r < BB_ROWS - 1; r++) {
        for (int c = 1; c < BB_COLS - 1; c++) {
            if (g->cell[r][c] != BB_C_FLOOR || in_pocket(r, c)) {
                continue;
            }
            bool taken = false;
            for (int i = 0; i < BB_FOES; i++) {
                if (g->foes[i].alive && actor_r(&g->foes[i].a) == r &&
                    actor_c(&g->foes[i].a) == c) {
                    taken = true;
                }
            }
            if (taken || range_of(g, 0, 99) >= 38) {
                continue;
            }
            g->cell[r][c] = BB_C_BRICK;
            bricks++;
        }
    }

    /*
     * The door and the pickups are hidden under bricks, chosen by walking the
     * board and taking every nth one - which spreads them, where drawing
     * positions at random puts two of them under the same brick and leaves
     * half the board with nothing in it.
     */
    int items = 2 + g->level / 2;
    if (items > 5) {
        items = 5;
    }
    int door = bricks > 0 ? range_of(g, bricks / 3, bricks - 1) : 0;
    int gap = bricks / (items + 1);
    int seen = 0, put = 0;

    for (int r = 1; r < BB_ROWS - 1 && bricks > 0; r++) {
        for (int c = 1; c < BB_COLS - 1; c++) {
            if (g->cell[r][c] != BB_C_BRICK) {
                continue;
            }
            if (seen == door) {
                g->item[r][c] = BB_I_DOOR;
                g->door_r = (uint8_t)r;
                g->door_c = (uint8_t)c;
            } else if (gap > 0 && put < items && seen % gap == gap / 2) {
                static const uint8_t KIND[3] = {BB_I_BOMB, BB_I_FLAME, BB_I_SPEED};
                g->item[r][c] = KIND[put % 3];
                put++;
            }
            seen++;
        }
    }
    if (bricks == 0) {
        /* nothing to hide it under: the way out is simply open */
        g->door_r = BB_ROWS - 2;
        g->door_c = BB_COLS - 2;
        g->item[g->door_r][g->door_c] = BB_I_DOOR;
    }
    g->door_open = g->cell[g->door_r][g->door_c] != BB_C_BRICK;

    g->clock = BB_CLOCK;
    g->idle = 0;
    g->phase = BB_READY;
    g->phase_timer = BB_T_READY;
    g->flash = false;
    g->redraw = true;
}

/*
 * A death costs the pickups as well as the life.  Keeping them would make the
 * back half of a long run a bomber with four-cell arms clearing a board in six
 * bombs, which is both easier and much less interesting to watch than the
 * first half; losing them means every board is played at about the reach the
 * board was drawn for.
 */
static void respawn(bb_game *g) {
    place_actor(&g->bomber, 1, 1, BB_RIGHT);
    g->bombs_max = BB_BOMBS_0;
    g->range = BB_RANGE_0;
    g->boots = 0;
    g->phase = BB_PLAY;
    g->phase_timer = BB_INVULN;
}

static void lose_life(bb_game *g, uint8_t cause) {
    if (g->phase == BB_DYING || g->phase == BB_OVER) {
        return;
    }
    g->cause = cause;
    g->sfx |= BB_SFX_DEATH;
    if (g->lives > 0) {
        g->lives--;
    }
    if (g->lives == 0) {
        g->phase = BB_OVER;
        g->phase_timer = BB_T_OVER;
        return;
    }
    g->phase = BB_DYING;
    g->phase_timer = BB_T_DYING;
}

/* ------------------------------------------------------------------ */
/* one frame                                                           */
/* ------------------------------------------------------------------ */

static void tick_fire(bb_game *g) {
    for (int r = 0; r < BB_ROWS; r++) {
        for (int c = 0; c < BB_COLS; c++) {
            if (g->fire[r][c] > 0) {
                g->fire[r][c]--;
            }
        }
    }
}

static void tick_bombs(bb_game *g) {
    for (int i = 0; i < BB_BOMBS; i++) {
        if (!g->bombs[i].live) {
            continue;
        }
        if (g->bombs[i].fuse > 0) {
            g->bombs[i].fuse--;
        }
        if (g->bombs[i].fuse == 0) {
            detonate(g, i);
        }
    }
}

static void burn_foes(bb_game *g) {
    for (int i = 0; i < BB_FOES; i++) {
        bb_foe *f = &g->foes[i];
        if (!f->alive || g->fire[actor_r(&f->a)][actor_c(&f->a)] == 0) {
            continue;
        }
        f->alive = false;
        g->foes_left--;
        g->score += 200;
        g->idle = 0;
        g->sfx |= BB_SFX_FOE;
    }
}

static void take_item(bb_game *g, int r, int c) {
    switch (g->item[r][c]) {
    case BB_I_BOMB:
        if (g->bombs_max < BB_BOMBS_MAX) {
            g->bombs_max++;
        }
        break;
    case BB_I_FLAME:
        if (g->range < BB_RANGE_MAX) {
            g->range++;
        }
        break;
    case BB_I_SPEED:
        if (g->boots < 2) {
            g->boots++;
        }
        break;
    default:
        return;
    }
    g->item[r][c] = BB_I_NONE;
    g->score += 50;
    g->sfx |= BB_SFX_ITEM;
}

static void move_bomber(bb_game *g) {
    bb_actor *a = &g->bomber;

    if (aligned(a)) {
        pilot(g);
        int r = actor_r(a), c = actor_c(a);
        if (a->dir < BB_DIRS) {
            int nr = r + DR[a->dir], nc = c + DC[a->dir];
            if (!walkable(g, nr, nc) || bomb_at(g, nr, nc) != NULL) {
                a->dir = BB_NODIR;
            }
        }
    }
    if (a->dir < BB_DIRS) {
        advance(a, bomber_speed(g));
    }
    if (aligned(a)) {
        take_item(g, actor_r(a), actor_c(a));
    }
}

static void check_bomber(bb_game *g) {
    const bb_actor *a = &g->bomber;
    int r = actor_r(a), c = actor_c(a);

    /* invulnerable for a moment after a respawn, or an enemy parked on the
     * corner the bomber comes back to would take the rest of the lives */
    if (g->phase == BB_PLAY && g->phase_timer > 0) {
        return;
    }
    if (g->fire[r][c] > 0) {
        lose_life(g, BB_D_BURNT);
        return;
    }
    for (int i = 0; i < BB_FOES; i++) {
        const bb_foe *f = &g->foes[i];
        if (!f->alive) {
            continue;
        }
        int dx = f->a.x - a->x, dy = f->a.y - a->y;
        if (dx < 0) {
            dx = -dx;
        }
        if (dy < 0) {
            dy = -dy;
        }
        if (dx < BB_HIT && dy < BB_HIT) {
            lose_life(g, BB_D_CAUGHT);
            return;
        }
    }
    if (g->foes_left == 0 && g->door_open && r == g->door_r && c == g->door_c) {
        g->score += 500 + 100 * g->level;
        g->sfx |= BB_SFX_CLEAR;
        g->phase = BB_CLEARED;
        g->phase_timer = BB_T_CLEAR;
    }
}

void bb_step(bb_game *g) {
    g->frame++;
    g->sfx = 0;

    tick_fire(g);
    tick_bombs(g);
    burn_foes(g);

    switch (g->phase) {
    case BB_READY:
        if (--g->phase_timer == 0) {
            g->phase = BB_PLAY;
            g->phase_timer = BB_INVULN;
        }
        return;
    case BB_PLAY:
        if (g->phase_timer > 0) {
            g->phase_timer--;
        }
        break;
    case BB_DYING:
        move_foes(g);
        if (--g->phase_timer == 0) {
            respawn(g);
        }
        return;
    case BB_CLEARED:
        g->flash = ((g->phase_timer >> 2) & 1) != 0;
        if (--g->phase_timer == 0) {
            g->level++;
            build_board(g);
        }
        return;
    case BB_OVER:
        if (--g->phase_timer == 0) {
            bb_init(g, g->rng);
        }
        return;
    }

    move_bomber(g);
    move_foes(g);
    check_bomber(g);

    g->idle++;
    if (g->clock > 0 && --g->clock == 0) {
        lose_life(g, BB_D_CLOCK);
        /* a board that ran out is a board that was not going to be finished,
         * so the next life gets a fresh one rather than the same warren */
        if (g->phase == BB_DYING) {
            uint8_t lives = g->lives;
            build_board(g);
            g->lives = lives;
            g->phase = BB_DYING;
            g->phase_timer = BB_T_DYING;
        }
    }
}

/* ------------------------------------------------------------------ */
/* setting up, and what the renderer asks                              */
/* ------------------------------------------------------------------ */

void bb_init(bb_game *g, uint32_t seed) {
    /* whatever the last words per minute set, which outlives a restart */
    uint8_t gear = g->speed;

    for (unsigned i = 0; i < sizeof(*g); i++) {
        ((uint8_t *)g)[i] = 0;
    }
    g->rng = seed ? seed : 1u;
    g->speed = gear ? gear : 4;
    g->lives = BB_LIVES;
    g->level = 1;
    g->bombs_max = BB_BOMBS_0;
    g->range = BB_RANGE_0;

    build_board(g);
}

void bb_set_speed(bb_game *g, uint8_t gear) { g->speed = gear ? gear : 1; }

bool bb_bomber_visible(const bb_game *g) {
    if (g->phase == BB_OVER || g->phase == BB_DYING) {
        /* it goes up in the flame that got it, so it stops being drawn at once */
        return g->phase == BB_DYING && g->phase_timer > BB_T_DYING - 6;
    }
    if (g->phase == BB_PLAY && g->phase_timer > 0) {
        return ((g->phase_timer >> 2) & 1) == 0;
    }
    return true;
}

bool bb_item_visible(const bb_game *g, int r, int c) {
    return g->cell[r][c] == BB_C_FLOOR && g->item[r][c] != BB_I_NONE;
}

const char *bb_banner(const bb_game *g) {
    switch (g->phase) {
    case BB_READY:
        return "READY";
    case BB_CLEARED:
        return "CLEAR";
    case BB_OVER:
        return "GAME OVER";
    default:
        return NULL;
    }
}
