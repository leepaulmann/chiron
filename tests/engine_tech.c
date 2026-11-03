// Unit test for the reimplemented technology queries, on a small made-up
// tech tree: no game files needed.
#include <stdio.h>
#include <string.h>

#include "../src/engine/game.h"
#include "../src/engine/tech.h"

static struct rules_tech tree[TECH_COUNT];
static uint8_t achieved[TECH_COUNT];
static int32_t expansion;
static int failures;

#define EXPECT(expr, want)                                                       \
    do {                                                                         \
        int got_ = (expr);                                                       \
        if (got_ != (want)) {                                                    \
            printf("FAIL %s = %d, expected %d\n", #expr, got_, (int)(want));    \
            failures++;                                                          \
        }                                                                        \
    } while (0)

static void set(int id, int preq1, int preq2, int growth, int tech, int wealth, int power) {
    tree[id] = (struct rules_tech){.growth = growth, .tech = tech, .wealth = wealth, .power = power,
                                   .preq1 = preq1, .preq2 = preq2};
}

int main(void) {
    for (int i = 0; i < TECH_COUNT; i++) set(i, TECH_NONE, TECH_NONE, 0, 0, 0, 0);
    set(0, TECH_NONE, TECH_NONE, 1, 2, 3, 4);  // power wins
    set(1, 0, TECH_NONE, 2, 2, 2, 2);          // all tied: growth
    set(2, 0, 1, 0, 3, 3, 0);                  // tech and wealth tied: tech
    set(3, TECH_DISABLED, TECH_NONE, 0, 0, 0, 0);
    set(4, 1, TECH_DISABLED, 0, 0, 0, 0);       // second prerequisite disabled
    set(5, TECH_NONE, TECH_DISABLED, 0, 0, 0, 0);  // ...but the first is "none"
    set(TECH_TRANSCEND, 2, TECH_NONE, 0, 0, 0, 0);
    achieved[0] = 1 << 1 | 1 << 2;
    achieved[1] = achieved[3] = achieved[4] = achieved[5] = achieved[TECH_TRANSCEND] = 1 << 1;
    game_tech = tree;
    game_tech_achieved = achieved;
    game_expansion = &expansion;

    EXPECT(has_tech(0, 1), 1);
    EXPECT(has_tech(0, 3), 0);
    EXPECT(has_tech(0, 0), 0);  // natives have nothing
    EXPECT(has_tech(TECH_NONE, 1), 1);
    EXPECT(has_tech(TECH_NONE, 0), 0);
    EXPECT(has_tech(TECH_DISABLED, 1), 0);
    EXPECT(has_tech(3, 1), 0);
    EXPECT(has_tech(4, 1), 0);
    EXPECT(has_tech(5, 1), 1);
    EXPECT(has_tech(TECH_TRANSCEND, 1), 0);
    EXPECT(has_tech(TECH_COUNT, 1), 0);
    EXPECT(has_tech(0, 33), 1);  // shift count wraps, as on x86

    EXPECT(tech_recurse(0, 0), 1);
    EXPECT(tech_recurse(2, 0), 3);  // 2 <- 1 <- 0
    EXPECT(tech_recurse(TECH_NONE, 5), 5);

    EXPECT(tech_category(0), 3);
    EXPECT(tech_category(1), 0);
    EXPECT(tech_category(2), 1);

    EXPECT(tech_avail(1, 2), 1);
    EXPECT(tech_avail(1, 1), 0);  // already had
    EXPECT(tech_avail(2, 1), 1);
    EXPECT(tech_avail(2, 2), 0);  // lacks tech 1
    EXPECT(tech_avail(5, 2), 0);  // a disabled prerequisite
    EXPECT(tech_avail(TECH_FIRST_SMACX, 2), 0);
    expansion = 1;
    EXPECT(tech_avail(TECH_FIRST_SMACX, 2), 1);
    EXPECT(tech_avail(TECH_COUNT, 1), 0);
    EXPECT(tech_avail((uint32_t)-1, 1), 0);

    EXPECT(tech_is_preq(0, 2, 1), 1);
    EXPECT(tech_is_preq(0, 2, 0), 0);
    EXPECT(tech_is_preq(2, 2, 0), 1);
    EXPECT(tech_is_preq(0, TECH_TRANSCEND, 2), 1);
    EXPECT(tech_is_preq(1, 0, 5), 0);
    EXPECT(tech_is_preq(TECH_NONE, 2, 3), 0);

    EXPECT(tech_mil(0), 4);
    EXPECT(tech_mil(TECH_COUNT), 0);
    EXPECT(tech_tech(0), 2);
    EXPECT(tech_tech(TECH_COUNT), 1);
    EXPECT(tech_infra(0), 3);
    EXPECT(tech_infra(TECH_COUNT + 5), 1);
    EXPECT(tech_colonize(0), 1);
    EXPECT(tech_colonize(TECH_COUNT), 1);

    if (failures) return 1;
    puts("PASS");
    return 0;
}
