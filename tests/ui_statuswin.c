// Unit test for the status panel's pure arithmetic (src/ui/statuswin.c): no game files needed. The numbers are worked out by hand
// from the original's code (StatusWin_draw_status, StatusWin_draw_active and the font set-up).
#include <stdio.h>

#include "../src/ui/statuswin.h"

static int failures;
static void check_(int line, const char *what, long got, long want) {
    if (got != want) {
        printf("FAIL line %d: %s = %ld, expected %ld\n", line, what, got, want);
        failures++;
    }
}
#define EXPECT(expr, want) check_(__LINE__, #expr, (long)(expr), (long)(want))

int main(void) {
    // The fonts: 10 on an 800-wide screen, 12 on any other.
    EXPECT(statuswin_font_size(0x320), 10);
    EXPECT(statuswin_font_size(1024), 12);
    EXPECT(statuswin_font_size(0x321), 12);

    // A line's height: the ascent while the font has no extra spacing, else its height and the spacing.
    EXPECT(statuswin_line_height(-1, 11, 14), 11);
    EXPECT(statuswin_line_height(4, 11, 14), 18);
    EXPECT(statuswin_line_height(0, 11, 14), 14);

    // The picture scale words are -1 outside a square's picture: the fallback. Inside, value * mul / div.
    EXPECT(statuswin_scaled(-1, 100, 100, 0x32), 0x32);
    EXPECT(statuswin_scaled(0, 100, 100, 0x4b), 0x4b);
    EXPECT(statuswin_scaled(200, 100, 100, 0x32), 200);
    EXPECT(statuswin_scaled(150, 75, 100, 0x38), 112);  // 11250 / 100
    EXPECT(statuswin_scaled(7, 100, 3, 5), 233);        // 700 / 3

    // A picture centred between left and right, the leftover halved toward zero (cdq, sub, sar).
    EXPECT(statuswin_centre_x(10, 90, 40), 30);   // (90 - 40 - 10) / 2 = 20
    EXPECT(statuswin_centre_x(10, 91, 40), 30);   // 41 / 2 = 20
    EXPECT(statuswin_centre_x(10, 40, 40), 5);    // -10 / 2 = -5
    EXPECT(statuswin_centre_x(10, 41, 40), 6);    // -9 / 2 = -4 (toward zero)

    // The altitude class: the top three bits of the climate byte less three.
    EXPECT(statuswin_altitude_class(0x00), -3);
    EXPECT(statuswin_altitude_class(0x1f), -3);
    EXPECT(statuswin_altitude_class(0x20), -2);
    EXPECT(statuswin_altitude_class(0x40), -1);
    EXPECT(statuswin_altitude_class(0x60), 0);
    EXPECT(statuswin_altitude_class(0xff), 4);

    // The first line under the picture: half the width below the top, and 3.
    EXPECT(statuswin_text_top(10, 110, 20), 73);
    EXPECT(statuswin_text_top(10, 111, 20), 73);  // 101 / 2 = 50

    // The strip of units: how many icons fit when each takes its width and 5, and the width they take once spread.
    EXPECT(statuswin_strip_capacity(100, 61), 1);
    EXPECT(statuswin_strip_capacity(400, 61), 6);
    EXPECT(statuswin_strip_capacity(65, 61), 0);
    EXPECT(statuswin_strip_icon(400, 7), 52);  // 400 / 7 = 57, less 5

    // Moves: the speed less what is spent, held between 0 and 999.
    EXPECT(statuswin_moves_left(9, 3), 6);
    EXPECT(statuswin_moves_left(3, 9), 0);
    EXPECT(statuswin_moves_left(2000, 1), 999);
    EXPECT(statuswin_moves_left(999, 0), 999);
    EXPECT(statuswin_moves_left(1000, 0), 999);

    // Hit points: ten a point of the reactor held to 1 to 100, or 1 for the alien plan 12.
    EXPECT(statuswin_hit_points(12, 5), 1);
    EXPECT(statuswin_hit_points(0, 3), 30);
    EXPECT(statuswin_hit_points(0, 0), 10);
    EXPECT(statuswin_hit_points(0, 150), 1000);
    EXPECT(statuswin_hit_points(11, 100), 1000);

    // The damage as a percentage of the hit points; the remaining points are held between 0 and 9999.
    EXPECT(statuswin_damage_percent(30, 10), 33);   // (30 - 20) * 100 / 30
    EXPECT(statuswin_damage_percent(30, 45), 100);  // remaining 0
    EXPECT(statuswin_damage_percent(10, 3), 30);
    EXPECT(statuswin_damage_percent(1000, 1), 0);   // 1 * 100 / 1000
    EXPECT(statuswin_damage_percent(1, 1), 100);
    EXPECT(statuswin_damage_percent(30, 0), 0);

    // How hurt: from a third of the points (hp / 3 = 10), from two thirds (2 * hp / 3 = 20).
    EXPECT(statuswin_damage_level(30, 9), 0);
    EXPECT(statuswin_damage_level(30, 10), 1);
    EXPECT(statuswin_damage_level(30, 19), 1);
    EXPECT(statuswin_damage_level(30, 20), 2);
    EXPECT(statuswin_damage_level(1, 0), 2);  // both thresholds are 0 for the alien units' single point

    // The turns of flight: the range, plus one while no moves are left, less the turns flown.
    EXPECT(statuswin_flight_turns(5, 0, 2), 4);
    EXPECT(statuswin_flight_turns(5, 3, 2), 3);

    if (failures) return 1;
    printf("ui_statuswin: all checks passed\n");
    return 0;
}
