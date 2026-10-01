/*
 * World model: packed map data, shared progression flags, item instances,
 * sector-aware collision, line of sight and portal-graph navigation.
 *
 * Coordinates are centimetres in the re-centred map frame produced by
 * tools/build_map.py (x east, y north). Sectors are convex polygons wound
 * counter-clockwise; each wall stores the *inward* unit normal in Q12.
 * This module has no PRG32 dependency so it can be unit-tested on the host.
 */
#ifndef G2007_WORLD_H
#define G2007_WORLD_H

#include <stdint.h>
#include "config.h"
#include "gen/ids.h"
#include "gen/string_ids.h"

typedef struct {
    int16_t x, y;
} MapVertex;

typedef struct {
    uint8_t v0, v1;       /* wall runs v0 -> v1 (counter-clockwise)      */
    int8_t neighbor;      /* sector on the other side, -1 = solid wall    */
    uint8_t mate;         /* index of the same edge seen from neighbor    */
    int16_t nx, ny;       /* inward unit normal, Q12                      */
    uint16_t ulen_q8;     /* wall length / dominant-axis span, Q8         */
    uint8_t tex;          /* TEX_* id (door texture when the edge is a door) */
    uint8_t door;         /* DOOR_* id, DOOR_NONE for ordinary edges      */
} MapWall;

typedef struct {
    uint8_t first_wall, wall_count;
    uint8_t light;        /* ambient light 0..16                          */
    uint8_t flags;        /* SECF_* bits                                  */
    int16_t floor_z, ceil_z;
    uint8_t floor_tex, ceil_tex;
    uint8_t zone;         /* ZONE_* narrative area                        */
    uint8_t provenance;   /* PROV_* (feeds documentation and the Archive) */
} MapSector;

typedef struct {
    int16_t x, y;
    uint8_t sector, kind, arg, arg2;
    uint8_t sprite;       /* SPR_* or 255                                 */
    uint8_t archive;      /* ARC_* unlocked by examining, or 255          */
    uint8_t flags;        /* bit0 hidden, bits 4..7 provenance            */
    uint8_t name, desc;   /* string ids (S_*)                             */
} MapEntity;

#define ENTF_HIDDEN 0x01u

#include "gen/map_data.h"

/* ---- Shared, monotonic world progression ------------------------------ */
#define WF_SHELTER_CLUES_COMPLETE (1u << 0)
#define WF_WALL_IDENTIFIED (1u << 1)
#define WF_WALL_OPEN (1u << 2)
#define WF_CAVITY_ENTERED (1u << 3)
#define WF_LAMPMAN_TRIGGERED (1u << 4)
#define WF_STAIRS_FOUND (1u << 5)
#define WF_GATE_A_OPEN (1u << 6)
#define WF_GRATE_OPEN (1u << 7)
#define WF_CLUE_1 (1u << 8)
#define WF_CLUE_2 (1u << 9)
#define WF_CLUE_3 (1u << 10)
#define WF_KEY_A_FOUND (1u << 11)
#define WF_SIGHTING_DONE (1u << 12)
#define WF_KNOWN_MASK 0x1FFFu
#define WF_ALL_CLUES (WF_CLUE_1 | WF_CLUE_2 | WF_CLUE_3)

/* ---- Item instances ---------------------------------------------------- */
enum { IS_HIDDEN = 0, IS_WORLD = 1, IS_CARRIED = 2, IS_USED = 3 };

typedef struct {
    uint8_t type;         /* ITEM_*                                        */
    uint8_t state;        /* IS_*                                          */
    uint8_t version;      /* 5-bit last-writer-wins version (network)      */
    uint8_t pristine;     /* 1 until the first change (accepts any record) */
    uint16_t holder;      /* player tag while IS_CARRIED                   */
    int16_t x, y;         /* position while IS_WORLD                       */
    uint8_t sector;
    uint8_t entity;       /* map entity that spawned it (name lookup)      */
} ItemInst;

typedef struct {
    uint32_t flags;                     /* WF_* bits, only ever OR-ed      */
    ItemInst items[MAX_WORLD_DROPS];
    uint8_t item_count;
    uint32_t dirty;                     /* items changed locally (net)     */
} WorldState;

extern WorldState g_world;

/* Mover classes for passability. */
enum { MOVER_PLAYER = 0, MOVER_AI = 1 };

typedef struct {
    int32_t x, y;         /* Q8 centimetres                                */
    int16_t sector;
} Body;

void world_init(void);
void world_set_flags(uint32_t flags);
int world_door_open(uint8_t door);
int world_wall_blocks(int wall, uint8_t mover);
int world_point_in_sector(int sector, int32_t x, int32_t y);
int world_find_sector(int hint, int32_t x, int32_t y);
int world_try_move(Body *body, int32_t dx_q8, int32_t dy_q8, uint8_t mover);
int world_exit_wall(int sector, int skip_wall, int32_t px, int32_t py,
                    int32_t rx, int32_t ry);
int world_line_of_sight(int sector_a, int32_t ax, int32_t ay,
                        int sector_b, int32_t bx, int32_t by);
int world_next_portal(int from, int to, uint8_t mover);
void world_wall_midpoint(int wall, int32_t *x, int32_t *y);
int world_entity_of_kind(uint8_t kind, uint8_t arg);

/* Item operations; all bump the version and mark the item dirty. */
int world_item_take(int item, uint16_t tag);
int world_item_drop(int item, int32_t x, int32_t y, int sector_hint);
int world_item_use(int item);
int world_item_reveal(int item, int32_t x, int32_t y, int sector);
int world_item_by_type(uint8_t type);

/* Network grid for dropped items (8 bits per axis). */
#define NET_POS_STEP 40
#define NET_POS_BIAS 5120
static inline uint8_t world_quant(int32_t v) {
    int32_t q = (v + NET_POS_BIAS) / NET_POS_STEP;
    return (uint8_t)(q < 0 ? 0 : (q > 255 ? 255 : q));
}
static inline int32_t world_dequant(uint8_t q) {
    return (int32_t)q * NET_POS_STEP - NET_POS_BIAS + NET_POS_STEP / 2;
}

#endif
