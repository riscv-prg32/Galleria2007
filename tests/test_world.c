/* World tests: portals, sector queries, collision, doors, LOS, items. */
#include "test_util.h"
#include "../src/fixed.c"
#include "../src/world.c"

/* Authoring coordinates (assets/map/galleria2007.json) -> map frame. */
#define AX(x) ((x) - MAP_ORIGIN_X)
#define AY(y) ((y) - MAP_ORIGIN_Y)

static int sector_named(int32_t x, int32_t y) { return world_find_sector(-1, AX(x), AY(y)); }

static int walk(Body *b, int32_t dx_cm, int32_t dy_cm, int steps) {
    int moved = 0;
    for (int i = 0; i < steps; ++i) moved += world_try_move(b, dx_cm * 256, dy_cm * 256, MOVER_PLAYER);
    return moved;
}

int main(void) {
    world_init();
    /* Portal adjacency is symmetric. */
    for (int w = 0; w < MAP_WALL_COUNT; ++w) {
        const MapWall *wall = &g_map_walls[w];
        if (wall->neighbor < 0) continue;
        const MapWall *mate = &g_map_walls[wall->mate];
        CHECK_EQ(mate->mate, w);
        CHECK_EQ(mate->v0, wall->v1);
        CHECK_EQ(mate->v1, wall->v0);
        CHECK_EQ(mate->door, wall->door);
    }
    /* Every sector polygon contains its own centroid. */
    for (int s = 0; s < MAP_SECTOR_COUNT; ++s) {
        int32_t cx = 0, cy = 0;
        const MapSector *sec = &g_map_sectors[s];
        for (int i = 0; i < sec->wall_count; ++i) {
            cx += g_map_vertices[g_map_walls[sec->first_wall + i].v0].x;
            cy += g_map_vertices[g_map_walls[sec->first_wall + i].v0].y;
        }
        CHECK(world_point_in_sector(s, cx / sec->wall_count, cy / sec->wall_count));
    }
    int start = sector_named(200, 150);
    CHECK_EQ(start, 0);
    /* Ray from the start facing north leaves through the northern edge. */
    int w = world_exit_wall(start, -1, AX(200), AY(150), 0, FX_ONE);
    CHECK(w >= 0);
    CHECK(g_map_walls[w].neighbor == sector_named(200, 640));
    /* Walking north descends the steps into the tunnel. */
    Body b = {AX(200) * 256, AY(150) * 256, (int16_t)start};
    walk(&b, 0, 6, 200);
    CHECK_EQ(b.sector, sector_named(200, 1300));
    /* Walls stop the player and keep the radius. */
    walk(&b, -6, 0, 200);
    CHECK((b.x >> 8) >= AX(0) + G2007_RADIUS - 1);
    /* The closed gate blocks; opening it is monotonic. */
    Body g = {AX(200) * 256, AY(3950) * 256, (int16_t)sector_named(200, 3950)};
    walk(&g, 0, 6, 100);
    CHECK((g.y >> 8) < AY(4100));
    CHECK(!world_door_open(DOOR_GATE_A));
    CHECK(!world_line_of_sight(g.sector, g.x >> 8, g.y >> 8, sector_named(200, 4800), AX(200), AY(4800)));
    CHECK_EQ(world_next_portal(g.sector, sector_named(200, 4800), MOVER_PLAYER), -1);
    world_set_flags(WF_GATE_A_OPEN);
    world_set_flags(0);
    CHECK(world_door_open(DOOR_GATE_A));
    walk(&g, 0, 6, 100);
    CHECK_EQ(g.sector, sector_named(200, 4800));
    CHECK(world_line_of_sight(sector_named(200, 1300), AX(200), AY(1300), sector_named(200, 3000), AX(200), AY(3000)));
    CHECK(world_next_portal(0, sector_named(200, 4800), MOVER_PLAYER) >= 0);
    /* The cistern drop is not walkable but is visible. */
    Body c = {AX(1100) * 256, AY(1700) * 256, (int16_t)sector_named(1100, 1700)};
    walk(&c, 0, 6, 100);
    CHECK_EQ(c.sector, sector_named(1100, 1700));
    /* The grate lets only the antagonist through. */
    int hall = sector_named(0, 5500), conn = sector_named(-400, 5750);
    CHECK_EQ(world_next_portal(hall, conn, MOVER_PLAYER), -1);
    CHECK(world_next_portal(hall, conn, MOVER_AI) >= 0);
    /* Items: unique instances, one owner at a time. */
    CHECK_EQ(g_world.item_count, MAP_ITEM_COUNT);
    int nb = world_item_by_type(ITEM_NOTEBOOK);
    CHECK(nb >= 0);
    CHECK(world_item_take(nb, 11));
    CHECK(!world_item_take(nb, 12));
    CHECK_EQ(g_world.items[nb].holder, 11);
    CHECK(world_item_drop(nb, AX(200), AY(300), 0));
    CHECK_EQ(g_world.items[nb].state, IS_WORLD);
    CHECK(world_item_take(nb, 12));
    /* Dropping far outside the map is refused and the item is kept. */
    CHECK(!world_item_drop(nb, 30000, 30000, 0));
    CHECK_EQ(g_world.items[nb].state, IS_CARRIED);
    int key = world_item_by_type(ITEM_KEY_A);
    CHECK_EQ(g_world.items[key].state, IS_HIDDEN);
    CHECK(!world_item_take(key, 11));
    CHECK(world_item_reveal(key, AX(1150), AY(1700), sector_named(1150, 1700)));
    CHECK_EQ(g_world.items[key].state, IS_WORLD);
    TEST_MAIN_END("test_world")
}
