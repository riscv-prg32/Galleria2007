/*
 * The only place where the PRG32 public API enters the cartridge.
 * Portable builds resolve every prg32_* call through the firmware ABI table;
 * host tests link the same declarations against tests/host/prg32_host.c.
 */
#ifndef G2007_PLATFORM_H
#define G2007_PLATFORM_H
#include "prg32.h"

/*
 * Features advertised by the running host in its PABI table
 * (PRG32_FEATURE_* bits). The same cartridge runs on the ESP32-C6 firmware,
 * the QEMU firmware, PRG32-QT and PRG32-iOS; optional services (multiplayer
 * is not offered by PRG32-iOS) are used only when advertised. The portable
 * stubs emitted by the PRG32 builder store the table pointer passed in a0
 * into __prg32_abi on every entry (init/update/draw).
 */
#ifdef G2007_HOST
uint32_t g2007_host_features(void);
#else
extern const prg32_abi_table_t *__prg32_abi;
static inline uint32_t g2007_host_features(void) {
    return __prg32_abi ? __prg32_abi->provided_features : 0u;
}
#endif
#endif
