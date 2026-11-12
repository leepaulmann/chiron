// The AI's answers to the player's proposals in the diplomacy talks (see
// diplomacy.c). The first faction is the player, the second the AI.
#pragma once

// The player threatens the AI over its proposal.
void threaten(int faction, int other);
// The player asks the AI for energy; 1 if energy changed hands.
int energy_trade(int faction, int other);
// The player proposes a pact; 1 when settled, 0 when a threat may follow.
int propose_pact(int faction, int other);
// The player proposes a treaty; the same.
int propose_treaty(int faction, int other);

// The player gives the AI a gift: energy, research or a base.
void make_gift(int faction, int other);
// The player asks the AI to join an attack on a third faction; 0 when a
// threat is turned away without a word.
int propose_attack(int faction, int other);
// The player offers to buy the tech (or commlink, or prototype) the AI
// could give; 0 if no price is named.
int buy_tech(int faction, int other, int counter, int high, int proposal);
// Two factions trade maps.
void trade_maps(int faction, int other);
// A base ceded in the talks to `faction`.
void give_a_base(int base, int faction);
// The player asks the AI for one of its bases; 0 once it changed hands.
int base_swap(int faction, int other);

// The player's menus in the talks: the proposal and the counter-proposal;
// the proposal, or 0.
int proposal_menu(int faction, int other);
// The player's proposal, from the menus (unless `skip`), put to the AI.
void make_a_proposal(int faction, int other, int skip);
// Techs traded between the player and the AI, proposed by the player
// (offering `counter`) or by the AI (`ai`); 1 after a trade the player
// proposed.
int tech_trade(int faction, int other, int ai, int counter, int proposal);
// The talks between the player (`faction`) and an AI that has something to say or
// that the player called (`unk`): the AI's proposals and demands, its answers
// to the player's menus, and the greeting.
void communicate(int faction, int other, int unk);
// The pact of brotherhood and the treaty are made in the talks; the player
// demands that the AI withdraw (2: it declared war, 1: it refused, else 0);
// `second`'s units leave `first`'s territory (returns how many).
void comm_check(int faction, int other);
// The base `faction` could ask `other` to attack with it, or -1.
int suggest_plan(int faction, int other);
void pact_of_brotherhood(int faction, int other);
void make_treaty(int faction, int other);
int demand_withdrawal(int faction, int other);
int do_withdrawal(int faction, int other);
void call_off_vendetta(int faction, int other);
// The AI brings the player battle plans.
void battle_plans(int faction, int other);

// The player meets the AI as it takes the AI's call: the greeting and, if
// either has planet busters, a word of them.
void introduce(int faction, int other);

// Shadow mode (quiet.c): in a sweep's copies of the game, the answers the
// diplomacy window's dialogs give, in turn, and the next one to give.
enum { QUIET_TALK_ANSWERS = 8 };
extern int quiet_talk_answers[QUIET_TALK_ANSWERS], quiet_talk_next;
// While set, a message with a portrait (x_pops) answers from them too.
extern int quiet_talk_pops;
