/*
 * hyb.h - 2D vector sprites (0x424ff8 renderer, 0x40706b bounding box).
 */

#ifndef AITD_HYB_H
#define AITD_HYB_H

#include "aitd.h"

/* Element types of a hybrid sprite (0x424ff8). */
enum hyb_element {
    HYB_SUB_SPRITE = 0,
    HYB_BITMAP = 1,
    HYB_POLYGON = 4,
    HYB_RECT = 5,
    HYB_POLYLINE = 6,
    HYB_POINTS = 7
};

/* Bit 15 of index mirrors the sprite horizontally. */
void hyb_render(int index, s16 x, s16 y, const u8 *data);
/* box = xmin, ymin, xmax, ymax; returns 0 for an empty sprite. */
int hyb_bbox(s16 *box, int index, const u8 *data);
int hyb_anim_sprite(const u8 *data, int anim, int frame);

#endif /* AITD_HYB_H */
