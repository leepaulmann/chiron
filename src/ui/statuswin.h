// The status panel on the main screen (StatusWin): the square under the mouse (its terrain, yields, landmarks and
// improvements) and the unit or base that is active on it, with the strip of the units stacked there. See statuswin.c.
// The pure arithmetic is here, so a test can check it without the game.
#pragma once

#include <stdint.h>

#include "ui.h"

// The size of the panel's three fonts: 10 on an 800-wide screen, 12 on any other.
int statuswin_font_size(int screen_w);

// The height of a line in a font of the given first (-1 while the font has no extra), ascent and height.
int statuswin_line_height(int first, int ascent, int height);

// What the game's two globals for a picture's size make of a size: `value * mul / div` while value is above zero,
// else `fallback` (both globals are -1 outside the picture's own drawing, so it is nearly always the fallback).
int statuswin_scaled(int value, int mul, int div, int fallback);

// The left edge of a picture `size` wide centred between left and right (the leftover halved toward zero).
int statuswin_centre_x(int left, int right, int size);

// The altitude class of a square from its climate byte: 0 and up for land, -3 deep ocean, -2 ocean, -1 shelf.
int statuswin_altitude_class(int climate);

// The y of the first line under the picture of a square: half the panel's width below the top, and 3.
int statuswin_text_top(int left, int right, int top);

// How many unit icons the strip holds across `width` when each takes `icon + 5`.
int statuswin_strip_capacity(int width, int icon);
// The width an icon takes once `count` of them fill `width` (the strip's own width, less 5 for the gap).
int statuswin_strip_icon(int width, int capacity);

// The moves a unit has left (its speed less what it has spent), held between 0 and 999.
int statuswin_moves_left(int speed, int spent);
// A unit's hit points as the original counts them: 10 a point of the reactor held to 1 to 100, or just 1 for a plan
// of 12 (the alien units).
int statuswin_hit_points(int plan, int reactor);
// The damage as a percentage of the hit points: how far the remaining points (held between 0 and 9999) fall short.
int statuswin_damage_percent(int hit_points, int damage);
// How hurt a unit is: 0 below a third of its points in damage, 1 from a third, 2 from two thirds.
int statuswin_damage_level(int hit_points, int damage);

// The turns of flight a flier has: the range plus one while it has no moves left, less the turns it has flown.
int statuswin_flight_turns(int range, int moves_left, int flown);
