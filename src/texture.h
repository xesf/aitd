/*
 * texture.h - Time Gate's textured and Gouraud shaded triangles, see
 * texture.c.
 */

#ifndef AITD_TEXTURE_H
#define AITD_TEXTURE_H

#include "aitd.h"

/* One triangle corner: screen position, texel and light level. */
struct tex_vertex {
    s16 x, y;
    s16 u, v;
    s16 level;
};

enum tex_fill {
    TEX_FILL_TEXTURE,       /* primitive 8: texels, 0 is transparent */
    TEX_FILL_GOURAUD,       /* primitive 9: shade[colour][level] */
    TEX_FILL_TEXTURE_LIT    /* primitive 10: shade[texel][level] */
};

struct tex_page {
    const u8 *texels;       /* 256 texels per row */
    u32 size;
};

/* 0x41fe2f - TEXTURES.PAK entry `index` through the 40 slot cache. `keep`
 * is never evicted (the other page of the same body). */
struct tex_page texture_page(int index, const u8 *keep);

/* 0x417cfe - the 64 level shade table, shade[colour * 64 + level]. */
const u8 *texture_shade_table(void);

/* 0x411e2f - light level 0..63 of a camera space point. */
s16 texture_light_level(const s16 *point, const s16 *dir, const s16 *pos);

/* 0x41bc2c / 0x412e4f / 0x4133bc - the high detail fillers. */
void texture_triangle(const struct tex_vertex *v, enum tex_fill fill,
                      const struct tex_page *page, int colour);

#endif /* AITD_TEXTURE_H */
