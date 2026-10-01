/*
 * The only place where the PRG32 public API enters the cartridge.
 * Portable builds resolve every prg32_* call through the firmware ABI table;
 * host tests link the same declarations against tests/host/prg32_host.c.
 */
#ifndef G2007_PLATFORM_H
#define G2007_PLATFORM_H
#include "prg32.h"
#endif
