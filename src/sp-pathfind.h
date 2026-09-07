// native tile pathfinder for the embedded single-player server's bots.
// A* port of rsc-server pathfind.js findPath (8 RSC directions) + rsc-path-finder
// isValidGameStep, over the JS PathFinder's obstacle bitfield (bound zero-copy,
// so door open/close updates are seen live). pure C, no QuickJS/Vita deps.
#ifndef _H_SP_PATHFIND
#define _H_SP_PATHFIND

#include <stdint.h>

// obstacle grid as rsc-path-finder builds it: width*height bits, MSB-first,
// indexed x*height + y; obstacle coords = (width - (gameX+1)*2, gameY*2)
typedef struct sp_pathgrid {
    const uint8_t *bits;
    int width;
    int height;
} sp_pathgrid;

// goal modes
#define SP_PATH_GOAL_EXACT 0
#define SP_PATH_GOAL_ADJACENT 1

// search from (sx,sy) to (gx,gy). blocked = blocked_count (x,y) tiles to route
// around; jitter_seed != 0 shuffles neighbour order. writes up to out_cap (dx,dy)
// step pairs, returns the step count or -1 if no path within max_nodes expansions
int sp_pathfind(const sp_pathgrid *grid, int sx, int sy, int gx, int gy,
                int goal_mode, int max_nodes, const int32_t *blocked,
                int blocked_count, uint32_t jitter_seed, int8_t *out_steps,
                int out_cap);

// the wall/obstacle step rule on its own
int sp_path_valid_step(const sp_pathgrid *grid, int start_x, int start_y,
                       int delta_x, int delta_y);

#endif
