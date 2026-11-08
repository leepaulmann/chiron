// The diplomacy talks' answers to the player's proposals: what the AI makes
// of a threat (threaten), a request for energy (energy_trade), and a pact
// or a treaty proposed (propose_pact, propose_treaty). The game calls
// these only from the player's talks (make_a_proposal(), from
// communicate()), so the first faction is always the player, at the
// console, and the second the AI it talks to. Reimplemented from terranx.exe 2.0's disassembly; names follow
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

#include "faction.h"
#include "game.h"
#include "map.h"
#include "shadow.h"
#include "tech.h"

enum {
    TREATY_TRUCE_ = 0x4, TREATY_SURRENDERED_ = 0x2000000,
    AGENDA_UNK_1000 = 0x1000, AGENDA_UNK_2000 = 0x2000,
    FACTION_FLAG_COOP_WITH_HUMAN_ = 0x400000,  // faction_data.flags
    TECH_COMMLINK_0 = 0x59,  // the commlink with faction 0; faction i's is 0x59 + i
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
    else game_give_a_base(base, faction);
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
// labels) are compared with the rest of the game's state.

extern struct hook_module diplomacy_module;
#define DIPLO (&diplomacy_module)

#define TALK_CHECK(ret, name, params, args, fmt, ...)                                            \
    SHADOW_DECLARE(ret, name, params)                                                            \
    static ret check_##name params {                                                             \
        struct hook_writes w_ = {0};                                                             \
        hooks_writes_game(&w_);                                                                  \
        hooks_writes_begin(&w_);                                                                 \
        quiet_talk_next = 0;                                                                     \
        hooks_nested++;                                                                          \
        SHADOW_RESULT_##ret(want) orig_##name args;                                              \
        hooks_nested--;                                                                          \
        hooks_writes_middle(&w_);                                                                \
        quiet_talk_next = 0;                                                                     \
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

TALK_CHECK(void, threaten, (int a, int b), (a, b), "%d, %d", a, b)
TALK_CHECK(int, energy_trade, (int a, int b), (a, b), "%d, %d", a, b)
TALK_CHECK(int, propose_pact, (int a, int b), (a, b), "%d, %d", a, b)
TALK_CHECK(int, propose_treaty, (int a, int b), (a, b), "%d, %d", a, b)

// ---- Sweep ---------------------------------------------------------------------------

static uint32_t mix(uint32_t a) {
    a ^= a >> 16, a *= 0x7feb352du;
    a ^= a >> 15, a *= 0x846ca68bu;
    return a ^ (a >> 16);
}
static uint32_t seed;
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
    check_threaten(faction, other);
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
    check_energy_trade(faction, other);
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
    check_propose_pact(faction, other);
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
    check_propose_treaty(faction, other);
}

// Once a turn, cases on protected copies of the game, each for a faction
// with bases made the player.
static void diplomacy_sweep(void) {
    static void (*const cases[])(int faction) = {sweep_threaten, sweep_energy_trade, sweep_propose_pact,
                                                 sweep_propose_treaty};
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
};
struct hook_module diplomacy_module = {"diplomacy", diplomacy_hooks, HOOK_COUNT(diplomacy_hooks), diplomacy_sweep, NULL,
                                       -1};
