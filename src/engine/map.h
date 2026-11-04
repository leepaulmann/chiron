// Map queries, reimplemented from terranx.exe 2.0. Behaviour follows the
// originals exactly, quirks included; see map.c.
#pragma once

#include <stdint.h>

// Coordinates and distances.
int on_map(int x, int y);
int xrange(int x);
int bad_reg(int region);
int x_dist(int x1, int x2);
int vector_dist_offset(int dx, int dy);
int vector_dist(int x1, int y1, int x2, int y2);
int radius_move_offset(int dx, int dy, int range);
int radius_move(int x1, int y1, int x2, int y2, int range);
int compass_move(int x1, int y1, int x2, int y2);

// One square.
int alt_at(int x, int y);
int alt_detail_at(int x, int y);
int region_at(int x, int y);
uint32_t bit_at(int x, int y);
int is_ocean(int x, int y);
int veh_who(int x, int y);
int base_who(int x, int y);
int anything_at(int x, int y);
uint8_t abstract_at(int x, int y);
int is_known(int x, int y, int faction);
int crappy(int x, int y);
int alt_natural(int x, int y);
int elev_at(int x, int y);
int minerals_at(int x, int y);
int bonus_at(int x, int y, int unused);
int goody_at(int x, int y);

// Around a square.
int is_coast(int x, int y, int base_radius);
int find_landmark(int x, int y, int radius);
int near_landmark(int x, int y);
int zoc_any(int x, int y, int faction);
int zoc_veh(int x, int y, int faction);

// Regions, coasts and ports.
int sea_coast(int region, int from);
int sea_coasts(int region);
int base_on_sea(int base, int sea);
int base_coast(int base);
int port_to_coast(int base, int region);
int port_to_port(int base, int other);
int transport_base(int base);
int naval_base(int base);
