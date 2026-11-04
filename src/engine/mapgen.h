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
