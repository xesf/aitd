/*
 * pak_dump.c - inspect and extract .PAK containers.
 *
 * Exercises the reconstructed reader in src/pak.c against real game data,
 * which is how the container format and the method 4 codec were confirmed.
 *
 *   pak_dump                     list the detected game and its containers
 *   pak_dump <name>              list every entry of one container
 *   pak_dump <name> <index>      write one entry to stdout
 *   pak_dump --verify            decode every entry of every container
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

#include "aitd.h"
#include "game.h"
#include "body.h"
#include "pak.h"
#include "platform.h"

static int list_container(const char *name, int quiet)
{
    struct pak pak;
    int i;
    int failures = 0;

    if (!pak_open(&pak, name)) {
        fprintf(stderr, "%s: cannot open\n", name);
        return -1;
    }

    if (!quiet) {
        printf("%s: %d entries\n", name, pak_entry_count(&pak));
    }

    for (i = 0; i < pak_entry_count(&pak); i++) {
        u32 size = 0;
        void *block = pak_read_entry(&pak, i, &size);

        if (block == NULL) {
            printf("  [%4d] FAILED\n", i);
            failures++;
            continue;
        }

        if (!quiet) {
            printf("  [%4d] %8u bytes\n", i, size);
        }

        mem_free(block);
    }

    pak_close(&pak);
    return failures;
}

static int verify_all(void)
{
    const char *dir = getenv("AITD_DATA_DIR");
    DIR *handle;
    struct dirent *entry;
    int containers = 0;
    int failures = 0;

    if (dir == NULL || dir[0] == '\0') {
        dir = ".";
    }

    handle = opendir(dir);
    if (handle == NULL) {
        fprintf(stderr, "cannot open data directory %s\n", dir);
        return 1;
    }

    while ((entry = readdir(handle)) != NULL) {
        const char *dot = strrchr(entry->d_name, '.');
        int result;

        if (dot == NULL || strcasecmp(dot, ".PAK") != 0) {
            continue;
        }

        result = list_container(entry->d_name, 1);
        if (result < 0) {
            failures++;
        } else {
            failures += result;
        }
        containers++;
    }

    closedir(handle);

    printf("%d containers checked, %d failures\n", containers, failures);
    return failures != 0;
}

/* Runs the reconstructed body header parser over every model in the cache's
 * backing container and reports how many parse cleanly. */
static int verify_bodies(void)
{
    const struct game_profile *profile = game_current();
    struct pak pak;
    int i;
    int parsed = 0;
    int failed = 0;
    int with_groups = 0;

    if (!pak_open(&pak, profile->list_body)) {
        fprintf(stderr, "%s: cannot open\n", profile->list_body);
        return 1;
    }

    for (i = 0; i < pak_entry_count(&pak); i++) {
        u32 size = 0;
        void *block = pak_read_entry(&pak, i, &size);
        struct body body;

        if (block == NULL) {
            failed++;
            continue;
        }

        if (body_parse(block, size, &body)) {
            parsed++;
            if (body.flags & BODY_FLAG_HAS_GROUPS) {
                with_groups++;
            }
        } else {
            failed++;
            printf("  [%4d] parse failed, %u bytes\n", i, size);
        }

        mem_free(block);
    }

    printf("%s: %d parsed, %d failed, %d with groups\n",
           profile->list_body, parsed, failed, with_groups);

    pak_close(&pak);
    return failed != 0;
}

int main(int argc, char **argv)
{
    const struct game_profile *profile = game_detect();

    if (argc >= 2 && strcmp(argv[1], "--bodies") == 0) {
        return verify_bodies();
    }

    if (argc >= 2 && strcmp(argv[1], "--verify") == 0) {
        printf("game: %s%s\n", profile->name,
               profile->verified ? "" : " (profile unverified)");
        return verify_all();
    }

    if (argc == 2) {
        return list_container(argv[1], 0) != 0;
    }

    if (argc >= 3) {
        u32 size = 0;
        void *block = pak_load(argv[1], atoi(argv[2]), &size);

        if (block == NULL) {
            fprintf(stderr, "%s[%s]: cannot read\n", argv[1], argv[2]);
            return 1;
        }

        fwrite(block, 1, size, stdout);
        mem_free(block);
        return 0;
    }

    printf("game: %s%s\n", profile->name,
           profile->verified ? "" : " (profile unverified)");
    printf("resources: %s\n", profile->resource_pak);
    printf("rooms:     %s##.PAK\n", profile->room_prefix);
    printf("cameras:   %s##.PAK\n", profile->camera_prefix);
    printf("caches:    %s %s %s %s %s %s %s\n",
           profile->list_body, profile->list_anim, profile->list_life,
           profile->list_trak, profile->list_hyb, profile->list_mat,
           profile->list_samp);
    return 0;
}
