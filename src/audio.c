/*
 * Adaptive audio implementation (see audio.h and docs/audio.md).
 */
#include "audio.h"
#include "platform.h"
#include "gen/audio_ids.h"

#define CH_AMBIENT 4
#define CH_EVENT 5
#define CH_STEREO_A 6   /* extra voices of the 8-voice stereo profile */
#define CH_STEREO_B 7

static uint8_t g_stereo;

static uint8_t g_mus_state = 0xFF, g_mus_wanted;
static uint32_t g_mus_start_ms, g_now_ms, g_next_drip_ms;
static uint16_t g_rng = 0x6007;

static uint16_t rnd(void) {
    g_rng = (uint16_t)(g_rng * 25173u + 13849u);
    return g_rng;
}

/* Track and tempo of each state (SILENCE has none). Explicit switch keeps
 * the code free of pointer tables (portable cartridges cannot relocate). */
static int state_track(uint8_t s, uint32_t *bar_ms) {
    uint32_t bpm = 84;
    int t = -1;
    switch (s) {
    case MUS_EXPLORATION: t = TRACK_EXPLORATION; bpm = TRACK_EXPLORATION_BPM; break;
    case MUS_DISCOVERY: t = TRACK_DISCOVERY; bpm = TRACK_DISCOVERY_BPM; break;
    case MUS_MYSTERY: t = TRACK_MYSTERY; bpm = TRACK_MYSTERY_BPM; break;
    case MUS_THREAT: t = TRACK_THREAT; bpm = TRACK_THREAT_BPM; break;
    case MUS_CHASE: t = TRACK_CHASE; bpm = TRACK_CHASE_BPM; break;
    case MUS_MEMORY: t = TRACK_MEMORY; bpm = TRACK_MEMORY_BPM; break;
    default: break;
    }
    *bar_ms = 4u * 60000u / bpm;
    return t;
}

static void start_state(uint8_t s) {
    uint32_t bar;
    int t = state_track(s, &bar);
    g_mus_state = s;
    g_mus_start_ms = g_now_ms;
    if (t < 0) {
        prg32_audio_stop_track();
        for (uint8_t ch = 0; ch < 4; ++ch) prg32_audio_note_off(ch);
    } else {
        prg32_audio_play_track((uint16_t)t);
    }
}

void audio_init(void) {
    g_stereo = (uint8_t)(prg32_audio_get_mode() == PRG32_AUDIO_MODE_STEREO);
    g_mus_state = 0xFF;
    g_mus_wanted = MUS_EXPLORATION;
    g_next_drip_ms = 0;
}

uint8_t audio_current(void) { return g_mus_state; }

uint8_t audio_is_stereo(void) { return g_stereo; }

/* Pan is a per-voice setting read by the next note; centre (0) falls back
 * to the instrument's default pan. */
static void play(uint8_t ch, uint8_t ins, uint8_t note, uint32_t vol, uint32_t ms, int8_t pan) {
    if (vol == 0) return;
    prg32_audio_set_channel_pan(ch, pan);
    prg32_audio_note(ch, ins, note, (uint8_t)(vol > 255 ? 255 : vol), ms);
}

void audio_request(uint8_t state) { g_mus_wanted = state; }

void audio_update(uint32_t now_ms, uint8_t ambient_water) {
    g_now_ms = now_ms;
    if (g_mus_wanted != g_mus_state) {
        int urgent = g_mus_state == 0xFF || g_mus_wanted == MUS_CHASE ||
                     g_mus_wanted == MUS_THREAT || g_mus_wanted == MUS_DISCOVERY ||
                     g_mus_wanted == MUS_SILENCE || g_mus_state == MUS_SILENCE;
        uint32_t bar;
        state_track(g_mus_state, &bar);
        uint32_t into_bar = (now_ms - g_mus_start_ms) % bar;
        /* Never cut the discovery motif short. */
        if (g_mus_state == MUS_DISCOVERY && now_ms - g_mus_start_ms < 2600) urgent = 0, into_bar = 1000;
        if (urgent || into_bar < 60) start_state(g_mus_wanted);
    }
    /* Environment: water drops, more frequent near the cistern. */
    if ((int32_t)(now_ms - g_next_drip_ms) >= 0) {
        if (g_next_drip_ms) {
            /* Drops fall somewhere around the listener: random side. */
            int8_t pan = (int8_t)((int)(rnd() % 113u) - 56);
            play(g_stereo ? CH_STEREO_B : CH_AMBIENT, INSTR_DRIP, (uint8_t)(88 + (rnd() & 7)),
                 ambient_water ? 70 : 40, 60, pan);
        }
        g_next_drip_ms = now_ms + (ambient_water ? 900u : 2500u) + (rnd() % 4000u);
    }
}

void audio_sfx_at(uint8_t sfx, int8_t pan, uint8_t gain) {
    uint32_t g = gain;
    uint8_t amb = CH_AMBIENT, ev = CH_EVENT;
    uint8_t far = g_stereo ? CH_STEREO_A : CH_AMBIENT;   /* LampMan, distant events */
    switch (sfx) {
    case SFX_STEP: {
        static uint8_t foot;
        foot ^= 1u;                                       /* left, right, left... */
        play(amb, INSTR_STEP, (uint8_t)(38 + (rnd() & 3)), 60 * g / 255, 45, foot ? -10 : 10);
        break;
    }
    case SFX_CLICK: play(ev, INSTR_CLICK, 86, 110 * g / 255, 25, 14); break;   /* torch hand */
    case SFX_PICK: play(ev, INSTR_CHIME, 79, 140 * g / 255, 140, pan); break;
    case SFX_DROP: play(ev, INSTR_STEP, 46, 120 * g / 255, 90, pan); break;
    case SFX_MENU: play(ev, INSTR_CLICK, 72, 90 * g / 255, 30, 0); break;
    case SFX_REGISTER: play(ev, INSTR_CHIME, 88, 150 * g / 255, 200, pan); break;
    case SFX_DOOR: play(ev, INSTR_RUMBLE, 40, 180 * g / 255, 500, pan); break;
    case SFX_RUMBLE:
        play(ev, INSTR_RUMBLE, 28, 230 * g / 255, 1600, pan);
        if (g_stereo) play(CH_STEREO_B, INSTR_RUMBLE, 33, 170 * g / 255, 1300, (int8_t)-pan);
        break;
    case SFX_DRIP: play(amb, INSTR_DRIP, 92, 60 * g / 255, 60, pan); break;
    case SFX_ALERT: play(far, INSTR_ALERT, 62, 160 * g / 255, 320, pan); break;
    case SFX_CAUGHT: play(ev, INSTR_ALERT, 49, 200 * g / 255, 900, 0); break;
    case SFX_WATER: play(amb, INSTR_DRIP, 76, 80 * g / 255, 120, pan); break;
    case SFX_LAMP_STEP: play(far, INSTR_HEAVY, (uint8_t)(31 + (rnd() & 1)), 120 * g / 255, 70, pan); break;
    default: break;
    }
}

void audio_sfx(uint8_t sfx) { audio_sfx_at(sfx, 0, 255); }
