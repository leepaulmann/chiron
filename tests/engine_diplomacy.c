// Unit test for the AI's answers to the player's proposals in the
// diplomacy talks, on made-up data: no game files needed.
#include <stdio.h>
#include <string.h>

#include "../src/engine/diplomacy.h"
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
static struct faction_def defs[FACTION_COUNT];
static struct base bases[4];
static int32_t base_count = 4, turn = 100, net, current = 1, rules = 0x2000000 /* no tech trading */;
static int32_t second, fear, friction, prev_hostility, attack_chance, exposed, satan, can_betray, spoke_turn;
static int32_t friction_faction = -1, friction_with = -1, proposal, counter, proposal_faction, ask_base, bid_base;
static int32_t gender, plural, control_turn, offer_tech[2], offer_value[2], offer_project[2], want_tech[4], want_value[4];
static int32_t trade_project, info_win[0x40];
static uint8_t humans = 1 << 1, alive = 0xfe;
static void *portraits[FACTION_COUNT];

// What the talks showed: each dialog's label, as an address in the image.
static uint32_t shown[16];
static int n_shown, said, answer_with, captions, energy_updates;
static uint32_t at(const char *p) { return (uint32_t)(p - (const char *)game_at(0x400000)) + 0x400000; }
static int x_pops_11_stub(char *label, int flags, void *sprite, void *fn) {
    if (n_shown < 16) shown[n_shown] = label == game_str_buffer ? 1 : at(label);
    n_shown++;
    return answer_with;
}
static int says_stub(int slot, char *text, int g, int p) { return said++, 0; }
static int num_stub(int slot, int value) { return 0; }
static int random_stub(int lo, int hi) { return lo; }
static void caption_stub(int f, int o) { captions++; }
static char *phrase_stub(int f, int unk) { return "phrase"; }
static char *pact_stub(int f) { return "pact"; }
static void __attribute__((thiscall)) update_stub(void *console, int unk) { energy_updates++; }
static void log_stub(char *text, int a, int b, int c) {}
static void tech_stub(int f, int t, int from, int steal) {}
static int __attribute__((thiscall)) add_global_stub(void *d) { return 0; }
static void __attribute__((thiscall)) unlock_stub(void *d) {}
static uint32_t __attribute__((stdcall)) time_stub(void) { return 0; }
static int ambience_stub(int sound) { return 0; }
static int32_t ambience_flags, multi_visible;
// Pacts and treaties.
static int32_t talk_rep, player_pacts, rival_pacts, help_faction, accepted_faction, runaway, strong_pacts, goodwill;
static int32_t captured, can_surrender, alter_fear, enemy_allies, rankings[8], ranked[2], scores[8];
static int pacts, treaties;
static void pact_stub2(int f, int o) { pacts++; }
static void treaty_stub(int f, int o) { treaties++; }
static char *pacts_stub(int f, int o) { return "pacts"; }
// The popups the gifts and joint attacks keep on their stacks: the menu's
// items and its answer.
static int items[16], n_items, menu_answer, popups;
static void __attribute__((thiscall)) pop_stub(void *pop) {}
static int __attribute__((thiscall)) pop_start_stub(void *pop, char *file, char *label, int a, int b, int c, int d) {
    return popups++, 0;
}
static void __attribute__((thiscall)) sprite_box_stub(void *box, void *sprite, int a, int b) {}
static void __attribute__((thiscall)) item_stub(void *dialogs, char *text, int id) {
    if (n_items < 16) items[n_items] = id;
    n_items++;
}
// The menus' answers: menu_answers in turn while menu_count is set.
static int menu_answers[4], menu_count, menu_next;
static int __attribute__((thiscall)) exec_stub(void *pop, int a, int b) {
    if (menu_count) return menu_next < menu_count ? menu_answers[menu_next++] : -1;
    return menu_answer;
}
static int text_open_stub(char *file, char *label) { return 0; }
static void text_get_stub(void) {}
static char line[8] = "line", *line_p = line, *script = "script";
static void parse_string_stub(char *text, char *buf) { strcpy(buf, text); }
static char *hood_stub(int f, int o) { return "hood"; }
static int32_t projects[64], menu_a, menu_b, maps_news, plans_a, plans_b, offered, hostility;
static int maps_traded_calls;
static void net_maps_stub(int f, int o, int unk) { maps_traded_calls++; }
static int plan_stub(int f, int o) { return -1; }
static int from_stub(int base, int f) { return 0; }
static int x_pops_stub(char *label, void *sprite, void *fn) { return 0; }
static int contiguous_stub(int f, int o, int region, int sea) { return 0; }
static char *pacts1_stub(int f) { return "pacts"; }
static int x_pop_stub(char *label, int unk) { return 0; }
static char pop_file[16];

static void reset(void) {
    memset(factions, 0, sizeof factions);
    n_shown = said = answer_with = captions = energy_updates = 0;
    n_items = menu_answer = popups = 0;
    menu_count = menu_next = 0;
    talk_rep = player_pacts = rival_pacts = help_faction = accepted_faction = runaway = strong_pacts = goodwill = 0;
    captured = can_surrender = alter_fear = enemy_allies = pacts = treaties = 0;
    fear = friction = prev_hostility = attack_chance = exposed = satan = can_betray = 0;
    friction_faction = friction_with = -1, spoke_turn = turn - 8, proposal = counter = proposal_faction = 0;
    for (int f = 1; f < FACTION_COUNT; f++) {
        factions[f].ranking = f, factions[f].diff_level = 3, factions[f].base_count = 3;
        for (int o = 1; o < FACTION_COUNT; o++) factions[f].diplo_status[o] = TREATY_COMMLINK;
    }
}

int main(void) {
    static uint8_t image[0x300000];  // strings, from 0x400000
    game_test_image = image;
    strcpy((char *)game_at(0x68dc8c), "PROVOKED");
    strcpy((char *)game_at(0x68dd0c), "PROVOKED");
    game_factions = factions, game_faction_defs = defs, game_bases = bases, game_base_count = &base_count;
    game_turn = &turn, game_net_multiplayer = &net, game_current_faction = &current, game_rules = (uint32_t *)&rules;
    game_human_factions = &humans, game_factions_alive = &alive, game_faction_portraits = portraits;
    game_diplo_second = &second, game_diplo_fear = &fear, game_diplo_friction = &friction;
    game_diplo_prev_hostility = &prev_hostility, game_diplo_attack_chance = &attack_chance;
    game_diplo_exposed_bases = &exposed, game_diplo_great_satan = &satan, game_diplo_can_betray = &can_betray;
    game_diplo_spoke_turn = &spoke_turn, game_diplo_friction_faction = &friction_faction;
    game_diplo_friction_with = &friction_with, game_diplo_proposal = &proposal, game_diplo_counter = &counter;
    game_diplo_proposal_faction = &proposal_faction, game_diplo_ask_base = &ask_base, game_diplo_bid_base = &bid_base;
    game_parse_gender = &gender, game_parse_plural = &plural, game_control_turn_a = &control_turn;
    game_trade_project = &trade_project, game_info_win = info_win;
    for (int i = 0; i < 2; i++)
        game_trade_offer_tech[i] = &offer_tech[i], game_trade_offer_value[i] = &offer_value[i],
        game_trade_offer_project[i] = &offer_project[i];
    for (int i = 0; i < 4; i++) game_trade_want_tech[i] = &want_tech[i], game_trade_want_value[i] = &want_value[i];
    game_x_pops_11 = x_pops_11_stub, game_parse_says = says_stub, game_parse_num = num_stub;
    game_random = random_stub, game_diplomacy_caption = caption_stub, game_get_noun_phrase = phrase_stub;
    game_get_pact = pact_stub, game_console_update_data = update_stub, game_log_say = log_stub;
    game_tech_achieve = tech_stub, game_net_daemon_add_global = add_global_stub, game_net_daemon_unlock = unlock_stub;
    game_time_get_time = time_stub, game_ambience = ambience_stub, game_ambience_flags = &ambience_flags;
    game_multi_win_visible = &multi_visible;
    static char str_buffer[256];
    game_str_buffer = str_buffer;
    game_diplo_reputation = &talk_rep, game_diplo_player_pacts = &player_pacts, game_diplo_rival_pacts = &rival_pacts;
    game_diplo_help_faction = &help_faction, game_diplo_accepted_faction = &accepted_faction;
    game_diplo_runaway_leader = &runaway, game_diplo_rival_strong_pacts = &strong_pacts, game_diplo_goodwill = &goodwill;
    game_diplo_captured_bases = &captured, game_diplo_can_surrender = &can_surrender, game_diplo_alter_fear = &alter_fear;
    game_diplo_enemy_allies = &enemy_allies, game_faction_rankings = rankings, game_ranked_faction = ranked;
    game_ranking_scores = scores, game_pact_of_brotherhood = pact_stub2, game_make_treaty = treaty_stub;
    game_get_phrase = phrase_stub, game_get_pacts_2 = pacts_stub;
    game_popup_ctor = pop_stub, game_popup_close = pop_stub, game_scroll_close = pop_stub;
    game_flat_button_dtor = pop_stub, game_graphic_win_dtor = pop_stub, game_base_pop_dtor = pop_stub;
    game_popup_start = pop_start_stub, game_sprite_box_sprite = sprite_box_stub, game_dialogs_item = item_stub;
    game_base_pop_exec_3 = exec_stub, game_pop_file = pop_file, game_contiguous = contiguous_stub;
    game_get_pacts = pacts1_stub, game_get_him_her = phrase_stub, game_x_pop = x_pop_stub;
    game_x_text_open = text_open_stub, game_text_get = text_get_stub, game_text_line = &line_p;
    game_script_file = &script, game_parse_string = parse_string_stub, game_get_pact_hood = hood_stub;
    for (int i = 0; i < 64; i++) projects[i] = -1;
    game_secret_projects = projects, game_diplo_menu_a = &menu_a, game_diplo_menu_b = &menu_b;
    game_net_maps = net_maps_stub, game_diplo_maps_news = &maps_news;
    game_diplo_plans_faction = &plans_a, game_diplo_plans_other = &plans_b, game_diplo_trade_offered = &offered;
    game_suggest_plan = plan_stub, game_x_pops = x_pops_stub, game_attack_from = from_stub, game_diplo_hostility = &hostility;

    // An AI set on betrayal declares a vendetta at once.
    reset();
    can_betray = 1;
    threaten(1, 2);
    EXPECT(factions[2].diplo_status[1] & TREATY_VENDETTA, TREATY_VENDETTA);
    EXPECT(factions[1].diplo_spoke[2], turn);
    EXPECT(n_shown, 1);
    EXPECT(shown[0], 1);  // PROVOKED0, put together in the string buffer
    EXPECT(second, 2);

    // No fear: friction 2, and the threat is ignored.
    reset();
    proposal = 6;
    friction_faction = 2, friction_with = 1;
    threaten(1, 2);
    EXPECT(factions[2].diplo_friction[1], 2);
    EXPECT(friction, 2);
    EXPECT(n_shown, 1);
    EXPECT(shown[0], 0x68dd18);
    EXPECT(factions[2].diplo_patience[1], 3);  // 3, less (friction 0 + 3) / 8

    // Fear 2, eight turns since they spoke: 25 * 2 * 8 / 16 = 25 energy,
    // and a truce.
    reset();
    proposal = 6, fear = 2;
    factions[2].energy_credits = 100;
    threaten(1, 2);
    EXPECT(factions[1].energy_credits, 25);
    EXPECT(factions[2].energy_credits, 75);
    EXPECT(shown[0], 0x68dcbc);
    EXPECT(energy_updates, 2);
    EXPECT(factions[2].diplo_status[1] & 0x4, 0x4);
    EXPECT(factions[1].diplo_status[2] & 0x4, 0x4);
    // At most the AI's reserves, in 25s.
    reset();
    proposal = 6, fear = 9;
    factions[2].energy_credits = 99;
    threaten(1, 2);
    EXPECT(factions[1].energy_credits, 75);

    // In a pact, a player ranked 3 against an AI ranked 6: fear from the
    // standing, 6 - 3 = 3 (the techs and the rest even), plus wealth.
    reset();
    proposal = 6;
    factions[1].diplo_status[2] |= TREATY_PACT, factions[2].diplo_status[1] |= TREATY_PACT;
    factions[1].ranking = 3, factions[2].ranking = 6, factions[2].ai_wealth = 1;
    factions[2].energy_credits = 1000;
    threaten(1, 2);
    EXPECT(fear, 4);
    EXPECT(factions[1].energy_credits, 50);  // 25 * 4 * 8 / 16
    EXPECT(factions[2].diplo_patience[1], 6);

    // Outside a pact, high friction against a weaker player: a vendetta.
    reset();
    friction = 16;
    factions[2].mil_strength = 100, factions[1].mil_strength = 50;
    threaten(1, 2);
    EXPECT(factions[1].diplo_status[2] & TREATY_VENDETTA, TREATY_VENDETTA);
    EXPECT(shown[0], 1);
    // But not against a stronger one: (3 / 2 + 0 + 4) * 80 / 4 = 100.
    reset();
    friction = 16;
    factions[2].mil_strength = 100, factions[1].mil_strength = 80;
    threaten(1, 2);
    EXPECT(factions[1].diplo_status[2] & TREATY_VENDETTA, 0);
    EXPECT(shown[0], 0x68dd18);

    // In a pact with nothing to fear, the AI turns the player away.
    reset();
    factions[2].diplo_status[1] |= TREATY_PACT;
    threaten(1, 2);
    EXPECT(n_shown, 1);
    EXPECT(shown[0], 0x68dd04);

    // A joint attack on a faction the AI is already at war with.
    reset();
    proposal = 8, proposal_faction = 3;
    factions[2].diplo_status[3] |= TREATY_VENDETTA;
    threaten(1, 2);
    EXPECT(shown[0], 0x68dc98);
    EXPECT(n_shown, 1);

    // ---- energy_trade
    for (int f = 0; f < FACTION_COUNT; f++) snprintf(defs[f].filename, sizeof defs[f].filename, "F%d", f);
    proposal = 6;
    // A loan, with a treaty, from an AI with as much commerce: a third of
    // its 300, at 50% over 100 turns at most: 150 owed, 2 a turn.
    reset();
    proposal = 6, counter = 6, answer_with = 1;
    factions[2].diplo_status[1] |= TREATY_TREATY, factions[1].diplo_status[2] |= TREATY_TREATY;
    factions[2].energy_credits = 300, factions[1].energy_credits = 50;
    EXPECT(energy_trade(1, 2), 1);
    EXPECT(factions[1].loan_balance[2], 200);  // 2 a turn for 100 turns, rounded up
    EXPECT(factions[1].loan_payment[2], 2);
    EXPECT(factions[1].energy_credits, 150);
    EXPECT(factions[2].energy_credits, 200);
    // Declined, nothing changes hands.
    reset();
    proposal = 6, counter = 6;
    factions[2].diplo_status[1] |= TREATY_TREATY;
    factions[2].energy_credits = 300;
    EXPECT(energy_trade(1, 2), 0);
    EXPECT(factions[1].loan_balance[2], 0);
    // Without a treaty it refuses (a line picked at random).
    reset();
    proposal = 6, counter = 1;
    factions[2].energy_credits = 300;
    EXPECT(energy_trade(1, 2), 0);
    EXPECT(n_shown, 1);
    EXPECT(shown[0], 1);
    EXPECT(factions[2].energy_credits, 300);
    // A rich partner far ahead gives energy for friendship: the original
    // gives all of its reserves when they exceed the eighth it reckons.
    reset();
    proposal = 6, counter = 1;
    factions[1].diplo_status[2] |= TREATY_PACT, factions[2].diplo_status[1] |= TREATY_PACT;
    factions[1].ranking = 2, factions[2].ranking = 5, factions[2].tech_ranking = 10;
    factions[1].energy_credits = 100, factions[2].energy_credits = 1000;
    EXPECT(energy_trade(1, 2), 0);
    EXPECT(factions[1].energy_credits, 1100);
    EXPECT(factions[2].energy_credits, 0);
    EXPECT(factions[2].diplo_patience[1], 2);

    // ---- propose_pact
    proposal = 2;
    // A threat from a player that beat the AI: it surrenders.
    reset();
    proposal = 2, counter = 3, can_surrender = 1;
    EXPECT(propose_pact(1, 2), 1);
    EXPECT(factions[2].diplo_status[1] & 0x2000000, 0x2000000);
    EXPECT(pacts, 1);
    // A threat it shrugs off (an easy player is always heard, so a hard
    // one): 0, so that the threat follows.
    reset();
    proposal = 2, counter = 3, goodwill = -10;
    EXPECT(propose_pact(1, 2), 0);
    EXPECT(pacts, 0);
    EXPECT(n_shown, 0);
    // An easy player, with nothing asked: the pact.
    reset();
    proposal = 2, counter = 1;
    factions[1].diff_level = 0;
    EXPECT(propose_pact(1, 2), 1);
    EXPECT(pacts, 1);
    // Unless the AI wants revenge: refused, with its patience up.
    reset();
    proposal = 2, counter = 1;
    factions[1].diff_level = 0, talk_rep = 3, factions[2].diplo_status[1] |= TREATY_WANT_REVENGE;
    EXPECT(propose_pact(1, 2), 1);
    EXPECT(pacts, 0);
    EXPECT(factions[2].diplo_patience[1], 1);
    EXPECT(shown[0], 0x68d8f0);
    // Energy asked, for an energy payment: the player's 400 in the share
    // of strength the AI holds (100 of 201), in 50s: 150; paid.
    reset();
    proposal = 2, counter = 5, answer_with = 1;
    factions[1].diff_level = 0, factions[1].energy_credits = 400;
    factions[1].mil_strength = 100, factions[2].mil_strength = 100;
    EXPECT(propose_pact(1, 2), 1);
    EXPECT(factions[1].energy_credits, 250);
    EXPECT(factions[2].energy_credits, 150);
    EXPECT(pacts, 1);

    // ---- propose_treaty
    proposal = 3;
    reset();
    proposal = 3, counter = 1;
    EXPECT(propose_treaty(1, 2), 1);
    EXPECT(treaties, 1);
    // Too much friction: refused.
    reset();
    proposal = 3, counter = 1, friction = 30;
    EXPECT(propose_treaty(1, 2), 1);
    EXPECT(treaties, 0);
    EXPECT(shown[0], 0x68d9d0);
    // But a threat after it: 0.
    reset();
    proposal = 3, counter = 3, friction = 30;
    EXPECT(propose_treaty(1, 2), 0);
    // An AI that cooperates with humans agrees anyway (asking nothing of a
    // threat; with friendship offered, the friction would make it ask for
    // energy the player hasn't got, and that would be the end of it).
    reset();
    proposal = 3, counter = 3, friction = 30;
    factions[2].flags |= 0x400000;
    EXPECT(propose_treaty(1, 2), 1);
    EXPECT(treaties, 1);

    // ---- make_gift
    proposal = 1;
    // Energy: the menu lists the player's 400 and its halves in 25s, down
    // to nothing. 200 given to an AI with 300, from a player with 400, on
    // turn 100: a large gift. Friction falls by 4 (40 in fifths: 10 at a
    // half, 20 at a third, 10 at a quarter: 13; then 17 / -5 * 3 / 2),
    // grudges go, the gift counts twice, and patience falls by 3.
    reset();
    counter = 5, menu_answer = 200;
    factions[1].energy_credits = 400, factions[2].energy_credits = 300;
    factions[2].diplo_friction[1] = 10, factions[1].integrity_blemishes = 3;
    factions[1].diplo_status[2] |= TREATY_WANT_REVENGE;
    make_gift(1, 2);
    EXPECT(n_items, 6);
    EXPECT(items[0], 400);
    EXPECT(items[2], 100);
    EXPECT(items[4], 25);
    EXPECT(items[5], 0);
    EXPECT(factions[1].energy_credits, 200);
    EXPECT(factions[2].energy_credits, 500);
    EXPECT(factions[2].diplo_friction[1], 6);
    EXPECT(factions[1].diplo_status[2] & TREATY_WANT_REVENGE, 0);
    EXPECT(factions[1].diplo_gifts[2], 2);
    EXPECT(factions[2].diplo_patience[1], -3);
    EXPECT(n_shown, 1);
    EXPECT(shown[0], 1);
    // 50 is a fair gift only to an AI with 200 or less: here not, so it
    // isn't counted; patience falls by 2.
    reset();
    counter = 5, menu_answer = 50, turn = 40;
    factions[1].energy_credits = 300, factions[2].energy_credits = 300;
    factions[1].integrity_blemishes = 3;
    make_gift(1, 2);
    EXPECT(factions[1].energy_credits, 250);
    EXPECT(factions[1].diplo_gifts[2], 0);
    EXPECT(factions[2].diplo_patience[1], -2);
    // Nothing picked: nothing given, nothing said.
    reset();
    counter = 5, menu_answer = 0;
    factions[1].energy_credits = 300;
    make_gift(1, 2);
    EXPECT(factions[1].energy_credits, 300);
    EXPECT(n_shown, 0);
    turn = 100;

    // ---- propose_attack
    proposal = 8;
    // An AI at war with the target already says so.
    reset();
    counter = 1, proposal_faction = 3;
    factions[2].diplo_status[3] |= TREATY_VENDETTA;
    EXPECT(propose_attack(1, 2), 1);
    EXPECT(shown[0], 0x68da74);
    // One out of touch with it (no commlink, not neighbours) and not in a
    // pact with the player refuses.
    reset();
    counter = 1, proposal_faction = 3;
    factions[2].diplo_status[3] = 0;
    EXPECT(propose_attack(1, 2), 1);
    EXPECT(shown[0], 0x68da84);
    // Energy: the target's strength 100, five times, by the AI's will to
    // fight (2), half again for a target that is no enemy of the player
    // nor human: 1500. Paid, the AI declares a vendetta on the target.
    reset();
    counter = 5, proposal_faction = 3, menu_answer = 1;
    for (int i = 0; i < 4; i++) want_tech[i] = -1;
    factions[3].mil_strength = 100;
    factions[1].energy_credits = 2000;
    EXPECT(propose_attack(1, 2), 1);
    EXPECT(factions[1].energy_credits, 500);
    EXPECT(factions[2].energy_credits, 1500);
    EXPECT(factions[2].diplo_status[3] & TREATY_VENDETTA, TREATY_VENDETTA);
    EXPECT(factions[2].diplo_status[3] & TREATY_UNK_40, TREATY_UNK_40);
    EXPECT(factions[2].diplo_status[1] & TREATY_UNK_80, TREATY_UNK_80);
    EXPECT(popups, 2);  // the price, and the menu to pay it
    // A player who can't pay isn't offered the menu.
    reset();
    counter = 5, proposal_faction = 3;
    factions[3].mil_strength = 100;
    factions[1].energy_credits = 1000;
    EXPECT(propose_attack(1, 2), 1);
    EXPECT(popups, 1);
    EXPECT(factions[2].diplo_status[3] & TREATY_VENDETTA, 0);
    // A threat from a player the AI has nothing against, which it
    // wouldn't fight anyway: turned away without a word.
    reset();
    counter = 3, proposal_faction = 3;
    EXPECT(propose_attack(1, 2), 0);
    EXPECT(n_shown, 0);
    // One that surrendered to the player joins at once.
    reset();
    counter = 3, proposal_faction = 3;
    factions[1].diplo_status[2] |= TREATY_PACT, factions[2].diplo_status[1] |= TREATY_PACT | 0x2000000;
    EXPECT(propose_attack(1, 2), 1);
    EXPECT(factions[2].diplo_status[3] & TREATY_VENDETTA, TREATY_VENDETTA);

    // ---- buy_tech
    // The world map (valued 2) for 13 eight-turn spans: 26, in 25s rounded
    // up, at least 100. A player with 50 can't pay for research, so no
    // price is named; nor for friendship, nor with nothing to sell.
    static uint8_t achieved[10000];
    game_tech_achieved = achieved;
    reset();
    offer_tech[0] = 9999, want_tech[0] = -1;
    factions[1].energy_credits = 50;
    EXPECT(buy_tech(1, 2, 4, 0, 4), 0);
    EXPECT(buy_tech(1, 2, 1, 0, 4), 0);
    offer_tech[0] = -1;
    EXPECT(buy_tech(1, 2, 5, 0, 4), 0);
    EXPECT(n_shown, 0);
    EXPECT(second, 2);

    // ---- trade_maps
    // Each learns what the other has seen, with what the other last saw
    // there; news for the first, who learned something.
    {
        static struct map_tile tiles[3];
        static struct map_tile *map = tiles;
        static int32_t area = 3, news;
        game_map = &map, game_map_area = &area, game_diplo_maps_news = &news;
        reset();
        current = 3;
        tiles[0].visibility = 1 << 1, tiles[0].bits = 0xf0, tiles[0].bits_seen[0] = 0x3c;
        tiles[1].visibility = 1 << 2;
        trade_maps(1, 2);
        EXPECT(tiles[0].visibility, 6);
        EXPECT(tiles[0].bits_seen[1], 0x30);
        EXPECT(tiles[1].visibility, 6);
        EXPECT(tiles[2].visibility, 0);
        EXPECT(news, 1);
        EXPECT(factions[1].maps_traded, 1 << 2);
        EXPECT(factions[2].maps_traded, 6);
        current = 1;
    }

    // ---- proposal_menu
    // Energy asked for, offering energy: the counter.
    reset();
    menu_count = 2, menu_answers[0] = 6, menu_answers[1] = 5;
    EXPECT(proposal_menu(1, 2), 6);
    EXPECT(counter, 5);
    EXPECT(proposal, 6);
    // Closing the talks asks for no counter: friendship.
    reset();
    menu_count = 1, menu_answers[0] = 10;
    EXPECT(proposal_menu(1, 2), 10);
    EXPECT(counter, 1);
    // A gift with nothing offered after all: nothing.
    reset();
    menu_count = 2, menu_answers[0] = 1, menu_answers[1] = 0;
    EXPECT(proposal_menu(1, 2), 0);
    EXPECT(proposal, 0);
    // A commlink with a faction the AI has no commlink with either: it says
    // so, and nothing is proposed.
    reset();
    factions[1].diplo_status[3] = 0, factions[2].diplo_status[3] = 0;
    menu_count = 2, menu_answers[0] = 13, menu_answers[1] = 3;
    EXPECT(proposal_menu(1, 2), 0);
    EXPECT(proposal_faction, 3);
    EXPECT(counter, 1);

    // ---- make_a_proposal
    // Maps, without friction: traded, the AI's patience up 2.
    reset();
    proposal = 7, counter = 1, maps_traded_calls = 0;
    game_diplo_maps_news = &maps_news, maps_news = 0;
    make_a_proposal(1, 2, 1);
    EXPECT(maps_traded_calls, 1);
    EXPECT(maps_news, 1);
    EXPECT(factions[2].diplo_patience[1], 2);
    EXPECT(shown[0], 0x68da68);
    EXPECT(menu_b, 1);
    // With friction, refused.
    reset();
    proposal = 7, counter = 1, friction = 5, maps_traded_calls = 0;
    make_a_proposal(1, 2, 1);
    EXPECT(maps_traded_calls, 0);
    EXPECT(shown[0], 0x68da60);
    // A loan repaid.
    reset();
    proposal = 11, counter = 1;
    factions[1].loan_balance[2] = 60, factions[1].loan_payment[2] = 3, factions[1].energy_credits = 100;
    make_a_proposal(1, 2, 1);
    EXPECT(factions[1].energy_credits, 40);
    EXPECT(factions[2].energy_credits, 60);
    EXPECT(factions[1].loan_balance[2], 0);
    EXPECT(factions[1].loan_payment[2], 0);

    // ---- battle_plans
    // In a pact, without targets: the AI says so.
    reset();
    factions[1].diplo_status[2] |= TREATY_PACT, factions[2].diplo_status[1] |= TREATY_PACT;
    battle_plans(1, 2);
    EXPECT(n_shown, 1);
    EXPECT(shown[0], 0x68dd44);
    EXPECT(plans_a, 1);
    EXPECT(plans_b, 2);
    // Outside a pact, with friction 18: refused at once.
    reset();
    friction = 18;
    battle_plans(1, 2);
    EXPECT(shown[0], 0x68dd20);

    // ---- tech_trade (the rules here forbid trading techs: none to trade)
    {
        static struct rules_reactor reactors[4];
        static int32_t refused;
        for (int i = 0; i < 4; i++) reactors[i].preq_tech = -1;
        game_reactors = reactors, game_diplo_trade_refused = &refused;
        // Friction of 18 outside a pact: hatred, said only to the player's
        // proposal.
        reset();
        friction = 18, refused = 1;
        EXPECT(tech_trade(1, 2, 0, 1, 4), 0);
        EXPECT(n_shown, 1);
        EXPECT(shown[0], 1);
        EXPECT(refused, 0);
        reset();
        friction = 18;
        EXPECT(tech_trade(1, 2, 1, 4, 4), 0);
        EXPECT(n_shown, 0);
        // Without friction, the AI wants the player's map (it has seen
        // none of the player's bases), but nothing is offered for
        // friendship: later.
        reset();
        offered = 0;
        for (int i = 0; i < 4; i++) want_tech[i] = -1;
        EXPECT(tech_trade(1, 2, 0, 1, 4), 0);
        EXPECT(want_tech[0], 9999);
        EXPECT(n_shown, 1);
    }

    if (failures) printf("%d failures\n", failures);
    else printf("ok\n");
    return failures != 0;
}
