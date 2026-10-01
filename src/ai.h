/*
 * L'Uomo della Lampada ("LampMan"): the fictional human antagonist.
 *
 * A finite-state machine driven by two stimuli - sight (strongly affected by
 * the player's torch) and noise. ai_think() is a pure transition function so
 * that the state graph can be unit-tested; ai_tick() adds perception and
 * movement through the sector graph. There is no combat: being caught only
 * sends the player back to the last safe checkpoint.
 */
#ifndef G2007_AI_H
#define G2007_AI_H

#include <stdint.h>
#include "world.h"

enum {
    AI_INACTIVE = 0,
    AI_SCRIPTED,   /* distant sighting in the vehicle hall (beat 3)   */
    AI_PATROL,
    AI_HEAR,       /* brief pause, turning towards a noise            */
    AI_INVESTIGATE,
    AI_SEE,        /* reaction time before the chase                  */
    AI_CHASE,
    AI_SEARCH,
    AI_RETURN,
    AI_STATE_COUNT
};

/* Stimuli for one decision. */
typedef struct {
    uint8_t sees;        /* player visible this tick                     */
    uint8_t hears;       /* new noise event this tick                    */
    uint8_t timer_done;  /* state timer expired                          */
    uint8_t at_target;   /* reached the current movement target          */
} AiStimuli;

typedef struct {
    int32_t x, y;        /* player position (cm)                         */
    int16_t sector;
    uint8_t light;       /* torch on                                     */
    uint8_t valid;       /* player present and not protected             */
    int32_t noise_x, noise_y;
    int16_t noise_sector;
    uint8_t noise;       /* a noise event happened this tick             */
} AiSense;

typedef struct {
    Body body;
    uint8_t state;
    uint8_t waypoint;
    uint16_t timer;      /* ticks left in the current state              */
    uint16_t cooldown;   /* ticks of blindness after a capture           */
    int32_t tx, ty;      /* movement target (cm)                         */
    int16_t tsector;
    uint8_t moving;
    uint16_t seed;       /* deterministic LCG for search wandering       */
} LampMan;

#define AI_SIGHT_LIGHT_CM 1800
#define AI_SIGHT_DARK_CM 420
#define AI_SIGHT_CLOSE_CM 140
#define AI_CATCH_CM 70

uint8_t ai_think(uint8_t state, const AiStimuli *st);
uint16_t ai_state_ticks(uint8_t state);
int ai_can_see(const LampMan *lm, const AiSense *sense);
void ai_reset(LampMan *lm);
void ai_place(LampMan *lm, int entity, uint8_t state);
void ai_tick(LampMan *lm, const AiSense *sense);
int ai_caught(const LampMan *lm, const AiSense *sense);
int ai_is_threatening(const LampMan *lm);

#endif
