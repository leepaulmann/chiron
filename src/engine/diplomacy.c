// The diplomacy talks' answers to the player's proposals: what the AI makes
// of a threat (threaten), a request for energy (energy_trade), a pact or a
// treaty proposed (propose_pact, propose_treaty), a gift (make_gift), a
// joint attack (propose_attack), techs traded (tech_trade) or bought (buy_tech)
// and a base asked for (base_swap); the player's menus (proposal_menu)
// and the proposal put (make_a_proposal), and the AI's battle plans
// (battle_plans). The game calls these only from the player's talks
// (communicate()), so the first
// faction is always the player, at the console, and the second the AI it
// talks to. The talks' deals use trade_maps() and give_a_base(), which the
// AIs' own deals call too. Reimplemented from terranx.exe 2.0's disassembly; names follow
// Thinker (MIT) and OpenSMACX (Brendan Casey, GPL-3.0-or-later). Thinker
// rewrites several of these with changes of its own; where it differs from
// the original the original is followed, as noted below.
//
// The window's dialogs (X_pops_11) show the AI's answer, with the
// player's choice where it has one. In a network game each step is sent
// to the other players (log_say(), message_data(), await_diplo()) instead
// of being made; the original has those inline, as below.
#include "diplomacy.h"

#include <stdio.h>
#include <string.h>

#include "base.h"
#include "council.h"
#include "faction.h"
#include "game.h"
#include "map.h"
#include "quiet.h"
#include "shadow.h"
#include "spot.h"
#include "tech.h"
#include "veh.h"

enum {
    TREATY_TRUCE_ = 0x4, TREATY_SURRENDERED_ = 0x2000000,
    AGENDA_UNK_1000 = 0x1000, AGENDA_UNK_2000 = 0x2000,
    FACTION_FLAG_COOP_WITH_HUMAN_ = 0x400000,  // faction_data.flags
    TECH_COMMLINK_0 = 0x59,  // the commlink with faction 0; faction i's is 0x59 + i
    RULES_SCN_NO_TECH_TRADING = 0x2000000, RULES_VICTORY_DIPLOMATIC = 0x8, RULES_UNK_100 = 0x100,  // game_rules
};

// The proposals and counter-proposals the player picks from the menus
// (proposal_menu()), in game_diplo_proposal and game_diplo_counter.
enum {
    PROPOSE_GIFT = 1, PROPOSE_PACT = 2, PROPOSE_TREATY = 3, PROPOSE_TECH = 4, PROPOSE_BUY_TECH = 5,
    PROPOSE_ENERGY = 6, PROPOSE_MAPS = 7, PROPOSE_ATTACK = 8, PROPOSE_BASE_SWAP = 9, PROPOSE_CLOSE = 10,
    PROPOSE_REPAY_LOAN = 11, PROPOSE_COUNCIL = 12, PROPOSE_COMMLINK = 13,
};
enum {
    COUNTER_FRIENDSHIP = 1, COUNTER_NAME_A_PRICE = 2, COUNTER_THREATEN = 3, COUNTER_RESEARCH = 4,
    COUNTER_ENERGY = 5, COUNTER_LOAN = 6, COUNTER_ALL_RESEARCH = 7, COUNTER_BASE = 8,
};

static inline char *text(uint32_t addr) { return (char *)game_at(addr); }
static inline int iabs(int v) { return v < 0 ? -v : v; }
static inline int clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline struct faction_data *F(int faction) { return &game_factions[faction]; }
static inline uint32_t status(int faction, int other) { return game_factions[faction].diplo_status[other]; }

// A faction's leader's title, the leader's name and the faction's noun, for
// the next message (with their grammar).
static void say_title(int slot, int faction) {
    struct faction_def *d = &game_faction_defs[faction];
    *game_parse_plural = 0, *game_parse_gender = d->leader_female;
    game_parse_says(slot, d->leader_title, -1, -1);
}
static void say_name(int slot, int faction) {
    struct faction_def *d = &game_faction_defs[faction];
    *game_parse_plural = 0, *game_parse_gender = d->leader_female;
    game_parse_says(slot, d->leader_name, -1, -1);
}
static void say_noun(int slot, int faction) {
    struct faction_def *d = &game_faction_defs[faction];
    *game_parse_gender = d->noun_gender, *game_parse_plural = d->noun_plural;
    game_parse_says(slot, d->noun, -1, -1);
}

// The AI's (`other`'s) line in the talks, with its portrait; the player's
// choice, where the line offers one.
static int answer(char *label, int other) {
    return game_x_pops_11(label, 0x100000, game_faction_portraits[other], game_pop_wait);
}
// A line picked at random from two: the label with 0 or 1 after it.
static char *label_random(uint32_t label) {
    char digit[12];
    game_str_buffer[0] = 0;
    strcat(game_str_buffer, text(label));
    snprintf(digit, sizeof(digit), "%d", game_random(0, 2));
    strcat(game_str_buffer, digit);
    return game_str_buffer;
}

// ---- A step of the talks, made or, in a network game, sent ------------------------------

// Sent without waiting for the other players (some of the original's
// steps), or waiting.
static void net_post(uint32_t log_label, int la, int lb, int lc, int type, int a, int b, int c, int d) {
    game_log_say(text(log_label), la, lb, lc);
    game_message_data(0x2000 + type, 0, a, b, c, d);
}
static void net_send(uint32_t log_label, int la, int lb, int lc, int type, int a, int b, int c, int d) {
    net_post(log_label, la, lb, lc, type, a, b, c, d);
    game_net_daemon_await_diplo(game_net_daemon, type);
}
static void net_treaty_on(int faction, int other, uint32_t bits) {
    if (*game_net_multiplayer) net_send(0x68d444, faction, other, (int)bits, 0x441, faction, other, (int)bits, 0);
    else treaty_on(faction, other, bits);
}
static void net_set_treaty(int faction, int other, uint32_t bits, int on) {
    if (*game_net_multiplayer) net_send(0x68d46c, faction, other, (int)bits, 0x443, faction, other, (int)bits, on);
    else set_treaty(faction, other, bits, on);
}
static void net_agenda_on(int faction, int other, uint32_t bits) {
    if (*game_net_multiplayer) net_send(0x68d480, faction, other, (int)bits, 0x444, faction, other, (int)bits, 0);
    else agenda_on(faction, other, bits);
}
static void net_double_cross(int faction, int victim, int other) {
    if (*game_net_multiplayer) net_send(0x68d53c, faction, victim, other, 0x440, faction, victim, other, 0);
    else double_cross(faction, victim, other);
}
static void net_tech(int faction, int tech, int from) {
    if (*game_net_multiplayer) net_send(0x68d4f0, faction, tech, from, 0x44b, faction, tech, from, 0);
    else game_tech_achieve(faction, tech, from, 0);
}
// The same without waiting for the other players (energy_trade()'s).
static void net_tech_nowait(int faction, int tech, int from) {
    if (*game_net_multiplayer) net_post(0x68d4f0, faction, tech, from, 0x44b, faction, tech, from, 0);
    else game_tech_achieve(faction, tech, from, 0);
}
static void net_cede_base(int base, int faction) {
    if (*game_net_multiplayer) net_send(0x68d528, base, faction, 0, 0x449, base, faction, 0, 0);
    else give_a_base(base, faction);
}
static void net_pact_ends(int faction, int other) {
    if (*game_net_multiplayer) net_send(0x68d500, 0, 0, 0, 0x44d, faction, other, 0, 0);
    else pact_ends(faction, other);
}
// Energy from `other` to `faction` (or back, for a negative amount), with
// the console's figures brought up to date.
static void net_energy(int faction, int amount, int other) {
    if (*game_net_multiplayer) {
        net_send(0x68d4bc, faction, other, amount, 0x447, faction, amount, other, -amount);
    } else {
        if (faction) F(faction)->energy_credits += amount;
        if (other) F(other)->energy_credits += -amount;
        game_console_update_data(game_console, 1);
    }
}
// And the info window's figures, after some.
static void energy_shown(void) {
    *(int32_t *)((char *)game_info_win + 0x90) = 0;
    game_console_update_data(game_console, 0);
}
// The talks held while a base changes hands or a pact ends (diplo_lock()
// and diplo_unlock(), inline): in a network game, until the other players
// are in step.
static void lock(void) {
    game_log_say(text(0x68d554), 0, 0, 0);
    game_time_get_time();
    while (!*game_control_turn_a && game_net_daemon_add_global(game_net_daemon)) {
        game_net_pump();
        game_time_get_time();
    }
}
static void unlock(void) {
    game_log_say(text(0x68d578), 0, 0, 0);
    game_net_daemon_unlock(game_net_daemon);
}

// ---- threaten ------------------------------------------------------------------------

// The player (`faction`) threatens the AI (`other`) to get the proposal it
// made (game_diplo_proposal): the AI's patience resets and the techs
// between them are weighed. An AI resolved to betray the player
// (game_diplo_can_betray) declares a vendetta at once. In a pact, a
// player ranked 5 or below threatening one ranked 6 or above (outside a
// joint attack) instils fear from their standing, techs and the AI's
// priorities (at least 4 if it surrendered); one that surrendered in a
// pact fears at least 2. Fear buys energy (25 for each turn since they
// last spoke, times the fear, over 16, at most the AI's reserves, in 25s);
// no fear adds friction. Then the proposal: a joint attack on a faction
// the AI isn't at war with (in a pact, when the AI would fight it anyway,
// surrendered, or is far behind the player or the target; with fear,
// when the player's strength times the fear beats the target's against
// the AI's), the energy, a commlink, the AI's best tech, or a base (by its
// value_of_base() against the fear; one that surrendered in a pact gives
// it up unless it is its last, or one it took from the player that it
// can't spare). Anything extorted brings a truce if there was no treaty.
// Otherwise the AI in a pact may end it if the player is hostile or a
// great satan and ahead, then turns the player away; outside one it may
// declare a vendetta if stronger, or ignores the threat.
//
// Thinker's threaten() first lets an AI in a pact end it over a tech or
// energy demand, which the original doesn't.
void threaten(int faction, int other) {
    struct faction_data *me = F(faction), *ai = F(other);
    *game_diplo_second = other;
    ai->diplo_patience[faction] = (int8_t)get_patience(faction, other);
    tech_analysis(faction, other);
    if (*game_diplo_can_betray) {
        me->diplo_spoke[other] = *game_turn;
        net_treaty_on(other, faction, TREATY_VENDETTA);
        game_diplomacy_caption(faction, other);
        say_title(0, faction), say_name(1, faction);
        game_parse_says(2, game_get_noun_phrase(faction, 8), -1, -1);
        answer(label_random(0x68dc8c), other);
        return;
    }
    int fear = *game_diplo_fear;
    if ((status(faction, other) & TREATY_PACT) && !fear && me->ranking <= 5 && ai->ranking >= 6 &&
        *game_diplo_proposal != PROPOSE_ATTACK) {
        int score = ai->tech_ranking / 2 - me->tech_ranking / 2 - me->tech_commerce_bonus - ai->ai_fight +
                    ai->tech_commerce_bonus - me->ranking + ai->ranking;
        if (*game_diplo_proposal == PROPOSE_TECH) score += ai->ai_tech - ai->ai_power;
        else if (*game_diplo_proposal == PROPOSE_ENERGY) score += ai->ai_wealth - ai->ai_power;
        if ((status(other, faction) & TREATY_SURRENDERED_) && score < 4) score = 4;
        else if (score < 0) score = 0;
        else if (score > 999) score = 999;
        *game_diplo_fear = fear = score;
    }
    if ((status(other, faction) & TREATY_PACT) && (status(other, faction) & TREATY_SURRENDERED_) && fear < 2)
        *game_diplo_fear = fear = 2;
    int value = (*game_turn - *game_diplo_spoke_turn) * fear * 25 / 16;
    if (value > ai->energy_credits) value = ai->energy_credits / 25 * 25;
    if (value < 0) value = 0;
    if (!fear) cause_friction(other, faction, 2);
    game_diplomacy_caption(faction, other);
    int extorted = 0, proposal = *game_diplo_proposal;
    if (proposal == PROPOSE_ATTACK) {
        int target = *game_diplo_proposal_faction;
        if (status(other, target) & TREATY_VENDETTA) {
            say_title(0, target), say_name(1, target);
            answer(text(0x68dc98), other);
            return;
        }
        if ((status(faction, other) & TREATY_PACT) &&
            (((status(faction, target) & TREATY_VENDETTA) && !(status(other, target) & TREATY_PACT)) ||
             (status(other, faction) & TREATY_SURRENDERED_))) {
            struct faction_data *t = F(target);
            if (wants_to_attack(other, target, 0) || wants_to_attack(target, other, 0) ||
                (status(other, faction) & TREATY_SURRENDERED_) || (ai->ranking <= 4 && me->ranking >= 6) ||
                ((status(faction, target) & TREATY_WANT_REVENGE) && t->ranking > me->ranking) ||
                (t->ranking > me->ranking && t->ranking > ai->ranking)) {
                say_title(0, target), say_name(1, target), say_noun(2, target);
                game_parse_says(3, game_get_pact(faction), -1, -1);
                answer(text(0x68dca8), other);
                net_double_cross(other, target, faction);
                extorted = 1;
            }
        }
        // Fear: the player's strength against the target's, as the AI
        // sees it (the products wrap as the original's do).
        fear = *game_diplo_fear;
        int strength = F(target)->mil_strength;
        if (fear && !(status(other, target) & TREATY_PACT) &&
            ((status(faction, other) & TREATY_TREATY) || !(status(other, target) & TREATY_TREATY)) &&
            me->mil_strength * fear > strength * strength / (ai->mil_strength + 1)) {
            say_title(0, target), say_name(1, target), say_noun(2, target);
            answer(text(0x68dcb0), other);
            net_double_cross(other, target, faction);
            extorted = 1;
        }
    }
    if (value && *game_diplo_proposal == PROPOSE_ENERGY) {
        say_title(0, faction), say_name(1, faction);
        game_parse_num(0, value);
        answer(text(0x68dcbc), other);
        net_energy(faction, value, other);
        energy_shown();
        extorted = 1;
    }
    if (*game_diplo_fear && *game_diplo_proposal == PROPOSE_COMMLINK) {
        game_parse_says(0, game_faction_defs[*game_diplo_proposal_faction].adjective, -1, -1);
        answer(text(0x68dccc), other);
        net_tech(faction, *game_diplo_proposal_faction + TECH_COMMLINK_0, other);
        extorted = 1;
    }
    if (*game_trade_offer_tech[0] >= 0 && *game_diplo_fear && *game_diplo_proposal == PROPOSE_TECH) {
        say_title(0, faction);
        game_str_buffer[0] = 0;
        say_tech(game_str_buffer, *game_trade_offer_tech[0], 1);
        game_parse_says(1, game_str_buffer, -1, -1);
        answer(text(0x68dcdc), other);
        net_tech(faction, *game_trade_offer_tech[0], other);
        *game_trade_offer_tech[0] = -1;
        extorted = 1;
    }
    if (*game_diplo_proposal == PROPOSE_BASE_SWAP) {
        int base = *game_diplo_ask_base;
        if (status(other, faction) & TREATY_PACT) {
            const struct base *B = &game_bases[base];
            int region = region_at(B->x, B->y);
            if (!(status(other, faction) & TREATY_SURRENDERED_) || ai->base_count <= 1 ||
                ((me->diplo_agenda[other] & AGENDA_UNK_1000) && B->faction_former != faction &&
                 (me->region_total_bases[region] < ai->region_total_bases[region] ||
                  ai->region_total_bases[region] > ai->base_count / 2))) {
                say_title(0, faction), say_name(1, faction);
                answer(text(0x68dce8), other);
                return;
            }
            net_agenda_on(faction, other, AGENDA_UNK_1000);
        }
        int hostile = *game_diplo_prev_hostility || *game_diplo_attack_chance;
        int worth = value_of_base(*game_diplo_ask_base, other, faction, *game_diplo_fear, hostile);
        if (worth > 0 && worth < *game_diplo_fear * 25) {
            lock();
            game_parse_says(0, game_bases[*game_diplo_ask_base].name, -1, -1);
            net_cede_base(*game_diplo_ask_base, faction);
            answer(text(0x68dcf4), other);
            if (!(status(other, faction) & TREATY_SURRENDERED_)) net_set_treaty(other, faction, TREATY_WANT_REVENGE, 1);
            unlock();
            extorted = 1;
        }
    }
    if (extorted) {
        if (!(status(faction, other) & TREATY_TREATY)) net_treaty_on(other, faction, TREATY_TRUCE_);
        return;
    }
    if (status(other, faction) & TREATY_PACT) {
        if ((*game_diplo_prev_hostility || *game_diplo_great_satan || *game_diplo_friction > 10) &&
            (me->ranking > ai->ranking || me->tech_ranking > ai->tech_ranking) &&
            !(status(other, faction) & TREATY_SURRENDERED_) && me->ranking >= 5) {
            say_title(0, faction), say_name(1, faction);
            game_parse_says(2, game_get_pact(faction), -1, -1);
            answer(text(0x68dcfc), other);
            lock();
            net_pact_ends(faction, other);
            unlock();
        }
        say_name(0, faction);
        answer(text(0x68dd04), other);
    } else if (*game_diplo_friction > (*game_diplo_prev_hostility ? 5 : 15) && !*game_diplo_fear &&
               (ai->best_weapon_value >= me->best_weapon_value || *game_diplo_attack_chance ||
                *game_diplo_exposed_bases) &&
               ai->mil_strength > (me->diff_level / 2 + me->integrity_blemishes + 4) * me->mil_strength / 4) {
        net_treaty_on(faction, other, TREATY_VENDETTA);
        me->diplo_spoke[other] = *game_turn;
        game_diplomacy_caption(faction, other);
        char *label = label_random(0x68dd0c);
        say_title(0, faction), say_name(1, faction);
        game_parse_says(2, game_get_noun_phrase(faction, 8), -1, -1);
        answer(label, other);
    } else {
        say_title(0, faction), say_name(1, faction);
        answer(text(0x68dd18), other);
    }
}

// ---- energy_trade --------------------------------------------------------------------

static int is_faction(int faction, uint32_t file) { return !strcmp(game_faction_defs[faction].filename, text(file)); }

// The player (`faction`) asks the AI (`other`) for energy, offering
// game_diplo_counter in return; 1 if energy changed hands. A richer AI
// ranked two or more above the player in a pact, ahead in techs, with
// more than four times the turn in reserves and patience below 2, gives
// it (unless research was wanted from it, over friendship): the rounded-up
// eighth of its reserves, 1 to 500, or, the original's way, all of its
// reserves when they are more. For research or a price, the AI buys the
// player's best tech (game_trade_want_tech) at its value to the AI times
// 32 for each eight turns over the factions that have the rival's best
// tech (game_trade_offer_tech; three quarters of that against a great
// satan), at most half its reserves, in 25s (at least 25): unless it is
// the faction that buys none (the Believers). The player may sell the
// second tech instead, which one faction (the Hive) never takes and the
// others only for less than it is worth, but for one (the University).
// For a loan or friendship (or a price without techs to sell), the AI
// lends a part of its reserves, by their commerce, with a treaty and no
// loans between them nor grudges, if it is richer than the player but
// for one faction (Morgan), at an interest from the reputation and
// commerce, over as many turns as it likes (10 to the total); otherwise
// it refuses.
//
// Thinker's mod_energy_trade() replaces all of this with its own reckoning.
int energy_trade(int faction, int other) {
    struct faction_data *me = F(faction), *ai = F(other);
    *game_diplo_second = other;
    tech_analysis(faction, other);
    int counter = *game_diplo_counter;
    int research = counter == COUNTER_RESEARCH || counter == COUNTER_NAME_A_PRICE;
    if ((status(other, faction) & TREATY_PACT) && counter != COUNTER_LOAN) {
        if (ai->ranking > me->ranking + 1 && ai->energy_credits > 2 * me->energy_credits &&
            ai->tech_ranking > me->tech_ranking && ai->energy_credits > *game_turn * 4 &&
            ai->diplo_patience[faction] < 2 && (*game_trade_want_tech[0] < 0 || counter == COUNTER_FRIENDSHIP)) {
            int value = clamp(ai->energy_credits / 8, 1, 500);
            value = (value + 24) / 25 * 25;
            if (value < ai->energy_credits) value = ai->energy_credits;
            net_energy(other, -value, faction);
            ai->diplo_patience[faction] += 2;
            answer(label_random(0x68d74c), other);
            return 0;
        }
    }
    if (counter == COUNTER_LOAN || counter == COUNTER_FRIENDSHIP ||
        (counter == COUNTER_NAME_A_PRICE && *game_trade_want_tech[0] < 0))
        goto loan;
    if (!research) goto refuse;
    say_title(0, faction), say_name(1, faction);
    if (*game_trade_want_tech[0] < 0 || is_faction(other, 0x68d77c)) {
        answer(text(0x68d784), other);
        return 0;
    }
    int rate = (*game_turn + 7) / 8 * 32;
    if (great_satan(faction, 0)) rate = rate * 3 / 4;
    int owners = game_bit_count(game_tech_achieved[*game_trade_offer_tech[0]]) - 1;
    rate /= clamp(owners, 1, 99);
    int price = tech_val(*game_trade_want_tech[0], other, 1) * rate / 32;
    int second = *game_trade_want_tech[1] < 0 ? 0 : tech_val(*game_trade_want_tech[1], other, 1) * rate / 32;
    int half = ai->energy_credits / 2;
    if (price < 0 || half < 0) price = 0;
    else if (price > half) price = half;
    price = price / 25 * 25;
    if (price < 25) price = 25;
    if (second < 25) second = 25;
    if (!price) {  // never: the price is at least 25
        answer(text(0x68d790), other);
        return 0;
    }
    for (;;) {
        game_str_buffer[0] = 0;
        say_tech(game_str_buffer, *game_trade_want_tech[0], 1);
        game_parse_says(0, game_str_buffer, -1, -1);
        if (*game_trade_want_tech[1] >= 0) {
            game_str_buffer[0] = 0;
            say_tech(game_str_buffer, *game_trade_want_tech[1], 1);
            game_parse_says(1, game_str_buffer, -1, -1);
        }
        game_parse_num(0, price);
        char *label;
        if (*game_trade_want_tech[1] >= 0) {
            game_str_buffer[0] = 0;
            strcat(game_str_buffer, text(0x68d7a0));
            strcat(game_str_buffer, text(0x68d7ac));
            label = game_str_buffer;
        } else {
            label = label_random(0x68d7a0);
        }
        int choice = answer(label, other);
        if (!choice) return 0;
        if (choice == 2 && *game_trade_want_tech[1] >= 0) {
            // The second tech for the same price.
            if (is_faction(other, 0x68d7b0) || (price < second && !is_faction(other, 0x68d7b8))) {
                answer(text(0x68d7cc), other);
                return 0;
            }
            answer(text(0x68d7c0), other);
            net_tech_nowait(other, *game_trade_want_tech[1], faction);
            break;
        }
        if (choice == 2 || choice == 3) {
            game_help_topic(14, *game_trade_want_tech[0]);
            continue;
        }
        net_tech_nowait(other, *game_trade_want_tech[0], faction);
        *game_trade_want_tech[0] = *game_trade_want_tech[1];
        break;
    }
    *game_trade_want_tech[1] = -1;
    net_energy(other, -price, faction);
    energy_shown();
    return 1;

loan:;
    int amount;
    if (ai->tech_commerce_bonus < me->tech_commerce_bonus) amount = ai->energy_credits / 4;
    else if (ai->tech_commerce_bonus == me->tech_commerce_bonus) amount = ai->energy_credits / 3;
    else amount = ai->energy_credits / 2;
    if (ai->energy_credits < me->energy_credits && !is_faction(other, 0x68d75c)) amount = 0;
    if (me->loan_balance[other] || ai->loan_balance[faction]) amount = 0;
    uint32_t st = status(other, faction);
    if (st & (TREATY_VENDETTA | TREATY_WANT_REVENGE)) amount = 0;
    if (!(st & TREATY_TREATY) || amount < 10) amount = 0;
    int interest = (*game_diplo_reputation + 2 * (ai->tech_commerce_bonus - me->tech_commerce_bonus) + 10) * 5;
    if (status(faction, other) & TREATY_PACT) {
        interest -= 10;
        if (ai->ranking > me->ranking) interest -= 10;
    }
    if (interest < 0) interest = 0;
    int turns = (10 - *game_diplo_reputation) * 10;
    if (turns < 20) turns = 20;
    if (!amount) goto refuse;
    int total = (interest + 100) * amount / 100;
    if (turns < 10 || total < 10) turns = 10;
    else if (turns > total) turns = total;
    int payment = (total + turns - 1) / turns, balance = payment * turns;
    game_parse_num(0, amount);
    game_parse_num(1, payment);
    game_parse_num(2, turns);
    say_title(0, faction), say_name(1, faction);
    {
        char digit[12];
        game_str_buffer[0] = 0;
        strcat(game_str_buffer, text(0x68d764));
        snprintf(digit, sizeof(digit), "%d", (payment != 1) + 1);
        strcat(game_str_buffer, digit);
    }
    if (!answer(game_str_buffer, other)) return 0;
    if (*game_net_multiplayer) {
        game_log_say(text(0x68d4cc), faction, other, balance);
        game_message_data(0x244e, 0, faction, other, balance, payment);
    } else {
        me->loan_balance[other] = balance, me->loan_payment[other] = payment;
    }
    net_energy(other, -amount, faction);
    return 1;

refuse:
    answer(label_random(0x68d770), other);
    return 0;
}

// ---- propose_pact --------------------------------------------------------------------

// What the AI (`other`) asks of the player (`faction`) for a deal: energy
// in 50s, from the player's reserves plus 50 for each point of the
// commerce lead and the reputation in talks, in the share of their
// strength the AI holds; brought down below the player's reserves unless
// energy is what the player offers.
static int energy_asked(int faction, int other) {
    struct faction_data *me = F(faction), *ai = F(other);
    int x = me->tech_commerce_bonus - ai->tech_commerce_bonus + *game_diplo_reputation;
    int ask = (me->energy_credits + 50 * x) * ai->mil_strength / (me->mil_strength + ai->mil_strength + 1) / 50 * 50;
    if (*game_diplo_counter != COUNTER_ENERGY && ask) {
        while (ask > me->energy_credits) {
            ask -= 50;
            if (ask < 0) {
                ask = 0;
                break;
            }
            if (ask > 99999) ask = 99999;
            else if (!ask) break;
        }
    }
    return ask;
}

// The player (`faction`) proposes a pact to the AI (`other`), offering
// game_diplo_counter; 1 when the talk is settled, 0 when a threat (the
// counter) or nothing more follows. The AI weighs its pacts and the
// player's, the rankings (and in a network game the leaders' scores), the
// rules, a faction it wants help against (its strength against the AI's),
// the friction, reputations, commerce, fear, hostility and the great
// satan, against its goodwill (or the player's level). A threat from a
// player that beat it brings its surrender. Refusing, it says why. Willing,
// it may ask for techs (two for the player's best, the player's choice,
// with the Datalinks open to it) or energy (energy_asked()) first, more of
// a great satan or a player with pacts; then for help against its enemy,
// which the player may give by turning on it; then the pact
// (pact_of_brotherhood()). Some factions take no techs (the Believers) or
// energy (the Gaians), or no techs when the energy will do (Morgan).
int propose_pact(int faction, int other) {
    struct faction_data *me = F(faction), *ai = F(other);
    int player_pacts = *game_diplo_player_pacts;
    int score = player_pacts + *game_diplo_rival_pacts;
    if (player_pacts) score++;
    *game_diplo_second = other;
    int help = *game_diplo_help_faction ? *game_diplo_help_faction : *game_diplo_accepted_faction;
    int weaker = 0;  // the AI is weaker than the faction it wants help against
    int32_t *ranked = game_ranked_faction, *scores = game_ranking_scores;
    if (*game_net_multiplayer) {
        if (faction == ranked[0] && (F(ranked[1])->ranking < 6 || 2 * scores[faction] > 3 * scores[ranked[1]])) score++;
        if (faction == ranked[1] &&
            (me->ranking < 6 || 2 * scores[game_faction_rankings[7]] > 3 * scores[faction]))
            score--;
    }
    if ((*game_rules & RULES_INTENSE_RIVALRY) && faction == ranked[0]) score++;
    if ((*game_rules & RULES_VICTORY_COOPERATIVE) && !(*game_rules & RULES_INTENSE_RIVALRY)) score--;
    if (ai->flags & FACTION_FLAG_COOP_WITH_HUMAN_) score--;
    if (me->ranking >= 6) {
        if (!*game_diplo_runaway_leader && ai->ranking >= 5 + (me->ranking != 7)) {
            score++;
            if (*game_turn >= (*game_net_multiplayer ? 50 : 100)) score++;
        } else if (ai->ranking >= 4 && *game_turn >= 50 && !*game_diplo_runaway_leader) {
            if (*game_diplo_rival_strong_pacts) score++;
            else if (!player_pacts) {
                score--;
                goto strong_done;
            } else {
                goto strong_done;
            }
        }
    }
    if (*game_diplo_rival_strong_pacts) score++;
strong_done:
    if (help) {
        score--;
        if (great_satan(help, 0)) score--;
        if (F(help)->ranking > ai->ranking) score--;
        if ((status(other, help) & TREATY_UNK_8000) && ai->mil_strength < F(help)->mil_strength) {
            weaker = 1;
            score -= (ai->mil_strength / 2 + F(help)->mil_strength) / (ai->mil_strength + 1);
        }
    }
    if (*game_diplo_friction >= 10 && *game_diplo_counter != COUNTER_THREATEN) score += *game_diplo_friction - 10;
    if (me->diplo_reputation >= 2 && !*game_diplo_reputation) score--;
    if (me->diplo_reputation <= 1) score++;
    if (ai->tech_commerce_bonus <= me->tech_commerce_bonus) score++;
    if (ai->tech_commerce_bonus > me->tech_commerce_bonus) score--;
    if (!*game_net_multiplayer && is_faction(other, 0x68d8dc)) score--;
    if (*game_diplo_fear) score--;
    if (*game_diplo_prev_hostility) score++;
    if (*game_diplo_great_satan) score++;
    if (ai->ranking > me->ranking) {
        if (ai->ranking == 7) score += 2;
        if (ai->ranking == 6) score++;
        if (me->ranking <= 4) score--;
    } else {
        if (me->ranking == 7) score += player_pacts;
        if (me->ranking - ai->ranking >= 3) score--;
        if (great_satan(faction, 0)) score++;
    }
    if (me->ranking <= 5 && ai->ranking <= 5) score--;
    int accept = 0;
    if ((*game_diplo_goodwill > score || !me->diff_level) && !(me->diplo_agenda[other] & AGENDA_UNK_2000)) {
        int limit = (me->diff_level ? 0 : 2) + (*game_rules & RULES_VICTORY_COOPERATIVE ? 1 : 0) + (weaker ? 2 : 0) + 2;
        if (*game_diplo_reputation <= limit && !(status(other, faction) & TREATY_WANT_REVENGE))
            accept = !*game_diplo_can_betray;
    }
    game_diplomacy_caption(faction, other);
    game_parse_says(0, game_get_pact(faction), -1, -1);
    say_title(1, faction), say_name(2, faction);

    if (*game_diplo_counter == COUNTER_THREATEN) {
        if (*game_diplo_can_surrender) {
            // The AI surrenders.
            if (*game_net_multiplayer)
                net_post(0x68d46c, other, faction, TREATY_SURRENDERED_, 0x443, other, faction, TREATY_SURRENDERED_, 1);
            else
                set_treaty(other, faction, TREATY_SURRENDERED_, 1);
            game_pact_of_brotherhood(faction, other);
            return 1;
        }
    } else if (accept) {
        goto willing;
    }
    // Refused.
    {
        int rep = *game_diplo_reputation, captured = *game_diplo_captured_bases;
        if ((rep > 2 || captured) && (*game_diplo_fear || *game_diplo_friction <= 2) &&
            !(status(other, faction) & TREATY_WANT_REVENGE)) {
            answer(text(0x68d8e4), other);
            return 1;
        }
        if (*game_diplo_counter == COUNTER_THREATEN) return 0;
        if (!(status(other, faction) & TREATY_WANT_REVENGE) && rep <= 2 && !captured) {
            if (great_satan(faction, 0) || *game_diplo_great_satan) {
                game_parse_says(3, game_get_phrase(faction, 3), -1, -1);
                answer(label_random(0x68d8fc), other);
                return 1;
            }
            game_parse_says(3, game_get_noun_phrase(other, 9), -1, -1);
            answer(text(ai->ranking > me->ranking || *game_diplo_friction > 10 ? 0x68d90c : 0x68d91c), other);
            return 1;
        }
        game_parse_says(3, game_get_phrase(faction, 5), -1, -1);
        answer(text(0x68d8f0), other);
        ai->diplo_patience[faction]++;
        return 1;
    }

willing:;
    int ask = energy_asked(faction, other);
    int want_energy = 0, want_tech = 0, deal = 0, shown = 0, counter = *game_diplo_counter;
    if (!weaker)
        want_energy = (*game_diplo_friction > 4 && me->tech_commerce_bonus >= ai->tech_commerce_bonus) ||
                      *game_diplo_friction > 10 ||
                      (*game_diplo_reputation && *game_diplo_reputation > ai->tech_commerce_bonus - me->tech_commerce_bonus);
    if (counter == COUNTER_RESEARCH || counter == COUNTER_ENERGY) want_tech = 1;
    if (counter == COUNTER_NAME_A_PRICE) {
        if (!weaker && !*game_diplo_help_faction && (*game_trade_want_tech[0] >= 0 || ask)) want_tech = 1;
        if (*game_trade_want_tech[0] < 0 && !ask) want_energy = 0;
    }
    if (great_satan(faction, 0) || player_pacts > 1 ||
        (*game_net_multiplayer && faction == ranked[0] && (player_pacts || ai->ranking > F(ranked[1])->ranking))) {
        // More of a great satan, or a player with pacts.
        ask = (2 * ask > *game_turn * 5 ? 2 * ask : *game_turn * 5) / 50 * 50;
        if (me->energy_credits >= ask ? *game_trade_want_tech[1] < 0 && counter == COUNTER_RESEARCH
                                      : counter == COUNTER_ENERGY || *game_trade_want_tech[1] < 0) {
            game_parse_says(3, game_get_noun_phrase(other, 9), -1, -1);
            answer(text(0x68d924), other);
            return 1;
        }
    } else {
        *game_trade_want_tech[1] = -1;
    }
    if (!want_energy && !want_tech) goto pact;
    // Techs.
    if (*game_trade_want_tech[0] >= 0 && !is_faction(other, 0x68d92c) &&
        !(is_faction(other, 0x68d934) && ask && me->energy_credits >= ask) &&
        (counter == COUNTER_RESEARCH || (counter == COUNTER_FRIENDSHIP && *game_diplo_friction < 4) ||
         counter == COUNTER_NAME_A_PRICE)) {
        shown = 1;
        int choice;
        for (;;) {
            say_title(1, other);
            game_str_buffer[0] = 0;
            say_tech(game_str_buffer, *game_trade_want_tech[0], 1);
            game_parse_says(2, game_str_buffer, -1, -1);
            game_parse_says(3, game_get_noun_phrase(other, 9), -1, -1);
            if (*game_trade_want_tech[1] >= 0) {
                game_str_buffer[0] = 0;
                say_tech(game_str_buffer, *game_trade_want_tech[1], 1);
            }
            char digit[12];
            game_str_buffer[0] = 0;
            strcat(game_str_buffer, text(0x68d93c));
            snprintf(digit, sizeof(digit), "%d", *game_trade_want_tech[1] >= 0 ? 2 : game_random(0, 2));
            strcat(game_str_buffer, digit);
            choice = answer(game_str_buffer, other);
            if (choice <= 1) break;
            game_help_topic(14, *game_trade_want_tech[0]);
        }
        if (choice) {
            deal = 1;
            net_tech(other, *game_trade_want_tech[0], faction);
            if (*game_trade_want_tech[1] >= 0) net_tech(other, *game_trade_want_tech[1], faction);
            cause_friction(other, faction, -(tech_val(*game_trade_want_tech[0], other, 1) / 2));
            game_diplomacy_caption(faction, other);
            *game_trade_want_tech[1] = *game_trade_want_tech[0] = -1;
        }
    }
    // Energy.
    counter = *game_diplo_counter;
    if ((want_energy || want_tech) && ask && me->energy_credits >= ask && !shown && !is_faction(other, 0x68d948) &&
        (counter == COUNTER_ENERGY || (counter == COUNTER_FRIENDSHIP && *game_diplo_friction < 4) ||
         counter == COUNTER_NAME_A_PRICE)) {
        shown = 1;
        game_parse_num(0, ask);
        say_title(1, other);
        if (answer(label_random(0x68d950), other)) {
            net_energy(faction, -ask, other);
            deal = 1;
            cause_friction(other, faction, -energy_value(ask));
            energy_shown();
            game_diplomacy_caption(faction, other);
        }
    }
    if ((want_energy || shown) && !deal) {
        if (shown) {
            answer(text(0x68d96c), other);
            return 1;
        }
        if (*game_diplo_counter == COUNTER_THREATEN) return 0;
        answer(text(0x68d95c), other);
        return 1;
    }

pact:
    if (help && !*game_diplo_accepted_faction) {
        // Help against its enemy.
        say_title(5, faction), say_noun(6, help);
        if (answer(text(0x68d97c), other)) {
            lock();
            if (*game_net_multiplayer) {
                net_post(0x68d53c, faction, help, other, 0x440, faction, help, other, 0);
                net_post(0x68d458, faction, other, 0x800000, 0x442, faction, other, 0x800000, 0);
            } else {
                double_cross(faction, help, other);
                treaty_off(faction, other, 0x800000);
            }
            net_set_treaty(other, help, TREATY_UNK_40, 1);
            unlock();
            *game_diplo_help_faction = 0, *game_diplo_enemy_allies = 0;
        } else if (weaker || (game_faction_rankings[7] == faction && *game_diplo_alter_fear >= 0) ||
                   *game_diplo_reputation > 2 || *game_diplo_alter_fear > 0) {
            ai->diplo_patience[faction]++;
            if (!deal) {
                game_parse_says(2, game_get_pacts_2(faction, other), -1, -1);
                answer(text(0x68d988), other);
                return 1;
            }
        }
    }
    game_pact_of_brotherhood(faction, other);
    return 1;
}

// ---- propose_treaty ------------------------------------------------------------------

// The player (`faction`) proposes a treaty to the AI (`other`), offering
// game_diplo_counter; 1 when the talk is settled, 0 when a threat or
// nothing more follows. Defiance (game_diplo_alter_fear below 0, twice
// for one faction, the Spartans) and a lone low-ranked AI make it readier;
// it agrees unless the friction is high (more allowed with a deal on
// offer), it has sworn revenge with a poor reputation, or it is weaker
// than an enemy it wants help against (which makes it agree, unless it
// has 0x800 on its agenda); otherwise by reputation, the player's level,
// captured bases and friction; one that cooperates with humans always
// does (bar revenge), and never with the player's 0x2000 on its agenda.
// Willing, it may ask for the player's best tech (a refusal ends the
// talk) or energy (less 50 for each point of its commerce lead; one
// faction, Morgan, takes all the player's reserves if the tech is worth
// more to them), then help against its enemy, then makes the treaty
// (make_treaty()).
int propose_treaty(int faction, int other) {
    struct faction_data *me = F(faction), *ai = F(other);
    *game_diplo_second = other;
    int defiant = *game_diplo_alter_fear < 0;
    int bonus = defiant + (is_faction(other, 0x68d998) && defiant);
    int help = *game_diplo_help_faction ? *game_diplo_help_faction : *game_diplo_accepted_faction;
    int weaker = help && (status(other, help) & TREATY_UNK_8000) && F(help)->mil_strength > ai->mil_strength &&
                 !(ai->diplo_agenda[faction] & AGENDA_UNK_800);
    int counter = *game_diplo_counter;
    int deal = counter == COUNTER_NAME_A_PRICE || counter == COUNTER_RESEARCH || counter == COUNTER_ENERGY ||
               counter == COUNTER_BASE;
    int accept = 0;
    if (!*game_diplo_great_satan && !*game_diplo_rival_strong_pacts && ai->ranking <= 4 && me->ranking >= 5 &&
        !*game_diplo_player_pacts)
        bonus++;
    int rep = *game_diplo_reputation;
    if (*game_diplo_friction > (deal ? bonus + 2 : 0) + 15 || (ai->diplo_agenda[faction] & AGENDA_UNK_400)) goto decided;
    if ((status(other, faction) & TREATY_WANT_REVENGE) &&
        (rep > 6 || rep > ai->tech_commerce_bonus - me->tech_commerce_bonus + (deal ? 2 : 0) - *game_diplo_alter_fear + 2))
        goto decided;
    if (!weaker) {
        if (*game_diplo_great_satan) goto decided;
        int level = clamp(me->diff_level, 0, 2);
        if (rep > (deal ? 2 : 0) - level + bonus + 5) goto decided;
        if (*game_diplo_captured_bases > deal + bonus + (counter == COUNTER_THREATEN ? 2 : 0) + 4) goto decided;
        if (*game_diplo_friction > deal * 5 + 10 + bonus * 3 && counter != COUNTER_THREATEN) goto decided;
    }
    accept = 1;
decided:
    if ((ai->flags & FACTION_FLAG_COOP_WITH_HUMAN_) && !(status(other, faction) & TREATY_WANT_REVENGE)) accept = 1;
    if (me->diplo_agenda[other] & AGENDA_UNK_2000) accept = 0;
    struct faction_def *d = &game_faction_defs[faction];
    say_name(0, faction), say_title(1, faction), say_noun(2, faction);
    game_parse_says(3, d->adjective, -1, -1);
    if (!accept) {
        rep = *game_diplo_reputation;
        if (rep > 4 && (*game_diplo_fear || *game_diplo_friction <= 2) && !(status(other, faction) & TREATY_WANT_REVENGE)) {
            answer(text(0x68d9a0), other);
            return 1;
        }
        if (*game_diplo_counter == COUNTER_THREATEN) return 0;
        if (!(status(other, faction) & TREATY_WANT_REVENGE) && rep <= 4) {
            if (*game_diplo_great_satan) {
                game_parse_says(4, game_get_noun_phrase(other, 9), -1, -1);
                game_parse_says(5, game_get_phrase(faction, 3), -1, -1);
                answer(text(0x68d9c0), other);
            } else {
                answer(text(0x68d9d0), other);
            }
            return 1;
        }
        *game_parse_plural = 0, *game_parse_gender = d->leader_female;
        game_parse_says(4, (char *)d + 0x3ac, -1, -1);
        answer(text(0x68d9b0), other);
        return 1;
    }

    // Energy asked: from the player's reserves and the reputation, in the
    // share of their strength the AI holds, in 50s.
    int ask = (me->energy_credits + *game_diplo_reputation) * ai->mil_strength /
              (me->mil_strength + ai->mil_strength + 1) / 50 * 50;
    if (!(status(other, faction) & TREATY_WANT_REVENGE))
        ask -= 50 * clamp(ai->tech_commerce_bonus - me->tech_commerce_bonus, 0, 99);
    if (is_faction(other, 0x68d9dc) && ask > me->energy_credits) {
        if (*game_trade_want_tech[0] < 0) {
            ask = me->energy_credits;
        } else {
            int reserves = me->energy_credits;
            if (reserves >= tech_val(*game_trade_want_tech[0], faction, 1) * ((*game_turn + 7) / 8)) ask = reserves;
        }
    }
    if (*game_diplo_counter != COUNTER_ENERGY && ask > me->energy_credits)
        ask -= 50 * (int)((uint32_t)(ask - me->energy_credits + 49) / 50);
    if (ask < 50) ask = 0;
    int want_energy = 0, want_tech = 0, shown = 0, done = 0;
    int friction = *game_diplo_friction, lead = clamp(ai->tech_commerce_bonus - me->tech_commerce_bonus, 0, 99);
    if ((!weaker && friction > 2 * bonus + 10 && me->tech_commerce_bonus >= ai->tech_commerce_bonus) || friction > 15 ||
        (status(other, faction) & TREATY_WANT_REVENGE) || *game_diplo_reputation > lead + 2)
        want_energy = 1;
    counter = *game_diplo_counter;
    if (counter == COUNTER_ENERGY || counter == COUNTER_RESEARCH) want_tech = 1;
    if (counter == COUNTER_NAME_A_PRICE) want_tech = 1, want_energy = 0;
    else if (counter == COUNTER_THREATEN) want_energy = 0;
    if (!want_energy && !want_tech) goto treaty;
    // A tech.
    if (*game_trade_want_tech[0] >= 0 && !is_faction(other, 0x68d9e4) &&
        !(is_faction(other, 0x68d9ec) && ask && me->energy_credits >= ask &&
          ask >= tech_val(*game_trade_want_tech[0], faction, 1) * 100) &&
        (counter == COUNTER_RESEARCH || counter == COUNTER_NAME_A_PRICE ||
         (counter == COUNTER_FRIENDSHIP && *game_diplo_friction < 10))) {
        shown = 1;
        int choice;
        for (;;) {
            say_title(1, other);
            game_str_buffer[0] = 0;
            say_tech(game_str_buffer, *game_trade_want_tech[0], 1);
            game_parse_says(2, game_str_buffer, -1, -1);
            game_parse_says(3, game_get_noun_phrase(other, 9), -1, -1);
            choice = answer(label_random(0x68d9f4), other);
            if (choice <= 1) break;
            game_help_topic(14, *game_trade_want_tech[0]);
        }
        if (!choice) {
            accept = 0;  // a refusal ends the talk
        } else {
            done = 1;
            net_tech(other, *game_trade_want_tech[0], faction);
            cause_friction(other, faction, -(tech_val(*game_trade_want_tech[0], faction, 1) / 2));
            game_diplomacy_caption(faction, other);
            *game_trade_want_tech[0] = *game_trade_want_tech[1];
            *game_trade_want_tech[1] = -1;
        }
    }
    // Energy.
    counter = *game_diplo_counter;
    if ((want_energy || want_tech) && ask && me->energy_credits >= ask && !shown && !is_faction(other, 0x68da00) &&
        (counter == COUNTER_ENERGY || counter == COUNTER_NAME_A_PRICE ||
         (counter == COUNTER_FRIENDSHIP && *game_diplo_friction < 10))) {
        shown = 1;
        game_parse_num(0, ask);
        say_title(1, other);
        if (!answer(label_random(0x68da08), other)) {
            answer(text(0x68da28), other);
            return 1;
        }
        net_energy(faction, -ask, other);
        done = 1;
        cause_friction(other, faction, -energy_value(ask));
        game_diplomacy_caption(faction, other);
        energy_shown();
    }
    if (!accept) {
        if (shown) {
            answer(text(0x68da28), other);
            return 1;
        }
    } else if (!want_energy || shown) {
        goto treaty;
    }
    if (*game_diplo_counter == COUNTER_FRIENDSHIP) return 0;
    answer(text(0x68da14), other);
    return 1;

treaty:
    counter = *game_diplo_counter;
    if ((counter == COUNTER_RESEARCH || counter == COUNTER_ENERGY) && done) *game_diplo_help_faction = 0;
    if (help && !*game_diplo_accepted_faction) {
        // Help against its enemy.
        say_title(5, faction), say_noun(6, help);
        if (answer(text(0x68da3c), other)) {
            lock();
            if (*game_net_multiplayer) {
                net_post(0x68d458, faction, other, 0x800000, 0x442, faction, other, 0x800000, 0);
                net_send(0x68d53c, faction, help, other, 0x440, faction, help, other, 0);
            } else {
                treaty_off(faction, other, 0x800000);
                double_cross(faction, help, other);
            }
            unlock();
            *game_diplo_help_faction = 0, *game_diplo_enemy_allies = 0;
        } else if ((weaker || game_faction_rankings[7] == faction || *game_diplo_reputation > 3 ||
                    *game_diplo_counter == COUNTER_FRIENDSHIP) &&
                   !done && !is_faction(other, 0x68da48)) {
            answer(text(0x68da50), other);
            ai->diplo_patience[faction]++;
            return 1;
        }
    }
    game_make_treaty(faction, other);
    return 1;
}

// ---- make_gift -----------------------------------------------------------------------

// The popup the originals keep on their stacks (as council.c's), and its
// parts.
enum { POP_DIALOGS = 0x21d0, POP_SPRITE_BOX = 0x2240, POP_ICON = 0x2144, POP_HAS_ICON = 0x2274 };
// The popup destroyed as the originals inline it.
static void pop_end(uint8_t *pop) {
    *(uint32_t *)pop = game_va(0x6695c8);
    *(uint32_t *)(pop + 0x444) = game_va(0x6695c0);
    game_popup_close(pop);
    *(uint32_t *)(pop + 0x3230) = game_va(0x669d58);
    *(uint32_t *)(pop + 0x3674) = game_va(0x669d50);
    game_scroll_close(pop + 0x3230);
    game_flat_button_dtor(pop + 0x4828);
    game_flat_button_dtor(pop + 0x3cdc);
    game_graphic_win_dtor(pop + 0x3230);
    game_base_pop_dtor(pop);
}
// A menu with the AI's portrait.
static void pop_start(uint8_t *pop, uint32_t label, int other) {
    game_popup_start(pop, game_pop_file, text(label), -1, 0, 0x100000, 0);
    void *portrait = game_faction_portraits[other];
    *(void **)(pop + POP_ICON) = *(int32_t *)(pop + POP_HAS_ICON) ? NULL : portrait;
    game_sprite_box_sprite(pop + POP_SPRITE_BOX, portrait, 0, 0);
}

// A faction's search key (faction_def's, beside the file name), as some
// of the talks tell factions apart.
static int is_key(int faction, uint32_t key) { return !strcmp(game_faction_defs[faction].search_key, text(key)); }

static void net_treaty_off(int faction, int other, uint32_t bits) {
    if (*game_net_multiplayer) net_send(0x68d458, faction, other, (int)bits, 0x442, faction, other, (int)bits, 0);
    else treaty_off(faction, other, bits);
}
// The same without waiting for the other players (the base gift's).
static void net_treaty_off_nowait(int faction, int other, uint32_t bits) {
    if (*game_net_multiplayer) net_post(0x68d458, faction, other, (int)bits, 0x442, faction, other, (int)bits, 0);
    else treaty_off(faction, other, bits);
}

// A gift counted against the giver's broken treaties (diplo_gifts): first
// (with `first`) once more if fewer than them, then once more if still
// fewer, or for a great satan, or in a network game.
static void gift_counted(int faction, int other, int first) {
    struct faction_data *me = F(faction);
    int32_t *gifts = &me->diplo_gifts[other];
    if (first && *gifts < me->integrity_blemishes) ++*gifts;
    if (*gifts < me->integrity_blemishes || great_satan(faction, 0) || *game_net_multiplayer) ++*gifts;
}

// The friction an energy gift worth `value` takes off: a fifth of it,
// the first 10 at a half, the next 20 at a third, the next at a quarter
// and so on, plus 4, a fifth again, and half as much more.
static int energy_goodwill(int value) {
    int sum = 0, rest = value / 5, step = 10;
    for (int k = 2; rest > 0; k++) {
        sum += (rest > step ? step : rest) / k;
        rest -= step;
        step = 20;
    }
    return (sum + 4) / -5 * 3 / 2;
}

// The player (`faction`) gives the AI (`other`) a gift, the counter it
// picked (game_diplo_counter):
// - Energy (COUNTER_ENERGY): the player picks an amount in a menu, its
//   reserves and their halves, in 25s, down to 0 (nothing). The AI values
//   it at the amount, twice that from one faction (Morgan), from another
//   (the Spartans) the same (twice, then halved: the original compares the
//   giver's search key, not the AI's). A large gift (half the AI's
//   reserves, a third of the player's and twice the turn) wipes grudges
//   away (treaty bits 0x60) and counts twice towards the player's gifts;
//   a fair one (a quarter of the AI's reserves, 25, a sixth of the
//   player's and the turn) counts once if it is the first. Friction falls
//   (energy_goodwill()), the AI thanks the player and its patience falls
//   by 2, 3 for a large gift.
// - A base (COUNTER_BASE, game_diplo_bid_base): valued with
//   value_of_base() as the AI would gain it; friction falls by one and a
//   half times that, then twice the value from the Spartans makes it
//   large as above (always, for the Believers, by file name). The talks
//   are held (lock()) while it changes hands. The original puts together
//   a thank-you label it never shows, showing BASEGIFT.
// - Research (COUNTER_RESEARCH): after tech_analysis(), the tech the AI
//   wants most, or the second (the player's choice); all research
//   (COUNTER_ALL_RESEARCH): every tech the player has and the AI hasn't.
//   Valued at tech_val() to the AI, twice from the University or the
//   Spartans, half from the Believers (here the AI's search key); 16 or
//   more is large, and here the grudges wiped are the AI's towards the
//   player (treaty_off(other, faction), where the others have the
//   player's). Patience falls by 2.
// Nothing for the AI to want, or the player changing their mind, ends it.
static void make_gift_body(uint8_t *pop, int faction, int other) {
    struct faction_data *me = F(faction), *ai = F(other);
    *game_diplo_second = other;
    int counter = *game_diplo_counter;
    if (counter == COUNTER_RESEARCH || counter == COUNTER_ALL_RESEARCH) {
        tech_analysis(faction, other);
        if (*game_trade_want_tech[0] < 0) {
            say_noun(1, other);
            answer(text(0x68db5c), other);
            return;
        }
        int value = 0;
        if (*game_diplo_counter == COUNTER_ALL_RESEARCH) {
            for (int t = 0; t < 89; t++) {  // the techs, not the commlinks
                if (!has_tech(t, faction) || has_tech(t, other)) continue;
                value += tech_val(t, other, 1);
                net_tech(other, t, faction);
            }
        } else {
            game_str_buffer[0] = 0;
            say_tech(game_str_buffer, *game_trade_want_tech[0], 1);
            game_parse_says(0, game_str_buffer, -1, -1);
            game_parse_says(2, game_get_noun_phrase(other, 9), -1, -1);
            int choice;
            if (*game_trade_want_tech[1] < 0) {
                choice = answer(text(0x68db6c), other);
            } else {
                game_str_buffer[0] = 0;
                say_tech(game_str_buffer, *game_trade_want_tech[1], 1);
                game_parse_says(1, game_str_buffer, -1, -1);
                choice = answer(text(0x68db78), other);
            }
            if (!choice) return;
            int tech = choice == 1 ? *game_trade_want_tech[0] : *game_trade_want_tech[1];
            value = tech_val(tech, other, 1);
            net_tech(other, tech, faction);
        }
        if (is_key(other, 0x68db84) || is_key(other, 0x68db8c)) value *= 2;
        if (is_key(other, 0x68db98)) value /= 2;
        cause_friction(other, faction, -value);
        char label[32] = "";
        strcat(label, text(0x68dba0));
        if (value >= 16) {
            strcat(label, text((status(other, faction) & 0x60) && *game_diplo_friction ? 0x68dbac : 0x68dbb0));
            net_treaty_off(other, faction, 0x60);
            gift_counted(faction, other, 1);
        } else if (!me->diplo_gifts[other]) {
            gift_counted(faction, other, 0);
        }
        game_diplomacy_caption(faction, other);
        answer(label, other);
        ai->diplo_patience[faction] -= 2;
        return;
    }
    if (counter == COUNTER_BASE) {
        int hostile = *game_diplo_prev_hostility || *game_diplo_attack_chance;
        int worth = value_of_base(*game_diplo_bid_base, other, faction, *game_diplo_fear, hostile);
        cause_friction(other, faction, -3 * worth / 2);
        if (is_key(faction, 0x68dbf4)) worth *= 2;
        int large = worth >= ai->energy_credits / 2 && worth >= me->energy_credits / 3 && worth >= 2 * *game_turn;
        if (is_faction(faction, 0x68dc00)) large = 1;
        lock();
        game_diplomacy_caption(faction, other);
        if (large) {
            net_treaty_off_nowait(faction, other, 0x60);
            if (me->diplo_gifts[other] < me->integrity_blemishes) me->diplo_gifts[other]++;
        }
        gift_counted(faction, other, 0);
        say_title(0, faction);
        net_cede_base(*game_diplo_bid_base, other);
        unlock();
        ai->diplo_patience[faction] -= 2 + large;
        answer(text(0x68dc1c), other);
        return;
    }
    if (counter != COUNTER_ENERGY) return;
    pop_start(pop, 0x68dbb4, other);
    int amount = me->energy_credits * 2;
    do {
        amount = amount / 2 / 25 * 25;
        snprintf(game_str_buffer, 16, "%d", amount);
        game_dialogs_item(pop + POP_DIALOGS, game_str_buffer, amount);
    } while (amount);
    int choice = game_base_pop_exec_3(pop, 0, (int)(intptr_t)game_pop_wait);
    if (!choice) return;
    int value = choice;
    if (is_key(faction, 0x68dbc0) || is_key(faction, 0x68dbc8)) value = 2 * choice;
    if (is_key(faction, 0x68dbd4)) value /= 2;
    int large = value >= ai->energy_credits / 2 && value >= me->energy_credits / 3 && value >= 2 * *game_turn;
    int fair = value >= ai->energy_credits / 4 && value >= 25 && value >= me->energy_credits / 6 && value >= *game_turn;
    net_energy(faction, -choice, other);
    energy_shown();
    cause_friction(other, faction, energy_goodwill(value));
    game_str_buffer[0] = 0;
    strcat(game_str_buffer, text(0x68dbe0));
    if (large) {
        strcat(game_str_buffer, text((status(other, faction) & 0x60) || great_satan(faction, 0) ? 0x68dbec : 0x68dbf0));
        net_treaty_off(faction, other, 0x60);
        gift_counted(faction, other, 1);
    } else if (fair && !me->diplo_gifts[other]) {
        gift_counted(faction, other, 0);
    }
    game_diplomacy_caption(faction, other);
    say_title(0, faction), say_name(1, faction);
    answer(game_str_buffer, other);
    ai->diplo_patience[faction] -= 2 + large;
}
void make_gift_in(uint8_t *pop, int faction, int other);
void make_gift_in(uint8_t *pop, int faction, int other) {
    game_popup_ctor(pop);
    make_gift_body(pop, faction, other);
    pop_end(pop);
}
// The popup where the original's frame keeps it.
__asm__(".text\n"
        ".globl make_gift\n"
        "make_gift:\n"
        "    pushl %ebp\n"
        "    movl %esp, %ebp\n"
        "    subl $0x5450, %esp\n"
        "    pushl 12(%ebp)\n"
        "    pushl 8(%ebp)\n"
        "    leal -0x5440(%ebp), %eax\n"
        "    pushl %eax\n"
        "    call make_gift_in\n"
        "    movl %ebp, %esp\n"
        "    popl %ebp\n"
        "    ret\n");

// ---- buy_tech ------------------------------------------------------------------------

// The player (`faction`) offers to buy from the AI (`other`) the tech it
// could give (game_trade_offer_tech[0]): its best prototype for
// PROPOSE_BUY_TECH (scan_prototypes() finds it, as a "tech" 97 on), a
// commlink for PROPOSE_COMMLINK (tech_trade() sets it), else the best tech
// (none from the University while it wants one of the player's). 0 if
// there is nothing to sell, friendship is offered, or the player can't
// pay (unless offering energy, or for a prototype); 1 once the AI has
// named its price. The price: the tech's value to the AI for each eight
// turns, twice for a great satan player and twice in the talks' eyes,
// half as much again when `high`, over the factions that have it but one
// (1 to 999); a quarter, or a half, from a partner ranked above the player
// more than 8 techs ahead, or ahead; then in 25s, rounded up, at least 100
// but for a commlink. The player who can pay may accept (answer 1): the
// energy goes (without waiting for the other players, in a network game)
// and the tech comes.
int buy_tech(int faction, int other, int counter, int high, int proposal) {
    struct faction_data *me = F(faction), *ai = F(other);
    *game_diplo_second = other;
    if (proposal == PROPOSE_BUY_TECH) {
        if (*game_diplo_best_proto < 0) {
            scan_prototypes(faction, other);
            if (*game_diplo_best_proto < 0) return 0;
        }
        *game_trade_offer_tech[0] = *game_diplo_best_proto + 97;
    } else if (proposal != PROPOSE_COMMLINK) {
        if (*game_trade_offer_tech[0] < 0) return 0;
        if (is_faction(other, 0x68d71c) && *game_trade_want_tech[0] >= 0) return 0;
    }
    if (counter == COUNTER_FRIENDSHIP) return 0;
    int price = tech_val(*game_trade_offer_tech[0], other, 1) * ((*game_turn + 7) / 8);
    if (great_satan(faction, 0)) price *= 2;
    if (*game_diplo_great_satan) price *= 2;
    if (high) price = price * 3 / 2;
    price /= clamp(game_bit_count(game_tech_achieved[*game_trade_offer_tech[0]]) - 1, 1, 999);
    if ((status(faction, other) & TREATY_PACT) && ai->ranking > me->ranking) {
        if (ai->tech_ranking > me->tech_ranking + 8) price /= 4;
        else if (ai->tech_ranking > me->tech_ranking) price /= 2;
    }
    price = (price + 25) / 25 * 25;
    if (proposal != PROPOSE_COMMLINK && price < 100) price = 100;
    if (me->energy_credits < price && counter != COUNTER_ENERGY && proposal != PROPOSE_BUY_TECH) return 0;
    game_str_buffer[0] = 0;
    say_tech(game_str_buffer, *game_trade_offer_tech[0], 1);
    game_parse_says(0, game_str_buffer, -1, -1);
    game_parse_num(0, price);
    game_str_buffer[0] = 0;
    strcat(game_str_buffer, text(0x68d724));
    if (proposal == PROPOSE_BUY_TECH) {
        strcat(game_str_buffer, text(0x68d728));
    } else if (proposal == PROPOSE_COMMLINK) {
        strcat(game_str_buffer, text(0x68d730));
        game_parse_says(0, game_faction_defs[*game_diplo_proposal_faction].adjective, -1, -1);
    } else {
        strcat(game_str_buffer, text(0x68d73c));
    }
    int afford = me->energy_credits >= price;
    if (!afford) strcat(game_str_buffer, text(0x68d744));
    char digit[12];
    snprintf(digit, sizeof(digit), "%d",
             proposal == PROPOSE_BUY_TECH || proposal == PROPOSE_COMMLINK ? 0 : game_random(0, 2));
    strcat(game_str_buffer, digit);
    int choice = answer(game_str_buffer, other);
    if (!afford || choice != 1) return 1;
    if (*game_net_multiplayer) {
        net_post(0x68d4bc, faction, other, -price, 0x447, faction, -price, other, price);
    } else {
        if (faction) me->energy_credits -= price;
        if (other) ai->energy_credits += price;
        game_console_update_data(game_console, 1);
    }
    net_tech(faction, *game_trade_offer_tech[0], other);
    if (proposal == PROPOSE_BUY_TECH) *game_diplo_best_proto = -1;
    energy_shown();
    game_diplomacy_caption(faction, other);
    return 1;
}

// ---- trade_maps, give_a_base ---------------------------------------------------------

static inline struct map_tile *tile_at(int x, int y) { return &(*game_map)[(x >> 1) + y * *game_map_half_width]; }

// Two factions trade maps: each learns the other's maps_traded (and the
// other), every square either has seen (with what it last saw there of
// the square's bits, between real factions), and the bases either has
// seen or owns (spot_base()). game_diplo_maps_news says whether the first
// learned anything. The console's map is redrawn if the player is one of
// them.
void trade_maps(int faction, int other) {
    struct faction_data *a = F(faction), *b = F(other);
    *game_diplo_maps_news = 0;
    a->maps_traded |= b->maps_traded | 1u << other;
    b->maps_traded |= a->maps_traded | 1u << faction;
    uint8_t mine = (uint8_t)(1u << faction), theirs = (uint8_t)(1u << other);
    struct map_tile *t = *game_map;
    for (int i = 0; i < *game_map_area; i++, t++) {
        if (t->visibility & mine) {
            t->visibility |= theirs;
            if (faction && other) t->bits_seen[other - 1] |= t->bits_seen[faction - 1] & t->bits;
        } else if (t->visibility & theirs) {
            *game_diplo_maps_news = 1;
            t->visibility |= mine;
            if (faction && other) t->bits_seen[faction - 1] |= t->bits_seen[other - 1] & t->bits;
        }
    }
    for (int i = 0; i < *game_base_count; i++) {
        const struct base *B = &game_bases[i];
        if ((B->visibility & mine) || B->faction == faction) spot_base(i, other);
        else if ((B->visibility & theirs) || B->faction == other) spot_base(i, faction);
    }
    if (faction == *game_current_faction || other == *game_current_faction) {
        *game_draw_state |= 4;
        game_graphic_win_redraw(game_world_win);
        game_draw_map(1);
    }
}

// A base ceded in the talks to `faction`. The new owner sees the squares
// around it. The old owner's units nearby (on the square, or next to it
// when on land): defensive and recon units on it and ships change hands
// with it, homed there; other units go to the old owner's base nearest
// this one (found as though it were already the new owner's) if that is
// more than a square away (homed there, if this was their home), or change
// hands too. The base's production and stored minerals are lost (its queue
// taken off the old owner's counts), it is recorded in the replay and its
// disorder and other state bits cleared; a threat (game_diplo_counter)
// makes it a captured base still to be assimilated (50 turns) unless it
// was the new owner's before, otherwise the new owner's own. The old
// owner's units homed there that are left are disbanded, and the
// territory and the base worked out again.
void give_a_base(int base, int faction) {
    struct base *B = &game_bases[base];
    int x = B->x, y = B->y, old = B->faction;
    for (int i = 0; i < 21; i++) {
        int x2 = xrange(game_radius_x[i] + x), y2 = game_radius_y[i] + y;
        if (!on_map(x2, y2)) continue;
        tile_at(x2, y2)->visibility |= (uint8_t)(1u << faction);
        synch_bit(x2, y2, faction);
    }
    for (int v = *game_veh_count - 1; v >= 0; v--) {
        struct veh *V = &game_vehs[v];
        if (V->faction != old) continue;
        int dist = vector_dist(V->x, V->y, x, y), land = (tile_at(V->x, V->y)->climate & 0xe0) >= 0x60;
        if (dist > land) continue;
        const struct unit_proto *P = &game_protos[V->proto];
        int transfer = 0;
        if ((P->plan == 2 || P->plan == 3) && !dist) transfer = 1;
        else if (game_chassis[P->chassis].triad == TRIAD_SEA && (!dist || land)) transfer = 1;
        if (!transfer) {
            B->faction = (uint8_t)faction;
            int to = game_base_find2(x, y, old);
            B->faction = (uint8_t)old;
            if (to < 0) {
                transfer = 1;
            } else {
                const struct base *T = &game_bases[to];
                if (!((x_dist(T->x, x) + iabs(T->y - y)) & ~1)) {
                    transfer = 1;
                } else {
                    stack_put(stack_veh(v, 1), T->x, T->y);
                    V->order = 0;
                    if (V->home_base == base) V->home_base = (int16_t)to;
                }
            }
        }
        if (transfer) {
            F(old)->units_active[V->proto]--;
            V->faction = (uint8_t)faction;
            F(faction)->units_active[V->proto]++;
            V->home_base = (int16_t)base;
            V->moves_spent = 0;
            if (V->order != 2) V->order = 0;
            owner_set(V->x, V->y, faction);
        }
    }
    B->faction = (uint8_t)faction;
    B->minerals_2 = 0, B->minerals = 0;
    for (int i = 0; i <= B->queue_size; i++)
        if (B->queue[i] >= 0) F(old)->units_queue[B->queue[i]]--;
    B->queue_size = 0;
    B->production_last = B->queue[0] = -0x45;
    game_replay_base(1, x, y, faction);
    B->state &= 0xffdef7b1u;
    B->nerve_staple_turns = 0;
    if (*game_diplo_counter == COUNTER_THREATEN) {
        if (B->faction_former != faction) {
            B->faction_former = (uint8_t)old;
            B->assimilation_turns = 50;
        } else {
            B->assimilation_turns = 0;
        }
    } else {
        if (B->faction_former == faction) B->assimilation_turns = 0;
        B->faction_former = (uint8_t)faction;
    }
    spot_base(base, old);
    reset_territory();
    for (int v = *game_veh_count - 1; v >= 0; v--) {
        if (v < *game_veh_count && game_vehs[v].faction == old && game_vehs[v].home_base == base) kill(v);
    }
    owner_set(x, y, faction);
    game_draw_tile_fixup(x, y, 1, 2);
    set_base(base);
    base_compute(1);
}

// ---- base_swap -----------------------------------------------------------------------

// The player (`faction`) asks the AI (`other`) for one of its bases
// (game_diplo_ask_base), offering game_diplo_counter: another base
// (game_diplo_bid_base), energy, research or a price. 0 once the base has
// changed hands, 1 otherwise. Both bases are valued with value_of_base()
// (the AI's own with the talks' fear and hostility): its own at half for
// one the player held before or from an AI that surrendered, the player's
// at twice for one the AI held before. It won't take a base worth less
// than nothing or the player's last, nor give one worth less than nothing,
// its last or its headquarters, nor deal while it wants revenge (but for
// a threat). For a base, it swaps when its own (plus a roll of 0 to 99)
// weighs more than the player's (plus another roll). Else the price is its
// base's value by the two rankings over twice its own, twice from the
// leader: in energy for energy or a price, when the player has it; or in
// research for research or a price, when the techs the player has and it
// hasn't, for each eight turns, are worth it to it and it isn't far ahead
// in techs (by more than 2, in halves). The player may accept.
int base_swap(int faction, int other) {
    struct faction_data *me = F(faction), *ai = F(other);
    *game_diplo_second = other;
    int hostile = *game_diplo_prev_hostility || *game_diplo_attack_chance;
    int ask = value_of_base(*game_diplo_ask_base, other, faction, *game_diplo_fear, hostile);
    int bid = value_of_base(*game_diplo_bid_base, other, faction, 0, 0);
    if (*game_diplo_ask_base >= 0) {
        const struct base *B = &game_bases[*game_diplo_ask_base];
        game_parse_says(0, (char *)B->name, -1, -1);
        if (B->faction_former == faction) ask /= 2;
        if ((status(other, faction) & TREATY_PACT) && (status(other, faction) & TREATY_SURRENDERED_)) ask /= 2;
    }
    if (*game_diplo_bid_base >= 0) {
        const struct base *B = &game_bases[*game_diplo_bid_base];
        game_parse_says(1, (char *)B->name, -1, -1);
        if (B->faction_former == other) bid *= 2;
        if (bid < 0 || me->base_count == 1) {
            answer(text(0x68ddf4), other);
            return 1;
        }
    }
    int asked = *game_diplo_ask_base;
    if (asked >= 0 &&
        (ask < 0 || ai->base_count == 1 || game_has_facility(&game_bases[asked], FAC_HEADQUARTERS))) {
        answer(text(0x68ddfc), other);
        return 1;
    }
    if ((status(other, faction) & TREATY_WANT_REVENGE) && *game_diplo_counter != COUNTER_THREATEN) {
        answer(text(0x68de04), other);
        return 1;
    }
    if (*game_diplo_bid_base >= 0) {
        if (asked < 0) return 1;
        int theirs = game_random(0, 100) + bid;
        if (game_random(0, 100) + ask <= theirs) {
            answer(text(0x68de20), other);
            return 1;
        }
        lock();
        net_cede_base(*game_diplo_ask_base, faction);
        net_cede_base(*game_diplo_bid_base, other);
        unlock();
        answer(text(0x68de14), other);
        return 0;
    }
    if (asked < 0) return 1;
    int price = (ai->ranking + me->ranking) * ask / (2 * ai->ranking);
    if (game_faction_rankings[7] == faction) price *= 2;
    int counter = *game_diplo_counter;
    if (counter == COUNTER_ENERGY || counter == COUNTER_NAME_A_PRICE) {
        if (me->energy_credits >= price) {
            game_parse_num(0, price);
            if (!answer(text(0x68de38), other)) return 1;
            lock();
            if (*game_net_multiplayer) {
                net_post(0x68d4bc, faction, other, -price, 0x447, faction, -price, other, price);
            } else {
                if (faction) me->energy_credits -= price;
                if (other) ai->energy_credits += price;
                game_console_update_data(game_console, 1);
            }
            net_cede_base(*game_diplo_ask_base, faction);
            unlock();
            answer(text(0x68de44), other);
            return 0;
        }
        if (counter == COUNTER_ENERGY) {
            game_parse_num(0, price);
            answer(text(0x68de2c), other);
            return 1;
        }
    }
    if (counter != COUNTER_RESEARCH && counter != COUNTER_NAME_A_PRICE) {
        game_parse_num(0, price);
        answer(text(0x68de70), other);
        return 1;
    }
    // Research: every tech the player has and the AI hasn't.
    int worth = 0;
    for (int t = 0; t < 89; t++)
        if (!has_tech(t, other) && has_tech(t, faction)) worth += tech_val(t, other, 1);
    if (ai->tech_ranking / 2 > me->tech_ranking / 2 + 2 || (*game_turn + 7) / 8 * worth < price) {
        answer(text(0x68de4c), other);
        return 1;
    }
    say_name(2, other);
    if (!answer(text(0x68de58), other)) return 1;
    lock();
    for (int t = 0; t < 89; t++) {
        if (has_tech(t, other) || !has_tech(t, faction)) continue;
        if (*game_net_multiplayer) net_post(0x68d4f0, other, t, faction, 0x44b, other, t, faction, 0);
        else game_tech_achieve(other, t, faction, 0);
    }
    net_cede_base(*game_diplo_ask_base, faction);
    unlock();
    answer(text(0x68de68), other);
    return 0;
}

// ---- propose_attack ------------------------------------------------------------------

// Steps sent without waiting for the other players, in a network game
// (propose_attack()'s).
static void net_treaty_on_nowait(int faction, int other, uint32_t bits) {
    if (*game_net_multiplayer) net_post(0x68d444, faction, other, (int)bits, 0x441, faction, other, (int)bits, 0);
    else treaty_on(faction, other, bits);
}
static void net_set_treaty_nowait(int faction, int other, uint32_t bits, int on) {
    if (*game_net_multiplayer) net_post(0x68d46c, faction, other, (int)bits, 0x443, faction, other, (int)bits, on);
    else set_treaty(faction, other, bits, on);
}
// A faction's insulting adjective (faction_def, beside its adjectives).
static char *insult_adjective(int faction) { return (char *)&game_faction_defs[faction] + 0x1e4; }

// How far the AI (`other`) is from `target`, for a joint attack, when they
// have no commlink and are not neighbours: 1 if both have bases in a
// region (but the poles' and the last), 2 if not.
static int attack_far(int other, int target) {
    for (int r = 1; r < 128; r++) {
        if ((r & 63) == 63 || !(r & 63)) continue;
        if (F(other)->region_total_bases[r] && F(target)->region_total_bases[r]) return 1;
    }
    return 2;
}

// What the AI (`other`) asks of the player (`faction`) to attack `target`,
// in 25s: the talks' reputation less the target's broken treaties, for
// each ten turns, plus five times its strength, by the AI's will to fight;
// halved, quartered or thirded when the target leads (by its score), and
// for an AI that cooperates with humans; more in a pact with the target,
// less in one with the player; less against a target with more broken
// treaties than the player; more against a great satan; less from an AI
// that surrendered; half again for a target that is no enemy of the
// player's and no human; halved between two of the top; times the gifts
// the target gave the AI beyond its wrongs to it; three quarters for a far
// target (`far` 1), a half for a farther one.
static int attack_price(int faction, int other, int target, int far) {
    struct faction_data *me = F(faction), *ai = F(other), *t = F(target);
    int v = (*game_diplo_reputation - t->integrity_blemishes) * (*game_turn / 10);
    v = (v + 5 * t->mil_strength) * (2 - ai->ai_fight);
    if (t->ranking > ai->ranking && t->ranking > me->ranking && !*game_diplo_rival_strong_pacts &&
        (me->ranking >= 4 || ai->ranking >= 4))
        v /= 2;
    int32_t *scores = game_ranking_scores;
    if (game_faction_rankings[7] == target) {
        int second = game_faction_rankings[6];
        if (scores[target] >= 2 * scores[second]) v /= 4;
        else if (great_satan(target, 0)) v /= 3;
        else if (2 * scores[target] >= 3 * scores[second]) v /= 2;
    }
    if (ai->flags & FACTION_FLAG_COOP_WITH_HUMAN_) v /= 2;
    if (status(other, target) & TREATY_PACT) {
        v *= 3 - ai->ai_fight;
        if (me->ranking >= 6 && t->ranking >= 5 && !*game_diplo_runaway_leader && ai->ranking <= 4 && *game_turn >= 50 &&
            (*game_diplo_player_pacts || me->integrity_blemishes > 2))
            v *= 2;
    } else if (status(other, faction) & TREATY_PACT) {
        v /= 2;
    }
    if (t->integrity_blemishes > me->integrity_blemishes) {
        v /= 2;
        if ((status(faction, target) & TREATY_WANT_REVENGE) && !(status(other, faction) & TREATY_WANT_REVENGE) &&
            !(status(target, faction) & TREATY_WANT_REVENGE))
            v /= 2;
    }
    if (*game_diplo_great_satan) v *= 2;
    if ((status(other, faction) & TREATY_PACT) && (status(other, faction) & TREATY_SURRENDERED_)) v /= 2;
    if (!(status(faction, target) & TREATY_VENDETTA) && !((*game_human_factions >> target) & 1)) v += v / 2;
    if (t->ranking >= 6 && ai->ranking >= 6) v /= 2;
    int gifts = t->diplo_gifts[other], wrongs = t->diplo_wrongs[other];
    if (gifts > wrongs) v *= gifts - wrongs + 1;
    return ((4 - far) * v / 4 + 24) / 25 * 25;
}

// The player (`faction`) asks the AI (`other`) to join an attack on
// game_diplo_proposal_faction, offering game_diplo_counter; 0 when a
// threat is turned away without a word, 1 otherwise.
// - An AI already at war with the target says so. One with no commlink to
//   it, not its neighbour (contiguous()), refuses unless in a pact with
//   the player, and asks more as the target is farther (attack_far()).
// - In a pact, for friendship or a threat, with the player at war with the
//   target and the AI not in a pact with it, the AI joins (double-crosses
//   the target) when either side would fight the other anyway, the player
//   wants revenge on a target ranked above it, or the target outranks both.
// - One that surrendered to the player joins for friendship or a threat.
//   Outside a pact, one that wants revenge on the player, or sees a poor
//   reputation (above 5), refuses (silently to a threat). Otherwise, for
//   friendship or a threat, one that wouldn't fight the target anyway
//   (nor, in a pact or with fear, the target it while the player is at war
//   with it) refuses (silently to a threat).
// - Else it names its price (attack_price()): the techs it wants (up to
//   four, the fewest worth 4/3 of the price), for research or a price,
//   when they are worth it (not from the Believers); else energy, for
//   energy or a price (not from the Gaians), which the player pays in a
//   menu (MAYBEWARPRICE, when they can). The University values techs
//   twice, the Peacekeepers half as much again (each sum from the total of
//   all four: the original's), Morgan half.
// Joining, the talks are held while the AI declares a vendetta on the
// target (treaty bits 0x80000018) and marks it (0x40) and the player
// (0x80). The target, at a level above 1, one time in two, then ends its
// pact with the player, or its treaty with a vendetta and revenge, with
// friction 4.
static int propose_attack_body(uint8_t *pop, int faction, int other) {
    struct faction_data *me = F(faction), *ai = F(other);
    *game_diplo_second = other;
    int target = *game_diplo_proposal_faction, far = 0;
    struct faction_data *t = F(target);
    if (status(other, target) & TREATY_VENDETTA) {
        say_title(0, target), say_name(1, target);
        answer(text(0x68da74), other);
        return 1;
    }
    if (!(status(other, target) & TREATY_COMMLINK) && !game_contiguous(other, target, 0, 1)) {
        if (!(status(other, faction) & TREATY_PACT)) {
            say_title(0, faction);
            game_parse_says(2, game_faction_defs[target].adjective, -1, -1);
            answer(text(0x68da84), other);
            return 1;
        }
        far = attack_far(other, target);
    }
    int price = attack_price(faction, other, target, far);
    int counter = *game_diplo_counter, joins = 0, asked = 0;
    int friendly = counter == COUNTER_FRIENDSHIP || counter == COUNTER_THREATEN;
    int at_war = (status(faction, target) & TREATY_VENDETTA) != 0, allied = (status(other, target) & TREATY_PACT) != 0;
    if ((status(faction, other) & TREATY_PACT) && friendly && at_war && !allied) {
        if (wants_to_attack(other, target, 0) || wants_to_attack(target, other, 0) ||
            ((status(faction, target) & TREATY_WANT_REVENGE) && t->ranking > me->ranking) ||
            (t->ranking > me->ranking && t->ranking > ai->ranking)) {
            say_title(0, target), say_name(1, target), say_noun(2, target);
            game_parse_says(3, game_get_pact(faction), -1, -1);
            answer(text(0x68da90), other);
            net_double_cross(other, target, faction);
            return 1;
        }
    }
    uint32_t st = status(other, faction);
    if ((st & TREATY_PACT) && (st & TREATY_SURRENDERED_)) {
        if (friendly) goto join;
    } else if (!(st & TREATY_PACT) && ((st & TREATY_WANT_REVENGE) || *game_diplo_reputation > 5)) {
        if (counter == COUNTER_THREATEN) return 0;
        game_parse_says(0, game_faction_defs[faction].adjective, -1, -1);
        say_title(1, faction), say_name(2, faction), say_noun(3, target);
        answer(text(0x68da98), other);
        return 1;
    } else if (friendly && !wants_to_attack(other, target, 0) &&
               !(((status(other, faction) & TREATY_PACT) || *game_diplo_fear) && wants_to_attack(target, other, 0) &&
                 (status(faction, target) & TREATY_VENDETTA))) {
        if (*game_diplo_counter == COUNTER_THREATEN) return 0;
        say_title(0, faction);
        answer(text(0x68daa4), other);
        return 1;
    }

    // The price: techs, or energy.
    int32_t **want = game_trade_want_tech;
    int rate = (*game_turn + 7) / 8, sum[4], total = 0;
    for (int i = 0; i < 4; i++) {
        if (*want[i] >= 0) total += tech_val(*want[i], other, 1) * rate;
        sum[i] = total;
    }
    if (is_faction(other, 0x68dab4)) {
        for (int i = 0; i < 4; i++) sum[i] *= 2;
    } else if (is_faction(other, 0x68dabc)) {
        sum[3] = sum[3] * 3 / 2;
        sum[0] = sum[1] = sum[2] = sum[3] * 3 / 2;
    } else if (is_faction(other, 0x68dac4)) {
        for (int i = 0; i < 4; i++) sum[i] /= 2;
    }
    // Only as many techs as it takes.
    int enough = price * 4 / 3;
    if (sum[2] >= enough) *want[3] = -1;
    if (sum[1] >= enough) *want[2] = -1;
    if (sum[0] >= enough) *want[1] = -1;
    counter = *game_diplo_counter;
    if (*want[0] >= 0 && !is_faction(other, 0x68dacc) && sum[3] >= price &&
        (counter == COUNTER_RESEARCH ||
         (counter == COUNTER_NAME_A_PRICE && (ai->energy_credits > me->energy_credits || sum[3] >= price * 3 / 2)))) {
        say_noun(0, target), say_title(1, target), say_name(2, target);
        static const int slots[4] = {3, 4, 8, 9};
        for (int i = 0; i < 4; i++) {
            if (i && *want[i] < 0) continue;
            game_str_buffer[0] = 0;
            say_tech(game_str_buffer, *want[i], 1);
            game_parse_says(slots[i], game_str_buffer, -1, -1);
        }
        say_title(5, other);
        game_parse_says(6, game_faction_defs[target].adjective, -1, -1);
        asked = 1;
        game_str_buffer[0] = 0;
        strcat(game_str_buffer, text(0x68dad4));
        if (status(other, target) & TREATY_PACT) strcat(game_str_buffer, text(0x68dae4));
        else if (far) strcat(game_str_buffer, text(0x68daec));
        if (*want[3] >= 0) strcat(game_str_buffer, text(0x68daf0));
        else if (*want[2] >= 0) strcat(game_str_buffer, text(0x68daf4));
        else if (*want[1] >= 0) strcat(game_str_buffer, text(0x68daf8));
        if (answer(game_str_buffer, other)) {
            joins = 1;
            for (int i = 3; i >= 0; i--)
                if (!i || *want[i] >= 0) net_tech(other, *want[i], faction);
            for (int i = 3; i >= 0; i--) *want[i] = -1;
        }
    } else if (!is_faction(other, 0x68dafc) && (counter == COUNTER_ENERGY || counter == COUNTER_NAME_A_PRICE)) {
        say_noun(0, target);
        game_parse_says(1, game_get_pact(target), -1, -1);
        game_parse_says(2, insult_adjective(target), -1, -1);
        game_parse_num(0, price);
        game_parse_says(6, game_faction_defs[target].adjective, -1, -1);
        game_str_buffer[0] = 0;
        strcat(game_str_buffer, text(0x68db04));
        if (status(other, target) & TREATY_PACT) strcat(game_str_buffer, text(0x68db14));
        else if (far) strcat(game_str_buffer, text(0x68db1c));
        game_popup_start(pop, game_pop_file, game_str_buffer, -1, 0, 0x100000, 0);
        // The menu to pay, if the player can.
        if (me->energy_credits >= price) game_popup_start(pop, game_pop_file, text(0x68db20), -1, 0, 0x100080, 0);
        void *portrait = game_faction_portraits[other];
        *(void **)(pop + POP_ICON) = *(int32_t *)(pop + POP_HAS_ICON) ? NULL : portrait;
        game_sprite_box_sprite(pop + POP_SPRITE_BOX, portrait, 0, 0);
        asked = 1;
        if (game_base_pop_exec_3(pop, 0, (int)(intptr_t)game_pop_wait) > 0) {
            net_energy(faction, -price, other);
            joins = 1;
        }
    }
    energy_shown();
    if (!joins) {
        if (!asked) {
            say_title(0, target), say_name(1, target);
            game_parse_says(2, game_get_him_her(target, 0), -1, -1);
            answer(text(0x68db30), other);
        }
        return 1;
    }

join:
    lock();
    net_treaty_on_nowait(other, target, 0x80000018);
    net_set_treaty_nowait(other, target, TREATY_UNK_40, 1);
    net_set_treaty_nowait(other, faction, TREATY_UNK_80, 1);
    say_title(0, target), say_name(1, target), say_noun(2, target);
    game_parse_says(3, insult_adjective(target), -1, -1);
    game_parse_says(4, game_get_phrase(target, 5), -1, -1);
    answer(label_random(0x68db38), other);
    game_parse_says(3, game_get_pacts(target), -1, -1);
    game_parse_says(4, game_faction_defs[target].adjective, -1, -1);
    // The target, told, may break with the player.
    uint32_t tie = status(faction, target);
    if ((tie & (TREATY_PACT | TREATY_TREATY)) && me->diff_level > 1 && !game_random(0, 2)) {
        if (!*game_net_multiplayer) cause_friction(target, faction, 4);
        if (tie & TREATY_PACT) {
            game_x_pop(text(0x68db40), (int)(intptr_t)game_pop_wait);
            net_pact_ends(faction, target);
        } else {
            game_x_pop(text(0x68db4c), (int)(intptr_t)game_pop_wait);
            net_treaty_on_nowait(faction, target, TREATY_VENDETTA);
            net_set_treaty(target, faction, TREATY_WANT_REVENGE, 1);
        }
    }
    unlock();
    return 1;
}
int propose_attack_in(uint8_t *pop, int faction, int other);
int propose_attack_in(uint8_t *pop, int faction, int other) {
    game_popup_ctor(pop);
    int r = propose_attack_body(pop, faction, other);
    pop_end(pop);
    return r;
}
// The popup where the original's frame keeps it.
__asm__(".text\n"
        ".globl propose_attack\n"
        "propose_attack:\n"
        "    pushl %ebp\n"
        "    movl %esp, %ebp\n"
        "    subl $0x5420, %esp\n"
        "    pushl 12(%ebp)\n"
        "    pushl 8(%ebp)\n"
        "    leal -0x540c(%ebp), %eax\n"
        "    pushl %eax\n"
        "    call propose_attack_in\n"
        "    movl %ebp, %esp\n"
        "    popl %ebp\n"
        "    ret\n");

// ---- proposal_menu, make_a_proposal --------------------------------------------------

enum { POP_LIST_SELECT = 0x220c, POP_DEFAULT = 0xa44 };  // the list's callback, the menu's default item

// A line of the menu being read (x_text_open()) put together into the
// string buffer.
static char *next_line(void) {
    game_text_get();
    game_str_buffer[0] = 0;
    game_parse_string(*game_text_line, game_str_buffer);
    return game_str_buffer;
}
static void item(uint8_t *pop, char *text, int id) { game_dialogs_item(pop + POP_DIALOGS, text, id); }
static int menu(uint8_t *pop) { return game_base_pop_exec_3(pop, 0, (int)(intptr_t)game_pop_wait); }
static void pop_start_flags(uint8_t *pop, char *label, int flags, int other) {
    game_popup_start(pop, game_pop_file, label, -1, 0, flags, 0);
    void *portrait = game_faction_portraits[other];
    *(void **)(pop + POP_ICON) = *(int32_t *)(pop + POP_HAS_ICON) ? NULL : portrait;
    game_sprite_box_sprite(pop + POP_SPRITE_BOX, portrait, 0, 0);
}
static int has_headquarters(int base) { return game_has_facility(&game_bases[base], FAC_HEADQUARTERS); }
// Whether `faction` holds the Empath Guild, which gives commlinks.
static int empath(int faction) {
    int base = game_secret_projects[SP_EMPATH_GUILD];
    return base >= 0 && game_bases[base].faction == faction;
}
// The planet window, where the base menus show the base picked.
static void *plan_win(void) { return game_at(0x834d70); }
static void plan_win_show(void) {
    uint8_t *win = plan_win();
    game_win_show(win + ((int32_t *)*(void **)win)[1], 0);
}
static void plan_win_line(int base) {
    game_plan_win_clear_lines(plan_win());
    game_plan_win_monocenter(plan_win(), game_bases[base].x, game_bases[base].y, 0);
    game_plan_win_add_line_2(plan_win(), base, base, 0);
    game_plan_win_on_redraw(game_at(0x856dc0));
}

// ---- tech_trade ----------------------------------------------------------------------

// A line with a digit after it, picked by the caller.
static char *label_digit(uint32_t label, int digit) {
    char d[12];
    game_str_buffer[0] = 0;
    strcat(game_str_buffer, text(label));
    snprintf(d, sizeof(d), "%d", digit);
    strcat(game_str_buffer, d);
    return game_str_buffer;
}
static void say_tech_slot(int slot, int tech) {
    game_str_buffer[0] = 0;
    say_tech(game_str_buffer, tech, 1);
    game_parse_says(slot, game_str_buffer, -1, -1);
}
// The patience an AI that gave a tech away has left: 1 at war, 6 in a
// pact (-12, from 500, for one that surrendered), else from the friction
// (3, less (friction + 3) / 8, one more with a treaty).
static int8_t patience_left(int other, int faction) {
    uint32_t st = status(other, faction);
    if (st & TREATY_VENDETTA) return 1;
    if (st & TREATY_PACT) return (int8_t)((st & TREATY_SURRENDERED_) ? 0x1f4 : 6);
    return (int8_t)(((st >> 1) & 1) - (*game_diplo_friction + 3) / 8 + 3);
}

// Techs traded between the player (`faction`) and the AI (`other`):
// proposed by the player (PROPOSE_TECH, PROPOSE_BUY_TECH, or
// PROPOSE_COMMLINK for a commlink with game_diplo_proposal_faction),
// offering game_diplo_counter, or by the AI (`ai`, from communicate()),
// which then trades again until the player declines. 1 after a trade the
// player proposed, 0 otherwise. After tech_analysis():
// - The AI won't trade outside a pact while it has grudges (or 0x8000)
//   against the player, holds captured bases, has friction of 18 or more,
//   or hostility (but the University), with a great satan whose friction
//   is high for its tech cost and treaty, or (in a network game, or with
//   intense rivalry) a leader far ahead in score and techs; nor, with
//   friction, while it guards its lead (ahead in techs, or holding a
//   reactor's tech the player lacks; less readily for the Hive) unless it
//   cooperates with humans; nor when a project is at stake. One that
//   surrendered skips these. In those cases it may still sell
//   its tech (buy_tech(), at the high price).
// - The player's map counts as a tech it wants (9999) when it has seen
//   few of the player's bases. A commlink from a partner, a tech from one
//   that surrendered, or (a richer, higher-ranked partner far ahead in
//   techs) a tech it doesn't need, it gives away; a partner asked for a
//   commlink trades it for its wanted tech (COMMLINKTECH).
// - For research or a price, with a tech wanted and no great satan, and
//   if the AI isn't too wary of a leading player (network games and
//   intense rivalry), wants no revenge, finds the player's integrity
//   good and has seen enough of the map: the trade (TRADETECH), where the
//   player may pick the AI's second tech offered (refused if worth more
//   to it than the tech it wants, unless in a pact), give a second tech
//   (refused by the Hive, a network game's leader, and but for the
//   University a great satan or a player behind it; never by one that
//   surrendered), see the Datalinks, or
//   decline. A trade marks the AI's agenda (0x180, and 0x1000000 in its
//   status when it proposed it) and lowers friction by a quarter of the
//   value it puts on the tech it wanted (an eighth, the original's, after
//   either second tech is picked).
// - Otherwise it sells its tech, or says later (or no commlink).
static int tech_trade_body(uint8_t *pop, int faction, int other, int ai, int counter, int proposal) {
    struct faction_data *me = F(faction), *O = F(other);
    int32_t **want = game_trade_want_tech, **offer = game_trade_offer_tech;
    *game_diplo_second = other;
    int high = counter == COUNTER_RESEARCH || counter == COUNTER_NAME_A_PRICE;
    *game_diplo_trade_refused = 0;
again:
    tech_analysis(faction, other);
    if (proposal == PROPOSE_COMMLINK) *offer[0] = *game_diplo_proposal_faction + TECH_COMMLINK_0;
    game_diplomacy_caption(faction, other);
    if (!(status(faction, other) & TREATY_PACT) &&
        ((status(other, faction) & (TREATY_WANT_REVENGE | TREATY_UNK_8000)) || *game_diplo_captured_bases ||
         *game_diplo_friction >= 18 || (*game_diplo_hostility && !is_faction(other, 0x68d7dc)))) {
        // Hatred.
        if (ai) return 0;
        say_title(0, faction), say_name(1, faction);
        *game_parse_gender = game_faction_defs[faction].leader_female, *game_parse_plural = 0;
        game_parse_says(2, (char *)&game_faction_defs[faction] + 0x3ac, -1, -1);
        game_parse_says(3, insult_adjective(faction), -1, -1);
        game_parse_says(4, game_get_phrase(faction, 3), -1, -1);
        answer(label_digit(0x68d7e4, proposal == PROPOSE_COMMLINK ? 0 : game_random(0, 3)), other);
        return 0;
    }
    uint32_t st = status(other, faction);
    int lead = 0;  // the AI has a reactor's tech the player lacks
    if (!((st & TREATY_PACT) && (st & TREATY_SURRENDERED_))) {
        for (int r = 0; r < 4; r++) {
            int t = game_reactors[r].preq_tech;
            if (has_tech(t, other) && !has_tech(t, faction)) lead = 1;
        }
        int fear = 0;
        if (great_satan(faction, 0) && !(status(faction, other) & TREATY_PACT) &&
            !(O->flags & FACTION_FLAG_COOP_WITH_HUMAN_)) {
            int cost = game_faction_defs[other].rule_techcost - 100, sign = cost > 0 ? 1 : cost < 0 ? -1 : 0;
            if (*game_diplo_friction >= ((st & TREATY_TREATY) ? 3 : 0) + 2 * (2 - sign)) fear = 1;
        }
        if (!fear && (*game_net_multiplayer || (*game_rules & RULES_INTENSE_RIVALRY)) && me->ranking == 7 &&
            !(status(faction, other) & TREATY_PACT) && !(O->flags & FACTION_FLAG_COOP_WITH_HUMAN_)) {
            if (great_satan(faction, 0)) goto feared_leader;
            int second = game_ranked_faction[1];
            if (3 * game_ranking_scores[faction] > 4 * game_ranking_scores[second] &&
                3 * me->tech_ranking > 4 * F(second)->tech_ranking)
                goto feared_leader;
        }
        if (fear) {
            if (ai || buy_tech(faction, other, counter, 1, proposal)) return 0;
            say_title(0, faction), say_name(1, faction);
            game_parse_says(2, game_get_phrase(faction, 3), -1, -1);
            answer(label_digit(0x68d7f0, proposal == PROPOSE_COMMLINK ? 0 : game_random(0, 2)), other);
            return 0;
        }
        if (O->tech_ranking > me->tech_ranking || lead) {
            if (ai) return 0;
            int hive = is_faction(other, 0x68d808) ? 3 : 0;
            int limit = ((st & TREATY_TREATY) ? 3 : 0) + (lead ? 3 : 9) - hive;
            if (*game_diplo_friction >= limit && !(status(faction, other) & TREATY_PACT) &&
                !(O->flags & FACTION_FLAG_COOP_WITH_HUMAN_)) {
                if (!lead && buy_tech(faction, other, counter, 1, proposal)) return 0;
                if (counter == COUNTER_ENERGY && proposal != PROPOSE_COMMLINK) {
                    say_title(0, faction), say_name(1, faction);
                    game_parse_says(2, game_get_phrase(faction, 3), -1, -1);
                    answer(text(0x68d810), other);
                    return 0;
                }
                if (!high) return 0;
                game_parse_says(0, insult_adjective(faction), -1, -1);
                answer(label_digit(0x68d820, lead ? 2 : game_random(0, 3)), other);
                return 0;
            }
        }
        if (*game_trade_offer_project[0] != -1 && proposal != PROPOSE_COMMLINK) {
            if (ai) return 0;
            int p = *game_trade_offer_project[0];
            game_parse_say(0, (int)(intptr_t)game_facilities[p >= 0 ? p + 0x46 : 0x44 - p].name, -1, -1);
            say_title(1, faction), say_name(2, faction);
            answer(label_digit(0x68d830, *game_trade_offer_project[0] >= 0 ? game_random(0, 3) : 2), other);
            return 0;
        }
    }
    if (*want[0] < 0 && proposal != PROPOSE_BUY_TECH && proposal != PROPOSE_COMMLINK &&
        !(O->maps_traded & (1u << faction)) && !*game_diplo_trade_offered && !(status(faction, other) & TREATY_PACT) &&
        !(O->flags & 0x200)) {
        // The player's map, when the AI has seen few of the player's bases.
        int seen = 0;
        for (int b = 0; b < *game_base_count; b++)
            if (game_bases[b].faction == faction && (game_bases[b].visibility & (1u << other))) seen++;
        if (*game_rules & RULES_UNK_100 ? !seen && *game_diplo_friction <= 7
                                                 : seen < (me->base_count + 1) / 2 && *game_diplo_friction <= 7)
            *want[0] = 9999;
    }
    st = status(other, faction);
    if (proposal == PROPOSE_COMMLINK && (st & TREATY_PACT)) {
        // A partner gives the commlink.
        if (!wants_to_speak(faction, other)) return 0;
        game_parse_says(0, game_faction_defs[*game_diplo_proposal_faction].adjective, -1, -1);
        game_x_pops(text(0x68d83c), game_faction_portraits[other], game_pop_wait);
        net_tech(faction, *offer[0], other);
        *offer[0] = -1;
        return 0;
    }
    if (*offer[0] >= 0 && proposal != PROPOSE_COMMLINK && (st & TREATY_PACT) && (st & TREATY_SURRENDERED_)) {
        // One that surrendered gives it.
        if (!wants_to_speak(faction, other)) return 0;
        say_tech_slot(0, *offer[0]);
        answer(text(0x68d84c), other);
        net_tech(faction, *offer[0], other);
        *offer[0] = -1;
        return 0;
    }
    if (*want[0] < 0 && *offer[0] >= 0 && proposal != PROPOSE_BUY_TECH && proposal != PROPOSE_COMMLINK &&
        (st & TREATY_PACT) && *game_diplo_friction < 10 &&
        !((*game_net_multiplayer || (*game_rules & RULES_INTENSE_RIVALRY)) && faction == game_ranked_faction[0]) &&
        !*game_diplo_great_satan) {
        int mr = me->ranking, ar = O->ranking;
        if ((ar > mr || (mr >= 6 && ar >= 3)) &&
            O->tech_ranking > (mr - ar) / 2 + 2 * O->ai_power + 4 - O->ai_tech + O->ai_fight + me->tech_ranking) {
            // A freebie from a partner far ahead.
            if (!wants_to_speak(faction, other)) return 0;
            say_tech_slot(0, *offer[0]);
            answer(text(0x68d858), other);
            net_tech(faction, *offer[0], other);
            *offer[0] = -1;
            O->diplo_patience[faction] = patience_left(other, faction);
            return 0;
        }
    }
    int seen = 0;  // the AI's bases the player has seen
    for (int b = 0; b < *game_base_count; b++)
        if (game_bases[b].faction == other && (other == faction || (game_bases[b].visibility & (1u << faction))))
            seen++;
    if (*want[0] < 0 || !high || *game_diplo_great_satan) goto no_trade;
    uint32_t agenda = me->diplo_agenda[other];
    int second = game_ranked_faction[1];
    if (*game_net_multiplayer) {
        if ((agenda & 0x80) && faction == game_ranked_faction[0] && !(status(faction, other) & TREATY_PACT)) {
            if (F(second)->ranking < 5 || 2 * game_ranking_scores[faction] > 3 * game_ranking_scores[second])
                goto no_trade;
        }
        if (agenda & 0x100) {
            if (faction != second) goto no_trade;
            if (me->ranking >= 6 && !*game_diplo_runaway_leader) goto no_trade;
        }
    }
    if ((st & TREATY_WANT_REVENGE) || (O->diplo_agenda[faction] & 0x200) || me->integrity_blemishes > 3) goto no_trade;
    if (*game_rules & RULES_INTENSE_RIVALRY) {
        if ((me->diplo_agenda[other] & 0x80) && faction == game_ranked_faction[0] &&
            !(status(faction, other) & TREATY_PACT) &&
            3 * game_ranking_scores[game_faction_rankings[7]] > 4 * game_ranking_scores[game_faction_rankings[6]])
            goto no_trade;
        if ((me->diplo_agenda[other] & 0x100) && !(status(faction, other) & TREATY_PACT) && me->ranking >= 6 &&
            !*game_diplo_runaway_leader)
            goto no_trade;
    }
    if (*offer[0] < 0) {
        if (*game_diplo_trade_offered || (status(faction, other) & TREATY_PACT) || (me->flags & 0x200)) goto no_trade;
        if (me->maps_traded & (1u << other)) {
            int limit = *game_rules & RULES_UNK_100 ? (O->base_count + 3) / 4 : (O->base_count + 1) / 2;
            if (seen >= limit) goto no_trade;
        }
    }
    if (!wants_to_speak(faction, other)) return 0;
    for (;;) {
        if (*want[0] != 9999) say_tech_slot(0, *want[0]);
        if (*offer[0] >= 0) say_tech_slot(1, *offer[0]);
        if (proposal == PROPOSE_COMMLINK) {
            // A commlink for the tech it wants.
            game_parse_says(2, game_faction_defs[*game_diplo_proposal_faction].adjective, -1, -1);
            if (!game_x_pops(text(0x68d880), game_faction_portraits[other], game_pop_wait)) return 0;
            net_tech(other, *want[0], faction);
            net_tech(faction, *offer[0], other);
            *offer[0] = -1;
            *want[0] = *want[1];
            *want[1] = -1;
            return 1;
        }
        if (*want[1] >= 0) say_tech_slot(2, *want[1]);
        say_title(3, faction), say_name(4, faction);
        game_parse_says(5, game_get_noun_phrase(faction, 9), -1, -1);
        game_parse_says(6, game_get_noun_phrase(other, 9), -1, -1);
        *game_parse_gender = game_faction_defs[other].leader_female, *game_parse_plural = 0;
        game_parse_says(8, game_faction_defs[other].leader_name, -1, -1);
        game_parse_says(9, game_get_noun_phrase(other, 7), -1, -1);
        int digit = *want[0] == 9999 ? (*offer[0] >= 0 ? 4 : 5) : *offer[0] < 0 ? 3 : game_random(0, 3);
        pop_start_flags(pop, label_digit(0x68d890, digit), 0x100000, other);
        if (!game_x_text_open(*game_script_file, text(0x68d89c))) {
            game_text_get();
            if (*offer[1] >= 0 && *offer[1] != *offer[0]) {
                say_tech_slot(9, *offer[1]);
                game_str_buffer[0] = 0;
                game_parse_string(*game_text_line, game_str_buffer);
                item(pop, game_str_buffer, 4);
            }
            game_text_get();
            if (*want[1] >= 0 && *want[1] != *want[0]) {
                game_str_buffer[0] = 0;
                game_parse_string(*game_text_line, game_str_buffer);
                item(pop, game_str_buffer, 2);
            }
            game_text_get();
            item(pop, *game_text_line, 3);
        }
        int choice = menu(pop);
        if (!choice) {
            *game_diplo_trade_refused = 1;
            return 0;
        }
        if (choice == 3) {
            game_help_topic(14, *want[0] == 9999 ? *offer[0] : *want[0]);
            continue;
        }
        int worth = *game_trade_want_value[0] / 4;  // an eighth after a second tech
        if (choice == 4) {
            // Its second tech instead.
            if (!(status(other, faction) & TREATY_PACT) && *want[1] < 0 &&
                tech_val(*offer[1], other, 0) > tech_val(*want[0], other, 0)) {
                if (*offer[0] >= 0) say_tech_slot(0, *offer[0]);
                answer(text(0x68d8ac), other);
                *offer[1] = -1;
                continue;
            }
            int t = *offer[0];
            *offer[0] = *offer[1], *offer[1] = t;
            worth = *game_trade_want_value[0] / 8;
        } else if (choice == 2) {
            // The player's second tech instead.
            st = status(other, faction);
            if (!((st & TREATY_PACT) && (st & TREATY_SURRENDERED_))) {
                if (is_faction(other, 0x68d8bc) || (*game_net_multiplayer && faction == game_ranked_faction[0]))
                    goto second_refused;
                if (!is_faction(other, 0x68d8c4) && (great_satan(faction, 0) || O->tech_ranking > me->tech_ranking))
                    goto second_refused;
            }
            int t = *want[0];
            *want[0] = *want[1], *want[1] = t;
            worth = *game_trade_want_value[1] / 8;
        }
        // The trade.
        if (ai) net_treaty_on_nowait(other, faction, 0x1000000);
        if (*game_net_multiplayer) net_post(0x68d480, other, faction, 0x180, 0x444, other, faction, 0x180, 0);
        else agenda_on(other, faction, 0x180);
        if (*offer[0] < 0) *offer[0] = 9999;
        net_tech(other, *want[0], faction);
        net_tech(faction, *offer[0], other);
        if (*want[0] == 9999) *game_diplo_trade_offered = 1;
        if (*offer[0] == 9999) *game_diplo_trade_offered = 1;
        *want[0] = *want[1];
        *want[1] = -1;
        cause_friction(other, faction, -worth);
        *game_diplo_hostility = 0;
        if (!ai) return 1;
        goto again;
    }

second_refused:
    answer(text(0x68d8cc), other);
    O->diplo_patience[faction]++;
    return 0;

feared_leader:
    if (ai || buy_tech(faction, other, counter, 1, proposal)) return 0;
    say_title(0, faction), say_name(1, faction);
    game_parse_says(2, game_get_phrase(faction, 3), -1, -1);
    answer(label_digit(0x68d7fc, proposal == PROPOSE_COMMLINK ? 0 : game_random(0, 2)), other);
    return 0;

no_trade:
    if (ai || buy_tech(faction, other, counter, 0, proposal)) return 0;
    if (proposal == PROPOSE_COMMLINK) {
        game_str_buffer[0] = 0;
        strcat(game_str_buffer, text(0x68d864));
    } else {
        int digit = game_random(0, 3);
        if (status(faction, other) & (TREATY_PACT | TREATY_TREATY)) {
            if (*want[0] < 0) digit = 4;
            else if (*offer[0] < 0) digit = 3;
        }
        label_digit(0x68d870, digit);
    }
    answer(game_str_buffer, other);
    return 0;
}
int tech_trade_in(uint8_t *pop, int faction, int other, int ai, int counter, int proposal);
int tech_trade_in(uint8_t *pop, int faction, int other, int ai, int counter, int proposal) {
    game_popup_ctor(pop);
    int r = tech_trade_body(pop, faction, other, ai, counter, proposal);
    pop_end(pop);
    return r;
}
// The popup where the original's frame keeps it.
__asm__(".text\n"
        ".globl tech_trade\n"
        "tech_trade:\n"
        "    pushl %ebp\n"
        "    movl %esp, %ebp\n"
        "    subl $0x54f0, %esp\n"
        "    pushl 24(%ebp)\n"
        "    pushl 20(%ebp)\n"
        "    pushl 16(%ebp)\n"
        "    pushl 12(%ebp)\n"
        "    pushl 8(%ebp)\n"
        "    leal -0x54d4(%ebp), %eax\n"
        "    pushl %eax\n"
        "    call tech_trade_in\n"
        "    movl %ebp, %esp\n"
        "    popl %ebp\n"
        "    ret\n");

// The player's (`faction`'s) menus in the talks with the AI (`other`): the
// proposal (PROPOSALMENU), then for a joint attack or a commlink its
// faction (ATTACKSTRING; an AI at war with the target already says so,
// and one without a commlink to the faction asked for, or the Empath
// Guild, says it has none to give), for a base swap the AI's base (one the
// player has seen, not its headquarters, on the planet window); then,
// but for maps, closing, a loan repaid, a tech bought or the Council, the
// counter-proposal (COUNTERMENU), and for a base given the player's base.
// Returns the proposal (game_diplo_proposal), 0 when nothing is proposed
// or a menu can't be read; game_diplo_counter is 1 (friendship) when no
// counter is asked. The original's check that the AI isn't already at war
// with the target reads its diplo_status[8], past the array: its first
// agenda (Thinker fixes it).
static int proposal_menu_body(uint8_t *pop, int faction, int other) {
    struct faction_data *me = F(faction), *ai = F(other);
    *game_diplo_bid_base = *game_diplo_ask_base = -1;
    *game_diplo_second = other;
    *game_diplo_counter = *game_diplo_proposal = *game_diplo_proposal_faction = 0;
    int swappable = 0, linked = 0, unlinked = 0;
    for (int b = 0; b < *game_base_count; b++) {
        const struct base *B = &game_bases[b];
        if (B->faction == other && (other == faction || (B->visibility & (1u << faction))) && !has_headquarters(b))
            swappable++;
    }
    for (int i = 1; i < FACTION_COUNT; i++) {
        if (!(*game_factions_alive >> i & 1) || i == faction || i == other) continue;
        if (status(faction, i) & TREATY_COMMLINK) linked++;
        else if (!empath(faction)) unlinked++;
    }
    say_title(0, other);
    game_parse_says(1, game_get_pact_hood(faction, other), -1, -1);
    game_parse_num(0, me->loan_balance[other]);
    game_diplomacy_caption(faction, other);
    pop_start_flags(pop, text(0x68de7c), 0x100000, other);
    if (game_x_text_open(*game_script_file, text(0x68de88))) return 0;
    uint32_t st = status(faction, other);
    item(pop, next_line(), 0);
    item(pop, next_line(), PROPOSE_GIFT);
    next_line();
    if ((st & TREATY_TREATY) && !(st & TREATY_PACT)) item(pop, game_str_buffer, PROPOSE_PACT);
    next_line();
    if (!(status(faction, other) & TREATY_TREATY)) item(pop, game_str_buffer, PROPOSE_TREATY);
    next_line();
    if (!(*game_rules & RULES_SCN_NO_TECH_TRADING)) item(pop, game_str_buffer, PROPOSE_TECH);
    next_line();
    next_line();
    if (unlinked) item(pop, game_str_buffer, PROPOSE_COMMLINK);
    item(pop, next_line(), PROPOSE_ENERGY);
    next_line();
    int loan = me->loan_balance[other];
    if (loan && me->energy_credits >= loan) item(pop, game_str_buffer, PROPOSE_REPAY_LOAN);
    next_line();
    next_line();
    if (linked) item(pop, game_str_buffer, PROPOSE_ATTACK);
    next_line();
    if (ai->base_count > 1 && swappable && !*game_net_multiplayer) item(pop, game_str_buffer, PROPOSE_BASE_SWAP);
    next_line();
    int proposal = *game_diplo_proposal = menu(pop);

    if (proposal == PROPOSE_ATTACK || proposal == PROPOSE_COMMLINK) {
        int attack = proposal == PROPOSE_ATTACK;
        pop_start_flags(pop, text(attack ? 0x68de98 : 0x68dec8), 0x100040, other);
        if (game_x_text_open(*game_script_file, text(attack ? 0x68dea8 : 0x68ded8))) goto cancel;
        game_text_get();
        for (int i = 1; i < FACTION_COUNT; i++) {
            if (!(*game_factions_alive >> i & 1) || i == faction || i == other) continue;
            int linked_i = (status(faction, i) & TREATY_COMMLINK) != 0;
            if (attack ? !linked_i && !game_is_human(i) : linked_i) continue;
            say_title(0, i), say_name(1, i), say_noun(2, i);
            game_str_buffer[0] = 0;
            game_parse_string(*game_text_line, game_str_buffer);
            item(pop, game_str_buffer, i);
        }
        int target = *game_diplo_proposal_faction = menu(pop);
        if (target < 0) goto cancel;
        if (attack) {
            if (ai->diplo_agenda[0] & TREATY_VENDETTA) {  // the original's diplo_status[8]
                say_title(0, target), say_name(1, *game_diplo_proposal_faction);
                answer(text(0x68deb8), other);
                goto cancel;
            }
        } else if (!(status(other, target) & TREATY_COMMLINK) && !empath(other)) {
            say_title(0, target), say_name(1, *game_diplo_proposal_faction), say_noun(2, *game_diplo_proposal_faction);
            *game_diplo_proposal = 0;
            game_x_pops(text(0x68dee8), game_faction_portraits[other], game_pop_wait);
        }
    }
    if (*game_diplo_proposal == PROPOSE_BASE_SWAP) {
        game_plan_win_init(plan_win(), 0);
        game_plan_win_back_redraw(plan_win());
        pop_start_flags(pop, text(0x68def4), 0x100040, other);
        int listed = 0;
        for (int b = 0; b < *game_base_count; b++) {
            const struct base *B = &game_bases[b];
            if (B->faction != other || (other != faction && !(B->visibility & (1u << faction))) || has_headquarters(b))
                continue;
            game_str_buffer[0] = 0;
            say_base(game_str_buffer, b);
            item(pop, game_str_buffer, b);
            if (!listed) listed = 1, plan_win_line(b);
        }
        if (listed) {
            plan_win_show();
            *(uint32_t *)(pop + POP_LIST_SELECT) = game_va(0x54b140);  // battle_routine()
            if ((*game_diplo_ask_base = menu(pop)) < 0) *game_diplo_proposal = 0;
        } else {
            *game_diplo_ask_base = -1;
            *game_diplo_proposal = 0;
        }
        game_plan_win_close(plan_win());
    }
    proposal = *game_diplo_proposal;
    if (!proposal || proposal == PROPOSE_MAPS || proposal == PROPOSE_CLOSE || proposal == PROPOSE_REPAY_LOAN ||
        proposal == PROPOSE_BUY_TECH || proposal == PROPOSE_COUNCIL)
        goto done;

    // The counter-proposal.
    say_title(0, other), say_name(1, other);
    game_parse_says(2, game_get_pact_hood(faction, other), -1, -1);
    game_diplomacy_caption(faction, other);
    char label[32];
    snprintf(label, sizeof(label), "%s%d", text(0x68df00), *game_diplo_proposal == PROPOSE_GIFT);
    strcpy(game_str_buffer, label);
    pop_start_flags(pop, game_str_buffer, 0x100000, other);
    if (game_x_text_open(*game_script_file, text(0x68df08))) return 0;
    item(pop, next_line(), 0);
    next_line();
    if (*game_diplo_proposal == PROPOSE_ENERGY) item(pop, game_str_buffer, COUNTER_LOAN);
    next_line();
    if (*game_diplo_proposal != PROPOSE_GIFT) item(pop, game_str_buffer, COUNTER_FRIENDSHIP);
    next_line();
    if (*game_diplo_proposal != PROPOSE_GIFT) item(pop, game_str_buffer, COUNTER_NAME_A_PRICE);
    next_line();
    if (*game_diplo_proposal != PROPOSE_GIFT && !(status(faction, other) & TREATY_PACT))
        item(pop, game_str_buffer, COUNTER_THREATEN);
    next_line();
    if (*game_diplo_proposal != PROPOSE_GIFT && (status(faction, other) & TREATY_PACT))
        item(pop, game_str_buffer, COUNTER_THREATEN);
    next_line();
    if (!(*game_rules & RULES_SCN_NO_TECH_TRADING)) item(pop, game_str_buffer, COUNTER_RESEARCH);
    next_line();
    if (*game_diplo_proposal == PROPOSE_GIFT) item(pop, game_str_buffer, COUNTER_ALL_RESEARCH);
    next_line();
    if (*game_diplo_proposal != PROPOSE_ENERGY) item(pop, game_str_buffer, COUNTER_ENERGY);
    next_line();
    if (me->base_count > 1 && !*game_net_multiplayer &&
        (*game_diplo_proposal == PROPOSE_BASE_SWAP || *game_diplo_proposal == PROPOSE_GIFT))
        item(pop, game_str_buffer, COUNTER_BASE);
    int counter = *game_diplo_counter = menu(pop);
    if (counter == COUNTER_BASE) {
        game_plan_win_init(plan_win(), 0);
        game_plan_win_back_redraw(plan_win());
        pop_start_flags(pop, text(0x68df14), 0x100040, other);
        int listed = 0;
        for (int b = 0; b < *game_base_count; b++) {
            if (game_bases[b].faction != faction || has_headquarters(b)) continue;
            game_str_buffer[0] = 0;
            say_base(game_str_buffer, b);
            item(pop, game_str_buffer, b);
            if (!listed) listed = 1, plan_win_line(b);
        }
        plan_win_show();
        *(uint32_t *)(pop + POP_LIST_SELECT) = game_va(0x54b140);
        if ((*game_diplo_bid_base = menu(pop)) < 0) *game_diplo_proposal = 0;
        game_plan_win_close(plan_win());
        counter = *game_diplo_counter;
    }
    if (!counter) *game_diplo_proposal = 0;
    return *game_diplo_proposal;

cancel:
    *game_diplo_proposal = 0;
done:
    *game_diplo_counter = COUNTER_FRIENDSHIP;
    return *game_diplo_proposal;
}
int proposal_menu_in(uint8_t *pop, int faction, int other);
int proposal_menu_in(uint8_t *pop, int faction, int other) {
    game_popup_ctor(pop);
    int r = proposal_menu_body(pop, faction, other);
    pop_end(pop);
    return r;
}
// The popup where the original's frame keeps it.
__asm__(".text\n"
        ".globl proposal_menu\n"
        "proposal_menu:\n"
        "    pushl %ebp\n"
        "    movl %esp, %ebp\n"
        "    subl $0x5410, %esp\n"
        "    pushl 12(%ebp)\n"
        "    pushl 8(%ebp)\n"
        "    leal -0x53f4(%ebp), %eax\n"
        "    pushl %eax\n"
        "    call proposal_menu_in\n"
        "    movl %ebp, %esp\n"
        "    popl %ebp\n"
        "    ret\n");

// Whether the Council menu of the talks lists the proposal for the player
// to put: by its tech (or in omniscient debugging), the Supreme Leader
// under the diplomatic victory rule while the game goes on, unpassed, for
// an eligible player, and the rest by what has passed before.
static int council_listed(int faction, int prop) {
    const int32_t *passes = game_proposal_passes;
    if (!has_tech(game_proposals[prop].preq_tech, faction) &&
        !((*game_state & GAME_STATE_OMNISCIENT) && (*game_state & GAME_STATE_DEBUG)))
        return 0;
    if (prop == PROP_SUPREME_LEADER && !(*game_rules & RULES_VICTORY_DIPLOMATIC)) return 0;
    switch (prop) {
    case PROP_SUPREME_LEADER: return !(*game_state & GAME_STATE_DONE) && !passes[prop] && eligible(faction);
    case PROP_UNITY_CORE:
    case PROP_SOLAR_SHADE: return !passes[prop];
    case PROP_TRADE_PACT: return passes[prop] <= passes[PROP_REPEAL_TRADE_PACT];
    case PROP_REPEAL_TRADE_PACT: return passes[prop] != passes[PROP_TRADE_PACT];
    case PROP_INCREASE_SHADE: return passes[PROP_SOLAR_SHADE] != 0;
    case PROP_REPEAL_CHARTER: return passes[prop] <= passes[PROP_REINSTATE_CHARTER];
    case PROP_REINSTATE_CHARTER: return passes[prop] < passes[PROP_REPEAL_CHARTER];
    default: return 1;
    }
}

// The player's (`faction`'s) proposal to the AI (`other`), picked in the
// menus (proposal_menu(), unless `skip`, when game_diplo_proposal and
// game_diplo_counter are set already), put to the AI: a pact or a treaty
// (a threat following one that the AI didn't settle), maps traded (when
// in a pact or without friction, and friction at most 4; patience up 2),
// a joint attack (from an AI that surrendered, whatever the counter),
// else a threat, a joint attack, a base, a tech trade (whose answer and
// an energy request's, in game_diplo_menu_a), a gift, a loan repaid, or
// the Council: a proposal the player may put (as the Council's own menu
// lists them, by the player's techs) and a vote bought for it
// (buy_council_vote()), or for governor, a candidate.
static void make_a_proposal_body(uint8_t *pop, int faction, int other, int skip) {
    *game_diplo_second = other;
    *game_diplo_menu_a = 0;
    if (!skip && !proposal_menu(faction, other)) return;
    int counter = *game_diplo_counter, threat = counter == COUNTER_THREATEN;
    *game_diplo_menu_b = 1;
    int proposal = *game_diplo_proposal;
    if (proposal == PROPOSE_PACT) {
        if (!propose_pact(faction, other) && threat) threaten(faction, other);
        return;
    }
    if (proposal == PROPOSE_TREATY) {
        if (!propose_treaty(faction, other) && threat) threaten(faction, other);
        return;
    }
    if (proposal == PROPOSE_MAPS) {
        *game_diplo_second = other;
        int friction = *game_diplo_friction;
        if (((status(faction, other) & TREATY_PACT) || friction <= 0) && friction <= 4) {
            answer(text(0x68da68), other);
            F(other)->diplo_patience[faction] += 2;
            game_net_maps(faction, other, 1);
            *game_diplo_maps_news = 1;
        } else {
            game_parse_says(0, game_faction_defs[other].adjective, -1, -1);
            answer(text(0x68da60), other);
        }
        return;
    }
    if (proposal == PROPOSE_CLOSE) return;
    if (proposal == PROPOSE_ATTACK && (status(other, faction) & TREATY_PACT) &&
        (status(other, faction) & TREATY_SURRENDERED_)) {
        propose_attack(faction, other);
        return;
    }
    if (counter == COUNTER_THREATEN) {
        threaten(faction, other);
        return;
    }
    switch (proposal) {
    case PROPOSE_ATTACK: propose_attack(faction, other); return;
    case PROPOSE_BASE_SWAP: base_swap(faction, other); return;
    case PROPOSE_TECH:
    case PROPOSE_BUY_TECH:
    case PROPOSE_COMMLINK: *game_diplo_menu_a = tech_trade(faction, other, 0, counter, proposal); return;
    case PROPOSE_ENERGY: *game_diplo_menu_a = energy_trade(faction, other); return;
    case PROPOSE_GIFT: make_gift(faction, other); return;
    }
    if (proposal == PROPOSE_REPAY_LOAN) {
        say_title(0, faction), say_name(1, faction);
        int loan = F(faction)->loan_balance[other];
        net_energy(faction, -loan, other);
        if (*game_net_multiplayer) {
            net_post(0x68d4cc, faction, other, 0, 0x44e, faction, other, 0, 0);
        } else {
            F(faction)->loan_balance[other] = 0;
            F(faction)->loan_payment[other] = 0;
        }
        answer(text(0x68df20), other);
        proposal = *game_diplo_proposal;
    }
    if (proposal != PROPOSE_COUNCIL) return;
    pop_start_flags(pop, text(0x68df2c), 0x100040, other);
    for (int prop = 0; prop < PROP_COUNT; prop++) {
        if (!council_listed(faction, prop)) continue;
        game_str_buffer[0] = 0;
        strcat(game_str_buffer, game_strings_get(game_strings, game_proposals[prop].name));
        strcat(game_str_buffer, text(0x68df3c));
        strcat(game_str_buffer, game_strings_get(game_strings, game_proposals[prop].description));
        item(pop, game_str_buffer, prop);
    }
    int prop = menu(pop);
    if (prop < 0) return;
    if (prop) {
        int vote = answer(text(0x68df40), other);
        if (-1 - vote) buy_council_vote(faction, other, prop, -1 - vote);
        return;
    }
    pop_start_flags(pop, text(0x68df4c), 0x100000, other);
    for (int i = 1; i < FACTION_COUNT; i++) {
        if (!(*game_factions_alive >> i & 1) || !eligible(i)) continue;
        struct faction_def *d = &game_faction_defs[i];
        game_str_buffer[0] = 0;
        *game_parse_gender = d->leader_female, *game_parse_plural = 0;
        strcat(game_str_buffer, d->leader_title);
        strcat(game_str_buffer, text(0x682820));
        *game_parse_gender = d->leader_female, *game_parse_plural = 0;
        strcat(game_str_buffer, d->leader_name);
        item(pop, game_str_buffer, i);
        if (i == faction) *(int32_t *)(pop + POP_DEFAULT) = i;
    }
    int target = menu(pop);
    if (target >= 0) buy_council_vote(faction, other, PROP_GOVERNOR, target);
}
void make_a_proposal_in(uint8_t *pop, int faction, int other, int skip);
void make_a_proposal_in(uint8_t *pop, int faction, int other, int skip) {
    game_popup_ctor(pop);
    make_a_proposal_body(pop, faction, other, skip);
    pop_end(pop);
}
// The popup where the original's frame keeps it.
__asm__(".text\n"
        ".globl make_a_proposal\n"
        "make_a_proposal:\n"
        "    pushl %ebp\n"
        "    movl %esp, %ebp\n"
        "    subl $0x53a0, %esp\n"
        "    pushl 16(%ebp)\n"
        "    pushl 12(%ebp)\n"
        "    pushl 8(%ebp)\n"
        "    leal -0x5388(%ebp), %eax\n"
        "    pushl %eax\n"
        "    call make_a_proposal_in\n"
        "    movl %ebp, %esp\n"
        "    popl %ebp\n"
        "    ret\n");

// ---- battle_plans --------------------------------------------------------------------

// The player's pick of a target for the AI in battle_plans().
static void battle_target(int faction, int other, int target, int theirs) {
    struct faction_data *ai = F(other);
    const struct base *T = &game_bases[target], *Bt = &game_bases[theirs];
    game_parse_says(0, (char *)T->name, -1, -1);
    if (T->faction != other && !(T->visibility & (1u << other))) spot_loc(T->x, T->y, other);
    int region = tile_at(T->x, T->y)->region;
    if (region >= 64 || !ai->region_total_bases[region]) {
        int own = tile_at(Bt->x, Bt->y)->region, invading = 0;
        if (ai->region_total_bases[own]) {
            for (int r = 0; r < 64 && !invading; r++)
                if ((r & 63) != 63 && (r & 63) && ai->region_total_bases[r] && ai->region_base_plan[r] == 7)
                    invading = 1;
            if (!invading) {
                answer(text(0x68dd6c), other);
                return;
            }
        }
    }
    game_base_find2(Bt->x, Bt->y, other);
    int near = *game_base_find_dist;
    game_base_find2(T->x, T->y, other);
    if (*game_base_find_dist >= 2 * near) {
        answer(text(0x68dd78), other);
        return;
    }
    ai->attack_target_base = target;
    answer(text(0x68dd84), other);
    if (ai->target_x < 0) return;
    if (tile_at(ai->target_x, ai->target_y)->region == tile_at(T->x, T->y)->region) return;
    for (int v = 0; v < *game_veh_count; v++) {
        struct veh *V = &game_vehs[v];
        if (V->faction == other && (V->state & 0x40000)) V->state &= 0xfff9ffffu;
    }
    ai->target_x = ai->target_y = -1;
    ai->target_kinds = 0, ai->target_units = 0;
}

// The AI (`other`) brings the player (`faction`) battle plans: each side's
// target base (suggest_plan()) and where it would attack from
// (attack_from()). Outside a pact it first wants maps (refusing outright
// while it has grudges or the 0x8000 bit against the player, captured
// bases, friction of 18 or more or hostility; the player may decline,
// unless game_diplo_trade_offered). Without targets on both sides it says
// so. Otherwise the planet window shows the plans, the AI takes its
// target, and the player may pick a base of a common enemy that one of
// them has seen for it instead: the AI declines one it has no bases near
// (unless its target's continent has none of its bases either, or it has
// a plan to invade another), or one twice as far from its bases as its
// own target; else it takes it, and drops a target on another continent
// with the units bound for it.
static void battle_plans_body(uint8_t *pop, int faction, int other) {
    struct faction_data *ai = F(other);
    *game_diplo_second = other;
    *game_diplo_plans_faction = faction, *game_diplo_plans_other = other;
    int mine = game_suggest_plan(faction, other), theirs = game_suggest_plan(other, faction);
    int mine_from = game_attack_from(mine, faction), theirs_from = game_attack_from(theirs, other);
    if (!(status(faction, other) & TREATY_PACT)) {
        if ((status(other, faction) & (TREATY_WANT_REVENGE | TREATY_UNK_8000)) || *game_diplo_captured_bases ||
            *game_diplo_friction >= 18 || *game_diplo_hostility) {
            answer(text(0x68dd20), other);
            return;
        }
        if (!*game_diplo_trade_offered && !answer(text(0x68dd34), other)) return;
        if (*game_net_multiplayer) net_send(0x68d4dc, faction, other, 0, 0x448, faction, other, 0, 0);
        else trade_maps(faction, other);
    }
    if (mine < 0 || theirs < 0) {
        answer(text(0x68dd44), other);
        return;
    }
    const struct base *A = &game_bases[mine], *Bt = &game_bases[theirs], *Af = &game_bases[mine_from],
                      *Bf = &game_bases[theirs_from];
    int32_t *colors = game_at(0x68f9f0);
    game_plan_win_init(plan_win(), 0);
    game_plan_win_find_view(plan_win(), A->x, A->y, Bt->x, Bt->y, Af->x, Af->y, Bf->x, Bf->y);
    game_plan_win_back_redraw(plan_win());
    game_plan_win_add_line_2(plan_win(), mine_from, mine, colors[faction]);
    game_plan_win_add_line_2(plan_win(), theirs_from, theirs, colors[other]);
    plan_win_show();
    ai->attack_target_base = theirs;
    game_parse_says(0, (char *)Bt->name, -1, -1);
    game_parse_says(1, (char *)A->name, -1, -1);
    if (answer(text(0x68dd54), other)) {
        say_title(0, faction), say_name(1, faction);
        game_popup_start(pop, game_pop_file, text(0x68dd60), -1, 0, 0x100042, 0);
        void *portrait = game_faction_portraits[other];
        *(void **)(pop + POP_ICON) = *(int32_t *)(pop + POP_HAS_ICON) ? NULL : portrait;
        game_sprite_box_sprite(pop + POP_SPRITE_BOX, portrait, 0, other);
        int listed = 0;
        for (int b = 0; b < *game_base_count; b++) {
            const struct base *B = &game_bases[b];
            int f = B->faction;
            if (f == faction || f == other || !(status(faction, f) & TREATY_VENDETTA) ||
                !(status(other, f) & TREATY_VENDETTA))
                continue;
            if (!(B->visibility & (1u << faction)) && !(B->visibility & (1u << other))) continue;
            item(pop, (char *)B->name, b);
            if (!listed) {
                int from_a = game_attack_from(b, *game_diplo_plans_faction);
                int from_b = game_attack_from(b, *game_diplo_plans_other);
                game_plan_win_clear_lines(plan_win());
                game_plan_win_add_line_2(plan_win(), from_a, b, colors[*game_diplo_plans_faction]);
                game_plan_win_add_line_2(plan_win(), from_b, b, colors[*game_diplo_plans_other]);
                game_plan_win_on_redraw(game_at(0x856dc0));
            }
            listed++;
        }
        int target = -1;
        if (listed) {
            *(uint32_t *)(pop + POP_LIST_SELECT) = game_va(0x54b140);  // battle_routine()
            target = menu(pop);
        }
        if (target >= 0) battle_target(faction, other, target, theirs);
    }
    game_plan_win_close(plan_win());
}

void battle_plans_in(uint8_t *pop, int faction, int other);
void battle_plans_in(uint8_t *pop, int faction, int other) {
    game_popup_ctor(pop);
    battle_plans_body(pop, faction, other);
    pop_end(pop);
}
// The popup where the original's frame keeps it.
__asm__(".text\n"
        ".globl battle_plans\n"
        "battle_plans:\n"
        "    pushl %ebp\n"
        "    movl %esp, %ebp\n"
        "    subl $0x53b0, %esp\n"
        "    pushl 12(%ebp)\n"
        "    pushl 8(%ebp)\n"
        "    leal -0x53a0(%ebp), %eax\n"
        "    pushl %eax\n"
        "    call battle_plans_in\n"
        "    movl %ebp, %esp\n"
        "    popl %ebp\n"
        "    ret\n");

// ---- introduce -----------------------------------------------------------------------

// The player (`faction`) meets the AI (`other`) in the diplomacy window, which
// opens when the player takes its call (wants_to_speak()): the leaders greet
// each other with the INTRO line of a kind chosen by how they stand, and
// a planet buster is mentioned. The AI's portrait shows the lines. The
// words the line may use (slots 0 to 4: the titles and names, and the AI's
// noun; 8 and 9: the AI's adjective and the player's leader's adjective) are
// set first, then those the kind adds from the player's faction's phrases
// (slots 5, 6 and 8).
// - NEW, if the player has not met them (or, in a network or PBEM game, when
//   the console asks again: game_diplo_state's bit 2): a phrase of the AI's;
//   at one in 6 the player's noun, or at one in 3 a word for how mighty it is
//   (the faction's ranking), with "a" or "an" before it in English (the
//   original tells a vowel by the capital letters A E I O U only, where
//   Thinker's version also takes the small ones).
// - MAD, when the AI wants revenge (the player's status towards it has
//   0x20): five ways to insult, by the player's phrases, adjectives and
//   the leader's insult.
// - Otherwise four ways of describing the player, kindly in a pact or
//   without hostility, insulting otherwise.
// Then the planet busters: the AI's (a line, as soon as it has any, where
// Thinker's version asks for the bit 0x4000 or a roll against its mood), and
// the player's (once, with the bit 0x4000 set on the status towards the AI,
// which is announced to the other players in a network game).
void introduce_in(uint8_t *pop, int faction, int other);
void introduce_in(uint8_t *pop, int faction, int other) {
    game_popup_ctor(pop);
    struct faction_def *me = &game_faction_defs[faction], *ai = &game_faction_defs[other];
    struct faction_data *player = F(faction), *rival = F(other);
    *game_diplo_second = other;
    *(int32_t *)game_at(0x93fab4) = 1;  // the talks' window state
    game_diplomacy_caption(faction, other);
    say_title(0, faction), say_name(1, faction);
    say_title(2, other), say_name(3, other);
    say_noun(4, other);
    game_parse_says(8, ai->adjective, -1, -1);
    game_parse_says(9, me->adj_leader, -1, -1);
    game_str_buffer[0] = 0;
    strcat(game_str_buffer, text(0x68d624));
    int roll;
    if (!(status(faction, other) & TREATY_UNK_8000000) ||
        ((*game_net_multiplayer || *game_pbem) && (game_diplo_state[other] & 2))) {
        strcat(game_str_buffer, text(0x68d62c));
        roll = game_random(0, 6);
        game_parse_says(6, game_get_phrase(other, 1), -1, -1);
        if (roll == 2) {
            say_noun(5, faction);
        } else if (roll == 3) {
            char buf[80];
            int might = 7 - player->ranking;
            might = might < 0 ? 0 : might > 6 ? 6 : might;
            char *adj = game_strings_get(game_strings, *(char **)game_at(0x94c55c + might * 8));
            buf[0] = 0;
            if (!*game_language) {
                char c = adj[0];
                int vowel = c == 'A' || c == 'E' || c == 'I' || c == 'O' || c == 'U';
                strcat(buf, game_label(vowel ? 0x115 : 0x114));
                strcat(buf, text(0x682820));
            }
            strcat(buf, adj);
            game_parse_says(5, buf, -1, -1);
        }
    } else if (status(faction, other) & TREATY_WANT_REVENGE) {
        strcat(game_str_buffer, text(0x68d630));
        roll = game_random(0, 5);
        switch (roll) {
        case 0:
            game_parse_says(5, game_get_phrase(faction, 3), -1, -1);
            game_parse_says(6, game_get_noun_phrase(faction, 8), -1, -1);
            break;
        case 1:
            game_parse_says(5, me->adj_insult_faction, -1, -1);
            game_parse_says(6, game_get_phrase(faction, 3), -1, -1);
            break;
        case 2:
            game_parse_says(6, game_get_noun_phrase(faction, 8), -1, -1);
            break;
        case 3:
            game_parse_says(8, game_get_phrase(faction, 6), -1, -1);
            break;
        default:
            game_parse_says(5, me->adj_insult_leader, -1, -1);
            *game_parse_gender = me->leader_female, *game_parse_plural = 0;
            game_parse_says(6, me->insult_leader, -1, -1);
            game_parse_says(8, game_get_phrase(faction, 6), -1, -1);
            break;
        }
    } else {
        roll = game_random(0, 4);
        int kind = (status(faction, other) & TREATY_PACT) || !*game_diplo_hostility;
        switch (roll) {
        case 0:
            game_parse_says(5, game_get_noun_phrase(faction, kind ? 9 : 10), -1, -1);
            break;
        case 1:
            game_parse_says(9, kind ? me->adj_leader : me->adj_insult_leader, -1, -1);
            break;
        case 2:
            game_parse_says(5, kind ? me->adj_faction : me->adj_insult_faction, -1, -1);
            break;
        default:
            game_parse_says(9, kind ? me->adj_leader : me->adj_insult_leader, -1, -1);
            game_parse_says(5, kind ? me->adj_faction : me->adj_insult_faction, -1, -1);
            break;
        }
    }
    char digits[12];
    snprintf(digits, sizeof(digits), "%d", roll);
    strcat(game_str_buffer, digits);
    answer(game_str_buffer, other);
    if (rival->planet_busters) answer(text(0x68d634), other);
    if (player->planet_busters && !(status(faction, other) & 0x4000)) {
        game_str_buffer[0] = 0;
        strcat(game_str_buffer, text(0x68d644));
        snprintf(digits, sizeof(digits), "%d", (rival->planet_busters != 0) + 1);
        strcat(game_str_buffer, digits);
        answer(game_str_buffer, other);
        net_set_treaty(faction, other, 0x4000, 1);
    }
    pop_end(pop);
}
// The popup where the original's frame keeps it.
__asm__(".text\n"
        ".globl introduce\n"
        "introduce:\n"
        "    pushl %ebp\n"
        "    movl %esp, %ebp\n"
        "    subl $0x5440, %esp\n"
        "    pushl 12(%ebp)\n"
        "    pushl 8(%ebp)\n"
        "    leal -0x5430(%ebp), %eax\n"
        "    pushl %eax\n"
        "    call introduce_in\n"
        "    movl %ebp, %esp\n"
        "    popl %ebp\n"
        "    ret\n");

// ---- Shadow mode -----------------------------------------------------------------------
//
// The game calls these only in the player's talks, where every line is a
// dialog in the diplomacy window (which allocates from the game's heap)
// and some ask the player, so a call from the game runs the original
// unchecked. The sweep checks them on copies of the game in which the
// first faction is made human and the player, with the talks' variables
// as the menus leave them: there the dialogs, the Datalinks and the
// phrases read from a faction's file are held (quiet.c), each run of a
// check gets the same made-up answers, and the two runs' dialogs (their
// labels) are compared with the rest of the game's state. A menu's answer
// (base_pop_exec_3()) is quiet_answer's, one the menu would offer.

extern struct hook_module diplomacy_module;
#define DIPLO (&diplomacy_module)

#define TALK_CHECK(ret, name, params, args, fmt, ...)                                            \
    SHADOW_DECLARE(ret, name, params)                                                            \
    static ret check_##name params {                                                             \
        struct hook_writes w_ = {0};                                                             \
        hooks_writes_game(&w_);                                                                  \
        hooks_writes_begin(&w_);                                                                 \
        quiet_talk_next = 0, quiet_menu_next = 0;                                                \
        hooks_nested++;                                                                          \
        SHADOW_RESULT_##ret(want) orig_##name args;                                              \
        hooks_nested--;                                                                          \
        hooks_writes_middle(&w_);                                                                \
        quiet_talk_next = 0, quiet_menu_next = 0;                                                \
        SHADOW_RESULT_##ret(got) name args;                                                      \
        const char *diff_ = hooks_writes_end(&w_, SHADOW_INT_##ret(got), SHADOW_INT_##ret(want)); \
        if (diff_) hooks_mismatch(&stats_##name, "(" fmt ") %s", __VA_ARGS__, diff_);            \
        hooks_ui_release();                                                                      \
        SHADOW_RETURN_##ret(want);                                                               \
    }                                                                                            \
    static ret shadow_##name params {                                                            \
        if (hooks_nested) SHADOW_TAIL_##ret(orig_##name args);                                   \
        if (shadow_bypass()) SHADOW_TAIL_##ret(name args);                                       \
        stats_##name.calls++;                                                                    \
        shadow_tick(DIPLO);                                                                      \
        SHADOW_TAIL_##ret(orig_##name args);                                                     \
    }

// The same for an original with a popup on its stack: both runs are made
// at the same stack depth (talk_call()), so that the popup is at the same
// address, as its parts point at each other and the game keeps some of
// those pointers. A menu's answer is quiet_answer's.
int talk_call(void *fn, int a, int b, int c);
__asm__(".text\n"
        "talk_call:\n"
        "    pushl %ebp\n"
        "    movl %esp, %ebp\n"
        "    pushl 20(%ebp)\n"
        "    pushl 16(%ebp)\n"
        "    pushl 12(%ebp)\n"
        "    call *8(%ebp)\n"
        "    movl %ebp, %esp\n"
        "    popl %ebp\n"
        "    ret\n");
#define TALK_CHECK_POP(name, has_result)                                                           \
    static struct hook_stats stats_##name = {#name};                                               \
    static void *orig_##name;                                                                      \
    static int check_##name(int faction, int other, int c_) {                                              \
        struct hook_writes w_ = {0};                                                               \
        hooks_writes_game(&w_);                                                                    \
        hooks_writes_begin(&w_);                                                                   \
        quiet_talk_next = quiet_menu_next = 0;                                                     \
        hooks_nested++;                                                                            \
        int want = talk_call(orig_##name, faction, other, c_);                                         \
        hooks_nested--;                                                                            \
        hooks_writes_middle(&w_);                                                                  \
        quiet_talk_next = quiet_menu_next = 0;                                                     \
        int got = talk_call((void *)name, faction, other, c_);                                         \
        if (!(has_result)) want = got = 0;                                                         \
        const char *diff_ = hooks_writes_end(&w_, got, want);                                      \
        if (diff_)                                                                                 \
            hooks_mismatch(&stats_##name, "(%d, %d; counter %d, menu %d) %s", faction, other,      \
                           *game_diplo_counter, quiet_answer, diff_);                              \
        hooks_ui_release();                                                                        \
        return want;                                                                               \
    }                                                                                              \
    static int shadow_##name(int faction, int other, int c_) {                                             \
        if (hooks_nested) return talk_call(orig_##name, faction, other, c_);                           \
        if (shadow_bypass()) return talk_call((void *)name, faction, other, c_);                       \
        stats_##name.calls++;                                                                      \
        shadow_tick(DIPLO);                                                                        \
        return talk_call(orig_##name, faction, other, c_);                                             \
    }

TALK_CHECK(void, threaten, (int a, int b), (a, b), "%d, %d", a, b)
TALK_CHECK(int, energy_trade, (int a, int b), (a, b), "%d, %d", a, b)
TALK_CHECK(int, propose_pact, (int a, int b), (a, b), "%d, %d", a, b)
TALK_CHECK(int, propose_treaty, (int a, int b), (a, b), "%d, %d", a, b)
TALK_CHECK(int, buy_tech, (int a, int b, int c, int d, int e), (a, b, c, d, e), "%d, %d, %d, %d, %d", a, b, c, d, e)
TALK_CHECK(int, base_swap, (int a, int b), (a, b), "%d, %d", a, b)
TALK_CHECK_POP(make_gift, 0)
TALK_CHECK_POP(propose_attack, 1)
TALK_CHECK_POP(proposal_menu, 1)
TALK_CHECK_POP(make_a_proposal, 0)
TALK_CHECK_POP(battle_plans, 0)
TALK_CHECK_POP(introduce, 0)

// tech_trade() has five arguments: its checks call both runs at the same
// depth too.
int talk_call5(void *fn, int a, int b, int c, int d, int e);
__asm__(".text\n"
        "talk_call5:\n"
        "    pushl %ebp\n"
        "    movl %esp, %ebp\n"
        "    pushl 28(%ebp)\n"
        "    pushl 24(%ebp)\n"
        "    pushl 20(%ebp)\n"
        "    pushl 16(%ebp)\n"
        "    pushl 12(%ebp)\n"
        "    call *8(%ebp)\n"
        "    movl %ebp, %esp\n"
        "    popl %ebp\n"
        "    ret\n");
static struct hook_stats stats_tech_trade = {"tech_trade"};
static void *orig_tech_trade;
static int check_tech_trade(int faction, int other, int ai, int counter, int proposal) {
    struct hook_writes w_ = {0};
    hooks_writes_game(&w_);
    hooks_writes_begin(&w_);
    quiet_talk_next = quiet_menu_next = 0;
    hooks_nested++;
    int want = talk_call5(orig_tech_trade, faction, other, ai, counter, proposal);
    hooks_nested--;
    hooks_writes_middle(&w_);
    quiet_talk_next = quiet_menu_next = 0;
    int got = talk_call5((void *)tech_trade, faction, other, ai, counter, proposal);
    const char *diff_ = hooks_writes_end(&w_, got, want);
    if (diff_)
        hooks_mismatch(&stats_tech_trade, "(%d, %d, %d, %d, %d; menu %d) %s", faction, other, ai, counter, proposal,
                       quiet_menu_answers[0], diff_);
    hooks_ui_release();
    return want;
}
static int shadow_tech_trade(int faction, int other, int ai, int counter, int proposal) {
    if (hooks_nested) return talk_call5(orig_tech_trade, faction, other, ai, counter, proposal);
    if (shadow_bypass()) return talk_call5((void *)tech_trade, faction, other, ai, counter, proposal);
    stats_tech_trade.calls++;
    shadow_tick(DIPLO);
    return talk_call5(orig_tech_trade, faction, other, ai, counter, proposal);
}

// trade_maps() and give_a_base() ask nothing: they are checked, whoever
// calls them, on all of the game's state and the map.
static void writes_map(struct hook_writes *w) {
    hooks_writes_game(w);
    if (*game_map) hooks_writes_add(w, *game_map, (size_t)*game_map_half_width * *game_map_height * sizeof(struct map_tile));
}
SHADOW_WRITER(DIPLO, void, trade_maps, (int a, int b), (a, b), writes_map(&w_), "%d, %d", a, b)
SHADOW_WRITER(DIPLO, void, give_a_base, (int a, int b), (a, b), writes_map(&w_), "%d, %d", a, b)

// ---- Sweep ---------------------------------------------------------------------------

static uint32_t mix(uint32_t a) {
    a ^= a >> 16, a *= 0x7feb352du;
    a ^= a >> 15, a *= 0x846ca68bu;
    return a ^ (a >> 16);
}
static uint32_t seed;
// Whether a case's proposal is put through make_a_proposal() (its
// dispatch), past the menus, rather than to the function itself.
static int whole;
static int rnd(int n) {
    seed = mix(seed + 0x9e3779b9u);
    return (int)(seed % (uint32_t)n);
}

static int alive(int faction) { return faction > 0 && faction < FACTION_COUNT && (*game_factions_alive >> faction & 1); }

// The standing between the player and the AI: none, a truce, a treaty, a
// pact (with a treaty), or a vendetta; in a pact one side may have
// surrendered to the other.
static void standing(int faction, int other) {
    static const uint32_t kinds[] = {0, TREATY_TRUCE_, TREATY_TREATY, TREATY_PACT | TREATY_TREATY, TREATY_VENDETTA};
    const uint32_t mask = TREATY_PACT | TREATY_TREATY | TREATY_TRUCE_ | TREATY_VENDETTA | TREATY_SURRENDERED_;
    uint32_t k = kinds[rnd(5)];
    F(faction)->diplo_status[other] = (status(faction, other) & ~mask) | k | TREATY_COMMLINK;
    F(other)->diplo_status[faction] = (status(other, faction) & ~mask) | k | TREATY_COMMLINK;
    if ((k & TREATY_PACT) && !rnd(3)) F(other)->diplo_status[faction] |= TREATY_SURRENDERED_;
    else if ((k & TREATY_PACT) && !rnd(6)) F(faction)->diplo_status[other] |= TREATY_SURRENDERED_;
}

// A faction for a joint attack (proposal_menu(): any other living faction
// the player has a commlink with, or a human one), or for a commlink (one
// the player has no commlink with and the AI has); 0 if none.
static int third_faction(int faction, int other, int commlink) {
    int start = rnd(FACTION_COUNT);
    for (int k = 0; k < FACTION_COUNT; k++) {
        int t = (start + k) % FACTION_COUNT;
        if (!alive(t) || t == faction || t == other) continue;
        int linked = (status(faction, t) & TREATY_COMMLINK) != 0;
        if (commlink ? !linked && (status(other, t) & TREATY_COMMLINK) : linked || ((*game_human_factions >> t) & 1))
            return t;
    }
    return 0;
}
// A base of the AI's the player can see (one it is made to have seen),
// without its headquarters, for a swap; -1 if none.
static int swap_base(int faction, int other) {
    int n = *game_base_count, start = rnd(n > 0 ? n : 1);
    for (int k = 0; k < n; k++) {
        int b = (start + k) % n;
        struct base *B = &game_bases[b];
        if (B->faction != other || game_has_facility(B, FAC_HEADQUARTERS)) continue;
        B->visibility |= (uint8_t)(1u << faction);
        return b;
    }
    return -1;
}

// The talks set up between a player and an AI as communicate() and the
// menus leave them, with some of their inputs varied. Returns the AI, or
// 0 if `faction` has no one to talk to.
static int talks(int faction) {
    int other = 0, start = rnd(FACTION_COUNT);
    for (int k = 0; k < FACTION_COUNT && !other; k++) {
        int o = (start + k) % FACTION_COUNT;
        if (alive(o) && o != faction) other = o;
    }
    if (!other) return 0;
    *game_human_factions = (uint8_t)(1u << faction);
    if (!rnd(4)) *game_human_factions |= (uint8_t)(1u << (1 + rnd(FACTION_COUNT - 1)));
    *game_human_factions &= (uint8_t)~(1u << other);
    *game_current_faction = faction;
    // The player took the call (wants_to_speak()), so the window is open.
    *game_diplo_caption_off = 1, *game_diplo_caption_mode = 1;
    struct faction_data *me = F(faction), *ai = F(other);
    if (rnd(3)) standing(faction, other);
    if (rnd(2)) me->diff_level = rnd(6);
    if (rnd(2)) me->ranking = 1 + rnd(7), ai->ranking = 1 + rnd(7);
    if (rnd(2)) me->tech_ranking = rnd(60), ai->tech_ranking = rnd(60);
    if (rnd(2)) ai->energy_credits = rnd(3) ? rnd(600) : rnd(5000);
    if (rnd(2)) me->energy_credits = rnd(3) ? rnd(600) : rnd(5000);
    if (rnd(2)) me->mil_strength = rnd(400), ai->mil_strength = rnd(400);
    if (!rnd(3)) me->best_weapon_value = rnd(12), ai->best_weapon_value = rnd(12);
    if (!rnd(3)) me->integrity_blemishes = rnd(6);
    if (!rnd(3)) ai->ai_fight = rnd(3) - 1, ai->ai_power = rnd(2), ai->ai_tech = rnd(2), ai->ai_wealth = rnd(2);
    if (!rnd(4)) me->diplo_agenda[other] ^= AGENDA_UNK_1000;
    // Living factions rank from 1 (diplomacy_check() divides by the
    // player's ranking).
    if (!me->ranking || !ai->ranking) return 0;
    diplomacy_check(faction, other, rnd(2));
    // Left over from other talks when not reckoned afresh: never the two
    // talking (as communicate()'s full reckoning leaves them).
    if (*game_diplo_help_faction == faction || *game_diplo_help_faction == other) *game_diplo_help_faction = 0;
    if (*game_diplo_accepted_faction == faction || *game_diplo_accepted_faction == other) *game_diplo_accepted_faction = 0;
    // What diplomacy_check() found, varied.
    if (rnd(2)) *game_diplo_fear = rnd(3) ? rnd(4) : rnd(12);
    if (!rnd(3)) *game_diplo_friction = rnd(25);
    if (!rnd(3)) *game_diplo_prev_hostility = rnd(5);
    if (!rnd(3)) *game_diplo_attack_chance = rnd(3);
    if (!rnd(3)) *game_diplo_exposed_bases = rnd(3);
    if (!rnd(3)) *game_diplo_great_satan = rnd(2);
    if (!rnd(4)) *game_diplo_can_betray = !rnd(3);
    if (!rnd(3)) *game_diplo_spoke_turn = *game_turn - rnd(60);
    return other;
}

// A proposal and its counter from the menus (proposal_menu()); 0 if this
// one couldn't be made.
static int proposal(int faction, int other, int kind, int counter) {
    *game_diplo_proposal = kind, *game_diplo_counter = counter;
    *game_diplo_proposal_faction = 0, *game_diplo_ask_base = *game_diplo_bid_base = -1;
    if (kind == PROPOSE_ATTACK || kind == PROPOSE_COMMLINK) {
        int t = third_faction(faction, other, kind == PROPOSE_COMMLINK);
        if (!t) return 0;
        *game_diplo_proposal_faction = t;
    } else if (kind == PROPOSE_BASE_SWAP) {
        if (F(other)->base_count <= 1 || (*game_diplo_ask_base = swap_base(faction, other)) < 0) return 0;
    }
    return 1;
}

static void answers(void) {
    for (int i = 0; i < QUIET_TALK_ANSWERS; i++) quiet_talk_answers[i] = rnd(4) ? rnd(2) : rnd(4);
}

static void sweep_threaten(int faction) {
    static const int kinds[] = {PROPOSE_PACT, PROPOSE_TREATY, PROPOSE_TECH, PROPOSE_ENERGY,
                                PROPOSE_ATTACK, PROPOSE_BASE_SWAP, PROPOSE_COMMLINK};
    int other = talks(faction);
    if (!other || !proposal(faction, other, kinds[rnd(7)], COUNTER_THREATEN)) return;
    int target = *game_diplo_proposal_faction;
    // The menu's check that the AI isn't already at war with the target
    // reads past the treaties (Thinker), so it may be.
    if (target && !rnd(4)) F(other)->diplo_status[target] ^= TREATY_VENDETTA;
    if (*game_diplo_proposal == PROPOSE_BASE_SWAP) {
        // A partner that surrendered, the base's region, and whether the
        // AI took it from the player.
        if (rnd(2)) {
            F(faction)->diplo_status[other] |= TREATY_PACT | TREATY_TREATY;
            F(other)->diplo_status[faction] |= TREATY_PACT | TREATY_TREATY | (rnd(3) ? TREATY_SURRENDERED_ : 0);
        }
        struct base *B = &game_bases[*game_diplo_ask_base];
        int region = region_at(B->x, B->y);
        if (rnd(2)) F(faction)->region_total_bases[region] = (uint8_t)rnd(5);
        if (rnd(2)) F(other)->region_total_bases[region] = (uint8_t)rnd(5);
        if (!rnd(3)) B->faction_former = (uint8_t)faction;
        if (rnd(2)) *game_diplo_fear = 1 + rnd(8);
    }
    answers();
    if (whole) check_make_a_proposal(faction, other, 1);
    else check_threaten(faction, other);
}

// Energy, for a loan, friendship, research or a price (the menu's
// counters to it but the threat); now and then a pact with a poorer player
// that would get it free, or a loan already owed.
static void sweep_energy_trade(int faction) {
    static const int counters[] = {COUNTER_FRIENDSHIP, COUNTER_NAME_A_PRICE, COUNTER_RESEARCH, COUNTER_LOAN};
    int other = talks(faction);
    if (!other) return;
    struct faction_data *me = F(faction), *ai = F(other);
    if (!rnd(4)) {
        me->ranking = 1 + rnd(3), ai->ranking = me->ranking + 1 + rnd(3);
        me->energy_credits = rnd(200), ai->energy_credits = 2 * me->energy_credits + *game_turn * 4 + rnd(800);
        ai->tech_ranking = me->tech_ranking + rnd(3);
        ai->diplo_patience[faction] = (int8_t)rnd(3);
        me->diplo_status[other] |= TREATY_PACT | TREATY_TREATY, ai->diplo_status[faction] |= TREATY_PACT | TREATY_TREATY;
    }
    if (!rnd(5)) *(rnd(2) ? &me->loan_balance[other] : &ai->loan_balance[faction]) = rnd(200);
    if (!rnd(3)) me->tech_commerce_bonus = rnd(4), ai->tech_commerce_bonus = rnd(4);
    if (!rnd(4)) *game_diplo_reputation = rnd(12);
    if (!proposal(faction, other, PROPOSE_ENERGY, counters[rnd(4)])) return;
    answers();
    if (whole) check_make_a_proposal(faction, other, 1);
    else check_energy_trade(faction, other);
}

// The talks' findings a pact or treaty proposal reads, varied: a faction
// the AI wants help against or the player already agreed to fight, the
// AI's defiance, whether it would surrender, the player's and the AI's
// pacts, and the trades and energy at stake.
static void vary_pact_inputs(int faction, int other) {
    struct faction_data *me = F(faction), *ai = F(other);
    int t = third_faction(faction, other, 0);
    if (!rnd(3)) *game_diplo_help_faction = rnd(2) ? t : 0;
    if (!rnd(4)) *game_diplo_accepted_faction = rnd(2) ? t : 0;
    if (!rnd(3)) *game_diplo_alter_fear = rnd(3) - 1;
    if (!rnd(3)) *game_diplo_can_surrender = rnd(2);
    if (!rnd(3)) *game_diplo_player_pacts = rnd(3), *game_diplo_rival_pacts = rnd(3);
    if (!rnd(4)) *game_diplo_rival_strong_pacts = rnd(2), *game_diplo_runaway_leader = rnd(2);
    if (!rnd(3)) *game_diplo_goodwill = rnd(12) - 4;
    if (!rnd(3)) *game_diplo_reputation = rnd(5);
    if (!rnd(4)) *game_diplo_captured_bases = rnd(2);
    if (!rnd(3)) me->diplo_reputation = rnd(4);
    if (!rnd(3)) me->tech_commerce_bonus = rnd(4), ai->tech_commerce_bonus = rnd(4);
    if (!rnd(4)) me->diplo_agenda[other] ^= AGENDA_UNK_2000;
    if (t && !rnd(3)) ai->diplo_status[t] ^= TREATY_UNK_8000;
    if (t && !rnd(3)) F(t)->mil_strength = rnd(500);
    if (!rnd(3)) me->energy_credits = rnd(3) ? rnd(400) : rnd(3000);
}

// A pact, offered by a player with a treaty (the menu offers it then
// only), with each counter the menu has for it.
static void sweep_propose_pact(int faction) {
    static const int counters[] = {COUNTER_FRIENDSHIP, COUNTER_NAME_A_PRICE, COUNTER_THREATEN, COUNTER_RESEARCH,
                                   COUNTER_ENERGY};
    int other = talks(faction);
    if (!other) return;
    F(faction)->diplo_status[other] = (status(faction, other) & ~(TREATY_PACT | TREATY_VENDETTA)) | TREATY_TREATY;
    F(other)->diplo_status[faction] = (status(other, faction) & ~(TREATY_PACT | TREATY_VENDETTA)) | TREATY_TREATY;
    vary_pact_inputs(faction, other);
    if (!proposal(faction, other, PROPOSE_PACT, counters[rnd(5)])) return;
    answers();
    if (whole) check_make_a_proposal(faction, other, 1);
    else check_propose_pact(faction, other);
}

// A treaty, offered by a player without one (at war, in a truce or
// neither), with each counter the menu has for it.
static void sweep_propose_treaty(int faction) {
    static const int counters[] = {COUNTER_FRIENDSHIP, COUNTER_NAME_A_PRICE, COUNTER_THREATEN, COUNTER_RESEARCH,
                                   COUNTER_ENERGY};
    static const uint32_t kinds[] = {0, TREATY_TRUCE_, TREATY_VENDETTA};
    const uint32_t mask = TREATY_PACT | TREATY_TREATY | TREATY_TRUCE_ | TREATY_VENDETTA | TREATY_SURRENDERED_;
    int other = talks(faction);
    if (!other) return;
    uint32_t k = kinds[rnd(3)];
    F(faction)->diplo_status[other] = (status(faction, other) & ~mask) | k;
    F(other)->diplo_status[faction] = (status(other, faction) & ~mask) | k;
    vary_pact_inputs(faction, other);
    if (!rnd(4)) F(other)->flags ^= FACTION_FLAG_COOP_WITH_HUMAN_;
    if (!rnd(4)) F(other)->diplo_agenda[faction] ^= rnd(2) ? AGENDA_UNK_400 : AGENDA_UNK_800;
    if (!rnd(4)) F(other)->diplo_status[faction] ^= TREATY_WANT_REVENGE;
    if (!proposal(faction, other, PROPOSE_TREATY, counters[rnd(5)])) return;
    answers();
    if (whole) check_make_a_proposal(faction, other, 1);
    else check_propose_treaty(faction, other);
}

// A base of the player's, without its headquarters, as the counter menu
// lists them for a base given; -1 if none.
static int own_base(int faction) {
    int n = *game_base_count, start = rnd(n > 0 ? n : 1);
    for (int k = 0; k < n; k++) {
        int b = (start + k) % n;
        if (game_bases[b].faction == faction && !game_has_facility(&game_bases[b], FAC_HEADQUARTERS)) return b;
    }
    return -1;
}

// A gift, with each counter the menu has for one: research (unless the
// rules forbid trading it), all research, energy, or a base (from a player
// with more than one). The menus' answers are the player's: an amount the
// energy menu lists (or nothing), and for research one of the techs
// tech_analysis() will offer (or none).
static void sweep_make_gift(int faction) {
    static const int counters[] = {COUNTER_RESEARCH, COUNTER_ALL_RESEARCH, COUNTER_ENERGY, COUNTER_BASE};
    int other = talks(faction);
    if (!other || !proposal(faction, other, PROPOSE_GIFT, counters[rnd(4)])) return;
    struct faction_data *me = F(faction), *ai = F(other);
    if (!rnd(3)) me->integrity_blemishes = rnd(4);
    if (!rnd(3)) me->diplo_gifts[other] = rnd(3);
    if (!rnd(3)) ai->diplo_status[faction] ^= TREATY_WANT_REVENGE;
    if (!rnd(4)) me->diplo_status[other] ^= TREATY_UNK_40;
    if (!rnd(3)) *game_diplo_bid_base = -1, *game_diplo_ask_base = -1;
    answers();
    int counter = *game_diplo_counter;
    if (counter == COUNTER_RESEARCH && (*game_rules & RULES_SCN_NO_TECH_TRADING)) return;
    if (counter == COUNTER_RESEARCH) {
        tech_analysis(faction, other);
        quiet_talk_answers[0] = rnd(*game_trade_want_tech[1] >= 0 ? 3 : 2);
    } else if (counter == COUNTER_BASE) {
        if (me->base_count <= 1 || (*game_diplo_bid_base = own_base(faction)) < 0) return;
    } else if (counter == COUNTER_ENERGY) {
        if (rnd(2)) me->energy_credits = rnd(3) ? rnd(800) : rnd(4000);
        int amounts[40], n = 0, amount = me->energy_credits * 2;
        do {
            amount = amount / 2 / 25 * 25;
            if (n < 40) amounts[n++] = amount;
        } while (amount);
        quiet_answer = amounts[rnd(n)];
    }
    if (whole) check_make_a_proposal(faction, other, 1);
    else check_make_gift(faction, other, 0);
    quiet_answer = 0;
}

// A joint attack on a third faction (proposal() picks one the menu would
// list), with the counters the menu has for it: friendship, a price,
// research or energy, and a threat, which make_a_proposal() puts to
// propose_attack() only from an AI that surrendered to the player. The
// standings around the target are varied. The player's answers: to the
// techs asked, yes or no; to the price, paid when the player can, or not.
static void sweep_propose_attack(int faction) {
    static const int counters[] = {COUNTER_FRIENDSHIP, COUNTER_NAME_A_PRICE, COUNTER_THREATEN, COUNTER_RESEARCH,
                                   COUNTER_ENERGY};
    int other = talks(faction);
    if (!other || !proposal(faction, other, PROPOSE_ATTACK, counters[rnd(5)])) return;
    int counter = *game_diplo_counter, target = *game_diplo_proposal_faction;
    if (counter == COUNTER_RESEARCH && (*game_rules & RULES_SCN_NO_TECH_TRADING)) return;
    struct faction_data *me = F(faction), *ai = F(other), *t = F(target);
    // The menu's check that the AI isn't already at war with the target
    // reads past the treaties (Thinker), so it may be.
    if (!rnd(5)) ai->diplo_status[target] ^= TREATY_VENDETTA;
    if (!rnd(3)) ai->diplo_status[target] ^= TREATY_COMMLINK;
    if (!rnd(4)) ai->diplo_status[target] ^= TREATY_PACT;
    if (!rnd(3)) me->diplo_status[target] ^= TREATY_VENDETTA;
    if (!rnd(4)) me->diplo_status[target] ^= TREATY_WANT_REVENGE;
    if (!rnd(4)) ai->diplo_status[faction] ^= TREATY_WANT_REVENGE;
    if (!rnd(3)) me->diplo_status[target] = (me->diplo_status[target] & ~(TREATY_PACT | TREATY_TREATY)) |
                                            (rnd(2) ? TREATY_TREATY : TREATY_PACT | TREATY_TREATY);
    if (!rnd(3)) *game_diplo_reputation = rnd(8);
    if (!rnd(3)) t->integrity_blemishes = rnd(4), me->integrity_blemishes = rnd(4);
    if (!rnd(3)) t->diplo_gifts[other] = rnd(4), t->diplo_wrongs[other] = rnd(3);
    if (!rnd(3)) t->mil_strength = rnd(500), t->ranking = 1 + rnd(7);
    if (counter == COUNTER_THREATEN) {
        me->diplo_status[other] |= TREATY_PACT | TREATY_TREATY;
        ai->diplo_status[faction] |= TREATY_PACT | TREATY_TREATY | TREATY_SURRENDERED_;
    }
    tech_analysis(faction, other);
    answers();
    for (int i = 0; i < QUIET_TALK_ANSWERS; i++) quiet_talk_answers[i] = rnd(2);
    int far = 0;
    if (!(status(other, target) & TREATY_COMMLINK) && !game_contiguous(other, target, 0, 1)) far = attack_far(other, target);
    quiet_answer = me->energy_credits >= attack_price(faction, other, target, far) ? rnd(2) : 0;
    if (whole) check_make_a_proposal(faction, other, 1);
    else check_propose_attack(faction, other, 0);
    quiet_answer = 0;
}

// A tech or a commlink bought, as tech_trade() asks buy_tech() for one
// (after tech_analysis(), with the commlink's "tech" set): for each counter
// the menu has for a tech trade, at the high price or not. (Its prototype
// sale, PROPOSE_BUY_TECH, no menu offers.) The player's answer: yes or no.
static void sweep_buy_tech(int faction) {
    static const int counters[] = {COUNTER_FRIENDSHIP, COUNTER_NAME_A_PRICE, COUNTER_RESEARCH, COUNTER_ENERGY};
    int other = talks(faction), kind = rnd(2) ? PROPOSE_TECH : PROPOSE_COMMLINK;
    if (!other || !proposal(faction, other, kind, counters[rnd(4)])) return;
    if (*game_rules & RULES_SCN_NO_TECH_TRADING) return;
    if (!rnd(3)) F(faction)->energy_credits = rnd(3) ? rnd(800) : rnd(5000);
    if (!rnd(3)) F(faction)->diplo_status[other] |= TREATY_PACT | TREATY_TREATY;
    tech_analysis(faction, other);
    if (kind == PROPOSE_COMMLINK) *game_trade_offer_tech[0] = *game_diplo_proposal_faction + TECH_COMMLINK_0;
    answers();
    for (int i = 0; i < QUIET_TALK_ANSWERS; i++) quiet_talk_answers[i] = rnd(2);
    check_buy_tech(faction, other, *game_diplo_counter, rnd(2), kind);
}

// Maps traded between two living factions (as pacts and the talks trade
// them), on a copy where the second may have seen more, or the first.
static void sweep_trade_maps(int faction) {
    int start = rnd(FACTION_COUNT), other = 0;
    for (int k = 0; k < FACTION_COUNT && !other; k++) {
        int o = (start + k) % FACTION_COUNT;
        if (alive(o) && o != faction) other = o;
    }
    if (!other) return;
    if (rnd(2)) *game_current_faction = rnd(2) ? faction : other;
    check_trade_maps(faction, other);
}

// A base given to another living faction, as the talks give them (a
// gift, a threat, a swap): any base of a faction with more than one, but
// its headquarters, with or without a threat.
static void sweep_give_a_base(int faction) {
    int start = rnd(FACTION_COUNT), other = 0;
    for (int k = 0; k < FACTION_COUNT && !other; k++) {
        int o = (start + k) % FACTION_COUNT;
        if (alive(o) && o != faction) other = o;
    }
    if (!other || F(faction)->base_count <= 1) return;
    int base = own_base(faction);
    if (base < 0) return;
    *game_diplo_counter = rnd(3) ? COUNTER_FRIENDSHIP : COUNTER_THREATEN;
    if (!rnd(3)) game_bases[base].faction_former = (uint8_t)other;
    check_give_a_base(base, other);
}

// A base of the AI's asked for (one the player has seen, not its
// headquarters, from an AI with more than one: proposal()), with each
// counter the menu has for it but the threat (which make_a_proposal()
// puts to threaten()): friendship, a price, research (unless the rules
// forbid trading it), energy, or a base of the player's (from a player
// with more than one). The player's answers: yes or no.
static void sweep_base_swap(int faction) {
    static const int counters[] = {COUNTER_FRIENDSHIP, COUNTER_NAME_A_PRICE, COUNTER_RESEARCH, COUNTER_ENERGY,
                                   COUNTER_BASE};
    int other = talks(faction);
    if (!other || !proposal(faction, other, PROPOSE_BASE_SWAP, counters[rnd(5)])) return;
    int counter = *game_diplo_counter;
    struct faction_data *me = F(faction), *ai = F(other);
    if (counter == COUNTER_RESEARCH && (*game_rules & RULES_SCN_NO_TECH_TRADING)) return;
    if (counter == COUNTER_BASE && (me->base_count <= 1 || (*game_diplo_bid_base = own_base(faction)) < 0)) return;
    struct base *B = &game_bases[*game_diplo_ask_base];
    if (!rnd(3)) B->faction_former = (uint8_t)(rnd(2) ? faction : other);
    if (*game_diplo_bid_base >= 0 && !rnd(3)) game_bases[*game_diplo_bid_base].faction_former = (uint8_t)other;
    if (!rnd(4)) ai->diplo_status[faction] ^= TREATY_WANT_REVENGE;
    if (!rnd(4)) ai->diplo_status[faction] |= TREATY_PACT | TREATY_TREATY | TREATY_SURRENDERED_,
                 me->diplo_status[other] |= TREATY_PACT | TREATY_TREATY;
    if (!rnd(3)) me->energy_credits = rnd(3) ? rnd(1000) : rnd(8000);
    if (!rnd(3)) me->tech_ranking = rnd(40), ai->tech_ranking = rnd(40);
    answers();
    for (int i = 0; i < QUIET_TALK_ANSWERS; i++) quiet_talk_answers[i] = rnd(2);
    if (whole) check_make_a_proposal(faction, other, 1);
    else check_base_swap(faction, other);
}

// The player's menus (proposal_menu()), read with their lines left unread
// (quiet_text), answered with items they list: a proposal (not the base
// swap, whose menu draws on the planet window), a faction for a joint
// attack or a commlink (or cancelled), and a counter (not a base given,
// likewise). Or the proposals that need no counter put through
// make_a_proposal(): maps, closing, a loan repaid, the Council (a
// proposal it lists, and the vote bought, or a candidate for governor).
static void sweep_menus(int faction) {
    int other = talks(faction);
    if (!other) return;
    struct faction_data *me = F(faction);
    if (!rnd(3)) me->loan_balance[other] = rnd(3) ? 0 : 50 + rnd(300);
    int no_trading = (*game_rules & RULES_SCN_NO_TECH_TRADING) != 0, n = 0;
    answers();
    for (int i = 0; i < QUIET_TALK_ANSWERS; i++) quiet_talk_answers[i] = rnd(2);
    if (rnd(2)) {
        // Through make_a_proposal().
        static const int kinds[] = {PROPOSE_MAPS, PROPOSE_CLOSE, PROPOSE_REPAY_LOAN, PROPOSE_COUNCIL};
        int kind = kinds[rnd(4)];
        if (kind == PROPOSE_REPAY_LOAN && (!me->loan_balance[other] || me->energy_credits < me->loan_balance[other]))
            return;
        proposal(faction, other, kind, COUNTER_FRIENDSHIP);
        if (!rnd(3)) *game_diplo_friction = rnd(8) - 2;
        if (kind == PROPOSE_COUNCIL) {
            int props[PROP_COUNT], k = 0;
            for (int prop = 0; prop < PROP_COUNT; prop++)
                if (council_listed(faction, prop)) props[k++] = prop;
            quiet_menu_answers[n++] = k && rnd(4) ? props[rnd(k)] : -1;
            if (quiet_menu_answers[0] == PROP_GOVERNOR) {
                int candidates[FACTION_COUNT], m = 0;
                for (int i = 1; i < FACTION_COUNT; i++)
                    if (alive(i) && eligible(i)) candidates[m++] = i;
                quiet_menu_answers[n++] = m && rnd(4) ? candidates[rnd(m)] : -1;
            }
        }
        quiet_menu_count = n, quiet_text = 1;
        check_make_a_proposal(faction, other, 1);
        quiet_menu_count = 0, quiet_text = 0;
        return;
    }
    // The proposal menu's items, but the base swap.
    uint32_t st = status(faction, other);
    int items[12], k = 0, linked = 0, unlinked = 0;
    for (int i = 1; i < FACTION_COUNT; i++) {
        if (!alive(i) || i == faction || i == other) continue;
        if (status(faction, i) & TREATY_COMMLINK) linked++;
        else if (!empath(faction)) unlinked++;
    }
    items[k++] = 0, items[k++] = PROPOSE_GIFT, items[k++] = PROPOSE_ENERGY;
    if ((st & TREATY_TREATY) && !(st & TREATY_PACT)) items[k++] = PROPOSE_PACT;
    if (!(st & TREATY_TREATY)) items[k++] = PROPOSE_TREATY;
    if (!no_trading) items[k++] = PROPOSE_TECH;
    if (unlinked) items[k++] = PROPOSE_COMMLINK;
    if (me->loan_balance[other] && me->energy_credits >= me->loan_balance[other]) items[k++] = PROPOSE_REPAY_LOAN;
    if (linked) items[k++] = PROPOSE_ATTACK;
    int kind = quiet_menu_answers[n++] = items[rnd(k)];
    if (kind == PROPOSE_ATTACK || kind == PROPOSE_COMMLINK) {
        int targets[FACTION_COUNT], m = 0;
        for (int i = 1; i < FACTION_COUNT; i++) {
            if (!alive(i) || i == faction || i == other) continue;
            int l = (status(faction, i) & TREATY_COMMLINK) != 0;
            if (kind == PROPOSE_ATTACK ? l || game_is_human(i) : !l) targets[m++] = i;
        }
        quiet_menu_answers[n++] = m && rnd(5) ? targets[rnd(m)] : -1;
    }
    // The counters.
    int counters[10], c = 0;
    counters[c++] = 0;
    if (kind == PROPOSE_ENERGY) counters[c++] = COUNTER_LOAN;
    if (kind != PROPOSE_GIFT) counters[c++] = COUNTER_FRIENDSHIP, counters[c++] = COUNTER_NAME_A_PRICE,
                              counters[c++] = COUNTER_THREATEN;
    if (!no_trading) counters[c++] = COUNTER_RESEARCH;
    if (kind == PROPOSE_GIFT) counters[c++] = COUNTER_ALL_RESEARCH;
    if (kind != PROPOSE_ENERGY) counters[c++] = COUNTER_ENERGY;
    quiet_menu_answers[n++] = counters[rnd(c)];
    quiet_menu_count = n;
    quiet_text = 1;
    check_proposal_menu(faction, other, 0);
    quiet_text = 0, quiet_menu_count = 0;
}

// Battle plans the AI brings the player, in a pact or not, with common
// enemies made now and then. The player's answers: yes or no, and a base
// the menu lists (or cancelled).
static void sweep_battle_plans(int faction) {
    int other = talks(faction);
    if (!other) return;
    struct faction_data *me = F(faction), *ai = F(other);
    if (!rnd(3)) {
        me->diplo_status[other] |= TREATY_PACT | TREATY_TREATY, ai->diplo_status[faction] |= TREATY_PACT | TREATY_TREATY;
        ai->diplo_status[faction] &= ~(uint32_t)(TREATY_WANT_REVENGE | TREATY_UNK_8000);
    }
    int t = third_faction(faction, other, 0);
    if (t && rnd(2)) me->diplo_status[t] |= TREATY_VENDETTA, ai->diplo_status[t] |= TREATY_VENDETTA;
    if (!rnd(3)) *game_diplo_trade_offered = rnd(2);
    if (!rnd(4)) ai->target_x = -1, ai->target_y = -1;
    answers();
    for (int i = 0; i < QUIET_TALK_ANSWERS; i++) quiet_talk_answers[i] = rnd(4) ? 1 : 0;
    int bases[64], n = 0;
    for (int b = 0; b < *game_base_count && n < 64; b++) {
        const struct base *B = &game_bases[b];
        int f = B->faction;
        if (f == faction || f == other || !(status(faction, f) & TREATY_VENDETTA) || !(status(other, f) & TREATY_VENDETTA))
            continue;
        if ((B->visibility & (1u << faction)) || (B->visibility & (1u << other))) bases[n++] = b;
    }
    quiet_answer = n && rnd(5) ? bases[rnd(n)] : -1;
    check_battle_plans(faction, other, 0);
    quiet_answer = 0;
}

// A tech trade: the player's proposal of a tech (when the rules allow
// trading them) or a commlink, with the counters the menu has for them
// but the threat (which make_a_proposal() puts to threaten()), or the
// AI's own (as communicate() makes it: research). The trade's menu
// (TRADETECHOPT, its lines left unread) is answered with items it lists
// for the techs tech_analysis() finds: decline, accept, the Datalinks (then
// another answer), the AI's second tech or the player's when offered; a
// later menu, after a refusal or the AI's next offer, declines or accepts.
// The commlink's offer (COMMLINKTECH) and the AI's lines are answered yes
// or no.
static void sweep_tech_trade(int faction) {
    static const int counters[] = {COUNTER_FRIENDSHIP, COUNTER_NAME_A_PRICE, COUNTER_RESEARCH, COUNTER_ENERGY};
    int other = talks(faction);
    if (!other) return;
    int no_trading = (*game_rules & RULES_SCN_NO_TECH_TRADING) != 0, ai = !rnd(4);
    int kind = ai || rnd(3) ? PROPOSE_TECH : PROPOSE_COMMLINK;
    if (kind == PROPOSE_TECH && no_trading) return;
    int counter = ai ? COUNTER_RESEARCH : counters[rnd(4)];
    if (counter == COUNTER_RESEARCH && no_trading) return;
    if (!proposal(faction, other, kind, counter)) return;
    struct faction_data *me = F(faction), *O = F(other);
    if (!rnd(3)) me->diplo_status[other] |= TREATY_PACT | TREATY_TREATY, O->diplo_status[faction] |= TREATY_PACT | TREATY_TREATY;
    if (!rnd(6)) O->diplo_status[faction] |= TREATY_SURRENDERED_;
    if (!rnd(3)) *game_diplo_trade_offered = rnd(2);
    if (!rnd(4)) me->maps_traded ^= 1u << other, O->maps_traded ^= 1u << faction;
    if (!rnd(3)) me->tech_ranking = rnd(40), O->tech_ranking = rnd(40);
    if (!rnd(4)) me->integrity_blemishes = rnd(5);
    answers();
    for (int i = 0; i < QUIET_TALK_ANSWERS; i++) quiet_talk_answers[i] = rnd(2);
    tech_analysis(faction, other);
    if (kind == PROPOSE_COMMLINK) *game_trade_offer_tech[0] = *game_diplo_proposal_faction + TECH_COMMLINK_0;
    int32_t **want = game_trade_want_tech, **offer = game_trade_offer_tech;
    int items[5], k = 0, n = 0;
    items[k++] = 0, items[k++] = 1, items[k++] = 3;
    if (*offer[1] >= 0 && *offer[1] != *offer[0]) items[k++] = 4;
    if (*want[1] >= 0 && *want[1] != *want[0]) items[k++] = 2;
    int first = items[rnd(k)];
    quiet_menu_answers[n++] = first;
    if (first == 3) {
        int again = items[rnd(k)];
        quiet_menu_answers[n++] = again == 3 ? 1 : again;
    }
    for (; n < QUIET_MENU_ANSWERS; n++) quiet_menu_answers[n] = n < QUIET_MENU_ANSWERS - 1 ? rnd(2) : 0;
    quiet_menu_count = QUIET_MENU_ANSWERS;
    quiet_text = 1, quiet_talk_pops = 1;
    if (whole && !ai) check_make_a_proposal(faction, other, 1);
    else check_tech_trade(faction, other, ai, counter, kind);
    quiet_text = 0, quiet_talk_pops = 0, quiet_menu_count = 0;
}

// The player meets the AI: whether they have met, a grudge, a pact, the
// hostility, the factions' rankings, planet busters on either side (and the
// player's having told them already), in a PBEM game now and then (the
// console's asking again), and English or another language.
static void sweep_introduce(int faction) {
    int other = talks(faction);
    if (!other) return;
    struct faction_data *me = F(faction), *ai = F(other);
    if (rnd(2)) me->diplo_status[other] ^= TREATY_UNK_8000000;
    if (!rnd(3)) me->diplo_status[other] ^= TREATY_WANT_REVENGE;
    if (!rnd(3)) me->diplo_status[other] ^= TREATY_PACT;
    if (rnd(2)) *game_diplo_hostility = rnd(5);
    if (rnd(2)) me->ranking = 1 + rnd(7);
    if (!rnd(3)) me->planet_busters = 1 + rnd(2);
    if (!rnd(3)) ai->planet_busters = 1 + rnd(2);
    if (!rnd(3)) me->diplo_status[other] ^= 0x4000;
    if (!rnd(3)) *game_pbem = 1, game_diplo_state[other] ^= 2;
    if (!rnd(3)) *game_language = rnd(2);
    check_introduce(faction, other, 0);
}

// Once a turn, cases on protected copies of the game, each for a faction
// with bases made the player.
static void diplomacy_sweep(void) {
    static void (*const cases[])(int faction) = {sweep_threaten, sweep_energy_trade, sweep_propose_pact,
                                                 sweep_propose_treaty, sweep_make_gift,
                                                 sweep_propose_attack, sweep_buy_tech, sweep_trade_maps,
                                                 sweep_give_a_base, sweep_base_swap, sweep_menus,
                                                 sweep_battle_plans, sweep_tech_trade, sweep_introduce};
    enum { N_CASES = sizeof(cases) / sizeof(cases[0]) };
    if (!*game_map || *game_base_count <= 0 || *game_net_multiplayer || *game_pbem) return;
    // Not while the player is in talks: the diplomacy window is open, and
    // diplomacy_caption() would redraw it.
    if (*((uint8_t *)game_diplo_pop + 0x9c) & 1) return;
    int sweeping = hooks_sweeping;
    hooks_sweeping = 1;
    for (int k = 0; k < 10 * N_CASES; k++) {
        seed = mix((uint32_t)*game_turn * 7919u + (uint32_t)k);
        int faction = 1 + rnd(FACTION_COUNT - 1);
        if (!alive(faction) || F(faction)->base_count <= 0) continue;
        struct hook_writes w = {.protect_only = 1};
        hooks_writes_game(&w);
        hooks_writes_begin(&w);
        *game_rand_state() = mix(seed + 1);
        whole = !rnd(3);
        cases[k % N_CASES](faction);
        hooks_writes_abandon(&w);
    }
    hooks_sweeping = sweeping;
}

#define PUSH_EBP_MOV_EBP_ESP 0x55, 0x8b, 0xec
static const struct hook diplomacy_hooks[] = {
    HOOK(0x549930, threaten, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x64),  // sub esp, 0x64
    HOOK(0x540630, energy_trade, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x60),  // sub esp, 0x60
    HOOK(0x543bc0, propose_pact, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x78),  // sub esp, 0x78
    HOOK(0x544e40, propose_treaty, PUSH_EBP_MOV_EBP_ESP, 0x81, 0xec, 0x88, 0x00, 0x00, 0x00),  // sub esp, 0x88
    HOOK(0x547c10, make_gift, PUSH_EBP_MOV_EBP_ESP, 0x64, 0xa1, 0x00, 0x00, 0x00, 0x00),  // mov eax, fs:[0]
    HOOK(0x545e80, propose_attack, PUSH_EBP_MOV_EBP_ESP, 0x64, 0xa1, 0x00, 0x00, 0x00, 0x00),
    HOOK(0x5401a0, buy_tech, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x50),  // sub esp, 0x50
    HOOK(0x543a20, trade_maps, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x08),  // sub esp, 8
    HOOK(0x54cf40, give_a_base, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x1c),  // sub esp, 0x1c
    HOOK(0x54d410, base_swap, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x08),  // sub esp, 8
    HOOK(0x54dcf0, proposal_menu, PUSH_EBP_MOV_EBP_ESP, 0x64, 0xa1, 0x00, 0x00, 0x00, 0x00),
    HOOK(0x54f420, make_a_proposal, PUSH_EBP_MOV_EBP_ESP, 0x64, 0xa1, 0x00, 0x00, 0x00, 0x00),
    HOOK(0x54b1c0, battle_plans, PUSH_EBP_MOV_EBP_ESP, 0x64, 0xa1, 0x00, 0x00, 0x00, 0x00),
    HOOK(0x541100, tech_trade, PUSH_EBP_MOV_EBP_ESP, 0x64, 0xa1, 0x00, 0x00, 0x00, 0x00),
    HOOK(0x53d360, introduce, PUSH_EBP_MOV_EBP_ESP, 0x64, 0xa1, 0x00, 0x00, 0x00, 0x00),
};
struct hook_module diplomacy_module = {"diplomacy", diplomacy_hooks, HOOK_COUNT(diplomacy_hooks), diplomacy_sweep, NULL,
                                       -1};
