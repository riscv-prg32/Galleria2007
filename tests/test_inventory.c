/* Backpack tests: free slot, full backpack, swap/drop, critical items,
 * drop refusal (the item pool never loses an instance). */
#include "test_util.h"
#include "../src/fixed.c"
#include "../src/world.c"
#include "../src/inventory.c"

#define AX(x) ((x) - MAP_ORIGIN_X)
#define AY(y) ((y) - MAP_ORIGIN_Y)
#define TAG 0x1234u

static int count_state(uint8_t state) {
    int n = 0;
    for (int i = 0; i < g_world.item_count; ++i) n += g_world.items[i].state == state;
    return n;
}

int main(void) {
    world_init();
    Backpack p;
    inv_init(&p);
    CHECK_EQ(inv_count(&p), 0);
    /* Take into free slots. */
    int taken = 0;
    for (int i = 0; i < g_world.item_count && taken < BACKPACK_SLOTS; ++i) {
        if (g_world.items[i].state != IS_WORLD) continue;
        CHECK(world_item_take(i, TAG));
        CHECK(inv_add(&p, (uint8_t)i) >= 0);
        ++taken;
    }
    CHECK(inv_full(&p));
    int target = -1;
    for (int i = 0; i < g_world.item_count; ++i)
        if (g_world.items[i].state == IS_WORLD) target = i;
    CHECK(target >= 0);
    CHECK_EQ(inv_add(&p, (uint8_t)target), -1);
    /* Swap: the abandoned item lands in the world and stays recoverable. */
    uint8_t old = p.slot[1];
    CHECK(inv_swap(&p, 1, target, TAG, AX(200), AY(300), 0));
    CHECK(inv_find_item(&p, (uint8_t)target) >= 0);
    CHECK(inv_find_item(&p, old) < 0);
    CHECK_EQ(g_world.items[old].state, IS_WORLD);
    CHECK_EQ(world_find_sector(-1, g_world.items[old].x, g_world.items[old].y), 0);
    /* Another player recovers it. */
    CHECK(world_item_take(old, 0x9999u));
    /* Invalid drop position: nothing changes and nothing is destroyed. */
    int before = inv_count(&p);
    uint8_t keep = p.slot[0];
    int t2 = -1;
    for (int i = 0; i < g_world.item_count; ++i)
        if (g_world.items[i].state == IS_WORLD) t2 = i;
    if (t2 >= 0) {
        CHECK(!inv_swap(&p, 0, t2, TAG, 30000, 30000, 0));
        CHECK_EQ(inv_count(&p), before);
        CHECK_EQ(p.slot[0], keep);
        CHECK_EQ(g_world.items[keep].state, IS_CARRIED);
    }
    /* Critical items exist and are flagged. */
    CHECK(ITEM_CRITICAL_MASK & (1u << ITEM_TOOL));
    CHECK(ITEM_CRITICAL_MASK & (1u << ITEM_KEY_A));
    CHECK(!(ITEM_CRITICAL_MASK & (1u << ITEM_BATTERY)));
    /* The instance pool is conserved: hidden + world + carried == all. */
    CHECK_EQ(count_state(IS_HIDDEN) + count_state(IS_WORLD) + count_state(IS_CARRIED),
             g_world.item_count);
    CHECK(g_world.item_count <= MAX_WORLD_DROPS);
    /* Reconcile removes an item another player won. */
    uint8_t lost = p.slot[0];
    g_world.items[lost].holder = 0x7777u;
    CHECK_EQ(inv_reconcile(&p, TAG), 1);
    CHECK(inv_find_item(&p, lost) < 0);
    TEST_MAIN_END("test_inventory")
}
