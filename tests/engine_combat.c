// Unit test for the reimplemented combat and probe team helpers, on a
// small made-up map: no game files needed.
#include <stdio.h>
#include <string.h>

#include "../src/engine/combat.h"
#include "../src/engine/game.h"
#include "../src/engine/veh.h"

enum { W = 16, H = 12 };

static struct map_tile tiles[W / 2 * H], *tiles_ptr = tiles;
static int32_t width = W, height = H, half_width = W / 2, base_count = 3, veh_count, turn = 100, current = 7;
static int32_t find_dist, net, prefs, gender, plural, algo, target, hsa, lift[4], refs[5], *net_ref, battle;
static uint32_t flat, state;
static uint8_t humans, alive = 0xfe;
static struct base bases[3];
static struct veh veh_slots[1 + VEH_COUNT + 1], *vehs = veh_slots + 1;  // one before, for unit -1
static struct faction_data factions[FACTION_COUNT];
static struct faction_def defs[FACTION_COUNT];
static struct unit_proto protos[PROTO_COUNT];
static struct rules_chassis chassis[CHASSIS_COUNT];
static struct rules_weapon weapons[WEAPON_COUNT];
static int32_t radius_x[RADIUS_SQUARES], radius_y[RADIUS_SQUARES], radius_count[9];
static char battle_text[256];
static int failures;

#define EXPECT(expr, want)                                                       \
    do {                                                                         \
        int got_ = (expr);                                                       \
        if (got_ != (want)) {                                                    \
            printf("FAIL %s = %d, expected %d\n", #expr, got_, (int)(want));    \
            failures++;                                                          \
        }                                                                        \
    } while (0)

static struct map_tile *at(int x, int y) { return &tiles[x / 2 + y * (W / 2)]; }

static char said[10][64];
static int popps, last_popp, crossed, picked = -1, achieved_tech = -2, rand_value, popp_answer;
static int parse_says_stub(int slot, char *text, int g, int p) {
    snprintf(said[slot], sizeof said[slot], "%s", text);
    return 0;
}
static int parse_num_stub(int slot, int value) { return 0; }
static int popp_stub(char *file, char *label, int flags, char *picture, int unk) {
    popps++;
    last_popp = (int)(label - (char *)game_at(0x400000)) + 0x400000;
    return popp_answer;
}
static void double_cross_stub(int f, int o, int u) { crossed++; }
static int32_t ambience_flags;
static int ambience_stub(int sound) { return 0; }
static int tech_pick_stub(int f, int u, int o, char *label) { return picked; }
static void tech_achieve_stub(int f, int t, int from, int steal) { achieved_tech = t; }
static void __attribute__((thiscall)) netmsg_stub(void *self, char *label, int d, int u, char *s) {}
static int rand_stub(void) { return rand_value; }
static void log_say_stub(char *t, int a, int b, int c) {}
static void __attribute__((thiscall)) deletion_stub(void *l, int v) {}
static void __attribute__((thiscall)) check_base_stub(void *w, int b) {}
static void __attribute__((thiscall)) check_loc_stub(void *w, int x, int y, int u) {}

// ---- The battle ---------------------------------------------------------------

static struct rules_armor armor[ARMOR_COUNT];
static struct rules_reactor reactors[REACTOR_COUNT];
static struct rules_ability abilities[32];
static struct rules_basic rules_basic;
static struct rules_tech techs[TECH_COUNT];
static uint8_t achieved[TECH_COUNT];
static int32_t near_x[9], near_y[9], projects[SP_COUNT], expansion = 1, more_prefs, attack_flags, draw_att, draw_def;
static int32_t combat_win[FACTION_COUNT], combat_loss[FACTION_COUNT], zoomed, status_battle, mod_counts[4], mods[8];
static int32_t morales[2], bits_quiet, pbem, proposal_passes[11], rebuilt, multi_visible;
static uint32_t rules, draw_state;
static char mod_labels[8][80], *terrain, *label_list[1200], **labels_ptr = label_list;
static uint8_t window[64];  // stands in for the game's window objects
static int rolls[64], roll_n, roll_i, kept_treaty, booms, monuments;
static int roll_stub(void) { return roll_i < roll_n ? rolls[roll_i++] : 0; }
static char *__attribute__((thiscall)) strings_get_stub(void *t, char *s) { return s; }
static int16_t __attribute__((stdcall)) key_stub(int key) { return 0; }
static void __attribute__((thiscall)) win_stub(void *w) {}
static void __attribute__((thiscall)) status_draw_stub(void *w, int a, int b, int c, int d, int e) {}
static void __attribute__((thiscall)) console_update_stub(void *c, int u) {}
static int __attribute__((thiscall)) focus_stub(void *c, int x, int y, int f) { return 1; }
static int break_treaty_stub(int f, int o, uint32_t t) { return kept_treaty; }
static void mon_stub(int f, int o) { monuments++; }
static void boom_stub(int x, int y, int f) { booms++; }
static void tile_stub(int x, int y, int u) {}
static void fixup_stub(int x, int y, int a, int b) {}
static void fixup2_stub(int x1, int y1, int x2, int y2, int a, int b) {}
static void void_stub(void) {}
static int tut_stub(int b) { return 0; }
static void synch_stub(int x, int y, int f) {}
static void scoot_stub(int v, int x, int y, int d, int u) {}
static void wait_stub(int ms) {}
static void interlude_stub(int id, char *t, int a, int b) {}
static int pop2_stub(char *l, char *p, int b) { return 0; }
static int parse_say_stub(int slot, int text, int g, int p) { return 0; }
static void synch_veh_stub(int v) {}

// Puts unit v alone at (x, y): faction f, design p.
static void place(int v, int x, int y, int f, int p) {
    vehs[v] = (struct veh){.x = (int16_t)x, .y = (int16_t)y, .proto = (int16_t)p, .faction = (uint8_t)f, .home_base = -1,
                           .next_in_stack = -1, .prev_in_stack = -1, .morale = 2};
    at(x, y)->bits |= MAP_UNIT;
}
// The attacker's rolls high (it wins every round) or low.
static void rolls_for(int attacker_wins, int n) {
    roll_n = 2 * n, roll_i = 0;
    for (int i = 0; i < n; i++) rolls[2 * i] = attacker_wins ? 999 : 0, rolls[2 * i + 1] = attacker_wins ? 0 : 999;
}

static void test_battles(void) {
    game_armor = armor, game_reactors = reactors, game_abilities = abilities, game_rules_basic = &rules_basic;
    game_tech = techs, game_tech_achieved = achieved, game_expansion = &expansion, game_secret_projects = projects;
    game_radius_near_x = near_x, game_radius_near_y = near_y, game_rules = &rules, game_more_preferences = &more_prefs;
    game_strings_get = strings_get_stub, game_labels = &labels_ptr, game_battle_mod_count = mod_counts;
    game_battle_mod = mods, game_battle_mod_label = mod_labels, game_battle_morale = morales;
    game_battle_terrain = &terrain, game_get_async_key_state = key_stub, game_veh_attack_flags = &attack_flags;
    game_veh_draw_att = &draw_att, game_veh_draw_def = &draw_def, game_faction_combat_win = combat_win;
    game_faction_combat_loss = combat_loss, game_base_win_zoomed = &zoomed, game_status_win_battle = &status_battle;
    game_battle_win = game_status_win = game_tut_win = game_main_interface = game_console = window;
    game_status_win_draw = status_draw_stub, game_status_win_reset = win_stub, game_status_win_redraw = win_stub;
    game_set_iface_mode = win_stub, game_battle_win_clear = win_stub, game_battle_win_stop_timer = win_stub;
    game_battle_win_pulse_timer = win_stub, game_base_win_focus = win_stub, game_graphic_win_redraw = win_stub;
    game_console_update_data = console_update_stub, game_console_focus = focus_stub, game_break_treaty = break_treaty_stub;
    game_mon_enemy_destroyed = mon_stub, game_boom = boom_stub, game_draw_tile = tile_stub;
    game_draw_tile_fixup = fixup_stub, game_draw_tile_fixup_2 = fixup2_stub, game_flush_input = void_stub;
    game_do_all_draws = void_stub, game_tut_check2 = tut_stub, game_base_bits_quiet = &bits_quiet, game_pbem = &pbem;
    game_proposal_passes = proposal_passes, game_draw_state = &draw_state, game_synch_bit = synch_stub;
    game_veh_at_rebuilt = &rebuilt, game_rand = roll_stub;
    game_multi_win_visible = &multi_visible, game_veh_scoot = scoot_stub, game_clock_wait = wait_stub;
    game_interlude = interlude_stub, game_pop2 = pop2_stub, game_parse_say = parse_say_stub, game_synch_veh = synch_veh_stub;
    for (int i = 0; i < 1200; i++) label_list[i] = "Label";
    for (int i = 0; i < 8; i++) near_x[i] = radius_x[i + 1], near_y[i] = radius_y[i + 1];
    for (int i = 0; i < SP_COUNT; i++) projects[i] = -1;
    for (int i = 0; i < TECH_COUNT; i++) techs[i].preq1 = techs[i].preq2 = TECH_NONE;
    rules_basic.move_rate_roads = 3;
    rules_basic.artillery_damage_num = 3, rules_basic.artillery_damage_den = 2;
    rules_basic.max_dmg_arty_base_bunker = 50, rules_basic.max_dmg_arty_open = 99, rules_basic.max_dmg_arty_sea = 100;
    chassis[0].speed = 1, chassis[0].triad = TRIAD_LAND;
    weapons[1].offense = 4, armor[1].defense = 1;
    // Faction 1's infantry (design 70) and faction 2's (71), alike.
    protos[70] = (struct unit_proto){.chassis = 0, .weapon = 1, .armor = 1, .reactor = 1, .flags = PROTO_FLAG_ACTIVE};
    protos[71] = protos[70];
    protos[72] = protos[70], protos[72].abilities = 0x8000;  // artillery
    factions[1].diff_level = factions[2].diff_level = 3;
    factions[1].base_count = factions[2].base_count = 2;
    humans = 0, current = 7, attack_flags = 3, base_count = 0;
    for (int i = 0; i < W / 2 * H; i++) tiles[i].bits = 0;

    // The odds: the attacker's strength over one more than the defender's,
    // eight times; the defender named.
    veh_count = 2;
    place(0, 4, 4, 1, 70);
    place(1, 5, 5, 2, 71);  // offset 2 from (4, 4)
    int off, def, def_id = -1;
    battle_compute(0, 1, &off, &def, 0);
    EXPECT(battle_fight_2(0, 2, 5, 5, 0, 0, &def_id), 8 * off / (def + 1));
    EXPECT(def_id, 1);
    EXPECT(battle_fight(0, 2, 0, 0, NULL), 8 * off / (def + 1));
    EXPECT(vehs[0].moves_spent, 0);

    // The attacker wins every round: the defender dies in ten, the
    // attacker spends a road move and is marked, and the war is scored.
    rolls_for(1, 10);
    *game_current_veh = 0;
    EXPECT(battle_fight_2(0, 2, 5, 5, 0, 1, NULL), 1);
    EXPECT(veh_count, 1);
    EXPECT(vehs[0].moves_spent, 3);
    EXPECT(vehs[0].state & 0x44, 0x44);
    EXPECT(factions[1].diplo_combat_count[2], 1);
    EXPECT(combat_win[1], 1);
    EXPECT(combat_loss[2], 1);
    EXPECT(monuments, 1);
    EXPECT(factions[2].units_lost[71], 1);
    EXPECT(factions[2].diplo_status[1] & TREATY_VENDETTA, TREATY_VENDETTA);  // an act of aggression
    EXPECT(protos[70].combat_factions & 1 << 2, 1 << 2);

    // The attacker loses every round, and dies with its stack.
    place(1, 5, 5, 2, 71);
    veh_count = 2, vehs[0].moves_spent = 0;
    rolls_for(0, 10);
    EXPECT(battle_fight_2(0, 2, 5, 5, 0, 1, NULL), 0);
    EXPECT(veh_count, 1);
    EXPECT(vehs[0].faction, 2);
    EXPECT(combat_win[2], 1);
    EXPECT(factions[2].diplo_combat_count[1], 1);

    // Artillery two squares off shells but can't kill in the open: it
    // leaves the rules' share of hit points.
    place(0, 5, 5, 2, 71);
    place(1, 7, 5, 1, 72);  // radius table offset 6 is (-2, 0)
    veh_count = 2;
    rules_basic.artillery_damage_num = 100;
    roll_n = 1, roll_i = 0, rolls[0] = 998;
    EXPECT(battle_fight_2(1, 6, 5, 5, 1, 1, NULL), 0);
    EXPECT(vehs[0].damage, 9);  // 10 hit points, at least 1% kept
    EXPECT(veh_count, 2);

    // The player kept a treaty (2), or called off a hopeless attack (1):
    // the moves spent are given back.
    humans = 1 << 1, current = 1, kept_treaty = 1;
    place(0, 8, 4, 2, 71);
    vehs[1].moves_spent = 0, vehs[1].proto = 70;
    EXPECT(battle_fight_2(1, 1, 8, 4, 0, 1, NULL), 2);
    kept_treaty = 0;
    vehs[1].damage = 9;  // one hit point left: odds far below one in two
    popp_answer = 0, popps = 0;
    EXPECT(battle_fight_2(1, 1, 8, 4, 0, 1, NULL), 1);
    EXPECT(popps, 1);
    EXPECT(vehs[1].moves_spent, 0);
    EXPECT(vehs[1].damage, 9);
    humans = 0, current = 7;
}

// ---- A base changing hands ----------------------------------------------------

static int conquests, replays, x_pops;
static void conquer_stub(int f, char *name) { conquests++; }
static void replay_stub(int e, int x, int y, int f) { replays++; }
static void log5_stub(char *a, char *b, int n1, int n2, int n3) {}
static int spying_stub(int f) { return 0; }
static int x_pop_stub(char *label, int unk) { return x_pops++, 0; }
static int __attribute__((thiscall)) ready_stub(void *c, int unk) { return -1; }
static void __attribute__((thiscall)) zoom_stub(void *w, int b, int u) {}
static void map_stub(int u) {}
static struct continent continents[REGION_COUNT];
static int site_stub(int x, int y, int u) { return 0; }
static int32_t goody_mult[2] = {1, 1}, map_seed;
// base_compute()'s tables and scratch: zeroed.
static struct res_info res;
static struct citizen citizens[16];
static struct rules_facility facility_rules[FACILITY_COUNT];
static struct rules_worldbuilder worldbuilder;
static int32_t zeros[64][32];
static int best_specialist_stub(void) { return 0; }
static int stack_fix_stub(int v) { return v; }
static int has_abil_stub(int p, uint32_t a) { return 0; }
static void add_goal_stub(int f, int t, int p, int x, int y, int b) {}
static int satan_stub(int f, int a) { return 0; }
static int veh_at_stub(int x, int y) { return -1; }
static int stack_check_stub(int v, int t, int a, int b, int c) { return 0; }
static void encounter_stub(int f, int g, int x, int y, int t, int n) {}
static void radius_stub(int x, int y, int r, int u) {}

static void test_capture(void) {
    game_mon_conquer_base = conquer_stub, game_replay_base = replay_stub, game_log_say5 = log5_stub;
    game_spying = spying_stub, game_x_pop = x_pop_stub, game_console_ready_search = ready_stub;
    game_base_win_zoom = zoom_stub, game_draw_map = map_stub, game_continents = continents;
    game_world_site = site_stub, game_goody_mult = goody_mult, game_map_seed = &map_seed;
    game_res_info = &res, game_citizens = citizens, game_facilities = facility_rules, game_worldbuilder = &worldbuilder;
    int32_t **ints[] = {&game_governor_faction, &game_dust_cloud, &game_solar_flares, &game_control_upkeep_a,
                        &game_terraform_reduce, &game_terraform_energy, &game_base_growth, &game_base_convoy_from,
                        &game_base_convoy_to, &game_base_forces_upkeep, &game_base_upkeep_state,
                        &game_tectonic_detonations, &game_sea_level, &game_native_life, &game_difficulty,
                        &game_base_commerce_import, &game_base_commerce_export, &game_tech_commerce_count,
                        &game_proposal_passes, &game_map_area_sqrt, &game_psych_talents, &game_psych_drones,
                        &game_psych_superdrones, &game_base_pacifism, &game_base_forces_supported,
                        &game_base_forces_paid, &game_base_workers, &game_base_reserved, &game_base_food_ok,
                        &game_current_turn, &game_base_square_flags, &game_cost_base, &game_faction_rankings,
                        &game_map_settings};
    for (unsigned i = 0; i < sizeof ints / sizeof ints[0]; i++) *ints[i] = zeros[i];
    game_best_specialist = best_specialist_stub, game_stack_fix = stack_fix_stub, game_has_abil = has_abil_stub;
    game_add_goal = add_goal_stub, game_great_satan = satan_stub, game_veh_at = veh_at_stub;
    game_stack_check = stack_check_stub, game_encounter = encounter_stub, game_draw_radius = radius_stub;
    rules_basic.nutrient_intake_citizen = 2, rules_basic.nutrient_cost_multi = rules_basic.mineral_cost_multi = 10;
    static int32_t current_base_id = 0, computed_base_id = -1;
    static struct base *current_base;
    game_current_base_id = &current_base_id, game_current_base = &current_base, game_computed_base_id = &computed_base_id;
    veh_count = 0, base_count = 3, humans = 1 << 1, current = 7, net = 0, rules = 0;
    for (int y = 0; y < H; y++)
        for (int x = y & 1; x < W; x += 2) at(x, y)->bits &= ~(uint32_t)MAP_UNIT;
    bases[0] = (struct base){.x = 2, .y = 2, .faction = 1, .faction_former = 1, .pop_size = 4, .queue_size = 0, .queue = {5}};
    bases[1] = (struct base){.x = 10, .y = 4, .faction = 2, .faction_former = 2, .pop_size = 6};
    bases[2] = (struct base){.x = 6, .y = 8, .faction = 1, .faction_former = 1, .pop_size = 2};
    strcpy(bases[0].name, "Alpha"), strcpy(bases[1].name, "Beta"), strcpy(bases[2].name, "Gamma");
    factions[1].energy_credits = 100, factions[1].pop_total = 9, factions[1].base_count = 2;
    factions[2].energy_credits = 0, factions[2].base_count = 1, factions[1].units_queue[5] = 1;
    factions[2].tech_ranking = 5;
    achieved[3] = achieved[4] = 1 << 1;  // two techs faction 2 lacks
    *(int32_t *)((uint8_t *)&factions[2] + 0x3c8) = 0;
    factions[2].diplo_combat_count[1] = 0;

    // Faction 2's probe team takes Alpha: no citizen lost, a share of the
    // energy taken, the queue cleared, the base renamed (from a human), and
    // research gained from the techs the defender knew.
    capture_base(0, 2, 1);
    EXPECT(bases[0].faction, 2);
    EXPECT(bases[0].faction_former, 1);
    EXPECT(bases[0].assimilation_turns, 50);
    EXPECT(bases[0].pop_size, 4);
    EXPECT(factions[2].energy_credits, 40);  // 4 * 100 / 10
    EXPECT(factions[1].energy_credits, 60);
    EXPECT(factions[1].base_count, 1);
    EXPECT(factions[2].base_count, 2);
    EXPECT(factions[1].units_queue[5], 0);
    EXPECT(bases[0].queue_size, 0);
    EXPECT((int)(bases[0].state & 0x10000), 0x10000);
    EXPECT(*(int32_t *)((uint8_t *)&factions[2] + 0x3c8), (5 + 3) * 2 * 5 / 8);
    EXPECT(factions[2].diplo_combat_count[1], 8);
    EXPECT(conquests, 1);
    EXPECT(replays, 1);
    EXPECT(factions[1].diplo_status[2] & TREATY_WANT_TO_TALK, TREATY_WANT_TO_TALK);

    // Faction 1 takes it back by force: a recapture, so no citizen lost and
    // no rename, and the assimilation is over.
    capture_base(0, 1, 0);
    EXPECT(bases[0].faction, 1);
    EXPECT(bases[0].assimilation_turns, 0);
    EXPECT(bases[0].pop_size, 4);

    // Faction 2 takes Gamma (size 2) by force: a citizen lost, and a third
    // of the facilities (rand() % 3 == 0 for each).
    bases[2].facilities_built[0] = 0xfe, rand_value = 3;
    capture_base(2, 2, 0);
    EXPECT(bases[2].faction, 2);
    EXPECT(bases[2].pop_size, 1);
    EXPECT(bases[2].facilities_built[0], 0);

    // Aliens take a human base (by probe): half its citizens flee as colony
    // pods onto free squares around it, the base keeping one.
    defs[2].rule_flags |= RULE_FLAG_ALIEN;
    humans = 0, rand_value = 1, veh_count = 0;
    bases[2].faction = 1, bases[2].faction_former = 1, bases[2].pop_size = 6;
    capture_base(2, 2, 1);
    EXPECT(bases[2].pop_size, 1);
    EXPECT(veh_count, 3);
    EXPECT(vehs[0].faction, 1);
    EXPECT(vehs[0].proto, 0);
    defs[2].rule_flags = 0, rand_value = 0, veh_count = 0;
}

int main(void) {
    static uint8_t image[0x300000];  // strings, from 0x400000; empty but for those set below
    game_test_image = image;
    strcpy((char *)game_at(0x6902b8), "100%");
    strcpy((char *)game_at(0x6902c0), "%");
    strcpy((char *)game_at(0x6902c4), ", ");
    strcpy((char *)game_at(0x6902c8), "%");
    game_map = &tiles_ptr, game_map_width = &width, game_map_height = &height, game_map_half_width = &half_width;
    game_map_flat = &flat, game_bases = bases, game_base_count = &base_count, game_vehs = vehs;
    game_veh_count = &veh_count, game_turn = &turn, game_current_faction = &current, game_net_multiplayer = &net;
    game_preferences = &prefs, game_default_gender = &gender, game_default_plural = &plural;
    game_human_factions = &humans, game_factions_alive = &alive, game_factions = factions, game_faction_defs = defs;
    game_protos = protos, game_chassis = chassis, game_weapons = weapons, game_state = &state;
    game_radius_x = radius_x, game_radius_y = radius_y, game_radius_count = radius_count;
    game_battle_text = battle_text, game_probe_algo = &algo, game_probe_target = &target;
    game_probe_target_hsa = &hsa, game_parse_says = parse_says_stub, game_parse_num = parse_num_stub;
    static char *script = "script";
    game_popp = popp_stub, game_script_file = &script, game_double_cross = double_cross_stub;
    game_ambience_flags = &ambience_flags, game_ambience = ambience_stub, game_tech_pick = tech_pick_stub, game_tech_achieve = tech_achieve_stub;
    game_netmsg_pop = netmsg_stub, game_rand = rand_stub;
    game_veh_lift_x = &lift[0], game_veh_lift_y = &lift[1], game_veh_lifted = &lift[2];
    game_log_say = log_say_stub, game_deletion_add = deletion_stub, game_base_win_check_base = check_base_stub;
    game_base_win_check_loc = check_loc_stub, game_net_veh_ref = &net_ref, game_battle_state = &battle;
    game_veh_ref_map_1 = &refs[0], game_veh_ref_map_2 = &refs[1], game_veh_ref_3 = &refs[2], game_veh_ref_4 = &refs[3];
    game_current_veh = &refs[4], game_base_find_dist = &find_dist;
    // The radius table's first squares: the centre, then the eight around.
    static const int rx[9] = {0, 1, 2, 1, 0, -1, -2, -1, 0}, ry[9] = {0, -1, 0, 1, 2, 1, 0, -1, -2};
    for (int i = 0; i < 9; i++) radius_x[i] = rx[i], radius_y[i] = ry[i];
    for (int i = 9; i < RADIUS_SQUARES; i++) radius_x[i] = 0, radius_y[i] = -40;  // off the map
    radius_count[0] = 1, radius_count[1] = 9;
    for (int y = 0; y < H; y++)
        for (int x = y & 1; x < W; x += 2) *at(x, y) = (struct map_tile){.climate = 4 << 5, .owner_site = 0xf, .territory = -1};
    bases[0] = (struct base){.x = 2, .y = 2, .faction = 1, .pop_size = 4};
    bases[1] = (struct base){.x = 10, .y = 4, .faction = 2, .pop_size = 6};
    bases[2] = (struct base){.x = 6, .y = 8, .faction = 1, .pop_size = 2};
    for (int b = 0; b < 3; b++) at(bases[b].x, bases[b].y)->bits |= MAP_BASE;
    strcpy(bases[0].name, "Alpha"), strcpy(bases[1].name, "Beta"), strcpy(bases[2].name, "Gamma");

    // Distance from headquarters, or 12 without one.
    EXPECT(vulnerable(1, 8, 2), 12);
    bases[2].facilities_built[0] |= 1 << 1;  // headquarters
    EXPECT(vulnerable(1, 6, 8), 0);
    EXPECT(vulnerable(1, 10, 8), 3);
    EXPECT(vulnerable(2, 6, 8), 12);

    // Energy stolen: reserves times the base's size over one more than the
    // faction's population.
    factions[2].energy_credits = 140, factions[2].pop_total = 13;
    EXPECT(steal_energy(1), 60);
    factions[2].energy_credits = 0;
    EXPECT(steal_energy(1), 0);

    // Probe odds: certain success, then success and survival.
    EXPECT(success_rates(0, 3, -1, -1), -1);
    EXPECT(strcmp(said[0], "100%"), 0);
    target = 2;
    EXPECT(success_rates(1, 4, 1, -1), 67);  // 100 - 100 / 3; survival 100 - 200 / 4
    EXPECT(strcmp(said[1], "67%, 50%"), 0);
    factions[2].se[SE_PROBE] = -1;  // weaker defenses
    EXPECT(success_rates(1, 4, 1, -1), 75);
    bases[1].facilities_built[34 / 8] |= 1 << (34 % 8);  // a Covert Ops Center undoes it
    EXPECT(success_rates(1, 4, 1, 1), 67);
    algo = 1;
    EXPECT(success_rates(1, 4, 1, 1), 84);  // losses halved
    hsa = 1;
    EXPECT(success_rates(1, 4, 1, 1), 16);  // halved, and (as the original) halved again
    EXPECT(strcmp(said[1], "33%, 50%"), 0);
    algo = hsa = 0;
    EXPECT(success_rates(2, 0, 0, -1), 100);  // morale at least 1
    EXPECT(strcmp(said[2], "100%, 50%"), 0);

    // An attack breaks a truce, treaty or pact, as the victim holds it.
    EXPECT(act_of_aggression(1, 2), 0);
    EXPECT(factions[1].diplo_status[2] & 0x8010, 0x8010);  // vendetta and bit 0x8000, both ways
    EXPECT(factions[2].diplo_status[1] & 0x8010, 0x8010);
    factions[1].diplo_status[2] = TREATY_TREATY;
    EXPECT(act_of_aggression(1, 2), 0);
    factions[2].diplo_status[1] = 0x4;
    EXPECT(act_of_aggression(1, 2), 1);
    EXPECT(crossed, 1);

    // A tech stolen; 1 when there was none.
    EXPECT(steal_tech(0, 2, 1), 0);
    humans = 1 << 1;  // no rethinking of builds for a human
    EXPECT(steal_tech(1, 2, 1), 1);
    EXPECT(achieved_tech, 9999);
    picked = 12;
    EXPECT(steal_tech(1, 2, 0), 0);
    EXPECT(achieved_tech, 12);
    net = 1, current = 3;
    EXPECT(steal_tech(1, 2, 0), 0);  // a human elsewhere in a network game
    net = 0, humans = 0, current = 7;

    // Orbital defense pods: a hit at the first roll that comes up 50 or more.
    struct faction_data *D = &factions[2];
    EXPECT(shoot(1, 2, 10, 4), 0);  // no pods, no flechettes
    D->satellites[3] = 2;
    rand_value = 75;
    EXPECT(shoot(1, 2, 10, 4), 1);
    EXPECT(D->odp_deployed, 1);
    EXPECT(D->satellites[3], 2);
    rand_value = 10;  // every roll misses: a pod is given up
    EXPECT(shoot(1, 2, 10, 4), 1);
    EXPECT(D->odp_deployed, 1);
    EXPECT(D->satellites[3], 1);
    // A human defender playing here is asked, and may say no.
    humans = 1 << 2, current = 2, popp_answer = 1, popps = 0;
    EXPECT(shoot(1, 2, 10, 4), 0);
    EXPECT(popps, 1);
    EXPECT(D->satellites[3], 1);
    humans = 0, current = 7, popp_answer = 0;
    // Flechette defenses at a base near the target, of anyone but the
    // attacker.
    D->satellites[3] = D->odp_deployed = 0;
    bases[1].facilities_built[39 / 8] |= 1 << (39 % 8);
    rand_value = 60;
    EXPECT(shoot(1, 2, 10, 4), 0);
    rand_value = 20;
    EXPECT(shoot(1, 2, 11, 5), 1);
    EXPECT(shoot(2, 1, 11, 5), 0);  // its own base's

    // Units killed: a stack's chaff is its native units and those with no
    // hit points left; later units move down a slot.
    veh_count = 4;
    protos[1].reactor = 1;
    for (int i = 0; i < 4; i++)
        vehs[i] = (struct veh){.x = 4, .y = 6, .proto = 1, .faction = 1, .next_in_stack = (int16_t)(i < 3 ? i + 1 : -1),
                               .prev_in_stack = (int16_t)(i - 1), .home_base = -1};
    vehs[1].faction = 0, vehs[3].damage = 10;
    vehs[-1].faction = 0;
    factions[1].units_active[1] = 3, factions[0].units_active[1] = 1;
    int killed = 0;
    battle_kill_chaff(2, &killed, NULL, NULL, -1, -1);
    EXPECT(killed, 2);
    EXPECT(veh_count, 2);
    EXPECT(factions[1].units_lost[1], 1);
    EXPECT(factions[0].units_lost[1], 1);
    EXPECT(vehs[0].next_in_stack, 1);
    EXPECT(vehs[1].prev_in_stack, 0);
    // An objective killed counts for the attacker's faction.
    vehs[1].flags = VEH_FLAG_UNK_20;
    EXPECT(battle_kill(1, &killed, NULL, NULL, -1, 3), 1);
    EXPECT(factions[3].objective_bonus, 1);
    battle_kill_stack(0, &killed, NULL, NULL, -1, -1);
    EXPECT(veh_count, 0);
    EXPECT(killed, 4);

    test_battles();
    test_capture();

    if (failures) return 1;
    printf("engine_combat: ok\n");
    return 0;
}
