/*
 * Multiplayer glue between the PRG32 multiplayer service and the gossip
 * protocol in net_proto.c. Local movement never waits for the network:
 * remote snapshots only update remote avatars and merge shared world state.
 * On QEMU the PRG32 offline stub accepts the join and reports no peers, so
 * the same code path runs solo.
 */
#include "game.h"
#include "net_proto.h"
#include "platform.h"

static uint8_t s_joined;
static NetPeerTrack s_track;
static uint8_t s_hold, s_rr;
static uint32_t s_record;

void mp_init(void) {
    uint8_t *raw = (uint8_t *)&s_track;
    for (unsigned i = 0; i < sizeof(s_track); ++i) raw[i] = 0;
    s_hold = 0;
    s_rr = 0;
    s_record = netp_world_record(0);
    s_joined = (uint8_t)(prg32_multiplayer_join(G2007_NET_SIGNATURE, PRG32_MP_FLAG_ENABLE) == 0);
}

/* Changed items first (they matter most), then a round robin over the
 * world record and every item that has ever changed. */
static uint32_t next_record(void) {
    if (g_world.dirty) {
        for (int i = 0; i < g_world.item_count; ++i) {
            if (g_world.dirty & (1u << i)) {
                g_world.dirty &= ~(1u << i);
                return netp_item_record(i, &g_world.items[i]);
            }
        }
    }
    for (int tries = 0; tries <= g_world.item_count; ++tries) {
        int k = s_rr;
        s_rr = (uint8_t)((s_rr + 1) % (g_world.item_count + 1));
        if (k == 0) return netp_world_record(g_world.flags);
        if (!g_world.items[k - 1].pristine) return netp_item_record(k - 1, &g_world.items[k - 1]);
    }
    return netp_world_record(g_world.flags);
}

static uint8_t tint_for(uint32_t id) {
    switch (id & 3u) {
    case 0: return 209;   /* orange */
    case 1: return 117;   /* light blue */
    case 2: return 40;    /* green */
    default: return 197;  /* magenta */
    }
}

void mp_tick(Game *g) {
    if (!s_joined) {
        g->remote_count = 0;
        return;
    }
    int n = prg32_multiplayer_get_peer_count();
    uint8_t rc = 0;
    for (int i = 0; i < n; ++i) {
        prg32_player_state_t ps;
        if (prg32_multiplayer_get_peer(i, &ps) != 0) continue;
        if (rc < MAX_PLAYERS - 1) {
            Remote *r = &g->remotes[rc++];
            r->active = 1;
            r->x = ps.x;
            r->y = ps.y;
            r->angle8 = (uint8_t)(ps.sprite & 0xFFu);
            r->light = (uint8_t)((ps.sprite & NETP_SPRITE_LIGHT) != 0);
            r->work = (uint8_t)((ps.sprite & NETP_SPRITE_WORK) != 0);
            r->tint = tint_for(ps.player_id);
            r->sector = (int16_t)world_find_sector(r->sector, ps.x, ps.y);
        }
        if (netp_accept_frame(&s_track, ps.player_id, ps.frame)) {
            NetSnapshotBits bits;
            bits.sprite = ps.sprite;
            bits.input = (uint8_t)ps.input;
            bits.flags = ps.flags;
            netp_apply(netp_unpack(&bits), &g_world);
        }
    }
    g->remote_count = rc;
    if (s_hold == 0) {
        s_record = next_record();
        s_hold = G2007_NET_HOLD_TICKS;
    } else {
        --s_hold;
    }
    NetSnapshotBits out;
    const Player *p = &g->player;
    netp_pack(s_record, (uint8_t)((p->angle >> 2) & 0xFF), p->light,
              (uint8_t)(g->wall_progress > 0), &out);
    prg32_multiplayer_set_local_state((int16_t)(p->body.x >> 8), (int16_t)(p->body.y >> 8),
                                      out.sprite, out.flags);
    prg32_multiplayer_set_input(out.input);
    prg32_multiplayer_tick();
}

int mp_peer_near(int32_t x, int32_t y, int32_t radius) {
    for (int i = 0; i < g_game.remote_count; ++i) {
        const Remote *r = &g_game.remotes[i];
        int32_t dx = r->x - x, dy = r->y - y;
        if (dx * dx + dy * dy <= radius * radius) return 1;   /* helper or lookout */
    }
    return 0;
}
