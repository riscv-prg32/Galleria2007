/* Host implementation of the PRG32 calls used by Galleria 2007.
 * The framebuffer is the 320x200 game viewport in RGB565, filled with the
 * same semantics as the firmware's indexed sprite path, so host frames are
 * pixel-identical to QEMU. Include once, after src/galleria2007.c. */
#ifndef G2007_HOST_PRG32_H
#define G2007_HOST_PRG32_H
#include <stdio.h>
#include <string.h>

uint16_t host_fb[200][320];
uint32_t host_ms = 1000;
uint32_t host_buttons;
int host_blits, host_notes, host_tracks_started, host_last_track = -1;
int host_mp_joined;
prg32_player_state_t host_peers[8];
int host_peer_count;
prg32_player_state_t host_local;
uint32_t host_rng = 12345;

uint32_t prg32_ticks_ms(void) { return host_ms; }
uint32_t prg32_input_read(void) { return host_buttons; }
uint32_t prg32_random_number(uint32_t min, uint32_t max) {
    host_rng = host_rng * 1103515245u + 12345u;
    return max <= min ? min : min + (host_rng >> 8) % (max - min + 1);
}
/* The cartridge composes its own text into the strips; these host calls
 * exist only so that an accidental direct use would still link and show. */
void prg32_gfx_text8(int x, int y, const char *s, uint16_t fg, uint16_t bg) {
    (void)x; (void)y; (void)s; (void)fg; (void)bg;
}
void prg32_gfx_rect_indexed(int x, int y, int w, int h, uint8_t index) {
    for (int py = y; py < y + h; ++py)
        for (int px = x; px < x + w; ++px)
            if ((unsigned)px < 320 && (unsigned)py < 200) host_fb[py][px] = g_pal565[index];
}
void prg32_sprite_draw_indexed(int x, int y, const prg32_indexed_sprite_t *s, uint32_t frame) {
    (void)frame;
    ++host_blits;
    for (int r = 0; r < s->height; ++r)
        for (int c = 0; c < s->width; ++c) {
            uint8_t i = s->pixels[r * s->width + c];
            int px = x + c, py = y + r;
            if ((unsigned)px < 320 && (unsigned)py < 200 && i < s->palette_count)
                host_fb[py][px] = s->palette[i];
        }
}
void prg32_audio_note(uint8_t ch, uint8_t ins, uint8_t note, uint8_t vol, uint32_t ms) {
    (void)ch; (void)ins; (void)note; (void)vol; (void)ms; ++host_notes;
}
void prg32_audio_note_off(uint8_t ch) { (void)ch; }
int host_palette_sets;
uint32_t host_features = PRG32_FEATURE_AUDIO | PRG32_FEATURE_AUDIO_PLUS | PRG32_FEATURE_MULTIPLAYER | PRG32_FEATURE_SPRITES;
uint32_t g2007_host_features(void) { return host_features; }
void prg32_palette_set(uint8_t index, uint16_t rgb565) { (void)index; (void)rgb565; ++host_palette_sets; }
int host_stereo = 1, host_pan_calls, host_pan_left, host_pan_right, host_last_pan;
prg32_audio_mode_t prg32_audio_get_mode(void) {
    return host_stereo ? PRG32_AUDIO_MODE_STEREO : PRG32_AUDIO_MODE_MONO;
}
void prg32_audio_set_channel_pan(uint8_t ch, int8_t pan) {
    (void)ch;
    ++host_pan_calls;
    host_last_pan = pan;
    host_pan_left += pan < -8;
    host_pan_right += pan > 8;
}
void prg32_audio_play_track(uint16_t t) { ++host_tracks_started; host_last_track = t; }
void prg32_audio_stop_track(void) { host_last_track = -1; }
int prg32_multiplayer_join(const char *sig, uint32_t flags) {
    (void)flags;
    host_mp_joined = strcmp(sig, G2007_NET_SIGNATURE) == 0;
    return host_mp_joined ? 0 : -1;
}
void prg32_multiplayer_tick(void) {}
int prg32_multiplayer_set_local_state(int16_t x, int16_t y, uint16_t sprite, uint16_t flags) {
    host_local.x = x; host_local.y = y; host_local.sprite = sprite; host_local.flags = flags;
    ++host_local.frame;
    return 0;
}
int prg32_multiplayer_set_input(uint32_t input) { host_local.input = input & 0x7fu; return 0; }
int prg32_multiplayer_get_peer_count(void) { return host_peer_count; }
int prg32_multiplayer_get_peer(int i, prg32_player_state_t *out) {
    if (i < 0 || i >= host_peer_count) return -1;
    *out = host_peers[i];
    return 0;
}

void host_write_ppm(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n320 200\n255\n");
    for (int y = 0; y < 200; ++y)
        for (int x = 0; x < 320; ++x) {
            uint16_t v = host_fb[y][x];
            unsigned char px[3] = {(unsigned char)(((v >> 11) & 31) * 255 / 31),
                                   (unsigned char)(((v >> 5) & 63) * 255 / 63),
                                   (unsigned char)((v & 31) * 255 / 31)};
            fwrite(px, 1, 3, f);
        }
    fclose(f);
}
#endif
