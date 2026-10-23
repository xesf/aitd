/*
 * common.c - file paths shared by every backend: the data folder, case
 * insensitive name resolution, the save folders and the disc image.
 *
 * The original opened "itd_ress.pak" and "DEFINES.ITD" with the casing
 * baked into the binary, which DOS and Windows resolve case insensitively.
 * Other hosts fall back to a directory scan.
 */

#if !defined(_WIN32) && !defined(__WATCOMC__)
#define _POSIX_C_SOURCE 200809L    /* S_ISDIR, opendir under -std=c99 */
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/types.h>

#if defined(__WATCOMC__)
#include <direct.h>         /* opendir and readdir in Open Watcom */
#else
#if defined(_WIN32)
#include <direct.h>
#endif
#include <dirent.h>
#endif
#if !defined(_WIN32) && !defined(__WATCOMC__)
#include <unistd.h>
#endif

#include "aitd.h"
#include "cdimage.h"
#include "platform.h"
#include "platforms/backend.h"

static int same_name(const char *a, const char *b)
{
    for (; *a != '\0' && *b != '\0'; a++, b++) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return 0;
        }
    }
    return *a == *b;
}

static int has_suffix(const char *name, const char *suffix)
{
    size_t n = strlen(name), m = strlen(suffix);

    return n > m && same_name(name + n - m, suffix);
}

static int file_readable(const char *path)
{
    FILE *f = fopen(path, "rb");

    if (f == NULL) {
        return 0;
    }
    fclose(f);
    return 1;
}

void platform_make_dir(const char *path)
{
#if defined(_WIN32) || defined(__WATCOMC__)
    _mkdir(path);
#else
    mkdir(path, 0755);
#endif
}

/* ---- disc images ------------------------------------------------------- */

/* A .cue sheet in the data folder is the game CD in the drive: installed
 * files are read from the folder, files only on the disc come out of the
 * image, and CD audio plays from its tracks. */
static void open_disc_in_folder(const char *dir)
{
    char path[1024];
    DIR *handle;
    struct dirent *entry;

    if ((handle = opendir(dir)) == NULL) {
        return;
    }
    while ((entry = readdir(handle)) != NULL) {
        if (has_suffix(entry->d_name, ".cue")) {
            snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);
            cdimage_open(path, dir);
            break;
        }
    }
    closedir(handle);
}

const char *common_data_dir(void)
{
    static char cached[1024];
    const char *env;

    if (cached[0] != '\0') {
        return cached;
    }

    env = getenv("AITD_DATA_DIR");
    if (env != NULL && env[0] != '\0') {
        snprintf(cached, sizeof(cached), "%s", env);
        if (has_suffix(env, ".cue")) {
            /* A disc image: the folder that holds it is the data folder,
             * and the files copied out of the image and the saves go
             * there. */
            char *slash = strrchr(cached, '/');
            char *back = strrchr(cached, '\\');

            if (back != NULL && (slash == NULL || back > slash)) {
                slash = back;
            }
            if (slash != NULL) {
                *slash = '\0';
            } else {
                snprintf(cached, sizeof(cached), ".");
            }
            cdimage_open(env, cached);
            return cached;
        }
    } else {
        backend_default_data_dir(cached, sizeof(cached));
        if (cached[0] == '\0') {
            snprintf(cached, sizeof(cached), ".");
        }
    }
    open_disc_in_folder(cached);
    return cached;
}

/* ---- paths ------------------------------------------------------------- */

/* Save games live in <data>/SAVE0<n>/, the directory layout of the original. */
int platform_save_path(const char *dir_name, const char *file_name, char *out,
                       size_t out_size, int create)
{
    if (snprintf(out, out_size, "%s/%s", common_data_dir(), dir_name) >= (int)out_size) {
        return 0;
    }
    if (create) {
        platform_make_dir(out);
    }
    if (file_name != NULL) {
        size_t len = strlen(out);
        if (snprintf(out + len, out_size - len, "/%s", file_name) >= (int)(out_size - len)) {
            return 0;
        }
    }
    return 1;
}

static char s_overlay_dir[64];

/* A folder under the data directory searched before the data directory
 * itself (Time Gate for Windows keeps its per-resolution files apart). */
void platform_set_overlay_dir(const char *name)
{
    snprintf(s_overlay_dir, sizeof(s_overlay_dir), "%s", name != NULL ? name : "");
}

int platform_is_directory(const char *path)
{
    struct stat st;

    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int resolve_in(const char *dir, const char *name, char *out, size_t out_size)
{
    DIR *handle;
    struct dirent *entry;

    if (snprintf(out, out_size, "%s/%s", dir, name) >= (int)out_size) {
        return 0;
    }
    if (file_readable(out) || platform_is_directory(out)) {
        return 1;
    }

    handle = opendir(dir);
    if (handle == NULL) {
        return 0;
    }
    while ((entry = readdir(handle)) != NULL) {
        if (same_name(entry->d_name, name)) {
            snprintf(out, out_size, "%s/%s", dir, entry->d_name);
            closedir(handle);
            return 1;
        }
    }
    closedir(handle);
    return 0;
}

int platform_resolve_path(const char *name, char *out, size_t out_size)
{
    const char *dir = common_data_dir();

    if (s_overlay_dir[0] != '\0') {
        char sub[1024];

        snprintf(sub, sizeof(sub), "%s/%s", dir, s_overlay_dir);
        if (resolve_in(sub, name, out, out_size)) {
            return 1;
        }
    }
    if (resolve_in(dir, name, out, out_size)) {
        return 1;
    }
    if (cdimage_is_open()) {
        if (s_overlay_dir[0] != '\0') {
            char inner[256];

            snprintf(inner, sizeof(inner), "%s/%s", s_overlay_dir, name);
            if (cdimage_resolve(inner, out, out_size)) {
                return 1;
            }
        }
        return cdimage_resolve(name, out, out_size);
    }
    return 0;
}

/* ---- settings file ----------------------------------------------------- */

/* Hosts without environment variables (the consoles) read the AITD_*
 * settings from aitd.env in the program's folder: one NAME=VALUE per line,
 * # starts a comment. Variables already set win. */
void platform_early_init(void)
{
    char dir[1024], path[1100], line[512];
    FILE *f;

    backend_default_data_dir(dir, sizeof(dir));
    snprintf(path, sizeof(path), "%s/aitd.env", dir[0] != '\0' ? dir : ".");
    if ((f = fopen(path, "r")) == NULL) {
        return;
    }
    while (fgets(line, sizeof(line), f) != NULL) {
        char *eq = strchr(line, '=');
        char *end = line + strlen(line);

        while (end > line && (end[-1] == '\n' || end[-1] == '\r' || end[-1] == ' ')) {
            *--end = '\0';
        }
        if (line[0] == '#' || eq == NULL) {
            continue;
        }
        *eq = '\0';
        if (getenv(line) == NULL) {
#if defined(_WIN32) || defined(__WATCOMC__)
            char entry[512];

            snprintf(entry, sizeof(entry), "%s=%s", line, eq + 1);
            _putenv(entry);
#else
            setenv(line, eq + 1, 0);
#endif
        }
    }
    fclose(f);
}
