/*
 * reader.c - documents, letters and books read on screen.
 *
 *   text_reader     0x4156a6  page through a TEXTES.PAK entry
 *   read_document   0x4160b6  opcode 33: a letter, book or note picture
 *   read_book       0x40584e  opcode 98, inline in the life interpreter
 *
 * Text markup: words are separated by spaces and line ends; an empty line
 * ends a paragraph (7 extra pixels); 0x1a ends the text. '#' commands:
 * #P new page (unless at the top), #C centre the line, #T a tab, #X<n>
 * a CD voice track. Lines are justified unless they end a paragraph.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aitd.h"
#include "cdimage.h"
#include "anim.h"
#include "draw.h"
#include "font.h"
#include "hyb.h"
#include "life.h"
#include "menu.h"
#include "palette.h"
#include "platform.h"
#include "game.h"
#include "picture.h"
#include "raster.h"
#include "subsystems.h"

enum reader_mode {
    READER_ARROWS = 0,      /* arrows beside the text, keys turn pages */
    READER_AUTO = 1,        /* pages turn by themselves */
    READER_ARROWS_LOW = 2   /* arrows under the text, silent, Enter quits */
};

/* read_document types (opcode 33). */
enum document_type {
    DOCUMENT_NOTE = 0,
    DOCUMENT_BOOK = 1,
    DOCUMENT_LETTER = 2
};

enum line_flag {
    LINE_JUSTIFY = 1,
    LINE_PARAGRAPH = 2,
    LINE_END = 4,
    LINE_CENTRE = 8
};

enum {
    LINE_PITCH = 14,
    PARAGRAPH_GAP = 7,
    WORD_GAP = 3,
    PAGE_MAX = 77,
    WORD_MAX = 80,
    ARROW_NEXT = 1,
    ARROW_PREV = 2
};

s16 g_page_turn_style;   /* 0x4350a5: 1 book, 3 fade, 0 none */
s16 g_voice_allowed;     /* 0x4350e3 */

struct word {
    const char *text;
    s16 width;
};

static const char s_tab[] = "    ";   /* 0x42f3fe */

/* 0x402fc4 */
static int sample_busy(void)
{
    return platform_sound_playing();
}

/* The sample list of opcode 97 plays one after another while reading. */
static void next_sample(u8 *index)
{
    if (*index < g_cutscene_sample_count && !sample_busy()) {
        sample_play(g_cutscene_samples[(*index)++ * 2 + 1]);
    }
}

/*
 * The CD release of AITD1 reads the character stories and the documents
 * aloud: page p of voice v is a run of Creative Voice files
 * "<v><p><n>.VOC" (two digits each, n from 0), played one after another
 * (INDARK.EXE reader 0x101f6, names built at 0x101cd). The voice comes with
 * the reading: 0 for Carnby's story, 1 for Emily's, the third word of
 * READ for a document, -1 for none.
 */
s16 g_reader_voc = -1;
static s16 s_voc_page = -1, s_voc_next;

/* 8 bit unsigned samples of a VOC file's sound blocks (types 1 and 2). */
static int voc_play(s16 voice, s16 page, s16 part)
{
    char name[16];
    u8 *file, *pcm;
    u32 size = 0, at, out = 0, rate = 11025;

    snprintf(name, sizeof(name), "%02d%02d%02d.VOC", voice, page, part);
    /* The original reads the speech from the CD (its path names the CD
     * drive), so a disc image wins over files in the data folder: an
     * installation from another language's CD keeps its own voices. */
    file = cdimage_read_file(name, &size);
    if (file == NULL) {
        size = file_size(name);
        file = size > 0 ? load_malloc(name) : NULL;
    }
    if (file == NULL) {
        return 0;
    }
    pcm = mem_alloc_checked(size);
    at = size >= 0x1a ? (u32)(file[0x14] | (file[0x15] << 8)) : size;
    while (at + 4 <= size && file[at] != 0) {
        u8 type = file[at];
        u32 len = (u32)file[at + 1] | ((u32)file[at + 2] << 8) | ((u32)file[at + 3] << 16);
        const u8 *data = file + at + 4;

        if (at + 4 + len > size) {
            len = size - at - 4;
        }
        if (type == 1 && len >= 2) {
            rate = 1000000u / (256u - data[0]);
            memcpy(pcm + out, data + 2, len - 2);
            out += len - 2;
        } else if (type == 2) {
            memcpy(pcm + out, data, len);
            out += len;
        }
        at += 4 + len;
    }
    if (out > 0) {
        platform_sound_play(pcm, out, rate, 1);
    }
    if (getenv("AITD_TRACE") != NULL) {
        fprintf(stderr, "voice %s: %u samples at %u Hz\n", name, out, rate);
    }
    mem_free(pcm);
    mem_free(file);     /* mem_free is free, for both sources */
    return out > 0;
}

static void voc_page(s16 page)
{
    if (g_reader_voc < 0 || game_id() != GAME_AITD1) {
        return;
    }
    platform_sound_stop();
    s_voc_page = page;
    s_voc_next = 0;
    if (voc_play(g_reader_voc, page, s_voc_next)) {
        s_voc_next++;
    } else {
        s_voc_page = -1;
    }
}

/* The next file of the page once the current one ended. */
static int voc_service(void)
{
    if (s_voc_page < 0) {
        return 0;
    }
    if (!sample_busy()) {
        if (voc_play(g_reader_voc, s_voc_page, s_voc_next)) {
            s_voc_next++;
        } else {
            s_voc_page = -1;
            return 0;
        }
    }
    return 1;
}

static void keep_samples_going(u8 *index)
{
    if (voc_service()) {
        return;
    }
    if (!sample_busy()) {
        g_sample_priority = -1;
        g_sample_current = -1;
        next_sample(index);
    }
}

static void voice_play(s16 track)
{
    platform_cdaudio_play_track(g_voice_cfg_value + track);   /* 0x40110a */
}

static void page_sound(void)
{
    sample_play((s16)g_defines_itd[0]);
    g_sample_current = -1;
}

/* 0x4156a6 - eax text, edx xmin, ebx ymin, ecx xmax, then ymax, mode,
 * colour and shadow colour (-1: no shadow, fade the first page in).
 * Returns the mode, cleared when an automatic reading ran to the end. */
s16 text_reader(s16 text, s16 xmin, s16 ymin, s16 xmax, s16 ymax, s16 mode,
                s16 colour, s16 shadow)
{
    struct raster_image back = raster_image_make(g_screen_width, g_screen_height, g_back_buffer);
    struct word words[WORD_MAX];
    char *pages[PAGE_MAX];
    char *buffer;
    struct game_timer timer = { 0 };
    s16 width = (s16)(xmax - xmin + 4);
    s16 page = 0;
    s16 previous = -1;
    s16 voice = 0;
    int voice_on = 0;
    int first = 1;
    int quit = 0;
    u8 sample_index = 0;

    buffer = file_load_alloc(text_pak_name(), (u16)text);
    if (buffer == NULL) {
        fatal_error(1, text_pak_name());
    }
    pages[0] = buffer;
    g_sample_current = -1;
    g_sample_priority = -1;
    next_sample(&sample_index);

    while (!quit) {
        char *p = pages[page];
        s16 y = ymin;
        int end = 0;

        raster_blit(0, 0, &back);
        raster_set_clip((s16)(xmin - 1), (s16)(ymin - 1), (s16)(xmax + 1), (s16)(ymax + 1));
        if (shadow == -1) {
            screen_present();
            palette_fade(0x10, 0);
            timer_reset(&timer);
            raster_rect((s16)(xmin - 3), (s16)(xmax + 3), (s16)(ymin - 3), (s16)(ymax + 3), 0);
            first = 0;
        }

        while (y <= ymax - LINE_PITCH) {
            int flags = LINE_JUSTIFY;
            s16 line_width = 0;
            s16 count = 0;
            s16 extra = 0;
            s16 x;
            int i;

            for (;;) {
                char *start;
                char c;
                s16 w;

                while (*p == '#') {
                    char command = p[1];

                    p += 2;
                    switch (command) {
                    case 'X':
                    case 'x': {
                        char digits[12];
                        int n = 0;

                        while (*p >= '0' && *p <= '9' && n < 11) {
                            digits[n++] = *p++;
                        }
                        digits[n] = '\0';
                        voice = (s16)atoi(digits);
                        break;
                    }
                    case 'P':
                    case 'p':
                        if (y > ymin) {
                            goto page_done;
                        }
                        break;
                    case 'C':
                    case 'c':
                        flags = (flags & ~LINE_JUSTIFY) | LINE_CENTRE;
                        break;
                    case 'T':
                    case 't':
                        if (count < WORD_MAX) {
                            words[count].text = s_tab;
                            words[count].width = (s16)(font_string_width(s_tab) + WORD_GAP);
                            line_width = (s16)(line_width + words[count].width);
                            count++;
                        }
                        break;
                    default:
                        break;
                    }
                }

                start = p;
                do {
                    c = *p++;
                } while ((u8)c > 0x20);
                p[-1] = '\0';
                w = (s16)(font_string_width(start) + WORD_GAP);

                if (w > width) {
                    quit = 1;   /* a word wider than the page */
                    break;
                }
                if (line_width + w > width || count >= WORD_MAX) {
                    p = start;  /* goes to the next line */
                    break;
                }
                words[count].text = start;
                words[count].width = w;
                line_width = (s16)(line_width + w);
                count++;

                if (c == 0x1a) {
                    flags = (flags & ~LINE_JUSTIFY) | LINE_END;
                    end = 1;
                    break;
                }
                if ((c == '\r' || c == '\0') && (u8)*p < 0x20) {
                    p++;
                    if (*p == '\r') {
                        p += 2;
                        flags = (flags & ~LINE_JUSTIFY) | LINE_PARAGRAPH;
                        break;
                    }
                    if (*p == '#') {
                        flags &= ~LINE_JUSTIFY;
                        break;
                    }
                }
            }

            /* 0x415a8e. A lone justified word would divide by zero in the
             * original. */
            if ((flags & LINE_JUSTIFY) && count > 1) {
                extra = (s16)((width - line_width) / (count - 1));
            }
            x = (flags & LINE_CENTRE) ? (s16)(xmin + (width - line_width) / 2) : xmin;
            for (i = 0; i < count; i++) {
                if (colour != shadow && shadow != -1) {
                    text_draw_string(x, (s16)(y + 1), words[i].text, (u8)shadow);
                }
                text_draw_string(x, y, words[i].text, (u8)colour);
                x = (s16)(x + words[i].width + extra);
            }
            if (flags & LINE_PARAGRAPH) {
                y = (s16)(y + PARAGRAPH_GAP);
            }
            y = (s16)(y + LINE_PITCH);
            if (end) {
                break;
            }
        }

page_done:
        if (end) {
            p[-1] = 0x1a;   /* put the terminator back for a later visit */
        } else if (page + 1 < PAGE_MAX) {
            pages[page + 1] = p;
        }
        raster_reset_clip();

        if (mode == READER_ARROWS && game_current()->hyb_frame) {
            if (page > 0) {
                hyb_render(ARROW_PREV, (s16)(xmin - 19), 185, engine_resource(ITD_RESS_UI_SPRITES));
            }
            if (!end) {
                hyb_render(ARROW_NEXT, (s16)(xmax + 4), 185, engine_resource(ITD_RESS_UI_SPRITES));
            }
        }
        if (mode == READER_ARROWS_LOW && game_current()->hyb_frame) {
            if (page > 0) {
                hyb_render(ARROW_PREV, (s16)(xmin - 3), 191, engine_resource(ITD_RESS_UI_SPRITES));
            }
            if (!end) {
                hyb_render(ARROW_NEXT, (s16)(xmax - 10), 191, engine_resource(ITD_RESS_UI_SPRITES));
            }
        }

        if (first) {
            input_poll();
            while (timer_elapsed(&timer) < 120 && !(g_last_scancode & 0xff) && !g_key_confirm) {
                keep_samples_going(&sample_index);
                input_poll();
            }
            screen_present();   /* 0x41565e / 0x415682 page flips only present */
            if (mode != READER_AUTO && g_page_turn_style) {
                palette_fade(0x10, 0);
            }
            first = 0;
            voice_on = g_voice_allowed && voice;
            if (voice_on) {
                voice_play(voice);
            }
        } else {
            screen_present();
            if (voice_on) {
                voice_play(voice);
            }
        }
        voc_page(page);
        (void)previous;

        if (mode == READER_AUTO) {
            timer_reset(&timer);
            input_poll();
            while (timer_elapsed(&timer) < 300 && !(g_last_scancode & 0xff) && !g_key_confirm) {
                keep_samples_going(&sample_index);
                input_poll();
            }
            if ((g_last_scancode & 0xff) || g_key_confirm) {
                quit = 1;
            }
            if (end) {
                quit = 1;
                mode = 0;
            } else {
                page++;
                page_sound();
            }
            continue;
        }

        /* Wait for the keys that turned the page to come up. */
        do {
            input_poll();
            keep_samples_going(&sample_index);
        } while ((g_last_scancode & 0xff) || g_key_dir_flags || g_key_confirm);

        for (;;) {
            input_poll();
            keep_samples_going(&sample_index);
            g_scene_scancode = (s16)(g_last_scancode & 0xff);
            g_scene_dir_flags = (s16)g_key_dir_flags;
            g_scene_confirm = (s16)g_key_confirm;

            if (g_scene_scancode == SC_ESC || g_scene_confirm) {
                quit = 1;
                break;
            }
            if (mode == READER_ARROWS_LOW && g_scene_scancode == SC_ENTER) {
                quit = 1;
                break;
            }
            if ((g_key_dir_flags & (DIR_DOWN | DIR_RIGHT)) || g_scene_scancode == SC_ENTER) {
                if (!end) {
                    previous = page++;
                    if (mode != READER_ARROWS_LOW) {
                        page_sound();
                        g_sample_priority = -1;
                    }
                    break;
                }
                if (g_scene_scancode == SC_ENTER) {
                    quit = 1;
                    break;
                }
            }
            if ((g_key_dir_flags & (DIR_UP | DIR_LEFT)) && page > 0) {
                previous = page--;
                if (mode != READER_ARROWS_LOW) {
                    page_sound();
                    g_sample_priority = -1;
                }
                break;
            }
        }
    }

    if (s_voc_page >= 0 || g_reader_voc >= 0) {
        platform_sound_stop();
        s_voc_page = -1;
    }
    g_reader_voc = -1;
    if (voice_on) {
        platform_cdaudio_stop();   /* 0x4011ce */
        if (g_music_pending != -1) {
            music_play(g_music_pending);
        }
    }
    g_sample_pending = -1;
    mem_free(buffer);
    return mode;
}

/* Background and palette for a reading: an ITD_RESS picture, with the
 * interface colours over its first 16 entries when the title has them.
 * Alone in the Dark 1 pictures carry no palette and keep the game's. */
void reader_set_backdrop(s16 entry)
{
    const u8 *overlay = game_current()->palette_overlay;

    if (picture_load(entry, g_back_buffer, g_back_buffer + SCREEN_PIXELS) == 1) {
        memcpy(g_palette_buffer, g_back_buffer + SCREEN_PIXELS, 0x300);
        if (overlay != NULL) {
            memcpy(g_palette_buffer, overlay, 0x30);
            if (game_current()->palette_source == PALETTE_CAMERA_6BIT) {
                int i;
                for (i = 0; i < 0x30; i++) {
                    g_palette_buffer[i] = (u8)((overlay[i] * 255u) / 63u);
                }
            }
        }
    } else {
        memcpy(g_back_buffer + SCREEN_PIXELS, g_palette_buffer, 0x300);
    }
}

/* Where each title draws a document: backdrop entry, text box, page turn
 * style, colours. AITD3 from 0x4160b6, the others from the data. */
struct document_layout {
    s16 entry;
    s16 xmin, ymin, xmax, ymax;
    s16 turn_style;
    s16 colour, shadow;
};

static const struct document_layout *document_layout(s16 type)
{
    static const struct document_layout aitd1[3] = {
        { 6, 60, 10, 245, 190, 3, 26, 0 },
        { 7, 48, 2, 260, 197, 1, 26, 0 },
        { 8, 50, 20, 250, 199, 3, 26, 0 },
    };
    static const struct document_layout aitd2[3] = {
        { 2, 60, 10, 245, 190, 3, 124, 124 },
        { 3, 60, 10, 245, 190, 1, 124, 124 },
        { 4, 60, 10, 245, 190, 3, 124, 124 },
    };
    static const struct document_layout jack[3] = {
        { 2, 60, 10, 245, 190, 1, 124, 124 },
        { 2, 60, 10, 245, 190, 1, 124, 124 },
        { 2, 60, 10, 245, 190, 1, 124, 124 },
    };
    static const struct document_layout aitd3[3] = {
        { ITD_RESS_NOTE, 60, 10, 245, 190, 3, 124, 124 },
        { ITD_RESS_BOOK, 48, 2, 260, 197, 1, 124, 124 },
        { ITD_RESS_LETTER, 50, 20, 250, 199, 3, 124, 124 },
    };
    /* TG320.EX_ 0x40b278: the hands holding a note, then two full screen
     * pages (a book, a newspaper). */
    static const struct document_layout timegate[3] = {
        { 6, 60, 10, 245, 190, 3, 124, 124 },
        { 7, 0, 0, 319, 199, 1, 124, 124 },
        { 8, 0, 0, 319, 199, 3, 124, 124 },
    };
    const struct document_layout *table;

    switch (game_id()) {
    case GAME_TIMEGATE: table = timegate; break;
    case GAME_AITD1: table = aitd1; break;
    case GAME_AITD2: table = aitd2; break;
    case GAME_JACK: table = jack; break;
    default: table = aitd3; break;
    }
    return (type >= 0 && type < 3) ? &table[type] : NULL;
}

/* 0x4160b6 - opcode 33. */
void read_document(s16 text, s16 type)
{
    const struct document_layout *d = document_layout(type);
    s16 music;

    if (getenv("AITD_SKIP_DOCUMENTS") != NULL) {
        return;     /* harness only: unattended runs never wait on a page */
    }
    ticks_freeze();
    g_voice_allowed = 1;
    if (d != NULL) {
        reader_set_backdrop(d->entry);
        g_page_turn_style = d->turn_style;
        text_reader(text, SCALED(d->xmin), SCALED(d->ymin),
                    (s16)(SCALED(d->xmax + 1) - 1), (s16)(SCALED(d->ymax + 1) - 1), READER_ARROWS,
                    d->colour, d->shadow);
    }
    g_voice_allowed = 1;
    music = g_music_pending;
    g_music_pending = -1;
    ticks_unfreeze();
    if (g_cdaudio_ready) {
        music_play(music);
    }
}

/* 0x40584e - opcode 98: picture, text, xmin, ymin, xmax, ymax, colour,
 * shadow. */
void read_book(const s16 *args)
{
    struct raster_image back = raster_image_make(g_screen_width, g_screen_height, g_back_buffer);

    ticks_freeze();
    memcpy(g_back_buffer, g_screen, SCREEN_PIXELS);   /* 0x402c8b */
    screen_clear();
    screen_present();
    reader_set_backdrop(args[0]);
    palette_set_range(g_palette_buffer, 0, 256);
    g_page_turn_style = 0;
    raster_blit(0, 0, &back);
    screen_present();
    text_reader((s16)(args[1] + 1), args[2], args[3], args[4], args[5],
                READER_ARROWS, args[6], args[7]);
    g_init_view = 2;
    ticks_unfreeze();
}
