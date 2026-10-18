/*
 * aitd.h - Alone in the Dark 3 (WAITD3.EXE, Win32/Watcom build, 1996)
 *
 * Reconstructed engine interface. Every declaration carries the address of
 * the routine or global it was derived from in the IDA database
 * (WAITD3.EXE.id0, image base 0x400000).
 *
 * The original build uses the Watcom register calling convention
 * (eax, edx, ebx, ecx then stack). Argument order in this header follows
 * that register order so the mapping back to the disassembly stays obvious.
 */

#ifndef AITD_H
#define AITD_H

#include <stddef.h>
#include <stdint.h>

typedef int8_t s8;
typedef int16_t s16;
typedef uint16_t u16;
typedef int32_t s32;
typedef uint32_t u32;
typedef uint64_t u64;
typedef uint8_t u8;

/* Native screen geometry. engine_init reads these from g_video_mode_info
 * (0x435160): word[0] -> width, word[1] -> height. The window is created
 * 0x140 x 0xC8, so both are 320x200 in practice. */
#define AITD_SCREEN_WIDTH 320
#define AITD_SCREEN_HEIGHT 200

/* Frame buffers are allocated as w*h + 0x302 bytes (0x4074 83..0x4074b4):
 * the pixel plane followed by a 768 byte VGA palette and a 2 byte tail. */
#define AITD_PALETTE_BYTES 768
#define AITD_FRAME_EXTRA 0x302

/* Result codes returned by main_menu_run (0x408f34), dispatched by
 * game_main at 0x4079c5..0x407b16. */
enum menu_result {
    MENU_RESULT_INTRO = -1,     /* 0x4079cc: replay intro sequence */
    MENU_RESULT_NEW_GAME = 0,   /* 0x407a0f */
    MENU_RESULT_LOAD_GAME = 1,  /* 0x407a67 */
    MENU_RESULT_DIFFICULTY = 2, /* 0x4079ea, text id 13 is the difficulty label */
    MENU_RESULT_QUIT = 3        /* 0x407af6 */
};

/* Entries of ITD_RESS.PAK. Pictures are 4 header bytes, 64000 pixels and a
 * 768 byte palette. The inventory art comes in three sets, one per player
 * type (DEFINES word 14). */
enum itd_ress {
    ITD_RESS_UI_SPRITES = 0x00,     /* hybrid sprites: page arrows, ... */
    ITD_RESS_FONT = 0x01,
    ITD_RESS_NOTE = 0x02,           /* document backdrops, read_document */
    ITD_RESS_BOOK = 0x03,
    ITD_RESS_LETTER = 0x04,
    ITD_RESS_MENU = 0x0d,
    ITD_RESS_INVENTORY_1 = 0x0e,
    ITD_RESS_INVENTORY_0 = 0x0f,
    ITD_RESS_INVENTORY_2 = 0x10,
    ITD_RESS_ESCAPE_MENU = 0x11,
    ITD_RESS_INVENTORY_FRAME_1 = 0x12,
    ITD_RESS_INVENTORY_FRAME_0 = 0x13,
    ITD_RESS_INVENTORY_FRAME_2 = 0x14,
    ITD_RESS_PICTURE_33 = 0x21,     /* loaded at start, never shown */
    ITD_RESS_MENU_PALETTE = 0x2f,   /* a bare 768 byte palette */
    ITD_RESS_MAP = 0x30,
    ITD_RESS_SAVE_SCREEN = 0x4f
};

/* Text ids passed to text_get_by_id (0x414fcb). The returned record stores
 * the string at offset +2. */
enum text_id {
    TEXT_ID_TITLE = 1,
    TEXT_ID_MUSIC_OFF = 0x29,       /* + g_cdaudio_ready */
    TEXT_ID_SOUND_OFF = 0x2b,       /* + g_sound_ready2 */
    TEXT_ID_SAVE_GAME = 0x2d,
    TEXT_ID_LOAD_GAME = 0x2e,
    TEXT_ID_QUIT = 0x2f,
    TEXT_ID_RETURN_TO_GAME = 0x30,
    TEXT_ID_DETAIL_LOW = 0x31,      /* + g_poly_detail */
    TEXT_ID_GAME_SAVED = 0x33,
    TEXT_ID_SAVE_FAILED = 0x34,
    TEXT_ID_DIFFICULTY = 0x35,
    TEXT_ID_SAVE_DIR_FIRST = 0x47,  /* California, Montana, Nevada, Oklahoma */
    TEXT_ID_LIFE_POINTS = 0x50,
    TEXT_ID_HIT_POINTS = 0x51,
    TEXT_ID_VILLAIN_POINTS = 0x52,
    TEXT_ID_PRESET_FIRST = 0x53,    /* Beginner, Intermediate, Expert, Return */
    TEXT_ID_LEVEL_FIRST = 0x5d,     /* Low, Average, High */
    TEXT_ID_WINDOW_TITLE = 0x138d,  /* 0x40726c */
    TEXT_ID_VIDEO_FAILED = 0x1388,  /* 0x4072a6 */
    TEXT_ID_CD_RETRY = 0x138b,      /* 0x407353 */
    TEXT_ID_CD_FAILED = 0x138c      /* 0x407372 */
};

/*
 * Resource cache. engine_init builds one per asset family with
 * hqr_init_resource (0x4195fc, eax=name, edx=byte_size, ebx=entries).
 *
 * The original is Adeline's lib386 HQR system: one flat arena, a directory
 * of loaded blocks, and least-recently-used eviction with compaction
 * (0x4194a0) when the arena fills. Eviction moves blocks, which is why the
 * engine re-applies an animation whenever a body comes back freshly loaded
 * (g_hqr_loaded, 0x435148).
 *
 * Here every entry is loaded once into its own allocation and kept. The
 * byte budget is recorded but not enforced: a host has the memory, and a
 * block that never moves cannot leave a stale pointer behind.
 */
struct hqr_resource {
    char name[16];
    char pak_name[32];
    u32 byte_size;      /* budget the original reserved, informational */
    s16 max_entries;    /* likewise */
    int entry_count;    /* slots in the arrays below */
    u8 **entries;
    u32 *sizes;
};

/*
 * Bitmap font. font_set_data (0x420eea) parses this header out of the blob
 * loaded from itd_ress.pak and caches the fields in globals 0x43522c..0x435228.
 */
struct font {
    u8 flags;        /* blob[0]  -> 0x43522e */
    u8 first_char;   /* blob[2]  -> 0x43522c, 32 in the shipped font */
    u8 last_char;    /* blob[3]  -> 0x43522d, 237 in the shipped font */

    /*
     * blob[4] as a word -> 0x435224. Reads 0x0e0d (3597) in the shipped font,
     * so this is not a glyph height; most likely an offset or a total size
     * within the 8761 byte blob. Unidentified.
     */
    u16 field_4;

    const u8 *glyphs; /* blob + 8 -> 0x435228, 10 bytes per glyph */
};

/* ---- globals (renamed in the IDB) ------------------------------------- */

extern s32 g_screen_width;          /* 0x444bc0 */
extern s32 g_screen_height;         /* 0x444bb0 */
extern u8 *g_front_buffer;          /* 0x435064 */
extern u8 *g_back_buffer;           /* 0x444bac */
extern u16 g_blit_width;            /* 0x444854 */
extern u16 g_blit_height;           /* 0x444856 */
extern u8 *g_blit_source;           /* 0x444858 */

extern u8 g_opt_cdaudio_enabled;    /* 0x430d9c, cleared by -c / c */
extern u8 g_opt_sound_enabled;      /* 0x430d9d, cleared by -s / s */
extern s16 g_cdaudio_ready;         /* 0x4350cb */
extern s16 g_sound_ready;           /* 0x4350d5 */
extern s16 g_sound_ready2;          /* 0x4350d7 */
extern s32 g_voice_cfg_value;       /* 0x435140, atoi of voice.cfg */

extern void *g_font_data;           /* 0x444b18 */
extern void *g_anim_buffer;         /* 0x444aa4, 0xcd00 bytes */
extern void *g_object_table;        /* 0x444974, 0x2ba6 bytes */
extern void *g_priority_itd;        /* 0x444980 */
extern u16 g_defines_itd[0x46];     /* 0x444b20..0x444bab; DEFINES.ITD fills the first 0x46 bytes */
extern u16 g_game_vars[0xfdc];      /* 0x4423a4, 0x1fb8 bytes, cleared per new game */

extern struct hqr_resource *g_cache_body;   /* 0x444a80 */
extern struct hqr_resource *g_cache_trak;   /* 0x444a84 */
extern struct hqr_resource *g_cache_hyb;    /* 0x444a88 */
extern struct hqr_resource *g_cache_raw;    /* 0x444a8c */
extern struct hqr_resource *g_cache_life;   /* 0x444a94 */
extern struct hqr_resource *g_cache_sample; /* 0x444a98 */
extern struct hqr_resource *g_cache_anim;   /* 0x444a9c */
extern struct hqr_resource *g_cache_mat;    /* 0x444aa8 */

extern s16 g_quit_requested;        /* 0x435058 */
/* DEFINES.ITD is read straight over a block of globals (0x444b20, 0x46
 * bytes); several of them have meanings of their own. */
#define g_tab_locked (*(s16 *)&g_defines_itd[4])        /* 0x444b28 */
#define g_alt_bg_flag(i) (*(s16 *)&g_defines_itd[5 + (i)]) /* 0x444b2a.. */
#define g_hit_player_object (*(s16 *)&g_defines_itd[13]) /* 0x444b3a */
#define g_player_type (*(s16 *)&g_defines_itd[14])      /* 0x444b3c */
#define g_transition_flag (*(s16 *)&g_defines_itd[15])  /* 0x444b3e */

/* ---- engine ------------------------------------------------------------ */

void game_main(int argc, char **argv);  /* 0x4078b7 */
void engine_init(void);                 /* 0x4071e1 */
void engine_shutdown(void);             /* 0x407687 */
void game_run(s16 mode, s16 stage, s16 flag); /* 0x4077f0 */
int main_menu_run(void);                /* 0x408f34 */
void menu_difficulty_select(s16 mode);  /* 0x409107 */
int savegame_restore(void *object_table, int in_game); /* 0x4192f6 */

/* ---- memory ------------------------------------------------------------ */

void *mem_alloc(size_t size);           /* 0x420b28 */
void mem_free(void *ptr);               /* 0x420b58 */
void *mem_alloc_checked(size_t size);   /* 0x402b6f, fatal_error on failure */

/* ---- errors ------------------------------------------------------------ */

void fatal_error(int code, const char *message); /* 0x4145ec, noreturn */
void sys_error_box(const char *caption, const char *message); /* 0x419d92 */
const char *text_get_by_id(int id);     /* 0x414fcb */

/* ---- files and resources ---------------------------------------------- */

void *file_load_alloc(const char *name, int index); /* 0x42025a, one .PAK entry */
void *file_load_text_alloc(const char *name);       /* 0x420a0e */
int file_open(const char *name, int mode);          /* 0x4203d5 */
int file_read(int handle, void *dst, u32 size);     /* 0x420728 */
int file_close(int handle);                         /* 0x420421 */

void *load_malloc_hqr(const char *pak_name, int index); /* 0x414760 */
void *load_malloc(const char *name);                 /* 0x4146ba */
u32 file_size(const char *name);                     /* 0x42067e */

/* 0x4195fc - name doubles as the backing container's base name (ListBody ->
 * LISTBODY.PAK), which is how the caches find their data. */
struct hqr_resource *hqr_init_resource(const char *name, u32 byte_size, s16 entries);
struct hqr_resource *hqr_init_resource_raw(u32 byte_size, s16 entries); /* 0x41991a */
void hqr_free_resource(struct hqr_resource *cache);

/* 0x4196b6 - fetches an entry, loading it on first use. Sets g_hqr_loaded
 * when the entry was not resident. A negative index returns NULL. */
void *hqr_get(struct hqr_resource *cache, int index, u32 *out_size);

extern s16 g_hqr_loaded;   /* 0x435148 */
extern s16 g_hqr_evicted;  /* 0x435144, never set: nothing is evicted */

/* ---- font -------------------------------------------------------------- */

void *font_set_data(void *blob);             /* 0x420eea */
void *font_set_color_table(void *table);     /* 0x420f47, xchg 0x435220 */
u16 font_set_spacing(u16 spacing);           /* 0x420f4e, xchg 0x435226 */

#endif /* AITD_H */
