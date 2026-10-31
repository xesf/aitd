/*
 * path.c - Time Gate's route finding (TG320.EX_ 0x41de11..0x41f244 and
 * the search at 0x42c580..0x430477).
 *
 * Each floor has an entry in PURSUIT.PAK:
 *
 *   u32 obstacles   u16 points   u16 zones   u16, u16 (unused)
 *   obstacles x 10  x1 z1 x2 z2 (cells), a word
 *   zones x 10      x1 z1 x2 z2, a word: the areas a route may cross
 *   256 bytes       links[from * 16 + to]: 1 + the zone between two zones
 *   points x 5      x z, a byte: cells no route may turn at
 *
 * A cell is 8 room units (80 world units). The obstacles, grown by a
 * margin, give four corners each; a route runs from corner to corner, and
 * two corners see each other when the segment between them crosses no
 * grown obstacle and is no longer than MAX_LEG. The search is a depth
 * first walk of at most 16 legs that only goes on to the corners the last
 * one could not see, and gives up on a branch once it is longer than the
 * best route so far (by 1/32 more for each zone it has to cross).
 */

#include <string.h>

#include "aitd.h"
#include "pak.h"
#include "path.h"

enum {
    MAX_NODES = 300,        /* 0x41e3ce: the matrices are 300 x 300 */
    MAX_DEPTH = 16,         /* 0x4514f0 */
    MAX_OBSTACLES = 100,    /* 0x451b04: 1000 bytes */
    PAD_X = 3,              /* 0x43c328 */
    PAD_Z = 3,              /* 0x43c32a */
    MAX_LEG = 3000,         /* 0x43c32c, in the units of isqrt16 */
    FAR = 0xffffff
};

struct rect { s16 x1, z1, x2, z2; };
struct node { s16 x, z; u8 corner; };
struct leg { u16 from, to; };

/* A level of the search (the 0x28 byte frames of 0x42c580). */
struct frame {
    u16 cur;
    u32 depth;
    u32 count;              /* candidates in list */
    u32 cost;
    const u16 *list;
    u16 *vis, *blk;         /* the candidates cur sees, and the others */
    struct leg *leg;
    u16 nvis, nblk;
};

static u8 *s_data;                      /* 0x43c2ad */
static int s_obstacle_count;            /* 0x451af8 */
static int s_point_count;               /* 0x43c331 */
static int s_zone_count;                /* 0x43c326 */
static struct rect s_rects[MAX_OBSTACLES + 16 + 1]; /* obstacles then zones */
static struct rect *s_zones;            /* 0x451b0c */
static struct rect s_grown[MAX_OBSTACLES]; /* 0x451b04 */
static s8 s_links[256];                 /* 0x445a1c */
static struct { s16 x, z; } s_points[512]; /* 0x451500 */

static struct node s_nodes[MAX_NODES];  /* 0x451b10 */
static u8 *s_blocked;                   /* 0x451afc, rows 0x4454ec */
static u32 *s_dist;                     /* 0x451ae8, rows 0x445024 */
static u16 s_order[MAX_NODES];          /* 0x451b08: 0, 1, 2 ... */
static u16 s_vis[MAX_DEPTH][MAX_NODES]; /* 0x451af4 */
static u16 s_blk[MAX_DEPTH][MAX_NODES]; /* 0x451b00 */

static s16 s_start_zone;                /* 0x452682 */
static s16 s_target_zone;               /* 0x45267c */
static s16 s_last_start_zone = -1;      /* 0x45267e */
static s16 s_last_target_zone;          /* 0x452688 */
static u8 s_nodes_dirty = 1;            /* 0x43c330 */
static u16 s_matrix_count;              /* 0x43c472 */
static u16 s_node_count;                /* 0x45268a */
static u32 s_slack;                     /* 0x4514f4 */
static u32 s_best;                      /* 0x4514f8 */
static u32 s_goal;                      /* 0x4514fc */
static u8 s_reverse;                    /* 0x45d174 */
static u16 s_leg_count;                 /* 0x452684 */
static struct leg s_best_legs[MAX_DEPTH]; /* 0x445b1c */
static struct leg s_try_legs[MAX_DEPTH];  /* 0x445b5c */
static struct leg s_legs[MAX_DEPTH];      /* 0x445b9c */

static s16 rd16(const u8 *p) { return (s16)(p[0] | (p[1] << 8)); }

/* The zone rectangle of an index; -1 reads the last obstacle, which comes
 * just before the zones in the original's buffer. */
static const struct rect *zone(s16 i)
{
    if (i < -1 || i >= s_zone_count || (i == -1 && s_obstacle_count == 0)) {
        return NULL;
    }
    return &s_zones[i];
}

static int inside(const struct rect *r, s16 x, s16 z)
{
    return r != NULL && x >= r->x1 && x <= r->x2 && z >= r->z1 && z <= r->z2;
}

/* links[] for two zones. The original reads outside the table when a
 * point is in no zone; that memory holds pointers, read here as 0. */
static s8 link_of(s16 from, s16 to)
{
    int i = from * 16 + to;

    return (i >= 0 && i < 256) ? s_links[i] : 0;
}

/* 0x41e051 */
void path_rebuild_boxes(void)
{
    int i;

    for (i = 0; i < s_obstacle_count; i++) {
        s_grown[i].x1 = (s16)(s_rects[i].x1 - PAD_X + 1);
        s_grown[i].x2 = (s16)(s_rects[i].x2 + PAD_X - 1);
        s_grown[i].z1 = (s16)(s_rects[i].z1 - PAD_Z + 1);
        s_grown[i].z2 = (s16)(s_rects[i].z2 + PAD_Z - 1);
    }
}

/* 0x41e3ce, 0x41f168 */
void path_load_floor(s16 floor)
{
    u32 size = 0;
    const u8 *p;
    int i, n;

    if (s_blocked == NULL) {
        s_blocked = mem_alloc_checked(MAX_NODES * MAX_NODES);
        s_dist = mem_alloc_checked(MAX_NODES * MAX_NODES * sizeof(u32));
        for (i = 0; i < MAX_NODES; i++) {
            s_order[i] = (u16)i;
        }
    }
    if (s_data != NULL) {
        mem_free(s_data);
    }
    s_obstacle_count = s_point_count = s_zone_count = 0;
    s_zones = s_rects;
    s_last_start_zone = -1;
    s_data = pak_load("PURSUIT.PAK", floor, &size);
    if (s_data == NULL || size < 12) {
        return;
    }
    p = s_data;
    n = (int)(p[0] | (p[1] << 8) | (p[2] << 16) | ((u32)p[3] << 24));
    s_point_count = (u16)rd16(p + 4);
    s_zone_count = (u16)rd16(p + 6);
    if (n > MAX_OBSTACLES || s_zone_count > 16 || s_point_count > 512
        || 12u + (u32)n * 10 + (u32)s_zone_count * 10 + 256 + (u32)s_point_count * 5 > size) {
        s_point_count = s_zone_count = 0;
        return;
    }
    s_obstacle_count = n;
    p += 12;
    for (i = 0; i < n + s_zone_count; i++, p += 10) {
        s_rects[i].x1 = rd16(p);
        s_rects[i].z1 = rd16(p + 2);
        s_rects[i].x2 = rd16(p + 4);
        s_rects[i].z2 = rd16(p + 6);
    }
    s_zones = s_rects + n;
    memcpy(s_links, p, 256);
    p += 256;
    for (i = 0; i < s_point_count; i++, p += 5) {
        s_points[i].x = rd16(p);
        s_points[i].z = rd16(p + 2);
    }
    path_rebuild_boxes();
}

/* 0x41ed90 */
static s16 zone_of(s16 x, s16 z)
{
    s16 i;

    for (i = 0; i < s_zone_count; i++) {
        if (inside(&s_zones[i], x, z)) {
            return i;
        }
    }
    return -1;
}

/* 0x41de65: a corner is unusable when it, or the cell a margin inside it
 * along either side, is in a grown obstacle, when it is a listed point,
 * or when it is in none of the zones of the route. */
static int corner_unusable(s16 x, s16 z, int corner)
{
    s16 sx = x, sz = z;
    s8 link;
    int i;

    for (i = 0; i < s_obstacle_count; i++) {
        const struct rect *b = &s_grown[i];

        if (inside(b, x, z)) {
            return 1;
        }
        if (corner == 1 || corner == 4) {
            sx = (s16)(x + PAD_X + 1);
            sz = z;
        } else if (corner == 2 || corner == 3) {
            sx = (s16)(x - PAD_X - 1);
            sz = z;
        }
        if (inside(b, sx, sz)) {
            return 1;
        }
        if (corner == 1 || corner == 2) {
            sz = (s16)(z + PAD_Z + 1);
            sx = x;
        } else if (corner == 3 || corner == 4) {
            sz = (s16)(z - PAD_Z - 1);
            sx = x;
        }
        if (inside(b, sx, sz)) {
            return 1;
        }
    }
    for (i = 0; i < s_point_count; i++) {
        if (s_points[i].x == x && s_points[i].z == z) {
            return 1;
        }
    }
    if (inside(zone(s_start_zone), x, z) || inside(zone(s_target_zone), x, z)) {
        return 0;
    }
    link = link_of(s_start_zone, s_target_zone);
    if (link > 0 && inside(zone((s16)(link - 1)), x, z)) {
        return 0;
    }
    return 1;
}

/* 0x41de11. The original compares the stored, signed, coordinates with
 * the new ones zero extended, so a negative cell is never a duplicate. */
static int known(s16 x, s16 z, int count)
{
    int i;

    for (i = 0; i < count; i++) {
        if ((s32)s_nodes[i].x == (s32)(u16)x && (s32)s_nodes[i].z == (s32)(u16)z) {
            return 1;
        }
    }
    return 0;
}

static void add_corner(s16 x, s16 z, int corner, int *count)
{
    if (*count >= MAX_NODES - 2) {
        return;     /* the original writes past its 300 nodes */
    }
    if (!corner_unusable(x, z, corner) && !known(x, z, *count)) {
        s_nodes[*count].x = x;
        s_nodes[*count].z = z;
        s_nodes[*count].corner = (u8)corner;
        (*count)++;
    }
}

/* 0x41e0c6: the corners of the grown obstacles, made again when either
 * end changed zone, then the two ends. Returns the node count. */
static int build_nodes(s16 x, s16 z, s16 tx, s16 tz)
{
    int count = 0, i;

    if (s_start_zone != s_last_start_zone || s_target_zone != s_last_target_zone) {
        s_last_start_zone = s_start_zone;
        s_last_target_zone = s_target_zone;
        s_nodes_dirty = 1;
        s_matrix_count = 0;
    }
    if (s_nodes_dirty) {
        s_nodes_dirty = 0;
        for (i = 0; i < s_obstacle_count; i++) {
            struct rect *b = &s_grown[i];

            if (b->z1 > b->z2) {
                s16 t = b->z1;
                b->z1 = b->z2;
                b->z2 = t;
            }
            add_corner((s16)(b->x1 - PAD_X), (s16)(b->z1 - PAD_Z), 1, &count);
            add_corner((s16)(b->x2 + PAD_X), (s16)(b->z1 - PAD_Z), 2, &count);
            add_corner((s16)(b->x2 + PAD_X), (s16)(b->z2 + PAD_Z), 3, &count);
            add_corner((s16)(b->x1 - PAD_X), (s16)(b->z2 + PAD_Z), 4, &count);
        }
    } else {
        count = (u16)(s_node_count - 2);
    }
    s_nodes[count].x = x;
    s_nodes[count].z = z;
    s_nodes[count].corner = 0;
    s_nodes[count + 1].x = tx;
    s_nodes[count + 1].z = tz;
    s_nodes[count + 1].corner = 0;
    return count + 2;
}

/* 0x41e56e, 0x41e6a6: whether the segment x1 <= x2 meets an edge of b. */
static int crosses(const struct rect *b, s32 x1, s32 z1, s32 x2, s32 z2)
{
    s32 dx = x2 - x1, dz = z2 - z1;
    s32 lo = z1 <= z2 ? z1 : z2, hi = z1 <= z2 ? z2 : z1;
    s32 e, v;

    e = b->x1;
    if (e >= x1 && e <= x2) {
        v = z1 + ((dx != 0 && dz != 0) ? (e - x1) * dz / dx : 0);
        if (v >= b->z1 && v <= b->z2) return 1;
    }
    e = b->x2;
    if (e >= x1 && e <= x2) {
        v = z1 + ((dx != 0 && dz != 0) ? (e - x1) * dz / dx : 0);
        if (v >= b->z1 && v <= b->z2) return 1;
    }
    e = b->z1;
    if (e >= lo && e <= hi) {
        v = x1 + ((dx != 0 && dz != 0) ? (e - z1) * dx / dz : 0);
        if (v >= b->x1 && v <= b->x2) return 1;
    }
    e = b->z2;
    if (e >= lo && e <= hi) {
        v = x1 + ((dx != 0 && dz != 0) ? (e - z1) * dx / dz : 0);
        if (v >= b->x1 && v <= b->x2) return 1;
    }
    return 0;
}

/* 0x41e7de */
static int segment_blocked(s32 x1, s32 z1, s32 x2, s32 z2)
{
    s32 lo = z1 <= z2 ? z1 : z2, hi = z1 <= z2 ? z2 : z1;
    int i;

    for (i = 0; i < s_obstacle_count; i++) {
        const struct rect *b = &s_grown[i];

        if (b->x1 <= x2 && b->x2 >= x1 && b->z1 <= hi && b->z2 >= lo
            && crosses(b, x1, z1, x2, z2)) {
            return 1;
        }
    }
    return 0;
}

/* 0x42c548: Newton's root of 16 v, that is 4 sqrt(v), in 32 steps at most. */
static u32 isqrt16(u32 v)
{
    u32 r, prev, n = v * 16;
    int i;

    if (v == 0) {
        return 0;
    }
    r = v * 8;
    for (i = 32; i > 0; i--) {
        prev = r;
        r = (n / r + r) >> 1;
        if (r == prev) {
            break;
        }
    }
    return r;
}

/* Whether nodes a and b see each other, and how far apart they are, into
 * both halves of the matrices. */
static void link_nodes(u32 a, u32 b)
{
    const struct node *na = &s_nodes[a], *nb = &s_nodes[b];
    u8 blocked;

    if (na->x > nb->x) {
        blocked = (u8)segment_blocked(nb->x, nb->z, na->x, na->z);
    } else {
        blocked = (u8)segment_blocked(na->x, na->z, nb->x, nb->z);
    }
    s_blocked[a * MAX_NODES + b] = blocked;
    s_blocked[b * MAX_NODES + a] = blocked;
    if (!blocked) {
        s32 dx = nb->x - na->x, dz = nb->z - na->z;
        u32 d = isqrt16((u32)(dx * dx + dz * dz));

        if (d > MAX_LEG) {
            s_blocked[a * MAX_NODES + b] = 1;
            s_blocked[b * MAX_NODES + a] = 1;
        } else {
            s_dist[a * MAX_NODES + b] = d;
            s_dist[b * MAX_NODES + a] = d;
        }
    }
}

/* 0x41e8c4 */
static void build_matrix(u32 count)
{
    u32 i, j;

    for (i = 0; i < count; i++) {
        s_blocked[i * (MAX_NODES + 1)] = 1;
        s_dist[i * (MAX_NODES + 1)] = FAR;
        for (j = i + 1; j < count; j++) {
            link_nodes(i, j);
        }
    }
}

/* 0x41ebc8. The diagonal index is kept in 16 bits, as the original's is. */
static void update_row(u32 node, u32 count)
{
    u32 diag = (u16)(node * (MAX_NODES + 1)), j;

    s_blocked[diag] = 1;
    s_dist[diag] = FAR;
    for (j = 0; j < count; j++) {
        if (j != node) {
            link_nodes(node, j);
        }
    }
}

/* 0x42c834 (skips cur) and the unrolled 0x430081.. (does not; cur is
 * never among a child's candidates). */
static void partition(struct frame *f, int skip_cur)
{
    const u8 *row = s_blocked + f->cur * MAX_NODES;
    u32 i;

    f->nvis = f->nblk = 0;
    for (i = 0; i < f->count; i++) {
        u16 n = f->list[i];

        if (skip_cur && n == f->cur) {
            continue;
        }
        if (row[n] == 0) {
            f->vis[f->nvis++] = n;
        } else {
            f->blk[f->nblk++] = n;
        }
    }
}

/* 0x42c580 (top) and 0x42c6cd. Returns 1 when cur sees the goal; deeper
 * routes are kept in s_best_legs as they are found. */
static int search(struct frame *f, int top)
{
    const u32 *row = s_dist + f->cur * MAX_NODES;
    struct frame c;
    u16 i;

    partition(f, top);
    if (f->nvis == 0) {
        return 0;
    }
    for (i = 0; i < f->nvis; i++) {
        if (f->vis[i] == (u16)s_goal) {
            f->cost += row[s_goal];
            f->leg->from = f->cur;
            f->leg->to = (u16)s_goal;
            if (f->depth == 0) {
                s_best_legs[0] = *f->leg;
                s_leg_count = 1;
                s_best = f->cost;
            }
            return 1;
        }
    }
    c.depth = f->depth + 1;
    if (c.depth == MAX_DEPTH) {
        return 0;
    }
    c.vis = s_vis[c.depth];
    c.blk = s_blk[c.depth];
    c.leg = &s_try_legs[c.depth];
    c.count = f->nblk;
    c.list = f->blk;
    for (i = 0; i < f->nvis; i++) {
        u16 n = f->vis[i];

        c.cost = row[n] + f->cost;
        if (c.cost * s_slack >= s_best << 5) {
            continue;
        }
        f->leg->from = f->cur;
        f->leg->to = n;
        c.cur = n;
        if (search(&c, 0) && c.cost < s_best) {
            s_best = c.cost;
            s_leg_count = (u16)(c.depth + 1);
            memcpy(s_best_legs, s_try_legs, (c.depth + 1) * sizeof(struct leg));
        }
    }
    return 0;
}

/* 0x41edde: the nodes and the matrices, and which end to search from (the
 * one that sees fewer nodes). Returns the node count. */
static int prepare(s16 x, s16 z, s16 tx, s16 tz)
{
    struct frame f;
    u16 start_vis;
    u32 n;

    s_start_zone = zone_of(x, z);
    s_target_zone = zone_of(tx, tz);
    s_slack = 0x20 + (s_target_zone != s_start_zone);
    if (link_of(s_start_zone, s_target_zone) > 0) {
        s_slack++;
    }
    s_node_count = (u16)build_nodes(x, z, tx, tz);
    n = s_node_count;
    if (n == 0) {
        return 0;
    }
    s_goal = n - 1;
    if (s_matrix_count != n) {
        s_matrix_count = (u16)n;
        build_matrix(n);
    } else {
        update_row(n - 2, n);
        update_row(s_goal, n);
    }
    memset(&f, 0, sizeof(f));
    f.cur = (u16)(n - 2);
    f.count = n;
    f.list = s_order;
    f.vis = s_vis[0];
    f.blk = s_blk[0];
    partition(&f, 1);
    start_vis = f.nvis;
    s_goal = n - 2;
    f.cur = (u16)(n - 1);
    partition(&f, 1);
    s_reverse = start_vis > f.nvis;
    return (int)n;
}

/* 0x41ef3c */
int path_find(s16 x, s16 z, s16 tx, s16 tz)
{
    struct frame f;
    u32 n;
    int i;

    s_leg_count = 0;
    if (s_blocked == NULL || prepare(x, z, tx, tz) == 0) {
        return 0;
    }
    n = s_node_count;
    s_best = 0x3ffffff;
    memset(&f, 0, sizeof(f));
    if (!s_reverse) {
        f.cur = (u16)(n - 2);
        s_goal = n - 1;
    } else {
        s_goal = n - 2;
        f.cur = (u16)(n - 1);
    }
    f.count = n;
    f.list = s_order;
    f.vis = s_vis[0];
    f.blk = s_blk[0];
    f.leg = &s_try_legs[0];
    search(&f, 1);
    for (i = 0; i < s_leg_count; i++) {
        if (!s_reverse) {
            s_legs[i] = s_best_legs[i];
        } else {
            const struct leg *l = &s_best_legs[s_leg_count - (i + 1)];

            s_legs[i].from = l->to;
            s_legs[i].to = l->from;
        }
    }
    return (u8)s_leg_count;
}

void path_first_node(s16 *x, s16 *z)
{
    const struct node *n = &s_nodes[s_legs[0].to < MAX_NODES ? s_legs[0].to : 0];

    *x = n->x;
    *z = n->z;
}
