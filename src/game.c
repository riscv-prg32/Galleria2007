/*
 * Game orchestration: state machine, player, interactions, the 2007 wall
 * puzzle, narrative triggers, antagonist integration and music selection.
 *
 * Logic runs at a fixed 30 Hz tick (G2007_TICK_MS) so behaviour does not
 * depend on the rendering frame rate of the board or emulator.
 */
#include "game.h"
#include "fixed.h"
#include "renderer.h"
#include "audio.h"
#include "platform.h"

Game g_game;

/* Item names follow the ITEM_* order (checked at compile time). */
typedef char check_item_names[(S_N_MAP - S_N_NOTEBOOK == ITEM_MAP - ITEM_NOTEBOOK) ? 1 : -1];
typedef char check_archive_strings[(S_A6_TITLE - S_A0_TITLE == 2 * (ARC_FICTION - ARC_GALLERIA)) ? 1 : -1];

#define TICKS(ms) ((ms) / G2007_TICK_MS)
#define WALL_FORCE_TICKS TICKS(4000)
#define NOISE_LOUD 40          /* x64 cm hearing radius */
#define NOISE_SOFT 10

static uint8_t item_name(uint8_t type) { return (uint8_t)(S_N_NOTEBOOK + type - ITEM_NOTEBOOK); }

static int has_type(const Game *g, uint8_t type) { return inv_find_type(&g->player.pack, type) >= 0; }

static int has_recorder(const Game *g) {
    return has_type(g, ITEM_NOTEBOOK) || has_type(g, ITEM_CAMERA);
}

/* Stereo position of a world sound for the listener: pan from the source's
 * lateral offset (-64 left .. 63 right), gain falling off to 0 at 25 m. */
static void sfx_from(const Game *g, uint8_t sfx, int32_t x, int32_t y) {
    const Player *p = &g->player;
    Camera cam;
    cam.x = p->body.x >> 8;
    cam.y = p->body.y >> 8;
    cam.angle = p->angle;
    int32_t tz, tx;
    int sx;
    render_project(&cam, x, y, &tz, &tx, &sx);
    int32_t dist = fx_abs(tz) + fx_abs(tx) / 2;
    int32_t gain = 255 - dist * 255 / 2500;
    if (gain <= 0) return;
    int32_t pan = (tx * 63) / (fx_abs(tx) + fx_abs(tz) + 1);
    audio_sfx_at(sfx, (int8_t)fx_clamp(pan, -64, 63), (uint8_t)fx_max(gain, 40));
}

static void make_noise(Game *g, int32_t x, int32_t y, int sector, uint8_t radius64) {
    if (radius64 < g->noise) return;
    g->noise = radius64;
    g->noise_x = x;
    g->noise_y = y;
    g->noise_sector = (int16_t)sector;
}

/* ------------------------------------------------------------------ */
/* Session                                                             */
/* ------------------------------------------------------------------ */

static void spawn_player(Game *g) {
    const MapEntity *e = &g_map_entities[g->player.checkpoint];
    g->player.body.x = (int32_t)e->x * 256;
    g->player.body.y = (int32_t)e->y * 256;
    g->player.body.sector = e->sector;
    g->player.angle = (int32_t)e->arg2 * 4;
    g->player.eye_z = g_map_sectors[e->sector].floor_z + G2007_EYE_HEIGHT;
}

static void new_session(Game *g) {
    uint8_t archive = g->archive;          /* Archive knowledge persists */
    uint16_t tag = g->player.tag;
    uint8_t *raw = (uint8_t *)g;
    for (unsigned i = 0; i < sizeof(*g); ++i) raw[i] = 0;
    g->archive = archive;
    g->player.tag = tag;
    world_init();
    inv_init(&g->player.pack);
    ai_reset(&g->lamp);
    g->player.checkpoint = (uint8_t)world_entity_of_kind(ENT_SPAWN, 0);
    g->player.light = 1;
    spawn_player(g);
    g->last_zone = 0xFF;
}

void ui_toast(Game *g, uint8_t a, uint8_t b) {
    g->toast_a = a;
    g->toast_b = b;
    g->toast_ticks = TICKS(2600);
}

static void open_read(Game *g, uint8_t title, uint8_t body, uint8_t label, uint8_t ret) {
    g->read_title = title;
    g->read_body = body;
    g->read_label = label;
    g->read_return = ret;
    g->state = GS_READ;
}

static void open_archive_entry(Game *g, uint8_t arc, uint8_t ret) {
    open_read(g, (uint8_t)(S_A0_TITLE + 2 * arc), (uint8_t)(S_A0_TITLE + 2 * arc + 1),
              arc == ARC_FICTION ? S_L_FICTION : S_L_HIST, ret);
}

static void unlock_archive(Game *g, uint8_t arc) {
    if (arc >= ARC_COUNT || (g->archive & (1u << arc))) return;
    g->archive |= (uint8_t)(1u << arc);
    if (arc == ARC_FICTION) ++g->secrets;
    ui_toast(g, S_M_NEW_ARCHIVE, S_NONE);
    audio_sfx(SFX_REGISTER);
}

/* ------------------------------------------------------------------ */
/* Interaction                                                         */
/* ------------------------------------------------------------------ */

static uint8_t entity_prompt(const Game *g, int e) {
    const MapEntity *ent = &g_map_entities[e];
    uint32_t f = g_world.flags;
    int fresh = !(g->registered & (1u << e)) && has_recorder(g);
    switch (ent->kind) {
    case ENT_HIST:
    case ENT_DECOR:
        return fresh ? S_P_REGISTER : S_P_EXAMINE;
    case ENT_CLUE:
        return (!(f & (WF_CLUE_1 << (ent->arg - 1))) && has_recorder(g)) ? S_P_REGISTER : S_P_EXAMINE;
    case ENT_CISTERN:
        return (!(f & WF_KEY_A_FOUND) && has_type(g, ITEM_ROPE)) ? S_P_USE : S_P_EXAMINE;
    case ENT_DOOR:
        if (ent->arg != DOOR_WALL_2007) return S_P_OPEN;
        if (!(f & WF_SHELTER_CLUES_COMPLETE)) return S_P_EXAMINE;
        if ((f & WF_WALL_IDENTIFIED) && has_type(g, ITEM_TOOL)) return S_P_FORCE;
        return S_P_DIFFERENT;
    default:
        return S_P_EXAMINE;
    }
}

static int entity_priority(uint8_t kind) {
    switch (kind) {
    case ENT_DOOR: return 1;
    case ENT_HIST:
    case ENT_DECOR: return 3;
    default: return 4;            /* clues, cistern, fiction (puzzle) */
    }
}

static void consider(Game *g, const Camera *cam, uint8_t kind, uint8_t index,
                     int32_t x, int32_t y, int prio, int32_t *best) {
    int32_t tz, tx;
    int sx;
    int32_t reach = g->player.light ? G2007_REACH_LIGHT_CM : G2007_REACH_CM;
    if (!render_project(cam, x, y, &tz, &tx, &sx)) return;
    if (tz > reach || fx_abs(tx) > tz / 2 + 25) return;
    sx = fx_clamp(sx, 0, G2007_SCREEN_W - 1);
    if (tz > g_zbuf[sx] + 30) return;             /* behind a wall */
    int32_t score = prio * 4096 + tz;
    if (score < *best) {
        *best = score;
        g->target.kind = kind;
        g->target.index = index;
    }
}

static void find_target(Game *g, const Camera *cam) {
    int32_t best = 0x7FFFFFFF;
    g->target.kind = TGT_NONE;
    for (int i = 0; i < g_world.item_count; ++i) {
        const ItemInst *it = &g_world.items[i];
        if (it->state == IS_WORLD) consider(g, cam, TGT_ITEM, (uint8_t)i, it->x, it->y, 2, &best);
    }
    for (int e = 0; e < MAP_ENTITY_COUNT; ++e) {
        const MapEntity *ent = &g_map_entities[e];
        switch (ent->kind) {
        case ENT_DOOR:
            if (world_door_open(ent->arg)) break;
            /* fall through */
        case ENT_HIST: case ENT_DECOR: case ENT_CLUE: case ENT_FICTION: case ENT_CISTERN:
            consider(g, cam, TGT_ENTITY, (uint8_t)e, ent->x, ent->y, entity_priority(ent->kind), &best);
            break;
        default:
            break;
        }
    }
    if (g->target.kind == TGT_ITEM) g->target.prompt = S_P_TAKE;
    else if (g->target.kind == TGT_ENTITY) g->target.prompt = entity_prompt(g, g->target.index);
}

static void take_item(Game *g, int item) {
    Player *p = &g->player;
    if (inv_full(&p->pack)) {
        g->swap_target = (uint8_t)item;
        g->inv_sel = 0;
        g->state = GS_SWAP;
        audio_sfx(SFX_MENU);
        return;
    }
    if (!world_item_take(item, p->tag)) return;
    inv_add(&p->pack, (uint8_t)item);
    ui_toast(g, S_M_TAKEN, item_name(g_world.items[item].type));
    sfx_from(g, SFX_PICK, g_world.items[item].x, g_world.items[item].y);
    make_noise(g, p->body.x >> 8, p->body.y >> 8, p->body.sector, NOISE_SOFT);
}

static int use_key(Game *g, uint8_t type, uint32_t flag, uint8_t msg) {
    int slot = inv_find_type(&g->player.pack, type);
    if (slot < 0) {
        ui_toast(g, S_M_LOCKED, S_NONE);
        return 0;
    }
    world_item_use(g->player.pack.slot[slot]);
    inv_remove(&g->player.pack, slot);
    world_set_flags(flag);
    ui_toast(g, msg, S_NONE);
    if (g->target.kind == TGT_ENTITY)
        sfx_from(g, SFX_DOOR, g_map_entities[g->target.index].x, g_map_entities[g->target.index].y);
    make_noise(g, g->player.body.x >> 8, g->player.body.y >> 8, g->player.body.sector, NOISE_SOFT * 2);
    return 1;
}

static void door_action(Game *g, const MapEntity *ent) {
    uint32_t f = g_world.flags;
    switch (ent->arg) {
    case DOOR_GATE_A: use_key(g, ITEM_KEY_A, WF_GATE_A_OPEN, S_M_GATE_OPENED); break;
    case DOOR_GRATE: use_key(g, ITEM_KEY_B, WF_GRATE_OPEN, S_M_GRATE_OPENED); break;
    case DOOR_WALL_2007:
        if (!(f & WF_SHELTER_CLUES_COMPLETE)) {
            ui_toast(g, S_M_WALL_PLAIN, S_NONE);
        } else if (!(f & WF_WALL_IDENTIFIED)) {
            world_set_flags(WF_WALL_IDENTIFIED);
            ui_toast(g, S_P_DIFFERENT, S_NONE);
            audio_sfx(SFX_REGISTER);
        } else if (!has_type(g, ITEM_TOOL)) {
            ui_toast(g, S_M_NEED_TOOL, S_NONE);
        }
        break;
    default:
        break;
    }
}

static void cistern_action(Game *g, int e) {
    const MapEntity *ent = &g_map_entities[e];
    unlock_archive(g, ARC_CISTERNS);
    if (!(g_world.flags & WF_KEY_A_FOUND) && has_type(g, ITEM_ROPE)) {
        int key = world_item_by_type(ITEM_KEY_A);
        if (world_item_reveal(key, ent->x, ent->y, ent->sector)) {
            world_set_flags(WF_KEY_A_FOUND);
            ui_toast(g, S_M_ROPE_KEY, S_NONE);
            sfx_from(g, SFX_WATER, ent->x, ent->y);
            if (!inv_full(&g->player.pack) && world_item_take(key, g->player.tag))
                inv_add(&g->player.pack, (uint8_t)key);
        }
        return;
    }
    if (!(g_world.flags & WF_KEY_A_FOUND)) ui_toast(g, S_M_CISTERN_DEEP, S_NONE);
    open_archive_entry(g, ARC_CISTERNS, GS_PLAY);
}

static void interact(Game *g) {
    if (g->target.kind == TGT_ITEM) {
        take_item(g, g->target.index);
        return;
    }
    if (g->target.kind != TGT_ENTITY) return;
    int e = g->target.index;
    const MapEntity *ent = &g_map_entities[e];
    uint32_t bit = 1u << e;
    switch (ent->kind) {
    case ENT_HIST:
    case ENT_DECOR:
        if (!(g->registered & bit) && has_recorder(g)) {
            g->registered |= bit;
            ++g->finds;
            audio_sfx(SFX_REGISTER);
        }
        unlock_archive(g, ent->archive);
        if (ent->archive == ARC_DISCOVERY || ent->archive == ARC_STEPS) g->discovery_ticks = TICKS(4000);
        open_archive_entry(g, ent->archive, GS_PLAY);
        break;
    case ENT_CLUE: {
        uint32_t flag = WF_CLUE_1 << (ent->arg - 1);
        if (!(g_world.flags & flag)) {
            if (has_recorder(g)) {
                world_set_flags(flag);
                g->registered |= bit;
                ++g->finds;
                ui_toast(g, S_M_REGISTERED, S_NONE);
                audio_sfx(SFX_REGISTER);
                if ((g_world.flags & WF_ALL_CLUES) == WF_ALL_CLUES) {
                    world_set_flags(WF_SHELTER_CLUES_COMPLETE);
                    ui_toast(g, S_M_CLUES_DONE, S_NONE);
                }
            } else {
                ui_toast(g, S_M_NEED_NOTEBOOK, S_NONE);
            }
        }
        if (ent->archive != 255) {
            unlock_archive(g, ent->archive);
            open_archive_entry(g, ent->archive, GS_PLAY);
        } else {
            open_read(g, ent->name, ent->desc, S_NONE, GS_PLAY);
        }
        break;
    }
    case ENT_FICTION:
        if (!(g->registered & bit)) {
            g->registered |= bit;
            ui_toast(g, S_M_SOMEONE, S_NONE);
        }
        unlock_archive(g, ARC_FICTION);
        open_read(g, ent->name, ent->desc, S_L_FICTION, GS_PLAY);
        break;
    case ENT_CISTERN: cistern_action(g, e); break;
    case ENT_DOOR: door_action(g, ent); break;
    default: break;
    }
}

static void spawn_dust(Game *g, int32_t x, int32_t y, int sector) {
    for (int i = 0; i < MAX_PARTICLES; ++i) {
        Particle *pt = &g->particles[i];
        pt->x = x + (i * 37 % 120) - 60;
        pt->y = y + (i * 53 % 160) - 80;
        pt->z = g_map_sectors[sector].floor_z + 60 + i * 20;
        pt->vz = (int16_t)(2 + (i & 3));
        pt->sector = (uint8_t)sector;
        pt->life = (uint8_t)(TICKS(2500) + i * 4);
    }
}

static void open_wall(Game *g, const MapEntity *ent) {
    world_set_flags(WF_WALL_OPEN);
    g->wall_progress = 0;
    g->silence_ticks = TICKS(5000);       /* beat 7: the music stops */
    spawn_dust(g, ent->x - 60, ent->y, ent->sector);
    ui_toast(g, S_M_WALL_OPEN, S_NONE);
    sfx_from(g, SFX_RUMBLE, ent->x, ent->y);
}

static void update_wall_force(Game *g) {
    int forcing = 0;
    if (g->target.kind == TGT_ENTITY && g->in.a_hold) {
        const MapEntity *ent = &g_map_entities[g->target.index];
        if (ent->kind == ENT_DOOR && ent->arg == DOOR_WALL_2007 &&
            (g_world.flags & WF_WALL_IDENTIFIED) && !(g_world.flags & WF_WALL_OPEN) &&
            has_type(g, ITEM_TOOL)) {
            forcing = 1;
            int helper = mp_peer_near(ent->x, ent->y, 350);
            if (helper && !g->helper) ui_toast(g, S_M_HELPER, S_NONE);
            g->helper = (uint8_t)helper;
            g->wall_progress = (uint16_t)(g->wall_progress + (helper ? 2 : 1));
            if (g->wall_progress % 20 < 2) sfx_from(g, SFX_DOOR, ent->x, ent->y);
            make_noise(g, ent->x, ent->y, ent->sector, NOISE_SOFT);
            if (g->wall_progress >= WALL_FORCE_TICKS) open_wall(g, ent);
        }
    }
    if (!forcing && g->wall_progress) --g->wall_progress;
}

/* ------------------------------------------------------------------ */
/* Player, triggers, antagonist                                        */
/* ------------------------------------------------------------------ */

static void update_player(Game *g) {
    Player *p = &g->player;
    const InputState *in = &g->in;
    int32_t turn = (G2007_TURN_UNITS_S * G2007_TICK_MS) / 1000;
    if (in->held & IN_LEFT) p->angle = (p->angle + turn) & ANG_MASK;
    if (in->held & IN_RIGHT) p->angle = (p->angle - turn) & ANG_MASK;
    int dir = (in->held & IN_UP) ? 1 : ((in->held & IN_DOWN) ? -1 : 0);
    if (dir) {
        int32_t step = (G2007_WALK_CM_S * G2007_TICK_MS * 256) / 1000 * dir;
        if (dir < 0) step = step * 2 / 3;
        int32_t mx = (fx_cos(p->angle) * step) >> FX_SHIFT;
        int32_t my = (fx_sin(p->angle) * step) >> FX_SHIFT;
        if (world_try_move(&p->body, mx, my, MOVER_PLAYER) ||
            world_try_move(&p->body, mx, 0, MOVER_PLAYER) ||
            world_try_move(&p->body, 0, my, MOVER_PLAYER)) {
            p->walk_cm += fx_abs(step) >> 8;
            if (p->walk_cm > 75) {
                p->walk_cm = 0;
                audio_sfx(SFX_STEP);
            }
        }
    }
    int32_t target = g_map_sectors[p->body.sector].floor_z + G2007_EYE_HEIGHT;
    p->eye_z += (target - p->eye_z) / 3;
    if (fx_abs(target - p->eye_z) < 3) p->eye_z = target;
    if (in->act_b) {
        p->light ^= 1u;
        audio_sfx(SFX_CLICK);
    }
    if (p->boost_ticks) --p->boost_ticks;
}

static void update_triggers(Game *g) {
    Player *p = &g->player;
    const MapSector *sec = &g_map_sectors[p->body.sector];
    uint8_t zone = sec->zone;
    g->zones_seen |= (uint8_t)(1u << zone);
    if (sec->flags & SECF_CHECKPOINT) {
        for (int e = 0; e < MAP_ENTITY_COUNT; ++e) {
            if (g_map_entities[e].kind == ENT_SPAWN && g_map_entities[e].sector == p->body.sector)
                p->checkpoint = (uint8_t)e;
        }
    }
    if (zone != g->last_zone) {
        g->last_zone = zone;
        if (zone == ZONE_VEHICLES && !(g_world.flags & WF_SIGHTING_DONE)) {
            /* Beat 3: a distant light crosses the hall and vanishes. */
            world_set_flags(WF_SIGHTING_DONE);
            ai_place(&g->lamp, world_entity_of_kind(ENT_SIGHTING, 0), AI_SCRIPTED);
            int grate = world_entity_of_kind(ENT_DOOR, DOOR_GRATE);
            g->lamp.tx = g_map_entities[grate].x;
            g->lamp.ty = g_map_entities[grate].y;
            g->lamp.tsector = g_map_entities[grate].sector;
            g->alert_ticks = TICKS(5000);
            ui_toast(g, S_M_LIGHT_FAR, S_NONE);
        }
        if (zone == ZONE_CAVITY && (g_world.flags & WF_WALL_OPEN) &&
            !(g_world.flags & WF_CAVITY_ENTERED)) {
            world_set_flags(WF_CAVITY_ENTERED);
            unlock_archive(g, ARC_DISCOVERY);
            g->discovery_ticks = TICKS(4500);
        }
    }
    if (sec->flags & SECF_END) {
        world_set_flags(WF_STAIRS_FOUND);
        unlock_archive(g, ARC_STEPS);
        unlock_archive(g, ARC_FICTION);
        g->discovery_ticks = TICKS(4500);
        g->state = GS_END;
        g->state_ticks = 0;
    }
}

static void update_lampman(Game *g) {
    LampMan *lm = &g->lamp;
    if ((g_world.flags & WF_WALL_OPEN) && !g->lamp_started) {
        /* Beat 9: the other light returns, now close enough to matter. */
        g->lamp_started = 1;
        world_set_flags(WF_LAMPMAN_TRIGGERED);
        ai_place(lm, world_entity_of_kind(ENT_LAMPMAN, 0), AI_PATROL);
        int wall = world_entity_of_kind(ENT_DOOR, DOOR_WALL_2007);
        make_noise(g, g_map_entities[wall].x - 120, g_map_entities[wall].y,
                   world_find_sector(-1, g_map_entities[wall].x - 120, g_map_entities[wall].y),
                   NOISE_LOUD);
    }
    const Player *p = &g->player;
    AiSense s;
    s.x = p->body.x >> 8;
    s.y = p->body.y >> 8;
    s.sector = p->body.sector;
    s.light = p->light;
    s.valid = (uint8_t)(g->state != GS_CAUGHT && g->state != GS_END);
    s.noise = 0;
    s.noise_x = g->noise_x;
    s.noise_y = g->noise_y;
    s.noise_sector = g->noise_sector;
    if (g->noise && lm->state >= AI_PATROL) {
        int32_t dx = (g->noise_x - (lm->body.x >> 8)) >> 6, dy = (g->noise_y - (lm->body.y >> 8)) >> 6;
        s.noise = (uint8_t)(dx * dx + dy * dy <= (int32_t)g->noise * g->noise);
    }
    g->noise = 0;
    uint8_t before = lm->state;
    int32_t ox = lm->body.x, oy = lm->body.y;
    ai_tick(lm, &s);
    int32_t lx = lm->body.x >> 8, ly = lm->body.y >> 8;
    if (lm->state == AI_SEE && before != AI_SEE) sfx_from(g, SFX_ALERT, lx, ly);
    /* His footsteps, placed in the stereo field: hear where he walks. */
    if (lm->state != AI_INACTIVE) {
        g->lamp_walk += fx_abs(lm->body.x - ox) + fx_abs(lm->body.y - oy);
        if (g->lamp_walk > 85 * 256) {
            g->lamp_walk = 0;
            sfx_from(g, SFX_LAMP_STEP, lx, ly);
        }
    }
    if (ai_caught(lm, &s) && g->state == GS_PLAY) {
        g->state = GS_CAUGHT;
        g->state_ticks = 0;
        audio_sfx(SFX_CAUGHT);
    }
}

static void respawn_after_capture(Game *g) {
    spawn_player(g);
    ai_place(&g->lamp, world_entity_of_kind(ENT_LAMPMAN, 0), AI_PATROL);
    g->lamp.cooldown = TICKS(6000);
    g->wall_progress = 0;
    g->state = GS_PLAY;
}

static void update_particles(Game *g) {
    for (int i = 0; i < MAX_PARTICLES; ++i) {
        Particle *pt = &g->particles[i];
        if (!pt->life) continue;
        --pt->life;
        pt->z -= pt->vz;
        int32_t floor = g_map_sectors[pt->sector].floor_z;
        if (pt->z < floor) pt->z = floor;
    }
}

static void choose_music(Game *g) {
    uint8_t m = MUS_EXPLORATION;
    uint8_t zone = g_map_sectors[g->player.body.sector].zone;
    const LampMan *lm = &g->lamp;
    if (g->state == GS_TITLE) {
        m = MUS_EXPLORATION;
    } else if (g->state == GS_INTRO) {
        m = MUS_SILENCE;
    } else if (g->state >= GS_END) {
        m = g->discovery_ticks ? MUS_DISCOVERY : MUS_MEMORY;
    } else if (lm->state == AI_CHASE) {
        m = MUS_CHASE;
    } else if (g->silence_ticks) {
        m = MUS_SILENCE;
    } else if (g->discovery_ticks) {
        m = MUS_DISCOVERY;
    } else if (g->alert_ticks || (lm->state >= AI_PATROL && ai_is_threatening(lm))) {
        m = MUS_THREAT;
    } else if (zone == ZONE_SHELTER) {
        m = MUS_MEMORY;
    } else if (zone == ZONE_PASSAGE || zone == ZONE_CAVITY || zone == ZONE_STAIRS ||
               (zone == ZONE_VEHICLES && (g_world.flags & WF_SIGHTING_DONE))) {
        m = MUS_MYSTERY;
    }
    audio_request(m);
}

/* ------------------------------------------------------------------ */
/* Menus                                                               */
/* ------------------------------------------------------------------ */

static int menu_move(uint8_t *sel, int count, const InputState *in) {
    if (count <= 0) return 0;
    if (in->pressed & IN_UP) { *sel = (uint8_t)((*sel + count - 1) % count); audio_sfx(SFX_MENU); return 1; }
    if (in->pressed & IN_DOWN) { *sel = (uint8_t)((*sel + 1) % count); audio_sfx(SFX_MENU); return 1; }
    return 0;
}

static void open_inventory(Game *g, uint8_t page, uint8_t archive_only, uint8_t ret) {
    g->inv_page = page;
    g->archive_only = archive_only;
    g->inv_return = ret;
    g->inv_confirm = 0;
    g->state = GS_INVENTORY;
    audio_sfx(SFX_MENU);
}

static void use_or_drop(Game *g) {
    Player *p = &g->player;
    uint8_t item = p->pack.slot[g->inv_sel];
    if (item == INV_EMPTY) return;
    uint8_t type = g_world.items[item].type;
    if (type == ITEM_BATTERY) {
        world_item_use(item);
        inv_remove(&p->pack, g->inv_sel);
        p->boost_ticks = (uint16_t)TICKS(90000);
        p->light = 1;
        ui_toast(g, S_M_BATTERY, S_NONE);
        audio_sfx(SFX_CLICK);
        return;
    }
    if (type == ITEM_MAP && !g->inv_confirm) {
        g->inv_confirm = 1;              /* next A drops it */
        open_read(g, S_N_MAP, S_D_MAP, S_NONE, GS_INVENTORY);
        return;
    }
    if (!g->inv_confirm) {
        g->inv_confirm = 1;
        audio_sfx(SFX_MENU);
        return;
    }
    g->inv_confirm = 0;
    if (world_item_drop(item, p->body.x >> 8, p->body.y >> 8, p->body.sector)) {
        inv_remove(&p->pack, g->inv_sel);
        ++g->shared;
        ui_toast(g, S_M_DROPPED, item_name(type));
        audio_sfx(SFX_DROP);
        if (g->inv_sel && g->inv_sel >= inv_count(&p->pack)) --g->inv_sel;
    } else {
        ui_toast(g, S_M_CANT_DROP, S_NONE);
    }
}

static void update_inventory(Game *g) {
    const InputState *in = &g->in;
    if (in->act_b || in->act_ab || (in->pressed & IN_START)) {
        g->state = g->inv_return;
        audio_sfx(SFX_MENU);
        return;
    }
    if (!g->archive_only && (in->pressed & (IN_LEFT | IN_RIGHT))) {
        g->inv_page ^= 1u;
        g->inv_confirm = 0;
        audio_sfx(SFX_MENU);
    }
    if (g->inv_page == 0) {
        if (menu_move(&g->inv_sel, inv_count(&g->player.pack), in)) g->inv_confirm = 0;
        if (in->act_a) use_or_drop(g);
    } else {
        menu_move(&g->arc_sel, ARC_COUNT, in);
        if (in->act_a && (g->archive & (1u << g->arc_sel))) open_archive_entry(g, g->arc_sel, GS_INVENTORY);
    }
}

static void update_swap(Game *g) {
    const InputState *in = &g->in;
    menu_move(&g->inv_sel, BACKPACK_SLOTS, in);
    if (in->act_b || in->act_ab) { g->state = GS_PLAY; return; }
    if (!in->act_a) return;
    Player *p = &g->player;
    uint8_t target = g->swap_target;
    if (g_world.items[target].state != IS_WORLD) {
        ui_toast(g, S_M_ALREADY_TAKEN, S_NONE);
    } else if (inv_swap(&p->pack, g->inv_sel, target, p->tag, p->body.x >> 8, p->body.y >> 8,
                        p->body.sector)) {
        ++g->shared;
        ui_toast(g, S_M_TAKEN, item_name(g_world.items[target].type));
        audio_sfx(SFX_PICK);
    } else {
        ui_toast(g, S_M_CANT_DROP, S_NONE);
    }
    g->state = GS_PLAY;
}

static void update_play(Game *g, const Camera *cam) {
    const InputState *in = &g->in;
    update_player(g);
    find_target(g, cam);
    if (in->act_ab || (in->pressed & IN_START)) {
        open_inventory(g, 0, 0, GS_PLAY);
        return;
    }
    if (in->act_a) interact(g);
    update_wall_force(g);
    update_triggers(g);
}

static void tick(Game *g) {
    Camera cam;
    cam.x = g->player.body.x >> 8;
    cam.y = g->player.body.y >> 8;
    cam.angle = g->player.angle;
    const InputState *in = &g->in;
    int confirm = in->act_a || in->act_b || in->act_ab || (in->pressed & IN_START);
    ++g->state_ticks;
    switch (g->state) {
    case GS_TITLE:
        menu_move(&g->menu, 2, in);
        if (in->act_a || (in->pressed & IN_START)) {
            if (g->menu == 0) { new_session(g); g->state = GS_INTRO; }
            else open_inventory(g, 1, 1, GS_TITLE);
        }
        break;
    case GS_INTRO:
        if ((g->state_ticks > TICKS(800) && confirm) || g->state_ticks > TICKS(6000)) g->state = GS_PLAY;
        break;
    case GS_PLAY: update_play(g, &cam); break;
    case GS_INVENTORY: update_inventory(g); break;
    case GS_SWAP: update_swap(g); break;
    case GS_READ:
        if (confirm) { g->state = g->read_return; audio_sfx(SFX_MENU); }
        break;
    case GS_CAUGHT:
        if (g->state_ticks > TICKS(3000) || (g->state_ticks > TICKS(1200) && confirm))
            respawn_after_capture(g);
        break;
    case GS_END:
        if (g->state_ticks > TICKS(2500) && confirm) { g->state = GS_OUTRO; g->menu = 0; }
        break;
    case GS_OUTRO:
        menu_move(&g->menu, 3, in);
        if (in->act_a) {
            if (g->menu == 0) open_inventory(g, 1, 1, GS_OUTRO);
            else if (g->menu == 1) { new_session(g); g->state = GS_INTRO; }
            else g->state = GS_INFO;
        }
        break;
    case GS_INFO:
        if (confirm) g->state = GS_OUTRO;
        break;
    default:
        break;
    }
    if (g->state != GS_TITLE && g->state != GS_INTRO && g->state < GS_END) {
        update_lampman(g);
        mp_tick(g);
        if (inv_reconcile(&g->player.pack, g->player.tag)) ui_toast(g, S_M_ALREADY_TAKEN, S_NONE);
        update_particles(g);
        ++g->play_ticks;
    } else if (g->state >= GS_END) {
        mp_tick(g);
    }
    if (g->toast_ticks) --g->toast_ticks;
    if (g->discovery_ticks) --g->discovery_ticks;
    if (g->silence_ticks) --g->silence_ticks;
    if (g->alert_ticks) --g->alert_ticks;
    choose_music(g);
}

/* ------------------------------------------------------------------ */
/* Public entry points                                                 */
/* ------------------------------------------------------------------ */

const char *str(int id) {
    if (id < 0 || id >= S_COUNT) id = S_NONE;
    return g_text + g_text_off[id];
}

void game_init(void) {
    Game *g = &g_game;
    uint8_t *raw = (uint8_t *)g;
    for (unsigned i = 0; i < sizeof(*g); ++i) raw[i] = 0;
    render_init();
    audio_init();
    input_reset(&g->in);
    g->player.tag = (uint16_t)prg32_random_number(1, 65535);
    new_session(g);
    g->state = GS_TITLE;
    mp_init();
    g->last_ms = prg32_ticks_ms();
}

void game_update(void) {
    Game *g = &g_game;
    uint32_t now = prg32_ticks_ms();
    uint32_t elapsed = now - g->last_ms;
    g->last_ms = now;
    if (elapsed > G2007_TICK_MS * G2007_MAX_TICKS) elapsed = G2007_TICK_MS * G2007_MAX_TICKS;
    g->accum_ms += elapsed;
    input_update(&g->in);
    int first = 1;
    while (g->accum_ms >= G2007_TICK_MS) {
        g->accum_ms -= G2007_TICK_MS;
        if (!first) {               /* edges belong to the first tick only */
            g->in.act_a = g->in.act_b = g->in.act_ab = 0;
            g->in.pressed = 0;
        }
        tick(g);
        first = 0;
    }
    audio_update(now, g_map_sectors[g->player.body.sector].zone == ZONE_CISTERN);
}

static int item_sprite(uint8_t type) {
    switch (type) {
    case ITEM_NOTEBOOK: return SPR_ITEM_NOTEBOOK;
    case ITEM_CAMERA: return SPR_ITEM_CAMERA;
    case ITEM_ROPE: return SPR_ITEM_ROPE;
    case ITEM_BATTERY: return SPR_ITEM_BATTERY;
    case ITEM_TOOL: return SPR_ITEM_TOOL;
    case ITEM_MAP: return SPR_ITEM_MAP;
    default: return SPR_ITEM_KEY;
    }
}

static int add_ref(SpriteRef *refs, int n, int32_t x, int32_t y, int sector, int z_above,
                   uint8_t sprite, uint8_t kind, uint8_t tint, uint8_t size) {
    if (n >= MAX_VISIBLE_SPRITES + 8 || sector < 0) return n;
    SpriteRef *r = &refs[n];
    r->x = x;
    r->y = y;
    r->sector = (uint8_t)sector;
    r->z = (int16_t)(g_map_sectors[sector].floor_z + z_above);
    r->sprite = sprite;
    r->kind = kind;
    r->tint = tint;
    r->size = size;
    return n + 1;
}

void game_draw(void) {
    Game *g = &g_game;
    static SpriteRef refs[MAX_VISIBLE_SPRITES + 8];
    int n = 0;
    Camera cam;
    cam.x = g->player.body.x >> 8;
    cam.y = g->player.body.y >> 8;
    cam.z = g->player.eye_z;
    cam.angle = g->player.angle;
    cam.sector = g->player.body.sector;
    cam.beam = (uint8_t)(g->player.light ? (g->player.boost_ticks ? 2 : 1) : 0);
    if (g->state == GS_TITLE) cam.beam = 1;
    for (int i = 0; i < g_world.item_count; ++i) {
        const ItemInst *it = &g_world.items[i];
        if (it->state == IS_WORLD)
            n = add_ref(refs, n, it->x, it->y, it->sector, 0, (uint8_t)item_sprite(it->type), SK_SPRITE, 0, 0);
    }
    for (int e = 0; e < MAP_ENTITY_COUNT; ++e) {
        const MapEntity *ent = &g_map_entities[e];
        if (ent->kind != ENT_ITEM && ent->sprite != 255)
            n = add_ref(refs, n, ent->x, ent->y, ent->sector, 0, ent->sprite, SK_SPRITE, 0, 0);
    }
    for (int i = 0; i < g->remote_count; ++i) {
        const Remote *r = &g->remotes[i];
        n = add_ref(refs, n, r->x, r->y, r->sector, 0, SPR_PLAYER, SK_SPRITE, r->tint, 0);
        if (r->light) n = add_ref(refs, n, r->x, r->y, r->sector, 165, 0, SK_GLOW, 0, 5);
    }
    const LampMan *lm = &g->lamp;
    if (lm->state != AI_INACTIVE) {
        int32_t lx = lm->body.x >> 8, ly = lm->body.y >> 8;
        n = add_ref(refs, n, lx, ly, lm->body.sector, 0, SPR_LAMPMAN, SK_SPRITE, 0, 0);
        n = add_ref(refs, n, lx, ly, lm->body.sector, 95, 0, SK_GLOW, 0, 7);
    }
    for (int i = 0; i < MAX_PARTICLES; ++i) {
        const Particle *pt = &g->particles[i];
        if (pt->life)
            n = add_ref(refs, n, pt->x, pt->y, pt->sector,
                        pt->z - g_map_sectors[pt->sector].floor_z, 0, SK_DUST, 0, 4);
    }
    render_frame(&cam, refs, n);
    ui_draw(g);
}
