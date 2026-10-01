/*
 * GALLERIA 2007 - PRG32 portable cartridge entry point.
 *
 * The PRG32 cartridge builder compiles exactly one C source, so this file is
 * a "unity build": it includes every module. Modules stay in separate files
 * for readability and so the pure ones can be unit-tested on the host.
 *
 * Language: build.sh compiles a tiny wrapper that defines G2007_LANG_EN (or
 * nothing, for Italian) and includes this file. See docs/localization.md.
 */
#include "config.h"
#include <stddef.h>
#include <stdint.h>

#include "gen/string_ids.h"
#if defined(G2007_LANG_EN)
#include "gen/strings_en.h"
#else
#include "gen/strings_it.h"
#endif

#include "fixed.c"
#include "world.c"
#include "inventory.c"
#include "ai.c"
#include "net_proto.c"
#include "input.c"
#include "renderer.c"
#include "audio.c"
#include "multiplayer.c"
#include "ui.c"
#include "game.c"

#ifndef G2007_HOST
/* The cartridge links with -nostdlib; GCC may still emit calls to these
 * for structure copies and zero-initialisation. */
__attribute__((optimize("no-tree-loop-distribute-patterns")))
void *memset(void *dst, int value, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    while (n--) *d++ = (uint8_t)value;
    return dst;
}

__attribute__((optimize("no-tree-loop-distribute-patterns")))
void *memcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) *d++ = *s++;
    return dst;
}
#endif

/* PRG32 lifecycle: init once, then update + draw every frame. */
void galleria2007_init(void) { game_init(); }
void galleria2007_update(void) { game_update(); }
void galleria2007_draw(void) { game_draw(); }
