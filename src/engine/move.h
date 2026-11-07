// Units on the move: orders to go somewhere, and the AI's courses (move.c).
#pragma once

void set_course(int veh, int icon, int x, int y);
void assemble_passengers(int base, int type, int priority);
void action_go_to(int veh);
void action_road_to(int veh);
int action_home(int veh, int type);
int valid_patrol(int veh, int x, int y);
void action_patrol(int veh, int x, int y);
int terraform_cost(int x, int y, int faction);
int can_terraform(int faction, int x, int y, int keen, int base);
int action_terraform(int veh, int terraform, int toggle);
int action(int veh);
int action_build(int veh, char *name);
void action_staple(int base);
void action_oblit(int veh, int base);
void action_gate(int veh, int base);
void action_destroy(int veh, int item, int x, int y);
void action_arty(int veh, int x, int y);
void action_destruct(int veh);
void action_tectonic(int veh, int x, int y);
void action_fungal(int veh, int x, int y);
