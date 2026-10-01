/* AI tests: transitions, torch-dependent detection, loss of sight -> SEARCH,
 * and absence of stuck/impossible loops. */
#include "test_util.h"
#include "../src/fixed.c"
#include "../src/world.c"
#include "../src/ai.c"

#define AX(x) ((x) - MAP_ORIGIN_X)
#define AY(y) ((y) - MAP_ORIGIN_Y)

int main(void) {
    world_init();
    AiStimuli none = {0, 0, 0, 0}, hear = {0, 1, 0, 0}, see = {1, 0, 0, 0};
    AiStimuli timer = {0, 0, 1, 0}, see_timer = {1, 0, 1, 0}, arrive = {0, 0, 0, 1};
    CHECK_EQ(ai_think(AI_PATROL, &none), AI_PATROL);
    CHECK_EQ(ai_think(AI_PATROL, &hear), AI_HEAR);
    CHECK_EQ(ai_think(AI_HEAR, &timer), AI_INVESTIGATE);
    CHECK_EQ(ai_think(AI_INVESTIGATE, &arrive), AI_SEARCH);
    CHECK_EQ(ai_think(AI_PATROL, &see), AI_SEE);
    CHECK_EQ(ai_think(AI_SEE, &see_timer), AI_CHASE);
    CHECK_EQ(ai_think(AI_SEE, &timer), AI_SEARCH);
    CHECK_EQ(ai_think(AI_CHASE, &see), AI_CHASE);
    CHECK_EQ(ai_think(AI_CHASE, &timer), AI_SEARCH);   /* lost sight */
    CHECK_EQ(ai_think(AI_SEARCH, &timer), AI_RETURN);
    CHECK_EQ(ai_think(AI_RETURN, &arrive), AI_PATROL);
    /* No impossible loop: with no stimuli, every state reaches PATROL when
     * timers expire and targets are reached. */
    for (uint8_t s = AI_PATROL; s < AI_STATE_COUNT; ++s) {
        uint8_t cur = s;
        AiStimuli idle = {0, 0, 1, 1};
        for (int i = 0; i < 8 && cur != AI_PATROL; ++i) cur = ai_think(cur, &idle);
        CHECK_EQ(cur, AI_PATROL);
        CHECK(ai_state_ticks(s) > 0 || s == AI_PATROL);
    }
    /* Torch changes detection range. */
    world_set_flags(WF_WALL_OPEN);
    LampMan lm;
    ai_reset(&lm);
    lm.body.x = AX(-1500) * 256;
    lm.body.y = AY(6600) * 256;
    lm.body.sector = (int16_t)world_find_sector(-1, AX(-1500), AY(6600));
    lm.state = AI_PATROL;
    AiSense s = {0};
    s.x = AX(-700);
    s.y = AY(6600);
    s.sector = (int16_t)world_find_sector(-1, s.x, s.y);
    s.valid = 1;
    s.light = 0;
    CHECK(!ai_can_see(&lm, &s));
    s.light = 1;
    CHECK(ai_can_see(&lm, &s));
    lm.cooldown = 10;
    CHECK(!ai_can_see(&lm, &s));
    lm.cooldown = 0;
    /* Walls block sight: the shelter is hidden while the 2007 wall is shut. */
    g_world.flags = 0;
    s.x = AX(700);
    s.y = AY(6500);
    s.sector = (int16_t)world_find_sector(-1, s.x, s.y);
    lm.body.x = AX(-100) * 256;
    lm.body.y = AY(6500) * 256;
    lm.body.sector = (int16_t)world_find_sector(-1, AX(-100), AY(6500));
    CHECK(!ai_can_see(&lm, &s));
    /* A chase through open portals closes the distance and catches. */
    world_set_flags(WF_WALL_OPEN);
    lm.state = AI_CHASE;
    lm.timer = ai_state_ticks(AI_CHASE);
    int caught = 0;
    for (int t = 0; t < 600 && !caught; ++t) {
        ai_tick(&lm, &s);
        caught = ai_caught(&lm, &s);
    }
    CHECK(caught);
    /* Loss of sight while chasing leads to SEARCH, then RETURN, PATROL. */
    s.valid = 0;
    lm.state = AI_CHASE;
    lm.timer = 1;
    ai_tick(&lm, &s);
    CHECK_EQ(lm.state, AI_SEARCH);
    for (int t = 0; t < 2000 && lm.state != AI_PATROL; ++t) ai_tick(&lm, &s);
    CHECK_EQ(lm.state, AI_PATROL);
    TEST_MAIN_END("test_ai")
}
