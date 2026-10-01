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
#define G2007_HORIZON 100      /* must match tools/build_assets.py HORIZON  */
#define G2007_FOCAL 176        /* focal length in pixels (~84 deg FOV);
                                  must match tools/build_assets.py FOCAL    */
#define G2007_STRIP_W 16       /* columns rendered per blit (16x200 bytes)  */
#define RENDER_X_STEP 1        /* 1 = full resolution, 2 = half horizontal  */
#define G2007_MAX_PORTALS 32   /* portal depth limit per screen column      */
#define G2007_NEAR_CM 8        /* near clipping distance                    */
#define G2007_FAR_CM 4000      /* beyond this everything is darkness        */
#define G2007_TEXTURED_FLATS 1 /* 0 = shaded flat floors/ceilings (faster)  */

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

#endif
