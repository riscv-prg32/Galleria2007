/* Network protocol tests: packing, validation, stale rejection, duplicate
 * pickup convergence, monotonic wall opening, malformed input. */
#include "test_util.h"
#include "../src/fixed.c"
#include "../src/world.c"
#include "../src/net_proto.c"

/* Deliver a record from `src` to `dst` through the 7-bit/16-bit snapshot. */
static int deliver(uint32_t record, WorldState *dst) {
    NetSnapshotBits bits;
    netp_pack(record, 0x55, 1, 0, &bits);
    CHECK_EQ(bits.sprite & 0xFF, 0x55);
    CHECK(bits.sprite & NETP_SPRITE_LIGHT);
    CHECK(bits.input < 128);
    return netp_apply(netp_unpack(&bits), dst);
}

int main(void) {
    world_init();
    WorldState base = g_world, a = base, b = base, c = base;
    /* Round trip of every bit. */
    for (uint32_t r = 0; r < (1u << 29); r += 0x0123457u) {
        NetSnapshotBits bits;
        netp_pack(r, 3, 0, 1, &bits);
        CHECK_EQ(netp_unpack(&bits), r);
    }
    /* World flags are monotonic: stale packets never close the passage. */
    CHECK_EQ(deliver(netp_world_record(WF_WALL_OPEN | WF_CLUE_1), &a), NETP_APPLIED);
    CHECK(a.flags & WF_WALL_OPEN);
    CHECK_EQ(deliver(netp_world_record(0), &a), NETP_IGNORED);
    CHECK(a.flags & WF_WALL_OPEN);
    /* Malformed records are ignored safely. */
    CHECK_EQ(netp_apply(0xFFFFFFFFu, &a), NETP_INVALID);
    CHECK_EQ(netp_apply((1u << 28) | (1u << 20), &a), NETP_INVALID);
    CHECK_EQ(netp_apply((1u << 28) | 0x8000u, &a), NETP_INVALID);
    CHECK_EQ(netp_apply(0, &a), NETP_INVALID);                  /* idle */
    CHECK_EQ(netp_apply(15u | (1u << 4) | (IS_WORLD << 9), &a), NETP_INVALID);
    CHECK_EQ(netp_apply(0u | (1u << 4) | (IS_CARRIED << 9), &a), NETP_INVALID);
    /* Item dropped off the map grid is rejected. */
    CHECK_EQ(netp_apply(0u | (1u << 4) | (IS_WORLD << 9) | (0xFFFFu << 11), &a), NETP_INVALID);
    /* Duplicate pickup: A and B take item 0 at the same time. */
    g_world = a; CHECK(world_item_take(0, 0x1111)); a = g_world;
    g_world = b; CHECK(world_item_take(0, 0x2222)); b = g_world;
    uint32_t ra = netp_item_record(0, &a.items[0]);
    uint32_t rb = netp_item_record(0, &b.items[0]);
    deliver(rb, &a);
    deliver(ra, &b);
    deliver(ra, &c);
    deliver(rb, &c);
    CHECK_EQ(a.items[0].holder, 0x2222);
    CHECK_EQ(b.items[0].holder, 0x2222);
    CHECK_EQ(c.items[0].holder, 0x2222);
    /* Order does not matter. */
    WorldState d = base;
    deliver(rb, &d);
    deliver(ra, &d);
    CHECK_EQ(d.items[0].holder, 0x2222);
    /* Winner drops it; the drop (newer version) reaches everybody, and the
     * older pickup record arriving late is rejected as stale. */
    g_world = b;
    CHECK(world_item_drop(0, g_world.items[0].x, g_world.items[0].y, g_world.items[0].sector));
    b = g_world;
    uint32_t rdrop = netp_item_record(0, &b.items[0]);
    CHECK_EQ(deliver(rdrop, &a), NETP_APPLIED);
    CHECK_EQ(a.items[0].state, IS_WORLD);
    CHECK_EQ(deliver(rb, &a), NETP_IGNORED);
    CHECK_EQ(deliver(ra, &a), NETP_IGNORED);
    CHECK_EQ(a.items[0].x, b.items[0].x);
    CHECK_EQ(a.items[0].sector, b.items[0].sector);
    /* Version arithmetic wraps. */
    CHECK(netp_newer(1, 31));
    CHECK(!netp_newer(31, 1));
    CHECK(!netp_newer(5, 5));
    /* Frame tracking rejects duplicates and stale snapshots. */
    NetPeerTrack t = {{0}, {0}};
    CHECK(netp_accept_frame(&t, 7, 100));
    CHECK(!netp_accept_frame(&t, 7, 100));
    CHECK(!netp_accept_frame(&t, 7, 99));
    CHECK(netp_accept_frame(&t, 7, 101));
    CHECK(netp_accept_frame(&t, 9, 1));
    CHECK(!netp_accept_frame(&t, 0, 5));
    TEST_MAIN_END("test_protocol")
}
