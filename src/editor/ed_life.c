/*
 * ed_life.c - the life script viewer: every script of LISTLIFE.PAK as a
 * listing, with the game's variables shown at their current values, and
 * the objects that run the script. A > marks the lines the script ran the
 * last time it ran (life.c keeps them).
 *
 * The operand shapes per title are the ones tools/life/lifecheck.py and
 * tgcheck.py confirm by parsing every script of each title to its exact
 * length (w a word, e an expression, j a jump, M a multi case, T a sample
 * table, L a counted expression list). Jumps are word offsets from the
 * word after them, as life.c reads them; the listing shows targets as
 * @offset.
 */

#include <stdio.h>
#include <string.h>

#include "aitd.h"
#include "game.h"
#include "life.h"
#include "pak.h"
#include "text.h"
#include "world.h"
#include "editor/ed_ui.h"

enum { MAX_LINES = 2048, LINE_BYTES = 128 };

struct op_shape {
    const char *name;
    const char *shape;
};

/* Alone in the Dark 2 and 3, Jack in the Dark, and Time Gate below 102. */
static const struct op_shape s_ops[] = {
    { "DO_MOVE", "" }, { "ANIM_ONCE", "ww" }, { "ANIM_ALL_ONCE", "ww" }, { "BODY", "e" },
    { "IF_EQUAL", "eej" }, { "IF_DIFFERENT", "eej" }, { "IF_SUP_EQUAL", "eej" },
    { "IF_SUP", "eej" }, { "IF_INF_EQUAL", "eej" }, { "IF_INF", "eej" }, { "GOTO", "j" },
    { "RETURN", "" }, { "END", "" }, { "ANIM_REPEAT", "w" }, { "ANIM_MOVE", "wwwwwww" },
    { "MOVE", "ww" }, { "HIT", "wwwwew" }, { "MESSAGE", "w" }, { "VAR", "we" }, { "INC", "w" },
    { "DEC", "w" }, { "ADD", "we" }, { "SUB", "we" }, { "LIFE_MODE", "w" }, { "SWITCH", "e" },
    { "CASE", "wj" }, { "START_CHRONO", "" }, { "MULTI_CASE", "M" }, { "FOUND", "w" },
    { "LIFE", "w" }, { "DELETE", "e" }, { "TAKE", "w" }, { "IN_HAND", "e" }, { "READ", "ww" },
    { "ANIM_SAMPLE", "eww" }, { "SPECIAL", "w" }, { "DO_REAL_ZV", "" }, { "SAMPLE", "w" },
    { "TYPE", "w" }, { "GAME_OVER", "" }, { "MANUAL_ROT", "" }, { "RND_FREQ", "w" },
    { "MUSIC", "w" }, { "SET_BETA", "ww" }, { "DO_ROT_ZV", "" }, { "STAGE", "wwwww" },
    { "FOUND_NAME", "w" }, { "FOUND_FLAG", "w" }, { "FOUND_LIFE", "w" },
    { "CAMERA_TARGET", "w" }, { "DROP", "ew" }, { "FIRE", "ewwwwwe" }, { "TEST_COL", "w" },
    { "FOUND_BODY", "w" }, { "SET_ALPHA", "ww" }, { "DO_MAX_ZV", "" },
    { "PUT", "wwwwwwwww" }, { "DO_NORMAL_ZV", "" }, { "DO_CARRE_ZV", "" },
    { "SAMPLE_THEN", "ww" }, { "LIGHT", "w" }, { "SHAKING", "w" }, { "INVENTORY", "w" },
    { "FOUND_WEIGHT", "w" }, { "PUT_AT", "ww" }, { "DEF_ZV", "wwwwww" }, { "HIT_OBJECT", "ww" },
    { "ANGLE", "www" }, { "REP_SAMPLE", "ww" }, { "THROW", "wwwwwww" }, { "WATER", "w" },
    { "PICTURE", "www" }, { "STOP_SAMPLE", "" }, { "NEXT_MUSIC", "w" }, { "FADE_MUSIC", "w" },
    { "STOP_HIT_OBJECT", "" }, { "COPY_ANGLE", "w" }, { "SAMPLE_THEN_REPEAT", "ww" },
    { "WAIT_GAME_OVER", "" }, { "GET_MATRICE", "" }, { "STAGE_LIFE", "w" },
    { "CONTINUE_TRACK", "" }, { "ANIM_RESET", "ww" }, { "RESET_MOVE_MANUAL", "" },
    { "PLUIE", "w" }, { "ANIM_HYBRIDE_ONCE", "ww" }, { "ANIM_HYBRIDE_REPEAT", "ww" },
    { "MODIF_C_VAR", "we" }, { "CALL_INVENTORY", "" }, { "BODY_RESET", "ee" },
    { "DEL_INVENTORY", "" }, { "SET_INVENTORY", "w" }, { "PLAY_SEQUENCE", "www" },
    { "2D_ANIM_SAMPLE", "eww" }, { "SET_GROUND", "w" }, { "PROTECT", "" },
    { "DEF_ABS_ZV", "wwwwww" }, { "DEF_SEQUENCE_SAMPLE", "T" },
    { "READ_ON_PICTURE", "wwwwwwww" }, { "FIRE_UP_DOWN", "ewwwwwwe" },
    { "DO_ROT_CLUT", "www" }, { "STOP_CLUT", "" }
};

#define OP_COUNT ((int)(sizeof(s_ops) / sizeof(s_ops[0])))

/* Alone in the Dark 1's order, by name into s_ops or its own. */
static const char *const s_aitd1_names[] = {
    "DO_MOVE", "ANIM_ONCE", "ANIM_ALL_ONCE", "BODY", "IF_EQUAL", "IF_DIFFERENT",
    "IF_SUP_EQUAL", "IF_SUP", "IF_INF_EQUAL", "IF_INF", "GOTO", "RETURN", "END",
    "ANIM_REPEAT", "ANIM_MOVE", "MOVE", "HIT", "MESSAGE", "MESSAGE_VALUE", "VAR", "INC", "DEC",
    "ADD", "SUB", "LIFE_MODE", "SWITCH", "CASE", "CAMERA", "START_CHRONO", "MULTI_CASE",
    "FOUND", "LIFE", "DELETE", "TAKE", "IN_HAND", "READ", "ANIM_SAMPLE", "SPECIAL",
    "DO_REAL_ZV", "SAMPLE", "TYPE", "GAME_OVER", "MANUAL_ROT", "RND_FREQ", "MUSIC",
    "SET_BETA", "DO_ROT_ZV", "STAGE", "FOUND_NAME", "FOUND_FLAG", "FOUND_LIFE",
    "CAMERA_TARGET", "DROP", "FIRE", "TEST_COL", "FOUND_BODY", "SET_ALPHA", "STOP_BETA",
    "DO_MAX_ZV", "PUT", "C_VAR", "DO_NORMAL_ZV", "DO_CARRE_ZV", "SAMPLE_THEN", "LIGHT",
    "SHAKING", "INVENTORY", "FOUND_WEIGHT", "UP_COOR_Y", "SPEED", "PUT_AT", "DEF_ZV",
    "HIT_OBJECT", "GET_HARD_CLIP", "ANGLE", "REP_SAMPLE", "THROW", "WATER", "PICTURE",
    "STOP_SAMPLE", "NEXT_MUSIC", "FADE_MUSIC", "STOP_HIT_OBJECT", "COPY_ANGLE",
    "END_SEQUENCE", "SAMPLE_THEN_REPEAT", "WAIT_GAME_OVER"
};

/* Shapes that differ from s_ops, per title. */
static const struct op_shape s_aitd1_shapes[] = {
    { "MESSAGE_VALUE", "ww" }, { "CAMERA", "w" }, { "STOP_BETA", "" }, { "C_VAR", "we" },
    { "UP_COOR_Y", "" }, { "SPEED", "w" }, { "GET_HARD_CLIP", "" }, { "END_SEQUENCE", "" },
    { "FIRE", "wwwwww" }, { "SAMPLE", "e" }, { "REP_SAMPLE", "ew" }, { "SAMPLE_THEN", "ee" },
    { "SAMPLE_THEN_REPEAT", "ee" }, { "IN_HAND", "w" }, { "DELETE", "w" }, { "READ", "www" },
    { NULL, NULL }
};

static const struct op_shape s_jack_shapes[] = {
    { "PLAY_SEQUENCE", "w" }, { "SAMPLE", "e" }, { "SAMPLE_THEN", "ee" }, { "IN_HAND", "w" },
    { "DELETE", "w" }, { NULL, NULL }
};

/* Time Gate, read from TG320.EX_'s interpreter (tools/life/tgcheck.py). */
static const char *const s_tg_shapes[123] = {
    "", "ww", "ww", "e", "eej", "eej", "eej", "eej", "eej", "eej", "j", "", "", "w",
    "wwwwwww", "ww", "wwwwew", "w", "we", "w", "w", "we", "we", "w", "e", "wj", "", "M", "w",
    "w", "e", "w", "e", "ww", "ewww", "w", "", "ew", "w", "", "", "w", "ww", "ww", "",
    "wwwww", "w", "w", "w", "w", "ew", "ewwwwwe", "w", "w", "ww", "", "wwwwwwwww", "", "",
    "ewew", "w", "w", "w", "w", "we", "wwwwww", "ww", "www", "ew", "wwwwwww", "", "wwew",
    "w", "www", "www", "", "w", "ewew", "", "", "w", "", "ww", "", "", "ww", "ww", "we", "",
    "ee", "", "w", "www", "eww", "w", "", "wwwwww", "T", "wwwwwwww", "ewwwwwweww", "www",
    "", "eeej", "eeej", "", "ww", "", "", "www", "ww", "ww", "www", "www", "wwwww",
    "wwwwww", "wwwww", "ww", "ww", "", "", "", "L", ""
};

static const char *const s_tg_names[] = {      /* 102.. */
    "IF_IN", "IF_OUT", "OP104", "SET_VOLUME_SAMPLE", "OP106", "OP107", "FADE_IN_MUSIC",
    "SET_MUSIC_VOLUME", "MUSIC_AND_LOOP", "MUSIC_THEN", "MUSIC_THEN_LOOP",
    "START_FADE_IN_MUSIC", "START_FADE_IN_MUSIC_THEN", "START_FADE_IN_MUSIC_LOOP",
    "FADE_OUT_MUSIC_STOP", "MUSIC_ALTER_TEMPO", "REP_SAMPLE_N_TIME", "OP119", "OP120",
    "SAMPLE_LIST", "STOP_SAMPLE_LIST"
};

static const char *const s_var_names[] = {
    "VARS", "COL", "HARD_DEC", "HARD_COL", "HIT", "HIT_BY", "ANIM", "END_ANIM", "FRAME",
    "END_FRAME", "BODY", "MARK", "NUM_TRACK", "CHRONO", "ROOM_CHRONO", "DIST", "COL_BY",
    "IN_INVENTORY", "ACTION", "POSREL", "KEYBOARD_INPUT", "BUTTON", "COL_ANY", "ALPHA",
    "BETA", "GAMMA", "IN_HAND", "HIT_FORCE", "CAMERA", "RAND", "FALLING", "ROOM", "LIFE",
    "OBJECT_TAKEN", "MUSIC", "DEFINE", "STAGE", "THROW", "MATRIX", "MATERIAL",
    "TEST_PROTECT", "END_WAY", "DIV_BY_2"
};

/* Words after a variable, by its raw number: a count, or -1 for an
 * expression (lifecheck.py VAREXTRA, tgcheck.py TG_VARS). */
static int var_extra(int raw)
{
    int aitd1 = game_current()->life_dialect == LIFE_DIALECT_AITD1;
    int tg = game_id() == GAME_TIMEGATE;

    switch (raw) {
    case 15: case 29: case 33: return 1;
    case 17: return -1;
    case 19: return tg ? -1 : 1;
    case 34: return tg ? 1 : 0;
    case 35: return aitd1 ? 2 : 1;
    case 37: return 1;
    case 38: return aitd1 ? 0 : 2;
    case 39: return aitd1 ? 1 : 0;
    case 42: return tg ? -1 : 0;
    default: return 0;
    }
}

static const char *var_name(int raw)
{
    static char other[16];
    s16 v = life_var_map((s16)raw);

    if (v == LIFE_VAR_ROOM_Y) {
        return "ROOM_Y";
    }
    if (v == LIFE_VAR_TEST_ZV_END_ANIM) {
        return "TEST_ZV_END_ANIM";
    }
    if (v >= 0 && v < (s16)(sizeof(s_var_names) / sizeof(s_var_names[0]))) {
        return s_var_names[v];
    }
    snprintf(other, sizeof(other), "VAR%d", raw);
    return other;
}

static const char *find_shape(const struct op_shape *table, const char *name)
{
    int i;

    for (i = 0; table != NULL && table[i].name != NULL; i++) {
        if (strcmp(table[i].name, name) == 0) {
            return table[i].shape;
        }
    }
    return NULL;
}

/* The name and operand shape of a raw opcode; 0 when unknown. */
static int op_lookup(int raw, const char **name, const char **shape)
{
    const struct op_shape *extra = NULL;
    const char *s;
    int i;

    if (game_current()->life_dialect == LIFE_DIALECT_AITD1) {
        if (raw < 0 || raw >= (int)(sizeof(s_aitd1_names) / sizeof(s_aitd1_names[0]))) {
            return 0;
        }
        *name = s_aitd1_names[raw];
        if ((s = find_shape(s_aitd1_shapes, *name)) != NULL) {
            *shape = s;
            return 1;
        }
        for (i = 0; i < OP_COUNT; i++) {
            if (strcmp(s_ops[i].name, *name) == 0) {
                *shape = s_ops[i].shape;
                return 1;
            }
        }
        return 0;
    }
    if (game_id() == GAME_TIMEGATE) {
        if (raw < 0 || raw >= 123) {
            return 0;
        }
        *name = raw < OP_COUNT ? s_ops[raw].name : s_tg_names[raw - OP_COUNT];
        *shape = s_tg_shapes[raw];
        return 1;
    }
    if (raw < 0 || raw >= OP_COUNT) {
        return 0;
    }
    if (game_id() == GAME_JACK) {
        extra = s_jack_shapes;
    }
    *name = s_ops[raw].name;
    *shape = (s = find_shape(extra, *name)) != NULL ? s : s_ops[raw].shape;
    return 1;
}

/* ---- the listing ---------------------------------------------------------- */

static struct {
    int at;                     /* word offset */
    int depth;
    char text[LINE_BYTES];
} s_lines[MAX_LINES];
static int s_line_count;

static const s16 *s_w;
static int s_len, s_pos, s_bad;

static int word(void)
{
    if (s_pos >= s_len) {
        s_bad = 1;
        return 0;
    }
    return s_w[s_pos++];
}

static void put(char *out, size_t size, const char *fmt, int v)
{
    size_t n = strlen(out);

    if (n < size) {
        snprintf(out + n, size - n, fmt, v);
    }
}

static void puts_(char *out, size_t size, const char *text)
{
    size_t n = strlen(out);

    if (n < size) {
        snprintf(out + n, size - n, "%s", text);
    }
}

static void expression(char *out, size_t size)
{
    int op = word(), extra;

    if (s_bad) {
        return;
    }
    if (op == -1) {
        put(out, size, " %d", word());
        return;
    }
    if (op == 0) {
        int v = word();

        put(out, size, " V%d", v);
        if (g_vars != NULL && v >= 0 && (u32)v < g_vars_size / 2) {
            put(out, size, "=%d", g_vars[v]);
        }
        return;
    }
    puts_(out, size, " ");
    if (op & 0x8000) {
        put(out, size, "o%d.", word());
    }
    puts_(out, size, var_name(op & 0x7fff));
    extra = var_extra(op & 0x7fff);
    if (extra < 0) {
        puts_(out, size, "(");
        expression(out, size);
        puts_(out, size, " )");
    } else {
        while (extra-- > 0) {
            put(out, size, "(%d)", word());
        }
    }
}

static void disassemble(const u8 *script, u32 size)
{
    int depth = 0, ends[64], nends = 0;

    s_w = (const s16 *)script;
    s_len = (int)(size / 2);
    s_pos = 0;
    s_bad = 0;
    s_line_count = 0;
    while (s_pos < s_len && s_line_count < MAX_LINES && !s_bad) {
        char *out = s_lines[s_line_count].text;
        size_t room = LINE_BYTES;
        const char *name, *shape, *c;
        int raw;

        while (nends > 0 && ends[nends - 1] <= s_pos) {
            nends--;
            depth--;
        }
        s_lines[s_line_count].at = s_pos;
        s_lines[s_line_count].depth = depth;
        out[0] = '\0';
        raw = word();
        if (raw & 0x8000) {
            put(out, room, "o%d.", word());
        }
        if (!op_lookup(raw & 0x7fff, &name, &shape)) {
            put(out, room, "?%d", raw & 0x7fff);
            s_line_count++;
            break;
        }
        puts_(out, room, name);
        for (c = shape; *c != '\0' && !s_bad; c++) {
            switch (*c) {
            case 'w':
                put(out, room, " %d", word());
                break;
            case 'e':
                expression(out, room);
                break;
            case 'j': {
                int off = word();
                int target = s_pos + off;

                put(out, room, " -> @%d", target);
                if (raw >= 4 && raw <= 9 && nends < 64 && target > s_pos) {
                    ends[nends++] = target;     /* IF_xx: the body runs to the target */
                    depth++;
                }
                break;
            }
            case 'M': {
                int n = word(), i;

                for (i = 0; i < n && !s_bad; i++) {
                    put(out, room, " %d", word());
                }
                {
                    int off = word();
                    put(out, room, " -> @%d", s_pos + off);
                }
                break;
            }
            case 'T': {
                int n = word(), i;

                put(out, room, " [%d]", n);
                for (i = 0; i < n * 2 && !s_bad; i++) {
                    word();
                }
                break;
            }
            case 'L': {
                int n = word(), i;

                put(out, room, " [%d]", n);
                for (i = 0; i < n && !s_bad; i++) {
                    expression(out, room);
                }
                break;
            }
            default:
                break;
            }
        }
        s_line_count++;
    }
}

/* Lists every script and counts those that do not end exactly at their
 * length (the shapes' check, --stage=life-check). */
int ed_life_check(int verbose)
{
    struct pak pak;
    int count = 0, bad = 0, i;

    if (pak_open(&pak, "LISTLIFE.PAK")) {
        count = pak_entry_count(&pak);
        pak_close(&pak);
    }
    for (i = 0; i < count; i++) {
        u32 size = 0;
        u8 *script = pak_load("LISTLIFE.PAK", i, &size);

        if (script == NULL || size == 0) {
            if (script != NULL) {
                mem_free(script);
            }
            continue;
        }
        disassemble(script, size);
        if (s_bad || s_pos != s_len) {
            bad++;
            if (verbose) {
                printf("life %d: stops at word %d of %d: %s\n", i, s_pos, s_len,
                       s_line_count > 0 ? s_lines[s_line_count - 1].text : "");
            }
        }
        mem_free(script);
    }
    printf("life-check: %d scripts, %d do not list to their end\n", count, bad);
    return bad;
}

/* ---- the viewer ----------------------------------------------------------- */

static int s_count;
static int s_entry;
static int s_loaded = -1;
static int s_first;
static int s_line;
static int s_size;
static int s_object = -1;       /* the object Enter last went to */

static void life_open(void)
{
    struct pak pak;

    s_count = 0;
    if (pak_open(&pak, "LISTLIFE.PAK")) {
        s_count = pak_entry_count(&pak);
        pak_close(&pak);
    }
    s_loaded = -1;
}

static void load(void)
{
    u32 size = 0;
    u8 *script;

    if (s_loaded == s_entry) {
        return;
    }
    s_loaded = s_entry;
    s_line = s_first = 0;
    s_line_count = 0;
    script = pak_load("LISTLIFE.PAK", s_entry, &size);
    s_size = (int)size;
    if (script != NULL) {
        disassemble(script, size);
        mem_free(script);
    }
}

/* Enter: the next object of the current room that has a life. */
static void next_object(void)
{
    int i, n = g_object_count;

    for (i = 1; i <= n; i++) {
        int k = (s_object + i + n) % n;
        struct world_object *o = object_get(k);

        if (o != NULL && o->life >= 0 && o->room == g_cur_room && o->actor >= 0) {
            s_object = k;
            s_entry = o->life;
            return;
        }
    }
}

static void life_key(int sc)
{
    if (ed_list_key(sc, &s_line, s_line_count, 10)) {
        return;
    }
    switch (sc) {
    case ED_KEY_LEFT: if (s_entry > 0) s_entry--; s_object = -1; break;
    case ED_KEY_RIGHT: if (s_entry < s_count - 1) s_entry++; s_object = -1; break;
    case ED_KEY_ENTER: next_object(); break;
    default: break;
    }
}

/* Whether the script ran the line the last time it ran (life_ran). */
static int ran(int at)
{
    const u16 *list;
    int n = life_ran((s16)s_entry, &list), i;

    for (i = 0; i < n; i++) {
        if (list[i] == at) {
            return 1;
        }
    }
    return 0;
}

static void row(int index, char *out, size_t size)
{
    snprintf(out, size, "%c%4d %*s%s", ran(s_lines[index].at) ? '>' : ' ', s_lines[index].at,
             s_lines[index].depth * 2, "", s_lines[index].text);
}

static void life_draw(void)
{
    char line[160];
    s16 top = ed_top(), bottom = ed_bottom();
    int i, users = 0;

    load();
    ed_clear(g_ed.black);
    snprintf(line, sizeof(line), "LISTLIFE %d/%d %db used by", s_entry, s_count - 1,
             s_size);
    for (i = 0; i < g_object_count && users < 4; i++) {
        struct world_object *o = object_get(i);

        if (o != NULL && o->life == s_entry) {
            size_t n = strlen(line);
            const char *nm = o->found_name >= 0 ? text_get_by_id(o->found_name) : NULL;

            snprintf(line + n, sizeof(line) - n, " o%d%s%s%s", i, nm != NULL ? "(" : "",
                     nm != NULL ? nm : "", nm != NULL ? ")" : "");
            users++;
        }
    }
    if (users == 0) {
        strncat(line, " none", sizeof(line) - strlen(line) - 1);
    }
    ed_text(3, (s16)(top + 1), line, s_object >= 0 ? g_ed.accent : g_ed.white);
    ed_list(0, (s16)(top + ed_line() + 2), (s16)(g_screen_width - 1), bottom, s_line_count,
            s_line, &s_first, row);
    ed_bars(4, "L/R script  Up/Dn line  Enter next obj");
}

const struct ed_viewer ed_view_life = {
    "Life", life_open, NULL, life_key, life_draw
};
