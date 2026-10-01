/*
 * Portal renderer implementation (see renderer.h and docs/renderer.md).
 *
 * Number formats used below:
 *   d        perpendicular distance to a wall hit (cm)
 *   scale    pixels per cm at distance d, Q8: (FOCAL << 8) / d
 *   R        column ray direction, Q12, with the forward component = 1.0
 *   v        vertical texture coordinate, Q16 texels (1 texel = 4 cm)
 *   L        brightness 0..16 (16 = full texture brightness)
 *
 * Performance notes (docs/performance.md): the inner loops avoid multiplies
 * and divisions. Shading is one lookup in a pre-shaded table (g_shade); light comes from per-frame ambient/torch tables; tall wall spans
 * pre-shade their 32-texel texture column once; floors and ceilings shade
 * rows in pairs; the wall distance uses one hardware divide.
 */
#include "renderer.h"
#include "fixed.h"
#include "world.h"
#include "platform.h"
#include "gen/assets_data.h"
#include "gen/font8.h"

#define H G2007_HORIZON
#define W G2007_SCREEN_W
#define SH G2007_SCREEN_H
#define VY0 G2007_VIEW_Y0
#define VY1 (G2007_VIEW_Y0 + G2007_VIEW_H - 1)
#define BLACK 16              /* system palette cube entry (0,0,0) */
#define BANDS 8               /* distinct torch-beam strengths     */

uint16_t g_zbuf[W];
uint16_t g_pal565[256];

/* The strip is also addressable as 32-bit words (a union, so the access is
 * well defined) for the half-resolution pass, which duplicates columns four
 * pixels at a time. */
static union {
    uint8_t px[SH][G2007_STRIP_W];
    uint32_t words[SH * G2007_STRIP_W / 4];
} g_strip_u;
#define g_strip g_strip_u.px
/* One strip is blitted as up to three sprites sharing the buffer: the 3D
 * view every frame, the HUD bands only when their content changed. */
static prg32_indexed_sprite_t g_desc_view, g_desc_top, g_desc_bottom, g_desc_full;
static Camera g_cam;
static int32_t g_dx, g_dy;    /* forward unit vector, Q12      */
static int32_t g_rgx, g_rgy;  /* right unit vector, Q12        */
static int32_t g_plx, g_ply;  /* camera plane (right * W/2/FOCAL), Q12 */

/* 2x2 ordered dither added before the >> 4 of the shading product. */
static const uint8_t k_bayer[4] = {0, 8, 12, 4};
/* Torch beam strengths: index 0 = no torch, 1..4 = edge to centre,
 * 5..7 = the boosted (battery) versions of 2..4. */
static const uint8_t k_band[BANDS] = {0, 1, 4, 9, 13, 7, 12, 16};

/* g_shade[material][L / 2][phase][lum]: the final palette index of a texel
 * of luminance lum, at brightness L, for each dither phase:
 * ramp[(lum * L + bayer[phase]) >> 4]. Built once at init (4.6 KB), it turns
 * shading into a single table lookup per pixel. Brightness is stored in 9
 * steps (the 6x6x6 colour cube is coarser than that; the ordered dither
 * supplies the in-between tones). */
#define SHADE_STEPS 9
#define SHADE_OF(l) (((l) + 1) >> 1)
static uint8_t g_shade[MAT_COUNT][SHADE_STEPS][4][16];
/* Light tables indexed by distance bin (64 cm): ambient light * fog and
 * torch strength * fall-off (saturated to 8 bits).
 * L = min(16, (amb + torch) >> 4). */
static uint8_t g_amb[17][64];
static uint8_t g_torch[BANDS][64];

typedef struct {
    int32_t tz;
    int16_t x0, x1, y0, y1;
    int32_t ustep, vstep;
    uint8_t sprite, kind;
    int16_t cx, cy, radius;
    uint8_t shade[16];
} VisSprite;

static VisSprite g_vis[MAX_VISIBLE_SPRITES];
static int g_vis_count;

enum { OP_RECT = 0, OP_TEXT = 1 };
typedef struct {
    uint8_t kind, fg, bg, len;
    int16_t x, y, w, h;
    uint16_t text;        /* offset into g_ui_pool */
} UiOp;

static UiOp g_ui[UI_MAX_OPS];
static int g_ui_count, g_ui_pool_used;
static char g_ui_pool[UI_TEXT_POOL];
static int16_t g_ui_fill = -1;

/* What was last sent to the display, to skip identical work. */
static uint32_t g_last_frame_hash, g_last_band_hash;
static uint8_t g_last_xstep, g_last_was_fill, g_have_frame;
static Camera g_prev_cam;

void render_ui_reset(void) {
    g_ui_count = 0;
    g_ui_pool_used = 0;
    g_ui_fill = -1;
}

void render_ui_fill(uint8_t color) { g_ui_fill = color; }

void render_ui_rect(int x, int y, int w, int h, uint8_t color) {
    if (g_ui_count >= UI_MAX_OPS || w <= 0 || h <= 0) return;
    UiOp *op = &g_ui[g_ui_count++];
    op->kind = OP_RECT;
    op->fg = color;
    op->bg = 0;
    op->len = 0;
    op->text = 0;
    op->x = (int16_t)x;
    op->y = (int16_t)y;
    op->w = (int16_t)w;
    op->h = (int16_t)h;
}

void render_ui_text(int x, int y, const char *s, int len, uint8_t fg, uint8_t bg) {
    int n = 0;
    while (n < len && s[n] && s[n] != '\n') ++n;
    if (!n || g_ui_count >= UI_MAX_OPS || g_ui_pool_used + n > UI_TEXT_POOL) return;
    UiOp *op = &g_ui[g_ui_count++];
    op->kind = OP_TEXT;
    op->fg = fg;
    op->bg = bg;
    op->x = (int16_t)x;
    op->y = (int16_t)y;
    op->w = 0;
    op->h = 0;
    op->len = (uint8_t)(n > 255 ? 255 : n);
    op->text = (uint16_t)g_ui_pool_used;
    for (int i = 0; i < op->len; ++i) g_ui_pool[g_ui_pool_used++] = s[i];
}

/* Compose the overlay ops that intersect rows [ymin, ymax] of one strip. */
static void draw_ui_strip(int strip_x, int ymin, int ymax) {
    int sx1 = strip_x + G2007_STRIP_W - 1;
    for (int i = 0; i < g_ui_count; ++i) {
        const UiOp *op = &g_ui[i];
        if (op->kind == OP_RECT) {
            int xa = fx_max(op->x, strip_x), xb = fx_min(op->x + op->w - 1, sx1);
            int ya = fx_max(op->y, ymin), yb = fx_min(op->y + op->h - 1, ymax);
            for (int y = ya; y <= yb; ++y)
                for (int x = xa; x <= xb; ++x) g_strip[y][x - strip_x] = op->fg;
            continue;
        }
        int x0 = op->x, x1 = op->x + 8 * op->len - 1;
        if (x1 < strip_x || x0 > sx1 || op->y > ymax || op->y + 7 < ymin) continue;
        int xa = fx_max(x0, strip_x), xb = fx_min(x1, sx1);
        for (int x = xa; x <= xb; ++x) {
            int cx = x - x0;
            unsigned ch = (unsigned char)g_ui_pool[op->text + (cx >> 3)];
            if (ch < 32 || ch > 126) ch = '?';
            uint8_t bit = (uint8_t)(0x80u >> (cx & 7));
            for (int r = 0; r < 8; ++r) {
                int y = op->y + r;
                if (y < ymin || y > ymax) continue;
                if (g_ui_font[ch - 32][r] & bit) g_strip[y][x - strip_x] = op->fg;
                else if (op->bg != UI_TRANSPARENT) g_strip[y][x - strip_x] = op->bg;
            }
        }
    }
}

static uint32_t hash_bytes(uint32_t h, const void *data, int n) {
    const uint8_t *p = (const uint8_t *)data;
    while (n--) h = (h ^ *p++) * 16777619u;      /* FNV-1a */
    return h;
}

/* Hash of the overlay ops touching rows [ymin, ymax] (all ops when the
 * range covers the screen). */
static uint32_t hash_ops(int ymin, int ymax) {
    uint32_t h = 2166136261u;
    for (int i = 0; i < g_ui_count; ++i) {
        const UiOp *op = &g_ui[i];
        int y1 = op->kind == OP_RECT ? op->y + op->h - 1 : op->y + 7;
        if (y1 < ymin || op->y > ymax) continue;
        h = hash_bytes(h, &op->kind, 4);
        h = hash_bytes(h, &op->x, 8);
        if (op->kind == OP_TEXT) h = hash_bytes(h, g_ui_pool + op->text, op->len);
    }
    return h;
}

static void init_desc(prg32_indexed_sprite_t *d, int y0, int rows) {
    d->pixels = &g_strip[y0][0];
    d->palette = g_pal565;
    d->width = G2007_STRIP_W;
    d->height = (uint16_t)rows;
    d->frame_count = 1;
    d->palette_count = 256;
    d->bits_per_pixel = PRG32_SPRITE_BPP_8;
    d->transparent_index = -1;
}

void render_init(void) {
    /* System palette as the ILI9341 driver understands it: it quantises an
     * RGB565 colour with component * 5 / max, so each cube level uses the
     * smallest component value that maps back to it. Hardware then shows
     * exactly the intended cube colour; QEMU (RGB565) shows the same. */
    static const uint8_t lv5[6] = {0, 7, 13, 19, 25, 31};
    static const uint8_t lv6[6] = {0, 13, 26, 38, 51, 63};
    static const uint16_t named[8] = {0x0000, 0xffff, 0xf800, 0x07e0,
                                      0x001f, 0xffe0, 0x07ff, 0xf81f};
    for (int i = 0; i < 256; ++i) {
        uint16_t c = 0;
        if (i < 8) {
            c = named[i];
        } else if (i >= 16 && i < 232) {
            int v = i - 16;
            c = (uint16_t)((lv5[v / 36] << 11) | (lv6[(v / 6) % 6] << 5) | lv5[v % 6]);
        }
        g_pal565[i] = c;
    }
    /* Install the same colours in the host's indexed palette. Hosts differ
     * in their *default* palette (the firmware uses a 6x6x6 cube, PRG32-QT an
     * RGB332 ramp), so the cartridge never relies on it: after this, strips
     * blitted with g_pal565 resolve to exactly these entries on the
     * ESP32-C6, QEMU, PRG32-QT and PRG32-iOS alike. Entries 8..15 and
     * 232..255 are left untouched (unused). */
    for (int i = 0; i < 232; ++i) {
        if (i < 8 || i >= 16) prg32_palette_set((uint8_t)i, g_pal565[i]);
    }
    init_desc(&g_desc_view, VY0, G2007_VIEW_H);
    init_desc(&g_desc_top, 0, VY0);
    init_desc(&g_desc_bottom, VY1 + 1, SH - VY1 - 1);
    init_desc(&g_desc_full, 0, SH);
    for (int step = 0; step < SHADE_STEPS; ++step) {
        for (int m = 0; m < MAT_COUNT; ++m)
            for (int ph = 0; ph < 4; ++ph)
                for (int lum = 0; lum < 16; ++lum)
                    g_shade[m][step][ph][lum] = g_ramps[m][(lum * step * 2 + k_bayer[ph]) >> 4];
    }
    for (int l = 0; l <= 16; ++l)
        for (int i = 0; i < 64; ++i) g_amb[l][i] = (uint8_t)fx_min(255, l * g_fog[i]);
    for (int b = 0; b < BANDS; ++b)
        for (int i = 0; i < 64; ++i) g_torch[b][i] = (uint8_t)fx_min(255, k_band[b] * g_flash[i]);
    g_have_frame = 0;
}

/* Brightness from ambient light, torch band and distance. */
static inline int light_at(int base, int band, int32_t d) {
    int i = d >> GEN_FOG_STEP_SHIFT;
    if (i > 63) i = 63;
    int l = (g_amb[base][i] + g_torch[band][i]) >> 4;
    return l > 16 ? 16 : l;
}

/* Torch beam band index (see k_band) for a screen column. */
static int beam_band(int x) {
    if (!g_cam.beam) return 0;
    int dx = x - W / 2;
    if (dx < 0) dx = -dx;
    int boost = g_cam.beam > 1 ? 3 : 0;
    if (dx < 34) return 4 + boost;
    if (dx < 68) return 3 + boost;
    if (dx < 110) return 2 + boost;
    return 1;
}

static inline uint8_t texel(const uint8_t *pattern, int u, int v) {
    uint8_t b = pattern[(v << 4) | (u >> 1)];
    return (u & 1) ? (uint8_t)(b & 15u) : (uint8_t)(b >> 4);
}

static void fill_black(int col, int y0, int y1) {
    for (int y = y0; y <= y1; ++y) g_strip[y][col] = BLACK;
}

/*
 * One textured wall span. The texture column u, the material and the
 * brightness are constant along the span, so each pixel is one texel fetch
 * and one lookup in the pre-shaded table of its row's dither phase. When the
 * wall is magnified (a texel covers two rows or more) rows are drawn in
 * pairs sharing one texel fetch.
 */
static void draw_wall(int col, int x, int y0, int y1, int32_t d, int tex, int u, int light) {
    if (y0 > y1) return;
    const uint8_t *tcol = g_patterns[g_tex_pattern[tex]] + (u >> 1);
    int shift = (u & 1) ? 0 : 4;
    /* Dither phase = ((y & 1) << 1) | (x & 1): two phases per column. */
    const uint8_t *shade_even = g_shade[g_tex_material[tex]][SHADE_OF(light)][x & 1];
    const uint8_t *shade_odd = shade_even + 32;
    int32_t vstep = (d << 14) / G2007_FOCAL;
    int32_t v = -g_cam.z * 16384 + (y0 - H) * vstep;
    uint8_t *p = &g_strip[y0][col];
    int y = y0;
    if (y & 1) {                                   /* align to an even row */
        *p = shade_odd[(tcol[(v >> 12) & 496] >> shift) & 15];
        p += G2007_STRIP_W;
        v += vstep;
        ++y;
    }
    if (vstep <= 0x8000) {
        /* Magnified: one texel fetch per pair of rows. */
        int32_t vstep2 = vstep * 2;
        for (; y < y1; y += 2) {
            int lum = (tcol[(v >> 12) & 496] >> shift) & 15;
            p[0] = shade_even[lum];
            p[G2007_STRIP_W] = shade_odd[lum];
            v += vstep2;
            p += 2 * G2007_STRIP_W;
        }
    } else {
        for (; y < y1; y += 2) {
            p[0] = shade_even[(tcol[(v >> 12) & 496] >> shift) & 15];
            v += vstep;
            p[G2007_STRIP_W] = shade_odd[(tcol[(v >> 12) & 496] >> shift) & 15];
            v += vstep;
            p += 2 * G2007_STRIP_W;
        }
    }
    if (y == y1) *p = shade_even[(tcol[(v >> 12) & 496] >> shift) & 15];
}

/* Floor or ceiling rows. The distance of a flat pixel depends only on its
 * row and the height difference: d = hdiff * FOCAL / |y - H| (table). Rows
 * are shaded in pairs (G2007_FLAT_Y_STEP): distance, light and texel are
 * computed once and written with each row's own dither phase. Beyond
 * G2007_FLAT_TEX_FAR a texel is smaller than a pixel, so far rows use the
 * pattern's mean luminance instead of sampling it. */
static void draw_flat(int col, int x, int y0, int y1, int32_t hdiff, int tex,
                      int base, int band, int32_t rx, int32_t ry) {
    if (y0 > y1) return;
    if (hdiff <= 0) { fill_black(col, y0, y1); return; }
    const uint8_t (*shade)[4][16] = g_shade[g_tex_material[tex]];
    const uint8_t *amb = g_amb[base], *torch = g_torch[band];
#if G2007_TEXTURED_FLATS
    const uint8_t *pattern = g_patterns[g_tex_pattern[tex]];
#else
    (void)rx; (void)ry;
#endif
    uint8_t *p = &g_strip[y0][col];
    int y = y0;
    while (y <= y1) {
        int rows = (G2007_FLAT_Y_STEP > 1 && y < y1) ? 2 : 1;
        int k = y - H;
        if (k < 0) k = -k;
        int32_t dist = k ? (hdiff * g_recip_row[k]) >> 8 : G2007_FAR_CM;
        if (dist >= G2007_FAR_CM) {
            p[0] = BLACK;
            if (rows == 2) p[G2007_STRIP_W] = BLACK;
        } else {
            int i = dist >> GEN_FOG_STEP_SHIFT;
            int light = (amb[i] + torch[i]) >> 4;
            if (light > 16) light = 16;
            int lum = 9;
#if G2007_TEXTURED_FLATS
            if (dist < G2007_FLAT_TEX_FAR) {
                int32_t wx = g_cam.x + ((rx * dist) >> FX_SHIFT);
                int32_t wy = g_cam.y + ((ry * dist) >> FX_SHIFT);
                lum = texel(pattern, (wx >> 2) & 31, (wy >> 2) & 31);
            }
#endif
            const uint8_t *sh = shade[SHADE_OF(light)][x & 1];   /* even-row phase */
            if (y & 1) {
                p[0] = sh[32 + lum];
                if (rows == 2) p[G2007_STRIP_W] = sh[lum];
            } else {
                p[0] = sh[lum];
                if (rows == 2) p[G2007_STRIP_W] = sh[32 + lum];
            }
        }
        y += rows;
        p += rows * G2007_STRIP_W;
    }
}

static inline int32_t project_y(int32_t z, int32_t scale) {
    return H - (((z - g_cam.z) * scale) >> 8);
}

/* d = num * 4096 / denom for 0 < num, denom, clamped to the view range.
 * Instead of a 64-bit division, num is shifted left as far as it fits and
 * denom is shifted right by the remaining bits: one hardware divide. The
 * walls in view have denom >> num, so the dropped bits are far below a
 * centimetre. */
static inline int32_t ray_distance(int32_t num, int32_t denom) {
    if (num <= 0) return G2007_NEAR_CM;
    if (num >= denom) return G2007_FAR_CM;          /* d >= 4096 cm */
    int s = 0;
    while (s < FX_SHIFT && num < 0x20000000) { num <<= 1; ++s; }
    denom >>= FX_SHIFT - s;
    if (denom == 0) return G2007_FAR_CM;
    int32_t d = num / denom;
    return d < G2007_NEAR_CM ? G2007_NEAR_CM : (d > G2007_FAR_CM ? G2007_FAR_CM : d);
}

/* Exit wall found by the previous column at each portal depth. Adjacent
 * rays almost always leave a sector through the same wall, and the exit
 * wall of a convex sector is unique, so testing the remembered wall first
 * (two cross products) usually replaces the scan of every wall. */
static struct {
    int16_t sector, wall;
} g_exit_cache[G2007_MAX_PORTALS];

static int exit_wall_cached(int depth, int s, int skip, int32_t rx, int32_t ry) {
    if (g_exit_cache[depth].sector == s) {
        int w = g_exit_cache[depth].wall;
        if (w >= 0 && w != skip) {
            const MapVertex *a = &g_map_vertices[g_map_walls[w].v0];
            const MapVertex *b = &g_map_vertices[g_map_walls[w].v1];
            if (rx * (a->y - g_cam.y) - ry * (a->x - g_cam.x) < 0 &&
                rx * (b->y - g_cam.y) - ry * (b->x - g_cam.x) >= 0) return w;
        }
    }
    int w = world_exit_wall(s, skip, g_cam.x, g_cam.y, rx, ry);
    g_exit_cache[depth].sector = (int16_t)s;
    g_exit_cache[depth].wall = (int16_t)w;
    return w;
}

static void render_column(int x, int col) {
    int32_t camx = ((2 * x + 1 - W) * FX_ONE) / W;
    int32_t rx = g_dx + ((g_plx * camx) >> FX_SHIFT);
    int32_t ry = g_dy + ((g_ply * camx) >> FX_SHIFT);
    int band = beam_band(x);
    int ytop = VY0, ybot = VY1, s = g_cam.sector, skip = -1;
    g_zbuf[x] = G2007_FAR_CM;
    for (int depth = 0; depth < G2007_MAX_PORTALS && s >= 0; ++depth) {
        const MapSector *sec = &g_map_sectors[s];
        int w = exit_wall_cached(depth, s, skip, rx, ry);
        if (w < 0) break;
        const MapWall *wall = &g_map_walls[w];
        const MapVertex *a = &g_map_vertices[wall->v0];
        const MapVertex *b = &g_map_vertices[wall->v1];
        int32_t ex = b->x - a->x, ey = b->y - a->y;
        int32_t denom = rx * ey - ry * ex;                       /* cross(R, E) */
        int32_t num = (a->x - g_cam.x) * ey - (a->y - g_cam.y) * ex; /* cross(D, E) */
        int32_t d = denom > 0 ? ray_distance(num, denom) : G2007_FAR_CM;
        int32_t hx = g_cam.x + ((rx * d) >> FX_SHIFT), hy = g_cam.y + ((ry * d) >> FX_SHIFT);
        int32_t ucm = fx_abs(fx_abs(ex) >= fx_abs(ey) ? hx - a->x : hy - a->y);
        int u = (int)(((ucm * wall->ulen_q8) >> 10) & 31);       /* >>8 then 4 cm/texel */
        int32_t scale = (G2007_FOCAL << 8) / d;
        int base = sec->light;
        int light = light_at(base, band, d);
        int32_t yc = project_y(sec->ceil_z, scale), yf = project_y(sec->floor_z, scale);
        draw_flat(col, x, ytop, fx_min(yc - 1, ybot), sec->ceil_z - g_cam.z,
                  sec->ceil_tex, base, band, rx, ry);
        draw_flat(col, x, fx_max(yf + 1, ytop), ybot, g_cam.z - sec->floor_z,
                  sec->floor_tex, base, band, rx, ry);
        int open = wall->neighbor >= 0 &&
                   (wall->door == DOOR_NONE || world_door_open(wall->door));
        if (!open) {
            draw_wall(col, x, fx_max(yc, ytop), fx_min(yf, ybot), d, wall->tex, u, light);
            g_zbuf[x] = (uint16_t)d;
            return;
        }
        const MapSector *ns = &g_map_sectors[wall->neighbor];
        int32_t ync = project_y(ns->ceil_z, scale), ynf = project_y(ns->floor_z, scale);
        if (ns->ceil_z < sec->ceil_z)
            draw_wall(col, x, fx_max(yc, ytop), fx_min(ync - 1, ybot), d, wall->tex, u, light);
        if (ns->floor_z > sec->floor_z)
            draw_wall(col, x, fx_max(ynf + 1, ytop), fx_min(yf, ybot), d, wall->tex, u, light);
        ytop = fx_max(ytop, fx_max(yc, ync));
        ybot = fx_min(ybot, fx_min(yf, ynf));
        if (ytop > ybot) {
            g_zbuf[x] = (uint16_t)d;
            return;
        }
        skip = wall->mate;
        s = wall->neighbor;
    }
    fill_black(col, ytop, ybot);
}

int render_project(const Camera *cam, int32_t x, int32_t y, int32_t *depth,
                   int32_t *lateral, int *screen_x) {
    int32_t rx = x - cam->x, ry = y - cam->y;
    int32_t c = fx_cos(cam->angle), s = fx_sin(cam->angle);
    int32_t tz = (rx * c + ry * s) >> FX_SHIFT;
    int32_t tx = (rx * s - ry * c) >> FX_SHIFT;   /* right = (sin, -cos) */
    *depth = tz;
    *lateral = tx;
    if (tz < G2007_NEAR_CM) return 0;
    *screen_x = W / 2 + (int)((tx * G2007_FOCAL) / tz);
    return 1;
}

/* Darken a system-cube colour component-wise (16 = unchanged). */
static uint8_t shade_index(uint8_t idx, int light) {
    if (idx < 16) return idx;
    int v = idx - 16, r = v / 36, g = (v / 6) % 6, b = v % 6;
    r = (r * light + 8) >> 4;
    g = (g * light + 8) >> 4;
    b = (b * light + 8) >> 4;
    return (uint8_t)(16 + r * 36 + g * 6 + b);
}

static void prepare_sprites(const SpriteRef *refs, int count) {
    g_vis_count = 0;
    for (int i = 0; i < count && g_vis_count < MAX_VISIBLE_SPRITES; ++i) {
        const SpriteRef *r = &refs[i];
        int32_t tz, tx;
        int sx;
        if (!render_project(&g_cam, r->x, r->y, &tz, &tx, &sx)) continue;
        if (tz < 24 || tz >= G2007_FAR_CM + 2000) continue;
        VisSprite *v = &g_vis[g_vis_count];
        v->tz = tz;
        v->kind = r->kind;
        v->sprite = r->sprite;
        if (r->kind == SK_SPRITE) {
            const SpriteInfo *info = &g_sprite_info[r->sprite];
            int32_t wpx = (info->world_w * G2007_FOCAL) / tz;
            int32_t zb = r->z + info->z, zt = zb + info->world_h;
            if (wpx < 1) continue;
            v->x0 = (int16_t)(sx - wpx / 2);
            v->x1 = (int16_t)(v->x0 + wpx - 1);
            v->y0 = (int16_t)(H - ((zt - g_cam.z) * G2007_FOCAL) / tz);
            v->y1 = (int16_t)(H - ((zb - g_cam.z) * G2007_FOCAL) / tz);
            if (v->y1 <= v->y0) continue;
            v->ustep = ((int32_t)info->w << 16) / wpx;
            v->vstep = ((int32_t)info->h << 16) / (v->y1 - v->y0 + 1);
            if (v->x1 < 0 || v->x0 >= W) continue;     /* cull before shading */
            int light = light_at(g_map_sectors[r->sector].light, beam_band(fx_clamp(sx, 0, W - 1)), tz);
            for (int c = 0; c < 16; ++c) {
                uint8_t src = g_sprite_pal[r->sprite][c];
                if (c == 1 && r->tint) src = r->tint;
                v->shade[c] = shade_index(src, light);
            }
        } else {
            int32_t rad = ((int32_t)r->size * 2 * G2007_FOCAL) / tz;
            if (rad < 1) rad = 1;
            v->cx = (int16_t)sx;
            v->cy = (int16_t)(H - ((r->z - g_cam.z) * G2007_FOCAL) / tz);
            v->radius = (int16_t)rad;
            v->x0 = (int16_t)(sx - rad);
            v->x1 = (int16_t)(sx + rad);
            v->y0 = (int16_t)(v->cy - rad);
            v->y1 = (int16_t)(v->cy + rad);
        }
        if (v->x1 < 0 || v->x0 >= W) continue;
        ++g_vis_count;
    }
    /* Insertion sort, far to near (painter's order inside a column). */
    for (int i = 1; i < g_vis_count; ++i) {
        VisSprite t = g_vis[i];
        int j = i - 1;
        while (j >= 0 && g_vis[j].tz < t.tz) {
            g_vis[j + 1] = g_vis[j];
            --j;
        }
        g_vis[j + 1] = t;
    }
}

static void draw_sprites_strip(int strip_x) {
    for (int i = 0; i < g_vis_count; ++i) {
        const VisSprite *v = &g_vis[i];
        int xa = fx_max(v->x0, strip_x), xb = fx_min(v->x1, strip_x + G2007_STRIP_W - 1);
        int ya = fx_max(v->y0, VY0), yb = fx_min(v->y1, VY1);
        if (xa > xb || ya > yb) continue;
        for (int x = xa; x <= xb; ++x) {
            if (v->tz >= g_zbuf[x]) continue;
            int col = x - strip_x;
            if (v->kind == SK_SPRITE) {
                const SpriteInfo *info = &g_sprite_info[v->sprite];
                int u = (int)(((x - v->x0) * v->ustep) >> 16);
                const uint8_t *px = g_sprite_pixels + info->offset;
                /* Step the texture row incrementally and fetch a texel only
                 * when the row changes (sprites are usually magnified). */
                int32_t acc = (ya - v->y0) * v->vstep;
                int last = -1;
                uint8_t c = 0;
                uint8_t *p = &g_strip[ya][col];
                for (int y = ya; y <= yb; ++y, p += G2007_STRIP_W, acc += v->vstep) {
                    int t = (int)(acc >> 16);
                    if (t != last) {
                        last = t;
                        int idx = t * info->w + u;
                        uint8_t b = px[idx >> 1];
                        c = (idx & 1) ? (uint8_t)(b & 15u) : (uint8_t)(b >> 4);
                        c = c ? v->shade[c] : 0;
                    }
                    if (c) *p = c;
                }
            } else {
                int dx = x - v->cx;
                for (int y = ya; y <= yb; ++y) {
                    int dy = y - v->cy;
                    int32_t r2 = dx * dx + dy * dy, rr = (int32_t)v->radius * v->radius;
                    if (r2 > rr) continue;
                    if (v->kind == SK_DUST) {
                        if (((x ^ y) & 1) == 0) g_strip[y][col] = 145;
                    } else if (r2 * 9 < rr) {
                        g_strip[y][col] = 1;                    /* white core   */
                    } else if (r2 * 3 < rr) {
                        g_strip[y][col] = 228;                  /* warm yellow  */
                    } else if (((x ^ y) & 1) == 0) {
                        g_strip[y][col] = 221;                  /* dithered halo */
                    }
                }
            }
        }
    }
}

/*
 * Render one frame.
 *
 * Work is skipped whenever the result would be identical: a frame whose
 * camera, sprites, doors and overlay are unchanged is not rendered or sent
 * at all; static full-screen pages are sent once; the HUD bands above and
 * below the view are sent only when their content changes. While the camera
 * moves the view is rendered at half horizontal resolution (each ray fills
 * two columns) and refined to full resolution as soon as it stops.
 */
void render_frame(const Camera *cam, const SpriteRef *refs, int count) {
    g_cam = *cam;
    if (g_cam.sector < 0) g_cam.sector = 0;
    int fill = g_ui_fill >= 0;
    int moved = !g_have_frame || cam->x != g_prev_cam.x || cam->y != g_prev_cam.y ||
                cam->z != g_prev_cam.z || cam->angle != g_prev_cam.angle;
    int xstep = RENDER_X_STEP;
#if G2007_DYNAMIC_RES
    if (moved) xstep = 2;
#endif
    /* Everything that determines the picture. */
    uint32_t h = hash_ops(0, SH - 1);
    h = hash_bytes(h, &g_ui_fill, sizeof(g_ui_fill));
    if (!fill) {
        h = hash_bytes(h, &cam->x, sizeof(cam->x));
        h = hash_bytes(h, &cam->y, sizeof(cam->y));
        h = hash_bytes(h, &cam->z, sizeof(cam->z));
        h = hash_bytes(h, &cam->angle, sizeof(cam->angle));
        h = hash_bytes(h, &cam->beam, sizeof(cam->beam));
        h = hash_bytes(h, &g_world.flags, sizeof(g_world.flags));
        for (int i = 0; i < count; ++i) {
            h = hash_bytes(h, &refs[i].x, 8);
            h = hash_bytes(h, &refs[i].z, 2);
            h = hash_bytes(h, &refs[i].sprite, 5);
        }
    }
    g_prev_cam = *cam;
#if !G2007_TEST_NO_SKIP
    if (g_have_frame && h == g_last_frame_hash && g_last_xstep <= xstep && g_last_was_fill == fill)
        return;                                   /* nothing changed: send nothing */
#endif
    uint32_t band_hash = hash_ops(0, VY0 - 1) * 31u + hash_ops(VY1 + 1, SH - 1);
    int bands = !g_have_frame || g_last_was_fill || band_hash != g_last_band_hash;
    g_last_frame_hash = h;
    g_last_band_hash = band_hash;
    g_last_xstep = (uint8_t)xstep;
    g_last_was_fill = (uint8_t)fill;
    g_have_frame = 1;

    if (!fill) {
        g_dx = fx_cos(cam->angle);
        g_dy = fx_sin(cam->angle);
        g_rgx = g_dy;
        g_rgy = -g_dx;
        g_plx = (g_rgx * (W / 2)) / G2007_FOCAL;
        g_ply = (g_rgy * (W / 2)) / G2007_FOCAL;
        prepare_sprites(refs, count);
    }
    for (int sx = 0; sx < W; sx += G2007_STRIP_W) {
        if (fill) {
            /* Full-screen page: no 3D scene behind it. */
            for (int y = 0; y < SH; ++y)
                for (int c = 0; c < G2007_STRIP_W; ++c) g_strip[y][c] = (uint8_t)g_ui_fill;
            draw_ui_strip(sx, 0, SH - 1);
            prg32_sprite_draw_indexed(sx, 0, &g_desc_full, 0);
            continue;
        }
        for (int c = 0; c < G2007_STRIP_W; c += xstep) {
            render_column(sx + c, c);
            if (xstep == 2) g_zbuf[sx + c + 1] = g_zbuf[sx + c];
        }
        if (xstep == 2) {
            /* Copy every even column into the odd one on its right, four
             * pixels per word: keep bytes 0 and 2, replicate them upwards. */
            uint32_t *wp = &g_strip_u.words[VY0 * G2007_STRIP_W / 4];
            for (int n = G2007_VIEW_H * G2007_STRIP_W / 4; n; --n, ++wp) {
                uint32_t even = *wp & 0x00FF00FFu;
                *wp = even | (even << 8);
            }
        }
        draw_sprites_strip(sx);
        draw_ui_strip(sx, VY0, VY1);
        prg32_sprite_draw_indexed(sx, VY0, &g_desc_view, 0);
        if (bands) {
            for (int y = 0; y < VY0; ++y)
                for (int c = 0; c < G2007_STRIP_W; ++c) g_strip[y][c] = BLACK;
            for (int y = VY1 + 1; y < SH; ++y)
                for (int c = 0; c < G2007_STRIP_W; ++c) g_strip[y][c] = BLACK;
            draw_ui_strip(sx, 0, VY0 - 1);
            draw_ui_strip(sx, VY1 + 1, SH - 1);
            prg32_sprite_draw_indexed(sx, 0, &g_desc_top, 0);
            prg32_sprite_draw_indexed(sx, VY1 + 1, &g_desc_bottom, 0);
        }
    }
}
