/*
 * User interface drawn over the 3D view with PRG32 text and rectangles.
 * The first-person view stays dominant: the play HUD is a tiny objective,
 * a torch icon and short-lived prompts. Colours are PRG32 system-palette
 * indices so they are identical on ILI9341 hardware and in QEMU.
 */
#include "game.h"
#include "renderer.h"
#include "platform.h"

#define C_BLACK 16
#define C_WHITE 1
#define C_PANEL 16
#define C_BORDER 178      /* (4,3,0) amber   */
#define C_ACCENT 220      /* (5,4,0) gold    */
#define C_DIM 145         /* (3,3,3) grey    */
#define C_HIST 117        /* (2,4,5) blue    */
#define C_FICTION 209     /* (5,2,1) orange  */
#define C_DANGER 52       /* (1,0,0) dark red */

static void text(int x, int y, const char *s, uint8_t fg, uint8_t bg) {
    prg32_gfx_text8(x, y, s, g_pal565[fg], g_pal565[bg]);
}

static int text_len(const char *s) {
    int n = 0;
    while (s[n] && s[n] != '\n') ++n;
    return n;
}

static void text_center(int y, const char *s, uint8_t fg, uint8_t bg) {
    text((G2007_SCREEN_W - 8 * text_len(s)) / 2, y, s, fg, bg);
}

/* Multi-line text; returns the y below the last line. */
static int text_lines(int x, int y, const char *s, uint8_t fg, uint8_t bg, int center) {
    char line[41];
    while (*s) {
        int n = 0;
        while (s[n] && s[n] != '\n' && n < 40) {
            line[n] = s[n];
            ++n;
        }
        line[n] = 0;
        if (n) {
            if (center) text_center(y, line, fg, bg);
            else text(x, y, line, fg, bg);
        }
        s += n;
        if (*s == '\n') ++s;
        y += 10;
    }
    return y;
}

static void panel(int x, int y, int w, int h) {
    prg32_gfx_rect_indexed(x, y, w, h, C_BORDER);
    prg32_gfx_rect_indexed(x + 2, y + 2, w - 4, h - 4, C_PANEL);
}

static void fill(uint8_t c) { prg32_gfx_rect_indexed(0, 0, G2007_SCREEN_W, G2007_SCREEN_H, c); }

/* Append an unsigned decimal number. */
static char *put_num(char *p, uint32_t v, int min_digits) {
    char tmp[10];
    int n = 0;
    do { tmp[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < 10);
    while (n < min_digits) tmp[n++] = '0';
    while (n) *p++ = tmp[--n];
    *p = 0;
    return p;
}

static char *put_str(char *p, const char *s) {
    while (*s) *p++ = *s++;
    *p = 0;
    return p;
}

static uint8_t objective(const Game *g) {
    uint32_t f = g_world.flags;
    uint8_t zone = g_map_sectors[g->player.body.sector].zone;
    if (inv_count(&g->player.pack) == 0 && !g->finds) return S_O_START;
    if (!(f & WF_GATE_A_OPEN)) return (g->zones_seen & (1u << ZONE_CISTERN)) ? S_O_GATE : S_O_TUNNEL;
    if (!(f & WF_SHELTER_CLUES_COMPLETE)) return S_O_SHELTER;
    if (!(f & WF_WALL_IDENTIFIED)) return S_O_WALL;
    if (!(f & WF_WALL_OPEN)) return S_O_FORCE;
    return zone == ZONE_STAIRS ? S_O_STAIRS : S_O_CAVITY;
}

static void torch_icon(const Game *g) {
    int x = 296, y = 4;
    prg32_gfx_rect_indexed(x, y + 2, 10, 6, C_DIM);
    prg32_gfx_rect_indexed(x + 10, y, 3, 10, C_DIM);
    if (g->player.light) {
        uint8_t c = g->player.boost_ticks ? C_WHITE : C_ACCENT;
        prg32_gfx_rect_indexed(x + 13, y + 1, 2, 8, c);
        prg32_gfx_rect_indexed(x + 15, y - 1, 3, 12, 221);
    }
}

static void draw_play_hud(Game *g) {
    text(4, 3, str(objective(g)), C_DIM, C_BLACK);
    torch_icon(g);
    if (g->remote_count) {
        char buf[24];
        char *p = put_str(buf, str(S_MP_PLAYERS));
        *p++ = ' ';
        put_num(p, (uint32_t)g->remote_count + 1, 1);
        text(G2007_SCREEN_W - 8 * text_len(buf) - 4, 16, buf, C_HIST, C_BLACK);
    }
    if (g->wall_progress) {
        int w = (int)g->wall_progress * 120 / (4000 / G2007_TICK_MS);
        if (w > 120) w = 120;
        prg32_gfx_rect_indexed(98, 158, 124, 8, C_BORDER);
        prg32_gfx_rect_indexed(100, 160, 120, 4, C_BLACK);
        prg32_gfx_rect_indexed(100, 160, w, 4, C_ACCENT);
    }
    if (g->toast_ticks) {
        char buf[80];
        char *p = put_str(buf, str(g->toast_a));
        if (g->toast_b != S_NONE) {
            *p++ = ' ';
            put_str(p, str(g->toast_b));
        }
        text_center(172, buf, C_WHITE, C_BLACK);
    }
    if (g->target.kind != TGT_NONE) text_center(188, str(g->target.prompt), C_ACCENT, C_BLACK);
}

static void draw_inventory(Game *g) {
    panel(48, 24, 224, 152);
    if (!g->archive_only) text_center(32, str(S_INV_TABS), C_BORDER, C_PANEL);
    else text_center(32, str(S_MENU_ARCHIVE), C_BORDER, C_PANEL);
    if (g->inv_page == 0) {
        text_center(48, str(S_INV_TITLE), C_ACCENT, C_PANEL);
        int count = inv_count(&g->player.pack);
        for (int i = 0; i < BACKPACK_SLOTS; ++i) {
            int y = 66 + i * 14;
            uint8_t item = g->player.pack.slot[i];
            if (item == INV_EMPTY) {
                if (count) text(84, y, "-", C_DIM, C_PANEL);
                continue;
            }
            text(68, y, i == g->inv_sel ? ">" : " ", C_ACCENT, C_PANEL);
            text(84, y, str(S_N_NOTEBOOK + g_world.items[item].type - ITEM_NOTEBOOK),
                 i == g->inv_sel ? C_WHITE : C_DIM, C_PANEL);
        }
        if (!count) text_center(66, str(S_INV_EMPTY), C_DIM, C_PANEL);
        text_center(152, str(g->inv_confirm ? S_INV_CONFIRM : S_INV_HELP), C_BORDER, C_PANEL);
    } else {
        for (int a = 0; a < ARC_COUNT; ++a) {
            int y = 50 + a * 13;
            int open = (g->archive >> a) & 1;
            uint8_t c = !open ? C_DIM : (a == ARC_FICTION ? C_FICTION : C_HIST);
            text(60, y, a == g->arc_sel ? ">" : " ", C_ACCENT, C_PANEL);
            text(76, y, open ? str(S_A0_TITLE + 2 * a) : str(S_L_LOCKED), c, C_PANEL);
        }
        text_center(152, str(S_ARC_HELP), C_BORDER, C_PANEL);
    }
}

static void draw_swap(Game *g) {
    panel(48, 24, 224, 152);
    text_center(32, str(S_BACKPACK_FULL), C_FICTION, C_PANEL);
    char buf[48];
    char *p = put_str(buf, str(S_SWAP_NEW));
    *p++ = ' ';
    put_str(p, str(S_N_NOTEBOOK + g_world.items[g->swap_target].type - ITEM_NOTEBOOK));
    text_center(46, buf, C_ACCENT, C_PANEL);
    text_center(60, str(S_SWAP_HELP), C_DIM, C_PANEL);
    for (int i = 0; i < BACKPACK_SLOTS; ++i) {
        uint8_t item = g->player.pack.slot[i];
        if (item == INV_EMPTY) continue;
        int y = 76 + i * 13;
        text(68, y, i == g->inv_sel ? ">" : " ", C_ACCENT, C_PANEL);
        text(84, y, str(S_N_NOTEBOOK + g_world.items[item].type - ITEM_NOTEBOOK),
             i == g->inv_sel ? C_WHITE : C_DIM, C_PANEL);
    }
    text_center(152, str(S_SWAP_KEYS), C_BORDER, C_PANEL);
}

static void draw_read(Game *g) {
    panel(4, 30, 312, 140);
    text_center(38, str(g->read_title), C_ACCENT, C_PANEL);
    int y = 52;
    if (g->read_label != S_NONE) {
        text_center(y, str(g->read_label), g->read_label == S_L_FICTION ? C_FICTION : C_HIST, C_PANEL);
        y += 14;
    }
    text_lines(12, y, str(g->read_body), C_WHITE, C_PANEL, 0);
    text_center(156, str(S_READ_HELP), C_BORDER, C_PANEL);
}

static void draw_stats(Game *g) {
    int y = 92;
    static const uint8_t labels[6] = {S_ST_PLACES, S_ST_FINDS, S_ST_ARCHIVE, S_ST_SECRETS,
                                      S_ST_SHARED, S_ST_TIME};
    uint32_t hist = 0;
    for (int a = 0; a < ARC_FICTION; ++a) hist += (g->archive >> a) & 1u;
    uint32_t zones = 0;
    for (int z = 0; z < ZONE_COUNT; ++z) zones += (g->zones_seen >> z) & 1u;
    uint32_t secs = g->play_ticks * G2007_TICK_MS / 1000;
    for (int i = 0; i < 6; ++i, y += 12) {
        char buf[16];
        char *p = buf;
        switch (i) {
        case 0: p = put_num(p, zones, 1); p = put_str(p, "/"); put_num(p, ZONE_COUNT, 1); break;
        case 1: put_num(p, g->finds, 1); break;
        case 2: p = put_num(p, hist, 1); p = put_str(p, "/"); put_num(p, ARC_FICTION, 1); break;
        case 3: put_num(p, g->secrets, 1); break;
        case 4: put_num(p, g->shared, 1); break;
        default: p = put_num(p, secs / 60, 2); p = put_str(p, ":"); put_num(p, secs % 60, 2); break;
        }
        text(40, y, str(labels[i]), C_DIM, C_BLACK);
        text(232, y, buf, C_WHITE, C_BLACK);
    }
}

void ui_draw(Game *g) {
    switch (g->state) {
    case GS_TITLE:
        panel(32, 40, 256, 120);
        text_center(52, str(S_TITLE), C_ACCENT, C_PANEL);
        text_center(68, str(S_SUBTITLE), C_WHITE, C_PANEL);
        text_center(82, str(S_TITLE_NOTE), C_DIM, C_PANEL);
        text_center(106, str(S_MENU_PLAY), g->menu == 0 ? C_ACCENT : C_DIM, C_PANEL);
        text_center(120, str(S_MENU_ARCHIVE), g->menu == 1 ? C_ACCENT : C_DIM, C_PANEL);
        text(100, g->menu == 0 ? 106 : 120, ">", C_ACCENT, C_PANEL);
        text_center(142, str(S_TITLE_HELP), C_BORDER, C_PANEL);
        text(296, 188, str(S_LANG), C_DIM, C_BLACK);
        break;
    case GS_INTRO:
        fill(C_BLACK);
        text_center(70, str(S_INTRO_PLACE), C_ACCENT, C_BLACK);
        text_lines(0, 96, str(S_INTRO_LINE), C_WHITE, C_BLACK, 1);
        break;
    case GS_PLAY: draw_play_hud(g); break;
    case GS_INVENTORY:
        if (g->archive_only) fill(C_BLACK);
        draw_inventory(g);
        break;
    case GS_SWAP: draw_swap(g); break;
    case GS_READ:
        if (g->read_return != GS_PLAY) fill(C_BLACK);
        draw_read(g);
        break;
    case GS_CAUGHT:
        prg32_gfx_rect_indexed(0, 70, G2007_SCREEN_W, 60, C_DANGER);
        text_lines(0, 82, str(S_C_TEXT), C_WHITE, C_DANGER, 1);
        text_center(110, str(S_C_BACK), C_ACCENT, C_DANGER);
        break;
    case GS_END:
        fill(C_BLACK);
        text_center(24, str(S_TITLE), C_ACCENT, C_BLACK);
        text_center(42, str(S_SUBTITLE), C_WHITE, C_BLACK);
        text_center(64, str(S_E_REACHED), C_HIST, C_BLACK);
        draw_stats(g);
        if (g->state_ticks > 2500 / G2007_TICK_MS) text_center(184, str(S_PRESS_A), C_BORDER, C_BLACK);
        break;
    case GS_OUTRO: {
        fill(C_BLACK);
        text_center(40, str(S_TITLE), C_ACCENT, C_BLACK);
        static const uint8_t items[3] = {S_OM_ARCHIVE, S_OM_REPLAY, S_OM_GALLERIA};
        for (int i = 0; i < 3; ++i)
            text_center(80 + i * 18, str(items[i]), g->menu == i ? C_ACCENT : C_DIM, C_BLACK);
        text(60, 80 + g->menu * 18, ">", C_ACCENT, C_BLACK);
        break;
    }
    case GS_INFO:
        fill(C_BLACK);
        text_center(24, str(S_OM_GALLERIA), C_ACCENT, C_BLACK);
        text_lines(16, 50, str(S_INFO_BODY), C_WHITE, C_BLACK, 0);
        text_center(184, str(S_READ_HELP), C_BORDER, C_BLACK);
        break;
    default:
        break;
    }
}
