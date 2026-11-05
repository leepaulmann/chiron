// Unit test for shadow mode's held UI calls: what a check holds back, how
// the two runs' calls are compared, and that the original's are made once
// the check is over.
#include <stdio.h>
#include <string.h>

#include "../src/engine/hooks.h"

static int failures;
static char shown[8][300];
static int n_shown;

#define EXPECT(cond)                                         \
    do {                                                     \
        if (!(cond)) {                                       \
            printf("FAIL line %d: %s\n", __LINE__, #cond);   \
            failures++;                                      \
        }                                                    \
    } while (0)

static int nested_once;
static const char *check(const char *orig, const char *ours, int nest);
static void show(const struct hook_ui_call *c);
// A replayed call that runs a check of its own, as a discovered tech does.
static void show_and_check(const struct hook_ui_call *c) {
    show(c);
    if (!nested_once++) check("NESTED", "NESTED", 1);
}

static void show(const struct hook_ui_call *c) {
    if (n_shown < 8) snprintf(shown[n_shown], sizeof(shown[0]), "%s %d", c->text[0], c->arg[0]);
    n_shown++;
}

// A message, held while state is protected.
static void message(const char *label, int value) {
    struct hook_ui_call c = {.name = "message", .replay = show, .arg = {value}};
    hooks_ui_text(&c, 0, label);
    if (hooks_protecting()) hooks_ui_hold(&c);
    else show(&c);
}

static int state;

// A check, as SHADOW_WRITER makes it: the original says `orig`, ours `ours`.
static const char *check(const char *orig, const char *ours, int nest) {
    static char result[640];
    struct hook_writes w = {0};
    hooks_writes_add(&w, &state, sizeof(state));
    hooks_writes_begin(&w);
    state = 1;
    if (orig) message(orig, 1);
    if (nest) check("INNER", "INNER", 0);
    hooks_writes_middle(&w);
    state = 1;
    if (ours) message(ours, 1);
    if (nest) message("INNER", 1);
    const char *diff = hooks_writes_end(&w, 0, 0);
    snprintf(result, sizeof(result), "%s", diff ? diff : "");
    hooks_ui_release();
    return result;
}

int main(void) {
    // Outside a check, messages are shown at once.
    message("NOW", 1);
    EXPECT(n_shown == 1);

    // The same message from both: no difference, shown once afterwards.
    n_shown = 0;
    EXPECT(!strcmp(check("STARVE", "STARVE", 0), ""));
    EXPECT(n_shown == 1 && !strcmp(shown[0], "STARVE 1"));
    EXPECT(state == 1);

    // Different messages, or a missing one, are differences; the
    // original's is the one shown.
    n_shown = 0;
    EXPECT(strstr(check("STARVE", "GROW", 0), "STARVE") != NULL);
    EXPECT(n_shown == 1 && !strcmp(shown[0], "STARVE 1"));
    n_shown = 0;
    EXPECT(strstr(check("STARVE", NULL, 0), "made 0 calls to the UI, original 1") != NULL);
    EXPECT(n_shown == 1);

    // A nested check keeps the original's for the outer one, which shows
    // both at the end, in order.
    n_shown = 0;
    EXPECT(!strcmp(check("OUTER", "OUTER", 1), ""));
    EXPECT(n_shown == 2 && !strcmp(shown[0], "OUTER 1") && !strcmp(shown[1], "INNER 1"));

    // A replayed call whose own check releases more doesn't disturb the
    // calls still to be replayed.
    n_shown = 0;
    {
        struct hook_writes w = {0};
        hooks_writes_add(&w, &state, sizeof(state));
        hooks_writes_begin(&w);
        struct hook_ui_call c = {.name = "message", .replay = show_and_check, .arg = {1}};
        hooks_ui_text(&c, 0, "FIRST");
        hooks_ui_hold(&c);
        message("SECOND", 1);
        hooks_writes_middle(&w);
        hooks_ui_hold(&c);
        message("SECOND", 1);
        hooks_writes_end(&w, 0, 0);
        hooks_ui_release();
    }
    EXPECT(n_shown == 4 && !strcmp(shown[0], "FIRST 1") && !strcmp(shown[1], "NESTED 1") && !strcmp(shown[2], "INNER 1") &&
           !strcmp(shown[3], "SECOND 1"));

    // A sweep's protected copy drops what was held in it.
    n_shown = 0;
    struct hook_writes copy = {.protect_only = 1};
    hooks_writes_add(&copy, &state, sizeof(state));
    hooks_writes_begin(&copy);
    hooks_sweeping = 1;
    check("SWEPT", "SWEPT", 0);
    message("ALSO", 2);
    hooks_sweeping = 0;
    hooks_writes_abandon(&copy);
    hooks_ui_release();
    EXPECT(n_shown == 0);

    if (failures) return 1;
    printf("engine_hooks: all passed\n");
    return 0;
}
