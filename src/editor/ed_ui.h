/*
 * ed_ui.h - what the editor's viewers share: the viewer interface, keys,
 * colours picked from the palette on screen, and text and list drawing in
 * the title's own font (ed_ui.c).
 */

#ifndef AITD_ED_UI_H
#define AITD_ED_UI_H

#include "aitd.h"

/* A key press as the viewers see it: the scancode (PC set 1), repeated
 * while the key is held. */
enum {
    ED_KEY_UP = 0xc8,
    ED_KEY_DOWN = 0xd0,
    ED_KEY_LEFT = 0xcb,
    ED_KEY_RIGHT = 0xcd,
    ED_KEY_PGUP = 0xc9,
    ED_KEY_PGDN = 0xd1,
    ED_KEY_HOME = 0xc7,
    ED_KEY_END = 0xcf,
    ED_KEY_ENTER = 0x1c,
    ED_KEY_SPACE = 0x39,
    ED_KEY_MINUS = 0x0c,
    ED_KEY_PLUS = 0x0d         /* the = / + key */
};

struct ed_viewer {
    const char *name;
    void (*open)(void);         /* NULL: nothing to load */
    void (*close)(void);        /* NULL: nothing to free */
    void (*key)(int scancode);
    void (*draw)(void);         /* draws the whole screen, under the bars */
};

extern const struct ed_viewer ed_view_pictures;
extern const struct ed_viewer ed_view_models;
extern const struct ed_viewer ed_view_fonts;
extern const struct ed_viewer ed_view_texts;
extern const struct ed_viewer ed_view_life;

/* The viewers in their order on the top bar (ed_ui.c). */
extern const struct ed_viewer *const g_ed_viewers[];
extern const int g_ed_viewer_count;

/* Colours of the palette on screen nearest to the editor's own. */
struct ed_colours {
    u8 black, white, grey, dim, accent, select;
};

extern struct ed_colours g_ed;

/* The palette the editor found on screen (the game's or the menu's). */
extern u8 g_ed_game_palette[AITD_PALETTE_BYTES];

/* Sets the palette on screen and picks the editor's colours from it. */
void ed_use_palette(const u8 *palette);

/* The top bar (title and viewers, the current one lit) and the bottom
 * bar (the viewer's help); the space between is ed_top()..ed_bottom(). */
void ed_bars(int current, const char *help);
s16 ed_top(void);
s16 ed_bottom(void);
s16 ed_line(void);              /* height of a line of text */

void ed_clear(u8 colour);
void ed_rect(s16 x1, s16 y1, s16 x2, s16 y2, u8 colour);
void ed_text(s16 x, s16 y, const char *text, u8 colour);
s16 ed_text_width(const char *text);

/* A scrolling list of `count` rows in x1..x2, y1..y2: `row` writes the
 * text of a row. Keeps *first so that `selected` stays in view. */
void ed_list(s16 x1, s16 y1, s16 x2, s16 y2, int count, int selected, int *first,
             void (*row)(int index, char *out, size_t size));

/* Moves a selection for a list key; returns 1 when the key was a list key. */
int ed_list_key(int scancode, int *selected, int count, int page);

#endif /* AITD_ED_UI_H */
