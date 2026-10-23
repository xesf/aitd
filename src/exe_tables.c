/*
 * exe_tables.c - the constant tables of the WAITD3.EXE renderer.
 *
 * The repository ships no bytes of the original program, so the tables
 * are rebuilt here:
 *
 *   g_sin_table  0x439abc  sin(a) * 32767 truncated toward zero, except
 *                          that entry 0 holds 4. cos(a) = sin(a + 256).
 *   g_poly_ramp  0x42f988  per span width w (pixels w + 1), the shade of
 *                          each pixel when there are fewer than 16 pixels
 *                          (k * (4096 / (w + 1)) >> 8, with entry 6, 5
 *                          set to 12), else the run length of each of the
 *                          16 shades: (w + 1) / 16 each, and the remainder
 *                          handed out in bit-reversed order from shade 15.
 *   g_poly_noise 0x401000  the noise fill (0x402246) reads the program's
 *                          own code bytes from 0x401acc. When WAITD3.EXE is
 *                          next to the game data those bytes are read from
 *                          it; otherwise a fixed pseudo-random sequence
 *                          stands in, and the noise pattern differs.
 *
 * The rebuilt sine and ramp tables match the originals entry for entry.
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "exe_tables.h"
#include "platform.h"

/* M_PI is POSIX, not C99. */
#ifndef PI
#define PI 3.14159265358979323846
#endif

s16 g_sin_table[1024];
u8 g_poly_noise[0x10800];
u8 g_poly_ramp[16 * 321];

enum { NOISE_VA = 0x401000, IMAGE_BASE = 0x400000 };

static void build_sine(void)
{
    int i;

    for (i = 0; i < 1024; i++) {
        g_sin_table[i] = (s16)(32767.0 * sin((double)i * 2.0 * PI / 1024.0));
    }
    g_sin_table[0] = 4;
}

static int reverse4(int v)
{
    return ((v & 1) << 3) | ((v & 2) << 1) | ((v & 4) >> 1) | ((v & 8) >> 3);
}

static void build_ramp(void)
{
    int w, k;

    memset(g_poly_ramp, 0, sizeof(g_poly_ramp));
    for (w = 1; w < 321; w++) {
        u8 *row = g_poly_ramp + w * 16;
        int n = w + 1;

        if (n < 16) {
            int step = 4096 / n;
            for (k = 0; k < n; k++) {
                row[k] = (u8)((k * step) >> 8);
            }
        } else {
            for (k = 0; k < 16; k++) {
                row[k] = (u8)(n / 16);
            }
            for (k = 0; k < n % 16; k++) {
                row[15 - reverse4(k)]++;
            }
        }
    }
    g_poly_ramp[6 * 16 + 5] = 12;
}

static u32 rd32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

/* The code bytes at NOISE_VA from the user's own WAITD3.EXE. */
static int load_noise(void)
{
    char path[1024];
    u8 header[0x400];
    FILE *fp;
    u32 pe, sections, optional, i;
    int ok = 0;

    if (!platform_resolve_path("WAITD3.EXE", path, sizeof(path))
        || (fp = fopen(path, "rb")) == NULL) {
        return 0;
    }
    if (fread(header, 1, sizeof(header), fp) == sizeof(header)
        && (pe = rd32(header + 0x3c)) + 24 < sizeof(header)
        && memcmp(header + pe, "PE\0\0", 4) == 0) {
        sections = (u32)(header[pe + 6] | (header[pe + 7] << 8));
        optional = (u32)(header[pe + 20] | (header[pe + 21] << 8));
        for (i = 0; i < sections; i++) {
            const u8 *s = header + pe + 24 + optional + i * 40;
            u32 va, raw_size, raw;

            if (s + 40 > header + sizeof(header)) {
                break;
            }
            va = IMAGE_BASE + rd32(s + 12);
            raw_size = rd32(s + 16);
            raw = rd32(s + 20);
            if (va <= NOISE_VA && NOISE_VA + sizeof(g_poly_noise) <= va + raw_size) {
                ok = fseek(fp, (long)(raw + NOISE_VA - va), SEEK_SET) == 0
                     && fread(g_poly_noise, 1, sizeof(g_poly_noise), fp)
                            == sizeof(g_poly_noise);
                break;
            }
        }
    }
    fclose(fp);
    return ok;
}

static void build_noise(void)
{
    u32 seed = 0x2b6c5f1du;
    size_t i;

    if (load_noise()) {
        return;
    }
    for (i = 0; i < sizeof(g_poly_noise); i++) {
        seed = seed * 1103515245u + 12345u;
        g_poly_noise[i] = (u8)(seed >> 16);
    }
}

void exe_tables_init(void)
{
    build_sine();
    build_ramp();
    build_noise();
}
