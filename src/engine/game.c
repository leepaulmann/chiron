#include "game.h"

#include "code.h"

struct rules_tech *game_tech;
uint8_t *game_tech_achieved;
int32_t *game_expansion;
int32_t *game_turn;

void game_bind(struct pe_image *exe) {
    game_tech = (struct rules_tech *)code_at(exe, 0x94f358);
    game_tech_achieved = code_at(exe, 0x9a6670);
    game_expansion = (int32_t *)code_at(exe, 0x9a6488);
    game_turn = (int32_t *)code_at(exe, 0x9a64d4);
}
