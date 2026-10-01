/*
 * Galleria 2007 - compile-time configuration.
 *
 * Every tunable engine limit lives here so that students can see the whole
 * memory/performance budget in one place. Values marked "must match" are
 * also used by the Python asset tools; the build fails if they disagree.
 */
#ifndef G2007_CONFIG_H
#define G2007_CONFIG_H

/* ---- PRG32 profile ------------------------------------------------------
 * Upstream PRG32 (main, 2026-10) defaults to a 64 KiB executable cartridge
 * RAM window on both ESP32-C6 and QEMU, and a 64 KiB stored package limit
 * (code + AUD0 audio + Store metadata/artwork). build.sh passes
 * --cart-ram-kib to the builder and checks the final package size. */
#define G2007_CART_RAM_KIB 64
#define G2007_PACKAGE_LIMIT_KIB 64

/* ---- Language variant ------------------------------------------------
 * Exactly one language is compiled into a cartridge (see docs/localization.md).
 * build.sh defines G2007_LANG_EN for the English variant; Italian is the
 * primary language and the default. */
#if !defined(G2007_LANG_IT) && !defined(G2007_LANG_EN)
#define G2007_LANG_IT 1
#endif

/* ---- Screen / projection ---------------------------------------------- */
#define G2007_SCREEN_W 320
#define G2007_SCREEN_H 200
/* The 3D view is a 320x160 window (rows 16..175). The rows above and below
 * are HUD bands that are sent to the display only when their content
 * changes: the SPI transfer of the dirty area dominates the frame time on
 * the ESP32-C6, so fewer changing pixels means more frames per second. */
#define G2007_VIEW_Y0 16       /* first row of the 3D view                  */
#define G2007_VIEW_H 160       /* rows of the 3D view                       */
#define G2007_HORIZON (G2007_VIEW_Y0 + G2007_VIEW_H / 2)
#define G2007_FOCAL 176        /* focal length in pixels (~84 deg FOV);
                                  must match tools/build_assets.py FOCAL    */
#define G2007_STRIP_W 16       /* columns rendered per blit (16x200 bytes)  */
#ifndef RENDER_X_STEP
#define RENDER_X_STEP 1        /* 1 = full resolution, 2 = always half horizontal */
#endif
#ifndef G2007_DYNAMIC_RES
#define G2007_DYNAMIC_RES 1    /* half horizontal resolution while the camera
                                  moves, full resolution when it is still   */
#endif
#ifndef G2007_FLAT_Y_STEP
#define G2007_FLAT_Y_STEP 2    /* floor/ceiling rows shaded per pair (1 = each) */
#endif
#ifndef G2007_FLAT_TEX_FAR
#define G2007_FLAT_TEX_FAR 1100 /* cm: farther floor/ceiling rows are not textured */
#endif
#define G2007_MAX_PORTALS 32   /* portal depth limit per screen column      */
#define G2007_NEAR_CM 8        /* near clipping distance                    */
#define G2007_FAR_CM 4000      /* beyond this everything is darkness        */
#ifndef G2007_TEXTURED_FLATS
#define G2007_TEXTURED_FLATS 1 /* 0 = shaded flat floors/ceilings (faster)  */
#endif

/* ---- Player ------------------------------------------------------------ */
#define G2007_EYE_HEIGHT 160   /* cm above the floor                        */
#define G2007_STEP_MAX 44      /* highest step a walker can climb (cm)      */
#define G2007_HEADROOM 180     /* minimum floor-to-ceiling gap to pass (cm) */
#define G2007_RADIUS 28        /* collision radius (cm)                     */
#define G2007_WALK_CM_S 190    /* walking speed                             */
#define G2007_TURN_UNITS_S 420 /* rotation speed (1024 units per turn)      */
#define G2007_REACH_CM 170     /* interaction reach with the torch off      */
#define G2007_REACH_LIGHT_CM 230 /* interaction reach with the torch on     */

/* ---- Simulation --------------------------------------------------------- */
#define G2007_TICK_MS 33       /* fixed logic step (~30 Hz, deterministic)  */
#define G2007_MAX_TICKS 4      /* catch-up limit per frame                  */
#define G2007_CHORD_MS 110     /* window in which A and B form the A+B chord */

/* ---- World pools -------------------------------------------------------- */
#define BACKPACK_SLOTS 5
#define MAX_WORLD_DROPS 16     /* item-instance pool; items are moved, never
                                  created, so the pool cannot overflow      */
#define MAX_PLAYERS 4          /* local player + up to 3 remote players     */
#define MAX_VISIBLE_SPRITES 32
#define MAX_PARTICLES 8

/* ---- Network ------------------------------------------------------------ */
#define G2007_NET_SIGNATURE "galleria2007-v1" /* shared by all languages    */
#define G2007_NET_HOLD_TICKS 2 /* ticks a record stays published (> 50 ms)  */

/* ---- Diagnostics (never enable in Store builds) ------------------------- */
#define G2007_DEBUG_STATS 0
#define G2007_DEBUG_MAP 0
#define G2007_DEBUG_AI 0
#define G2007_DEBUG_NET 0

/* ---- Test scenes (performance measurements only) -------------------------
 * Must stay 0 in the sources. scripts/perf.sh defines them in wrapper files
 * to boot straight into a fixed view: G2007_TEST_SCENE 1 plus G2007_TEST_X,
 * G2007_TEST_Y (authoring cm, as in assets/map/galleria2007.json),
 * G2007_TEST_ANGLE (degrees) and G2007_TEST_FLAGS (WF_* bits).
 * G2007_TEST_NO_SKIP 1 renders and sends every frame even when nothing
 * changed, to measure the cost of a full frame. The rendering knobs above
 * can also be overridden from a wrapper. */
#ifndef G2007_TEST_SCENE
#define G2007_TEST_SCENE 0
#endif
#ifndef G2007_TEST_NO_SKIP
#define G2007_TEST_NO_SKIP 0
#endif

#endif
