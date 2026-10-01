/* Input tests: A+B chord, single-button actions, hold, no accidental torch. */
#define G2007_HOST_NO_PLATFORM 1
#include "test_util.h"
#include "../src/config.h"
#include "../src/input.c"

static InputState in;
static uint32_t now;
static int a, b, ab;

static void step(uint8_t raw, int ms) {
    for (int t = 0; t < ms; t += 10) {
        now += 10;
        input_feed(&in, raw, now);
        a += in.act_a;
        b += in.act_b;
        ab += in.act_ab;
    }
}

static void reset(void) { input_reset(&in); a = b = ab = 0; }

int main(void) {
    /* Quick A tap: one interaction. */
    reset(); step(IN_A, 40); step(0, 200);
    CHECK_EQ(a, 1); CHECK_EQ(b, 0); CHECK_EQ(ab, 0);
    /* Quick B tap: torch toggles once. */
    reset(); step(IN_B, 40); step(0, 200);
    CHECK_EQ(b, 1); CHECK_EQ(a, 0);
    /* A then B inside the chord window: backpack only, no torch, no A. */
    reset(); step(IN_A, 50); step(IN_A | IN_B, 300); step(IN_B, 50); step(0, 200);
    CHECK_EQ(ab, 1); CHECK_EQ(a, 0); CHECK_EQ(b, 0);
    /* Both together, held: one chord only (requires release to repeat). */
    reset(); step(IN_A | IN_B, 600); step(0, 50); step(IN_A | IN_B, 50); step(0, 100);
    CHECK_EQ(ab, 2); CHECK_EQ(a, 0); CHECK_EQ(b, 0);
    /* Holding A alone fires A once after the window, then reports a hold. */
    reset(); step(IN_A, 400);
    CHECK_EQ(a, 1); CHECK(in.a_hold);
    step(0, 50);
    CHECK(!in.a_hold);
    /* Direction edges. */
    reset(); input_feed(&in, IN_UP, 10); CHECK(in.pressed & IN_UP);
    input_feed(&in, IN_UP, 20); CHECK(!(in.pressed & IN_UP));
    TEST_MAIN_END("test_input")
}
