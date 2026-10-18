/*
 * resource.c - memory, file and font primitives.
 *
 * These are the leaf routines engine_init depends on. Each one is small
 * enough in the original that its behaviour is fully determined:
 *
 *   mem_alloc            0x420b28  malloc
 *   mem_free             0x420b58  nfree, returns 1
 *   mem_alloc_checked    0x402b6f  malloc + fatal_error(1, ...) on NULL
 *   file_open            0x4203d5
 *   file_read            0x420463  raw bytes
 *   file_read_be16       0x420728  big-endian words
 *   file_close           0x420421
 *   file_load_alloc      0x42025a  load one .PAK entry by index
 *   file_load_text_alloc 0x420a0e  same, used for voice.cfg
 *   font_set_data        0x420eea
 *   font_set_color_table 0x420f47  xchg with 0x435220
 *   font_set_spacing     0x420f4e  xchg with 0x435226
 *   hqr_init_resource    0x4195fc
 *   hqr_init_resource_raw 0x41991a
 *   load_malloc_hqr      0x414760
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aitd.h"
#include "pak.h"
#include "platform.h"
#include "subsystems.h"

#define FILE_HANDLE_MAX 16

static FILE *s_handles[FILE_HANDLE_MAX];


u8 g_font_color_table[256];  /* 0x44485c */
u8 g_rand_state[64];         /* 0x4350e5 */

/* ---- memory ------------------------------------------------------------ */

void *mem_alloc(size_t size)
{
    return malloc(size);
}

void mem_free(void *ptr)
{
    free(ptr);
}

/*
 * 0x402b6f. The failure branch tail jumps into fatal_error with eax = 1 and
 * edx pointing at the message at 0x42f128.
 */
void *mem_alloc_checked(size_t size)
{
    void *block = mem_alloc(size);

    if (block == NULL) {
        fatal_error(1, "out of memory");
    }

    return block;
}

/* ---- errors ------------------------------------------------------------ */

/*
 * 0x4145ec. Reached by tail jump, never by call, and never returns: it tears
 * the engine down and terminates. The real routine also builds a log line and
 * flushes the caches before quitting.
 */
void fatal_error(int code, const char *message)
{
    platform_error_box("Alone in the Dark 3", message);
    engine_shutdown();
    exit(code != 0 ? code : -1);
}

/* 0x419d92 - MessageBoxA wrapper. */
void sys_error_box(const char *caption, const char *message)
{
    platform_error_box(caption, message);
}

/* ---- files ------------------------------------------------------------- */

static int handle_alloc(FILE *fp)
{
    int i;

    for (i = 1; i < FILE_HANDLE_MAX; i++) {
        if (s_handles[i] == NULL) {
            s_handles[i] = fp;
            return i;
        }
    }

    fclose(fp);
    return 0;
}

/* 0x4203d5. Returns 0 on failure; engine_init tests for exactly that. */
int file_open(const char *name, int mode)
{
    char path[1024];
    FILE *fp;

    (void)mode;

    if (!platform_resolve_path(name, path, sizeof(path))) {
        return 0;
    }

    fp = fopen(path, "rb");
    if (fp == NULL) {
        return 0;
    }

    return handle_alloc(fp);
}

/* 0x420463 - eax = handle, edx = destination, ebx = byte count. */
int file_read(int handle, void *dst, u32 size)
{
    if (handle <= 0 || handle >= FILE_HANDLE_MAX || s_handles[handle] == NULL) {
        return 0;
    }

    return (int)fread(dst, 1, size, s_handles[handle]);
}

/* 0x420728 - `words` big-endian 16 bit values, stored native. Returns the
 * number of bytes read. DEFINES.ITD is the only file read this way. */
int file_read_be16(int handle, u16 *dst, u32 words)
{
    u32 i;
    int got = 0;

    for (i = 0; i < words; i++) {
        u8 b[2];
        int n = file_read(handle, b, 2);

        got += n;
        if (n != 2) {
            break;
        }
        dst[i] = (u16)((b[0] << 8) | b[1]);
    }
    return got;
}

/* 0x420421 */
int file_close(int handle)
{
    if (handle <= 0 || handle >= FILE_HANDLE_MAX || s_handles[handle] == NULL) {
        return 0;
    }

    fclose(s_handles[handle]);
    s_handles[handle] = NULL;
    return 1;
}

static void *load_whole_file(const char *name, size_t extra)
{
    char path[1024];
    FILE *fp;
    long size;
    void *block;

    if (!platform_resolve_path(name, path, sizeof(path))) {
        return NULL;
    }

    fp = fopen(path, "rb");
    if (fp == NULL) {
        return NULL;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }

    size = ftell(fp);
    if (size < 0) {
        fclose(fp);
        return NULL;
    }
    rewind(fp);

    block = mem_alloc((size_t)size + extra);
    if (block == NULL) {
        fclose(fp);
        return NULL;
    }

    if (fread(block, 1, (size_t)size, fp) != (size_t)size) {
        mem_free(block);
        fclose(fp);
        return NULL;
    }

    fclose(fp);

    if (extra > 0) {
        ((char *)block)[size] = '\0';
    }

    return block;
}

/* 0x42025a - loads entry `index` of a .PAK container: directory seek at
 * index * 4 + 4, entry header, allocate, unpack. */
void *file_load_alloc(const char *name, int index)
{
    return pak_load(name, index, NULL);
}

/* 0x420a0e - reads a text file and null terminates it. voice.cfg is fed
 * straight to atoi afterwards (0x4073a9). */
void *file_load_text_alloc(const char *name)
{
    return load_whole_file(name, 1);
}

/* ---- pak --------------------------------------------------------------- */

/*
 * 0x414760 - load_malloc_hqr(eax = pak name, edx = entry index).
 *
 * The lib386 ancestor of this is LoadMalloc_HQR. The container walk and the
 * decompression both live in pak.c, reconstructed from file_load_alloc
 * (0x42025a) and pak_unpack (0x425dca).
 */
void *load_malloc_hqr(const char *pak_name, int index)
{
    return pak_load(pak_name, index, NULL);
}

/* 0x4146ba - loads a standalone .ITD file (Priority.ITD at 0x40752c). */
void *load_malloc(const char *name)
{
    return load_whole_file(name, 0);
}

/* 0x42067e - size of a whole file, 0 when it cannot be opened. */
u32 file_size(const char *name)
{
    char path[1024];
    FILE *fp;
    long size;

    if (!platform_resolve_path(name, path, sizeof(path))) {
        return 0;
    }
    fp = fopen(path, "rb");
    if (fp == NULL) {
        return 0;
    }
    fseek(fp, 0, SEEK_END);
    size = ftell(fp);
    fclose(fp);
    return size < 0 ? 0 : (u32)size;
}

/* ---- caches ------------------------------------------------------------ */

s16 g_hqr_loaded;   /* 0x435148 */
s16 g_hqr_evicted;  /* 0x435144 */

/*
 * 0x4195fc - hqr_init_resource(eax = name, edx = byte budget, ebx = entries).
 * The cache name is also the base name of the container that backs it:
 * "ListBody" -> LISTBODY.PAK.
 */
struct hqr_resource *hqr_init_resource(const char *name, u32 byte_size, s16 entries)
{
    struct hqr_resource *cache = mem_alloc_checked(sizeof(*cache));

    memset(cache, 0, sizeof(*cache));
    snprintf(cache->name, sizeof(cache->name), "%s", name);
    if (name[0] != '\0') {
        snprintf(cache->pak_name, sizeof(cache->pak_name), "%s.PAK", name);
    }
    cache->byte_size = byte_size;
    cache->max_entries = entries;

    return cache;
}

/* Points a cache at another container, dropping what it holds (Alone in
 * the Dark 1 swaps body and animation files with the chosen hero). */
void hqr_set_file(struct hqr_resource *cache, const char *name)
{
    int i;

    if (cache == NULL || strcmp(cache->name, name) == 0) {
        return;
    }
    for (i = 0; i < cache->entry_count; i++) {
        mem_free(cache->entries[i]);
        cache->entries[i] = NULL;
        cache->sizes[i] = 0;
    }
    snprintf(cache->name, sizeof(cache->name), "%s", name);
    snprintf(cache->pak_name, sizeof(cache->pak_name), "%s.PAK", name);
}

/* 0x41991a - same shape with no backing container, used once at 0x407581. */
struct hqr_resource *hqr_init_resource_raw(u32 byte_size, s16 entries)
{
    return hqr_init_resource("", byte_size, entries);
}

void hqr_free_resource(struct hqr_resource *cache)
{
    int i;

    if (cache == NULL) {
        return;
    }

    for (i = 0; i < cache->entry_count; i++) {
        mem_free(cache->entries[i]);
    }
    mem_free(cache->entries);
    mem_free(cache->sizes);
    mem_free(cache);
}

static int hqr_grow(struct hqr_resource *cache, int index)
{
    int count = cache->entry_count;
    int wanted = index + 1;
    u8 **entries;
    u32 *sizes;

    if (wanted <= count) {
        return 1;
    }
    if (wanted < count * 2) {
        wanted = count * 2;
    }
    if (wanted < 64) {
        wanted = 64;
    }

    entries = realloc(cache->entries, (size_t)wanted * sizeof(*entries));
    if (entries == NULL) {
        return 0;
    }
    cache->entries = entries;

    sizes = realloc(cache->sizes, (size_t)wanted * sizeof(*sizes));
    if (sizes == NULL) {
        return 0;
    }
    cache->sizes = sizes;

    memset(cache->entries + count, 0, (size_t)(wanted - count) * sizeof(*entries));
    memset(cache->sizes + count, 0, (size_t)(wanted - count) * sizeof(*sizes));
    cache->entry_count = wanted;
    return 1;
}

/* 0x4196b6 */
void *hqr_get(struct hqr_resource *cache, int index, u32 *out_size)
{
    void *loaded;
    u32 size = 0;

    g_hqr_evicted = 0;

    if (out_size != NULL) {
        *out_size = 0;
    }

    /* 0x4196d6: a negative index (bit 15 set) yields NULL. */
    if (cache == NULL || index < 0 || (index & 0x8000) || cache->pak_name[0] == '\0') {
        return NULL;
    }

    if (index < cache->entry_count && cache->entries[index] != NULL) {
        g_hqr_loaded = 0;
        if (out_size != NULL) {
            *out_size = cache->sizes[index];
        }
        return cache->entries[index];
    }

    if (!hqr_grow(cache, index)) {
        return NULL;
    }

    loaded = pak_load(cache->pak_name, index, &size);
    if (loaded == NULL) {
        fatal_error(0, cache->pak_name);
    }

    cache->entries[index] = loaded;
    cache->sizes[index] = size;
    g_hqr_loaded = 1;

    if (out_size != NULL) {
        *out_size = size;
    }

    return loaded;
}

