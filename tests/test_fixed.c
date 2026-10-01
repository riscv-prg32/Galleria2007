/* Fixed-point math tests: trig, wrapping, mul/div boundaries, projection. */
#include "test_util.h"
#include "../src/config.h"
#include "../src/fixed.c"

int main(void) {
    /* Cardinal directions (0 = east, 256 = north). */
    CHECK_EQ(fx_sin(0), 0);
    CHECK_EQ(fx_cos(0), FX_ONE);
    CHECK_EQ(fx_sin(256), FX_ONE);
    CHECK_EQ(fx_cos(256), 0);
    CHECK_EQ(fx_sin(512), 0);
    CHECK_EQ(fx_cos(512), -FX_ONE);
    CHECK_EQ(fx_sin(768), -FX_ONE);
    /* Wrapping */
    CHECK_EQ(fx_sin(1024 + 256), FX_ONE);
    CHECK_EQ(fx_sin(-256), -FX_ONE);
    CHECK_EQ(fx_angle_diff(1000, 24), 48);
    CHECK_EQ(fx_angle_diff(24, 1000), -48);
    /* Pythagoras holds within rounding for all angles. */
    int worst = 0;
    for (int a = 0; a < ANG_FULL; ++a) {
        int32_t s = fx_sin(a), c = fx_cos(a);
        int err = (int)fx_abs((s * s + c * c) / FX_ONE - FX_ONE);
        if (err > worst) worst = err;
    }
    CHECK(worst <= 4);
    /* muldiv: fast path, 64-bit path, signs, saturation. */
    CHECK_EQ(fx_muldiv(1000, 4096, 1000), 4096);
    CHECK_EQ(fx_muldiv(100000000, 4096, 100000000), 4096);
    CHECK_EQ(fx_muldiv(-300000000, 4096, 1000000), -1228800);
    CHECK_EQ(fx_muldiv(7, -9, 2), -31);
    CHECK_EQ(fx_muldiv(2000000000, 2000000000, 1), 0x7FFFFFFF);
    CHECK_EQ(fx_muldiv(5, 5, 0), 0x7FFFFFFF);
    CHECK_EQ(fx_udiv64(1, 0, 2), 0x80000000u);
    CHECK_EQ(fx_udiv64(3, 0, 2), 0xFFFFFFFFu);
    for (uint32_t i = 1; i < 200000; i += 997) {
        uint64_t n = (uint64_t)i * 123456789u;
        uint32_t d = i * 7u + 13u;
        CHECK_EQ(fx_udiv64((uint32_t)(n >> 32), (uint32_t)n, d), (uint32_t)(n / d));
    }
    CHECK_EQ(fx_isqrt(0), 0);
    CHECK_EQ(fx_isqrt(99), 9);
    CHECK_EQ(fx_isqrt(100), 10);
    CHECK_EQ(fx_isqrt(0xFFFFFFFFu), 65535);
    /* Projection boundaries: a wall at the near plane fits the int32 maths
     * used by the renderer, and the far plane maps close to the horizon. */
    int32_t scale_near = (G2007_FOCAL << 8) / G2007_NEAR_CM;
    int32_t y_near = G2007_HORIZON - (((1300 - G2007_EYE_HEIGHT) * scale_near) >> 8);
    CHECK(y_near < -20000);
    int32_t scale_far = (G2007_FOCAL << 8) / G2007_FAR_CM;
    CHECK_EQ(G2007_HORIZON - (((560 - G2007_EYE_HEIGHT) * scale_far) >> 8), 83);
    TEST_MAIN_END("test_fixed")
}
