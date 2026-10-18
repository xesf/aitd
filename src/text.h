/*
 * text.h - text table, see text.c. Record layout comes from text_get_by_id
 * (0x414fcb) and the two centred-draw routines (0x4150bb, 0x41502f).
 */

#ifndef AITD_TEXT_H
#define AITD_TEXT_H

#include "aitd.h"

struct text_record {
    u16 id;        /* +0 */
    char *text;    /* +2 */
    s16 width;     /* +6, pixel width used for centring */
};

int text_table_load(const char *pak_name, int index);  /* 0x414e53 */
void text_table_free(void);
int text_record_count(void);

const struct text_record *text_get_record(int id);     /* 0x414fcb */

#endif /* AITD_TEXT_H */
