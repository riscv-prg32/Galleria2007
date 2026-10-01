/*
 * LampMan finite-state machine (see ai.h).
 *
 *   PATROL --hear--> HEAR --timer--> INVESTIGATE --arrive/timer--> SEARCH
 *     |                                   |                          |
 *     +--see--> SEE --timer & sees--> CHASE --lost sight--> SEARCH --timer--> RETURN --arrive--> PATROL
 *
 * Every state has a timer or an arrival condition, so without stimuli the
 * machine always drifts back to PATROL (tested in tests/test_ai.c).
 */
#include "ai.h"
#include "fixed.h"

#define AI_WAYPOINT_MAX 8
#define AI_RETURN_TIMEOUT 600

uint16_t ai_state_ticks(uint8_t state) {
    switch (state) {
    case AI_HEAR: return 18;
    case AI_INVESTIGATE: return 330;
    case AI_SEE: return 14;
    case AI_CHASE: return 75;          /* time allowed without sight */
    case AI_SEARCH: return 160;
    case AI_RETURN: return AI_RETURN_TIMEOUT;
    default: return 0;
    }
}

uint8_t ai_think(uint8_t state, const AiStimuli *st) {
    switch (state) {
    case AI_PATROL:
        if (st->sees) return AI_SEE;
        if (st->hears) return AI_HEAR;
        return AI_PATROL;
    case AI_HEAR:
        if (st->sees) return AI_SEE;
        return st->timer_done ? AI_INVESTIGATE : AI_HEAR;
    case AI_INVESTIGATE:
        if (st->sees) return AI_SEE;
        if (st->hears) return AI_HEAR;
        return (st->at_target || st->timer_done) ? AI_SEARCH : AI_INVESTIGATE;
    case AI_SEE:
        if (!st->timer_done) return AI_SEE;
        return st->sees ? AI_CHASE : AI_SEARCH;
    case AI_CHASE:
        return (!st->sees && st->timer_done) ? AI_SEARCH : AI_CHASE;
    case AI_SEARCH:
        if (st->sees) return AI_SEE;
        if (st->hears) return AI_HEAR;
        return st->timer_done ? AI_RETURN : AI_SEARCH;
    case AI_RETURN:
        if (st->sees) return AI_SEE;
        if (st->hears) return AI_HEAR;
        return (st->at_target || st->timer_done) ? AI_PATROL : AI_RETURN;
    default:
        return state;   /* INACTIVE and SCRIPTED are driven by the game */
    }
}

static int32_t dist2(int32_t ax, int32_t ay, int32_t bx, int32_t by) {
    int32_t dx = (ax - bx) >> 2, dy = (ay - by) >> 2;   /* /4 avoids overflow */
    return dx * dx + dy * dy;
}

int ai_can_see(const LampMan *lm, const AiSense *sense) {
    if (!sense->valid || lm->cooldown) return 0;
    int32_t range = sense->light ? AI_SIGHT_LIGHT_CM : AI_SIGHT_DARK_CM;
    int32_t d2 = dist2(lm->body.x >> 8, lm->body.y >> 8, sense->x, sense->y);
    if (d2 > (range >> 2) * (range >> 2)) return 0;
    if (d2 <= (AI_SIGHT_CLOSE_CM >> 2) * (AI_SIGHT_CLOSE_CM >> 2)) return 1;
    return world_line_of_sight(lm->body.sector, lm->body.x >> 8, lm->body.y >> 8,
                               sense->sector, sense->x, sense->y);
}

int ai_caught(const LampMan *lm, const AiSense *sense) {
    if (lm->state < AI_PATROL || !sense->valid || lm->cooldown) return 0;
    return dist2(lm->body.x >> 8, lm->body.y >> 8, sense->x, sense->y) <
           (AI_CATCH_CM >> 2) * (AI_CATCH_CM >> 2);
}

int ai_is_threatening(const LampMan *lm) {
    return lm->state == AI_SEE || lm->state == AI_CHASE || lm->state == AI_SEARCH ||
           lm->state == AI_INVESTIGATE || lm->state == AI_HEAR;
}

void ai_reset(LampMan *lm) {
    uint8_t *raw = (uint8_t *)lm;
    for (unsigned i = 0; i < sizeof(*lm); ++i) raw[i] = 0;
    lm->state = AI_INACTIVE;
    lm->seed = 0x2007u;
}

static void set_target_entity(LampMan *lm, int e) {
    if (e < 0) return;
    lm->tx = g_map_entities[e].x;
    lm->ty = g_map_entities[e].y;
    lm->tsector = g_map_entities[e].sector;
}

void ai_place(LampMan *lm, int entity, uint8_t state) {
    if (entity < 0) return;
    const MapEntity *e = &g_map_entities[entity];
    lm->body.x = (int32_t)e->x * 256;
    lm->body.y = (int32_t)e->y * 256;
    lm->body.sector = e->sector;
    lm->state = state;
    lm->timer = ai_state_ticks(state);
    lm->waypoint = 0;
    lm->tx = e->x;
    lm->ty = e->y;
    lm->tsector = e->sector;
    if (state == AI_PATROL) set_target_entity(lm, world_entity_of_kind(ENT_WAYPOINT, 0));
}

/* Walk towards (tx, ty): straight when in the same sector, otherwise towards
 * the middle of the next portal on the shortest sector path. */
static void move_towards(LampMan *lm, int32_t speed_cm_s) {
    int32_t gx = lm->tx, gy = lm->ty;
    if (lm->body.sector != lm->tsector) {
        int w = world_next_portal(lm->body.sector, lm->tsector, MOVER_AI);
        if (w < 0) { lm->moving = 0; return; }
        world_wall_midpoint(w, &gx, &gy);
        /* Aim slightly past the portal so the body actually crosses it. */
        gx -= g_map_walls[w].nx * 40 / FX_ONE;
        gy -= g_map_walls[w].ny * 40 / FX_ONE;
    }
    int32_t dx = gx - (lm->body.x >> 8), dy = gy - (lm->body.y >> 8);
    int32_t len = (int32_t)fx_isqrt((uint32_t)(dx * dx + dy * dy));
    if (len < 4) { lm->moving = 0; return; }
    int32_t step_q8 = (speed_cm_s * G2007_TICK_MS * 256) / 1000;
    int32_t mx = fx_muldiv(dx, step_q8, len), my = fx_muldiv(dy, step_q8, len);
    lm->moving = (uint8_t)(world_try_move(&lm->body, mx, my, MOVER_AI) ||
                           world_try_move(&lm->body, mx, 0, MOVER_AI) ||
                           world_try_move(&lm->body, 0, my, MOVER_AI));
}

static int at_target(const LampMan *lm) {
    return lm->body.sector == lm->tsector &&
           dist2(lm->body.x >> 8, lm->body.y >> 8, lm->tx, lm->ty) < 50 * 50 / 16;
}

static uint16_t next_rand(LampMan *lm) {
    lm->seed = (uint16_t)(lm->seed * 25173u + 13849u);
    return lm->seed;
}

static void enter_state(LampMan *lm, uint8_t state, const AiSense *sense) {
    lm->state = state;
    lm->timer = ai_state_ticks(state);
    switch (state) {
    case AI_HEAR:
    case AI_INVESTIGATE:
        if (sense->noise) {
            lm->tx = sense->noise_x;
            lm->ty = sense->noise_y;
            lm->tsector = sense->noise_sector;
        }
        break;
    case AI_CHASE:
        lm->tx = sense->x;
        lm->ty = sense->y;
        lm->tsector = sense->sector;
        break;
    case AI_SEARCH: {
        int32_t ox = (int32_t)(next_rand(lm) % 400u) - 200;
        int32_t oy = (int32_t)(next_rand(lm) % 400u) - 200;
        int s = world_find_sector(lm->tsector, lm->tx + ox, lm->ty + oy);
        if (s >= 0 && !(g_map_sectors[s].flags & SECF_NO_AI)) {
            lm->tx += ox;
            lm->ty += oy;
            lm->tsector = (int16_t)s;
        }
        break;
    }
    case AI_RETURN:
    case AI_PATROL:
        set_target_entity(lm, world_entity_of_kind(ENT_WAYPOINT, lm->waypoint));
        break;
    default:
        break;
    }
}

void ai_tick(LampMan *lm, const AiSense *sense) {
    if (lm->state == AI_INACTIVE) return;
    if (lm->state == AI_SCRIPTED) {
        move_towards(lm, 120);
        if (at_target(lm) || !lm->moving) lm->state = AI_INACTIVE;
        return;
    }
    if (lm->cooldown) --lm->cooldown;
    if (lm->timer) --lm->timer;
    AiStimuli st;
    st.sees = (uint8_t)ai_can_see(lm, sense);
    st.hears = (uint8_t)(sense->noise && !lm->cooldown);
    st.timer_done = (uint8_t)(lm->timer == 0);
    st.at_target = (uint8_t)at_target(lm);
    uint8_t next = ai_think(lm->state, &st);
    if (next != lm->state) {
        enter_state(lm, next, sense);
    } else if (next == AI_CHASE && st.sees) {
        lm->timer = ai_state_ticks(AI_CHASE);       /* refresh while in sight */
        lm->tx = sense->x;
        lm->ty = sense->y;
        lm->tsector = sense->sector;
    } else if (next == AI_PATROL && st.at_target) {
        int count = 0;
        while (count < AI_WAYPOINT_MAX && world_entity_of_kind(ENT_WAYPOINT, (uint8_t)count) >= 0) ++count;
        lm->waypoint = (uint8_t)(count ? (lm->waypoint + 1) % count : 0);
        set_target_entity(lm, world_entity_of_kind(ENT_WAYPOINT, lm->waypoint));
    }
    switch (lm->state) {
    case AI_PATROL: move_towards(lm, 70); break;
    case AI_INVESTIGATE: move_towards(lm, 110); break;
    case AI_CHASE: move_towards(lm, 150); break;
    case AI_SEARCH: move_towards(lm, 80); break;
    case AI_RETURN: move_towards(lm, 90); break;
    default: lm->moving = 0; break;
    }
}
