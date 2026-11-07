// Units on the move: orders to go somewhere, and the AI's courses (move.c).
#pragma once

void set_course(int veh, int icon, int x, int y);
void assemble_passengers(int base, int type, int priority);
void action_go_to(int veh);
void action_road_to(int veh);
int action_home(int veh, int type);
int valid_patrol(int veh, int x, int y);
void action_patrol(int veh, int x, int y);
