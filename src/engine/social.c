// Social engineering. Reimplemented from terranx.exe 2.0's disassembly;
// names follow OpenSMACX (Brendan Casey, GPL-3.0-or-later) and Thinker
// (MIT).
#include <string.h>

#include "game.h"
#include "map.h"
#include "shadow.h"
#include "social.h"
#include "base.h"
#include "tech.h"

enum {
    CATEGORY_VALUES = 2, CATEGORY_FUTURE = 3, MODEL_POWER = 1, MODEL_CYBERNETIC = 1, MODEL_THOUGHT_CONTROL = 3,
    SP_ASCETIC_VIRTUES_ = 13, SP_NETWORK_BACKBONE_ = 21, SP_LIVING_REFINERY = 23, SP_CLONING_VATS_ = 24,
};

static int holds_project(int project, int faction) {
    int b = game_secret_projects[project];
    return b >= 0 && game_bases[b].faction == faction;
}

static int model_effect(int category, int model, int effect) {
    const uint8_t *p = game_social_effects + category * SOCIAL_CATEGORY_SIZE + model * SOCIAL_MODEL_SIZE;
    int32_t v;
    memcpy(&v, p + 4 * effect, sizeof v);
    return v;
}

// The effects of a faction's four social models, into `effect`
// [SE_COUNT]: each model's, a penalty cancelled by the Network Backbone
// (Cybernetic) or the Cloning Vats (Thought Control and Power), and
// negated or doubled by the faction's IMPUNITY or PENALTY for the model.
// Unless `quick`, also its projects (Ascetic Virtues POLICE +1, the
// Living Refinery SUPPORT +2), a temple (PLANET +1, and RESEARCH +1 for
// the Progenitors), its own base values, and its IMMUNITY (no negative)
// and ROBUST (negative halved). `unused` is passed by the game and not
// read.
void social_calc(const int32_t *models, int32_t *effect, int faction, int unused, int quick) {
    (void)unused;
    memset(effect, 0, SE_COUNT * sizeof *effect);
    const struct faction_def *d = &game_faction_defs[faction];
    for (int cat = 0; cat < SOCIAL_CATEGORIES; cat++) {
        int model = models[cat];
        for (int e = 0; e < SE_COUNT; e++) {
            int v = model_effect(cat, model, e);
            if (v < 0) {
                if (cat == CATEGORY_FUTURE && model == MODEL_CYBERNETIC && holds_project(SP_NETWORK_BACKBONE_, faction))
                    continue;
                if (((cat == CATEGORY_FUTURE && model == MODEL_THOUGHT_CONTROL) ||
                     (cat == CATEGORY_VALUES && model == MODEL_POWER)) &&
                    holds_project(SP_CLONING_VATS_, faction))
                    continue;
                for (int i = 0; i < d->bonus_count; i++) {
                    if (d->bonus_val1[i] != cat || d->bonus_val2[i] != model) continue;
                    if (d->bonus_id[i] == BONUS_IMPUNITY) effect[e] -= v;
                    else if (d->bonus_id[i] == BONUS_PENALTY) effect[e] += v;
                }
            }
            effect[e] += v;
        }
    }
    if (quick) return;
    if (holds_project(SP_ASCETIC_VIRTUES_, faction)) effect[SE_POLICE]++;
    if (holds_project(SP_LIVING_REFINERY, faction)) effect[SE_SUPPORT] += 2;
    if (has_temple(faction)) {
        effect[SE_PLANET]++;
        if (d->rule_flags & RULE_FLAG_ALIEN) effect[SE_RESEARCH]++;
    }
    for (int e = 0; e < SE_COUNT; e++) effect[e] += game_factions[faction].se_base[e];
    for (int i = 0; i < d->bonus_count; i++) {
        int id = d->bonus_id[i];
        if (id != BONUS_IMMUNITY && id != BONUS_ROBUST) continue;
        int32_t *v = &effect[d->bonus_val1[i]];
        if (id == BONUS_IMMUNITY) *v = *v < 0 ? 0 : *v > 999 ? 999 : *v;
        else if (*v < 0) *v /= 2;
    }
}

// The start of a faction's turn: its pending models take effect, and its
// effects are worked out for them three times over (pending, current, and
// a second copy the game keeps); nothing paid yet for changes.
void social_upkeep(int faction) {
    struct faction_data *f = &game_factions[faction];
    memcpy(f->se_models, f->se_models_pending, sizeof f->se_models);
    social_calc(f->se_models_pending, f->se_pending, faction, 0, 0);
    social_calc(f->se_models_pending, f->se, faction, 0, 0);
    social_calc(f->se_models_pending, f->se_2, faction, 1, 0);
    f->se_upheaval_paid = 0;
}

// Whether a faction may choose a model: it has the model's tech, and its
// leader isn't set against it.
int society_avail(int category, int model, int faction) {
    const struct faction_def *d = &game_faction_defs[faction];
    if (d->anti_ideology_category == category && d->anti_ideology_model == model) return 0;
    return has_tech(game_social_preqs[category * SOCIAL_PREQ_STRIDE + model], faction);
}

// The energy it costs a faction to switch from its current models to
// `models`: one more than the number of categories changed, cubed, times
// its difficulty level (3 for the AI), and half as much again for the
// Progenitors.
int social_upheaval(int faction, const int32_t *models) {
    const struct faction_data *f = &game_factions[faction];
    int changes = 0;
    for (int c = 0; c < SOCIAL_CATEGORIES; c++) changes += models[c] != f->se_models[c];
    if (!changes) return 0;
    changes++;
    int cost = changes * changes * changes * (game_is_human(faction) ? f->diff_level : 3);
    if (game_faction_defs[faction].rule_flags & RULE_FLAG_ALIEN) cost += cost / 2;
    return cost;
}

static int clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

// The AI's choice of social models. Every combination of models the
// faction may choose is scored on its effects, weighed by what the faction
// cares about (growth, tech, wealth, power and its taste for a fight), how
// threatened it is and how far it trails the leader; the best (the last of
// equals) wins. Without `output`, an AI faction scores with its own
// priorities and switches to the winner, paying for the upheaval;
// with it, the choice is written to `output` [4] for the social
// engineering screen's advice, scored with the given priorities, or with
// the faction's own and one effect (`growth` - 100) favoured above all
// when `growth` is over 100. A negative `growth` asks for nothing.
//
// OpenSMACX's version has several branches nested wrongly (its planet,
// research and industry terms only count for the advice), checks a
// leader's opposed model with "or" rather than "and", reads the probe
// term's factions from the loop counter, and leaves out ECONOMY's
// rounding: (base + 2 ECONOMY - 4) is divided by the threat, not just 4.
void social_ai(int faction, int growth, int tech, int wealth, int power, int32_t *output) {
    struct faction_data *f = &game_factions[faction];
    const struct faction_def *d = &game_faction_defs[faction];
    int target = -1, fight;
    if (!output) {
        if (game_is_human(faction)) return;
    } else if (growth < 0) {
        return;
    } else if (growth > 100) {
        target = growth - 100;
    }
    if (!output || target >= 0) {
        growth = f->ai_growth, tech = f->ai_tech, wealth = f->ai_wealth, power = f->ai_power;
        fight = f->ai_fight;
    } else {
        fight = power ? 1 : wealth ? -1 : 0;
    }
    // How much its bases still mean to grow.
    int pop_growth = 0;
    for (int b = 0; b < *game_base_count; b++)
        if (game_bases[b].faction == faction) pop_growth += pop_goal(b) - game_bases[b].pop_size;
    // Its armies in threatened regions (defensive ones counting twice), and
    // the regions it is attacking from and colonizing.
    int threat = 0, colonizing = 0, attacking = 0;
    for (int r = 1; r < 63; r++) {
        if (!f->region_total_bases[r]) continue;
        int plan = f->region_base_plan[r];
        if (plan == PLAN_DEFENSIVE || (f->region_flags[r] & 0x400)) {
            threat += 2 * f->region_total_combat_units[r];
        } else if (plan == PLAN_OFFENSIVE) {
            threat += f->region_total_combat_units[r];
            attacking++;
        } else if (plan == PLAN_COLONY) {
            colonizing++;
        }
    }
    if (output && power && threat < 1) threat = 1;
    // Late in the game, a faction well behind the leader in tech.
    int behind = 1;
    if ((*game_state & GAME_STATE_RANKED) && f->ranking < 6) {
        int gap = game_factions[game_ranked_faction[0]].tech_ranking / 2 - f->tech_ranking / 2;
        if (gap > 5) behind = 2;
        if (gap > 10) behind++;
    }
    int best = -9999, choice = -1;
    for (int i = 0; i < 256; i++) {
        int32_t models[SOCIAL_CATEGORIES], e[SE_COUNT];
        int ok = 1;
        for (int c = 0, k = i; c < SOCIAL_CATEGORIES && ok; c++, k >>= 2) {
            models[c] = k & 3;
            if (models[c] && !society_avail(c, models[c], faction)) ok = 0;
        }
        if (!ok) continue;
        game_do_all_non_input();
        social_calc(models, e, faction, 0, 0);
        int score = target >= 0 ? e[target] * 1000 : 0;
        // The leader's favoured model and effect; an opposed choice rules
        // the combination out.
        int bad = 0;
        if (d->ideology_category >= 0 && d->ideology_model) {
            int m = models[d->ideology_category];
            if (m == d->ideology_model) score += f->base_count;
            else if (m) bad = 1;
        }
        if (d->ideology_effect >= 0) {
            int v = e[d->ideology_effect];
            if (v > 0) score += f->base_count;
            if (v < 0) bad = 1;
        }
        if (bad) continue;
        // ECONOMY, with the energy EFFICIENCY loses: a high ECONOMY makes up
        // for some of it.
        int effic = f->social_effic[clamp(4 - e[SE_EFFIC], 0, 8)];
        int econ = e[SE_ECONOMY], w;
        if (econ >= 2) {
            w = (f->unk_7f4 + 2 * (econ > 4 ? 4 : econ) - 4) / (threat + 1);
        } else if (econ == 1) {
            w = 0;
            for (int r = 0; r < REGION_COUNT; r++)
                w += ((f->region_base_plan[r] != PLAN_DEFENSIVE) + 1) * f->region_total_bases[r];
            w /= threat + 1;
        } else if (econ >= -1) {
            w = -1;
        } else {
            w = 0;
            for (int r = 0; r < REGION_COUNT; r++)
                w -= ((f->region_base_plan[r] != PLAN_DEFENSIVE) + 1) * f->region_total_bases[r] * (econ + 1);
            w /= threat + 1;
        }
        if (w > 0) {
            while (effic > 0) {
                w--, effic--;
                if (w <= 0) break;
            }
        }
        if (!threat) w *= 2;
        w *= (f->ai_fight < 0 && threat < 2) + tech * 2 + wealth + 1;
        if (output) {
            if (wealth || tech) w *= 2;
            if (growth || power) w /= 2;
        } else if (wealth && !power && !growth) {
            w *= 2;
        }
        score += w / behind;
        // SUPPORT: the minerals its units would cost.
        int support = e[SE_SUPPORT];
        w = f->social_support[clamp(support + 4, 0, 7)];
        if (support <= -4) w *= 3;
        if (threat) {
            w *= 2;
            if (support <= -3) w *= 2;
            if ((support == -1 || support == -2) && econ < 2) w += w / 2;
        }
        if (output) {
            if (power) w *= 3;
            if (growth) w *= 2;
            if (tech) w /= 2;
        } else {
            if (fight > 0) w *= 2;
            if (growth > 0) w += w / 2;
            if (wealth > 0) w /= 2;
            if (tech > 0) w += -(w / 4);
            if (power > 0) w += w / 2;
        }
        score -= w;
        // MORALE, the steps past 1 counting less.
        int morale = clamp(e[SE_MORALE], -4, 4) * 2;
        if (morale < -6) morale++;
        if (morale < -2) morale++;
        if (morale > 6) morale--;
        if (morale > 2) morale--;
        w = (f->armed_units + f->base_count) * (threat + 1) * (fight + 2) * morale * behind / 8;
        if (output) {
            if (power) w *= 3;
            if (growth || wealth) w /= 2;
        } else {
            if (fight > 1) w *= 2;
            if (power && !growth && !wealth) w *= 2;
            if (fight < 0 && !power && (wealth || tech) && !threat) w /= 2;
        }
        score += w;
        // EFFICIENCY: the energy still lost.
        w = effic;
        if (e[SE_EFFIC] == -3) w *= 2;
        else if (e[SE_EFFIC] <= -4) w *= 4;
        if (!threat) w *= 2;
        if (output) {
            if (tech) w *= 3;
            if (growth || power) w /= 2;
        } else if (!wealth && !tech) {
            if (power) w /= 2;
        } else if (!power) {
            if (!growth) w *= 2;
        } else if (!wealth) {
            w /= 2;
        }
        score -= w;
        // GROWTH, by the growth to come and the regions being settled.
        w = (f->nutrient_surplus_total + pop_growth) * (growth * 2 + 2 - wealth) * (colonizing + 1) * e[SE_GROWTH] / 5;
        if (game_aah_ooga(faction, faction)) w *= 2;
        if (d->rule_population > 0) w /= 2;
        if (output) {
            if (growth || wealth) w *= 2;
            if (tech) w /= 2;
        } else if (growth && power) {
            w *= 2;
        }
        score += w;
        // POLICE, more for a faction trailing the second-ranked one and
        // less for one ahead of the leader.
        int police = e[SE_POLICE];
        w = (attacking * 2 + 1) * f->base_count * clamp(police + 2, -2, 2);
        const struct faction_data *r0 = &game_factions[game_ranked_faction[0]];
        const struct faction_data *r1 = &game_factions[game_ranked_faction[1]];
        if (f->tech_ranking < r1->tech_ranking && f->ranking < r1->ranking) w *= 2;
        if (f->tech_ranking >= r0->tech_ranking && f->ranking > r0->ranking) w /= 2;
        if (output && (power || growth)) w *= 2;
        score += w / (2 - fight);
        // TALENT with POLICE: how content its bases would be.
        w = f->social_psych[clamp(e[SE_TALENT] + 4, 0, 7)][clamp(police + 5, 0, 8)];
        if (threat) w += f->base_count * police;
        if (output) {
            if (power || growth) w *= 2;
            if (wealth) w /= 2;
        } else {
            if (growth && tech) w *= 2;
            if (power && (tech || wealth)) w /= 2;
        }
        score += w;
        // PLANET: ecological damage, and psi units.
        w = f->eco_damage_total * (4 - e[SE_PLANET]) * 4 / 4;
        if (f->se_base[SE_PLANET] > 0 || f->best_psi_offense > f->best_weapon_value) w += f->total_combat_units;
        else if (behind > 1) w /= 2;
        if (output) {
            if (growth) w *= 2;
            if (wealth) w /= 2;
        } else if (power) {
            if (!growth) w /= 2;
            if ((tech || wealth) && !growth) w /= 2;
        } else if (!growth) {
            if (tech || wealth) w /= 2;
        } else if (!tech) {
            w *= 4;
        }
        score -= w;
        // RESEARCH and INDUSTRY.
        w = clamp(e[SE_RESEARCH], -5, 5) * f->labs_total / 10;
        if (!threat) w *= 2;
        if (output) {
            if (tech) w *= 2;
            if (power) w /= 2;
        }
        score += w;
        w = ((threat + wealth) * 2 + 1) * f->pop_total * clamp(behind, 1, 2) * e[SE_INDUSTRY] / 10;
        if (power) w *= 2;
        if (wealth) w += w / 2;
        if (tech) w /= 2;
        if (fight < 0 && !wealth) w /= 2;
        score += w;
        score += social_upheaval(faction, models) / -3;
        // PROBE, against each other faction by how much it has been
        // probing and how relations stand.
        if (e[SE_PROBE]) {
            for (int o = 1; o < FACTION_COUNT; o++) {
                if (o == faction) continue;
                w = game_factions[o].mind_control_total / 4 + 1;
                uint32_t diplo = f->diplo_status[o];
                if (diplo & TREATY_PACT) w /= 2;
                if (!(diplo & TREATY_WANT_REVENGE)) {
                    if (diplo & TREATY_TREATY) w /= 2;
                    if (!(diplo & TREATY_COMMLINK)) w /= 2;
                    if (!(diplo & TREATY_VENDETTA)) w /= 2;
                }
                w = (game_factions[o].base_count + f->base_count) * clamp(e[SE_PROBE], -2, 3) * w / 2;
                if (tech || wealth) w *= 2;
                score += w;
            }
        }
        if (score >= best) best = score, choice = i;
    }
    if (choice < 0) {
        if (output) memcpy(output, f->se_models_pending, sizeof f->se_models_pending);
        return;
    }
    if (output) {
        for (int c = 0; c < SOCIAL_CATEGORIES; c++) output[c] = (choice >> (2 * c)) & 3;
        return;
    }
    int same = 1;
    for (int c = 0; c < SOCIAL_CATEGORIES; c++) {
        int m = (choice >> (2 * c)) & 3;
        if (m != f->se_models_pending[c]) same = 0, f->se_models_pending[c] = m;
    }
    if (same) return;
    game_social_set(faction);
    int cost = social_upheaval(faction, f->se_models_pending);
    f->energy_credits = clamp(f->energy_credits - cost, 0, 999999999);
    f->se_upheaval_paid += cost;
    if (!game_is_human(faction)) game_consider_designs(faction);
}

// ---- Hooks and shadow checks ------------------------------------------------

extern struct hook_module social_module;
#define SOCIAL (&social_module)
SHADOW_WRITER(SOCIAL, void, social_calc, (const int32_t *a, int32_t *b, int c, int d, int e), (a, b, c, d, e),
              WRITES(b, SE_COUNT * sizeof(int32_t)), "%d, %d, %d", c, d, e)
SHADOW_WRITER(SOCIAL, void, social_upkeep, (int a), (a), WRITES(&game_factions[a], sizeof(struct faction_data)), "%d", a)
SHADOW3(SOCIAL, int, society_avail, int, int, int)
SHADOW2(SOCIAL, int, social_upheaval, int, const int32_t *)
// Advice writes only its output; a choice puts it into effect (social_set
// rescales the bases' stores) and has the AI redesign its units.
#define WRITES_SOCIAL_AI \
    if (o) WRITES(o, SOCIAL_CATEGORIES * sizeof(int32_t)); \
    else hooks_writes_game(&w_)
SHADOW_WRITER(SOCIAL, void, social_ai, (int a, int b, int c, int d, int e, int32_t *o), (a, b, c, d, e, o),
              WRITES_SOCIAL_AI, "%d, %d, %d, %d, %d, %s", a, b, c, d, e, o ? "advice" : "choice")

static int slice;

// Each faction's effects for every combination of models, quick and not,
// and its turn's upkeep with made-up pending models.
static void social_sweep(void) {
    slice++;
    if (!*game_map) return;
    int sweeping = hooks_sweeping;
    hooks_sweeping = 1;
    for (int f = 1; f < FACTION_COUNT; f++) {
        int32_t effect[SE_COUNT];
        for (int c = 0; c < 256; c++) {
            int32_t models[SOCIAL_CATEGORIES] = {c & 3, (c >> 2) & 3, (c >> 4) & 3, (c >> 6) & 3};
            check_social_calc(models, effect, f, c & 1, (c + slice) % 3 == 0);
        }
        struct faction_data *fd = &game_factions[f];
        int32_t pending[SOCIAL_CATEGORIES];
        memcpy(pending, fd->se_models_pending, sizeof pending);
        check_social_upkeep(f);
        for (int c = 0; c < SOCIAL_CATEGORIES; c++) fd->se_models_pending[c] = (f + c + slice) & 3;
        check_social_upkeep(f);
        memcpy(fd->se_models_pending, pending, sizeof pending);
        for (int c = 0; c < SOCIAL_CATEGORIES; c++)
            for (int m = 0; m < SOCIAL_MODELS; m++) check_society_avail(c, m, f);
        int32_t models[SOCIAL_CATEGORIES];
        for (int c = 0; c < SOCIAL_CATEGORIES; c++) models[c] = (fd->se_models[c] + (slice >> c)) & 3;
        check_social_upheaval(f, fd->se_models);
        check_social_upheaval(f, models);
        // Advice for each mix of priorities, then with an effect favoured.
        int32_t advice[SOCIAL_CATEGORIES];
        for (int p = 0; p < 16; p++) check_social_ai(f, p & 1, (p >> 1) & 1, (p >> 2) & 1, p >> 3, advice);
        check_social_ai(f, 101 + (f + slice) % (SE_COUNT - 1), 0, 0, 0, advice);
        check_social_ai(f, -1, 0, 0, 0, advice);
    }
    // The AI's choice made, one faction a turn.
    check_social_ai(1 + slice % (FACTION_COUNT - 1), 0, 0, 0, 0, NULL);
    hooks_sweeping = sweeping;
}

#define PUSH_EBP_MOV_EBP_ESP 0x55, 0x8b, 0xec
static const struct hook social_hooks[] = {
    HOOK(0x5b4210, social_calc, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x10),  // sub esp, 16
    HOOK(0x5b44d0, social_upkeep, PUSH_EBP_MOV_EBP_ESP, 0x53, 0x56, 0x57),  // push ebx; push esi; push edi
    HOOK(0x5b4550, social_upheaval, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x4d, 0x08),  // mov ecx, [ebp+8]
    HOOK(0x5b4730, society_avail, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x4d, 0x10),  // mov ecx, [ebp+16]
    HOOK(0x5b4790, social_ai, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x7c),  // sub esp, 0x7c
};
struct hook_module social_module = {"social", social_hooks, HOOK_COUNT(social_hooks), social_sweep, NULL, -1};
