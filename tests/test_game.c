/* Gameplay tests on the real game code with the host PRG32 implementation.
 *
 *   - boot to title, intro, play;
 *   - the whole vertical slice is completable SOLO, using only the actions a
 *     player has (look at a thing, press or hold A, walk with UP);
 *   - full-backpack swap and recovery of a dropped item;
 *   - capture by LampMan and recovery at a checkpoint with progress kept;
 *   - multiplayer: shared wall opening and a lost simultaneous pickup.
 * Run for every language (tests/run_tests.sh builds IT and EN). */
#define G2007_HOST 1
#include <math.h>
#include "test_util.h"
#include "../src/galleria2007.c"
#include "host/host_prg32.h"

#define AX(x) ((x) - MAP_ORIGIN_X)
#define AY(y) ((y) - MAP_ORIGIN_Y)

static void frames(int n, uint32_t buttons) {
    for (int i = 0; i < n; ++i) {
        host_buttons = buttons;
        host_ms += G2007_TICK_MS;
        galleria2007_update();
        galleria2007_draw();
    }
    host_buttons = 0;
}

static void tap_a(void) {
    frames(1, PRG32_BTN_A);
    frames(6, 0);
}

/* Stand ~90 cm from (x, y) facing it, in a spot that sees it. */
static int face_point(int32_t x, int32_t y, int sector) {
    for (int k = 0; k < 16; ++k) {
        double a = k * 3.14159265358979 / 8.0;
        for (int dist = 90; dist <= 150; dist += 30) {
            int32_t px = x + (int32_t)lround(cos(a) * dist), py = y + (int32_t)lround(sin(a) * dist);
            int s = world_find_sector(sector, px, py);
            if (s < 0 || !world_line_of_sight(s, px, py, sector, x, y)) continue;
            Player *p = &g_game.player;
            p->body.x = px * 256;
            p->body.y = py * 256;
            p->body.sector = (int16_t)s;
            p->eye_z = g_map_sectors[s].floor_z + G2007_EYE_HEIGHT;
            double look = atan2((double)(y - py), (double)(x - px));
            p->angle = (int32_t)lround(look / (2 * 3.14159265358979) * ANG_FULL) & ANG_MASK;
            /* Close every panel, then render twice so the z-buffer is fresh. */
            g_game.state = GS_PLAY;
            frames(2, 0);
            if (g_game.target.kind != TGT_NONE) return 1;
        }
    }
    return 0;
}

static int face_entity(int e) {
    const MapEntity *ent = &g_map_entities[e];
    if (!face_point(ent->x, ent->y, ent->sector)) return 0;
    return g_game.target.kind == TGT_ENTITY && g_game.target.index == e;
}

static int face_item(int item) {
    const ItemInst *it = &g_world.items[item];
    if (!face_point(it->x, it->y, it->sector)) return 0;
    return g_game.target.kind == TGT_ITEM && g_game.target.index == item;
}

static int take(uint8_t type) {
    int item = world_item_by_type(type);
    CHECK(face_item(item));
    tap_a();
    if (g_game.state == GS_READ) tap_a();
    return inv_find_item(&g_game.player.pack, (uint8_t)item) >= 0;
}

static void use_entity(int e) {
    CHECK(face_entity(e));
    tap_a();
    if (g_game.state == GS_READ) tap_a();
}

static void place(int32_t x, int32_t y, int32_t angle) {
    Player *p = &g_game.player;
    p->body.x = AX(x) * 256;
    p->body.y = AY(y) * 256;
    p->body.sector = (int16_t)world_find_sector(-1, AX(x), AY(y));
    p->angle = angle;
    g_game.state = GS_PLAY;
    frames(1, 0);
}

static int entity(uint8_t kind, uint8_t arg) { return world_entity_of_kind(kind, arg); }

static int entity_named(uint8_t name) {
    for (int e = 0; e < MAP_ENTITY_COUNT; ++e)
        if (g_map_entities[e].name == name) return e;
    return -1;
}

static void start_game(void) {
    galleria2007_init();
    frames(5, 0);
    CHECK_EQ(g_game.state, GS_TITLE);
    tap_a();
    CHECK_EQ(g_game.state, GS_INTRO);
    frames(30, 0);
    tap_a();
    CHECK_EQ(g_game.state, GS_PLAY);
}

static void test_solo_walkthrough(void) {
    start_game();
    CHECK(host_mp_joined);
    CHECK(host_blits >= 20);
    CHECK(host_palette_sets > 0 && host_palette_sets % 224 == 0); /* 224 entries per init */
    /* Notebook and camera at the access. */
    CHECK(take(ITEM_NOTEBOOK));
    CHECK(take(ITEM_CAMERA));
    /* Historical point in the tunnel: register + Archive. */
    use_entity(entity_named(S_N_EXCAVATION));
    CHECK(g_game.archive & (1u << ARC_GALLERIA));
    CHECK_EQ(g_game.finds, 1);
    /* The torch toggles with B, and the backpack opens with A+B (not B). */
    uint8_t light = g_game.player.light;
    frames(1, PRG32_BTN_B); frames(8, 0);
    CHECK_EQ(g_game.player.light, light ^ 1u);
    frames(1, PRG32_BTN_B); frames(8, 0);
    frames(3, PRG32_BTN_A | PRG32_BTN_B); frames(4, 0);
    CHECK_EQ(g_game.state, GS_INVENTORY);
    CHECK_EQ(g_game.player.light, light);
    frames(1, PRG32_BTN_B); frames(4, 0);
    CHECK_EQ(g_game.state, GS_PLAY);
    /* Rope and battery, then the cistern gives key A (backpack becomes full). */
    CHECK(take(ITEM_ROPE));
    CHECK(take(ITEM_BATTERY));
    use_entity(entity(ENT_CISTERN, 0));
    CHECK(g_world.flags & WF_KEY_A_FOUND);
    CHECK(inv_find_type(&g_game.player.pack, ITEM_KEY_A) >= 0);
    CHECK(inv_full(&g_game.player.pack));
    /* The gate opens with key A and the key is consumed. */
    use_entity(entity(ENT_DOOR, DOOR_GATE_A));
    CHECK(g_world.flags & WF_GATE_A_OPEN);
    CHECK(inv_find_type(&g_game.player.pack, ITEM_KEY_A) < 0);
    CHECK(world_next_portal(0, world_find_sector(-1, AX(200), AY(4800)), MOVER_PLAYER) >= 0);
    /* Vehicle hall: the distant light appears; take the crowbar. */
    use_entity(entity_named(S_N_MOTO));
    CHECK(g_world.flags & WF_SIGHTING_DONE);
    CHECK(g_game.archive & (1u << ARC_DEPOSIT));
    CHECK(take(ITEM_TOOL));
    CHECK(inv_full(&g_game.player.pack));
    /* Full backpack: the map triggers the swap; leave the battery. */
    int map = world_item_by_type(ITEM_MAP);
    CHECK(face_item(map));
    tap_a();
    CHECK_EQ(g_game.state, GS_SWAP);
    int battery_slot = inv_find_type(&g_game.player.pack, ITEM_BATTERY);
    g_game.inv_sel = (uint8_t)battery_slot;
    tap_a();
    CHECK_EQ(g_game.state, GS_PLAY);
    CHECK(inv_find_type(&g_game.player.pack, ITEM_MAP) >= 0);
    int battery = world_item_by_type(ITEM_BATTERY);
    CHECK_EQ(g_world.items[battery].state, IS_WORLD);       /* persists in the world */
    /* Swap again: leave the map for key B. */
    int keyb = world_item_by_type(ITEM_KEY_B);
    CHECK(face_item(keyb));
    tap_a();
    CHECK_EQ(g_game.state, GS_SWAP);
    g_game.inv_sel = (uint8_t)inv_find_type(&g_game.player.pack, ITEM_MAP);
    tap_a();
    CHECK(inv_find_type(&g_game.player.pack, ITEM_KEY_B) >= 0);
    /* The wall is plain until the shelter clues are registered. */
    int wall = entity(ENT_DOOR, DOOR_WALL_2007);
    CHECK(face_entity(wall));
    CHECK_EQ(g_game.target.prompt, S_P_EXAMINE);
    for (uint8_t c = 1; c <= 3; ++c) use_entity(entity(ENT_CLUE, c));
    CHECK(g_world.flags & WF_SHELTER_CLUES_COMPLETE);
    CHECK(g_game.archive & (1u << ARC_SHELTERS));
    use_entity(entity_named(S_N_FOOTPRINTS));
    CHECK(g_game.archive & (1u << ARC_FICTION));
    /* Identify, then hold A with the crowbar to open the 2007 passage. */
    CHECK(face_entity(wall));
    CHECK_EQ(g_game.target.prompt, S_P_DIFFERENT);
    tap_a();
    CHECK(g_world.flags & WF_WALL_IDENTIFIED);
    CHECK(face_entity(wall));
    CHECK_EQ(g_game.target.prompt, S_P_FORCE);
    frames(160, PRG32_BTN_A);
    frames(2, 0);
    CHECK(g_world.flags & WF_WALL_OPEN);
    CHECK(g_game.lamp_started);
    CHECK(g_game.lamp.state >= AI_PATROL);
    CHECK_EQ(audio_current(), MUS_SILENCE);                  /* the music stops */
    /* Walk through the passage into the cavity (pressing UP only). */
    g_game.lamp.cooldown = 30000;                            /* keep the walk calm */
    place(400, 6500, ANG_FULL / 2);                          /* in the shelter, facing west */
    frames(120, PRG32_BTN_UP);
    CHECK_EQ(g_map_sectors[g_game.player.body.sector].zone, ZONE_CAVITY);
    CHECK(g_world.flags & WF_CAVITY_ENTERED);
    CHECK(g_game.archive & (1u << ARC_DISCOVERY));
    /* The stairs: examine, then climb every step to Vico del Grottone. */
    use_entity(entity_named(S_N_STAIRS));
    place(-900, 7200, ANG_QUARTER);                          /* cavity, facing north */
    for (int i = 0; i < 400 && g_game.state == GS_PLAY; ++i) frames(1, PRG32_BTN_UP);
    CHECK_EQ(g_game.state, GS_END);
    CHECK(g_world.flags & WF_STAIRS_FOUND);
    CHECK_EQ(g_game.archive, (1u << ARC_COUNT) - 1);
    /* End card, outro menu, information screen, replay. */
    frames(90, 0);
    tap_a();
    CHECK_EQ(g_game.state, GS_OUTRO);
    frames(1, PRG32_BTN_DOWN); frames(1, 0); frames(1, PRG32_BTN_DOWN); frames(2, 0);
    tap_a();
    CHECK_EQ(g_game.state, GS_INFO);
    tap_a();
    CHECK_EQ(g_game.state, GS_OUTRO);
    frames(1, PRG32_BTN_UP); frames(2, 0);
    tap_a();
    CHECK_EQ(g_game.state, GS_INTRO);
    CHECK_EQ(g_world.flags, 0u);                             /* new session */
    /* Stereo: effects were placed on both sides of the field. */
    CHECK(audio_is_stereo());
    CHECK(host_pan_left > 0);
    CHECK(host_pan_right > 0);
    CHECK_EQ(g_game.archive, (1u << ARC_COUNT) - 1);         /* knowledge kept */
}

static void test_capture_and_recovery(void) {
    start_game();
    world_set_flags(WF_GATE_A_OPEN | WF_SHELTER_CLUES_COMPLETE | WF_WALL_IDENTIFIED | WF_WALL_OPEN);
    frames(2, 0);
    CHECK(g_game.lamp_started);
    /* Stand in the cavity with the torch on, right next to LampMan. */
    LampMan *lm = &g_game.lamp;
    Player *p = &g_game.player;
    p->body.x = lm->body.x + 200 * 256;
    p->body.y = lm->body.y;
    p->body.sector = (int16_t)world_find_sector(lm->body.sector, p->body.x >> 8, p->body.y >> 8);
    p->light = 1;
    for (int i = 0; i < 300 && g_game.state != GS_CAUGHT; ++i) frames(1, 0);
    CHECK_EQ(g_game.state, GS_CAUGHT);
    frames(120, 0);
    CHECK_EQ(g_game.state, GS_PLAY);
    CHECK(g_world.flags & WF_WALL_OPEN);                     /* progress kept */
    CHECK(lm->cooldown > 0);
    CHECK_EQ(g_map_sectors[p->body.sector].zone, g_map_sectors[g_map_entities[p->checkpoint].sector].zone);
}

static void test_multiplayer(void) {
    start_game();
    /* A peer opened the wall: the monotonic world record reaches us. */
    host_peer_count = 1;
    prg32_player_state_t *peer = &host_peers[0];
    peer->player_id = 42;
    peer->x = (int16_t)AX(300);
    peer->y = (int16_t)AY(500);
    NetSnapshotBits bits;
    netp_pack(netp_world_record(WF_GATE_A_OPEN | WF_WALL_OPEN), 64, 1, 0, &bits);
    peer->sprite = bits.sprite; peer->flags = bits.flags; peer->input = bits.input;
    peer->frame = 10;
    frames(3, 0);
    CHECK(g_world.flags & WF_WALL_OPEN);
    CHECK_EQ(g_game.remote_count, 1);
    CHECK(g_game.remotes[0].light);
    /* A stale snapshot (same frame) is ignored; flags never close anyway. */
    netp_pack(netp_world_record(0), 64, 0, 0, &bits);
    peer->sprite = bits.sprite; peer->flags = bits.flags; peer->input = bits.input;
    frames(2, 0);
    CHECK(g_world.flags & WF_WALL_OPEN);
    /* Simultaneous pickup of the notebook: the peer's higher key wins. */
    CHECK(take(ITEM_NOTEBOOK));
    int nb = world_item_by_type(ITEM_NOTEBOOK);
    ItemInst theirs = g_world.items[nb];
    theirs.holder = 0xFFFF;                                  /* tie-break winner */
    netp_pack(netp_item_record(nb, &theirs), 64, 1, 0, &bits);
    peer->sprite = bits.sprite; peer->flags = bits.flags; peer->input = bits.input;
    peer->frame = 11;
    frames(3, 0);
    CHECK_EQ(g_world.items[nb].holder, 0xFFFF);
    CHECK(inv_find_type(&g_game.player.pack, ITEM_NOTEBOOK) < 0);
    CHECK_EQ(g_game.toast_a, S_M_ALREADY_TAKEN);
    /* Our own published snapshot carries a valid record and our torch. */
    CHECK(host_local.frame > 0);
    CHECK(host_local.input < 128);
    host_peer_count = 0;
}

static void test_stereo_positioning(void) {
    start_game();
    place(200, 1300, ANG_QUARTER);                           /* tunnel, facing north */
    int32_t x = AX(200), y = AY(1300);
    sfx_from(&g_game, SFX_LAMP_STEP, x + 400, y + 100);      /* east = right */
    CHECK(host_last_pan > 20);
    sfx_from(&g_game, SFX_LAMP_STEP, x - 400, y + 100);      /* west = left */
    CHECK(host_last_pan < -20);
    sfx_from(&g_game, SFX_LAMP_STEP, x, y + 600);            /* ahead = centre */
    CHECK(host_last_pan > -8 && host_last_pan < 8);
    int before = host_notes;
    sfx_from(&g_game, SFX_LAMP_STEP, x, y + 3000);           /* beyond hearing */
    CHECK_EQ(host_notes, before);
}

/* A host without the multiplayer feature (PRG32-iOS) still plays solo and
 * the cartridge never calls the multiplayer service. */
static void test_host_without_multiplayer(void) {
    host_features = PRG32_FEATURE_AUDIO | PRG32_FEATURE_AUDIO_PLUS | PRG32_FEATURE_SPRITES;
    host_mp_joined = 0;
    int frame_before = (int)host_local.frame;
    start_game();
    frames(60, PRG32_BTN_UP);
    CHECK(!host_mp_joined);
    CHECK_EQ((int)host_local.frame, frame_before);
    CHECK_EQ(g_game.state, GS_PLAY);
    host_features |= PRG32_FEATURE_MULTIPLAYER;
}

int main(void) {
    test_host_without_multiplayer();
    test_stereo_positioning();
    test_solo_walkthrough();
    test_capture_and_recovery();
    test_multiplayer();
    TEST_MAIN_END("test_game " G2007_LANG_CODE)
}
