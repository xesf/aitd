/*
 * cd_dump.c - dump a CD, real or a mounted disc image, to BIN/CUE.
 *
 * macOS only. Reads the whole-disc raw device (/dev/rdiskN) through the
 * IOKit CD ioctls: DKIOCCDREADTOC for the full TOC, DKIOCCDREAD for the
 * sectors. Any image format the system can attach as a CD drive (MDX,
 * MDS/MDF, CCD, ...) dumps the same way as a pressed disc, so no image
 * format is parsed here.
 *
 * All tracks go to one "<name>.bin" in 2352 byte sectors, or with --split
 * each to its own "<name> (Track NN).bin": data tracks with sync, header and EDC/ECC as the drive returns
 * them, audio as 16 bit little endian stereo. When the device returns the
 * Q subchannel, the pregap (INDEX 00) of each track is found from it;
 * otherwise only the audio gap after a data track is found, by sector
 * type. A pregap is written just before its track's INDEX 01.
 *
 *   cd_dump [--split] <disk> <out>   e.g. cd_dump disk3 build/ALONECD
 *                                    writes build/ALONECD.cue and .bin
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <IOKit/storage/IOCDMediaBSDClient.h>
#include <IOKit/storage/IOCDTypes.h>

enum {
    SECTOR_RAW = 2352,
    CHUNK = 32,             /* sectors per read */
    MAX_PREGAP = 75 * 10    /* never look further back than 10 s */
};

struct track {
    int number;
    int data;
    int mode;               /* 1 or 2 for data tracks */
    long start;             /* INDEX 01, LBA */
    long pregap;            /* sectors of INDEX 00 before start */
    long end;               /* first LBA after the track */
};

static struct track s_tracks[99];
static int s_track_count;

static int bcd(int v)
{
    return (v >> 4) * 10 + (v & 15);
}

static int read_sectors(int fd, long lba, int count, int data, void *buf)
{
    dk_cd_read_t r;

    memset(&r, 0, sizeof(r));
    r.offset = (uint64_t)lba * SECTOR_RAW;
    if (data) {
        r.sectorArea = kCDSectorAreaSync | kCDSectorAreaHeader |
                       kCDSectorAreaSubHeader | kCDSectorAreaUser |
                       kCDSectorAreaAuxiliary;
        r.sectorType = kCDSectorTypeUnknown;
    } else {
        r.sectorArea = kCDSectorAreaUser;
        r.sectorType = kCDSectorTypeCDDA;
    }
    r.bufferLength = (uint32_t)count * SECTOR_RAW;
    r.buffer = buf;
    if (ioctl(fd, DKIOCCDREAD, &r) < 0) {
        return 0;
    }
    return r.bufferLength == (uint32_t)count * SECTOR_RAW;
}

/* Q subchannel of one sector: track and index, or 0 when unavailable. */
static int read_q(int fd, long lba, int *track, int *index)
{
    dk_cd_read_t r;
    unsigned char q[16];

    memset(&r, 0, sizeof(r));
    r.offset = (uint64_t)lba * sizeof(q);
    r.sectorArea = kCDSectorAreaSubChannelQ;
    r.sectorType = kCDSectorTypeUnknown;
    r.bufferLength = sizeof(q);
    r.buffer = q;
    if (ioctl(fd, DKIOCCDREAD, &r) < 0 || r.bufferLength != sizeof(q)) {
        return 0;
    }
    if ((q[0] & 15) != 1) {
        return 0;           /* not a position block */
    }
    *track = bcd(q[1]);
    *index = bcd(q[2]);
    return 1;
}

static int read_toc(int fd)
{
    static unsigned char buf[4096];
    dk_cd_read_toc_t r;
    CDTOC *toc;
    UInt32 i, count;
    long leadout = -1;

    memset(&r, 0, sizeof(r));
    r.format = kCDTOCFormatTOC;
    r.formatAsTime = 1;
    r.bufferLength = sizeof(buf);
    r.buffer = buf;
    if (ioctl(fd, DKIOCCDREADTOC, &r) < 0) {
        perror("DKIOCCDREADTOC");
        return 0;
    }
    toc = (CDTOC *)buf;
    count = CDTOCGetDescriptorCount(toc);
    for (i = 0; i < count; i++) {
        CDTOCDescriptor *d = &toc->descriptors[i];

        if (d->adr != 1) {
            continue;
        }
        if (d->point >= 1 && d->point <= 99 && s_track_count < 99) {
            struct track *t = &s_tracks[s_track_count++];

            t->number = d->point;
            t->data = (d->control & 4) != 0;
            t->start = (long)CDConvertMSFToLBA(d->p);
        } else if (d->point == 0xA2 && leadout < 0) {
            leadout = (long)CDConvertMSFToLBA(d->p);   /* first session */
        }
    }
    if (s_track_count == 0 || leadout < 0) {
        fprintf(stderr, "no tracks in the TOC\n");
        return 0;
    }
    for (i = 0; i < (UInt32)s_track_count; i++) {
        s_tracks[i].end = i + 1 < (UInt32)s_track_count
                        ? s_tracks[i + 1].start : leadout;
    }
    return 1;
}

/*
 * A sector that reads as CDDA is audio; a data sector fails that read.
 * Without the Q subchannel this still finds the usual gap at the end of
 * a data track, audio silence that belongs to the next audio track.
 */
static int is_audio(int fd, long lba)
{
    static unsigned char buf[SECTOR_RAW];

    return read_sectors(fd, lba, 1, 0, buf);
}

static void find_pregaps(int fd)
{
    int i, tno, index;
    int have_q = read_q(fd, s_tracks[0].start, &tno, &index);

    for (i = 1; i < s_track_count; i++) {
        struct track *t = &s_tracks[i];
        struct track *prev = &s_tracks[i - 1];
        long lba = t->start - 1;

        if (have_q) {
            while (lba > prev->start && t->start - lba <= MAX_PREGAP &&
                   read_q(fd, lba, &tno, &index) &&
                   tno == t->number && index == 0) {
                lba--;
            }
        } else if (prev->data && !t->data) {
            while (lba > prev->start && t->start - lba <= MAX_PREGAP &&
                   is_audio(fd, lba)) {
                lba--;
            }
        }
        t->pregap = t->start - 1 - lba;
        prev->end -= t->pregap;
    }
}

static int dump_track(int fd, struct track *t, FILE *f, const char *path)
{
    static unsigned char buf[CHUNK * SECTOR_RAW];
    long lba = t->start - t->pregap;

    while (lba < t->end) {
        int n = t->end - lba < CHUNK ? (int)(t->end - lba) : CHUNK;

        if (!read_sectors(fd, lba, n, t->data, buf)) {
            fprintf(stderr, "\n%s: read failed at sector %ld: %s\n",
                    path, lba, strerror(errno));
            return 0;
        }
        if (t->data && lba == t->start) {
            t->mode = buf[15] == 2 ? 2 : 1;
        }
        fwrite(buf, SECTOR_RAW, (size_t)n, f);
        lba += n;
        fprintf(stderr, "\rtrack %02d: %ld/%ld", t->number,
                lba - (t->start - t->pregap), t->end - (t->start - t->pregap));
    }
    fprintf(stderr, "\n");
    return 1;
}

static void msf(long sectors, char *out)
{
    sprintf(out, "%02ld:%02ld:%02ld",
            sectors / (75 * 60), sectors / 75 % 60, sectors % 75);
}

int main(int argc, char **argv)
{
    char device[256], path[1024], cue_path[1024], time[16];
    const char *name, *out;
    FILE *cue, *bin = NULL;
    long written = 0;       /* sectors in the current .bin */
    int split = 0;
    int fd, i;

    if (argc == 4 && strcmp(argv[1], "--split") == 0) {
        split = 1;
        argv++;
        argc--;
    }
    if (argc != 3) {
        fprintf(stderr, "usage: %s [--split] <disk> <out>\n"
                        "  e.g. %s disk3 build/ALONECD\n", argv[0], argv[0]);
        return 1;
    }
    name = argv[1];
    if (strncmp(name, "/dev/", 5) == 0) {
        name += 5;
    }
    if (name[0] == 'r') {
        name++;
    }
    snprintf(device, sizeof(device), "/dev/r%s", name);
    fd = open(device, O_RDONLY);
    if (fd < 0) {
        perror(device);
        return 1;
    }
    if (!read_toc(fd)) {
        return 1;
    }
    find_pregaps(fd);

    out = argv[2];
    name = strrchr(out, '/');
    name = name ? name + 1 : out;
    snprintf(cue_path, sizeof(cue_path), "%s.cue", out);
    cue = fopen(cue_path, "w");
    if (!cue) {
        perror(cue_path);
        return 1;
    }
    for (i = 0; i < s_track_count; i++) {
        struct track *t = &s_tracks[i];

        if (split || i == 0) {
            if (split) {
                snprintf(path, sizeof(path), "%s (Track %02d).bin", out,
                         t->number);
            } else {
                snprintf(path, sizeof(path), "%s.bin", out);
            }
            if (bin && fclose(bin) != 0) {
                perror(path);
                return 1;
            }
            bin = fopen(path, "wb");
            if (!bin) {
                perror(path);
                return 1;
            }
            fprintf(cue, "FILE \"%s\" BINARY\r\n", strrchr(path, '/')
                    ? strrchr(path, '/') + 1 : path);
            written = 0;
        }
        if (!dump_track(fd, t, bin, path)) {
            return 1;
        }
        if (t->data) {
            fprintf(cue, "  TRACK %02d MODE%d/2352\r\n", t->number, t->mode);
        } else {
            fprintf(cue, "  TRACK %02d AUDIO\r\n", t->number);
        }
        if (t->pregap > 0) {
            msf(written, time);
            fprintf(cue, "    INDEX 00 %s\r\n", time);
        }
        msf(written + t->pregap, time);
        fprintf(cue, "    INDEX 01 %s\r\n", time);
        written += t->end - (t->start - t->pregap);
    }
    close(fd);
    if (fclose(bin) != 0 || fclose(cue) != 0) {
        perror(out);
        return 1;
    }
    printf("%s: %d tracks\n", cue_path, s_track_count);
    return 0;
}
