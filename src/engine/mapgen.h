// Map generation, reimplemented from terranx.exe 2.0; see mapgen.c.
#pragma once

// Changing altitude.
void alt_set_both(int x, int y, int altitude);
void world_alt_set(int x, int y, int altitude, int both);
void world_raise_alt(int x, int y);
void world_lower_alt(int x, int y);

// Painting land.
void brush(int x, int y, int altitude);
void paint_land(int x, int y, int altitude, int size);
void build_continent(int size);
void build_hills(int altitude);
void world_riverbeds(void);

// Making a map.
void map_wipe(void);
int world_validate(void);
void world_temperature(void);
void world_analysis(void);
void world_alt_put_detail(int x, int y);
void world_polar_caps(void);
void world_linearize_contours(void);

// Landmarks: each at (x, y), or a site of its own when that is off the map.
void world_crater(int x, int y);
void world_monsoon(int x, int y);
void world_sargasso(int x, int y);
void world_ruin(int x, int y);
void world_dune(int x, int y);
void world_diamond(int x, int y);
void world_fresh(int x, int y);
void world_volcano(int x, int y, int no_name);
void world_borehole(int x, int y);
void world_temple(int x, int y);
void world_unity(int x, int y);
void world_fossil(int x, int y);
void world_canyon(int x, int y);
void world_mesa(int x, int y);
void world_ridge(int x, int y);
void world_geothermal(int x, int y);
void world_landmarks(void);
