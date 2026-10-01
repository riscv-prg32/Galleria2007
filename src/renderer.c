/*
 * Portal renderer implementation (see renderer.h and docs/renderer.md).
 *
 * Number formats used below:
 *   d        perpendicular distance to a wall hit (cm)
 *   scale    pixels per cm at distance d, Q8: (FOCAL << 8) / d
 *   R        column ray direction, Q12, with the forward component = 1.0
 *   v        vertical texture coordinate, Q16 texels (1 texel = 4 cm)
 *   L        brightness 0..16 (16 = full texture brightness)
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
#define BLACK 16              /* system palette cube entry (0,0,0) */

uint16_t g_zbuf[W];
uint16_t g_pal565[256];

static uint8_t g_strip[SH][G2007_STRIP_W];
static prg32_indexed_sprite_t g_strip_desc;
static Camera g_cam;
static int32_t g_dx, g_dy;    /* forward unit vector, Q12      */
static int32_t g_rgx, g_rgy;  /* right unit vector, Q12        */
static int32_t g_plx, g_ply;  /* camera plane (right * W/2/FOCAL), Q12 */

/* 2x2 ordered dither added before the >> 4 of the shading product. */
static const uint8_t k_bayer[4] = {0, 8, 12, 4};

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
    op->len = (uint8_t)(n > 255 ? 255 : n);
    op->text = (uint16_t)g_ui_pool_used;
    for (int i = 0; i < op->len; ++i) g_ui_pool[g_ui_pool_used++] = s[i];
}

/* Compose the overlay ops that intersect one strip, in submission order. */
static void draw_ui_strip(int strip_x) {
    int sx1 = strip_x + G2007_STRIP_W - 1;
    for (int i = 0; i < g_ui_count; ++i) {
        const UiOp *op = &g_ui[i];
        if (op->kind == OP_RECT) {
            int xa = fx_max(op->x, strip_x), xb = fx_min(op->x + op->w - 1, sx1);
            int ya = fx_max(op->y, 0), yb = fx_min(op->y + op->h - 1, SH - 1);
            for (int y = ya; y <= yb; ++y)
                for (int x = xa; x <= xb; ++x) g_strip[y][x - strip_x] = op->fg;
            continue;
        }
        int x0 = op->x, x1 = op->x + 8 * op->len - 1;
        if (x1 < strip_x || x0 > sx1) continue;
        int xa = fx_max(x0, strip_x), xb = fx_min(x1, sx1);
        for (int x = xa; x <= xb; ++x) {
            int cx = x - x0;
            unsigned ch = (unsigned char)g_ui_pool[op->text + (cx >> 3)];
            if (ch < 32 || ch > 126) ch = '?';
            uint8_t bit = (uint8_t)(0x80u >> (cx & 7));
            for (int r = 0; r < 8; ++r) {
                int y = op->y + r;
                if ((unsigned)y >= SH) continue;
                if (g_ui_font[ch - 32][r] & bit) g_strip[y][x - strip_x] = op->fg;
                else if (op->bg != UI_TRANSPARENT) g_strip[y][x - strip_x] = op->bg;
            }
        }
    }
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
     * blitted with g_pal565 and rect_indexed() UI colours resolve to exactly
     * these entries on the ESP32-C6, QEMU, PRG32-QT and PRG32-iOS alike.
     * Entries 8..15 and 232..255 are left untouched (unused). */
    for (int i = 0; i < 232; ++i) {
        if (i < 8 || i >= 16) prg32_palette_set((uint8_t)i, g_pal565[i]);
    }
    g_strip_desc.pixels = &g_strip[0][0];
    g_strip_desc.palette = g_pal565;
    g_strip_desc.width = G2007_STRIP_W;
    g_strip_desc.height = SH;
    g_strip_desc.frame_count = 1;
    g_strip_desc.palette_count = 256;
    g_strip_desc.bits_per_pixel = PRG32_SPRITE_BPP_8;
    g_strip_desc.transparent_index = -1;
}

/* Brightness from ambient light, torch band and distance (tables built by
 * tools/build_assets.py: exponential fog and a torch fall-off). */
static inline int light_at(int base, int band, int32_t d) {
    int i = d >> GEN_FOG_STEP_SHIFT;
    if (i > 63) i = 63;
    int l = (base * g_fog[i] + band * g_flash[i]) >> 4;
    return l > 16 ? 16 : l;
}

/* Torch beam: 3 discrete bands around the screen centre. */
static int beam_band(int x) {
    if (!g_cam.beam) return 0;
    int dx = x - W / 2;
    if (dx < 0) dx = -dx;
    int boost = g_cam.beam > 1 ? 3 : 0;
    if (dx < 34) return 13 + boost;
    if (dx < 68) return 9 + boost;
    if (dx < 110) return 4 + boost;
    return 1;
}

static inline uint8_t texel(int pattern, int u, int v) {
    uint8_t b = g_patterns[pattern][(v << 4) | (u >> 1)];
    return (u & 1) ? (uint8_t)(b & 15u) : (uint8_t)(b >> 4);
}

static void fill_black(int col, int y0, int y1) {
    for (int y = y0; y <= y1; ++y) g_strip[y][col] = BLACK;
}

static void draw_wall(int col, int x, int y0, int y1, int32_t d, int tex, int u, int light) {
    if (y0 > y1) return;
    const uint8_t *ramp = g_ramps[g_tex_material[tex]];
    int pat = g_tex_pattern[tex];
    int32_t vstep = (d << 14) / G2007_FOCAL;
    int32_t v = -g_cam.z * 16384 + (y0 - H) * vstep;
    uint8_t *p = &g_strip[y0][col];
    for (int y = y0; y <= y1; ++y) {
        int lum = texel(pat, u, (v >> 16) & 31);
        *p = ramp[(lum * light + k_bayer[((y & 1) << 1) | (x & 1)]) >> 4];
        p += G2007_STRIP_W;
        v += vstep;
    }
}

/* Floor or ceiling rows. The distance of a flat pixel depends only on its
 * row and the height difference: d = hdiff * FOCAL / |y - H| (table). */
static void draw_flat(int col, int x, int y0, int y1, int32_t hdiff, int tex,
                      int base, int band, int32_t rx, int32_t ry) {
    if (y0 > y1) return;
    if (hdiff <= 0) { fill_black(col, y0, y1); return; }
    const uint8_t *ramp = g_ramps[g_tex_material[tex]];
#if G2007_TEXTURED_FLATS
    int pat = g_tex_pattern[tex];
#else
    (void)rx; (void)ry; (void)tex;
#endif
    uint8_t *p = &g_strip[y0][col];
    for (int y = y0; y <= y1; ++y, p += G2007_STRIP_W) {
        int k = y - H;
        if (k < 0) k = -k;
        if (k == 0) { *p = BLACK; continue; }
        int32_t dist = (hdiff * g_recip_row[k]) >> 8;
        if (dist >= G2007_FAR_CM) { *p = BLACK; continue; }
        int light = light_at(base, band, dist);
#if G2007_TEXTURED_FLATS
        int32_t wx = g_cam.x + ((rx * dist) >> FX_SHIFT);
        int32_t wy = g_cam.y + ((ry * dist) >> FX_SHIFT);
        int lum = texel(pat, (wx >> 2) & 31, (wy >> 2) & 31);
#else
        int lum = 10;
#endif
        *p = ramp[(lum * light + k_bayer[((y & 1) << 1) | (x & 1)]) >> 4];
    }
}

static inline int32_t project_y(int32_t z, int32_t scale) {
    return H - (((z - g_cam.z) * scale) >> 8);
}

static void render_column(int x, int col) {
    int32_t camx = ((2 * x + 1 - W) * FX_ONE) / W;
    int32_t rx = g_dx + ((g_plx * camx) >> FX_SHIFT);
    int32_t ry = g_dy + ((g_ply * camx) >> FX_SHIFT);
    int band = beam_band(x);
    int ytop = 0, ybot = SH - 1, s = g_cam.sector, skip = -1;
    g_zbuf[x] = G2007_FAR_CM;
    for (int depth = 0; depth < G2007_MAX_PORTALS && s >= 0; ++depth) {
        const MapSector *sec = &g_map_sectors[s];
        int w = world_exit_wall(s, skip, g_cam.x, g_cam.y, rx, ry);
        if (w < 0) break;
        const MapWall *wall = &g_map_walls[w];
        const MapVertex *a = &g_map_vertices[wall->v0];
        const MapVertex *b = &g_map_vertices[wall->v1];
        int32_t ex = b->x - a->x, ey = b->y - a->y;
        int32_t denom = rx * ey - ry * ex;                       /* cross(R, E) */
        int32_t num = (a->x - g_cam.x) * ey - (a->y - g_cam.y) * ex; /* cross(D, E) */
        int32_t d = denom > 0 ? fx_muldiv(num, FX_ONE, denom) : G2007_FAR_CM;
        d = fx_clamp(d, G2007_NEAR_CM, G2007_FAR_CM);
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
        int ya = fx_max(v->y0, 0), yb = fx_min(v->y1, SH - 1);
        for (int x = xa; x <= xb; ++x) {
            if (v->tz >= g_zbuf[x]) continue;
            int col = x - strip_x;
            if (v->kind == SK_SPRITE) {
                const SpriteInfo *info = &g_sprite_info[v->sprite];
                int u = (int)(((x - v->x0) * v->ustep) >> 16);
                const uint8_t *px = g_sprite_pixels + info->offset;
                for (int y = ya; y <= yb; ++y) {
                    int t = (int)(((y - v->y0) * v->vstep) >> 16);
                    int idx = t * info->w + u;
                    uint8_t b = px[idx >> 1];
                    uint8_t c = (idx & 1) ? (b & 15u) : (b >> 4);
                    if (c) g_strip[y][col] = v->shade[c];
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

void render_frame(const Camera *cam, const SpriteRef *refs, int count) {
    g_cam = *cam;
    if (g_cam.sector < 0) g_cam.sector = 0;
    g_dx = fx_cos(cam->angle);
    g_dy = fx_sin(cam->angle);
    g_rgx = g_dy;
    g_rgy = -g_dx;
    g_plx = (g_rgx * (W / 2)) / G2007_FOCAL;
    g_ply = (g_rgy * (W / 2)) / G2007_FOCAL;
    if (g_ui_fill < 0) prepare_sprites(refs, count);
    for (int sx = 0; sx < W; sx += G2007_STRIP_W) {
        if (g_ui_fill >= 0) {
            /* Full-screen page: no 3D scene behind it. */
            for (int y = 0; y < SH; ++y)
                for (int c = 0; c < G2007_STRIP_W; ++c) g_strip[y][c] = (uint8_t)g_ui_fill;
        } else {
            for (int c = 0; c < G2007_STRIP_W; c += RENDER_X_STEP) {
                render_column(sx + c, c);
#if RENDER_X_STEP == 2
                for (int y = 0; y < SH; ++y) g_strip[y][c + 1] = g_strip[y][c];
                g_zbuf[sx + c + 1] = g_zbuf[sx + c];
#endif
            }
            draw_sprites_strip(sx);
        }
        draw_ui_strip(sx);
        prg32_sprite_draw_indexed(sx, 0, &g_strip_desc, 0);
    }
}
