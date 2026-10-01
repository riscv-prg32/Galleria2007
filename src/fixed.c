/*
 * Fixed-point helpers (see fixed.h for the number formats).
 */
#include "fixed.h"
#include "gen/trig_table.h"

/* Quarter-wave table: 257 Q12 samples of sin over [0, 90] degrees. The other
 * three quadrants are mirrored/negated, saving 3/4 of the table memory. */
int32_t fx_sin(int32_t angle) {
    uint32_t a = (uint32_t)angle & ANG_MASK;
    uint32_t i = a & (ANG_QUARTER - 1);
    switch (a >> 8) {
    case 0: return g_sin_quarter[i];
    case 1: return g_sin_quarter[ANG_QUARTER - i];
    case 2: return -g_sin_quarter[i];
    default: return -g_sin_quarter[ANG_QUARTER - i];
    }
}

int32_t fx_cos(int32_t angle) { return fx_sin(angle + ANG_QUARTER); }

uint32_t fx_udiv64(uint32_t hi, uint32_t lo, uint32_t d) {
    if (d == 0 || hi >= d) {
        return 0xFFFFFFFFu; /* quotient would not fit in 32 bits */
    }
    uint32_t q = 0;
    for (int i = 0; i < 32; ++i) {
        uint32_t carry = hi >> 31;
        hi = (hi << 1) | (lo >> 31);
        lo <<= 1;
        q <<= 1;
        if (carry || hi >= d) {
            hi -= d;
            q |= 1u;
        }
    }
    return q;
}

int32_t fx_muldiv(int32_t a, int32_t b, int32_t c) {
    uint32_t neg = 0;
    uint32_t ua = (uint32_t)a, ub = (uint32_t)b, uc = (uint32_t)c;
    if (a < 0) { ua = 0u - ua; neg ^= 1u; }
    if (b < 0) { ub = 0u - ub; neg ^= 1u; }
    if (c < 0) { uc = 0u - uc; neg ^= 1u; }
    uint64_t p = (uint64_t)ua * ub;          /* mul + mulhu on RV32IM */
    uint32_t hi = (uint32_t)(p >> 32), lo = (uint32_t)p;
    uint32_t q;
    if (uc == 0) {
        q = 0x7FFFFFFFu;
    } else if (hi == 0) {
        q = lo / uc;                         /* fast path: hardware divu */
    } else {
        q = fx_udiv64(hi, lo, uc);           /* exact slow path */
    }
    if (q > 0x7FFFFFFFu) {
        q = 0x7FFFFFFFu;
    }
    return neg ? -(int32_t)q : (int32_t)q;
}

uint32_t fx_isqrt(uint32_t v) {
    uint32_t r = 0, bit = 1u << 30;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) {
            v -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return r;
}
