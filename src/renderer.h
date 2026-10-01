/*
 * Doom-like 2.5D renderer for PRG32.
 *
 * The map is a set of convex sectors with floor/ceiling heights connected by
 * portals. Each screen column casts one ray and walks from sector to sector
 * through portals, drawing ceiling, upper step, lower step, floor and
 * finally a solid wall (a column-order "portal raycaster", like the Build
 * engine). Pixels are written into a 16x200 byte strip which is blitted with
 * prg32_sprite_draw_indexed(); the cartridge never owns a full framebuffer.
 * See docs/renderer.md for the derivation of every formula.
 */
#ifndef G2007_RENDERER_H
#define G2007_RENDERER_H

#include <stdint.h>
#include "config.h"

typedef struct {
    uint16_t offset;      /* into g_sprite_pixels (4 bits per pixel)      */
    uint8_t w, h;         /* texels                                       */
    uint16_t world_w, world_h; /* size in the world (cm)                  */
    uint8_t z;            /* height of the sprite bottom above its floor  */
} SpriteInfo;

typedef struct {
    int32_t x, y;         /* cm                                           */
    int32_t z;            /* eye height (cm, absolute)                    */
    int32_t angle;        /* 10-bit                                       */
    int16_t sector;
    uint8_t beam;         /* 0 torch off, 1 on, 2 boosted (battery)       */
} Camera;

enum { SK_SPRITE = 0, SK_GLOW = 1, SK_DUST = 2 };

typedef struct {
    int32_t x, y;         /* cm                                           */
    int16_t z;            /* absolute bottom height (cm)                  */
    uint8_t sprite;       /* SPR_*                                        */
    uint8_t kind;         /* SK_*                                         */
    uint8_t tint;         /* palette index for SPR_PLAYER jackets, 0=none */
    uint8_t sector;
    uint8_t size;         /* glow radius / dust size (cm / 2)             */
} SpriteRef;

/* 2D overlay composed into the same strips as the 3D view (HUD, panels,
 * menus). Text uses the cartridge's own 8x8 font, so it looks identical on
 * every PRG32 host (PRG32-QT/iOS draw prg32_gfx_text8 with a reduced font).
 * Ops are collected each frame before render_frame(). */
#define UI_MAX_OPS 64
#define UI_TEXT_POOL 1024
#define UI_TRANSPARENT 255   /* text background colour meaning "no box" */

void render_ui_reset(void);
void render_ui_rect(int x, int y, int w, int h, uint8_t color);
/* Copies up to `len` characters (stops at NUL or newline). */
void render_ui_text(int x, int y, const char *s, int len, uint8_t fg, uint8_t bg);
/* Covers the whole view: the 3D scene is not rendered behind it. */
void render_ui_fill(uint8_t color);

extern uint16_t g_zbuf[G2007_SCREEN_W];
extern uint16_t g_pal565[256];

void render_init(void);
void render_frame(const Camera *cam, const SpriteRef *refs, int count);
/* Camera-space projection; returns 0 when behind the camera. */
int render_project(const Camera *cam, int32_t x, int32_t y, int32_t *depth,
                   int32_t *lateral, int *screen_x);

#endif
