/*
 * text.c - the text table behind text_get_by_id (0x414fcb).
 *
 * The table is a flat array of 8 byte records, scanned linearly for a
 * matching id, with a hard bound of 500 entries (0x414ff5):
 *
 *   +0 u16   id
 *   +2 char* text
 *   +6 s16   pixel width, used by text_draw_centered to centre the string
 *
 * The records are built from a container entry holding lines of the form
 *   @<id>:<text><CR><LF>
 * Ids are sparse and the file is grouped by purpose; the menu entries are
 * 11..14, which is what menu_draw_entry asks for (index + 0x0b).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aitd.h"
#include "font.h"
#include "pak.h"
#include "text.h"

/* 0x414ff5 bounds the scan at 0x1f4 records. */
#define TEXT_RECORD_MAX 500

static struct text_record s_records[TEXT_RECORD_MAX];  /* 0x444ac4 */
static int s_record_count;
static char *s_blob;

/*
 * Parses the @id:text form in place: the blob is kept, each line's terminator
 * is replaced with a NUL and the record points into it. That mirrors the
 * original, whose records hold pointers rather than copies.
 */
static void parse_records(char *text)
{
    char *p = text;

    s_record_count = 0;

    while (*p != '\0' && s_record_count < TEXT_RECORD_MAX) {
        char *end;
        int id;

        if (*p != '@') {
            p++;
            continue;
        }

        p++;                       /* past '@' */
        id = (int)strtol(p, &end, 10);

        if (end == p || *end != ':') {
            continue;              /* not a record header after all */
        }

        p = end + 1;               /* past ':' */

        end = p;
        while (*end != '\0' && *end != '\r' && *end != '\n') {
            end++;
        }

        if (*end != '\0') {
            *end = '\0';
            end++;
        }

        s_records[s_record_count].id = (u16)id;
        s_records[s_record_count].text = p;
        s_records[s_record_count].width = (s16)font_string_width(p);
        s_record_count++;

        p = end;
    }
}

/*
 * 0x414e53 - builds the table. The container entry chosen here is the one
 * carrying the menu ids; the game ships several, one per language, and which
 * index is selected is not established yet.
 */
int text_table_load(const char *pak_name, int index)
{
    u32 size = 0;
    void *block;

    text_table_free();

    block = pak_load(pak_name, index, &size);
    if (block == NULL || size == 0) {
        mem_free(block);
        return 0;
    }

    /* The entry is not NUL terminated on disk, so grow it by one. */
    s_blob = mem_alloc(size + 1);
    if (s_blob == NULL) {
        mem_free(block);
        return 0;
    }

    memcpy(s_blob, block, size);
    s_blob[size] = '\0';
    mem_free(block);

    parse_records(s_blob);
    return s_record_count;
}

void text_table_free(void)
{
    mem_free(s_blob);
    s_blob = NULL;
    s_record_count = 0;
}

int text_record_count(void)
{
    return s_record_count;
}

/*
 * 0x414fcb - linear scan, NULL when the id is absent.
 */
const struct text_record *text_get_record(int id)
{
    int i;

    for (i = 0; i < s_record_count && i < TEXT_RECORD_MAX; i++) {
        if (s_records[i].id == (u16)id) {
            return &s_records[i];
        }
    }

    return NULL;
}

const char *text_get_by_id(int id)
{
    const struct text_record *record = text_get_record(id);

    return (record != NULL) ? record->text : "";
}
