/*
 * World model implementation. See world.h for conventions.
 */
#include "world.h"
#include "fixed.h"

WorldState g_world;

#define DROP_LIMIT_CM 100 /* walkers refuse to step off higher ledges */

void world_init(void) {
    uint8_t *raw = (uint8_t *)&g_world;
    for (unsigned i = 0; i < sizeof(g_world); ++i) raw[i] = 0;
    for (int e = 0; e < MAP_ENTITY_COUNT; ++e) {
        const MapEntity *ent = &g_map_entities[e];
        if (ent->kind != ENT_ITEM || g_world.item_count >= MAX_WORLD_DROPS) continue;
        ItemInst *it = &g_world.items[g_world.item_count++];
        it->type = ent->arg;
        it->state = (ent->flags & ENTF_HIDDEN) ? IS_HIDDEN : IS_WORLD;
        it->pristine = 1;
        it->x = ent->x;
        it->y = ent->y;
        it->sector = ent->sector;
        it->entity = (uint8_t)e;
    }
}

void world_set_flags(uint32_t flags) { g_world.flags |= flags & WF_KNOWN_MASK; }

int world_door_open(uint8_t door) {
    switch (door) {
    case DOOR_GATE_A: return (g_world.flags & WF_GATE_A_OPEN) != 0;
    case DOOR_WALL_2007: return (g_world.flags & WF_WALL_OPEN) != 0;
    case DOOR_GRATE: return (g_world.flags & WF_GRATE_OPEN) != 0;
    default: return 1;
    }
}

/* Can `mover` cross this wall from its own sector into the neighbour? */
int world_wall_blocks(int wall, uint8_t mover) {
    const MapWall *w = &g_map_walls[wall];
    if (w->neighbor < 0) return 1;
    if (w->door != DOOR_NONE && !world_door_open(w->door)) {
        /* The antagonist knows the hidden route behind the grate (fiction). */
        if (!(mover == MOVER_AI && w->door == DOOR_GRATE)) return 1;
    }
    const MapSector *a = &g_map_sectors[g_map_walls[w->mate].neighbor];
    const MapSector *b = &g_map_sectors[w->neighbor];
    if (b->floor_z - a->floor_z > G2007_STEP_MAX) return 1;
    if (a->floor_z - b->floor_z > DROP_LIMIT_CM) return 1;
    if (fx_min(a->ceil_z, b->ceil_z) - b->floor_z < G2007_HEADROOM) return 1;
    if (mover == MOVER_AI && (b->flags & SECF_NO_AI)) return 1;
    return 0;
}

/* Signed distance (cm) from (x, y) to the wall line; positive = inside. */
static int32_t wall_distance(const MapWall *w, int32_t x, int32_t y) {
    const MapVertex *a = &g_map_vertices[w->v0];
    return ((x - a->x) * w->nx + (y - a->y) * w->ny) >> FX_SHIFT;
}

int world_point_in_sector(int sector, int32_t x, int32_t y) {
    const MapSector *s = &g_map_sectors[sector];
    for (int i = 0; i < s->wall_count; ++i) {
        if (wall_distance(&g_map_walls[s->first_wall + i], x, y) < 0) return 0;
    }
    return 1;
}

int world_find_sector(int hint, int32_t x, int32_t y) {
    if (hint >= 0 && hint < MAP_SECTOR_COUNT) {
        if (world_point_in_sector(hint, x, y)) return hint;
        const MapSector *s = &g_map_sectors[hint];
        for (int i = 0; i < s->wall_count; ++i) {
            int n = g_map_walls[s->first_wall + i].neighbor;
            if (n >= 0 && world_point_in_sector(n, x, y)) return n;
        }
    }
    for (int i = 0; i < MAP_SECTOR_COUNT; ++i) {
        if (world_point_in_sector(i, x, y)) return i;
    }
    return -1;
}

/* Sector containing (x, y) reachable from `from` through passable portals
 * (two levels deep, enough for one movement step). */
static int locate_reachable(int from, int32_t x, int32_t y, uint8_t mover) {
    if (world_point_in_sector(from, x, y)) return from;
    const MapSector *s = &g_map_sectors[from];
    for (int i = 0; i < s->wall_count; ++i) {
        int w = s->first_wall + i;
        if (world_wall_blocks(w, mover)) continue;
        int n = g_map_walls[w].neighbor;
        if (world_point_in_sector(n, x, y)) return n;
        const MapSector *ns = &g_map_sectors[n];
        for (int j = 0; j < ns->wall_count; ++j) {
            int w2 = ns->first_wall + j;
            int n2 = g_map_walls[w2].neighbor;
            if (n2 == from || world_wall_blocks(w2, mover)) continue;
            if (world_point_in_sector(n2, x, y)) return n2;
        }
    }
    return -1;
}

/*
 * Move a body by (dx, dy) in Q8 cm. The target point must lie in a sector
 * reachable through passable portals; then it is pushed out of every
 * blocking wall closer than the collision radius (this is what makes the
 * player slide along walls). Returns 1 when the body moved.
 */
int world_try_move(Body *body, int32_t dx_q8, int32_t dy_q8, uint8_t mover) {
    int32_t nx = body->x + dx_q8, ny = body->y + dy_q8;
    int s = locate_reachable(body->sector, nx >> 8, ny >> 8, mover);
    if (s < 0) return 0;
    const MapSector *sec = &g_map_sectors[s];
    for (int pass = 0; pass < 2; ++pass) {
        for (int i = 0; i < sec->wall_count; ++i) {
            int wi = sec->first_wall + i;
            if (!world_wall_blocks(wi, mover)) continue;
            const MapWall *w = &g_map_walls[wi];
            const MapVertex *a = &g_map_vertices[w->v0];
            const MapVertex *b = &g_map_vertices[w->v1];
            int32_t px = nx >> 8, py = ny >> 8;
            int32_t ex = b->x - a->x, ey = b->y - a->y;
            int32_t along = (px - a->x) * ex + (py - a->y) * ey;
            int32_t len2 = ex * ex + ey * ey;
            int32_t margin = G2007_RADIUS * fx_max(fx_abs(ex), fx_abs(ey));
            if (along < -margin || along > len2 + margin) continue;
            int32_t d = wall_distance(w, px, py);
            if (d < G2007_RADIUS) {
                nx += (w->nx * (G2007_RADIUS - d)) >> (FX_SHIFT - 8);
                ny += (w->ny * (G2007_RADIUS - d)) >> (FX_SHIFT - 8);
            }
        }
    }
    if (!world_point_in_sector(s, nx >> 8, ny >> 8)) return 0;
    body->x = nx;
    body->y = ny;
    body->sector = (int16_t)s;
    return 1;
}

/*
 * The wall through which the ray P + t*R leaves a convex sector.
 * A counter-clockwise polygon is left through the unique edge whose start
 * vertex lies to the right of the ray and whose end vertex lies on or to
 * its left. Only sign tests are needed - no division. This is the core of
 * both the renderer and line-of-sight queries.
 */
int world_exit_wall(int sector, int skip_wall, int32_t px, int32_t py,
                    int32_t rx, int32_t ry) {
    const MapSector *s = &g_map_sectors[sector];
    const MapVertex *v0 = &g_map_vertices[g_map_walls[s->first_wall].v0];
    int32_t side_a = rx * (v0->y - py) - ry * (v0->x - px);
    for (int i = 0; i < s->wall_count; ++i) {
        int wi = s->first_wall + i;
        const MapVertex *b = &g_map_vertices[g_map_walls[wi].v1];
        int32_t side_b = rx * (b->y - py) - ry * (b->x - px);
        if (side_a < 0 && side_b >= 0 && wi != skip_wall) return wi;
        side_a = side_b;
    }
    return -1;
}

int world_line_of_sight(int sector_a, int32_t ax, int32_t ay,
                        int sector_b, int32_t bx, int32_t by) {
    int s = sector_a, skip = -1;
    int32_t rx = bx - ax, ry = by - ay;
    for (int depth = 0; depth < 40; ++depth) {
        if (s == sector_b) return 1;
        int w = world_exit_wall(s, skip, ax, ay, rx, ry);
        if (w < 0) return 0;
        /* Stop when B lies before the exit wall (B is inside this sector's
         * half-plane but we did not match its sector: treat as blocked). */
        if (((bx - g_map_vertices[g_map_walls[w].v0].x) * g_map_walls[w].nx +
             (by - g_map_vertices[g_map_walls[w].v0].y) * g_map_walls[w].ny) > 0) return 0;
        const MapWall *wall = &g_map_walls[w];
        if (wall->neighbor < 0) return 0;
        if (wall->door != DOOR_NONE && !world_door_open(wall->door)) return 0;
        skip = wall->mate;
        s = wall->neighbor;
    }
    return 0;
}

/* Breadth-first search over the sector graph. Returns the wall (in `from`)
 * leading one step closer to `to`, or -1 when unreachable or already there. */
int world_next_portal(int from, int to, uint8_t mover) {
    if (from == to || from < 0 || to < 0) return -1;
    int8_t prev_wall_sector[MAP_SECTOR_COUNT];
    uint8_t via_wall[MAP_SECTOR_COUNT];
    uint8_t queue[MAP_SECTOR_COUNT];
    for (int i = 0; i < MAP_SECTOR_COUNT; ++i) prev_wall_sector[i] = -2;
    int head = 0, tail = 0;
    queue[tail++] = (uint8_t)from;
    prev_wall_sector[from] = -1;
    while (head < tail) {
        int s = queue[head++];
        if (s == to) break;
        const MapSector *sec = &g_map_sectors[s];
        for (int i = 0; i < sec->wall_count; ++i) {
            int wi = sec->first_wall + i;
            if (world_wall_blocks(wi, mover)) continue;
            int n = g_map_walls[wi].neighbor;
            if (prev_wall_sector[n] != -2) continue;
            prev_wall_sector[n] = (int8_t)s;
            via_wall[n] = (uint8_t)wi;
            queue[tail++] = (uint8_t)n;
        }
    }
    if (prev_wall_sector[to] == -2) return -1;
    int s = to;
    while (prev_wall_sector[s] != from) s = prev_wall_sector[s];
    return via_wall[s];
}

void world_wall_midpoint(int wall, int32_t *x, int32_t *y) {
    const MapWall *w = &g_map_walls[wall];
    *x = (g_map_vertices[w->v0].x + g_map_vertices[w->v1].x) / 2;
    *y = (g_map_vertices[w->v0].y + g_map_vertices[w->v1].y) / 2;
}

int world_entity_of_kind(uint8_t kind, uint8_t arg) {
    for (int e = 0; e < MAP_ENTITY_COUNT; ++e) {
        if (g_map_entities[e].kind == kind && g_map_entities[e].arg == arg) return e;
    }
    return -1;
}

int world_item_by_type(uint8_t type) {
    for (int i = 0; i < g_world.item_count; ++i) {
        if (g_world.items[i].type == type) return i;
    }
    return -1;
}

static void item_touch(int item) {
    ItemInst *it = &g_world.items[item];
    it->version = (uint8_t)((it->version + 1) & 31u);
    if (it->version == 0) it->version = 1;   /* 0 is reserved for "never changed" */
    it->pristine = 0;
    g_world.dirty |= 1u << item;
}

int world_item_take(int item, uint16_t tag) {
    if (item < 0 || item >= g_world.item_count) return 0;
    ItemInst *it = &g_world.items[item];
    if (it->state != IS_WORLD || tag == 0) return 0;
    it->state = IS_CARRIED;
    it->holder = tag;
    item_touch(item);
    return 1;
}

/* Place a carried item on the network grid near (x, y). The 3x3 grid
 * neighbourhood is searched for a cell inside a sector, so every peer
 * reconstructs exactly the same position. Returns 0 (item unchanged) when no
 * valid cell exists: items are refused, never destroyed. */
int world_item_drop(int item, int32_t x, int32_t y, int sector_hint) {
    if (item < 0 || item >= g_world.item_count) return 0;
    ItemInst *it = &g_world.items[item];
    if (it->state != IS_CARRIED) return 0;
    for (int k = 0; k < 9; ++k) {
        int qx = world_quant(x) + (k % 3 == 1 ? -1 : (k % 3 == 2 ? 1 : 0));
        int qy = world_quant(y) + (k / 3 == 1 ? -1 : (k / 3 == 2 ? 1 : 0));
        if (qx < 0 || qx > 255 || qy < 0 || qy > 255) continue;
        int32_t wx = world_dequant((uint8_t)qx), wy = world_dequant((uint8_t)qy);
        int s = world_find_sector(sector_hint, wx, wy);
        if (s < 0) continue;
        it->state = IS_WORLD;
        it->holder = 0;
        it->x = (int16_t)wx;
        it->y = (int16_t)wy;
        it->sector = (uint8_t)s;
        item_touch(item);
        return 1;
    }
    return 0;
}

int world_item_use(int item) {
    if (item < 0 || item >= g_world.item_count) return 0;
    ItemInst *it = &g_world.items[item];
    if (it->state != IS_CARRIED) return 0;
    it->state = IS_USED;
    it->holder = 0;
    item_touch(item);
    return 1;
}

int world_item_reveal(int item, int32_t x, int32_t y, int sector) {
    if (item < 0 || item >= g_world.item_count) return 0;
    ItemInst *it = &g_world.items[item];
    if (it->state != IS_HIDDEN) return 0;
    it->state = IS_CARRIED;          /* temporarily: drop() places it */
    if (!world_item_drop(item, x, y, sector)) {
        it->state = IS_HIDDEN;
        return 0;
    }
    return 1;
}
