"""
Checks MAZE_ART in pacman_core.c for the three things a maze here has to hold:
no 2x2 all wall (a wall thickening), no 2x2 all corridor (a corridor widening)
and no dead ends, plus that every tile can be reached.

The odd lattice the maze is built on gives all three for free, so this is only
needed where a row departs from it - row 7 does, to put an isolated tile
between the two L's along the bottom.

    python3 tools/check_maze.py
"""

import re, sys
src = open('boards/shields/arcade_adapter/widgets/game/pacman_core.c').read()
art = re.search(r'MAZE_ART\[PM_ROWS\] = \{(.*?)\};', src, re.S).group(1)
rows = re.findall(r'"(.*?)"', art)
H, W = len(rows), len(rows[0])
wall = lambda y, x: rows[y][x] in '#H'          # house walls block too
open_ = lambda y, x: not wall(y, x)
bad = []

for y in range(H - 1):
    for x in range(W - 1):
        blk = [(y, x), (y, x+1), (y+1, x), (y+1, x+1)]
        if all(wall(*c) for c in blk):
            bad.append(f"2x2 all wall at {y},{x}")
        if all(open_(*c) for c in blk) and rows[y][x] != 'h':
            bad.append(f"2x2 all corridor at {y},{x}")

for y in range(H):
    for x in range(W):
        if wall(y, x) or rows[y][x] in 'hD':
            continue
        deg = 0
        for dy, dx in ((-1,0),(1,0),(0,-1),(0,1)):
            ny, nx = y + dy, x + dx
            if not (0 <= ny < H):
                continue
            nx %= W                              # the tunnel wraps
            if open_(ny, nx) or rows[ny][nx] == 'D':
                deg += 1
        if deg < 2:
            bad.append(f"dead end at {y},{x} (degree {deg})")

seen, stack = set(), [(0, 0)]
while stack:
    y, x = stack.pop()
    if (y, x) in seen or not (0 <= y < H) or wall(y, x):
        continue
    seen.add((y, x))
    for dy, dx in ((-1,0),(1,0),(0,-1),(0,1)):
        stack.append((y + dy, (x + dx) % W))
reach = sum(1 for y in range(H) for x in range(W) if open_(y, x))  # 'h' counts: the door lets ghosts in and out
if len(seen) != reach:
    bad.append(f"unreachable tiles: {reach - len(seen)}")

print("\n".join(rows))
print(f"\npellets {sum(r.count('.') for r in rows)} + {sum(r.count('o') for r in rows)} power")
print("\n".join(bad) if bad else "no 2x2 wall/corridor blocks, no dead ends, all reachable")
sys.exit(1 if bad else 0)
