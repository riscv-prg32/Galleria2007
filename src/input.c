/*
 * Input abstraction implementation (see input.h for the chord rules).
 */
#include "input.h"
#include "config.h"
#ifndef G2007_HOST_NO_PLATFORM
#include "platform.h"
#endif

void input_reset(InputState *in) {
    uint8_t *raw = (uint8_t *)in;
    for (unsigned i = 0; i < sizeof(*in); ++i) raw[i] = 0;
}

void input_feed(InputState *in, uint8_t raw, uint32_t now_ms) {
    uint8_t prev = in->held;
    in->held = raw;
    in->pressed = (uint8_t)(raw & ~prev & (IN_UP | IN_DOWN | IN_LEFT | IN_RIGHT | IN_START));
    in->act_a = in->act_b = in->act_ab = 0;
    uint8_t ab = raw & (IN_A | IN_B);
    if (in->chorded) {                       /* swallow until both released */
        if (!ab) in->chorded = 0;
        in->a_hold = 0;
        return;
    }
    uint8_t down = (uint8_t)(ab & ~(prev & (IN_A | IN_B)));
    if (ab == (IN_A | IN_B) && (in->pending || down == (IN_A | IN_B))) {
        in->act_ab = 1;                      /* both inside the window */
        in->pending = 0;
        in->chorded = 1;
        in->a_hold = 0;
        return;
    }
    if (!in->pending && down) {
        in->pending = down;
        in->pending_ms = now_ms;
    }
    if (in->pending) {
        int released = (ab & in->pending) == 0;
        int expired = (uint32_t)(now_ms - in->pending_ms) >= G2007_CHORD_MS;
        if (released || expired) {
            if (in->pending & IN_A) in->act_a = 1;
            if (in->pending & IN_B) in->act_b = 1;
            in->pending = 0;
        }
    }
    /* "Hold A" (used to force the 2007 wall) starts after the chord window. */
    in->a_hold = (uint8_t)((ab & IN_A) && !(in->pending & IN_A) && !(ab & IN_B));
}

#ifndef G2007_HOST_NO_PLATFORM
void input_update(InputState *in) {
    uint32_t b = prg32_input_read();
    uint8_t raw = 0;
    if (b & PRG32_BTN_UP) raw |= IN_UP;
    if (b & PRG32_BTN_DOWN) raw |= IN_DOWN;
    if (b & PRG32_BTN_LEFT) raw |= IN_LEFT;
    if (b & PRG32_BTN_RIGHT) raw |= IN_RIGHT;
    if (b & PRG32_BTN_A) raw |= IN_A;
    if (b & PRG32_BTN_B) raw |= IN_B;
    if (b & PRG32_BTN_START) raw |= IN_START;
    input_feed(in, raw, prg32_ticks_ms());
}
#endif
