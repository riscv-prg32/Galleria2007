/*
 * Game state shared by the orchestration (game.c), the UI (ui.c) and the
 * multiplayer glue (multiplayer.c).
 */
#ifndef G2007_GAME_H
#define G2007_GAME_H

#include <stdint.h>
#include "config.h"
#include "world.h"
#include "inventory.h"
#include "input.h"
#include "ai.h"

typedef enum {
    GS_TITLE = 0,
    GS_INTRO,       /* "NAPOLI - 2007" title card                        */
    GS_PLAY,
    GS_INVENTORY,   /* backpack page and archive page                    */
    GS_SWAP,        /* backpack full: choose what to leave               */
    GS_READ,        /* text panel (examine / archive entry)              */
    GS_CAUGHT,
    GS_END,         /* end card + statistics                             */
    GS_OUTRO,       /* ARCHIVIO / RIGIOCA / GALLERIA BORBONICA           */
    GS_INFO         /* partner-approved site information (placeholder)   */
} GameState;

enum { TGT_NONE = 0, TGT_ITEM, TGT_ENTITY };

typedef struct {
    uint8_t kind;          /* TGT_*                                       */
    uint8_t index;         /* item instance or map entity                 */
    uint8_t prompt;        /* string id of the prompt                     */
} Target;

typedef struct {
    Body body;
    int32_t angle;         /* 10-bit                                      */
    int32_t eye_z;         /* smoothed eye height (cm)                    */
    uint8_t light;         /* torch on                                    */
    uint16_t boost_ticks;  /* battery boost remaining                     */
    uint16_t tag;          /* random network identity (never 0)           */
    Backpack pack;
    uint8_t checkpoint;    /* SPAWN entity index                          */
    int32_t walk_cm;       /* distance accumulator for footsteps          */
} Player;

typedef struct {
    uint8_t active, light, work, tint;
    int16_t x, y;
    int16_t sector;
    uint8_t angle8;
} Remote;

typedef struct {
    int32_t x, y, z;       /* cm                                          */
    int16_t vz;
    uint8_t sector;
    uint8_t life;
} Particle;

typedef struct {
    uint8_t state;
    Player player;
    LampMan lamp;
    InputState in;
    uint32_t last_ms, accum_ms, play_ticks;
    uint32_t registered;   /* map entities registered by this player     */
    uint8_t archive;       /* unlocked Archive entries (bits)             */
    uint8_t zones_seen;
    uint8_t finds, secrets, shared;
    /* UI */
    uint8_t menu, inv_sel, inv_page, inv_confirm, arc_sel, archive_only;
    uint8_t swap_target;
    uint8_t toast_a, toast_b;
    uint16_t toast_ticks;
    uint8_t read_title, read_body, read_label, read_return;
    Target target;
    /* puzzle / drama */
    uint16_t wall_progress;
    uint8_t helper;
    uint16_t discovery_ticks, silence_ticks, state_ticks, alert_ticks;
    uint8_t last_zone;
    uint8_t inv_return;    /* state to resume after the backpack/archive  */
    uint8_t lamp_started;  /* LampMan activated after the wall opened     */
    int32_t lamp_walk;     /* distance LampMan walked since his last step  */
    uint8_t noise;         /* pending noise radius / 64 (0 = none)        */
    int32_t noise_x, noise_y;
    int16_t noise_sector;
    Particle particles[MAX_PARTICLES];
    Remote remotes[MAX_PLAYERS - 1];
    uint8_t remote_count;
} Game;

extern Game g_game;

const char *str(int id);

/* Lifecycle (game.c). */
void game_init(void);
void game_update(void);
void game_draw(void);

/* Multiplayer glue (multiplayer.c). */
void mp_init(void);
void mp_tick(Game *g);
int mp_peer_near(int32_t x, int32_t y, int32_t radius);

/* UI (ui.c). */
void ui_draw(Game *g);
void ui_toast(Game *g, uint8_t a, uint8_t b);

#endif
