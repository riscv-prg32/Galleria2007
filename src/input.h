/*
 * Input abstraction for the two-button control scheme.
 *
 *   UP/DOWN move, LEFT/RIGHT rotate, A interact, B torch, A+B backpack.
 *
 * A and B fire on the *chord-resolved* edge: when one of them goes down we
 * wait up to G2007_CHORD_MS for the other. If both are held inside the
 * window the A+B chord fires once and the single actions are suppressed
 * until both buttons are released; otherwise the single action fires when
 * the window expires or the button is released, whichever comes first.
 * START (where present) is an extra shortcut for the backpack.
 */
#ifndef G2007_INPUT_H
#define G2007_INPUT_H

#include <stdint.h>

enum {
    IN_UP = 1u << 0, IN_DOWN = 1u << 1, IN_LEFT = 1u << 2, IN_RIGHT = 1u << 3,
    IN_A = 1u << 4, IN_B = 1u << 5, IN_START = 1u << 6
};

typedef struct {
    uint8_t held;        /* IN_* currently down (raw)                  */
    uint8_t pressed;     /* IN_UP..IN_RIGHT and IN_START rising edges  */
    uint8_t act_a;       /* resolved single A action this frame        */
    uint8_t act_b;       /* resolved single B action this frame        */
    uint8_t act_ab;      /* resolved A+B chord this frame              */
    uint8_t a_hold;      /* A held long enough to be a "hold" (not chord) */
    /* chord state machine */
    uint8_t pending;     /* IN_A / IN_B waiting for the chord window   */
    uint8_t chorded;     /* chord fired; wait for full release         */
    uint32_t pending_ms;
} InputState;

void input_reset(InputState *in);
/* Pure core, unit-testable: feed raw IN_* bits and the time in ms. */
void input_feed(InputState *in, uint8_t raw, uint32_t now_ms);
/* Read PRG32 buttons and feed them. */
void input_update(InputState *in);

#endif
