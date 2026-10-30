/*
 * ed_text.c - the text viewer: the language's text table as the game
 * loaded it (messages, object names, dialogue lines, menu words), a row
 * per record, and the selected record in full, wrapped, underneath.
 */

#include <stdio.h>
#include <string.h>

#include "aitd.h"
#include "text.h"
#include "editor/ed_ui.h"

static int s_selected;
static int s_first;

static void row(int index, char *out, size_t size)
{
    const struct text_record *r = text_record_at(index);

    snprintf(out, size, "%4d  %s", r != NULL ? r->id : 0, r != NULL && r->text != NULL ? r->text : "");
}

static void text_key(int sc)
{
    ed_list_key(sc, &s_selected, text_record_count(), 10);
}

/* Breaks the text at spaces to fit the width, a line at a time. */
static void wrapped(s16 x, s16 y, s16 width, s16 bottom, const char *text)
{
    char line[256];
    size_t used = 0;

    line[0] = '\0';
    while (*text != '\0' && y + ed_line_game() <= bottom) {
        const char *end = text;
        char word[128];
        size_t n;

        while (*end != '\0' && *end != ' ') {
            end++;
        }
        n = (size_t)(end - text);
        if (n >= sizeof(word)) {
            n = sizeof(word) - 1;
        }
        memcpy(word, text, n);
        word[n] = '\0';
        if (used > 0) {
            char test[256 + 128];

            snprintf(test, sizeof(test), "%s %s", line, word);
            if (ed_text_game_width(test) > width) {
                ed_text_game(x, y, line, g_ed.white);
                y = (s16)(y + ed_line_game());
                line[0] = '\0';
                used = 0;
            }
        }
        if (used > 0 && used + 1 < sizeof(line)) {
            line[used++] = ' ';
            line[used] = '\0';
        }
        strncat(line, word, sizeof(line) - used - 1);
        used = strlen(line);
        text = *end == ' ' ? end + 1 : end;
    }
    if (used > 0 && y + ed_line_game() <= bottom) {
        ed_text_game(x, y, line, g_ed.white);
    }
}

static void text_draw(void)
{
    int count = text_record_count();
    s16 top = ed_top(), bottom = ed_bottom();
    s16 split = (s16)(top + (bottom - top) * 2 / 3);
    const struct text_record *r;
    char line[64];

    ed_clear(g_ed.black);
    if (s_selected >= count) {
        s_selected = count > 0 ? count - 1 : 0;
    }
    ed_list(0, top, (s16)(g_screen_width - 1), (s16)(split - 1), count, s_selected, &s_first, row);
    ed_rect(0, split, (s16)(g_screen_width - 1), split, g_ed.grey);
    r = text_record_at(s_selected);
    if (r != NULL) {
        snprintf(line, sizeof(line), "record %d/%d  id %d  width %d", s_selected, count,
                 r->id, r->width);
        ed_text(3, (s16)(split + 2), line, g_ed.accent);
        if (r->text != NULL) {
            wrapped(3, (s16)(split + 2 + ed_line()), (s16)(g_screen_width - 6), bottom, r->text);
        }
    }
    ed_bars(3, "Up/Dn PgUp/PgDn record  Tab next");
}

const struct ed_viewer ed_view_texts = {
    "Texts", NULL, NULL, text_key, text_draw
};
