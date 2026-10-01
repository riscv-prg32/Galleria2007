/*
 * Fixed-point helpers.
 *
 * The ESP32-C6 has no floating-point unit and cartridges link without libgcc,
 * so all hot-path math is integer:
 *   - world coordinates are centimetres (int32), player positions add 8
 *     fractional bits (Q8, "cm * 256");
 *   - directions are Q12 vectors (4096 == 1.0);
 *   - angles are 10-bit (1024 units per full turn) and wrap with a mask.
 * 64-bit products are allowed (RV32IM computes them with mul/mulhu), but
 * 64-bit division is not (it would call __divdi3), hence fx_muldiv().
 */
#ifndef G2007_FIXED_H
#define G2007_FIXED_H

#include <stdint.h>

#define FX_SHIFT 12
#define FX_ONE (1 << FX_SHIFT)
#define ANG_BITS 10
#define ANG_FULL (1 << ANG_BITS)
#define ANG_MASK (ANG_FULL - 1)
#define ANG_QUARTER (ANG_FULL / 4)

/* Q12 sine/cosine of a 10-bit angle (angle 0 = east, 256 = north). */
int32_t fx_sin(int32_t angle);
int32_t fx_cos(int32_t angle);

/* (a * b) / c with a 64-bit intermediate; truncates toward zero and
 * saturates to +/-INT32_MAX (also when c == 0). */
int32_t fx_muldiv(int32_t a, int32_t b, int32_t c);

/* Unsigned (hi:lo) / d by shift-and-subtract. Returns 0xFFFFFFFF when the
 * quotient does not fit 32 bits. Slow path of fx_muldiv. */
uint32_t fx_udiv64(uint32_t hi, uint32_t lo, uint32_t d);

/* Integer square root (floor). */
uint32_t fx_isqrt(uint32_t v);

static inline int32_t fx_abs(int32_t v) { return v < 0 ? -v : v; }
static inline int32_t fx_min(int32_t a, int32_t b) { return a < b ? a : b; }
static inline int32_t fx_max(int32_t a, int32_t b) { return a > b ? a : b; }
static inline int32_t fx_clamp(int32_t v, int32_t lo, int32_t hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

/* Signed shortest angular difference b - a in (-512, 512]. */
static inline int32_t fx_angle_diff(int32_t a, int32_t b) {
    int32_t d = (b - a) & ANG_MASK;
    return d > ANG_FULL / 2 ? d - ANG_FULL : d;
}

#endif
