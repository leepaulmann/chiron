// Unit test for the reimplemented probe and energy market costs, on
// made-up data: no game files needed.
#include <stdio.h>
#include <string.h>

#include "../src/engine/faction.h"
#include "../src/engine/game.h"

static int failures;
#define EXPECT(expr, want)                                                    \
    do {                                                                      \
        int got_ = (expr);                                                    \
        if (got_ != (want)) {                                                 \
            printf("FAIL %s = %d, expected %d\n", #expr, got_, (int)(want)); \
            failures++;                                                       \
        }                                                                     \
    } while (0)

static struct faction_data factions[FACTION_COUNT];
static struct base bases[3];
static int32_t base_count = 3, width = 40, half_width = 20, projects[SP_COUNT];
static uint32_t flat = 0;
static uint8_t humans;
static int defenders, encrypted;

static int veh_at_stub(int x, int y) { return 0; }
static int stack_fix_stub(int veh) { return veh; }
static int stack_check_stub(int veh, int type, int c1, int c2, int c3) {
    return type == 6 ? encrypted : c1 == 1 ? defenders : 0;
}

int main(void) {
    game_factions = factions, game_bases = bases, game_base_count = &base_count;
    game_map_width = &width, game_map_half_width = &half_width, game_map_flat = &flat;
    game_human_factions = &humans, game_secret_projects = projects;
    game_veh_at = veh_at_stub, game_stack_fix = stack_fix_stub, game_stack_check = stack_check_stub;
    for (int i = 0; i < SP_COUNT; i++) projects[i] = -1;

    // Faction 2's headquarters at (36, 4), its base at (2, 10): across the
    // seam 6 squares in x, 6 in y.
    bases[0] = (struct base){.x = 36, .y = 4, .faction = 2, .faction_former = 2};
    bases[0].facilities_built[0] = 1 << 1;
    bases[1] = (struct base){.x = 2, .y = 10, .faction = 2, .faction_former = 2, .pop_size = 4};
    bases[2] = (struct base){.x = 10, .y = 10, .faction = 1, .faction_former = 1, .pop_size = 3};
    factions[2].energy_credits = 300;
    factions[1].mind_control_total = 8;
    int dist = 6;  // rings out on the diagonal grid
    defenders = 2, encrypted = 0;
    // (defenders + past control / 4 + size) * ((reserves + 1200) / (distance + 4))
    EXPECT(mind_control(1, 1, 0), (2 + 2 + 4) * ((300 + 1200) / (dist + 4)));
    // Polymorphic Encryption: defenders count again.
    encrypted = 1;
    EXPECT(mind_control(1, 1, 0), (4 + 2 + 4) * ((1500) / (dist + 4)));
    encrypted = 0;
    // No headquarters: distance 12. Its own headquarters is right there:
    // a probe can't, the market pays as for distance 1.
    bases[0].facilities_built[0] = 0;
    EXPECT(mind_control(1, 1, 0), 8 * (1500 / 16));
    bases[0].facilities_built[0] = 1 << 1;
    EXPECT(mind_control(0, 1, 0), -1);
    EXPECT(mind_control(0, 1, 1), (2 + 2 + 0) * (1500 / 5));
    // A Genejack Factory doubles the distance, a creche halves it.
    bases[1].facilities_built[22 / 8] = 1 << (22 % 8);
    EXPECT(mind_control(1, 1, 0), 8 * (1500 / (dist * 2 + 4)));
    bases[1].facilities_built[22 / 8] = 0;
    // A pact doubles a probe's cost and halves the market's; commerce
    // scales the market's; riots halve, a golden age doubles, a grudge
    // adds half, an atrocity doubles.
    int base_cost = 8 * (1500 / (dist + 4));
    factions[1].diplo_status[2] = 0x1;
    EXPECT(mind_control(1, 1, 0), base_cost * 2);
    factions[2].tech_commerce_bonus = 1;
    EXPECT(mind_control(1, 1, 1), base_cost / 2 * 2 / 1);
    factions[1].diplo_status[2] = 0, factions[2].tech_commerce_bonus = 0;
    bases[1].state = 0x2;
    EXPECT(mind_control(1, 1, 0), base_cost / 2);
    bases[1].state = 0x4;
    EXPECT(mind_control(1, 1, 0), base_cost * 2);
    bases[1].state = 0;
    factions[2].diplo_status[1] = 0x20;
    EXPECT(mind_control(1, 1, 0), base_cost + base_cost / 2);
    factions[2].diplo_status[1] = 0x40020;
    EXPECT(mind_control(1, 1, 0), base_cost * 2);
    factions[2].diplo_status[1] = 0;
    // Against a human on a hard level, the AI pays less.
    humans = 1 << 2, factions[2].diff_level = 5;
    EXPECT(mind_control(1, 1, 0), base_cost * 3 / 5);
    humans = 0;

    // The market: the other factions' bases, at least 1000; none from a
    // faction that has surrendered to it in a pact.
    int sum = mind_control(0, 1, 1) + mind_control(1, 1, 1);
    EXPECT(corner_market(1), sum < 1000 ? 1000 : sum);
    factions[2].diplo_status[1] = 0x2000001;
    EXPECT(corner_market(1), 1000);
    factions[2].diplo_status[1] = 0;
    // Bases of its own don't count.
    bases[0].faction = bases[1].faction = 1;
    EXPECT(corner_market(1), 1000);

    if (failures) return 1;
    printf("engine_faction: all tests passed\n");
    return 0;
}
