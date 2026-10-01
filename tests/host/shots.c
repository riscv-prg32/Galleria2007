/* Render deterministic screenshots of the real game code on the host.
 *   build/shots/<name>.ppm  (tests/run_tests.sh shots converts them to PNG)
 * Views are set up by placing the player directly, so each shot shows a
 * specific place of the provisional map. */
#define G2007_HOST 1
#include "../../src/galleria2007.c"
#include "host_prg32.h"

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

static void place(int32_t x, int32_t y, int angle_deg, uint8_t light) {
    Player *p = &g_game.player;
    p->body.x = AX(x) * 256;
    p->body.y = AY(y) * 256;
    p->body.sector = (int16_t)world_find_sector(-1, AX(x), AY(y));
    p->angle = angle_deg * ANG_FULL / 360;
    p->light = light;
    p->eye_z = g_map_sectors[p->body.sector].floor_z + G2007_EYE_HEIGHT;
    g_game.state = GS_PLAY;
    frames(2, 0);
}

static void shot(const char *dir, const char *name) {
    char path[512];
    snprintf(path, sizeof path, "%s/%s.ppm", dir, name);
    host_write_ppm(path);
}

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : ".";
    galleria2007_init();
    frames(3, 0);
    shot(dir, "00_title");
    frames(1, PRG32_BTN_A);
    frames(40, 0);
    shot(dir, "01_intro");
    frames(1, PRG32_BTN_A);
    frames(20, 0);
    shot(dir, "02_start");
    place(200, 1100, 90, 1);   shot(dir, "03_tunnel_torch");
    place(200, 1100, 90, 0);   shot(dir, "04_tunnel_dark");
    place(600, 1800, 0, 1);    shot(dir, "05_cistern_corridor");
    place(1000, 1700, 60, 1);  shot(dir, "06_cistern_well");
    world_set_flags(WF_GATE_A_OPEN);
    place(200, 3700, 90, 1);   shot(dir, "07_gate_open");
    place(200, 4300, 100, 1);  shot(dir, "08_vehicle_hall");
    /* Store screenshot: tuff, torch beam, a vehicle silhouette, minimal HUD. */
    place(380, 4250, 112, 1);
    g_game.toast_ticks = 0;
    frames(1, 0);
    g_game.toast_ticks = 0;
    g_game.target.kind = TGT_NONE;
    galleria2007_draw();
    shot(dir, "store_screenshot");
    place(600, 6200, 160, 1);  shot(dir, "09_shelter");
    world_set_flags(WF_WALL_OPEN | WF_SHELTER_CLUES_COMPLETE | WF_WALL_IDENTIFIED);
    place(500, 6500, 180, 1);  shot(dir, "10_wall_open");
    place(-500, 6500, 180, 1); shot(dir, "11_cavity");
    ai_place(&g_game.lamp, world_entity_of_kind(ENT_LAMPMAN, 0), AI_PATROL);
    g_game.lamp_started = 1;
    place(-600, 6600, 180, 0); shot(dir, "12_lampman_dark");
    place(-900, 7000, 90, 1);  shot(dir, "13_stairs");
    g_game.state = GS_INVENTORY; g_game.inv_page = 0; g_game.inv_return = GS_PLAY;
    frames(1, 0);              shot(dir, "14_backpack");
    g_game.archive = 0x7F; g_game.inv_page = 1; frames(1, 0); shot(dir, "15_archive");
    open_archive_entry(&g_game, ARC_DISCOVERY, GS_PLAY); frames(1, 0); shot(dir, "16_read");
    g_game.state = GS_END; g_game.state_ticks = 200; frames(1, 0); shot(dir, "17_end");
    printf("shots written to %s (%d blits)\n", dir, host_blits);
    return 0;
}
