/*
 * Galleria 2007 multiplayer protocol.
 *
 * The PRG32 multiplayer service relays, for every player, only the latest
 * snapshot {x, y, sprite(16 bits), flags(16 bits), input(7 bits), frame}
 * about every 50 ms. There are no reliable messages. The protocol therefore
 * uses *state gossip*: every player keeps re-publishing its view of the
 * shared world, one 29-bit record at a time, and receivers merge records
 * with rules that converge regardless of order, loss or duplication:
 *
 *   - world record: progression flags, merged with OR (monotonic, so a stale
 *     packet can never close an opened passage);
 *   - item record: last-writer-wins register per item instance with a 5-bit
 *     wrapping version; equal versions are tie-broken by the larger
 *     (state, payload) key, so simultaneous pickups resolve identically on
 *     every board.
 *
 * Snapshot field layout:
 *   x, y     player position (cm)
 *   sprite   bits 0..7 angle (10-bit angle >> 2), bit 8 torch on,
 *            bit 9 forcing the 2007 wall, bits 10..15 record bits 23..28
 *   input    record bits 16..22 (PRG32 keeps 7 bits)
 *   flags    record bits 0..15
 *
 * Record (29 bits): bit 28 = kind (1 world, 0 item)
 *   world: bits 0..15 world flags, bits 16..27 must be zero
 *   item : bits 0..3 item index, 4..8 version, 9..10 state,
 *          11..26 payload (carried: 16-bit holder tag; on the ground:
 *          x grid << 8 | y grid), bit 27 must be zero
 */
#ifndef G2007_NET_PROTO_H
#define G2007_NET_PROTO_H

#include <stdint.h>
#include "world.h"

#define NETP_SPRITE_LIGHT (1u << 8)
#define NETP_SPRITE_WORK (1u << 9)

typedef struct {
    uint16_t sprite;
    uint8_t input;
    uint16_t flags;
} NetSnapshotBits;

/* Apply results */
enum { NETP_IGNORED = 0, NETP_APPLIED = 1, NETP_INVALID = -1 };

uint32_t netp_world_record(uint32_t world_flags);
uint32_t netp_item_record(int index, const ItemInst *item);
void netp_pack(uint32_t record, uint8_t angle8, uint8_t light, uint8_t work,
               NetSnapshotBits *out);
uint32_t netp_unpack(const NetSnapshotBits *in);
int netp_newer(uint8_t incoming, uint8_t current);
int netp_apply(uint32_t record, WorldState *world);

/* Per-peer frame tracking: rejects duplicate and stale snapshots. */
#define NETP_MAX_TRACK 8
typedef struct {
    uint32_t id[NETP_MAX_TRACK];
    uint32_t frame[NETP_MAX_TRACK];
} NetPeerTrack;

int netp_accept_frame(NetPeerTrack *track, uint32_t peer_id, uint32_t frame);

#endif
