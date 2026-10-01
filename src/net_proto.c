/*
 * Galleria 2007 multiplayer protocol (see net_proto.h for the layout).
 */
#include "net_proto.h"

#define REC_KIND_WORLD (1u << 28)

uint32_t netp_world_record(uint32_t world_flags) {
    return REC_KIND_WORLD | (world_flags & 0xFFFFu & WF_KNOWN_MASK);
}

static uint32_t item_payload(const ItemInst *it) {
    if (it->state == IS_CARRIED) return it->holder;
    if (it->state == IS_WORLD) return ((uint32_t)world_quant(it->x) << 8) | world_quant(it->y);
    return 0;
}

uint32_t netp_item_record(int index, const ItemInst *it) {
    return ((uint32_t)index & 15u) | ((uint32_t)(it->version & 31u) << 4) |
           ((uint32_t)(it->state & 3u) << 9) | (item_payload(it) << 11);
}

void netp_pack(uint32_t record, uint8_t angle8, uint8_t light, uint8_t work,
               NetSnapshotBits *out) {
    out->flags = (uint16_t)(record & 0xFFFFu);
    out->input = (uint8_t)((record >> 16) & 0x7Fu);
    out->sprite = (uint16_t)(angle8 | (light ? NETP_SPRITE_LIGHT : 0) |
                             (work ? NETP_SPRITE_WORK : 0) |
                             (((record >> 23) & 0x3Fu) << 10));
}

uint32_t netp_unpack(const NetSnapshotBits *in) {
    return (uint32_t)in->flags | ((uint32_t)(in->input & 0x7Fu) << 16) |
           ((uint32_t)(in->sprite >> 10) << 23);
}

/* Serial-number arithmetic on 5 bits: `incoming` is newer when it is 1..15
 * steps ahead of `current`. */
int netp_newer(uint8_t incoming, uint8_t current) {
    uint8_t d = (uint8_t)((incoming - current) & 31u);
    return d >= 1 && d <= 15;
}

int netp_apply(uint32_t record, WorldState *w) {
    if (record >> 29) return NETP_INVALID;
    if (record & REC_KIND_WORLD) {
        if (record & 0x0FFF0000u) return NETP_INVALID;
        uint32_t flags = record & 0xFFFFu;
        if (flags & ~WF_KNOWN_MASK) return NETP_INVALID;
        if ((w->flags | flags) == w->flags) return NETP_IGNORED;
        w->flags |= flags;
        return NETP_APPLIED;
    }
    if (record & (1u << 27)) return NETP_INVALID;
    int index = (int)(record & 15u);
    uint8_t version = (uint8_t)((record >> 4) & 31u);
    uint8_t state = (uint8_t)((record >> 9) & 3u);
    uint16_t payload = (uint16_t)((record >> 11) & 0xFFFFu);
    if (index >= w->item_count || version == 0 || state == IS_HIDDEN) return NETP_INVALID;
    if (state == IS_CARRIED && payload == 0) return NETP_INVALID;
    if (state == IS_USED && payload != 0) return NETP_INVALID;
    ItemInst *it = &w->items[index];
    if (!it->pristine) {
        if (version == it->version) {
            uint32_t key_in = ((uint32_t)state << 16) | payload;
            uint32_t key_cur = ((uint32_t)it->state << 16) | item_payload(it);
            if (key_in <= key_cur) return NETP_IGNORED;     /* tie-break */
        } else if (!netp_newer(version, it->version)) {
            return NETP_IGNORED;                            /* stale */
        }
    }
    int16_t x = it->x, y = it->y;
    uint8_t sector = it->sector;
    if (state == IS_WORLD) {
        x = (int16_t)world_dequant((uint8_t)(payload >> 8));
        y = (int16_t)world_dequant((uint8_t)payload);
        int s = world_find_sector(it->sector, x, y);
        if (s < 0) return NETP_INVALID;
        sector = (uint8_t)s;
    }
    it->state = state;
    it->version = version;
    it->pristine = 0;
    it->holder = state == IS_CARRIED ? payload : 0;
    it->x = x;
    it->y = y;
    it->sector = sector;
    return NETP_APPLIED;
}

int netp_accept_frame(NetPeerTrack *t, uint32_t peer_id, uint32_t frame) {
    int free_slot = -1;
    for (int i = 0; i < NETP_MAX_TRACK; ++i) {
        if (t->id[i] == peer_id && peer_id != 0) {
            /* Wrapping comparison: newer when 1..2^31-1 ahead. */
            int32_t d = (int32_t)(frame - t->frame[i]);
            if (d <= 0) return 0;
            t->frame[i] = frame;
            return 1;
        }
        if (t->id[i] == 0 && free_slot < 0) free_slot = i;
    }
    if (peer_id == 0) return 0;
    if (free_slot < 0) free_slot = (int)(peer_id % NETP_MAX_TRACK);
    t->id[free_slot] = peer_id;
    t->frame[free_slot] = frame;
    return 1;
}
