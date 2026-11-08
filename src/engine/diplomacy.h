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

// Shadow mode (quiet.c): in a sweep's copies of the game, the answers the
// diplomacy window's dialogs give, in turn, and the next one to give.
enum { QUIET_TALK_ANSWERS = 8 };
extern int quiet_talk_answers[QUIET_TALK_ANSWERS], quiet_talk_next;
