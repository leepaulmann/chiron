// The game's Dialog, SpriteBox and BasePop (popup) classes (OpenSMACX's and Thinker's names), and the small
// text functions popups run their lines through. The layouts are in game.h.
//
// A Dialog is a block of data (made with room for a number of bytes, or one a caller gave), a position
// and size, the font and the four rows of text colours its items take, and a list of items (the game's
// StringStruct) at 0xbc. The list is a ring of nodes; it is emptied by calling its vtable's slot 1 with
// each item and deleting the item and then the node through their own vtables (slot 0 of the vtable
// found through the object's first word and its entry at 0x4, called with 1). Dialog_close empties the
// list, closes the data and puts every field back to what a new Dialog takes from the game's defaults;
// the seven init functions are close followed by their own fields and, when they are given a size,
// the data. A BasePop holds a Dialog at 0x21d0 (its buttons) and a CheckBox at 0x2228 as virtual
// bases, and a SpriteBox keeps a Spot list and a list of sprites with the functions a screen set to
// hear of the mouse.
//
// Here are: purge_leading, purge_trailing, purge_spaces, kill_lf and add_lf (text); BasePop's set_height,
// set_loc, read_check, the key functions (on_key_down, on_key_click, on_key_up, double_click,
// on_left_down), set_external_window, the string and button font and colour setters and the
// defaults the new popups take, set_def_file, on_modal, fallout and UNK3 and UNK4 (two flag bits);
// Dialog's close, the seven init functions, UNK1 (a call of the list's slot 1 at the Dialog's place),
// and the font and colour setters; SpriteBox's id_to_pos, pos_to_id, on_dialog_focus and the seven
// mouse functions that tell a screen's function which sprite was hit (and post the Enter key to the
// game's window when it answers non-zero).
//
// The popup's layout and drawing: BasePop's calculate_dimensions (the size of the popup, its picture, its lines of text and the
// widget it holds) and on_redraw (the background, the frame picture, the picture or the screen's function for it, and the text). The
// widget's own layout (0x6132a0, which picks the part its kind says) stays with the original.
//
// The popup engine's text half: parse_string (fills the text variables into a line, with the choices by
// gender and number and the French articles) and findnum; the script text's number readers stoi, btoi and
// htoi, the item readers of the line that text_get holds (text_item and the number, binary and hex
// versions, on the game's own line and on a Text object), the label search of a Text index
// (text_index_search) over the game's four (text_search_index); and the 31 entry points that the game's
// code calls to show a message (popup, popups and popup_ask with their numbers), which build the
// arguments of pops_18 and pop_ask_6.
//
// Left to the original:
//  - what opens, builds or frees objects the game keeps on its heap and is not made and unmade by the
//    life cycle below: BasePop's init_class, close_class, basepop_alloc and UNK1, the six SpriteBox
//    inits, and Dialog_item through StringStruct_add (the Dialog's, the BasePop's and the SpriteBox's
//    constructors, destructors, closes and text setters are here: see "Life cycle");
//  - everything that draws or measures with a Font and a Buffer in the SpriteBox (calculate_dimensions,
//    draw_item, on_left_click, on_key_down and on_redraw; the popup's own layout and redraw are here: see
//    "BasePop: layout and drawing"), runs the popup (BasePop_start and exec_3, which quiet.c already holds while state is protected)
//    or calls a function that is not ours to run on a scratch object (BasePop_on_button_clicked calls
//    Dialogs_UNK at 0x613740, the SpriteBox's attach and sprite, Dialog's UNK2, a window procedure);
//  - the runs of a popup: pops_18 (0x6276a0), pop_ask_6 (0x627910), pop_ask_number and its three
//    kin, BasePop_start and exec_3 (they open a window and run its loop, with the heap, the script file
//    and the Font and Buffer in use; the entry points above call them, and a check of one keeps a log
//    instead of calling them), the X_ functions that call the entry points, text_get and the other readers of the script's lines (they read a file with the C
//    runtime's fgets), text_item_string (it adds the text to the string table, on the game's heap), and
//    the make and close of the text indexes (they read the whole file and allocate);
//  - set_width, BasePop_init and string, Dialogs_item and SpriteBox_sprite, which the quiet hooks
//    already own.
//
// Differences from OpenSMACX and Thinker: neither lays the classes out. parse_string reads its digit
// tests as signed, so a character below '0' (after "$NUM" or the grammar of a choice) passes as a negative
// variable and reads the table before it; it never ends on a "$NUM", "$HEX" or other word with no digit
// after it (the original asks for the same dollar sign for ever; this ends the line there, as for the
// other words), and the gender of a choice made with '#' is the stack's leftover in the original (the line's
// last choice, or whatever was there), 0 here. stoi wraps a long number round, as the C runtime's atoi did. Dialog_UNK1 calls slot 1 of
// the Dialog's vtable with the Dialog's position; BasePop_UNK3 and UNK4 set bit 0 and bit 1 of the
// field at 0x20; the defaults' colour setters count their row from a number that is not checked above
// 3, so one past it writes the next row's first colour (and a last row's, the Dialog default flags).
// purge_spaces copies through a 512-byte buffer on the stack; this moves the text in place, so it does
// not overrun on a longer one. kill_lf cuts at the last line feed (the game's 0x6458a0 is strrchr, not
// strchr). Dialog_close answers -1, whatever is left in the register the original set the Dialog's -1
// fields from; init_5 and init_7 are hooked past their call of Dialog_close (the hook table re-aims it).
//
// Checks. A function that writes only the object and a few globals is checked as it is called, on the
// memory it writes. A function that calls through a vtable, a screen's function or the Windows
// journal (the double click and left-down handlers of the popup, the SpriteBox's mouse functions, UNK1)
// has effects no check can put back, so the game's calls of it run the original in shadow mode and
// the sweep checks it on scratch objects whose vtables log the calls. Dialog_close and the init
// functions empty a list through such vtables and free the data, so they are the same, with the
// sweep also running every init on a twin pair of Dialogs that are given a size (the data is made on
// the real heap, which a check cannot put back) and comparing the two field by field, the data by
// whether it is set. A plant that writes nothing, or a wrong value, shows as a difference.
//
// The text functions are checked as they are called (they write the buffer they are given, the item buffer
// and the line's place, or nothing) with the sweep on made-up lines and indexes. parse_string's buffer has
// no known size: its check takes as many bytes as the longer of what ours makes and the line's own length.
// The entry points are checked by logging: the check runs the original entry point and ours, each ending in
// a call that a shadow-only hook on pops_18 or pop_ask_6 answers with a count and records, and compares the
// two logs and answers; the game's own calls of them run the original.
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bufobj.h"
#include "button.h"
#include "drawcheck.h"
#include "dialog.h"
#include "game.h"
#include "hooks.h"
#include "life.h"
#include "edit.h"
#include "menu.h"
#include "scroll.h"
#include "sprite.h"
#include "win.h"
#include "shadow.h"
#include "strlist.h"
#include "text.h"

#define DLG (&dialog_module)
extern struct hook_module dialog_module;

// ---- Helpers ------------------------------------------------------------------------------------

static void *vslot(void *o, int slot) { return *(void **)((uint8_t *)*(void ***)o + slot); }
static int vcall0(void *o, int slot) { return ((int(THISCALL *)(void *))vslot(o, slot))(o); }
static int vcall1(void *o, int slot, int a) { return ((int(THISCALL *)(void *, int))vslot(o, slot))(o, a); }

// Delete an object through its vtable: the entry at 0x4 is where the part that holds the deleting
// destructor lies, and its vtable's slot 0 is called with 1.
static void vdelete(void *o) {
    void **vt = *(void ***)o;
    uint8_t *part = (uint8_t *)o + (intptr_t)vt[1];
    ((void(THISCALL *)(void *, int))(*(void ***)part)[0])(part, 1);
}

// ---- Text ---------------------------------------------------------------------------------------

// The first character that is not a space (the answer is the address, as a number).
static char *skip_spaces(char *s) {
    while (*s == ' ') s++;
    return s;
}
int purge_leading(char *s) { return (int)(intptr_t)skip_spaces(s); }
// Cut off the spaces at the end.
void purge_trailing(char *s) {
    size_t n = strlen(s);
    if (!n) return;
    char *p = s + n - 1;
    while (p >= s && *p == ' ') {
        *p = 0;
        if (p == s) break;
        p--;
    }
}
// Both ends: the text moves down to the start.
void purge_spaces(char *s) {
    purge_trailing(s);
    char *p = skip_spaces(s);
    memmove(s, p, strlen(p) + 1);
}
// Cut the text at its last line feed (the game's strrchr scans from the end).
void kill_lf(char *s) {
    char *p = strrchr(s, '\n');
    if (p) *p = 0;
}
// A line feed at the end (the room is the caller's).
void add_lf(char *s) {
    size_t n = strlen(s);
    s[n] = '\n';
    s[n + 1] = 0;
}

// ---- Text variables -----------------------------------------------------------------------------

// The first character in the text that is a digit, by the two characters the game keeps for the range
// (signed comparisons, as the original's), or NULL.
char *findnum(char *s) {
    if (!s) return NULL;
    int8_t lo = game_findnum_range[0], hi = game_findnum_range[1];
    for (; *s; s++)
        if ((int8_t)*s >= lo && (int8_t)*s <= hi) return s;
    return NULL;
}

// French: " de ", " le " or " la " (and "De ", "Le ", "La " at the start of a sentence) before a vowel or
// an h loses its vowel and its space. Each pass takes the earliest of the six words, and the next one
// starts a character after it.
static void french_articles(char *text) {
    char *from = text;
    for (;;) {
        char *best = NULL;
        int len = 0;
        for (int i = 0; i < 6; i++) {
            char *m = strstr(from, game_fr_articles + i * 20);
            if (m && (!best || m < best)) best = m, len = i < 3 ? 2 : 1;
        }
        if (!best) return;
        char after = best[len + 2];
        if (after && strchr("aeiouyhAEIOUYH", after)) {
            char *p = best + len;
            *p = '\'';
            char c;
            do {
                c = p[2];
                p++;
                *p = c;
            } while (c);
        }
        from = best + 1;
    }
}

// Fill the text variables into a line. The text before a directive is copied to the output and the
// directive's own text goes in its place:
//   $$           a dollar sign
//   $NUMBERn     the number n (set by parse_num), in decimal; so are $NUMn and $HEXn
//   $LINK<       kept as it is (the link's text goes through the same way)
//   $<...>       a choice among words, by gender and number (below)
//   $NAMEn ...   any other word that starts with a dollar sign takes the text variable n (a string
//                parse_says set), n being the first digit after the word, wherever it is; a dollar sign
//                with no digit anywhere after it ends the line there.
// A choice is "$<", the grammar, the words separated by colons, and ">". The grammar is M, F or N (the
// gender 0, 1 or 2) with an optional digit after it that sets the number (the digit less one), "#n"
// (the number n's value, the plural unless it is 1), or a digit n (the gender and number parse_say set
// for the variable n). The word is picked by how many colons there are: 1: singular or plural; 2: by
// gender; 3: by gender not 0 and by plural; 5: by gender and by plural (three each); with any other
// count the one that number of colons along. With no colons the words are in the text variable named
// by the first digit after the grammar. A variable number above 9 in a "$NUM" or "$HEX" ends the line
// with the answer 14; a null pointer answers 3. After a French line (language 1) the articles are
// joined to the vowels after them. The rest of the line, after the last word, is added with strcat: the
// buffer's own text stays when the line has nothing the output was written for (no dollar sign in it,
// or a word with no digit after it, which stops the line), and the line is put after it. The callers
// that mean to take the line only start the buffer with a 0, and supply_options, which does not for its
// second and third lines, has them follow the first.
// The original tests its digits with signed comparisons, so a character below '0' passes as a negative
// number and reads the table before its start; this does too. It never ends on a "$NUM..." or "$HEX..."
// word with no digit after it: it looks for the same dollar sign again forever; this ends the line, as
// the other words do. The gender of a "#" choice is whatever the stack held in the original (the
// value of the line's last choice, or what was there); this starts the line with 0.
int parse_string(char *src, char *dst) {
    if (!src || !dst) return 3;
    char *cur = src, *out = dst;
    int gender = 0;
    for (;;) {
        char *d = strchr(cur, '$');
        if (!d) break;
        size_t n = (size_t)(d - cur);
        char kind = d[1];
        if (kind == '$') {
            memcpy(out, cur, n + 1);
            out += n + 1, cur = d + 2, *out = 0;
            continue;
        }
        if (kind == '<') {
            memcpy(out, cur, n);
            out += n, *out = 0;
            char *close = strchr(d, '>');
            if (!close) {
                cur = d + 1;
                continue;
            }
            char *p = d + 2;
            cur = close + 1;
            while (*p == ' ') p++;
            char c = *p;
            int var = -1, plural = 0;
            if (c == 'M' || c == 'm') gender = 0;
            else if (c == 'F' || c == 'f') gender = 1;
            else if (c == 'N' || c == 'n') gender = 2;
            else if (c == '#') {
                var = (int8_t)*++p - '0';
                plural = game_parse_nums[var] != 1;
            } else {
                var = (int8_t)c - '0';
                if (var >= 10) continue;
                gender = game_parse_genders[var], plural = game_parse_plurals[var];
            }
            p++;
            if (var < 0 && *p >= '0' && *p <= '9') plural = *p++ - '1';
            while (*p == ' ') p++;
            if (*p == ':') p++;
            int colons = 0;
            for (char *q = p; q < cur; q++) colons += *q == ':';
            int words = 1;  // whether the words are there (else, in a variable)
            if (!colons) {
                char *digit = findnum(p);
                if (!digit) words = 0;
                else {
                    var = (int8_t)*digit - '0';
                    if (var >= 10) continue;
                    p = game_parse_text + var * 256;
                    for (char *q = p; *q; q++) colons += *q == ':';
                    if (!*p) words = 0;
                }
            }
            if (words) {
                int pick = colons;
                if (colons == 1) pick = plural != 0;
                else if (colons == 2) pick = gender;
                else if (colons == 3) pick = (gender != 0) + (plural ? 2 : 0);
                else if (colons == 5) pick = gender + (plural ? 3 : 0);
                while (pick)
                    if (*p++ == ':') pick--;
            }
            while (*p && *p != ':' && *p != '>') *out++ = *p++;
            *out = 0;
            continue;
        }
        if (kind == 'L' && !strncmp(d, "$LINK<", 6)) {
            memcpy(out, cur, n + 1);
            out += n + 1, cur = d + 1, *out = 0;
            continue;
        }
        int digits = kind == 'N' && !strncmp(d, "$NUMBER", 7) ? 7 : (kind == 'N' && !strncmp(d, "$NUM", 4)) || (kind == 'H' && !strncmp(d, "$HEX", 4)) ? 4 : 0;
        if (digits) {
            int k = (int8_t)d[digits] - '0';
            if (k > 9) return 14;
            memcpy(out, cur, n);
            out += n, *out = 0;
            out += sprintf(out, "%d", game_parse_nums[k]);
            cur = d + digits + 1;
            continue;
        }
        // Any other word: the first digit after it names the variable.
        char *digit = findnum(d);
        if (!digit) break;
        int k = (int8_t)*digit - '0';
        if (k > 9) return 14;
        memcpy(out, cur, n);
        out += n, *out = 0;
        cur = digit + 1;
        strcpy(out, game_parse_text + k * 256);
        out += strlen(out);
    }
    strcat(out, cur);
    if (*game_language == 1) french_articles(dst);
    return 0;
}

// ---- Script text --------------------------------------------------------------------------------

// Numbers in text: the C runtime's own isxdigit, isdigit and toupper took anything, and answered no for
// what is not an ASCII letter or digit.
static int is_hex(int c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }

// Binary digits: the value of the 0s and 1s at the start (zero when there are none).
int btoi(const char *s) {
    uint32_t v = 0;
    for (; *s == '0' || *s == '1'; s++) v = v * 2 + (uint32_t)(*s - '0');
    return (int)v;
}
// Hexadecimal digits, either case.
int htoi(const char *s) {
    uint32_t v = 0;
    for (; is_hex((int8_t)*s); s++) {
        char c = *s;
        v = v * 16 + (uint32_t)(c <= '9' ? c - '0' : (c & ~0x20) - 'A' + 10);
    }
    return (int)v;
}
// The C runtime's atoi: white space, a sign and digits; a number too long for an int wraps round (the
// original's does not stop at the largest).
static int atoi_wrap(const char *s) {
    while (*s == ' ' || (*s >= 9 && *s <= 13)) s++;
    int neg = *s == '-';
    if (*s == '-' || *s == '+') s++;
    uint32_t v = 0;
    for (; *s >= '0' && *s <= '9'; s++) v = v * 10 + (uint32_t)(*s - '0');
    return (int)(neg ? 0 - v : v);
}
// A number from text: "0b" or "0B" and binary digits, "0x" or "0X" and hexadecimal ones, and "0d" or
// "0D" and decimal ones are read as such. Any other text is decimal (atoi); one that starts with a 0
// is read from the character after it.
int stoi(const char *s) {
    if (*s != '0') return atoi_wrap(s);
    s++;
    switch (*s) {
    case 'b': case 'B': return btoi(s + 1);
    case 'x': case 'X': return htoi(s + 1);
    case 'd': case 'D': return atoi_wrap(s + 1);
    default: return atoi_wrap(s);
    }
}

// The next item of the line the script file's reader holds (the game's text_get): the characters up
// to a comma (or the end), moved to the item buffer with the spaces off both ends, the line's place
// going past the comma. The original moves the place a character at a time, as it copies.
static char *take_item(char **cursor, char *buf) {
    char *p = *cursor, *out = buf;
    while (*p && *p != ',') *out++ = *p++;
    *cursor = p;
    *out = 0;
    if (*p) *cursor = p + 1;
    purge_spaces(buf);
    return buf;
}
// (The answer is the address, as a number, as purge_leading's is.)
int text_item(void) { return (int)(intptr_t)take_item(game_text_cursor, *game_text_item_buf); }
int text_item_number(void) { return stoi((char *)(intptr_t)text_item()); }
int text_item_binary(void) { return btoi((char *)(intptr_t)text_item()); }
int text_item_hex(void) { return htoi((char *)(intptr_t)text_item()); }

// The same on a Text object (the line's place at 0x150 and the item buffer at 0x15c; the game never calls these).
static char **obj_cursor(void *t) { return (char **)((uint8_t *)t + 0x150); }
static char *obj_buf(void *t) { return *(char **)((uint8_t *)t + 0x15c); }
int DLG_THIS text_obj_item(void *t) { return (int)(intptr_t)take_item(obj_cursor(t), obj_buf(t)); }
int DLG_THIS text_obj_item_number(void *t) { return stoi((char *)(intptr_t)text_obj_item(t)); }
int DLG_THIS text_obj_item_binary(void *t) { return btoi((char *)(intptr_t)text_obj_item(t)); }
int DLG_THIS text_obj_item_hex(void *t) { return htoi((char *)(intptr_t)text_obj_item(t)); }

// Case-blind comparison, as the C runtime's _stricmp.
static int icmp(const char *a, const char *b) {
    for (;; a++, b++) {
        int x = (uint8_t)*a, y = (uint8_t)*b;
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return x < y ? -1 : 1;
        if (!x) return 0;
    }
}

// Where a "#LABEL" is in a script file, from the index the game made of its labels: the Text object holds
// the file's name at 0 (it must be the given name, with ".txt" added if there is no dot in it) and the
// number of labels at 0x100, and a pointer at 0x108 to the labels, each a file offset (an int) followed by
// its name and a 0. The first label equal to the given one (with the "#" off), whatever case, answers its
// offset; no match, or another file, -1. The count is only looked at after the first label.
int DLG_THIS text_index_search(void *t, const char *name, const char *label) {
    char file[0x200];
    strcpy(file, name);
    if (!strchr(file, '.')) strcat(file, ".txt");
    if (icmp(file, (const char *)t)) return -1;
    if (*label == '#') label++;
    int left = *(int32_t *)((uint8_t *)t + 0x100);
    const char *entry = *(const char **)((uint8_t *)t + 0x108);
    for (;;) {
        const char *at = entry + 4;
        if (!icmp(label, at)) return *(const int32_t *)entry;
        entry = at + strlen(at) + 1;
        if (--left == 0) return -1;
    }
}
// The same over the game's four text indexes (the ones that have labels).
int text_search_index(const char *name, const char *label) {
    for (int i = 0; i < 4; i++) {
        uint8_t *slot = game_text_slots + i * 0x118;
        if (!*(int32_t *)(slot + 0x100)) continue;
        int at = text_index_search(slot, name, label);
        if (at >= 0) return at;
    }
    return -1;
}

// ---- Popup entry points ----------------------------------------------------------------------

// The functions the game's code calls to show a message: each takes the label (and the file, unless it is
// the game's own popup file, the one set by pop_set_def_file) and what its kind of popup needs, and passes
// them to pops_18 (0x6276a0), which takes nine arguments; the ones the kind of popup fixes are 0, -1 or 1,
// and the file is the popup file's address when it is not given. The ones with a string (popup_3,
// popups_7 and popups_8, popup_ask_4 and popup_ask_5) pass its length too, and answer -1 at once when it
// is a null pointer. popup_ask and its kin do the same for pop_ask_6
// (0x627910), whose popup takes a number typed in. A run of the popup itself (pops_18, pop_ask_6,
// pop_ask_number_4 and the others that call BasePop_start and exec_3) stays with the original.
int popup(int a0, int a1) { return game_pops_18((int)(intptr_t)game_pop_file, a0, -1, 0, 0, 0, 1, 1, a1); }
int popup_2(int a0, int a1, int a2) { return game_pops_18(a0, a1, -1, 0, 0, 0, 1, 1, a2); }
int popup_3(int a0, int a1, int a2, int a3, int a4) {
    if (!a2) return -1;
    return game_pops_18(a0, a1, (int)strlen((const char *)(intptr_t)a2), a2, a3, 0, 1, 1, a4);
}
int popup_4(int a0, int a1, int a2, int a3) { return game_pops_18((int)(intptr_t)game_pop_file, a0, a1, 0, a2, 0, 1, 1, a3); }
int popup_5(int a0, int a1, int a2, int a3, int a4) { return game_pops_18(a0, a1, a2, 0, a3, 0, 1, 1, a4); }
int popup_6(int a0, int a1, int a2) { return game_pops_18((int)(intptr_t)game_pop_file, a0, -1, 0, a1, 0, 1, 1, a2); }
int popup_7(int a0, int a1, int a2, int a3) { return game_pops_18(a0, a1, -1, 0, a2, 0, 1, 1, a3); }
int popup_8(int a0, int a1, int a2, int a3, int a4) { return game_pops_18((int)(intptr_t)game_pop_file, a0, a1, a2, a3, 0, 1, 1, a4); }
int popup_9(int a0, int a1, int a2, int a3, int a4, int a5) { return game_pops_18(a0, a1, a2, a3, a4, 0, 1, 1, a5); }
int popups(int a0, int a1, int a2) { return game_pops_18((int)(intptr_t)game_pop_file, a0, -1, 0, 0, a1, 1, 1, a2); }
int popups_2(int a0, int a1, int a2, int a3, int a4) { return game_pops_18((int)(intptr_t)game_pop_file, a0, -1, 0, 0, a1, a2, a3, a4); }
int popups_3(int a0, int a1, int a2, int a3) { return game_pops_18(a0, a1, -1, 0, 0, a2, 1, 1, a3); }
int popups_4(int a0, int a1, int a2, int a3, int a4, int a5) { return game_pops_18(a0, a1, -1, 0, 0, a2, a3, a4, a5); }
int popups_5(int a0, int a1, int a2, int a3, int a4) { return game_pops_18((int)(intptr_t)game_pop_file, a0, a1, 0, a2, a3, 1, 1, a4); }
int popups_6(int a0, int a1, int a2, int a3, int a4, int a5, int a6) { return game_pops_18((int)(intptr_t)game_pop_file, a0, a1, 0, a2, a3, a4, a5, a6); }
int popups_7(int a0, int a1, int a2, int a3, int a4, int a5) {
    if (!a2) return -1;
    return game_pops_18(a0, a1, (int)strlen((const char *)(intptr_t)a2), a2, a3, a4, 1, 1, a5);
}
int popups_8(int a0, int a1, int a2, int a3, int a4, int a5, int a6, int a7) {
    if (!a2) return -1;
    return game_pops_18(a0, a1, (int)strlen((const char *)(intptr_t)a2), a2, a3, a4, a5, a6, a7);
}
int popups_9(int a0, int a1, int a2, int a3, int a4, int a5) { return game_pops_18(a0, a1, a2, 0, a3, a4, 1, 1, a5); }
int popups_10(int a0, int a1, int a2, int a3, int a4, int a5, int a6, int a7) { return game_pops_18(a0, a1, a2, 0, a3, a4, a5, a6, a7); }
int popups_11(int a0, int a1, int a2, int a3) { return game_pops_18((int)(intptr_t)game_pop_file, a0, -1, 0, a1, a2, 1, 1, a3); }
int popups_12(int a0, int a1, int a2, int a3, int a4, int a5) { return game_pops_18((int)(intptr_t)game_pop_file, a0, -1, 0, a1, a2, a3, a4, a5); }
int popups_13(int a0, int a1, int a2, int a3, int a4) { return game_pops_18(a0, a1, -1, 0, a2, a3, 1, 1, a4); }
int popups_14(int a0, int a1, int a2, int a3, int a4, int a5, int a6) { return game_pops_18(a0, a1, -1, 0, a2, a3, a4, a5, a6); }
int popups_15(int a0, int a1, int a2, int a3, int a4, int a5) { return game_pops_18((int)(intptr_t)game_pop_file, a0, a1, a2, a3, a4, 1, 1, a5); }
int popups_16(int a0, int a1, int a2, int a3, int a4, int a5, int a6, int a7) { return game_pops_18((int)(intptr_t)game_pop_file, a0, a1, a2, a3, a4, a5, a6, a7); }
int popups_17(int a0, int a1, int a2, int a3, int a4, int a5, int a6) { return game_pops_18(a0, a1, a2, a3, a4, a5, 1, 1, a6); }
int popup_ask(int a0, int a1, int a2, int a3, int a4) { return game_pop_ask_6((int)(intptr_t)game_pop_file, a0, a1, a2, a3, a4); }
int popup_ask_2(int a0, int a1, int a2, int a3, int a4) { return game_pop_ask_6(a0, a1, a2, 0, a3, a4); }
int popup_ask_3(int a0, int a1, int a2, int a3) { return game_pop_ask_6((int)(intptr_t)game_pop_file, a0, a1, 0, a2, a3); }
int popup_ask_4(int a0, int a1, int a2, int a3, int a4) {
    if (!a2) return -1;
    return game_pop_ask_6(a0, a1, (int)strlen((const char *)(intptr_t)a2), a2, a3, a4);
}
int popup_ask_5(int a0, int a1, int a2, int a3) {
    if (!a1) return -1;
    return game_pop_ask_6((int)(intptr_t)game_pop_file, a0, (int)strlen((const char *)(intptr_t)a1), a1, a2, a3);
}

// ---- BasePop ------------------------------------------------------------------------------------

// The Dialog and CheckBox a BasePop holds as virtual bases: the word at 0x21d0 (0x2228) is a table whose
// entry at 0x8 says how far from there the Dialog is.
static struct game_dialog *bp_dialog(struct game_base_pop *p) {
    return (struct game_dialog *)((uint8_t *)p + 0x21d0 + p->dlg_vbtable[2]);
}
static struct game_dialog *bp_check_dialog(struct game_base_pop *p) {
    return (struct game_dialog *)((uint8_t *)p + 0x2228 + p->chk_vbtable[2]);
}

// The height of the buttons' Dialog: a screen at least 1024 wide scales it by 3/2 when the popup has no
// external window and is not marked 0x400.
void DLG_THIS pop_set_height(struct game_base_pop *p, int h) {
    struct game_dialog *d = bp_dialog(p);
    if (!p->ext && !(p->flags & 0x400) && *game_win_screen_w >= 0x400)
        d->h = h * 3 / 2;
    else
        d->h = h;
}
// Where it goes: 0x2000 leaves a coordinate as it is.
void DLG_THIS pop_set_loc(struct game_base_pop *p, int x, int y) {
    if (x != 0x2000) p->x = x;
    if (y != 0x2000) p->y = y;
}
// The CheckBox's selection mask.
int DLG_THIS pop_read_check(struct game_base_pop *p) { return bp_check_dialog(p)->sel; }
int DLG_THIS pop_on_key_click(struct game_base_pop *p, int a, int b) {
    (void)a, (void)b;
    return !(p->flags & 0x4000);
}
int DLG_THIS pop_on_key_up(struct game_base_pop *p, int a) {
    (void)a;
    return !(p->flags & 0x4000);
}
// Take the selected button's id as the answer (the CheckBox's mask when it is the default button's kind).
int DLG_THIS pop_on_key_down(struct game_base_pop *p, int key) {
    (void)key;
    if (p->flags & 0x4000) return 0;
    int def = p->def_button;
    p->answer = 0;
    if (def != -1) {
        struct game_dialog *d = bp_dialog(p);
        if (d->list.count) {
            if (def == 1) {
                p->answer = bp_check_dialog(p)->sel;
                return 1;
            }
            p->answer = dialog_get_selected_id((struct game_dlg *)d);
        }
    }
    return 1;
}
// A double click on the popup that has the focus: the selected button is the answer, then the popup is
// redrawn (unless 0x100) and, with 0x4000, closed (slot 0x8).
void pop_double_click(void) {
    struct game_base_pop *p = (struct game_base_pop *)*game_win_modal_focus;
    if (!p) return;
    int def = p->def_button;
    uint32_t flags = p->flags;
    if (def == 1) return;
    if (def != -1) {
        struct game_dialog *d = bp_dialog(p);
        if (d->list.count) p->answer = dialog_get_selected_id((struct game_dlg *)d);
    }
    p->result = 0;
    if (!(flags & 0x100)) {
        if (p->ext)
            vcall0(p->ext, 0xe8);
        else
            vcall0(p, 0xe8);
    }
    if (flags & 0x4000) vcall0(p, 0x8);
}
// A press inside one of the buttons' rectangles (kind 0 only) tells the popup (slot 0xb0).
void DLG_THIS pop_on_left_down(struct game_base_pop *p, int x, int y) {
    int id = 0, kind = 0;
    if (spot_check(&p->spots, x, y, &id, &kind) != -1 && kind == 0) vcall1(p, 0xb0, id);
}
// The external window (and where it is, its width and height): the Dialog takes the size and the
// position is kept unless it is 0x2000.
void DLG_THIS pop_set_external_window(struct game_base_pop *p, void *win, int x, int y, int w, int h) {
    p->ext = win, p->ext_a = x, p->ext_b = y, p->ext_c = w, p->ext_d = h;
    if (!win) return;
    struct game_dialog *d = bp_dialog(p);
    d->w = w;
    d->h = h;  // (the screen scaling in set_height is out of reach with a window)
    if (x != 0x2000) p->x = x;
    if (y != 0x2000) p->y = y;
    d->flags |= 3;
}
void pop_set_def_external_window(int a, int b, int c, int d, int e) {
    game_pop_ext_a[0] = a, game_pop_ext_a[1] = b, game_pop_ext_a[2] = c;
    game_pop_ext_b[0] = d, game_pop_ext_b[1] = e;
}

// The fonts: a font that is not made (no GDI font) leaves the old one, and none at all answers 3.
int DLG_THIS pop_set_string_font(struct game_base_pop *p, struct font *f, int a, int b, int c) {
    if (!f) return 3;
    if (f->hfont) p->string_font = f;
    p->string_a = a, p->string_b = b, p->string_c = c;
    return 0;
}
int DLG_THIS pop_set_button_font(struct game_base_pop *p, struct font *f, int a, int b) {
    if (!f) return 3;
    if (f->hfont) p->button_font = f;
    p->button_a = a, p->button_b = b;
    return 0;
}
// The text colours: column `v` of the four rows (string colour, 2, 3 and the link colour).
static void set_string_color(struct game_base_pop *p, int v, int a, int b, int c, int d) {
    p->string_color[0][v] = a, p->string_color[1][v] = b, p->string_color[2][v] = c, p->string_color[3][v] = d;
}
void DLG_THIS pop_set_string_color(struct game_base_pop *p, int a, int b, int c, int d) { set_string_color(p, 0, a, b, c, d); }
void DLG_THIS pop_set_string_color2(struct game_base_pop *p, int a, int b, int c, int d) { set_string_color(p, 1, a, b, c, d); }
void DLG_THIS pop_set_string_color3(struct game_base_pop *p, int a, int b, int c, int d) { set_string_color(p, 2, a, b, c, d); }
void DLG_THIS pop_set_string_color_hyper(struct game_base_pop *p, int a, int b, int c, int d) { set_string_color(p, 3, a, b, c, d); }
// A button's colours: the first is a byte.
static void set_button_color(struct game_base_pop *p, int v, int a, int b, int c, int d) {
    p->button_flag[v] = (uint8_t)a;
    p->button_color[0][v] = b, p->button_color[1][v] = c, p->button_color[2][v] = d;
}
void DLG_THIS pop_set_button_color(struct game_base_pop *p, int a, int b, int c, int d) { set_button_color(p, 0, a, b, c, d); }
void DLG_THIS pop_set_button_color2(struct game_base_pop *p, int a, int b, int c, int d) { set_button_color(p, 1, a, b, c, d); }
void DLG_THIS pop_set_button_color3(struct game_base_pop *p, int a, int b, int c, int d) { set_button_color(p, 2, a, b, c, d); }

// The defaults new popups take.
int pop_set_def_string_font(struct font *f, int a, int b, int c) {
    if (!f) return 3;
    if (f->hfont) game_pop_fonts->string_font = f;
    game_pop_fonts->string_a = a, game_pop_fonts->string_b = b, game_pop_fonts->string_c = c;
    return 0;
}
int pop_set_def_button_font(struct font *f, int a, int b) {
    if (!f) return 3;
    if (f->hfont) game_pop_fonts->button_font = f;
    game_pop_fonts->button_a = a, game_pop_fonts->button_b = b;
    return 0;
}
static void set_def_string_color(int v, int a, int b, int c, int d) {
    game_pop_defs->string[0][v] = a, game_pop_defs->string[1][v] = b, game_pop_defs->string[2][v] = c, game_pop_defs->string[3][v] = d;
}
void pop_set_def_string_color(int a, int b, int c, int d) { set_def_string_color(0, a, b, c, d); }
void pop_set_def_string_color2(int a, int b, int c, int d) { set_def_string_color(1, a, b, c, d); }
void pop_set_def_string_color3(int a, int b, int c, int d) { set_def_string_color(2, a, b, c, d); }
void pop_set_def_string_color_hyper(int a, int b, int c, int d) { set_def_string_color(3, a, b, c, d); }
static void set_def_button_color(int v, int a, int b, int c, int d) {
    game_pop_defs->button[0][v] = a, game_pop_defs->button[1][v] = b, game_pop_defs->button[2][v] = c, game_pop_defs->button[3][v] = d;
}
void pop_set_def_button_color(int a, int b, int c, int d) { set_def_button_color(0, a, b, c, d); }
void pop_set_def_button_color2(int a, int b, int c, int d) { set_def_button_color(1, a, b, c, d); }
void pop_set_def_button_color3(int a, int b, int c, int d) { set_def_button_color(2, a, b, c, d); }

// The name of the file popups read their text from: at most twelve characters (answers 3 for more or
// none).
int pop_set_def_file(const char *name) {
    if (!name || strlen(name) > 12) return 3;
    game_pop_file[0] = 0;
    strcpy(game_pop_file, name);
    return 0;
}
int pop_on_modal(void) { return *game_pop_open == 0; }
void pop_fallout(void) {
    if (*game_pop_fallout_on) *game_pop_open = 1;
}
void DLG_THIS pop_unk3(struct game_base_pop *p, int on) { p->f20 = on ? p->f20 | 1 : p->f20 & ~1u; }
void DLG_THIS pop_unk4(struct game_base_pop *p, int on) { p->f20 = on ? p->f20 | 2 : p->f20 & ~2u; }

// ---- BasePop: layout and drawing ----------------------------------------------------------------
//
// calculate_dimensions works out how big the popup is (and what the combined widget it holds at 0x21d0 has to be) from the margins, the
// picture at 0x2144, the lines of text in the list at 0x2180 (measured with the string font and colours the popup holds, on the Buffer
// of the window it is drawn in: 0xa3c, which is the popup itself unless an external window was set), the buttons' font height and the
// window the popup is given (0xa14 set). on_redraw draws the background (the tile at 0x2148, or a fill), the frame picture at 0x214c,
// the picture at 0x2144 (or hands it to the function the screen set at 0x3168) and the text.
//
// Fields (offsets into the popup): 0x20f4 buttons per row, 0x2144 the picture, 0x2148 the background tile, 0x214c the frame picture,
// 0x2180 the list of lines (0x2188 head, 0x218c current, 0x2190 count, 0x2194 place), 0x21b0 to 0x21c4 margins asked for, 0x21c8 an
// indent, 0x21cc a width asked for (-1: none), 0x2350 the kind of the combined widget (-1 for none), 0xa14 the external window set
// (0xa18 to 0xa24: its position and size), 0xa3c the window drawn in, 0xa44 the item to select, 0x30a8 flags (0x2 the buttons are
// separate, 0x40 and 0x10100 count rows, 0x100 and 0x400 layout), 0x30b0 the rows of buttons, 0x30b4 and 0x30b8 where it is put, 0x30bc
// and 0x30c0 its size, 0x30c4 the widget's own height, 0x30cc and 0x30d0 a button's width and height, 0x30d4 the text's height, 0x30d8
// and 0x30dc the picture's size, 0x30e0 and 0x30e4 the margins used, 0x30e8 to 0x30f4 the margins the widget gets, 0x3104 and 0x3108
// the picture's scale (a ratio), 0x3110 to 0x315c the string font and the four colour rows of the Buffer, 0x316c the buttons' font.
// The kinds of the lines: 0 plain, 2 centred, 3 a blank line of the font's height. Three globals are written: 0x9b8cec and 0x9b8ce8
// (the width and the height the text took, for the screen that places the popup) and 0x9b8cf4 (the popup being drawn, set only around
// the picture's drawing).
//
// Differences from the references: OpenSMACX and Thinker have the names but no bodies. The lines after the first that are centred are
// measured on the popup's own Buffer, not the window's (the original does, and so does this). The original scales the height asked for
// by 1.5 on a wide screen only when there is no external window, in code that runs with one, so it never does and is left out. Where
// the original would read through a null pointer (a list with a head but no current line) this reads no text.

#define I_(o, off) (*(int32_t *)((uint8_t *)(o) + (off)))
#define PTR_(o, off) (*(void **)((uint8_t *)(o) + (off)))
#define G_(addr) (*(int32_t *)game_at(addr))
enum { BPG_WIDTH = 0x9b8cec, BPG_HEIGHT = 0x9b8ce8, BPG_DRAWING = 0x9b8cf4, BPG_HANDLER = 0x6044d0 };

// The line a list's current node holds (its text at 0x4, its kind at 0x8); none without a head or a current node.
static const char *line_text(const uint8_t *o) {
    if (!I_(o, 0x2188) || !PTR_(o, 0x218c)) return NULL;
    uint8_t *item = ((struct game_dlist_node *)PTR_(o, 0x218c))->item;
    return item ? *(const char **)(item + 4) : NULL;
}
static int line_kind(const uint8_t *o) {
    if (!I_(o, 0x2188) || !PTR_(o, 0x218c)) return 0;
    uint8_t *item = ((struct game_dlist_node *)PTR_(o, 0x218c))->item;
    return item ? *(int32_t *)(item + 8) : 0;
}

// The colours and the font the lines are drawn in, set on the Buffer of the window the popup is drawn in.
static void pop_text_setup(const uint8_t *o, struct game_buffer *b) {
    buffer_set_text_color(b, I_(o, 0x3120), I_(o, 0x3130), I_(o, 0x3140), I_(o, 0x3150));
    buffer_set_text_color2(b, I_(o, 0x3124), I_(o, 0x3134), I_(o, 0x3144), I_(o, 0x3154));
    buffer_set_text_color3(b, I_(o, 0x3128), I_(o, 0x3138), I_(o, 0x3148), I_(o, 0x3158));
    buffer_set_text_color_hyper(b, I_(o, 0x312c), I_(o, 0x313c), I_(o, 0x314c), I_(o, 0x315c));
    buffer_set_font(b, PTR_(o, 0x3110), PTR_(o, 0x3114), PTR_(o, 0x3118), PTR_(o, 0x311c));
}

// Draws the popup: the background, the frame picture, the picture and the text.
void DLG_THIS pop_on_redraw(struct game_base_pop *p) {
    uint8_t *o = (uint8_t *)p;
    struct game_buffer *own = (struct game_buffer *)(o + 0x444);
    buffer_clear_links_draw(own);
    int32_t margin = I_(o, 0x30e0);
    int32_t x = margin + I_(o, 0x30f0) + I_(o, 0x21c8);
    int32_t top = I_(o, 0x30e8) + margin, y = top;
    uint8_t *host = PTR_(o, 0xa3c);
    if (host == o) {
        win_move((struct game_win *)p, I_(o, 0x30b4), I_(o, 0x30b8));
        if (!vcall0(p, 0x180)) {
            struct game_sprite *tile = PTR_(o, 0x2148);
            if (tile && !I_(o, 0xa14)) buffer_tile_2(own, tile, 0, 0, 0, 0, own->width, -own->neg_height);
            else graphic_win_fill((struct game_win *)p, I_(o, 0x310c));
        }
        struct game_sprite *frame = PTR_(o, 0x214c);
        if (!I_(o, 0xa14) && frame && frame->width < own->width && frame->height < -own->neg_height) {
            int32_t indent = I_(o, 0x21c8);
            sprite_draw_2(frame, own, frame->key, (own->width - frame->full_w - indent) / 2 + indent,
                          -((frame->full_h + own->neg_height) / 2), 1, 1);
        }
    } else {
        vcall0(p, 0x180);
        x += I_(o, 0x30b4), y += I_(o, 0x30b8);
    }
    struct game_sprite *pic = PTR_(o, 0x2144);
    if (pic) {
        G_(BPG_DRAWING) = (int32_t)(intptr_t)p;
        int(__attribute__((cdecl)) * handler)(void *, void *, int, int, int) = (void *)PTR_(o, 0x3168);
        if (handler) {
            int id = sprite_box_pos_to_id((struct game_sprite_box *)(o + 0x2240), 0);
            handler(pic, host, id, x, y);
        } else {
            sprite_draw_2(pic, host ? (struct game_buffer *)(host + 0x444) : NULL, pic->key, x, y, I_(o, 0x3104), I_(o, 0x3108));
        }
        G_(BPG_DRAWING) = 0;
    }
    int32_t picture_w = I_(o, 0x30d8);
    x += picture_w;
    if (I_(o, 0x30d4) == 0) {
        G_(BPG_WIDTH) = x - picture_w - I_(o, 0x21c8);
        int32_t least = I_(o, 0x30dc) + top;
        G_(BPG_HEIGHT) = y > least ? y : least;
        return;
    }
    struct game_buffer *hb = (struct game_buffer *)(host + 0x444);
    pop_text_setup(o, hb);
    int32_t width = I_(o, 0x30bc) - 2 * margin - I_(o, 0x30f4) - picture_w - I_(o, 0x30f0) - I_(o, 0x21c8);
    int32_t end = buffer_write_strings(hb, (struct str_list *)(o + 0x2180), x, y, width, 0);
    G_(BPG_WIDTH) = x - picture_w - I_(o, 0x21c8);
    int32_t least = top + I_(o, 0x30dc);
    G_(BPG_HEIGHT) = end > least ? end : least;
}

// The Dialog the combined widget holds as a virtual base (found through the widget's table).
static uint8_t *widget_dialog(uint8_t *o) { return o + 0x21d0 + (*(int32_t **)(o + 0x21d0))[2]; }
#define D_(db, off) (*(int32_t *)((db) + (off)))

// Works out the popup's size and the room the widget and the text take; answers 0, or what the widget's layout answered when that was
// not 0 (the work is then left off where it was).
int DLG_THIS pop_calculate_dimensions(struct game_base_pop *p) {
    uint8_t *o = (uint8_t *)p;
    uint8_t *host = PTR_(o, 0xa3c);
    struct game_buffer *hb = (struct game_buffer *)(host + 0x444);
    pop_text_setup(o, hb);
    int ext = I_(o, 0xa14) != 0;
    // The rows of buttons: as many as asked for, one more if flag 0x40, and one more unless flag 0x100 or 0x10000.
    I_(o, 0x30b0) = I_(o, 0x20f4);
    if (o[0x30a8] & 0x40) I_(o, 0x30b0)++;
    if (!(I_(o, 0x30a8) & 0x10100)) I_(o, 0x30b0)++;
    // The margins: none sideways with an external window; one and a half times as asked on a screen 1024 wide or more unless flag 0x400
    // forbids it; otherwise as asked.
    if (ext) {
        I_(o, 0x30e0) = 0, I_(o, 0x30e4) = I_(o, 0x21c4);
    } else if (!(I_(o, 0x30a8) & 0x400) && *game_win_screen_w >= 0x400) {
        I_(o, 0x30e0) = I_(o, 0x21c0) * 3 / 2, I_(o, 0x30e4) = I_(o, 0x21c4) * 3 / 2;
    } else {
        I_(o, 0x30e0) = I_(o, 0x21c0), I_(o, 0x30e4) = I_(o, 0x21c4);
    }
    I_(o, 0x30e8) = I_(o, 0x21b8), I_(o, 0x30ec) = I_(o, 0x21bc);
    int32_t side = I_(o, 0x21b0);
    I_(o, 0x30f0) = side, I_(o, 0x30f4) = I_(o, 0x21b4);
    // The picture's size, scaled (the width with the margin, and a gap if there are lines of text).
    struct game_sprite *pic = PTR_(o, 0x2144);
    if (pic) {
        int32_t gap = I_(o, 0x2190) ? I_(o, 0x30e4) : 0;
        int32_t num = I_(o, 0x3104), den = I_(o, 0x3108);
        I_(o, 0x30d8) = pic->full_w * num / den + I_(o, 0x30e0) + gap;
        I_(o, 0x30dc) = pic->full_h * num / den;
    } else {
        I_(o, 0x30d8) = 0, I_(o, 0x30dc) = 0;
    }
    int32_t font_height = ((struct font *)PTR_(o, 0x316c))->height;
    uint8_t *db = widget_dialog(o);
    if (ext) {
        // The widget gets the window's room, and is laid out for it.
        D_(db, 0x2c) = I_(o, 0xa20) - 2 * I_(o, 0x30e0) - I_(o, 0x21b4) - side;
        D_(db, 0x30) = I_(o, 0xa24) - (I_(o, 0x30b0) ? I_(o, 0x30d0) : 0) - I_(o, 0x30ec) - I_(o, 0x30e8);
        if (I_(o, 0xa18) != 0x2000) I_(o, 0x30f8) = I_(o, 0xa18);
        if (I_(o, 0xa1c) != 0x2000) I_(o, 0x30fc) = I_(o, 0xa1c);
        D_(db, 0x20) |= 1;
        pop_unk4((struct game_base_pop *)db, 1);
    }
    int r = game_dialog_calc(o + 0x21d0);
    if (r) return r;
    // The popup's width: the widget's (what it made, or what it was asked for) or, without an external window, the picture's if wider,
    // with the margins and the width asked for.
    int32_t wide = D_(db, 0x5c) ? D_(db, 0x5c) : D_(db, 0x2c);
    if (ext) {
        I_(o, 0x30bc) = wide;
        I_(o, 0x30c0) = D_(db, 0x60) ? D_(db, 0x60) : D_(db, 0x30);
    } else {
        int32_t widest = wide > I_(o, 0x30d8) ? wide : I_(o, 0x30d8);
        I_(o, 0x30bc) = I_(o, 0x21cc) == -1 ? widest + 2 * I_(o, 0x30e0) : I_(o, 0x21cc) + I_(o, 0x30e0) + widest;
    }
    I_(o, 0x30c4) = D_(db, 0x60);
    // What a widget of the popup's kind is given: the function that answers a double click (the popup's own) and the item selected.
    int32_t item = I_(o, 0xa44);
    int32_t handler = (int32_t)(intptr_t)game_at(BPG_HANDLER);
    switch ((uint32_t)(I_(o, 0x2350) + 1)) {
    case 0: break;
    case 2: I_(o, 0x2234) = handler; break;
    case 3:
        if (item != -1) listbox_set_selected_id((struct game_listbox *)(o + 0x21d0), item);
        else listbox_set_selected_pos((struct game_listbox *)(o + 0x21d0), 0, 1);
        I_(o, 0x2210) = handler;
        break;
    case 5:
        if (item != -1) group_set_text_limits((struct game_editgroup *)(o + 0x22c8), item);
        break;
    case 9:
        I_(o, 0x22a8) = I_(o, 0x3160), I_(o, 0x22c0) = I_(o, 0x3164), I_(o, 0x22a4) = I_(o, 0x3168);
        I_(o, 0x2260) = I_(o, 0x3108), I_(o, 0x225c) = I_(o, 0x3104);
        break;
    default:
        if (item != -1) dialog_set_selected_id((struct game_dlg *)db, item);
        else D_(db, 0xec) = 0;
        I_(o, 0x2224) = handler;
    }
    // The height of the lines: the first, then each in turn, as the Buffer lays flying text out.
    if (I_(o, 0x2190) - 1 >= 0) I_(o, 0x218c) = I_(o, 0x2188), I_(o, 0x2194) = 0;
    int32_t room = I_(o, 0x30bc) - 2 * I_(o, 0x30e0) - I_(o, 0x21c8) - I_(o, 0x30d8);
    int kind = line_kind(o);
    I_(o, 0x30d4) = 0;
    if (kind == 3) {
        I_(o, 0x30d4) += buffer_text_line_height(hb);
        buffer_wrap_height_flying(hb, NULL, room);
    } else if (kind == 2) {
        I_(o, 0x30d4) += buffer_wrap_height(hb, line_text(o), room);
        buffer_wrap_height_flying(hb, NULL, room);
    } else {
        buffer_wrap_height_flying(hb, line_text(o), room);
    }
    if (I_(o, 0x2190) - 1 > 0) {
        int i = 0;
        do {
            if (I_(o, 0x2188)) {
                struct game_dlist_node *cur = PTR_(o, 0x218c);
                int32_t at = I_(o, 0x2194) + 1;
                PTR_(o, 0x218c) = cur->next;
                I_(o, 0x2194) = at;
                if (at == I_(o, 0x2190)) I_(o, 0x2194) = 0;
            }
            const char *text = line_text(o);
            if (I_(o, 0x2188)) {
                int k = line_kind(o);
                if (k != 0) {
                    I_(o, 0x30d4) += buffer_wrap_height_flying_flush(hb);
                    int h;
                    if (k == 2) {
                        h = buffer_wrap_height((struct game_buffer *)(o + 0x444), text, room);
                    } else if (k == 3) {
                        h = buffer_text_line_height(hb);
                    } else {
                        buffer_wrap_height_flying(hb, text, room);
                        goto next;
                    }
                    I_(o, 0x30d4) += h;
                    buffer_wrap_height_flying(hb, NULL, room);
                } else {
                    buffer_wrap_height_flying_2(hb, text);
                }
            } else {
                buffer_wrap_height_flying_2(hb, NULL);
            }
        next:
            i++;
        } while (i < I_(o, 0x2190) - 1);
    }
    I_(o, 0x30d4) += buffer_wrap_height_flying_flush(hb);
    int32_t text_h = I_(o, 0x30d4);
    // A button: seven quarters of a font high, and the row's room shared by the rows of buttons.
    if (I_(o, 0x30b0)) {
        I_(o, 0x30d0) = font_height * 7 / 4;
        I_(o, 0x30cc) = (I_(o, 0x30bc) - 2 * I_(o, 0x30e0) - (I_(o, 0x30b0) - 1) * I_(o, 0x30e4)) / I_(o, 0x30b0);
    } else {
        I_(o, 0x30d0) = 0, I_(o, 0x30cc) = 0;
    }
    if (ext && (I_(o, 0x30a8) & 2)) {
        D_(db, 0x30) = I_(o, 0xa24) - 2 * I_(o, 0x30e4) - I_(o, 0x30d0) - text_h;
        r = game_dialog_calc(o + 0x21d0);
        if (r) return r;
    }
    vcall0(p, 0x174);
    if (I_(o, 0x30a8) & 0x100) I_(o, 0x30d0) = 0;
    int32_t taller = I_(o, 0x30d4) > I_(o, 0x30dc) ? I_(o, 0x30d4) : I_(o, 0x30dc);
    if (ext) {
        D_(db, 0x2c) = I_(o, 0xa20) - 2 * I_(o, 0x30e0) - I_(o, 0x30f4) - I_(o, 0x30f0);
        D_(db, 0x30) = I_(o, 0xa24) - (I_(o, 0x30b0) ? I_(o, 0x30d0) : 0) - taller - I_(o, 0x30ec) - I_(o, 0x30e8);
        if (I_(o, 0xa18) != 0x2000) I_(o, 0x30f8) = I_(o, 0xa18);
        if (I_(o, 0xa1c) != 0x2000) I_(o, 0x30fc) = I_(o, 0xa1c);
        D_(db, 0x20) |= 3;
        I_(o, 0x30bc) = I_(o, 0xa20), I_(o, 0x30c0) = I_(o, 0xa24);
        if (I_(o, 0x2350) == -1) I_(o, 0x30c4) = 0, D_(db, 0x60) = 0;
    } else {
        int32_t own = I_(o, 0x30c4), gap = I_(o, 0x30e4);
        int32_t h = own + 2 * I_(o, 0x30e0) + taller + I_(o, 0x30d0);
        if (own) h += gap;
        if (I_(o, 0x30d0)) h += gap;
        I_(o, 0x30c0) = h + I_(o, 0x30ec) + I_(o, 0x30e8);
        I_(o, 0x30bc) += I_(o, 0x30f4) + I_(o, 0x30f0);
    }
    int apart = host != o;
    G_(BPG_WIDTH) = (apart ? I_(o, 0x30b4) : 0) + I_(o, 0x30f0) + I_(o, 0x30e0) + I_(o, 0x30d8);
    G_(BPG_HEIGHT) = (apart ? I_(o, 0x30b8) : 0) + I_(o, 0x30e0) + I_(o, 0x30d4) + I_(o, 0x30e8);
    return 0;
}

// ---- Dialog -------------------------------------------------------------------------------------

// Empty the list: each item is told to the list (slot 1) and deleted, then its node; the count is read
// again after every one, as the original does.
static void list_clear(struct game_dlist *l) {
    if (l->head) {
        int i = 0;
        if (l->count > 0) {
            do {
                struct game_dlist_node *n = l->head;
                l->cur = n->next;
                void *item = n->item;
                ((void(THISCALL *)(void *, void *))l->vtable[1])(l, item);
                if (item) vdelete(item);
                l->head->item = NULL;
                if (l->head) vdelete(l->head);
                l->head = l->cur;
                i++;
            } while (i < l->count);
        }
        l->head = NULL, l->count = 0, l->idx = 0;
    }
    l->idx = 0;
}

// What a Dialog takes at the start from the game's defaults (everything but the list and the data).
static void dialog_defaults(struct game_dialog *d) {
    const struct game_dialog_defs_a *a = game_dialog_defs_a;
    const struct game_dialog_defs_b *b = game_dialog_defs_b;
    d->flags = (uint32_t)a->flags, d->x = 0, d->y = 0, d->w = a->w, d->h = b->h;
    d->sel = 0, d->hover = -1;
    d->v38 = 0, d->v3c = 0, d->v40 = b->v40, d->v44 = a->v44, d->v48 = -1, d->v34 = b->v34, d->v4c = a->v4c, d->v50 = a->v50;
    memset(d->v54, 0, sizeof d->v54);
    d->v6c = a->v6c;
    d->vac = b->vac, d->vb0 = b->vb0, d->vb4 = b->vb4, d->vb8 = b->vb8;
    d->font = b->font, d->font_a = b->font_a, d->font_b = b->font_b;
    memcpy(d->color, a->color, sizeof d->color);
    d->size = -1;
}
// Empty the list, close the data and put everything back. The answer is -1, what the original leaves in
// its register (the one it set the Dialog's -1 fields from).
int DLG_THIS dialog_close(struct game_dialog *d) {
    list_clear(&d->list);
    game_dialog_data_close(&d->data_flag);
    d->ext = NULL;
    dialog_defaults(d);
    return -1;
}

// The tail the inits share: give the data its size (answering 4 when there is no memory), empty the list
// again and set where the list's data is (the data, a block a caller gave, or none).
static int dialog_finish_size(struct game_dialog *d) {
    if (d->size) {
        if (game_dialog_data_alloc(&d->data_flag, d->size)) return 4;
        list_clear(&d->list);
        d->list.buf = &d->data_flag;
    } else {
        list_clear(&d->list);
        d->list.buf = NULL;
    }
    return 0;
}
static int dialog_finish_ext(struct game_dialog *d, void *ext) {
    d->ext = ext;
    list_clear(&d->list);
    d->list.buf = ext;
    return 0;
}
static void dialog_place(struct game_dialog *d, int x, int y, int w, int h) {
    d->x = x, d->w = w, d->h = h, d->y = y;
    d->flags |= 3;
}

// Make the data its size (-1 gives the init no work and answers 1).
int DLG_THIS dialog_init(struct game_dialog *d) {
    if (d->size < 0) return 1;
    list_clear(&d->list);
    return dialog_finish_size(d);
}
// Position and size, then the data's size.
int DLG_THIS dialog_init_2(struct game_dialog *d, int x, int y, int w, int h, int size) {
    dialog_close(d);
    dialog_place(d, x, y, w, h);
    d->size = size;
    return dialog_finish_size(d);
}
// The same from a rectangle (left, top, right, bottom); none answers 3.
int DLG_THIS dialog_init_3(struct game_dialog *d, const int32_t *r, int size) {
    if (!r) return 3;
    dialog_close(d);
    dialog_place(d, r[0], r[1], r[2] - r[0], r[3] - r[1]);
    d->size = size;
    return dialog_finish_size(d);
}
int DLG_THIS dialog_init_4(struct game_dialog *d, int size) {
    dialog_close(d);
    d->size = size;
    return dialog_finish_size(d);
}
// With a block of the caller's.
int DLG_THIS dialog_init_5(struct game_dialog *d, int x, int y, int w, int h, void *ext) {
    dialog_close(d);
    dialog_place(d, x, y, w, h);
    return dialog_finish_ext(d, ext);
}
int DLG_THIS dialog_init_6(struct game_dialog *d, const int32_t *r, void *ext) {
    if (!r) return 3;
    dialog_close(d);
    dialog_place(d, r[0], r[1], r[2] - r[0], r[3] - r[1]);
    return dialog_finish_ext(d, ext);
}
int DLG_THIS dialog_init_7(struct game_dialog *d, void *ext) {
    dialog_close(d);
    d->size = -1;
    return dialog_finish_ext(d, ext);
}
// The Dialog's vtable slot 1 with the position (an item added at the Dialog's place).
int DLG_THIS dialog_unk1(struct game_dialog *d, int a, int b) {
    return ((int(THISCALL *)(void *, int, int, int, int))vslot(d, 4))(d, a, d->x, d->y, b);
}

int DLG_THIS dialog_set_font(struct game_dialog *d, struct font *f, int a, int b) {
    if (!f) return 3;
    if (f->hfont) d->font = f;
    d->font_a = a, d->font_b = b;
    return 0;
}
static void set_dialog_color(struct game_dialog *d, int j, int a, int b, int c, int e) {
    d->color[0][j] = a, d->color[1][j] = b, d->color[2][j] = c, d->color[3][j] = e;
}
void DLG_THIS dialog_set_text_color(struct game_dialog *d, int a, int b, int c, int e) { set_dialog_color(d, 0, a, b, c, e); }
void DLG_THIS dialog_set_text_color2(struct game_dialog *d, int a, int b, int c, int e) { set_dialog_color(d, 1, a, b, c, e); }
void DLG_THIS dialog_set_text_color3(struct game_dialog *d, int a, int b, int c, int e) { set_dialog_color(d, 2, a, b, c, e); }
int dialog_set_def_font(struct font *f, int a, int b) {
    if (!f) return 3;
    if (f->hfont) game_dialog_defs_b->font = f;
    game_dialog_defs_b->font_a = a, game_dialog_defs_b->font_b = b;
    return 0;
}
// The defaults of the text colours, for column `idx` (0 for one outside 0 to 3). The column is not
// limited to the three there are: 3 reaches the next row's first colour, and the last row's the
// default flags.
void dialog_set_def_text_color(int a, int b, int c, int d, int idx) {
    if (idx < 0 || idx > 3) idx = 0;
    int32_t *f = (int32_t *)((uint8_t *)game_dialog_defs_a + offsetof(struct game_dialog_defs_a, color));
    f[idx] = a, f[3 + idx] = b, f[6 + idx] = c, f[9 + idx] = d;
}

// ---- SpriteBox ----------------------------------------------------------------------------------

// Find the sprite with this id from the head: `cur` and `idx` are left at it, or after a full turn of the
// ring when there is none. With no sprites nothing is touched; the answer is the place.
static int list_seek(struct game_dlist *l, int id) {
    if (l->head) {
        int n = l->count;
        l->idx = 0;
        l->cur = l->head;
        for (int i = 0; i < n; i++) {
            if (l->cur->key == id) break;
            l->idx++;
            l->cur = l->cur->next;
        }
    }
    return l->idx;
}
int DLG_THIS sprite_box_id_to_pos(struct game_sprite_box *sb, int id) { return list_seek(&sb->list, id); }
// Go to a place (back from the head if it is negative) and answer the id there. A place past the end,
// or further back than a full turn, leaves `cur` at the head and `idx` as it was.
int DLG_THIS sprite_box_pos_to_id(struct game_sprite_box *sb, int pos) {
    struct game_dlist *l = &sb->list;
    if (pos <= l->count - 1) {
        l->cur = l->head;
        if (pos < 0) {
            int n = pos < 0 ? (int)(0u - (uint32_t)pos) : pos;
            if (n <= l->count) {
                if (n > 0) {
                    do l->cur = l->cur->prev;
                    while (--n != 0);
                }
                l->idx = l->count + pos;
            }
        } else {
            if (pos > 0) {
                for (int n = pos; n != 0; n--) l->cur = l->cur->next;
            }
            l->idx = pos;
        }
    }
    return l->head ? l->cur->key : 0;
}
// The Win part (at 0x8c) is drawn again: the SpriteBox's vtable entry at 0x4 says where the Win is.
void DLG_THIS sprite_box_on_dialog_focus(void *w, int unused) {
    (void)unused;
    uint8_t *sb = (uint8_t *)w - 0x8c;
    vcall0(sb + (*(int32_t **)sb)[1], 0xf8);
}

// A mouse event on the Win part: the sprite hit tells the screen's function (which answers non-zero to
// have the Enter key posted to the window).
static void sprite_mouse(void *w, int x, int y, int fn) {
    struct game_sprite_box *sb = (struct game_sprite_box *)((uint8_t *)w - 0x8c);
    int id = 0;
    if (spot_check(&sb->spot, x, y, &id, NULL) == -1) return;
    if (!sb->fn[fn]) return;
    if (sb->fn[fn](id)) game_post_message((void *)(uintptr_t)*game_win_hwnd, 0x100, 0xd, 0);
}
void DLG_THIS sprite_box_on_left_down(void *w, int x, int y) { sprite_mouse(w, x, y, 1); }
void DLG_THIS sprite_box_on_left_up(void *w, int x, int y) { sprite_mouse(w, x, y, 2); }
void DLG_THIS sprite_box_on_right_click(void *w, int x, int y) { sprite_mouse(w, x, y, 3); }
void DLG_THIS sprite_box_on_right_down(void *w, int x, int y) { sprite_mouse(w, x, y, 4); }
void DLG_THIS sprite_box_on_right_up(void *w, int x, int y) { sprite_mouse(w, x, y, 5); }
void DLG_THIS sprite_box_on_left_double_click(void *w, int x, int y) { sprite_mouse(w, x, y, 6); }
void DLG_THIS sprite_box_on_right_double_click(void *w, int x, int y) { sprite_mouse(w, x, y, 7); }

// ---- Shadow checks ------------------------------------------------------------------------------

#define P(p) ((int)(uintptr_t)(p))

// What the stand-in vtables of a sweep leave behind: a count and a hash of their calls, so a different call,
// argument or order shows as a difference in these eight bytes.
static struct {
    uint32_t hash;
    int32_t n;
} vlog;
static void writes_vlog(struct hook_writes *w) { hooks_writes_add(w, &vlog, sizeof vlog); }

enum { ND = 3, NN = 5, NB = 2, NS = 2 };
struct sbox_scratch {
    struct game_sprite_box sb;
    uint8_t tail[0x40];  // (the Win part and what follows it)
};
struct scratch {
    struct game_dialog dlg[ND];
    struct game_dlist_node nodes[ND][NN];
    void *items[ND][NN];
    struct game_base_pop pop[NB];
    struct game_dialog pop_dlg[NB][2];  // (the BasePop's Dialog and CheckBox)
    struct game_dlist_node pnodes[NB][2][NN];
    void *pitems[NB][2][NN];
    struct {
        void **vtable;
    } ext;
    struct sbox_scratch box[NS];
    struct game_dlist_node snodes[NS][NN];
    void *sitems[NS][NN];
    struct game_spot_rect prects[NB][4], brects[NS][4];
    int32_t vb_dlg[NB][4], vb_chk[NB][4];
};
static struct scratch S;
static void writes_scratch(struct hook_writes *w) {
    hooks_writes_add(w, &S, sizeof S);
    writes_vlog(w);
}

// The sweep's checks that cannot run on the game's own objects: in shadow mode the game's calls run the
// original (and are counted, to start the sweep); the sweep calls check_NAME.
#define SWEEP_ONLY(ret, name, params, args, writes, fmt, ...)             \
    SHADOW_DECLARE_T(ret, name, params)                                   \
    SHADOW_WRITER_BODY(ret, name, params, args, writes, fmt, __VA_ARGS__) \
    static ret THISCALL shadow_##name params {                            \
        if (!hooks_nested && !shadow_bypass()) {                          \
            stats_##name.calls++;                                         \
            shadow_tick(DLG);                                             \
        }                                                                 \
        SHADOW_TAIL_##ret(orig_##name args);                              \
    }
// The same for a cdecl function with no arguments.
#define SWEEP_ONLY_C0(name, writes)                                              \
    SHADOW_WRITER_CHECK(void, name, (void), (), orig_##name(), writes, "%s", "") \
    static void shadow_##name(void) {                                            \
        if (!hooks_nested && !shadow_bypass()) {                                 \
            stats_##name.calls++;                                                \
            shadow_tick(DLG);                                                    \
        }                                                                        \
        orig_##name();                                                           \
    }

#define BPW(p) WRITES((p), sizeof(struct game_base_pop))
#define DW(d) WRITES((d), sizeof(struct game_dialog))

// ---- Text.
SHADOW_FN(DLG, int, purge_leading, (char *s), (s), "%d", P(s))
SHADOW_WRITER(DLG, void, purge_trailing, (char *s), (s), WRITES(s, strlen(s) + 1), "%d", P(s))
SHADOW_WRITER(DLG, void, purge_spaces, (char *s), (s), WRITES(s, strlen(s) + 1), "%d", P(s))
SHADOW_WRITER(DLG, void, kill_lf, (char *s), (s), WRITES(s, strlen(s) + 1), "%d", P(s))
SHADOW_WRITER(DLG, void, add_lf, (char *s), (s), WRITES(s, strlen(s) + 2), "%d", P(s))
SHADOW_FN(DLG, char *, findnum, (char *s), (s), "%d", P(s))
SHADOW_FN(DLG, int, stoi, (const char *s), (s), "%s", s)
SHADOW_FN(DLG, int, btoi, (const char *s), (s), "%s", s)
SHADOW_FN(DLG, int, htoi, (const char *s), (s), "%s", s)
// The item functions write the item buffer (as many characters as there are before a comma) and the line's place.
static size_t item_room(const char *p) {
    size_t n = 0;
    while (p[n] && p[n] != ',') n++;
    return n + 1;
}
#define ITEM_WRITES                                  \
    WRITES(game_text_cursor, sizeof(char *));        \
    WRITES(*game_text_item_buf, item_room(*game_text_cursor))
#define OBJ_WRITES                             \
    WRITES(obj_cursor(t), sizeof(char *));     \
    WRITES(obj_buf(t), item_room(*obj_cursor(t)))
SHADOW_WRITER(DLG, int, text_item, (void), (), ITEM_WRITES, "%s", "")
SHADOW_WRITER(DLG, int, text_item_number, (void), (), ITEM_WRITES, "%s", "")
SHADOW_WRITER(DLG, int, text_item_binary, (void), (), ITEM_WRITES, "%s", "")
SHADOW_WRITER(DLG, int, text_item_hex, (void), (), ITEM_WRITES, "%s", "")
SHADOW_WRITER_T(DLG, int, text_obj_item, (void *t), (t), OBJ_WRITES, "%d", P(t))
SHADOW_WRITER_T(DLG, int, text_obj_item_number, (void *t), (t), OBJ_WRITES, "%d", P(t))
SHADOW_WRITER_T(DLG, int, text_obj_item_binary, (void *t), (t), OBJ_WRITES, "%d", P(t))
SHADOW_WRITER_T(DLG, int, text_obj_item_hex, (void *t), (t), OBJ_WRITES, "%d", P(t))
SHADOW_THIS_FN(DLG, int, text_index_search, (void *t, const char *name, const char *label), (t, name, label), "%s, %s", name,
               label)
SHADOW_FN(DLG, int, text_search_index, (const char *name, const char *label), (name, label), "%s, %s", name, label)
// The popup entry points. pops_18 and pop_ask_6 run a popup, which no check can put back; while one of the
// entry points is being checked, shadow-only hooks on those two keep a log of the calls they get and answer
// a number made from the count, without running. The check calls the original entry point and ours in turn
// (each ends in a call of pops_18 or pop_ask_6) and compares the answers and the logs. The game's own calls
// of an entry point run the original and are counted.
static int pops_logging;
static struct popup_log {
    int n;
    struct {
        int kind, a[9];
    } c[4];
} plog;
static int popup_stub(int kind, const int *a, int count) {
    if (plog.n < 4) {
        plog.c[plog.n].kind = kind;
        memcpy(plog.c[plog.n].a, a, (size_t)count * sizeof *a);
    }
    return 0x5100 + plog.n++;
}
static int (*orig_pops_18)(int, int, int, int, int, int, int, int, int);
static struct hook_stats stats_pops_18 = {"(logged pops_18)"};
static int shadow_pops_18(int a, int b, int c, int d, int e, int f, int g, int h, int i) {
    if (pops_logging) return popup_stub(0, (const int[]){a, b, c, d, e, f, g, h, i}, 9);
    return orig_pops_18(a, b, c, d, e, f, g, h, i);
}
static int (*orig_pop_ask_6)(int, int, int, int, int, int);
static struct hook_stats stats_pop_ask_6 = {"(logged pop_ask_6)"};
static int shadow_pop_ask_6(int a, int b, int c, int d, int e, int f) {
    if (pops_logging) return popup_stub(1, (const int[]){a, b, c, d, e, f}, 6);
    return orig_pop_ask_6(a, b, c, d, e, f);
}
#define FORWARDER(name, params, args)                                                                            \
    SHADOW_DECLARE(int, name, params)                                                                            \
    static int check_##name params {                                                                             \
        memset(&plog, 0, sizeof plog);                                                                           \
        pops_logging = 1;                                                                                        \
        hooks_nested++;                                                                                          \
        int want = orig_##name args;                                                                             \
        hooks_nested--;                                                                                          \
        struct popup_log wlog = plog;                                                                            \
        memset(&plog, 0, sizeof plog);                                                                           \
        int got = name args;                                                                                     \
        pops_logging = 0;                                                                                        \
        if (got != want || memcmp(&wlog, &plog, sizeof wlog))                                                    \
            hooks_mismatch(&stats_##name, "(a0 = %d) = %d, original %d; %d calls, original %d", (int)a0, got, want, plog.n, wlog.n); \
        return want;                                                                                             \
    }                                                                                                            \
    static int shadow_##name params {                                                                            \
        if (!hooks_nested && !shadow_bypass()) {                                                                 \
            stats_##name.calls++;                                                                                \
            shadow_tick(DLG);                                                                                    \
        }                                                                                                        \
        return orig_##name args;                                                                                 \
    }
FORWARDER(popup, (int a0, int a1), (a0, a1))
FORWARDER(popup_2, (int a0, int a1, int a2), (a0, a1, a2))
FORWARDER(popup_3, (int a0, int a1, int a2, int a3, int a4), (a0, a1, a2, a3, a4))
FORWARDER(popup_4, (int a0, int a1, int a2, int a3), (a0, a1, a2, a3))
FORWARDER(popup_5, (int a0, int a1, int a2, int a3, int a4), (a0, a1, a2, a3, a4))
FORWARDER(popup_6, (int a0, int a1, int a2), (a0, a1, a2))
FORWARDER(popup_7, (int a0, int a1, int a2, int a3), (a0, a1, a2, a3))
FORWARDER(popup_8, (int a0, int a1, int a2, int a3, int a4), (a0, a1, a2, a3, a4))
FORWARDER(popup_9, (int a0, int a1, int a2, int a3, int a4, int a5), (a0, a1, a2, a3, a4, a5))
FORWARDER(popups, (int a0, int a1, int a2), (a0, a1, a2))
FORWARDER(popups_2, (int a0, int a1, int a2, int a3, int a4), (a0, a1, a2, a3, a4))
FORWARDER(popups_3, (int a0, int a1, int a2, int a3), (a0, a1, a2, a3))
FORWARDER(popups_4, (int a0, int a1, int a2, int a3, int a4, int a5), (a0, a1, a2, a3, a4, a5))
FORWARDER(popups_5, (int a0, int a1, int a2, int a3, int a4), (a0, a1, a2, a3, a4))
FORWARDER(popups_6, (int a0, int a1, int a2, int a3, int a4, int a5, int a6), (a0, a1, a2, a3, a4, a5, a6))
FORWARDER(popups_7, (int a0, int a1, int a2, int a3, int a4, int a5), (a0, a1, a2, a3, a4, a5))
FORWARDER(popups_8, (int a0, int a1, int a2, int a3, int a4, int a5, int a6, int a7), (a0, a1, a2, a3, a4, a5, a6, a7))
FORWARDER(popups_9, (int a0, int a1, int a2, int a3, int a4, int a5), (a0, a1, a2, a3, a4, a5))
FORWARDER(popups_10, (int a0, int a1, int a2, int a3, int a4, int a5, int a6, int a7), (a0, a1, a2, a3, a4, a5, a6, a7))
FORWARDER(popups_11, (int a0, int a1, int a2, int a3), (a0, a1, a2, a3))
FORWARDER(popups_12, (int a0, int a1, int a2, int a3, int a4, int a5), (a0, a1, a2, a3, a4, a5))
FORWARDER(popups_13, (int a0, int a1, int a2, int a3, int a4), (a0, a1, a2, a3, a4))
FORWARDER(popups_14, (int a0, int a1, int a2, int a3, int a4, int a5, int a6), (a0, a1, a2, a3, a4, a5, a6))
FORWARDER(popups_15, (int a0, int a1, int a2, int a3, int a4, int a5), (a0, a1, a2, a3, a4, a5))
FORWARDER(popups_16, (int a0, int a1, int a2, int a3, int a4, int a5, int a6, int a7), (a0, a1, a2, a3, a4, a5, a6, a7))
FORWARDER(popups_17, (int a0, int a1, int a2, int a3, int a4, int a5, int a6), (a0, a1, a2, a3, a4, a5, a6))
FORWARDER(popup_ask, (int a0, int a1, int a2, int a3, int a4), (a0, a1, a2, a3, a4))
FORWARDER(popup_ask_2, (int a0, int a1, int a2, int a3, int a4), (a0, a1, a2, a3, a4))
FORWARDER(popup_ask_3, (int a0, int a1, int a2, int a3), (a0, a1, a2, a3))
FORWARDER(popup_ask_4, (int a0, int a1, int a2, int a3, int a4), (a0, a1, a2, a3, a4))
FORWARDER(popup_ask_5, (int a0, int a1, int a2, int a3), (a0, a1, a2, a3))
// parse_string writes the line into a buffer of the caller's, of no known size: the span is what ours makes
// of the text (and at least the text's own length, so that a version that writes nothing is not missed).
static size_t parse_span(char *src, char *dst) {
    static char scratch[0x10000];
    if (!src || !dst) return 0;
    size_t len = strlen(src), dollars = 0;
    for (char *p = src; *p; p++) dollars += *p == '$';
    if (len + dollars * 256 + 16 > sizeof scratch) return len + dollars * 256 + 16;
    size_t keep = strnlen(dst, 0x400);  // (a line with no dollar sign goes after the buffer's text)
    if (len + dollars * 256 + 16 + keep > sizeof scratch) return len + dollars * 256 + 16 + keep;
    memcpy(scratch, dst, keep), scratch[keep] = 0;
    parse_string(src, scratch);
    size_t n = strlen(scratch);
    return (n > len ? n : len) + 2;
}
SHADOW_WRITER(DLG, int, parse_string, (char *src, char *dst), (src, dst), WRITES(dst, parse_span(src, dst)), "%s",
              src ? src : "(null)")

// ---- BasePop. The object and the Dialog it holds are what a function can change.
#define BPD(p) (BPW(p), DW(bp_dialog(p)))
SHADOW_WRITER_T(DLG, void, pop_set_height, (struct game_base_pop * p, int h), (p, h), DW(bp_dialog(p)), "%d, %d", P(p), h)
SHADOW_WRITER_T(DLG, void, pop_set_loc, (struct game_base_pop * p, int x, int y), (p, x, y), BPW(p), "%d, %d, %d", P(p), x, y)
SHADOW_THIS_FN(DLG, int, pop_read_check, (struct game_base_pop * p), (p), "%d", P(p))
SHADOW_THIS_FN(DLG, int, pop_on_key_click, (struct game_base_pop * p, int a, int b), (p, a, b), "%d, %d, %d", P(p), a, b)
SHADOW_THIS_FN(DLG, int, pop_on_key_up, (struct game_base_pop * p, int a), (p, a), "%d, %d", P(p), a)
SHADOW_WRITER_T(DLG, int, pop_on_key_down, (struct game_base_pop * p, int key), (p, key), BPD(p), "%d, %d", P(p), key)
SWEEP_ONLY_C0(pop_double_click, writes_scratch(&w_))
SWEEP_ONLY(void, pop_on_left_down, (struct game_base_pop * p, int x, int y), (p, x, y), writes_scratch(&w_), "%d, %d, %d", P(p), x, y)
SHADOW_WRITER_T(DLG, void, pop_set_external_window, (struct game_base_pop * p, void *win, int x, int y, int w, int h), (p, win, x, y, w, h),
                BPD(p), "%d, %d, %d, %d, %d, %d", P(p), P(win), x, y, w, h)
SHADOW_WRITER(DLG, void, pop_set_def_external_window, (int a, int b, int c, int d, int e), (a, b, c, d, e),
              (WRITES(game_pop_ext_a, 12), WRITES(game_pop_ext_b, 8)), "%d, %d, %d, %d, %d", a, b, c, d, e)
SHADOW_WRITER_T(DLG, int, pop_set_string_font, (struct game_base_pop * p, struct font * f, int a, int b, int c), (p, f, a, b, c), BPW(p),
                "%d, %d, %d, %d, %d", P(p), P(f), a, b, c)
SHADOW_WRITER_T(DLG, int, pop_set_button_font, (struct game_base_pop * p, struct font * f, int a, int b), (p, f, a, b), BPW(p),
                "%d, %d, %d, %d", P(p), P(f), a, b)
#define POP_COLOR_CHECK(name)                                                                                   \
    SHADOW_WRITER_T(DLG, void, name, (struct game_base_pop * p, int a, int b, int c, int d), (p, a, b, c, d), BPW(p), \
                    "%d, %d, %d, %d, %d", P(p), a, b, c, d)
POP_COLOR_CHECK(pop_set_string_color)
POP_COLOR_CHECK(pop_set_string_color2)
POP_COLOR_CHECK(pop_set_string_color3)
POP_COLOR_CHECK(pop_set_string_color_hyper)
POP_COLOR_CHECK(pop_set_button_color)
POP_COLOR_CHECK(pop_set_button_color2)
POP_COLOR_CHECK(pop_set_button_color3)
SHADOW_WRITER(DLG, int, pop_set_def_string_font, (struct font * f, int a, int b, int c), (f, a, b, c), WRITES(game_pop_fonts, sizeof *game_pop_fonts),
              "%d, %d, %d, %d", P(f), a, b, c)
SHADOW_WRITER(DLG, int, pop_set_def_button_font, (struct font * f, int a, int b), (f, a, b), WRITES(game_pop_fonts, sizeof *game_pop_fonts),
              "%d, %d, %d", P(f), a, b)
#define POP_DEF_COLOR_CHECK(name)                                                                             \
    SHADOW_WRITER(DLG, void, name, (int a, int b, int c, int d), (a, b, c, d), WRITES(game_pop_defs, sizeof *game_pop_defs), \
                  "%d, %d, %d, %d", a, b, c, d)
POP_DEF_COLOR_CHECK(pop_set_def_string_color)
POP_DEF_COLOR_CHECK(pop_set_def_string_color2)
POP_DEF_COLOR_CHECK(pop_set_def_string_color3)
POP_DEF_COLOR_CHECK(pop_set_def_string_color_hyper)
POP_DEF_COLOR_CHECK(pop_set_def_button_color)
POP_DEF_COLOR_CHECK(pop_set_def_button_color2)
POP_DEF_COLOR_CHECK(pop_set_def_button_color3)
SHADOW_WRITER(DLG, int, pop_set_def_file, (const char *name), (name), WRITES(game_pop_file, 13), "%d", P(name))
SHADOW_FN(DLG, int, pop_on_modal, (void), (), "%s", "")
SHADOW_WRITER(DLG, void, pop_fallout, (void), (), WRITES(game_pop_open, 4), "%s", "")
SHADOW_WRITER_T(DLG, void, pop_unk3, (struct game_base_pop * p, int on), (p, on), WRITES(&p->f20, 4), "%d, %d", P(p), on)
SHADOW_WRITER_T(DLG, void, pop_unk4, (struct game_base_pop * p, int on), (p, on), WRITES(&p->f20, 4), "%d, %d", P(p), on)
static void vrec(int slot, const void *o, int a, int b, int c, int d);

// The popup's layout and drawing call the window's virtual functions, the function a screen set and the widget's layout, whose effects no
// check can put back, so the game's calls run the original and the sweep checks them on scratch popups (px_sweep, below). What they
// write: the popup (the widget and the Dialog it holds lie inside it), the Buffers of the popup and of the window it draws in (the
// object, the pixels and the device contexts text goes through), the globals text leaves, the three the popup's size is given in, and
// the logs of the stand-ins. Buffer_clear_links is held in the checks of the drawing (see bufobj.c) and logged.
#define PXG(addr) ((void *)game_at(addr))
static void writes_pop_draw(struct hook_writes *w, struct game_base_pop *p) {
    uint8_t *o = (uint8_t *)p;
    hooks_writes_add(w, p, sizeof *p);
    struct game_buffer *own = (struct game_buffer *)(o + 0x444);
    dc_writes_pixels(w, own), dc_writes_text_state(w, own);
    uint8_t *host = PTR_(o, 0xa3c);
    if (host && host != o) {
        struct game_buffer *hb = (struct game_buffer *)(host + 0x444);
        dc_writes_pixels(w, hb), dc_writes_text_state(w, hb);
    }
    hooks_writes_add(w, PXG(BPG_WIDTH), 4), hooks_writes_add(w, PXG(BPG_HEIGHT), 4), hooks_writes_add(w, PXG(BPG_DRAWING), 4);
    hooks_writes_add(w, &buffer_clear_links_log, sizeof buffer_clear_links_log);
    writes_vlog(w);
}
SWEEP_ONLY(void, pop_on_redraw, (struct game_base_pop * p), (p), writes_pop_draw(&w_, p), "%d", P(p))
SWEEP_ONLY(int, pop_calculate_dimensions, (struct game_base_pop * p), (p), writes_pop_draw(&w_, p), "%d", P(p))

// The widget's layout (0x6132a0) is not ours: in a check of the two above it is a stand-in that logs the call and answers from its count
// (the same for both runs), and gives the widget a size of its own half the time. Otherwise it is the original.
static int px_logging;
static int(THISCALL *orig_dialog_calc)(void *);
static struct hook_stats stats_dialog_calc = {"(logged dialog_calc)"};
static int THISCALL shadow_dialog_calc(void *w) {
    if (!px_logging) return orig_dialog_calc(w);
    stats_dialog_calc.calls++;
    uint8_t *db = (uint8_t *)w + (*(int32_t **)w)[2];
    vrec(0x500, w, I_(w, 0x180), D_(db, 0x2c), D_(db, 0x30), D_(db, 0x20));
    int n = vlog.n;
    D_(db, 0x5c) = n % 3 ? 0 : 150 + n % 17;
    D_(db, 0x60) = n % 4 ? 0 : 10 + n % 13;
    return n % 11 == 0 ? 5 : 0;
}

// ---- Dialog. The init functions and close empty a list through vtables and free the data, so the game's
// calls run the original; the sweep checks them on scratch Dialogs.
SWEEP_ONLY(int, dialog_close, (struct game_dialog * d), (d), writes_scratch(&w_), "%d", P(d))
SWEEP_ONLY(int, dialog_init, (struct game_dialog * d), (d), writes_scratch(&w_), "%d", P(d))
SWEEP_ONLY(int, dialog_init_2, (struct game_dialog * d, int x, int y, int w, int h, int size), (d, x, y, w, h, size), writes_scratch(&w_),
           "%d, %d, %d, %d, %d, %d", P(d), x, y, w, h, size)
SWEEP_ONLY(int, dialog_init_3, (struct game_dialog * d, const int32_t *r, int size), (d, r, size), writes_scratch(&w_), "%d, %d, %d", P(d), P(r),
           size)
SWEEP_ONLY(int, dialog_init_4, (struct game_dialog * d, int size), (d, size), writes_scratch(&w_), "%d, %d", P(d), size)
SWEEP_ONLY(int, dialog_init_5, (struct game_dialog * d, int x, int y, int w, int h, void *ext), (d, x, y, w, h, ext), writes_scratch(&w_),
           "%d, %d, %d, %d, %d, %d", P(d), x, y, w, h, P(ext))
SWEEP_ONLY(int, dialog_init_6, (struct game_dialog * d, const int32_t *r, void *ext), (d, r, ext), writes_scratch(&w_), "%d, %d, %d", P(d), P(r),
           P(ext))
SWEEP_ONLY(int, dialog_init_7, (struct game_dialog * d, void *ext), (d, ext), writes_scratch(&w_), "%d, %d", P(d), P(ext))
SWEEP_ONLY(int, dialog_unk1, (struct game_dialog * d, int a, int b), (d, a, b), writes_scratch(&w_), "%d, %d, %d", P(d), a, b)
SHADOW_WRITER_T(DLG, int, dialog_set_font, (struct game_dialog * d, struct font * f, int a, int b), (d, f, a, b), DW(d), "%d, %d, %d, %d",
                P(d), P(f), a, b)
#define DLG_COLOR_CHECK(name)                                                                                       \
    SHADOW_WRITER_T(DLG, void, name, (struct game_dialog * d, int a, int b, int c, int e), (d, a, b, c, e), DW(d), \
                    "%d, %d, %d, %d, %d", P(d), a, b, c, e)
DLG_COLOR_CHECK(dialog_set_text_color)
DLG_COLOR_CHECK(dialog_set_text_color2)
DLG_COLOR_CHECK(dialog_set_text_color3)
SHADOW_WRITER(DLG, int, dialog_set_def_font, (struct font * f, int a, int b), (f, a, b), WRITES(game_dialog_defs_b, sizeof *game_dialog_defs_b),
              "%d, %d, %d", P(f), a, b)
SHADOW_WRITER(DLG, void, dialog_set_def_text_color, (int a, int b, int c, int d, int idx), (a, b, c, d, idx),
              WRITES(game_dialog_defs_a, sizeof *game_dialog_defs_a), "%d, %d, %d, %d, %d", a, b, c, d, idx)

// ---- SpriteBox.
SHADOW_WRITER_T(DLG, int, sprite_box_id_to_pos, (struct game_sprite_box * sb, int id), (sb, id), WRITES(&sb->list, sizeof sb->list), "%d, %d", P(sb),
                id)
SHADOW_WRITER_T(DLG, int, sprite_box_pos_to_id, (struct game_sprite_box * sb, int pos), (sb, pos), WRITES(&sb->list, sizeof sb->list), "%d, %d",
                P(sb), pos)
SWEEP_ONLY(void, sprite_box_on_dialog_focus, (void *w, int unused), (w, unused), writes_scratch(&w_), "%d, %d", P(w), unused)
#define SB_MOUSE_CHECK(name) \
    SWEEP_ONLY(void, name, (void *w, int x, int y), (w, x, y), writes_scratch(&w_), "%d, %d, %d", P(w), x, y)
SB_MOUSE_CHECK(sprite_box_on_left_down)
SB_MOUSE_CHECK(sprite_box_on_left_up)
SB_MOUSE_CHECK(sprite_box_on_right_click)
SB_MOUSE_CHECK(sprite_box_on_right_down)
SB_MOUSE_CHECK(sprite_box_on_right_up)
SB_MOUSE_CHECK(sprite_box_on_left_double_click)
SB_MOUSE_CHECK(sprite_box_on_right_double_click)

// ---- Sweep --------------------------------------------------------------------------------------
//
// Scratch Dialogs, popups and sprite boxes in static memory with a random state each round, and stand-in
// vtables whose slots log the call (see vlog) and answer a number of their own, so what a function calls,
// and with what, is compared.

static uint32_t rng_state;
static uint32_t rnd(void) {
    rng_state ^= rng_state << 13, rng_state ^= rng_state >> 17, rng_state ^= rng_state << 5;
    return rng_state;
}
static int rnd_to(int n) { return (int)(rnd() % (uint32_t)n); }

static void vrec(int slot, const void *o, int a, int b, int c, int d) {
    vlog.hash = ((((vlog.hash * 31u + (uint32_t)slot) * 31u + (uint32_t)(uintptr_t)o) * 31u + (uint32_t)a) * 31u + (uint32_t)b * 7u +
                 (uint32_t)c * 13u) * 31u + (uint32_t)d * 17u;
    vlog.n++;
}
static void THISCALL stub_dtor(void *o, int flag) { vrec(0x100, o, flag, 0, 0, 0); }
static void THISCALL stub_list1(void *l, void *item) { vrec(0x101, l, (int)(intptr_t)item, 0, 0, 0); }
static int THISCALL stub_s4(void *o, int a, int x, int y, int b) { return vrec(0x4, o, a, x, y, b), 41; }
static int THISCALL stub_s8(void *o) { return vrec(0x8, o, 0, 0, 0, 0), 9; }
static int THISCALL stub_sb0(void *o, int a) { return vrec(0xb0, o, a, 0, 0, 0), 11; }
static int THISCALL stub_se8(void *o) { return vrec(0xe8, o, 0, 0, 0, 0), 13; }
static int THISCALL stub_sf8(void *o) { return vrec(0xf8, o, 0, 0, 0, 0), 15; }
static void *stub_vt[0x180 / 4], *sbox_vt[0x180 / 4], *item_vt[2], *list_vt[2];
static void fill_stub_vt(void) {
    stub_vt[0x4 / 4] = (void *)stub_s4, stub_vt[0x8 / 4] = (void *)stub_s8, stub_vt[0xb0 / 4] = (void *)stub_sb0;
    stub_vt[0xe8 / 4] = (void *)stub_se8, stub_vt[0xf8 / 4] = (void *)stub_sf8;
    memcpy(sbox_vt, stub_vt, sizeof sbox_vt);
    sbox_vt[1] = (void *)(intptr_t)(0x8c + 8);  // (where the Win is: the part after the SpriteBox's own fields)
    item_vt[0] = (void *)stub_dtor, item_vt[1] = NULL;
    list_vt[0] = NULL, list_vt[1] = (void *)stub_list1;
}

static int sprite_fn_answer;
static int cb_fn_a(int id) { return vrec(0x200, NULL, id, 0, 0, 0), sprite_fn_answer; }
static int cb_fn_b(int id) { return vrec(0x201, NULL, id, 0, 0, 0), !sprite_fn_answer; }

static void random_list(struct game_dlist *l, struct game_dlist_node *nodes, void **items) {
    l->vtable = list_vt;
    int nn = rnd_to(NN + 1);
    for (int i = 0; i < nn; i++) {
        struct game_dlist_node *n = &nodes[i];
        n->vtable = item_vt, n->key = rnd_to(6) - 1;
        items[i] = item_vt;
        n->item = rnd_to(5) ? (void *)&items[i] : NULL;
        n->next = &nodes[(i + 1) % nn], n->prev = &nodes[(i + nn - 1) % nn];
    }
    l->head = nn ? &nodes[0] : (rnd_to(4) ? NULL : &nodes[0]);
    l->cur = l->head ? &nodes[rnd_to(nn ? nn : 1)] : NULL;  // (a list with a head has a place in it)
    l->count = rnd_to(8) ? nn : rnd_to(NN + 1);
    if (!nn && l->head) l->count = 0;
    if (!l->head) l->count = 0;
    l->idx = rnd_to(NN + 2) - 1;
    l->buf = rnd_to(2) ? (void *)l : NULL;
}

static void random_dialog(struct game_dialog *d, struct game_dlist_node *nodes, void **items) {
    memset(d, 0, sizeof *d);
    d->vtable = stub_vt;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 3; j++) d->color[i][j] = (int)rnd();
    d->x = rnd_to(100) - 10, d->y = rnd_to(100) - 10, d->w = rnd_to(300), d->h = rnd_to(300), d->flags = rnd();
    d->v34 = (int)rnd(), d->v38 = (int)rnd(), d->v3c = (int)rnd(), d->v40 = (int)rnd(), d->v44 = (int)rnd(), d->v48 = (int)rnd();
    d->v4c = (int)rnd(), d->v50 = (int)rnd(), d->v6c = (int)rnd(), d->vac = (int)rnd(), d->vb0 = (int)rnd(), d->vb4 = (int)rnd(), d->vb8 = (int)rnd();
    for (int i = 0; i < 6; i++) d->v54[i] = (int)rnd();
    d->font = (void *)(intptr_t)rnd(), d->font_a = (int)rnd(), d->font_b = (int)rnd();
    d->ext = rnd_to(2) ? (void *)(intptr_t)rnd() : NULL;
    d->size = rnd_to(4) ? 0 : -1;  // (a size that makes data is left to the twins)
    d->sel = (int)rnd(), d->hover = (int)rnd();
    random_list(&d->list, nodes, items);
}

static void build(void) {
    memset(&S, 0, sizeof S);
    for (int k = 0; k < ND; k++) random_dialog(&S.dlg[k], S.nodes[k], S.items[k]);
    S.ext.vtable = stub_vt;
    for (int k = 0; k < NB; k++) {
        struct game_base_pop *p = &S.pop[k];
        p->vtable = stub_vt;
        random_dialog(&S.pop_dlg[k][0], S.pnodes[k][0], S.pitems[k][0]);
        random_dialog(&S.pop_dlg[k][1], S.pnodes[k][1], S.pitems[k][1]);
        S.vb_dlg[k][2] = (int32_t)((uint8_t *)&S.pop_dlg[k][0] - ((uint8_t *)p + 0x21d0));
        S.vb_chk[k][2] = (int32_t)((uint8_t *)&S.pop_dlg[k][1] - ((uint8_t *)p + 0x2228));
        p->dlg_vbtable = S.vb_dlg[k], p->chk_vbtable = S.vb_chk[k];
        p->flags = (rnd_to(2) ? 0x100 : 0) | (rnd_to(2) ? 0x400 : 0) | (rnd_to(3) ? 0 : 0x4000) | (uint32_t)rnd_to(8) << 28;
        p->def_button = rnd_to(6) - 2;
        p->ext = rnd_to(3) ? NULL : &S.ext;
        p->answer = (int)rnd(), p->result = (int)rnd(), p->x = (int)rnd(), p->y = (int)rnd();
        p->f20 = rnd();
        p->spots.rects = S.prects[k], p->spots.cap = 4, p->spots.count = rnd_to(5);
        for (int i = 0; i < 4; i++) {
            struct game_spot_rect *r = &S.prects[k][i];
            r->rect[0] = rnd_to(40), r->rect[1] = rnd_to(40), r->rect[2] = r->rect[0] + rnd_to(30), r->rect[3] = r->rect[1] + rnd_to(30);
            r->kind = rnd_to(3) == 0, r->id = rnd_to(6);
        }
    }
    for (int k = 0; k < NS; k++) {
        struct game_sprite_box *sb = &S.box[k].sb;
        sb->vtable = sbox_vt;
        *(void ***)(S.box[k].tail + 8) = stub_vt;
        random_list(&sb->list, S.snodes[k], S.sitems[k]);
        sb->spot.rects = S.brects[k], sb->spot.cap = 4, sb->spot.count = rnd_to(5);
        for (int i = 0; i < 4; i++) {
            struct game_spot_rect *r = &S.brects[k][i];
            r->rect[0] = rnd_to(40), r->rect[1] = rnd_to(40), r->rect[2] = r->rect[0] + rnd_to(30), r->rect[3] = r->rect[1] + rnd_to(30);
            r->kind = rnd_to(3), r->id = rnd_to(6);
        }
        for (int i = 0; i < 8; i++) sb->fn[i] = rnd_to(4) ? (rnd_to(2) ? cb_fn_a : cb_fn_b) : NULL;
    }
}

// Two Dialogs alike, one for the original's init and one for ours, for the inits given a size: the data is
// made on the real heap, which a check cannot put back, so they are compared outside any check. The data
// is compared by whether it is set (and the cursor by whether it is at its start), the list's data by
// whether it is the Dialog's own.
static struct game_dialog TA, TB;
static void twin_reset(void) {
    memset(&TA, 0, sizeof TA);
    TA.vtable = stub_vt, TA.list.vtable = list_vt;
    TA.x = rnd_to(100), TA.y = rnd_to(100), TA.w = rnd_to(300), TA.h = rnd_to(300), TA.flags = rnd();
    TA.size = rnd_to(40) + 1;
    TA.sel = (int)rnd(), TA.ext = rnd_to(2) ? (void *)(intptr_t)rnd() : NULL;
    for (int i = 0; i < 4; i++) TA.color[i][i % 3] = (int)rnd();
    TB = TA;
}
static void twin_normal(struct game_dialog *c, const struct game_dialog *d) {
    *c = *d;
    c->data = (void *)(intptr_t)(d->data != NULL);
    c->cursor = (void *)(intptr_t)(d->cursor == d->data);
    if (d->list.buf == (const void *)&d->data_flag) c->list.buf = (void *)1;
}
static void twin_compare(struct hook_stats *st, const char *name, int ra, int rb) {
    struct game_dialog a, b;
    twin_normal(&a, &TA), twin_normal(&b, &TB);
    if (ra != rb || memcmp(&a, &b, sizeof a)) {
        const uint8_t *pa = (const uint8_t *)&a, *pb = (const uint8_t *)&b;
        int at = -1;
        for (size_t i = 0; i < sizeof a; i++)
            if (pa[i] != pb[i]) {
                at = (int)i;
                break;
            }
        hooks_mismatch(st, "(twin %s) = %d, original %d; first difference at 0x%x", name, rb, ra, at);
    }
    if (TA.data) game_dialog_data_close(&TA.data_flag);
    if (TB.data) game_dialog_data_close(&TB.data_flag);
}
// `ocall` runs the original and `ncall` ours, with D the Dialog.
#define TWIN(name, ocall, ncall)                    \
    do {                                            \
        twin_reset();                               \
        struct game_dialog *D = &TA;                \
        int ra_ = (ocall);                          \
        D = &TB;                                    \
        int rb_ = (ncall);                          \
        twin_compare(&stats_##name, #name, ra_, rb_); \
    } while (0)

static void random_text(char *buf, int cap) {
    int n = rnd_to(cap - 4);
    int mode = rnd_to(4);
    for (int i = 0; i < n; i++) {
        int r = rnd_to(10);
        buf[i] = mode == 0 ? ' ' : r < 4 ? ' ' : r < 5 && mode != 1 ? '\n' : (char)('a' + rnd_to(26));
    }
    buf[n] = 0;
}

// A line for parse_string: words, directives and choices, put together at random. (No choice is left
// without its grammar: a character below '0' after "$<" would pass as a negative variable and take its gender
// from the memory before the table, which can be any number, and the words are then looked for that many
// colons along, off the end of the line.) A "$" word that is not a
// $NUM, $NUMBER or $HEX takes the first digit anywhere after it, and the original, given none, never
// comes back; so a line with such a word that has no digit of its own ends in one. A choice made with '#'
// has no gender of its own (the original reads a stack slot it never set), so it has one or four colons.
static void random_line(char *buf, size_t cap) {
    static const char *const words[] = {" de ", " le ", " la ", "De ", "Le ", "La ", "ile ", "Hotel ", "hotel ", "yak ", "x", ".",
                                        "base of ", " ", "a:b", ">", "d'", "", "Un ", "de homme "};
    static const char *const fixed[] = {"$$", "$LINK<abc>", "$LINK<", "$<M:he:she>", "$<F:le:la>", "$<N:a:b:c>",
                                        "$<M1:x:y>", "$<M2:x:y>", "$< F 3 :a:b:c>", "$<M 3>", "$<F 4>", "$<Q:a:b>",
                                        "$<m:a:b:c:d>", "$<f1:a:b:c:d:e:f>", "$<n:u:v:w:x:y:z:o>", "$<M:a:b:c>",
                                        "$<M:no end", "$< :a:b>", "$<#1:one:many>", "$<5:a:b>", "$<M2: :>", "$<M:::>"};
    static const char *const risky[] = {"$NAME", "$HEXAGON", "$NUMERIC", "$X", "$ ", "$."};
    size_t n = 0;
    int need_digit = 0;
    buf[0] = 0;
    int parts = 1 + rnd_to(8);
    for (int i = 0; i < parts && n + 64 < cap; i++) {
        char piece[48];
        int r = rnd_to(24);
        if (r < 6) snprintf(piece, sizeof piece, "%s", words[rnd_to(sizeof words / sizeof *words)]);
        else if (r < 9) snprintf(piece, sizeof piece, "$NUM%d", rnd_to(10));
        else if (r < 11) snprintf(piece, sizeof piece, "$NUMBER%d", rnd_to(10));
        else if (r < 12) snprintf(piece, sizeof piece, "$HEX%d", rnd_to(10));
        else if (r < 13) snprintf(piece, sizeof piece, "$%s%d", rnd_to(2) ? "NAME" : "CITY LABEL ", rnd_to(10));
        else if (r < 14) snprintf(piece, sizeof piece, "$<%d:%s>", rnd_to(10), rnd_to(2) ? "a:b" : "a:b:c:d:e:f");
        else if (r < 15) snprintf(piece, sizeof piece, "$<#%d:one:many>", rnd_to(10));
        else if (r < 16) snprintf(piece, sizeof piece, "$<#%d:s:p:q:r:t>", rnd_to(10));
        else if (r < 17) snprintf(piece, sizeof piece, "$<%d>", rnd_to(10));
        else if (r < 18) snprintf(piece, sizeof piece, "$<M %d>", rnd_to(10));
        else if (r < 19) snprintf(piece, sizeof piece, "$<%c%d:p:q%s>", "MFN"[rnd_to(3)], rnd_to(10), rnd_to(2) ? ":r" : "");
        else if (r < 20) snprintf(piece, sizeof piece, "$%s", rnd_to(2) ? "NUM " : "HEX-");
        else if (r < 21) {
            snprintf(piece, sizeof piece, "%s", risky[rnd_to(sizeof risky / sizeof *risky)]);
            need_digit = 1;
        } else snprintf(piece, sizeof piece, "%s", fixed[rnd_to(sizeof fixed / sizeof *fixed)]);
        size_t l = strlen(piece);
        memcpy(buf + n, piece, l + 1);
        n += l;
    }
    if (need_digit || !rnd_to(4)) snprintf(buf + n, cap - n, "%s%d", rnd_to(2) ? " " : "", rnd_to(10));
}


// ---- Sweep of the popup's layout and drawing ------------------------------------------------------
//
// A scratch popup in static memory laid out as the game's (the table of its widget's virtual bases at 0x21d0 gives the Win at 0x188 and
// the Dialog at 0xba0 into it), a window it may be drawn in, a parent it may take its background from, sprites, a list of lines of text of
// every kind, and a real Buffer with a device context and a font (the button sweep's), so the text is really measured and drawn. The
// window's virtual functions log, and the function a screen may set logs what it is given and what the global that names the popup
// holds. The widget's layout is the logged stand-in above. Each round the numbers the layout reads are made at random, the layout is
// checked, and the drawing on what it made.
enum { PXL = 6 };
_Static_assert(sizeof(struct game_win) == 0x444, "a window ends where its Buffer starts");
static struct px {
    uint8_t obj[0x3230] __attribute__((aligned(16)));
    struct {
        struct game_win win;
        struct game_buffer buf;
    } host, parent;
    int32_t vbt[4];
    struct game_dlist_node lnodes[PXL], snodes[PXL];
    struct {
        void *vt;
        const char *text;
        int32_t kind;
    } litems[PXL];
    void *sitems[PXL];
    struct game_sprite spr[3];
    uint8_t sdata[3][1024], pix[72 * 48 + 16];
    struct font bfont;
} PX;
static void *px_vt[0x200 / 4];
static int THISCALL px_s174(void *o) { return vrec(0x174, o, 0, 0, 0, 0), 0; }
static int THISCALL px_s180(void *o) { return vrec(0x180, o, 0, 0, 0, 0), vlog.n & 1; }
static int THISCALL px_sf4(void *o) { return vrec(0xf4, o, 0, 0, 0, 0), (vlog.n & 2) != 0; }
static int px_handler(void *pic, void *host, int id, int x, int y) {
    return vrec(0x300, pic, (int)(intptr_t)host, id, x * 1000 + y, G_(BPG_DRAWING) == (int32_t)(intptr_t)PX.obj), 0;
}
static const char *const PX_WORDS[] = {"Ok", "Alpha beta", "A line with enough words in it that it has to wrap in the width", "", "x",
                                       "WWWW MMMM iiii", "Wide wide wide wide wide wide wide wide wide wide"};

static void px_hand_buffer(struct game_buffer *b, uint8_t *pixels, int w, int h) {
    memset(b, 0, sizeof *b);
    for (int i = 0; i < w * h + 8; i++) pixels[i] = (uint8_t)rnd();
    b->bits = pixels;
    b->width = w, b->neg_height = -h, b->pitch = w;
    b->full[2] = w, b->full[3] = h;
    b->clip[2] = w, b->clip[3] = h;
}
static void px_sprite(struct game_sprite *s, uint8_t *data) {
    memset(s, 0, sizeof *s);
    int w = 1 + rnd_to(18), h = 1 + rnd_to(14), key = rnd_to(8);
    for (int i = 0; i < 1024; i++) {
        int c = rnd_to(6);
        data[i] = c == 0 ? (uint8_t)key : c == 1 ? 0 : (uint8_t)(1 + rnd_to(7));
    }
    s->data = data, s->key = (uint8_t)key;
    s->width = w, s->stride = w, s->height = h, s->full_w = w + rnd_to(6), s->full_h = h + rnd_to(6);
}

// The numbers the layout reads, at random (`font` is the real Font, `big` the real Buffer).
static void px_numbers(uint8_t *o, struct font *font) {
    // (Not the list box's kind, 2: selecting in a list box moves its scroll bar and calls the function a screen set for it; those are
    // checked on list boxes of their own, in scroll.c.)
    int32_t kinds[] = {-1, 0, 1, 3, 4, 5, 8, 9, 12, 20};
    I_(o, 0x20f4) = rnd_to(4);
    I_(o, 0x21b0) = rnd_to(12), I_(o, 0x21b4) = rnd_to(12), I_(o, 0x21b8) = rnd_to(12), I_(o, 0x21bc) = rnd_to(12);
    I_(o, 0x21c0) = rnd_to(12), I_(o, 0x21c4) = rnd_to(12), I_(o, 0x21c8) = rnd_to(6), I_(o, 0x21cc) = rnd_to(2) ? -1 : rnd_to(100);
    I_(o, 0x2350) = kinds[rnd_to(10)];
    // (The text is laid out in what the widget and the picture leave, which the original would never come to the end of if it were
    // nothing: the widget is wide.)
    D_(o + 0x2d70, 0x2c) = 150 + rnd_to(100), D_(o + 0x2d70, 0x30) = 20 + rnd_to(100);
    I_(o, 0xa14) = rnd_to(3) == 0 ? 1 : 0;
    I_(o, 0xa18) = rnd_to(3) ? rnd_to(40) : 0x2000, I_(o, 0xa1c) = rnd_to(3) ? rnd_to(40) : 0x2000;
    I_(o, 0xa20) = 200 + rnd_to(200), I_(o, 0xa24) = 40 + rnd_to(120);
    I_(o, 0xa44) = rnd_to(2) ? -1 : rnd_to(5);
    I_(o, 0x30a8) = (rnd_to(2) ? 0x2 : 0) | (rnd_to(2) ? 0x40 : 0) | (rnd_to(2) ? 0x100 : 0) | (rnd_to(2) ? 0x400 : 0) |
                    (rnd_to(4) == 0 ? 0x10000 : 0);
    I_(o, 0x30b4) = rnd_to(30), I_(o, 0x30b8) = rnd_to(30);
    I_(o, 0x30f8) = rnd_to(50), I_(o, 0x30fc) = rnd_to(50);
    I_(o, 0x3104) = 1 + rnd_to(2), I_(o, 0x3108) = 1 + rnd_to(2), I_(o, 0x310c) = rnd_to(256);
    for (int i = 0x3120; i < 0x3160; i += 4) I_(o, i) = rnd_to(256);
    I_(o, 0x3160) = (int32_t)rnd(), I_(o, 0x3164) = (int32_t)rnd();
    for (int i = 0x3110; i <= 0x311c; i += 4) PTR_(o, i) = font;
    PTR_(o, 0x3168) = rnd_to(2) ? (void *)px_handler : NULL;
    PX.bfont.height = 6 + rnd_to(14);
    // What the layout makes and the drawing reads.
    I_(o, 0x30bc) = 150 + rnd_to(100), I_(o, 0x30c0) = 20 + rnd_to(80), I_(o, 0x30c4) = rnd_to(20), I_(o, 0x30cc) = rnd_to(40);
    I_(o, 0x30d0) = rnd_to(20), I_(o, 0x30d4) = rnd_to(3) ? 5 + rnd_to(40) : 0, I_(o, 0x30d8) = rnd_to(3) ? rnd_to(30) : 0;
    I_(o, 0x30dc) = rnd_to(30), I_(o, 0x30e0) = rnd_to(8), I_(o, 0x30e4) = rnd_to(8), I_(o, 0x30e8) = rnd_to(8);
    I_(o, 0x30ec) = rnd_to(8), I_(o, 0x30f0) = rnd_to(8), I_(o, 0x30f4) = rnd_to(8), I_(o, 0x21c8) = rnd_to(6);
}

static void px_build(struct game_buffer *big, struct font *font) {
    memset(&PX, 0, sizeof PX);
    uint8_t *o = PX.obj;
    PTR_(o, 0) = px_vt;
    PX.vbt[1] = 0x188, PX.vbt[2] = 0xba0;
    PTR_(o, 0x21d0) = PX.vbt;
    PTR_(o, 0x21d0 + 0x188) = px_vt;  // (the widget's own Win)
    PX.host.win.vtable = px_vt, PX.parent.win.vtable = px_vt;
    for (int i = 0; i < 3; i++) px_sprite(&PX.spr[i], PX.sdata[i]);
    px_hand_buffer(&PX.parent.buf, PX.pix, 72, 48);
    memcpy(o + 0x444, big, sizeof *big);
    memcpy(&PX.host.buf, big, sizeof *big);
    PTR_(o, 0xa3c) = rnd_to(2) ? (void *)o : (void *)&PX.host;
    struct game_win *w = (struct game_win *)o;
    w->parent = rnd_to(2) ? &PX.parent.win : NULL;
    w->flags = rnd_to(2) ? 0x80000 : 0;
    for (int i = 0; i < 4; i++) w->inner[i] = rnd_to(20) - 5, w->outer[i] = rnd_to(20) - 5;
    w->flags2 = rnd_to(2) ? 0x1 : 0;
    PTR_(o, 0x2144) = rnd_to(2) ? &PX.spr[0] : NULL;
    PTR_(o, 0x2148) = rnd_to(2) ? &PX.spr[1] : NULL;
    PTR_(o, 0x214c) = rnd_to(2) ? &PX.spr[2] : NULL;
    PTR_(o, 0x316c) = &PX.bfont;
    // The lines: a ring of nodes holding items of text, kind and next.
    int n = rnd_to(PXL + 1);
    struct game_dlist *l = (struct game_dlist *)(o + 0x2180);
    for (int i = 0; i < n; i++) {
        PX.litems[i].text = PX_WORDS[rnd_to(7)], PX.litems[i].kind = rnd_to(4);
        PX.lnodes[i].item = &PX.litems[i], PX.lnodes[i].key = 20 + i;
        PX.lnodes[i].next = &PX.lnodes[(i + 1) % n], PX.lnodes[i].prev = &PX.lnodes[(i + n - 1) % n];
    }
    l->head = n ? PX.lnodes : NULL, l->cur = n ? &PX.lnodes[rnd_to(n)] : NULL, l->count = n, l->idx = n ? rnd_to(n) : 0;
    // The SpriteBox's list (the picture's id is its first entry's).
    random_list((struct game_dlist *)(o + 0x2264), PX.snodes, PX.sitems);
    px_numbers(o, font);
}

static void px_sweep(struct game_buffer *big, struct font *font) {
    int32_t screen = *game_win_screen_w, g_w = G_(BPG_WIDTH), g_h = G_(BPG_HEIGHT), g_d = G_(BPG_DRAWING);
    const uint8_t *tint = *game_win_tint;
    *game_win_tint = NULL;
    memset(px_vt, 0, sizeof px_vt);
    px_vt[0x174 / 4] = (void *)px_s174, px_vt[0x180 / 4] = (void *)px_s180, px_vt[0xf4 / 4] = (void *)px_sf4;
    for (int round = 0; round < 30; round++) {
        px_build(big, font);
        struct game_base_pop *p = (struct game_base_pop *)PX.obj;
        static const int widths[] = {640, 800, 1024, 1280};
        for (int k = 0; k < 4; k++) {
            *game_win_screen_w = widths[rnd_to(4)];
            // (A check leaves the state as it found it in a sweep, and what the stand-ins log is put back too: so the stand-ins' count is
            // started from a number of its own. What the layout makes is not there for the drawing to work from: px_numbers gives the
            // drawing sizes of its own.)
            px_logging = 1;
            vlog.n = (int)rnd();
            check_pop_calculate_dimensions(p);
            px_logging = 0;
            vlog.n = (int)rnd();
            buffer_clear_links_held = 1;
            check_pop_on_redraw(p);
            buffer_clear_links_held = 0;
            // Something else next time: where it is drawn, the lines, the flags.
            memcpy(PX.obj + 0x444, big, sizeof *big);
            if (rnd_to(2)) memcpy(&PX.host.buf, big, sizeof *big);
            px_numbers(PX.obj, font);
            PTR_(PX.obj, 0xa3c) = rnd_to(2) ? (void *)PX.obj : (void *)&PX.host;
            struct game_dlist *l = (struct game_dlist *)(PX.obj + 0x2180);
            if (l->count) l->cur = &PX.lnodes[rnd_to(l->count)], l->idx = rnd_to(l->count);
            for (int i = 0; i < l->count; i++) PX.litems[i].kind = rnd_to(4);
        }
    }
    *game_win_screen_w = screen, G_(BPG_WIDTH) = g_w, G_(BPG_HEIGHT) = g_h, G_(BPG_DRAWING) = g_d;
    *game_win_tint = tint;
}

static void life_sweep_dialog(void);
static void life_sweep_pop(void);
static void life_sweep_sprite_box(void);
static void life_sweep_dialog_scalar(void);
static void dialog_sweep(void) {
    static struct font f_ok, f_bad;
    int sweeping = hooks_sweeping;
    hooks_sweeping = 1;
    rng_state = 0x3a91c5d7u ^ (uint32_t)*game_turn * 2654435761u;
    if (!rng_state) rng_state = 1;
    fill_stub_vt();
    f_ok.hfont = (void *)0x1234, f_bad.hfont = NULL;
    // What the sweep sets and puts back.
    struct game_dialog_defs_a da = *game_dialog_defs_a;
    struct game_dialog_defs_b db = *game_dialog_defs_b;
    struct game_pop_defs pd = *game_pop_defs;
    struct game_pop_fonts pf = *game_pop_fonts;
    int32_t ext_a[3], ext_b[2], open = *game_pop_open, fallout = *game_pop_fallout_on, screen_w = *game_win_screen_w;
    memcpy(ext_a, game_pop_ext_a, sizeof ext_a), memcpy(ext_b, game_pop_ext_b, sizeof ext_b);
    char file[16];
    memcpy(file, game_pop_file, 13);
    uint32_t hwnd = *game_win_hwnd;
    struct game_win *focus = *game_win_modal_focus;
    int32_t pnums[10], pgenders[10], pplurals[10], language = *game_language;
    char ptext[2560];
    memcpy(pnums, game_parse_nums, sizeof pnums), memcpy(pgenders, game_parse_genders, sizeof pgenders);
    memcpy(pplurals, game_parse_plurals, sizeof pplurals), memcpy(ptext, game_parse_text, sizeof ptext);
    *game_win_hwnd = 1;
    for (int round = 0; round < 30; round++) {
        build();
        // The text.
        char buf[72];
        for (int j = 0; j < 6; j++) {
            random_text(buf, sizeof buf), check_purge_leading(buf);
            random_text(buf, sizeof buf), check_purge_trailing(buf);
            random_text(buf, sizeof buf), check_purge_spaces(buf);
            random_text(buf, sizeof buf), check_kill_lf(buf);
            random_text(buf, sizeof buf), check_add_lf(buf);
        }
        strcpy(buf, ""), check_purge_spaces(buf), check_purge_trailing(buf), check_kill_lf(buf);
        strcpy(buf, "   "), check_purge_spaces(buf);
        strcpy(buf, " a "), check_purge_trailing(buf), check_purge_leading(buf);
        // The popup entry points, with random arguments (a string where the entry point takes one).
        {
            static const char *const strs[] = {NULL, "", "ab", "a longer popup label, for its length"};
#define R() (rnd_to(4) ? (int)rnd_to(7) - 2 : (int)rnd())
#define STR() ((int)(intptr_t)strs[rnd_to(4)])
            for (int j = 0; j < 3; j++) {
                check_popup(R(), R());
                check_popup_2(R(), R(), R());
                check_popup_3(R(), R(), STR(), R(), R());
                check_popup_4(R(), R(), R(), R());
                check_popup_5(R(), R(), R(), R(), R());
                check_popup_6(R(), R(), R());
                check_popup_7(R(), R(), R(), R());
                check_popup_8(R(), R(), R(), R(), R());
                check_popup_9(R(), R(), R(), R(), R(), R());
                check_popups(R(), R(), R());
                check_popups_2(R(), R(), R(), R(), R());
                check_popups_3(R(), R(), R(), R());
                check_popups_4(R(), R(), R(), R(), R(), R());
                check_popups_5(R(), R(), R(), R(), R());
                check_popups_6(R(), R(), R(), R(), R(), R(), R());
                check_popups_7(R(), R(), STR(), R(), R(), R());
                check_popups_8(R(), R(), STR(), R(), R(), R(), R(), R());
                check_popups_9(R(), R(), R(), R(), R(), R());
                check_popups_10(R(), R(), R(), R(), R(), R(), R(), R());
                check_popups_11(R(), R(), R(), R());
                check_popups_12(R(), R(), R(), R(), R(), R());
                check_popups_13(R(), R(), R(), R(), R());
                check_popups_14(R(), R(), R(), R(), R(), R(), R());
                check_popups_15(R(), R(), R(), R(), R(), R());
                check_popups_16(R(), R(), R(), R(), R(), R(), R(), R());
                check_popups_17(R(), R(), R(), R(), R(), R(), R());
                check_popup_ask(R(), R(), R(), R(), R());
                check_popup_ask_2(R(), R(), R(), R(), R());
                check_popup_ask_3(R(), R(), R(), R());
                check_popup_ask_4(R(), R(), STR(), R(), R());
                check_popup_ask_5(R(), STR(), R(), R());
            }
#undef R
#undef STR
        }
        // The text variables: numbers, grammar and strings set at random (and the game's own put back below).
        {
            static const char *const slot_text[] = {"",     "he:she",       "he:she:it",    "a:b:c:d",   "a:b:c:d:e:f", "plain",
                                                    "x:y",  "la:le:les:l'", "an apple",     ":",         "::",          "p:q:r:s:t"};
            for (int j = 0; j < 10; j++) {
                game_parse_nums[j] = rnd_to(3) ? rnd_to(5) : (int32_t)rnd() >> rnd_to(30);
                game_parse_genders[j] = rnd_to(3), game_parse_plurals[j] = rnd_to(3) ? rnd_to(2) : (int)rnd_to(1000) + 2;
                char *slot = game_parse_text + j * 256;
                if (rnd_to(8) == 0) {  // a long one, to the end of the variable's room
                    memset(slot, 'w', 255), slot[255] = 0;
                    if (rnd_to(2)) slot[rnd_to(255)] = ':';
                } else strcpy(slot, slot_text[rnd_to(sizeof slot_text / sizeof *slot_text)]);
            }
            static char line[600], out[0x4000];
            for (int j = 0; j < 14; j++) {
                *game_language = rnd_to(3);
                random_line(line, sizeof line);
                static const char *const before[] = {"", "", "line", "x", "a longer text, to be added to"};
                strcpy(out, before[rnd_to(sizeof before / sizeof *before)]);
                check_parse_string(line, out);
                check_findnum(line);
            }
            *game_language = rnd_to(2);
            // Text at the end of a variable, the end of the line, and nothing at all.
            out[0] = 0;
            strcpy(line, "$NUM1"), check_parse_string(line, out);
            strcpy(line, "$NUMx"), check_parse_string(line, out);
            strcpy(line, "$NUMBERq1 $NUM2"), check_parse_string(line, out);
            strcpy(line, "a $HEXk"), check_parse_string(line, out);
            strcpy(line, "$$$$"), check_parse_string(line, out);
            strcpy(out, "old"), strcpy(line, "text"), check_parse_string(line, out);
            strcpy(out, "old"), strcpy(line, "$X text"), check_parse_string(line, out);
            strcpy(out, "old"), strcpy(line, "a $NUM1 b"), check_parse_string(line, out);
            out[0] = 0;
            strcpy(line, ""), check_parse_string(line, out), check_findnum(line);
            check_parse_string(NULL, out), check_parse_string(line, NULL), check_findnum(NULL);
        }
        // The script text: numbers in text, the items of a line, and the labels of an index.
        {
            static const char *const items[] = {"12", "-7", "0x1F", "0X7fFf", "0b101", "0B1102", "0d77", "0D5x", "007", "0", "00", "0xg",
                                                "0b", "0x", "+3", " 42 ", "abc", "", "  ", "0Xzz", "0xFFFFFFFF", "0b11111111111111111111111111111111",
                                                "0xdead", "0BEEF", "0Xface", "  0x10", "0d", "x", "9z", "0xab cd"};
            static char line[400], item[600], file[0x160], index[400];
            char *saved_cursor = *game_text_cursor, *saved_buf = *game_text_item_buf;
            *game_text_item_buf = item;
            for (int j = 0; j < 6; j++) {
                size_t n = 0;
                line[0] = 0;
                for (int k = 0, parts = 1 + rnd_to(6); k < parts; k++)
                    n += (size_t)snprintf(line + n, sizeof line - n, "%s%s", k ? (rnd_to(6) ? "," : ", ") : "", items[rnd_to(sizeof items / sizeof *items)]);
                for (int k = 0; k < 4; k++) {
                    *game_text_cursor = line + rnd_to((int)n + 1);
                    check_text_item(), check_text_item_number(), check_text_item_binary(), check_text_item_hex();
                    check_stoi(*game_text_cursor), check_btoi(*game_text_cursor), check_htoi(*game_text_cursor);
                    *(char **)(file + 0x150) = *game_text_cursor, *(char **)(file + 0x15c) = item;
                    check_text_obj_item(file), check_text_obj_item_number(file);
                    check_text_obj_item_binary(file), check_text_obj_item_hex(file);
                }
            }
            for (size_t k = 0; k < sizeof items / sizeof *items; k++) check_stoi(items[k]), check_btoi(items[k]), check_htoi(items[k]);
            *game_text_cursor = saved_cursor, *game_text_item_buf = saved_buf;
            // The index: a file name at 0, the number of labels at 0x100 and a pointer to them at 0x108.
            static const char *const labels[] = {"ALPHA", "Beta", "gamma", "DELTA", "x", "", "epsilon zeta"};
            static const char *const names[] = {"script", "SCRIPT.TXT", "script.txt", "other", "Script.Bin", "", "s.txt", "a.b.txt"};
            static const char *const wanted[] = {"#alpha", "BETA", "#Gamma", "#delta", "X", "#", "epsilon ZETA", "zzz", "#zeta", "alph", "ALPHAS", ""};
            for (int j = 0; j < 8; j++) {
                memset(file, 0, sizeof file);
                int count = 1 + rnd_to((int)(sizeof labels / sizeof *labels));
                size_t at = 0;
                for (int k = 0; k < count; k++) {
                    *(int32_t *)(index + at) = 100 * (k + 1) + rnd_to(50), at += 4;
                    size_t l = strlen(labels[k]) + 1;
                    memcpy(index + at, labels[k], l), at += l;
                }
                strcpy(file, rnd_to(2) ? "script.txt" : names[rnd_to(sizeof names / sizeof *names)]);
                *(int32_t *)(file + 0x100) = count, *(char **)(file + 0x108) = index;
                for (int k = 0; k < 5; k++)
                    check_text_index_search(file, names[rnd_to(sizeof names / sizeof *names)], wanted[rnd_to(sizeof wanted / sizeof *wanted)]);
            }
            // The game's own four indexes.
            uint8_t saved_slots[4 * 0x118];
            memcpy(saved_slots, game_text_slots, sizeof saved_slots);
            for (int j = 0; j < 3; j++) {
                memset(game_text_slots, 0, sizeof saved_slots);
                for (int s = 0; s < 4; s++) {
                    uint8_t *slot = game_text_slots + s * 0x118;
                    if (rnd_to(3) == 0) continue;
                    strcpy((char *)slot, names[rnd_to(sizeof names / sizeof *names)]);
                    if (!strchr((char *)slot, '.')) strcat((char *)slot, ".txt");
                    size_t at2 = 0;
                    static char idx[4][200];
                    int count = 1 + rnd_to(4);
                    for (int k = 0; k < count; k++) {
                        *(int32_t *)(idx[s] + at2) = 1000 * s + k, at2 += 4;
                        const char *l = labels[rnd_to(5)];
                        memcpy(idx[s] + at2, l, strlen(l) + 1), at2 += strlen(l) + 1;
                    }
                    *(int32_t *)(slot + 0x100) = count, *(char **)(slot + 0x108) = idx[s];
                }
                for (int k = 0; k < 6; k++)
                    check_text_search_index(names[rnd_to(sizeof names / sizeof *names)], wanted[rnd_to(sizeof wanted / sizeof *wanted)]);
            }
            memcpy(game_text_slots, saved_slots, sizeof saved_slots);
        }
        // The popups.
        for (int k = 0; k < NB; k++) {
            struct game_base_pop *p = &S.pop[k];
            int screen[4] = {640, 1023, 1024, 1280};
            *game_win_screen_w = screen[rnd_to(4)];
            check_pop_set_height(p, rnd_to(300) - 20);
            p->ext = rnd_to(3) ? NULL : &S.ext, p->flags ^= 0x400;
            check_pop_set_height(p, rnd_to(300) - 20);
            check_pop_set_loc(p, rnd_to(3) ? rnd_to(500) - 50 : 0x2000, rnd_to(3) ? rnd_to(500) - 50 : 0x2000);
            check_pop_read_check(p);
            check_pop_on_key_click(p, (int)rnd(), (int)rnd()), check_pop_on_key_up(p, (int)rnd());
            for (int j = 0; j < 3; j++) {
                p->def_button = rnd_to(6) - 2, p->flags ^= (uint32_t)rnd_to(2) << 14;
                check_pop_on_key_down(p, rnd_to(200));
            }
            for (int j = 0; j < 4; j++) {
                *game_win_modal_focus = rnd_to(5) ? (struct game_win *)p : NULL;
                p->def_button = rnd_to(6) - 2, p->flags ^= (uint32_t)rnd_to(2) << 14 | (uint32_t)rnd_to(2) << 8;
                p->ext = rnd_to(3) ? NULL : &S.ext;
                check_pop_double_click();
            }
            for (int j = 0; j < 4; j++) {
                int x = rnd_to(60), y = rnd_to(60);
                check_pop_on_left_down(p, x, y);
                if (p->spots.count) {
                    const int32_t *r = p->spots.rects[rnd_to(p->spots.count)].rect;
                    check_pop_on_left_down(p, r[0], r[1]);
                }
            }
            for (int j = 0; j < 3; j++)
                check_pop_set_external_window(p, rnd_to(3) ? &S.ext : NULL, rnd_to(2) ? 0x2000 : rnd_to(400), rnd_to(2) ? 0x2000 : rnd_to(400),
                                              rnd_to(500), rnd_to(500));
            struct font *fonts[3] = {NULL, &f_ok, &f_bad};
            check_pop_set_string_font(p, fonts[rnd_to(3)], (int)rnd(), (int)rnd(), (int)rnd());
            check_pop_set_button_font(p, fonts[rnd_to(3)], (int)rnd(), (int)rnd());
            check_pop_set_string_color(p, (int)rnd(), (int)rnd(), (int)rnd(), (int)rnd());
            check_pop_set_string_color2(p, (int)rnd(), (int)rnd(), (int)rnd(), (int)rnd());
            check_pop_set_string_color3(p, (int)rnd(), (int)rnd(), (int)rnd(), (int)rnd());
            check_pop_set_string_color_hyper(p, (int)rnd(), (int)rnd(), (int)rnd(), (int)rnd());
            check_pop_set_button_color(p, (int)rnd(), (int)rnd(), (int)rnd(), (int)rnd());
            check_pop_set_button_color2(p, (int)rnd(), (int)rnd(), (int)rnd(), (int)rnd());
            check_pop_set_button_color3(p, (int)rnd(), (int)rnd(), (int)rnd(), (int)rnd());
            check_pop_unk3(p, rnd_to(3)), check_pop_unk4(p, rnd_to(3));
        }
        check_pop_set_def_external_window((int)rnd(), (int)rnd(), (int)rnd(), (int)rnd(), (int)rnd());
        struct font *fonts[3] = {NULL, &f_ok, &f_bad};
        check_pop_set_def_string_font(fonts[rnd_to(3)], (int)rnd(), (int)rnd(), (int)rnd());
        check_pop_set_def_button_font(fonts[rnd_to(3)], (int)rnd(), (int)rnd());
        check_pop_set_def_string_color((int)rnd(), (int)rnd(), (int)rnd(), (int)rnd());
        check_pop_set_def_string_color2((int)rnd(), (int)rnd(), (int)rnd(), (int)rnd());
        check_pop_set_def_string_color3((int)rnd(), (int)rnd(), (int)rnd(), (int)rnd());
        check_pop_set_def_string_color_hyper((int)rnd(), (int)rnd(), (int)rnd(), (int)rnd());
        check_pop_set_def_button_color((int)rnd(), (int)rnd(), (int)rnd(), (int)rnd());
        check_pop_set_def_button_color2((int)rnd(), (int)rnd(), (int)rnd(), (int)rnd());
        check_pop_set_def_button_color3((int)rnd(), (int)rnd(), (int)rnd(), (int)rnd());
        {
            char name[20];
            int n = rnd_to(16);
            for (int i = 0; i < n; i++) name[i] = (char)('a' + rnd_to(26));
            name[n] = 0;
            check_pop_set_def_file(name);
            check_pop_set_def_file(NULL);
            check_pop_set_def_file("");
            check_pop_set_def_file("twelve_chars");
        }
        for (int j = 0; j < 4; j++) {
            *game_pop_open = rnd_to(3) ? 0 : (int)rnd(), *game_pop_fallout_on = rnd_to(3) ? 0 : (int)rnd();
            check_pop_on_modal();
            check_pop_fallout();
        }
        // The Dialogs.
        for (int k = 0; k < ND; k++) {
            struct game_dialog *d = &S.dlg[k];
            int32_t r[4] = {rnd_to(50), rnd_to(50), 0, 0};
            r[2] = r[0] + rnd_to(100), r[3] = r[1] + rnd_to(100);
            random_dialog(d, S.nodes[k], S.items[k]), check_dialog_close(d);
            random_dialog(d, S.nodes[k], S.items[k]), check_dialog_init(d);
            random_dialog(d, S.nodes[k], S.items[k]), check_dialog_init_2(d, rnd_to(200) - 10, rnd_to(200), rnd_to(300), rnd_to(300), 0);
            random_dialog(d, S.nodes[k], S.items[k]), check_dialog_init_3(d, r, 0);
            random_dialog(d, S.nodes[k], S.items[k]), check_dialog_init_3(d, NULL, 0);
            random_dialog(d, S.nodes[k], S.items[k]), check_dialog_init_4(d, 0);
            random_dialog(d, S.nodes[k], S.items[k]), check_dialog_init_5(d, rnd_to(200), rnd_to(200), rnd_to(300), rnd_to(300), rnd_to(2) ? d : NULL);
            random_dialog(d, S.nodes[k], S.items[k]), check_dialog_init_6(d, r, rnd_to(2) ? d : (void *)&S);
            random_dialog(d, S.nodes[k], S.items[k]), check_dialog_init_6(d, NULL, NULL);
            random_dialog(d, S.nodes[k], S.items[k]), check_dialog_init_7(d, rnd_to(2) ? d : NULL);
            random_dialog(d, S.nodes[k], S.items[k]), check_dialog_unk1(d, (int)rnd(), (int)rnd());
            random_dialog(d, S.nodes[k], S.items[k]);
            check_dialog_set_font(d, fonts[rnd_to(3)], (int)rnd(), (int)rnd());
            check_dialog_set_text_color(d, (int)rnd(), (int)rnd(), (int)rnd(), (int)rnd());
            check_dialog_set_text_color2(d, (int)rnd(), (int)rnd(), (int)rnd(), (int)rnd());
            check_dialog_set_text_color3(d, (int)rnd(), (int)rnd(), (int)rnd(), (int)rnd());
        }
        check_dialog_set_def_font(fonts[rnd_to(3)], (int)rnd(), (int)rnd());
        for (int j = 0; j < 8; j++)
            check_dialog_set_def_text_color((int)rnd(), (int)rnd(), (int)rnd(), (int)rnd(), j < 6 ? j - 1 : rnd_to(8) - 2);
        // The SpriteBoxes.
        for (int k = 0; k < NS; k++) {
            struct game_sprite_box *sb = &S.box[k].sb;
            void *w = (uint8_t *)sb + 0x8c;
            sprite_fn_answer = rnd_to(2);
            for (int j = 0; j < 3; j++) {
                check_sprite_box_id_to_pos(sb, rnd_to(8) - 2);
                check_sprite_box_pos_to_id(sb, rnd_to(2 * NN + 6) - NN - 3);
                int x = rnd_to(60), y = rnd_to(60);
                if (sb->spot.count) {
                    const int32_t *r = sb->spot.rects[rnd_to(sb->spot.count)].rect;
                    x = rnd_to(2) ? r[0] : x, y = rnd_to(2) ? r[1] : y;
                }
                check_sprite_box_on_left_down(w, x, y), check_sprite_box_on_left_up(w, x, y);
                check_sprite_box_on_right_click(w, x, y), check_sprite_box_on_right_down(w, x, y);
                check_sprite_box_on_right_up(w, x, y), check_sprite_box_on_left_double_click(w, x, y);
                check_sprite_box_on_right_double_click(w, x, y);
                sprite_fn_answer = !sprite_fn_answer;
            }
            check_sprite_box_on_dialog_focus(w, 0);
        }
        // The twins (with a size, so the data is made).
        {
            int32_t r[4] = {rnd_to(50), rnd_to(50), 0, 0};
            r[2] = r[0] + rnd_to(100), r[3] = r[1] + rnd_to(100);
            TWIN(dialog_init, orig_dialog_init(D), dialog_init(D));
            TWIN(dialog_init_2, orig_dialog_init_2(D, 5, 6, 70, 80, 33), dialog_init_2(D, 5, 6, 70, 80, 33));
            TWIN(dialog_init_3, orig_dialog_init_3(D, r, 17), dialog_init_3(D, r, 17));
            TWIN(dialog_init_4, orig_dialog_init_4(D, 21), dialog_init_4(D, 21));
        }
    }
    memcpy(game_parse_nums, pnums, sizeof pnums), memcpy(game_parse_genders, pgenders, sizeof pgenders);
    memcpy(game_parse_plurals, pplurals, sizeof pplurals), memcpy(game_parse_text, ptext, sizeof ptext);
    *game_language = language;
    *game_win_hwnd = hwnd, *game_win_modal_focus = focus;
    *game_dialog_defs_a = da, *game_dialog_defs_b = db, *game_pop_defs = pd, *game_pop_fonts = pf;
    memcpy(game_pop_ext_a, ext_a, sizeof ext_a), memcpy(game_pop_ext_b, ext_b, sizeof ext_b);
    memcpy(game_pop_file, file, 13);
    *game_pop_open = open, *game_pop_fallout_on = fallout, *game_win_screen_w = screen_w;
    life_sweep_dialog();
    life_sweep_pop();
    life_sweep_sprite_box();
    life_sweep_dialog_scalar();
    struct font *real_font;
    struct game_buffer *real_buf = button_sweep_buffer(&real_font);
    if (real_buf) px_sweep(real_buf, real_font);
    hooks_sweeping = sweeping;
}

// ---- Life cycle -----------------------------------------------------------------------------------

// A Dialog made and unmade. It holds its data (a String arena at 0x4), the defaults of the game's table
// (what Dialog_close puts back) and the list of its items at 0xbc, which has a virtual base of its own at
// 0xe4: the constructor saves the game's list head (0x9b3374) in the base (0xe8) and clears it, and the
// destructor puts it back. The destructor closes the Dialog, walks the list's vtables back to the base
// class's (which empties it), and gives the arena back. These allocate and free on the game's heap, so
// in shadow mode the game's calls run the original and the sweep checks Chiron's code on twin Dialogs
// (life.c). The exception frames are not reproduced (nothing reads them). Differences from OpenSMACX: it
// has none of these. The destructor's second walk of the list (the original inlines the close of the
// list again after calling remove_all) finds it empty and does nothing, so it is left out.

enum { DLG_VBPTR = 0x66b0ec, DLG_VTABLE = 0x6703fc, DLG_LIST_VT = 0x6698c4, DLG_LIST_PART = 0x6698c0, DLG_BASE_VT = 0x6693a4,
       DLG_BASE_PART = 0x6693a0, DLG_VB_VTABLE = 0x6693ac, DLG_HEAD = 0x9b3374 };

// Sets the vtable of the list's virtual base: the object's own address (0xc0 into the Dialog) plus the
// offset the base table gives.
static void dlist_set_part_vtable(struct game_dlist *l, uint32_t vtable) {
    const int32_t *vbtable = (const int32_t *)(uintptr_t)l->vbptr;
    *(uint32_t *)((uint8_t *)l + 4 + vbtable[1]) = vtable;
}

// A list as the Dialog class keeps it (and a BasePop and a StringBox keep their own): the list part, then its virtual base's table
// (a vbptr at 0x4) and, 0x28 on, the base's vtable and the game's list head the constructor saved and cleared (0x2c). The
// constructor saves and clears, the destructor empties the list, walks the vtables back and puts the head back.
void dlist_ctor(struct game_dlist *l) {
    uint8_t *o = (uint8_t *)l;
    l->vbptr = (void *)(uintptr_t)game_va(DLG_VBPTR);
    *(uint32_t *)(o + 0x28) = game_va(DLG_VB_VTABLE);
    *(uint32_t *)(o + 0x2c) = *(uint32_t *)game_at(DLG_HEAD);
    *(uint32_t *)game_at(DLG_HEAD) = 0;
    l->vtable = (void **)(uintptr_t)game_va(DLG_BASE_VT);
    dlist_set_part_vtable(l, game_va(DLG_BASE_PART));
    l->head = NULL, l->cur = NULL, l->count = 0, l->idx = 0, l->buf = NULL;
    l->vtable = (void **)(uintptr_t)game_va(DLG_LIST_VT);
    dlist_set_part_vtable(l, game_va(DLG_LIST_PART));
}

void dlist_dtor(struct game_dlist *l) {
    uint8_t *o = (uint8_t *)l;
    l->vtable = (void **)(uintptr_t)game_va(DLG_LIST_VT);
    dlist_set_part_vtable(l, game_va(DLG_LIST_PART));
    strlist_remove_all((struct game_strlist *)l);
    l->idx = 0;
    l->vtable = (void **)(uintptr_t)game_va(DLG_BASE_VT);
    dlist_set_part_vtable(l, game_va(DLG_BASE_PART));
    l->idx = 0;
    uint32_t saved = *(uint32_t *)(o + 0x2c);
    *(uint32_t *)(o + 0x28) = game_va(DLG_VB_VTABLE);
    *(uint32_t *)game_at(DLG_HEAD) = saved;
}

void *DLG_THIS dialog_ctor(struct game_dialog *d) {
    d->data_flag = 0, d->data = NULL, d->cursor = NULL, d->data_size = 0, d->data_left = 0;
    dlist_ctor(&d->list);
    d->vtable = (void **)(uintptr_t)game_va(DLG_VTABLE);
    dialog_defaults(d);
    return d;
}

void DLG_THIS dialog_dtor(struct game_dialog *d) {
    d->vtable = (void **)(uintptr_t)game_va(DLG_VTABLE);
    dialog_close(d);
    dlist_dtor(&d->list);
    game_dialog_data_close(&d->data_flag);
}

// ---- Life cycle: BasePop and SpriteBox -------------------------------------------------------------

// A BasePop (the popup, 0x3230 bytes) is a GraphicWin of its own kind with many parts: a string arena (0xa28), two FlatButtons (0xa5c, the
// OK button, and 0x15a8), a Sprite (0x2118), two dialog lists (0x2150, 0x2180), the combined widget the game's own functions make (0x21d0:
// a ListBox, with a RadioButton at +0x44, a CheckBox at +0x58, a SpriteBox at +0x70 and an EditGroup at +0xf8 as parts, a GraphicWin at
// +0x188 and a Dialog at +0xba0 as virtual bases), a Spot list (0x3098), tables the game's defaults are copied into (0x2e64 on) and the
// popup's own fields. The constructor and close put the same defaults in (close also asks the buttons to close themselves, gives back the
// two texts, the file name and the popup's eight objects, and closes each part); the destructor closes, unmakes the parts one by one and the
// GraphicWin. The combined widget's constructor and the part of its destructor before its bases (0x612830, 0x406910) and the CheckBox's
// close are the original's. Quirks kept: close puts the same number in two fields where the constructor puts two (0x3104 and 0x3108 both
// come from 0x696ed8); the original's scaling of the height with the screen (when the external window is not set) never runs, as it tests
// the window it has just found set. The ones that allocate or free do so on the game's heap, so in shadow mode the game's calls run the
// original and the sweep checks Chiron's code on twin popups (life.c). The exception frames are not reproduced. Differences from
// OpenSMACX: it has none of these.

#define PW(o, off) (*(uint32_t *)((uint8_t *)(o) + (off)))
#define GLOB(addr) (*(uint32_t *)game_at(addr))
enum {
    BP_VTABLE = 0x6698d4, BP_BUFFER_VTABLE = 0x6698cc, BP_ARENA = 0xa28, BP_BTN_A = 0xa5c, BP_BTN_B = 0x15a8, BP_SPRITE = 0x2118,
    BP_LIST_A = 0x2150, BP_LIST_B = 0x2180, BP_DIALOGS = 0x21d0, BP_RADIO = 0x2214, BP_CHECK = 0x2228, BP_SPRITEBOX = 0x2240,
    BP_EDITGROUP = 0x22c8, BP_DLG_WIN = 0x2358, BP_DLG_DLG = 0x2d70, BP_SPOT = 0x3098, BP_VT_CLOSE = 0x168
};

// The defaults both the constructor and close put in: the text and button colours and fonts, the default sizes and the external window
// (0xa14 on). The tables the game keeps are copied in by pop_copy_tables and pop_colour_rows.
static const struct life_set pop_defaults[] = {
    LG(0x21b8, 0x9b8d94), LG(0x21bc, 0x9b8d90), LG(0x21b0, 0x9b8d88), LG(0x21b4, 0x9b8d8c), LG(0x21c0, 0x696ed0), LG(0x21c4, 0x696ed4),
    LZ(0x30a8), LZ(0x30ac),
    LZ(0x30b0), LZ(0x30b4), LZ(0x30b8), LZ(0x30bc), LZ(0x30c0), LZ(0x30c4), LZ(0x30c8), LZ(0x30cc), LZ(0x30d0), LZ(0x30d4), LZ(0x30d8),
    LZ(0x30dc), LZ(0x30e0), LZ(0x30e4), LZ(0x30e8), LZ(0x30ec), LZ(0x30f0), LZ(0x30f4),
    LC(0x30f8, 0x1000), LC(0x30fc, 0x1000), LZ(0x3100), LG(0x3104, 0x696ed8), LG(0x310c, 0x696ee0), LG(0x3110, 0x9b8d98),
    LG(0x3120, 0x696ee4), LG(0x3130, 0x696ef4), LG(0x3140, 0x696f04), LG(0x3150, 0x696f14), LG(0x3114, 0x9b8d9c),
    LG(0x3124, 0x696ee8), LG(0x3134, 0x696ef8), LG(0x3144, 0x696f08), LG(0x3154, 0x696f18), LG(0x3118, 0x9b8da0),
    LG(0x3128, 0x696eec), LG(0x3138, 0x696efc), LG(0x3148, 0x696f0c), LG(0x3158, 0x696f1c), LG(0x311c, 0x9b8da4),
    LG(0x312c, 0x696ef0), LG(0x313c, 0x696f00), LG(0x314c, 0x696f10), LG(0x315c, 0x696f20), LZ(0x3160), LZ(0x3164), LZ(0x3168),
    LG(0x316c, 0x9b8da8), LG(0x317c, 0x696f30), LG(0x3188, 0x696f3c), LG(0x3194, 0x696f48), LG(0x3170, 0x9b8dac),
    LG(0x3180, 0x696f34), LG(0x318c, 0x696f40), LG(0x3198, 0x696f4c), LG(0x3174, 0x9b8db0), LG(0x3184, 0x696f38),
    LG(0x3190, 0x696f44), LG(0x319c, 0x696f50), LG(0x31a4, 0x696f58), LG(0x31a8, 0x696f5c), LG(0x31ac, 0x696f60),
    LG(0x31b0, 0x9b8db4), LG(0x31b4, 0x9b8db8), LG(0x214c, 0x9b8dc0), LG(0x2148, 0x9b8dbc), LZ(0x322c), LG(0x322c, 0x9b8dd0),
    LG(0xa14, 0x9b8d04), LG(0xa18, 0x9b8d08), LG(0xa1c, 0x9b8d0c), LG(0xa20, 0x696ec4), LG(0xa24, 0x696ec8),
};

// The table and three bytes the defaults take besides, and the field the constructor and close take different globals for (0x3108).
static void pop_defaults_apply(uint8_t *p, int ctor) {
    life_apply(p, pop_defaults, (int)(sizeof pop_defaults / sizeof pop_defaults[0]));
    PW(p, 0x3108) = GLOB(ctor ? 0x696edc : 0x696ed8);
    p[0x3178] = *(uint8_t *)game_at(0x696f24), p[0x3179] = *(uint8_t *)game_at(0x696f28), p[0x317a] = *(uint8_t *)game_at(0x696f2c);
    p[0x31a0] = *(uint8_t *)game_at(0x696f54);
}

// The two tables of 0x118 bytes, and the three columns of nine numbers (from the game's table at 0x9b8d10).
static void pop_copy_tables(uint8_t *p) {
    PW(p, 0x30a4) = GLOB(0x9b8dcc), PW(p, 0x3094) = GLOB(0x9b8cf8);
    memcpy(p + 0x2e64, game_at(0x9b8ab8), 0x118);
    memcpy(p + 0x2f7c, game_at(0x9b8bd0), 0x118);
}
static void pop_colour_rows(uint8_t *p) {
    for (int k = 0; k < 3; k++) {
        uint8_t *c = p + 0x31c4 + 4 * k;
        uint32_t a = (uint32_t)(4 * k);
        PW(c, -0xc) = GLOB(0x9b8d10 + a), PW(c, 0) = GLOB(0x9b8d1c + a), PW(c, 0xc) = GLOB(0x9b8d28 + a), PW(c, 0x18) = GLOB(0x9b8d34 + a);
        PW(c, 0x24) = GLOB(0x9b8d40 + a), PW(c, 0x30) = GLOB(0x9b8d4c + a), PW(c, 0x3c) = GLOB(0x9b8d58 + a), PW(c, 0x48) = GLOB(0x9b8d64 + a);
        PW(c, 0x54) = GLOB(0x9b8d70 + a);
    }
}

// The external window's size goes to the dialog part of the combined widget (and its flags get bits 0 and 1), and its place to the popup.
static void pop_external(uint8_t *p) {
    if (!PW(p, 0xa14)) return;
    uint8_t *dlg = p + BP_DIALOGS + life_vb(p + BP_DIALOGS, 2);
    PW(dlg, 0x2c) = PW(p, 0xa20);
    PW(dlg, 0x30) = PW(p, 0xa24);
    if (PW(p, 0xa18) != 0x2000) PW(p, 0x30f8) = PW(p, 0xa18);
    if (PW(p, 0xa1c) != 0x2000) PW(p, 0x30fc) = PW(p, 0xa1c);
    PW(dlg, 0x20) |= 3;
}

void *DLG_THIS pop_ctor(struct game_base_pop *pop) {
    uint8_t *p = (uint8_t *)pop;
    graphic_win_ctor((struct game_win *)pop);
    game_arena_ctor(p + BP_ARENA);
    flat_button_ctor((struct game_flat_button *)(p + BP_BTN_A));
    flat_button_ctor((struct game_flat_button *)(p + BP_BTN_B));
    sprite_ctor((struct game_sprite *)(p + BP_SPRITE));
    dlist_ctor((struct game_dlist *)(p + BP_LIST_A));
    dlist_ctor((struct game_dlist *)(p + BP_LIST_B));
    game_dialogs_ctor(p + BP_DIALOGS, 1);
    game_spot_ctor(p + BP_SPOT);
    PW(p, 0) = game_va(BP_VTABLE), PW(p, 0x444) = game_va(BP_BUFFER_VTABLE);
    pop_copy_tables(p);
    pop_colour_rows(p);
    PW(p, 0x21c8) = 0, PW(p, 0x21cc) = 0xffffffffu, PW(p, 0x3224) = 0, PW(p, 0x3228) = 0, PW(p, 0xa3c) = (uint32_t)(uintptr_t)p;
    PW(p, 0x20f4) = 0, PW(p, 0xa40) = 0, PW(p, 0xa44) = 0xffffffffu;
    memset(p + 0x20f8, 0, 32);
    PW(p, 0xa48) = 0;
    pop_defaults_apply(p, 1);
    sprite_close((struct game_sprite *)(p + BP_SPRITE));
    PW(p, 0x2144) = 0;
    pop_external(p);
    return pop;
}

// Closes every part and puts the defaults back; gives back the two button texts, the file name (0xa48) and the eight objects (0x20f8).
void DLG_THIS pop_close(struct game_base_pop *pop) {
    uint8_t *p = (uint8_t *)pop;
    pop_copy_tables(p);
    for (int off = 0x3224; off <= 0x3228; off += 4)
        if (PW(p, off)) game_free((void *)(uintptr_t)PW(p, off)), PW(p, off) = 0;
    PW(p, 0xa3c) = (uint32_t)(uintptr_t)p;
    pop_colour_rows(p);
    listbox_close(p + BP_DIALOGS);
    game_checkbox_close(p + BP_CHECK);
    radio_close(p + BP_RADIO);
    sprite_box_close(p + BP_SPRITEBOX);
    editgroup_close(p + BP_EDITGROUP);
    graphic_win_close((struct game_win *)pop);
    sprite_close((struct game_sprite *)(p + BP_SPRITE));
    PW(p, 0x20f4) = 0, PW(p, 0x2144) = 0, PW(p, 0xa40) = 0, PW(p, 0xa44) = 0xffffffffu;
    if (PW(p, 0xa48)) game_free((void *)(uintptr_t)PW(p, 0xa48)), PW(p, 0xa48) = 0;
    PW(p, 0x21c8) = 0, PW(p, 0x21cc) = 0xffffffffu;
    vcall0(p + BP_BTN_A, BP_VT_CLOSE);
    vcall0(p + BP_BTN_B, BP_VT_CLOSE);
    for (int off = BP_LIST_B; off >= BP_LIST_A; off -= BP_LIST_B - BP_LIST_A) {
        struct game_strlist *l = (struct game_strlist *)(p + off);
        strlist_remove_all(l);
        l->idx = 0;
    }
    for (int off = 0x20f8; off < 0x2118; off += 4)
        if (PW(p, off)) {
            void *obj = (void *)(uintptr_t)PW(p, off);
            ((void(THISCALL *)(void *, int))(*(void ***)obj)[0])(obj, 1);
            PW(p, off) = 0;
        }
    pop_defaults_apply(p, 0);
    pop_external(p);
    game_dialog_data_close(p + BP_ARENA);
}

void DLG_THIS pop_dtor(struct game_base_pop *pop) {
    uint8_t *p = (uint8_t *)pop;
    PW(p, 0) = game_va(BP_VTABLE), PW(p, 0x444) = game_va(BP_BUFFER_VTABLE);
    pop_close(pop);
    game_spot_dtor(p + BP_SPOT);
    game_dialogs_dtor_part(p + BP_DLG_WIN);
    dialog_dtor((struct game_dialog *)(p + BP_DLG_DLG));
    graphic_win_dtor((struct game_win *)(p + BP_DLG_WIN));
    dlist_dtor((struct game_dlist *)(p + BP_LIST_B));
    dlist_dtor((struct game_dlist *)(p + BP_LIST_A));
    sprite_close((struct game_sprite *)(p + BP_SPRITE));
    for (int off = BP_BTN_B; off >= BP_BTN_A; off -= BP_BTN_B - BP_BTN_A) {
        struct game_flat_button *f = (struct game_flat_button *)(p + off);
        f->base.win.vtable = (void **)(uintptr_t)game_va(0x669754);
        f->base.buf.vtable = (void **)(uintptr_t)game_va(0x66974c);
        flat_button_close(f);
        base_button_dtor(&f->base);
    }
    game_arena_close(p + BP_ARENA);
    graphic_win_dtor((struct game_win *)pop);
}

// The popup's button texts and the defaults for new popups: the old copy is freed, the new one made on the game's heap. No text answers 3
// and no memory 5 (the default cancel text answers 4: the original's two last functions differ there), leaving the copy empty.
static int pop_set_text(uint32_t *slot, const char *text, int fail) {
    if (!text) return 3;
    if (*slot) game_free((void *)(uintptr_t)*slot), *slot = 0;
    char *copy = game_mem_get(strlen(text) + 1);
    *slot = (uint32_t)(uintptr_t)copy;
    if (!copy) return fail;
    copy[0] = 0;
    strcpy(copy, text);
    return 0;
}
int DLG_THIS pop_set_ok_text(struct game_base_pop *p, const char *text) { return pop_set_text((uint32_t *)((uint8_t *)p + 0x3224), text, 5); }
int DLG_THIS pop_set_cancel_text(struct game_base_pop *p, const char *text) { return pop_set_text((uint32_t *)((uint8_t *)p + 0x3228), text, 5); }
int pop_set_def_ok_text(const char *text) { return pop_set_text((uint32_t *)game_at(0x9b8d80), text, 5); }
int pop_set_def_cancel_text(const char *text) { return pop_set_text((uint32_t *)game_at(0x9b8d84), text, 4); }

// SpriteBox: an object with two virtual bases, a GraphicWin (at 0x8c when it is the whole object) and a Dialog (0xaa4), found through the table the
// first word points to, as the CheckBox's and ListBox's are. Its own fields: a Spot list (0x10), a list of sprites (0x24, with a base table of its own
// at 0x28 and the game's list head saved and cleared at 0x50), the functions a screen set to hear of the mouse (0x68 on, cleared by the constructor and close) and a few numbers. The
// constructor (given whether it is the whole object) makes the bases and the Spot and the list, sets the vtables and the word before each base,
// the numbers, and the Buffer's text colours and fonts from the game's defaults; close closes the bases and starts the fields again; the
// destructor (given the GraphicWin, 0x8c on) sets the vtables again, closes, unmakes the list and the Spot, and puts the list head back.
enum {
    SB_VBPTR = 0x6708b4, SB_WIN_VT = 0x670738, SB_BUF_VT = 0x670730, SB_DLG_VT = 0x670724, SB_LIST_VBTABLE = 0x6708ac, SB_LIST_VT = 0x6708a4,
    SB_LIST_PART = 0x6708a0, SB_LIST_OLD_VT = 0x66943c, SB_LIST_OLD_PART = 0x669438, SB_WIN = 0x8c, SB_DLG = 0xaa4, SB_LIST = 0x24
};

static void sprite_box_vtables(uint8_t *o) {
    int32_t win = life_vb(o, 1), dlg = life_vb(o, 2);
    PW(o, win) = game_va(SB_WIN_VT), PW(o, win + 0x444) = game_va(SB_BUF_VT), PW(o, dlg) = game_va(SB_DLG_VT);
    PW(o, win - 4) = (uint32_t)(win - SB_WIN), PW(o, dlg - 4) = (uint32_t)(dlg - SB_DLG);
}

// The numbers a new SpriteBox and close start with, and the mouse functions (0x68 on) cleared.
static void sprite_box_numbers(uint8_t *o) {
    PW(o, 4) = 0, PW(o, 0x1c) = 1, PW(o, 0x20) = 1;
    memset(o + 0x54, 0, 0x84 - 0x54 + 4);
}

// The Buffer's text colours and fonts from the game's table.
static void sprite_box_buffer(uint8_t *o) {
    struct game_buffer *b = (struct game_buffer *)(o + life_vb(o, 1) + 0x444);
    PW(o, 8) = GLOB(0x9b8fc4);
    buffer_set_text_color(b, (int)GLOB(0x6970ac), (int)GLOB(0x6970b8), (int)GLOB(0x6970c4), (int)GLOB(0x6970d0));
    buffer_set_text_color2(b, (int)GLOB(0x6970b0), (int)GLOB(0x6970bc), (int)GLOB(0x6970c8), (int)GLOB(0x6970d4));
    buffer_set_text_color3(b, (int)GLOB(0x6970b4), (int)GLOB(0x6970c0), (int)GLOB(0x6970cc), (int)GLOB(0x6970d8));
    buffer_set_font(b, (struct font *)(uintptr_t)GLOB(0x9b8ec0), (struct font *)(uintptr_t)GLOB(0x9b8ec4),
                    (struct font *)(uintptr_t)GLOB(0x9b8ec8), NULL);
}

void *DLG_THIS sprite_box_ctor(uint8_t *o, int most_derived) {
    if (most_derived) {
        PW(o, 0) = game_va(SB_VBPTR);
        graphic_win_ctor((struct game_win *)(o + SB_WIN));
        dialog_ctor((struct game_dialog *)(o + SB_DLG));
    }
    game_spot_ctor(o + 0x10);
    struct game_dlist *l = (struct game_dlist *)(o + SB_LIST);
    l->vbptr = (void *)(uintptr_t)game_va(SB_LIST_VBTABLE);
    PW(o, 0x4c) = game_va(DLG_VB_VTABLE);
    PW(o, 0x50) = GLOB(DLG_HEAD);
    GLOB(DLG_HEAD) = 0;
    l->vtable = (void **)(uintptr_t)game_va(SB_LIST_OLD_VT);
    dlist_set_part_vtable(l, game_va(SB_LIST_OLD_PART));
    l->head = NULL, l->cur = NULL, l->count = 0, l->idx = 0, l->buf = NULL;
    l->vtable = (void **)(uintptr_t)game_va(SB_LIST_VT);
    dlist_set_part_vtable(l, game_va(SB_LIST_PART));
    sprite_box_vtables(o);
    sprite_box_numbers(o);
    sprite_box_buffer(o);
    return o;
}

void DLG_THIS sprite_box_close(uint8_t *o) {
    graphic_win_close((struct game_win *)(o + life_vb(o, 1)));
    dialog_close((struct game_dialog *)(o + life_vb(o, 2)));
    sprite_box_numbers(o);
    struct game_strlist *l = (struct game_strlist *)(o + SB_LIST);
    strlist_remove_all(l);
    l->idx = 0;
    spot_shutdown((struct game_spot *)(o + 0x10));
    sprite_box_buffer(o);
}

void DLG_THIS sprite_box_dtor(uint8_t *win) {
    uint8_t *o = win - SB_WIN;
    sprite_box_vtables(o);
    sprite_box_close(o);
    struct game_dlist *l = (struct game_dlist *)(o + SB_LIST);
    l->vtable = (void **)(uintptr_t)game_va(SB_LIST_OLD_VT);
    dlist_set_part_vtable(l, game_va(SB_LIST_OLD_PART));
    strlist_remove_all((struct game_strlist *)l);
    l->idx = 0;
    uint32_t saved = PW(o, 0x50);
    PW(o, 0x4c) = game_va(DLG_VB_VTABLE);
    GLOB(DLG_HEAD) = saved;
    spot_dtor((struct game_spot *)(o + 0x10));
}

// A Dialog's scalar deleting destructor: the destructor, then the object freed if bit 0 of the flag is set.
LIFE_SCALAR(dialog_scalar_dtor, struct game_dialog, dialog_dtor)

// ---- Life cycle: shadow mode and the sweep ----

SHADOW_PASS_T(DLG, void *, dialog_ctor, (struct game_dialog * d), (d))
SHADOW_PASS_T(DLG, void, dialog_dtor, (struct game_dialog * d), (d))

LIFE_FN(dialog_ctor, orig_dialog_ctor(o), dialog_ctor(o))
LIFE_FN(dialog_dtor, orig_dialog_dtor(o), dialog_dtor(o))

// The data block and its cursor, and the list's head and cursor.
static const struct life_slot dialog_slots[] = {{8, -1}, {0xc, -1}, {0xc4, -1}, {0xc8, -1}};
static const struct life_class dialog_class = {"Dialog", 0xf4, dialog_slots, 4};

static void life_sweep_dialog(void) {
    uint32_t *head_global[1] = {(uint32_t *)game_at(DLG_HEAD)};
    if (!game_mem_get || !game_string_alloc) return;
    for (int round = 0; round < 4; round++) {
        uint8_t fill = (uint8_t)(round * 31 + 9);
        life_fill(&dialog_class, life_a, life_b, fill);
        life_run(&stats_dialog_ctor, &dialog_class, life_orig_dialog_ctor, life_ours_dialog_ctor, NULL, head_global, 1);
        // The destructor, on Dialogs with 0 to 3 items and, on some rounds, data.
        life_build(&dialog_class, fill, life_orig_dialog_ctor, NULL);
        life_nodes(0xbc, round);
        if (round & 1) life_data(0, 64);
        life_run(&stats_dialog_dtor, &dialog_class, life_orig_dialog_dtor, life_ours_dialog_dtor, NULL, head_global, 1);
    }
}

// ---- Life cycle: BasePop and SpriteBox, shadow mode and the sweep ----

SHADOW_PASS_T(DLG, void *, pop_ctor, (struct game_base_pop * p), (p))
SHADOW_PASS_T(DLG, void, pop_close, (struct game_base_pop * p), (p))
SHADOW_PASS_T(DLG, void, pop_dtor, (struct game_base_pop * p), (p))
SHADOW_PASS_T(DLG, int, pop_set_ok_text, (struct game_base_pop * p, const char *text), (p, text))
SHADOW_PASS_T(DLG, int, pop_set_cancel_text, (struct game_base_pop * p, const char *text), (p, text))
SHADOW_PASS(DLG, int, pop_set_def_ok_text, (const char *text), (text))
SHADOW_PASS(DLG, int, pop_set_def_cancel_text, (const char *text), (text))
SHADOW_PASS_T(DLG, void *, sprite_box_ctor, (uint8_t * o, int most), (o, most))
SHADOW_PASS_T(DLG, void, sprite_box_close, (uint8_t * o), (o))
SHADOW_PASS_T(DLG, void, sprite_box_dtor, (uint8_t * win), (win))
SHADOW_PASS_T(DLG, void *, dialog_scalar_dtor, (struct game_dialog * d, int flag), (d, flag))

// (The setters' answers go to a counter the check compares as a global.)
static uint32_t pop_text_answer;
LIFE_FN(pop_ctor, orig_pop_ctor(o), pop_ctor(o))
LIFE_FN(pop_close, orig_pop_close(o), pop_close(o))
LIFE_FN(pop_dtor, orig_pop_dtor(o), pop_dtor(o))
LIFE_FN(pop_set_ok_text, pop_text_answer = (uint32_t)orig_pop_set_ok_text(o, x), pop_text_answer = (uint32_t)pop_set_ok_text(o, x))
LIFE_FN(pop_set_cancel_text, pop_text_answer = (uint32_t)orig_pop_set_cancel_text(o, x), pop_text_answer = (uint32_t)pop_set_cancel_text(o, x))
LIFE_FN(sprite_box_ctor, orig_sprite_box_ctor(o, 1), sprite_box_ctor(o, 1))
LIFE_FN(sprite_box_close, orig_sprite_box_close(o), sprite_box_close(o))
LIFE_FN(sprite_box_dtor, orig_sprite_box_dtor((uint8_t *)o + SB_WIN), sprite_box_dtor((uint8_t *)o + SB_WIN))
LIFE_FN(dialog_scalar_dtor, orig_dialog_scalar_dtor(o, 0), dialog_scalar_dtor(o, 0))

// The default texts are the game's own words (0x9b8d80, 0x9b8d84): the check puts each run's block in the object's second word (the old one,
// freed by the run) and finds the new one in the first.
static void def_text_run(uint8_t *o, uint32_t addr, const char *x, int ours, int cancel) {
    GLOB(addr) = *(uint32_t *)(o + 4);
    const char *t = x;
    if (cancel) pop_text_answer = (uint32_t)(ours ? pop_set_def_cancel_text(t) : orig_pop_set_def_cancel_text(t));
    else pop_text_answer = (uint32_t)(ours ? pop_set_def_ok_text(t) : orig_pop_set_def_ok_text(t));
    *(uint32_t *)o = GLOB(addr);
}
static void life_orig_def_ok(void *o, void *x) { def_text_run(o, 0x9b8d80, x, 0, 0); }
static void life_ours_def_ok(void *o, void *x) { def_text_run(o, 0x9b8d80, x, 1, 0); }
static void life_orig_def_cancel(void *o, void *x) { def_text_run(o, 0x9b8d84, x, 0, 1); }
static void life_ours_def_cancel(void *o, void *x) { def_text_run(o, 0x9b8d84, x, 1, 1); }

// The popup's slots: the Win's dialog list, the file name, the eight objects, the two button texts, the two lists' head and cursor, the
// combined widget's GraphicWin's dialog list and its Dialog's data, cursor, list head and cursor.
static const struct life_slot pop_slots[] = {
    {0xcc, -1}, {0xd0, -1}, {0xa48, -1}, {0x20f8, -1}, {0x20fc, -1}, {0x2100, -1}, {0x2104, -1}, {0x2108, -1}, {0x210c, -1}, {0x2110, -1},
    {0x2114, -1}, {0x3224, -1}, {0x3228, -1}, {0x2158, -1}, {0x215c, -1}, {0x2188, -1}, {0x218c, -1}, {BP_DLG_WIN + 0xcc, -1},
    {BP_DLG_WIN + 0xd0, -1}, {BP_DLG_DLG + 8, -1}, {BP_DLG_DLG + 0xc, -1}, {BP_DLG_DLG + 0xc4, -1}, {BP_DLG_DLG + 0xc8, -1},
    {BP_SPOT, -1},
};
static const struct life_class pop_class = {"BasePop", 0x3230, pop_slots, 24, 0x444, 0x9cc, {0x21c8, 0x3100}};
static const struct life_slot pop_text_slots[] = {{0x3224, 0}, {0x3228, 0}};
static const struct life_class pop_text_class = {"BasePop texts", 0x3230, pop_text_slots, 2};
static const struct life_slot def_slots[] = {{0, 0}, {4, -1}};
static const struct life_class def_class = {"BasePop default texts", 8, def_slots, 2};

// Both objects get, for the Spot list at `off`, room for rectangles of the game's heap of its own (shutting it down gives the block back).
static void spot_room(int off) {
    uint8_t *objs[2] = {life_a, life_b};
    for (int k = 0; k < 2; k++) {
        struct game_spot *sp = (struct game_spot *)(objs[k] + off);
        sp->rects = game_mem_get(48);
        memset(sp->rects, 0, 48);
        sp->cap = 2, sp->count = 1;
    }
}

// What a popup holds besides its Win and Buffer: the arena's data, texts, objects, list items and the combined widget's Dialog's.
static void pop_extra(int round) {
    life_data(0xa24, 32);
    life_data(BP_DLG_DLG, 64);
    life_nodes(BP_DLG_DLG + 0xbc, round % 3);
    life_nodes(BP_LIST_A, round % 3);
    life_nodes(BP_LIST_B, (round + 1) % 3);
    spot_room(BP_SPOT);
    life_objects(0x20f8, round % 4);
    if (round & 1) life_text(0xa48, "a file name");
    if (round & 2) life_text(0x3224, "OK"), life_text(0x3228, "Cancel");
}

// The external window (a number in 0x9b8d04, which the popup only tests): set on some rounds, with a size and a place.
static void pop_external_globals(int round) {
    GLOB(0x9b8d04) = (round & 4) ? 0x1234 : 0;
    GLOB(0x9b8d08) = (round & 8) ? 0x2000 : 0x333;
    GLOB(0x9b8d0c) = (round & 16) ? 0x2000 : 0x444;
    GLOB(0x696ec4) = 0x555, GLOB(0x696ec8) = 0x666;
}

static void life_sweep_pop(void) {
    uint32_t *globals[2] = {(uint32_t *)game_at(DLG_HEAD), (uint32_t *)game_at(0x9b6618)};
    uint32_t *answer[1] = {&pop_text_answer};
    uint32_t saved[5] = {GLOB(0x9b8d04), GLOB(0x9b8d08), GLOB(0x9b8d0c), GLOB(0x696ec4), GLOB(0x696ec8)};
    if (!game_mem_get || *game_ddraw || !hooks_dc_same || !game_string_alloc) return;
    static const char *const texts[] = {"OK", "", "a much longer button text than the one before", NULL};
    for (int round = 0; round < 12; round++) {
        uint8_t fill = (uint8_t)(round * 67 + 29);
        pop_external_globals(round);
        life_ctor_check(&stats_pop_ctor, &pop_class, life_orig_pop_ctor, life_ours_pop_ctor, fill);
        for (int which = 0; which < 2; which++) {
            struct hook_stats *s = which ? &stats_pop_dtor : &stats_pop_close;
            life_build(&pop_class, fill, life_orig_pop_ctor, NULL);
            life_buffers(0x444, 4 + round % 3, 3 + round % 3);
            pop_extra(round);
            if (life_run(s, &pop_class, which ? life_orig_pop_dtor : life_orig_pop_close, which ? life_ours_pop_dtor : life_ours_pop_close, NULL,
                         globals, 2))
                life_buffer_check(s);
            if (!which)
                for (int i = 0; i < 2; i++) orig_pop_dtor((struct game_base_pop *)(i ? life_b : life_a));  // (close leaves the parts standing)
        }
        // The setters, on popups that hold no text, one or both.
        const char *t = texts[round % 4];
        life_fill(&pop_text_class, life_a, life_b, fill);
        if (round & 1) life_text(0x3224, "an old OK text"); else PW(life_a, 0x3224) = PW(life_b, 0x3224) = 0;
        if (round & 2) life_text(0x3228, "an old cancel text"); else PW(life_a, 0x3228) = PW(life_b, 0x3228) = 0;
        life_run(&stats_pop_set_ok_text, &pop_text_class, life_orig_pop_set_ok_text, life_ours_pop_set_ok_text, (void *)(uintptr_t)t, answer, 1);
        life_run(&stats_pop_set_cancel_text, &pop_text_class, life_orig_pop_set_cancel_text, life_ours_pop_set_cancel_text, (void *)(uintptr_t)t,
                 answer, 1);
        for (int i = 0; i < 2; i++)
            for (int off = 0x3224; off <= 0x3228; off += 4) {
                void *b = *(void **)((i ? life_b : life_a) + off);
                if (b) game_free(b);
            }
        // The defaults for new popups (words of the game): an old block of each run's own in the second word.
        for (int which = 0; which < 2; which++) {
            memset(life_a, 0, 8), memset(life_b, 0, 8);
            life_text(4, "an old default");
            uint32_t keep = GLOB(which ? 0x9b8d84 : 0x9b8d80);
            life_run(which ? &stats_pop_set_def_cancel_text : &stats_pop_set_def_ok_text, &def_class, which ? life_orig_def_cancel : life_orig_def_ok,
                     which ? life_ours_def_cancel : life_ours_def_ok, (void *)(uintptr_t)t, answer, 1);
            // (A null text leaves the old block alone, which the first word then holds as the global did: free it once.)
            for (int i = 0; i < 2; i++) game_free(*(void **)((i ? life_b : life_a) + (t ? 0 : 4)));
            GLOB(which ? 0x9b8d84 : 0x9b8d80) = keep;
        }
    }
    GLOB(0x9b8d04) = saved[0], GLOB(0x9b8d08) = saved[1], GLOB(0x9b8d0c) = saved[2], GLOB(0x696ec4) = saved[3], GLOB(0x696ec8) = saved[4];
}

static const struct life_slot sbox_slots[] = {{SB_WIN + 0xcc, -1}, {SB_WIN + 0xd0, -1}, {SB_DLG + 8, -1}, {SB_DLG + 0xc, -1}, {SB_DLG + 0xc4, -1},
                                              {SB_DLG + 0xc8, -1}, {SB_LIST + 8, -1}, {SB_LIST + 0xc, -1}, {0x10, -1}};
static const struct life_class sbox_class = {"SpriteBox", SB_DLG + 0xf4, sbox_slots, 9, SB_WIN + 0x444, SB_WIN + 0x9cc, {0x1c, 0x58}};
static void sbox_extra(int round) {
    life_nodes(SB_LIST, round % 4);
    spot_room(0x10);
}

static void life_sweep_sprite_box(void) {
    if (!game_mem_get || *game_ddraw || !hooks_dc_same) return;
    uint32_t keep_a = GLOB(0x9b8fc4);
    for (int round = 0; round < 8; round++) {
        uint8_t fill = (uint8_t)(round * 71 + 3);
        life_vb_round(&sbox_class, SB_WIN, SB_DLG, &stats_sprite_box_ctor, life_orig_sprite_box_ctor, life_ours_sprite_box_ctor, &stats_sprite_box_close,
                      life_orig_sprite_box_close, life_ours_sprite_box_close, round, fill, sbox_extra);
        life_vb_round(&sbox_class, SB_WIN, SB_DLG, &stats_sprite_box_ctor, life_orig_sprite_box_ctor, life_ours_sprite_box_ctor, &stats_sprite_box_dtor,
                      life_orig_sprite_box_dtor, life_ours_sprite_box_dtor, round, fill, sbox_extra);
    }
    GLOB(0x9b8fc4) = keep_a;
}

static void life_sweep_dialog_scalar(void) {
    static const struct life_slot slots[] = {{8, -1}, {0xc, -1}, {0xc4, -1}, {0xc8, -1}};
    static const struct life_class cls = {"Dialog", 0xf4, slots, 4};
    uint32_t *head_global[1] = {(uint32_t *)game_at(DLG_HEAD)};
    if (!game_mem_get || !game_string_alloc) return;
    for (int round = 0; round < 4; round++) {
        life_build(&cls, (uint8_t)(round * 31 + 9), life_orig_dialog_ctor, NULL);
        life_nodes(0xbc, round);
        if (round & 1) life_data(0, 64);
        life_run(&stats_dialog_scalar_dtor, &cls, life_orig_dialog_scalar_dtor, life_ours_dialog_scalar_dtor, NULL, head_global, 1);
    }
}

// ---- Hooks --------------------------------------------------------------------------------------

static const struct hook dialog_hooks[] = {
    HOOK(0x600860, pop_ctor, 0x6a, 0xff, 0x68, 0xf8, 0x2d, 0x66, 0x00),  // push -1; push handler
    HOOK(0x600f00, pop_close, 0x51, 0xa1, 0xcc, 0x8d, 0x9b, 0x00, 0x53, 0x8b, 0xd9),  // push ecx; mov eax, [0x9b8dcc]; push ebx; mov ebx, ecx
    HOOK(0x4064d0, pop_dtor, 0x55, 0x8b, 0xec, 0x6a, 0xff, 0x68, 0x6e, 0x09, 0x65, 0x00),
    // push esi; push edi; mov edi, [esp+0xc]; mov esi, ecx
    HOOK(0x6017a0, pop_set_ok_text, 0x56, 0x57, 0x8b, 0x7c, 0x24, 0x0c, 0x8b, 0xf1),
    HOOK(0x601820, pop_set_cancel_text, 0x56, 0x57, 0x8b, 0x7c, 0x24, 0x0c, 0x8b, 0xf1),
    HOOK(0x6018a0, pop_set_def_ok_text, 0x56, 0x8b, 0x74, 0x24, 0x08, 0x85, 0xf6),  // push esi; mov esi, [esp+8]; test esi, esi
    HOOK(0x601910, pop_set_def_cancel_text, 0x56, 0x8b, 0x74, 0x24, 0x08, 0x85, 0xf6),
    HOOK(0x60ff00, sprite_box_ctor, 0x6a, 0xff, 0x68, 0xf0, 0x2f, 0x66, 0x00),  // push -1; push handler
    HOOK(0x610120, sprite_box_dtor, 0x6a, 0xff, 0x68, 0x10, 0x30, 0x66, 0x00),
    HOOK(0x610280, sprite_box_close, 0x51, 0x55, 0x56, 0x8b, 0xf1, 0x57, 0x8b, 0x06),  // push ecx; push ebp; push esi; mov esi, ecx; push edi; mov eax, [esi]
    HOOK_CALL(0x609d90, dialog_scalar_dtor, 0x56, 0x8b, 0xf1, 0xe8, 0x78, 0xf0, 0xff, 0xff),  // push esi; mov esi, ecx; call dialog_dtor
    HOOK(0x608c10, dialog_ctor, 0x51, 0x56, 0x8b, 0xf1, 0x57),  // push ecx; push esi; mov esi, ecx; push edi
    HOOK(0x608e10, dialog_dtor, 0x6a, 0xff, 0x68, 0xe4, 0x2e, 0x66, 0x00),  // push -1; push handler
    HOOK(0x600760, purge_leading, 0x8b, 0x44, 0x24, 0x04, 0x80, 0x38, 0x20),
    HOOK(0x600780, purge_trailing, 0x56, 0x8b, 0x74, 0x24, 0x08, 0x56),
    HOOK(0x6007b0, purge_spaces, 0x81, 0xec, 0x00, 0x02, 0x00, 0x00),
    HOOK(0x600820, kill_lf, 0x8b, 0x44, 0x24, 0x04, 0x6a, 0x0a),
    HOOK(0x600840, add_lf, 0x56, 0x8b, 0x74, 0x24, 0x08, 0x56),
    HOOK(0x625880, parse_string, 0x81, 0xec, 0xfc, 0x00, 0x00, 0x00, 0x53, 0x8b, 0x9c, 0x24, 0x04, 0x01, 0x00, 0x00),
    HOOK(0x628b30, findnum, 0x55, 0x8b, 0xec, 0x51, 0x53, 0x8b, 0x45, 0x08),
    HOOK(0x6288d0, btoi, 0x8b, 0x54, 0x24, 0x04, 0x33, 0xc0, 0x8a, 0x0a),
    HOOK(0x6288f0, htoi, 0x56, 0x57, 0x8b, 0x7c, 0x24, 0x0c, 0x33, 0xf6),
    HOOK(0x628950, stoi, 0x8b, 0x44, 0x24, 0x04, 0x80, 0x38, 0x30),
    HOOK(0x5fd670, text_item, 0xa1, 0xf0, 0x7c, 0x9b, 0x00, 0x8b, 0x0d, 0xfc, 0x7c, 0x9b, 0x00),
    HOOK(0x5fd740, text_item_number, 0xa1, 0xf0, 0x7c, 0x9b, 0x00, 0x8b, 0x0d, 0xfc, 0x7c, 0x9b, 0x00),
    HOOK(0x5fd7a0, text_item_binary, 0xa1, 0xf0, 0x7c, 0x9b, 0x00, 0x8b, 0x0d, 0xfc, 0x7c, 0x9b, 0x00),
    HOOK(0x5fd800, text_item_hex, 0xa1, 0xf0, 0x7c, 0x9b, 0x00, 0x8b, 0x0d, 0xfc, 0x7c, 0x9b, 0x00),
    HOOK(0x5fdd10, text_obj_item, 0x56, 0x8b, 0xf1, 0x8b, 0x8e, 0x50, 0x01, 0x00, 0x00),
    HOOK(0x5fddf0, text_obj_item_number, 0x56, 0x8b, 0xf1, 0x8b, 0x8e, 0x50, 0x01, 0x00, 0x00),
    HOOK(0x5fde60, text_obj_item_binary, 0x56, 0x8b, 0xf1, 0x8b, 0x8e, 0x50, 0x01, 0x00, 0x00),
    HOOK(0x5fded0, text_obj_item_hex, 0x56, 0x8b, 0xf1, 0x8b, 0x8e, 0x50, 0x01, 0x00, 0x00),
    HOOK(0x5fe120, text_index_search, 0x8b, 0x44, 0x24, 0x04, 0x81, 0xec, 0x00, 0x02, 0x00, 0x00),
    HOOK(0x5fe230, text_search_index, 0x53, 0x8b, 0x5c, 0x24, 0x08, 0x56, 0x57, 0x8b, 0x7c, 0x24, 0x14),
    HOOK(0x627130, popup, 0x8b, 0x44, 0x24, 0x08, 0x8b, 0x4c, 0x24, 0x04),
    HOOK(0x627160, popup_2, 0x8b, 0x44, 0x24, 0x0c, 0x8b, 0x4c, 0x24, 0x08),
    HOOK(0x627190, popup_3, 0x56, 0x8b, 0x74, 0x24, 0x10),
    HOOK(0x6271d0, popup_4, 0x8b, 0x44, 0x24, 0x10, 0x8b, 0x4c, 0x24, 0x0c),
    HOOK(0x627200, popup_5, 0x8b, 0x44, 0x24, 0x14, 0x8b, 0x4c, 0x24, 0x10),
    HOOK(0x627230, popup_6, 0x8b, 0x44, 0x24, 0x0c, 0x8b, 0x4c, 0x24, 0x08),
    HOOK(0x627260, popup_7, 0x8b, 0x44, 0x24, 0x10, 0x8b, 0x4c, 0x24, 0x0c),
    HOOK(0x627290, popup_8, 0x8b, 0x44, 0x24, 0x14, 0x8b, 0x4c, 0x24, 0x10),
    HOOK(0x6272c0, popup_9, 0x8b, 0x44, 0x24, 0x18, 0x8b, 0x4c, 0x24, 0x14),
    HOOK(0x627310, popups, 0x8b, 0x44, 0x24, 0x0c, 0x8b, 0x4c, 0x24, 0x08),
    HOOK(0x627340, popups_2, 0x8b, 0x44, 0x24, 0x14, 0x8b, 0x4c, 0x24, 0x10),
    HOOK(0x627370, popups_3, 0x8b, 0x44, 0x24, 0x10, 0x8b, 0x4c, 0x24, 0x0c),
    HOOK(0x6273a0, popups_4, 0x8b, 0x44, 0x24, 0x18, 0x8b, 0x4c, 0x24, 0x14),
    HOOK(0x6273d0, popups_5, 0x8b, 0x44, 0x24, 0x14, 0x8b, 0x4c, 0x24, 0x10),
    HOOK(0x627400, popups_6, 0x8b, 0x44, 0x24, 0x1c, 0x8b, 0x4c, 0x24, 0x18),
    HOOK(0x627440, popups_7, 0x56, 0x8b, 0x74, 0x24, 0x10),
    HOOK(0x627480, popups_8, 0x8b, 0x44, 0x24, 0x0c, 0x85, 0xc0),
    HOOK(0x6274d0, popups_9, 0x8b, 0x44, 0x24, 0x18, 0x8b, 0x4c, 0x24, 0x14),
    HOOK(0x627500, popups_10, 0x8b, 0x44, 0x24, 0x20, 0x8b, 0x4c, 0x24, 0x1c),
    HOOK(0x627540, popups_11, 0x8b, 0x44, 0x24, 0x10, 0x8b, 0x4c, 0x24, 0x0c),
    HOOK(0x627570, popups_12, 0x8b, 0x44, 0x24, 0x18, 0x8b, 0x4c, 0x24, 0x14),
    HOOK(0x6275a0, popups_13, 0x8b, 0x44, 0x24, 0x14, 0x8b, 0x4c, 0x24, 0x10),
    HOOK(0x6275d0, popups_14, 0x8b, 0x44, 0x24, 0x1c, 0x8b, 0x4c, 0x24, 0x18),
    HOOK(0x627600, popups_15, 0x8b, 0x44, 0x24, 0x18, 0x8b, 0x4c, 0x24, 0x14),
    HOOK(0x627630, popups_16, 0x8b, 0x44, 0x24, 0x20, 0x8b, 0x4c, 0x24, 0x1c),
    HOOK(0x627670, popups_17, 0x8b, 0x44, 0x24, 0x1c, 0x8b, 0x4c, 0x24, 0x18),
    HOOK(0x627800, popup_ask, 0x8b, 0x44, 0x24, 0x14, 0x8b, 0x4c, 0x24, 0x10),
    HOOK(0x627830, popup_ask_2, 0x8b, 0x44, 0x24, 0x14, 0x8b, 0x4c, 0x24, 0x10),
    HOOK(0x627860, popup_ask_3, 0x8b, 0x44, 0x24, 0x10, 0x8b, 0x4c, 0x24, 0x0c),
    HOOK(0x627890, popup_ask_4, 0x8b, 0x44, 0x24, 0x0c, 0x85, 0xc0),
    HOOK(0x6278d0, popup_ask_5, 0x8b, 0x44, 0x24, 0x08, 0x85, 0xc0),
    {0x6276a0, (const uint8_t[]){0x8b, 0x0d, 0x74, 0xc0, 0x9b, 0x00, 0x53, 0x56, 0x57}, 9, NULL, (void *)shadow_pops_18,
     (void **)&orig_pops_18, &stats_pops_18, 1, 0},
    {0x627910, (const uint8_t[]){0x8b, 0x0d, 0x74, 0xc0, 0x9b, 0x00, 0x56}, 7, NULL, (void *)shadow_pop_ask_6,
     (void **)&orig_pop_ask_6, &stats_pop_ask_6, 1, 0},
    HOOK(0x604ab0, pop_on_redraw, 0x51, 0x53, 0x55, 0x56, 0x8b, 0xf1),
    HOOK(0x603800, pop_calculate_dimensions, 0x83, 0xec, 0x08, 0x53, 0x55, 0x56, 0x8b, 0xf1),
    {0x6132a0, (const uint8_t[]){0x8b, 0x81, 0x80, 0x01, 0x00, 0x00}, 6, NULL, (void *)shadow_dialog_calc, (void **)&orig_dialog_calc,
     &stats_dialog_calc, 1, 0},
    HOOK(0x601ac0, pop_set_height, 0x8b, 0x81, 0x14, 0x0a, 0x00, 0x00),
    HOOK(0x601b80, pop_set_loc, 0x8b, 0x44, 0x24, 0x04, 0x3d, 0x00, 0x20, 0x00, 0x00),
    HOOK(0x601bd0, pop_read_check, 0x8b, 0x81, 0x28, 0x22, 0x00, 0x00),
    HOOK(0x604410, pop_on_key_down, 0x56, 0x8b, 0xf1, 0x8b, 0x86, 0xa8, 0x30, 0x00, 0x00),
    HOOK(0x604490, pop_on_key_click, 0x8b, 0x81, 0xa8, 0x30, 0x00, 0x00),
    HOOK(0x6044b0, pop_on_key_up, 0x8b, 0x81, 0xa8, 0x30, 0x00, 0x00),
    HOOK(0x6044d0, pop_double_click, 0x56, 0x8b, 0x35, 0x7c, 0x8d, 0x9b, 0x00),
    HOOK(0x6043d0, pop_set_def_file, 0x56, 0x8b, 0x74, 0x24, 0x08, 0x85, 0xf6),
    HOOK(0x6046f0, pop_set_string_font, 0x8b, 0x44, 0x24, 0x04, 0x85, 0xc0),
    HOOK(0x604730, pop_set_string_color, 0x8b, 0x44, 0x24, 0x04, 0x8b, 0x54, 0x24, 0x08),
    HOOK(0x604760, pop_set_string_color2, 0x8b, 0x44, 0x24, 0x04, 0x8b, 0x54, 0x24, 0x08),
    HOOK(0x604790, pop_set_string_color3, 0x8b, 0x44, 0x24, 0x04, 0x8b, 0x54, 0x24, 0x08),
    HOOK(0x6047c0, pop_set_string_color_hyper, 0x8b, 0x44, 0x24, 0x04, 0x8b, 0x54, 0x24, 0x08),
    HOOK(0x6047f0, pop_set_button_font, 0x8b, 0x44, 0x24, 0x04, 0x85, 0xc0),
    HOOK(0x604830, pop_set_button_color, 0x8a, 0x44, 0x24, 0x04, 0x8b, 0x54, 0x24, 0x08),
    HOOK(0x604860, pop_set_button_color2, 0x8a, 0x44, 0x24, 0x04, 0x8b, 0x54, 0x24, 0x08),
    HOOK(0x604890, pop_set_button_color3, 0x8a, 0x44, 0x24, 0x04, 0x8b, 0x54, 0x24, 0x08),
    HOOK(0x6048c0, pop_set_def_string_font, 0x8b, 0x44, 0x24, 0x04, 0x85, 0xc0),
    HOOK(0x604900, pop_set_def_string_color, 0x8b, 0x44, 0x24, 0x04, 0x8b, 0x4c, 0x24, 0x08),
    HOOK(0x604930, pop_set_def_string_color2, 0x8b, 0x44, 0x24, 0x04, 0x8b, 0x4c, 0x24, 0x08),
    HOOK(0x604960, pop_set_def_string_color3, 0x8b, 0x44, 0x24, 0x04, 0x8b, 0x4c, 0x24, 0x08),
    HOOK(0x604990, pop_set_def_string_color_hyper, 0x8b, 0x44, 0x24, 0x04, 0x8b, 0x4c, 0x24, 0x08),
    HOOK(0x6049c0, pop_set_def_button_font, 0x8b, 0x44, 0x24, 0x04, 0x85, 0xc0),
    HOOK(0x6049f0, pop_set_def_button_color, 0x8b, 0x44, 0x24, 0x04, 0x8b, 0x4c, 0x24, 0x08),
    HOOK(0x604a20, pop_set_def_button_color2, 0x8b, 0x44, 0x24, 0x04, 0x8b, 0x4c, 0x24, 0x08),
    HOOK(0x604a50, pop_set_def_button_color3, 0x8b, 0x44, 0x24, 0x04, 0x8b, 0x4c, 0x24, 0x08),
    HOOK(0x604a80, pop_on_modal, 0x8b, 0x0d, 0xfc, 0x8c, 0x9b, 0x00),
    HOOK(0x604a90, pop_fallout, 0xa1, 0x00, 0x8d, 0x9b, 0x00, 0x85, 0xc0),
    HOOK(0x605010, pop_set_external_window, 0x8b, 0x44, 0x24, 0x04, 0x8b, 0x54, 0x24, 0x10),
    HOOK(0x6050f0, pop_set_def_external_window, 0x8b, 0x44, 0x24, 0x04, 0x8b, 0x4c, 0x24, 0x08),
    HOOK(0x605130, pop_on_left_down, 0x8b, 0x54, 0x24, 0x08, 0x56, 0x8b, 0xf1),
    HOOK(0x605180, pop_unk3, 0x8b, 0x44, 0x24, 0x04, 0x85, 0xc0),
    HOOK(0x6051a0, pop_unk4, 0x8b, 0x44, 0x24, 0x04, 0x85, 0xc0),
    HOOK(0x608f50, dialog_close, 0x51, 0x55, 0x56, 0x8b, 0xf1, 0x57),
    HOOK(0x609150, dialog_init, 0x51, 0x8b, 0x41, 0x1c, 0x53, 0x55),
    HOOK(0x609300, dialog_init_2, 0x53, 0x55, 0x56, 0x57, 0x8b, 0xf1),
    HOOK(0x609470, dialog_init_3, 0x8b, 0x44, 0x24, 0x04, 0x56, 0x85, 0xc0),
    HOOK(0x6095f0, dialog_init_4, 0x53, 0x55, 0x56, 0x57, 0x8b, 0xf1),
    // (these two begin with push ebx; push esi; mov esi, ecx; call Dialog_close: the call is re-aimed in the trampoline)
    {0x609730, (const uint8_t[]){0x53, 0x56, 0x8b, 0xf1, 0xe8, 0x17, 0xf8, 0xff, 0xff}, 9, (void *)dialog_init_5, (void *)shadow_dialog_init_5,
     (void **)&orig_dialog_init_5, &stats_dialog_init_5, 0, 1},
    HOOK(0x6097f0, dialog_init_6, 0x8b, 0x44, 0x24, 0x04, 0x56, 0x85, 0xc0),
    {0x6098d0, (const uint8_t[]){0x53, 0x56, 0x8b, 0xf1, 0xe8, 0x77, 0xf6, 0xff, 0xff}, 9, (void *)dialog_init_7, (void *)shadow_dialog_init_7,
     (void **)&orig_dialog_init_7, &stats_dialog_init_7, 0, 1},
    HOOK(0x609970, dialog_unk1, 0x8b, 0x54, 0x24, 0x08, 0x8b, 0x01),
    HOOK(0x609c60, dialog_set_font, 0x8b, 0x44, 0x24, 0x04, 0x85, 0xc0),
    HOOK(0x609c90, dialog_set_text_color, 0x8b, 0x44, 0x24, 0x04, 0x8b, 0x54, 0x24, 0x08),
    HOOK(0x609cc0, dialog_set_text_color2, 0x8b, 0x44, 0x24, 0x04, 0x8b, 0x54, 0x24, 0x08),
    HOOK(0x609cf0, dialog_set_text_color3, 0x8b, 0x44, 0x24, 0x04, 0x8b, 0x54, 0x24, 0x08),
    HOOK(0x609d20, dialog_set_def_font, 0x8b, 0x44, 0x24, 0x04, 0x85, 0xc0),
    HOOK(0x609d50, dialog_set_def_text_color, 0x8b, 0x44, 0x24, 0x14, 0x85, 0xc0),
    HOOK(0x611600, sprite_box_id_to_pos, 0x8b, 0x41, 0x2c, 0x33, 0xd2, 0x53),
    HOOK(0x611640, sprite_box_pos_to_id, 0x56, 0x8b, 0xf1, 0x57, 0x8b, 0x7c, 0x24, 0x0c),
    HOOK(0x6115e0, sprite_box_on_dialog_focus, 0x8b, 0x81, 0x74, 0xff, 0xff, 0xff),
    HOOK(0x611150, sprite_box_on_left_down, 0x8b, 0x54, 0x24, 0x04, 0x56, 0x8b, 0xf1),
    HOOK(0x6111a0, sprite_box_on_left_up, 0x8b, 0x54, 0x24, 0x04, 0x56, 0x8b, 0xf1),
    HOOK(0x6111f0, sprite_box_on_right_click, 0x8b, 0x54, 0x24, 0x04, 0x56, 0x8b, 0xf1),
    HOOK(0x611240, sprite_box_on_right_down, 0x8b, 0x54, 0x24, 0x04, 0x56, 0x8b, 0xf1),
    HOOK(0x611290, sprite_box_on_right_up, 0x8b, 0x54, 0x24, 0x04, 0x56, 0x8b, 0xf1),
    HOOK(0x6112e0, sprite_box_on_left_double_click, 0x8b, 0x54, 0x24, 0x04, 0x56, 0x8b, 0xf1),
    HOOK(0x611330, sprite_box_on_right_double_click, 0x8b, 0x54, 0x24, 0x04, 0x56, 0x8b, 0xf1),
};
struct hook_module dialog_module = {"dialog", dialog_hooks, HOOK_COUNT(dialog_hooks), dialog_sweep, NULL, -1};
