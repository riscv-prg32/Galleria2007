/*
 * Adaptive stereo audio: one tracker track per musical state (voices 0..3,
 * spread across the stereo field with tracker SET_PAN moves) plus positional
 * procedural effects (voices 4..5, and 6..7 when the board runs in stereo).
 * Silence is a deliberate state (environment sounds only). Mono-safe.
 */
#ifndef G2007_AUDIO_H
#define G2007_AUDIO_H

#include <stdint.h>

enum {
    MUS_SILENCE = 0, MUS_EXPLORATION, MUS_DISCOVERY, MUS_MYSTERY,
    MUS_THREAT, MUS_CHASE, MUS_MEMORY, MUS_COUNT
};

enum {
    SFX_STEP = 0, SFX_CLICK, SFX_PICK, SFX_DROP, SFX_MENU, SFX_REGISTER,
    SFX_DOOR, SFX_RUMBLE, SFX_DRIP, SFX_ALERT, SFX_CAUGHT, SFX_WATER,
    SFX_LAMP_STEP
};

void audio_init(void);
/* Ask for a musical state. Urgent states (CHASE, THREAT, DISCOVERY,
 * SILENCE) switch at once; calmer changes wait for the next bar line. */
void audio_request(uint8_t state);
void audio_update(uint32_t now_ms, uint8_t ambient_water);
void audio_sfx(uint8_t sfx);
/* Positional effect: pan -64 (left) .. 63 (right), gain 0..255. In mono
 * the firmware ignores the pan, so every effect stays audible. */
void audio_sfx_at(uint8_t sfx, int8_t pan, uint8_t gain);
uint8_t audio_is_stereo(void);
uint8_t audio_current(void);

#endif
