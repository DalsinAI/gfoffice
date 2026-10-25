/*
 * libowf: the formula engine (OpenCalc's). Each formula cell is a node:
 * its compiled code and the areas it reads. An index finds, for any cell,
 * the formulas that read it: one bucket per cell for single-cell
 * references, one per column for narrow ranges, one per sheet for wide
 * ones. Typing a cell marks what reads it dirty (a worklist, not
 * recursion); a recalc works the dirty formulas out in dependency order
 * (Kahn's algorithm), and what is left when no formula is ready is a loop:
 * its strongly connected parts (Tarjan's algorithm, with its own stack)
 * get #REF!, and the formulas after them go on from there.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "owf_calc_int.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ---- A hash map from three numbers to an int ---- */

#define H_EMPTY (-1)
#define H_GONE (-2)

typedef struct {
    unsigned a, b, c;
    int value;
} hslot;

typedef struct {
    hslot *slots;
    unsigned cap, used, live;    /* used counts the gone too */
} hmap;

static unsigned hash3(unsigned a, unsigned b, unsigned c)
{
    unsigned h = a * 2654435761u;
    h ^= b + 0x9E3779B9u + (h << 6) + (h >> 2);
    h ^= c + 0x9E3779B9u + (h << 6) + (h >> 2);
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    return h;
}

static void hmap_free(hmap *m)
{
    free(m->slots);
    memset(m, 0, sizeof *m);
}

static hslot *hmap_slot(const hmap *m, unsigned a, unsigned b, unsigned c)
{
    unsigned i;
    if (!m->cap)
        return NULL;
    for (i = hash3(a, b, c) & (m->cap - 1);; i = (i + 1) & (m->cap - 1)) {
        hslot *s = &m->slots[i];
        if (s->value == H_EMPTY)
            return NULL;
        if (s->value != H_GONE && s->a == a && s->b == b && s->c == c)
            return s;
    }
}

static int hmap_get(const hmap *m, unsigned a, unsigned b, unsigned c)
{
    const hslot *s = hmap_slot(m, a, b, c);
    return s ? s->value : -1;
}

static int hmap_grow(hmap *m)
{
    hmap bigger;
    unsigned i, want = m->cap ? m->cap * 2 : 64;
    while (want < m->live * 2 + 16)
        want *= 2;
    bigger.slots = malloc((size_t)want * sizeof *bigger.slots);
    if (!bigger.slots)
        return 0;
    bigger.cap = want;
    bigger.used = bigger.live = 0;
    for (i = 0; i < want; i++)
        bigger.slots[i].value = H_EMPTY;
    for (i = 0; i < m->cap; i++) {
        hslot *s = &m->slots[i];
        if (s->value >= 0) {
            unsigned k;
            for (k = hash3(s->a, s->b, s->c) & (want - 1); bigger.slots[k].value != H_EMPTY; k = (k + 1) & (want - 1))
                ;
            bigger.slots[k] = *s;
            bigger.used++;
            bigger.live++;
        }
    }
    free(m->slots);
    *m = bigger;
    return 1;
}

static int hmap_put(hmap *m, unsigned a, unsigned b, unsigned c, int value)
{
    hslot *s = hmap_slot(m, a, b, c);
    unsigned i;
    if (s) {
        s->value = value;
        return 1;
    }
    if ((m->used + 1) * 4 > m->cap * 3 && !hmap_grow(m))
        return 0;
    for (i = hash3(a, b, c) & (m->cap - 1); m->slots[i].value >= 0; i = (i + 1) & (m->cap - 1))
        ;
    if (m->slots[i].value == H_EMPTY)
        m->used++;
    m->slots[i].a = a;
    m->slots[i].b = b;
    m->slots[i].c = c;
    m->slots[i].value = value;
    m->live++;
    return 1;
}

static void hmap_del(hmap *m, unsigned a, unsigned b, unsigned c)
{
    hslot *s = hmap_slot(m, a, b, c);
    if (s) {
        s->value = H_GONE;
        m->live--;
    }
}

/* ---- The engine ---- */

enum {
    NODE_ARRAY = 1 << 8,         /* an array formula's cell */
    NODE_QUEUED = 1 << 9,        /* during a recalc */
    NODE_IN_LIST = 1 << 10,
    NODE_DONE = 1 << 11,
    NODE_FREE = 1 << 12,
    NODE_TYPED = 1 << 13         /* the user typed it (else it came from a file or the program) */
};
#define PUBLIC_STATE 0xFF

typedef struct {
    calc_prog prog;              /* no code: a formula that won't parse, or one kept */
    unsigned row;
    unsigned short col;
    unsigned short flags;        /* OWF_CALC_... and NODE_... */
    int sheet;                   /* the free list runs through it when the node is free */
    int first_entry;             /* its entries in the index */
    int indeg;                   /* during a recalc */
    int scc_low;                 /* while looking for loops: a position, then the lowest reached */
    unsigned short scc_on;       /* on Tarjan's stack */
    unsigned short adc;          /* an array formula's cell: where in the result */
    unsigned adr;
} calc_node;

/* What a node reads, filed under a cell, a column or a sheet (the
 * bucket's key holds the sheet). */
typedef struct {
    int node;                    /* -1 when the node has gone */
    unsigned r0, r1;
    unsigned short c0, c1;
    int next;                    /* in its bucket */
    int owner_next;              /* the node's next one */
} dep_entry;

enum { KEY_CELL, KEY_COLUMN, KEY_SHEET };
#define NARROW 4                 /* ranges this many columns wide go in each column's bucket */

struct owf_calc {
    owf_book *book;
    int dmy;
    owf_calc_notify notify;
    void *notify_user;
    calc_node *nodes;
    int nnodes, capnodes, free_node;
    hmap cells;                  /* (sheet, row, col) -> node */
    hmap buckets;                /* (KEY_ << 16 | sheet, a, b) -> first entry */
    dep_entry *entries;
    int nentries, capentries, dead_entries;
    calc_arena arena;
    unsigned long rand_state;
    long used[2];                /* the book's rows and columns used; -1: not known */
    int circular;
    int in_recalc;               /* during owf_calc_recalc: calc_note_reach looks */
    int stale;                   /* the index needs a rebuild (memory ran out) */
    int *work;                   /* worklists */
    int nwork, capwork;
};

static int push_work(owf_calc *calc, int v)
{
    if (calc->nwork == calc->capwork) {
        int want = calc->capwork ? calc->capwork * 2 : 256;
        int *p = realloc(calc->work, (size_t)want * sizeof *p);
        if (!p)
            return 0;
        calc->work = p;
        calc->capwork = want;
    }
    calc->work[calc->nwork++] = v;
    return 1;
}

/* ---- The index ---- */

static int add_entry(owf_calc *calc, int node, unsigned kind, int sheet, unsigned a, unsigned b, const calc_area *area)
{
    dep_entry *e;
    int head;
    if (calc->nentries == calc->capentries) {
        int want = calc->capentries ? calc->capentries * 2 : 256;
        dep_entry *p = realloc(calc->entries, (size_t)want * sizeof *p);
        if (!p)
            return 0;
        calc->entries = p;
        calc->capentries = want;
    }
    head = hmap_get(&calc->buckets, kind << 16 | (unsigned)sheet, a, b);
    if (!hmap_put(&calc->buckets, kind << 16 | (unsigned)sheet, a, b, calc->nentries))
        return 0;
    e = &calc->entries[calc->nentries];
    e->node = node;
    e->r0 = area->r0;
    e->c0 = (unsigned short)area->c0;
    e->r1 = area->r1;
    e->c1 = (unsigned short)area->c1;
    e->next = head;
    e->owner_next = calc->nodes[node].first_entry;
    calc->nodes[node].first_entry = calc->nentries++;
    return 1;
}

typedef struct {
    owf_calc *calc;
    int node;
    int failed;
} area_job;

static void index_area(void *user, const calc_area *a)
{
    area_job *j = user;
    int s;
    for (s = a->s0; s <= a->s1 && !j->failed; s++) {
        unsigned c;
        if (s < 0 || s >= j->calc->book->nsheets)
            continue;
        if (a->r0 == a->r1 && a->c0 == a->c1)
            j->failed = !add_entry(j->calc, j->node, KEY_CELL, s, a->r0, a->c0, a);
        else if (a->c1 - a->c0 < NARROW)
            for (c = a->c0; c <= a->c1 && !j->failed; c++)
                j->failed = !add_entry(j->calc, j->node, KEY_COLUMN, s, c, 0, a);
        else
            j->failed = !add_entry(j->calc, j->node, KEY_SHEET, s, 0, 0, a);
    }
}

static void unindex(owf_calc *calc, int node)
{
    int e = calc->nodes[node].first_entry;
    while (e >= 0) {
        calc->entries[e].node = -1;
        calc->dead_entries++;
        e = calc->entries[e].owner_next;
    }
    calc->nodes[node].first_entry = -1;
}

/* Calls fn for each node that reads (sheet, row, col), once for each of
 * its entries there. */
typedef void (*dep_fn)(owf_calc *calc, int node, void *user);

static void each_dependent(owf_calc *calc, int sheet, unsigned row, unsigned col, dep_fn fn, void *user)
{
    int e;
    for (e = hmap_get(&calc->buckets, KEY_CELL << 16 | (unsigned)sheet, row, col); e >= 0; e = calc->entries[e].next)
        if (calc->entries[e].node >= 0)
            fn(calc, calc->entries[e].node, user);
    for (e = hmap_get(&calc->buckets, KEY_COLUMN << 16 | (unsigned)sheet, col, 0); e >= 0; e = calc->entries[e].next) {
        const dep_entry *d = &calc->entries[e];
        if (d->node >= 0 && row >= d->r0 && row <= d->r1)
            fn(calc, d->node, user);
    }
    for (e = hmap_get(&calc->buckets, KEY_SHEET << 16 | (unsigned)sheet, 0, 0); e >= 0; e = calc->entries[e].next) {
        const dep_entry *d = &calc->entries[e];
        if (d->node >= 0 && row >= d->r0 && row <= d->r1 && col >= d->c0 && col <= d->c1)
            fn(calc, d->node, user);
    }
}

/* ---- Nodes ---- */

static int node_at(const owf_calc *calc, int sheet, unsigned row, unsigned col)
{
    return hmap_get(&calc->cells, (unsigned)sheet, row, col);
}

static void remove_node(owf_calc *calc, int id)
{
    calc_node *n = &calc->nodes[id];
    unindex(calc, id);
    hmap_del(&calc->cells, (unsigned)n->sheet, n->row, n->col);
    calc_prog_free(&n->prog);
    n->flags = NODE_FREE;
    n->sheet = calc->free_node;          /* the free list runs through sheet */
    calc->free_node = id;
}

/* Where an array formula's cell is in its range: up and left while the
 * cells there are of the same array formula. */
static void array_offset(const owf_sheet *sheet, const owf_cell *cell, unsigned *dr, unsigned *dc)
{
    const owf_cell *c;
    unsigned r = cell->row, k = cell->col;
    while (r > 0 && (c = owf_sheet_find(sheet, r - 1, cell->col)) && (c->flags & OWF_CELL_ARRAY) && c->formula &&
           !strcmp(c->formula, cell->formula))
        r--;
    while (k > 0 && (c = owf_sheet_find(sheet, cell->row, k - 1)) && (c->flags & OWF_CELL_ARRAY) && c->formula &&
           !strcmp(c->formula, cell->formula))
        k--;
    *dr = cell->row - r;
    *dc = cell->col - k;
}

/* A node for a formula cell. typed: the user typed it (a function the
 * engine lacks gives #NAME?); else from a file (the cell keeps its value).
 * OWF_OK, OWF_ERR_FORMAT (typed, won't parse: a node all the same) or
 * OWF_ERR_MEMORY (no node). */
static int add_node(owf_calc *calc, int sheet, const owf_cell *cell, int typed, int *id_out)
{
    calc_node *n;
    calc_prog prog;
    int id, result, array = (cell->flags & OWF_CELL_ARRAY) != 0;
    area_job job;

    result = calc_compile(calc->book, sheet, cell->formula, array, &prog, NULL);
    if (result == OWF_ERR_MEMORY)
        return OWF_ERR_MEMORY;
    if (calc->free_node >= 0) {
        id = calc->free_node;
        calc->free_node = calc->nodes[id].sheet;
    } else {
        if (calc->nnodes == calc->capnodes) {
            int want = calc->capnodes ? calc->capnodes * 2 : 64;
            calc_node *p = realloc(calc->nodes, (size_t)want * sizeof *p);
            if (!p) {
                calc_prog_free(&prog);
                return OWF_ERR_MEMORY;
            }
            calc->nodes = p;
            calc->capnodes = want;
        }
        id = calc->nnodes++;
    }
    n = &calc->nodes[id];
    memset(n, 0, sizeof *n);
    n->sheet = sheet;
    n->row = cell->row;
    n->col = (unsigned short)cell->col;
    n->first_entry = -1;
    n->flags = (unsigned short)(OWF_CALC_FORMULA | (typed ? NODE_TYPED : 0));
    if (array) {
        n->flags |= NODE_ARRAY;
        unsigned dc;
        array_offset(&calc->book->sheets[sheet], cell, &n->adr, &dc);
        n->adc = (unsigned short)dc;
    }
    if (result == OWF_OK && (prog.flags & PROG_BADARGS) && typed) {
        calc_prog_free(&prog);
        result = OWF_ERR_FORMAT;
    }
    if (result != OWF_OK) {
        n->flags |= typed ? OWF_CALC_BAD : OWF_CALC_KEPT;
    } else if (!typed && (prog.flags & (PROG_UNKNOWN | PROG_EXTERN | PROG_BADARGS))) {
        n->flags |= OWF_CALC_KEPT;
        calc_prog_free(&prog);
    } else {
        n->prog = prog;
        if (prog.flags & PROG_VOLATILE)
            n->flags |= OWF_CALC_VOLATILE;
    }
    if (!hmap_put(&calc->cells, (unsigned)sheet, n->row, n->col, id)) {
        calc_prog_free(&n->prog);
        n->flags = NODE_FREE;
        n->sheet = calc->free_node;
        calc->free_node = id;
        return OWF_ERR_MEMORY;
    }
    if (n->prog.code) {
        job.calc = calc;
        job.node = id;
        job.failed = 0;
        calc_prog_areas(&n->prog, index_area, &job);
        if (job.failed) {
            remove_node(calc, id);
            return OWF_ERR_MEMORY;
        }
    }
    *id_out = id;
    return result == OWF_OK ? OWF_OK : typed ? OWF_ERR_FORMAT : OWF_OK;
}

static void free_all(owf_calc *calc)
{
    int i;
    for (i = 0; i < calc->nnodes; i++)
        if (!(calc->nodes[i].flags & NODE_FREE))
            calc_prog_free(&calc->nodes[i].prog);
    free(calc->nodes);
    calc->nodes = NULL;
    calc->nnodes = calc->capnodes = 0;
    calc->free_node = -1;
    hmap_free(&calc->cells);
    hmap_free(&calc->buckets);
    free(calc->entries);
    calc->entries = NULL;
    calc->nentries = calc->capentries = calc->dead_entries = 0;
}

static int build(owf_calc *calc, int dirty)
{
    int s, i, id, count = 0;
    free_all(calc);
    calc->used[0] = calc->used[1] = -1;
    /* the nodes' room in one go, not doubling (half of it would be spare) */
    for (s = 0; s < calc->book->nsheets; s++)
        for (i = 0; i < calc->book->sheets[s].ncells; i++)
            count += calc->book->sheets[s].cells[i].formula != NULL;
    if (count) {
        calc->nodes = malloc(((size_t)count + 16) * sizeof *calc->nodes);
        if (!calc->nodes) {
            calc->stale = 1;
            return OWF_ERR_MEMORY;
        }
        calc->capnodes = count + 16;
    }
    for (s = 0; s < calc->book->nsheets; s++) {
        const owf_sheet *sheet = &calc->book->sheets[s];
        for (i = 0; i < sheet->ncells; i++) {
            if (!sheet->cells[i].formula)
                continue;
            if (add_node(calc, s, &sheet->cells[i], 0, &id) == OWF_ERR_MEMORY) {
                free_all(calc);
                calc->stale = 1;
                return OWF_ERR_MEMORY;
            }
            if (dirty)
                calc->nodes[id].flags |= OWF_CALC_DIRTY;
        }
    }
    calc->stale = 0;
    return OWF_OK;
}

/* The index sheds the entries of nodes gone once they outnumber the rest. */
static int tidy(owf_calc *calc)
{
    int id;
    if (calc->dead_entries < 1024 || calc->dead_entries * 2 < calc->nentries)
        return OWF_OK;
    hmap_free(&calc->buckets);
    free(calc->entries);
    calc->entries = NULL;
    calc->nentries = calc->capentries = calc->dead_entries = 0;
    for (id = 0; id < calc->nnodes; id++) {
        area_job job;
        calc_node *n = &calc->nodes[id];
        if (n->flags & NODE_FREE)
            continue;
        n->first_entry = -1;
        if (!n->prog.code)
            continue;
        job.calc = calc;
        job.node = id;
        job.failed = 0;
        calc_prog_areas(&n->prog, index_area, &job);
        if (job.failed) {
            calc->stale = 1;
            return OWF_ERR_MEMORY;
        }
    }
    return OWF_OK;
}

owf_calc *owf_calc_new(owf_book *book)
{
    owf_calc *calc = calloc(1, sizeof *calc);
    owf_time t;
    if (!calc)
        return NULL;
    calc->book = book;
    calc->dmy = OWF_CALC_DMY;
    calc->free_node = -1;
    calc->used[0] = calc->used[1] = -1;
    calc->rand_state = 0x2545F491UL;
    if (owf_sys_now(&t))
        calc->rand_state ^= (unsigned long)(t.second + 60 * (t.minute + 60 * (t.hour + 24 * t.day))) * 2654435761UL;
    if (build(calc, 0) != OWF_OK) {
        owf_calc_free(calc);
        return NULL;
    }
    return calc;
}

void owf_calc_free(owf_calc *calc)
{
    if (!calc)
        return;
    free_all(calc);
    calc_arena_free(&calc->arena);
    free(calc->work);
    free(calc);
}

int owf_calc_rebuild(owf_calc *calc)
{
    return build(calc, 1);
}

void owf_calc_set_date_order(owf_calc *calc, int order)
{
    calc->dmy = order == OWF_CALC_MDY ? OWF_CALC_MDY : OWF_CALC_DMY;
}

void owf_calc_set_notify(owf_calc *calc, owf_calc_notify notify, void *user)
{
    calc->notify = notify;
    calc->notify_user = user;
}

unsigned owf_calc_state(const owf_calc *calc, int sheet, unsigned row, unsigned col)
{
    int id = node_at(calc, sheet, row, col);
    return id >= 0 ? calc->nodes[id].flags & PUBLIC_STATE : 0;
}

int owf_calc_circular_count(const owf_calc *calc)
{
    return calc->circular;
}

/* ---- RAND and the clock ---- */

double calc_random(calc_ctx *ctx)
{
    static unsigned long fallback = 0x9E3779B9UL;
    unsigned long *s = ctx->calc ? &ctx->calc->rand_state : &fallback, x = *s & 0xFFFFFFFFUL;
    unsigned long hi, lo;
    /* xorshift32, twice for 53 bits */
    x ^= (x << 13) & 0xFFFFFFFFUL;
    x ^= x >> 17;
    x ^= (x << 5) & 0xFFFFFFFFUL;
    hi = x >> 5;                         /* 27 bits */
    x ^= (x << 13) & 0xFFFFFFFFUL;
    x ^= x >> 17;
    x ^= (x << 5) & 0xFFFFFFFFUL;
    lo = x >> 6;                         /* 26 bits */
    *s = x;
    return ((double)hi * 67108864.0 + (double)lo) / 9007199254740992.0;
}

double calc_now(calc_ctx *ctx)
{
    owf_time t;
    double serial;
    if (!owf_sys_now(&t) || !calc_date_serial(t.year, t.month, t.day, ctx->book->date1904, &serial))
        return 0;
    return serial + (t.hour * 3600.0 + t.minute * 60.0 + t.second) / 86400.0;
}

size_t calc_memory(const owf_calc *calc)
{
    size_t n = (size_t)calc->capnodes * sizeof(calc_node) + (size_t)calc->capentries * sizeof(dep_entry) +
               ((size_t)calc->cells.cap + calc->buckets.cap) * sizeof(hslot) + (size_t)calc->capwork * sizeof(int) +
               (size_t)calc->arena.total + sizeof *calc;
    int i;
    for (i = 0; i < calc->nnodes; i++)
        n += calc->nodes[i].prog.len;
    return n;
}

long *calc_used_cache(owf_calc *calc)
{
    return calc ? calc->used : NULL;
}

/* A cell an OFFSET or INDIRECT reached: a formula still to be worked out
 * in this recalc (not the formula's own cell)? */
static int still_to_do(const owf_calc *calc, const calc_ctx *ctx, int sheet, unsigned row, unsigned col)
{
    int id;
    if (sheet == ctx->sheet && row == ctx->row && col == ctx->col)
        return 0;
    id = node_at(calc, sheet, row, col);
    return id >= 0 && (calc->nodes[id].flags & (NODE_IN_LIST | NODE_DONE)) == NODE_IN_LIST;
}

/* The first cell at or after (row, col), from cell `lo` on, by halves. */
static int seek_cell(const owf_sheet *s, int lo, unsigned row, unsigned col)
{
    int hi = s->ncells;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        const owf_cell *c = &s->cells[mid];
        if (c->row < row || (c->row == row && c->col < col))
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

void calc_note_reach(calc_ctx *ctx, const calc_area *a)
{
    const owf_calc *calc = ctx->calc;
    int sheet, i;
    if (!calc || ctx->pending || !calc->in_recalc)
        return;
    for (sheet = a->s0; sheet <= a->s1; sheet++) {
        const owf_sheet *s;
        if (sheet < 0 || sheet >= calc->book->nsheets)
            continue;
        s = &calc->book->sheets[sheet];
        /* each row's cells from c0 to c1, found by halves (a formula's
         * cell may not have a value yet, so every cell counts) */
        i = seek_cell(s, 0, a->r0, a->c0);
        while (i < s->ncells) {
            const owf_cell *c = &s->cells[i];
            if (c->row > a->r1)
                break;
            if (c->col < a->c0) {
                i = seek_cell(s, i, c->row, a->c0);
                continue;
            }
            if (c->col > a->c1) {
                if (c->row == a->r1)
                    break;
                i = seek_cell(s, i, c->row + 1, a->c0);
                continue;
            }
            if (c->formula && still_to_do(calc, ctx, sheet, c->row, c->col)) {
                ctx->pending = 1;
                return;
            }
            i++;
        }
    }
}

/* ---- Dirty marking ---- */

static void mark_one(owf_calc *calc, int node, void *user)
{
    (void)user;
    if (!(calc->nodes[node].flags & OWF_CALC_DIRTY)) {
        calc->nodes[node].flags |= OWF_CALC_DIRTY;
        if (!push_work(calc, node))
            calc->stale = 1;
    }
}

/* Everything that reads (sheet, row, col), and what reads that... */
static void mark_from(owf_calc *calc, int sheet, unsigned row, unsigned col)
{
    calc->nwork = 0;
    each_dependent(calc, sheet, row, col, mark_one, NULL);
    while (calc->nwork) {
        const calc_node *n = &calc->nodes[calc->work[--calc->nwork]];
        each_dependent(calc, n->sheet, n->row, n->col, mark_one, NULL);
    }
}

/* ---- Working a node out ---- */

static int same_value(const owf_cell *c, const calc_value *v)
{
    switch (v->type) {
    case CV_NUM: return c->type == OWF_VALUE_NUMBER && c->number == v->u.num;
    case CV_BOOL: return c->type == OWF_VALUE_BOOL && c->number == v->u.num;
    case CV_ERR: return c->type == OWF_VALUE_ERROR && c->error == v->u.err;
    case CV_STR:
        return c->type == OWF_VALUE_TEXT && c->text && strlen(c->text) == v->u.str.len &&
               !memcmp(c->text, v->u.str.p, v->u.str.len);
    }
    return 0;
}

/* Puts a result into a cell; 1 when it changed, -1 when memory ran out. */
static int store(owf_cell *c, const calc_value *in)
{
    calc_value v = *in;
    if (v.type == CV_EMPTY || v.type == CV_MISSING)
        cv_num(&v, 0);                   /* =A1 of an empty A1 is 0 */
    if (same_value(c, &v))
        return 0;
    switch (v.type) {
    case CV_NUM: owf_cell_set_number(c, v.u.num); break;
    case CV_BOOL: owf_cell_set_bool(c, v.u.num != 0); break;
    case CV_ERR: owf_cell_set_error(c, v.u.err); break;
    case CV_STR:
        if (owf_cell_set_text(c, v.u.str.p, v.u.str.len) < 0)
            return -1;
        break;
    default:
        owf_cell_set_number(c, 0);
        break;
    }
    return 1;
}

/* Works a node out and stores its value. With `wait` (not NULL), a formula
 * whose OFFSET or INDIRECT reached one still to be worked out keeps its
 * value and *wait is set. */
static int evaluate(owf_calc *calc, int id, int *wait)
{
    calc_node *n = &calc->nodes[id];
    owf_cell *cell = owf_sheet_cell(&calc->book->sheets[n->sheet], n->row, n->col, 0);
    calc_value v;
    calc_ctx ctx;
    int changed;
    if (wait)
        *wait = 0;
    if (!cell || (n->flags & OWF_CALC_KEPT))
        return OWF_OK;
    if (n->flags & OWF_CALC_BAD) {
        cv_err(&v, OWF_ERROR_NAME);
    } else {
        calc_arena_reset(&calc->arena);
        calc_ctx_init(&ctx, calc, calc->book, &calc->arena, n->sheet, n->row, n->col);
        ctx.dmy = calc->dmy;
        if (n->flags & NODE_ARRAY) {
            ctx.array = 1;
            ctx.adr = n->adr;
            ctx.adc = n->adc;
        }
        calc_run(&ctx, &n->prog, &v);
        if (ctx.failed)
            return OWF_ERR_MEMORY;
        if (ctx.pending && wait) {
            *wait = 1;
            return OWF_OK;
        }
    }
    changed = store(cell, &v);
    if (changed < 0)
        return OWF_ERR_MEMORY;
    if (changed && calc->notify)
        calc->notify(calc->notify_user, n->sheet, n->row, n->col);
    return OWF_OK;
}

/* ---- Recalculation ---- */

static void count_in(owf_calc *calc, int node, void *user)
{
    (void)user;
    if (calc->nodes[node].flags & NODE_IN_LIST)
        calc->nodes[node].indeg++;
}

typedef struct {
    int *queue;
    int head, tail;
} ready;

static void count_out(owf_calc *calc, int node, void *user)
{
    ready *q = user;
    calc_node *n = &calc->nodes[node];
    if ((n->flags & NODE_IN_LIST) && !(n->flags & NODE_DONE) && --n->indeg == 0 && !(n->flags & NODE_QUEUED)) {
        n->flags |= NODE_QUEUED;
        q->queue[q->tail++] = node;
    }
}

/* The loops among the nodes left: Tarjan's strongly connected parts, its
 * recursion done with a stack of our own. Nodes in a loop are marked
 * OWF_CALC_CIRCULAR. */
typedef struct {
    int *adj_start, *adj;        /* edges among the nodes left, node -> readers */
    int nadj, capadj;
    int failed;
    int from;
} edges;

static void add_edge(owf_calc *calc, int node, void *user)
{
    edges *g = user;
    if (!(calc->nodes[node].flags & NODE_IN_LIST) || (calc->nodes[node].flags & NODE_DONE))
        return;
    if (g->nadj == g->capadj) {
        int want = g->capadj ? g->capadj * 2 : 256, *p = realloc(g->adj, (size_t)want * sizeof *p);
        if (!p) {
            g->failed = 1;
            return;
        }
        g->adj = p;
        g->capadj = want;
    }
    g->adj[g->nadj++] = node;
}

static int find_loops(owf_calc *calc, const int *left, int nleft)
{
    edges g;
    int *pos = NULL, *stack = NULL, *call = NULL, *edge_at = NULL, i, index = 0, nstack = 0, ncall;
    int result = OWF_ERR_MEMORY;

    memset(&g, 0, sizeof g);
    g.adj_start = malloc(((size_t)nleft + 1) * sizeof *g.adj_start);
    pos = malloc((size_t)nleft * sizeof *pos);
    stack = malloc((size_t)nleft * sizeof *stack);
    call = malloc((size_t)nleft * sizeof *call);
    edge_at = malloc((size_t)nleft * sizeof *edge_at);
    if (!g.adj_start || !pos || !stack || !call || !edge_at)
        goto out;
    /* node ids to positions in left[], through scc_low for now */
    for (i = 0; i < nleft; i++) {
        calc->nodes[left[i]].scc_low = i;
        calc->nodes[left[i]].scc_on = 0;
    }
    for (i = 0; i < nleft; i++) {
        const calc_node *n = &calc->nodes[left[i]];
        g.adj_start[i] = g.nadj;
        each_dependent(calc, n->sheet, n->row, n->col, add_edge, &g);
        if (g.failed)
            goto out;
    }
    g.adj_start[nleft] = g.nadj;
    for (i = 0; i < g.nadj; i++)
        g.adj[i] = calc->nodes[g.adj[i]].scc_low;   /* as positions */
    for (i = 0; i < nleft; i++)
        pos[i] = -1;                     /* the visit index; -1 not yet */
    for (i = 0; i < nleft; i++) {
        if (pos[i] >= 0)
            continue;
        ncall = 0;
        call[ncall] = i;
        edge_at[ncall++] = g.adj_start[i];
        pos[i] = index;
        calc->nodes[left[i]].scc_low = index++;
        calc->nodes[left[i]].scc_on = 1;
        stack[nstack++] = i;
        while (ncall) {
            int v = call[ncall - 1];
            if (edge_at[ncall - 1] < g.adj_start[v + 1]) {
                int w = g.adj[edge_at[ncall - 1]++];
                if (pos[w] < 0) {
                    pos[w] = index;
                    calc->nodes[left[w]].scc_low = index++;
                    calc->nodes[left[w]].scc_on = 1;
                    stack[nstack++] = w;
                    call[ncall] = w;
                    edge_at[ncall++] = g.adj_start[w];
                } else if (calc->nodes[left[w]].scc_on) {
                    if (pos[w] < calc->nodes[left[v]].scc_low)
                        calc->nodes[left[v]].scc_low = pos[w];
                }
                continue;
            }
            /* v is done: a root takes its part off the stack */
            if (calc->nodes[left[v]].scc_low == pos[v]) {
                int size = 0, k, self = 0;
                for (k = nstack - 1; k >= 0; k--) {
                    size++;
                    if (stack[k] == v)
                        break;
                }
                if (size == 1) {
                    int e;
                    for (e = g.adj_start[v]; e < g.adj_start[v + 1]; e++)
                        if (g.adj[e] == v)
                            self = 1;
                }
                for (k = 0; k < size; k++) {
                    int w = stack[--nstack];
                    calc->nodes[left[w]].scc_on = 0;
                    if (size > 1 || self)
                        calc->nodes[left[w]].flags |= OWF_CALC_CIRCULAR;
                }
            }
            ncall--;
            if (ncall) {
                int u = call[ncall - 1];
                if (calc->nodes[left[v]].scc_low < calc->nodes[left[u]].scc_low)
                    calc->nodes[left[u]].scc_low = calc->nodes[left[v]].scc_low;
            }
        }
    }
    result = OWF_OK;
out:
    free(g.adj_start);
    free(g.adj);
    free(pos);
    free(stack);
    free(call);
    free(edge_at);
    return result;
}

static int recalc(owf_calc *calc, int all)
{
    int *list = NULL, *left = NULL, *waiting = NULL, nlist = 0, nwaiting = 0, id, i, done = 0, result = OWF_OK;
    int done_before = -1, force = 0, wait;
    ready q;

    if (calc->stale && build(calc, 1) != OWF_OK)
        return OWF_ERR_MEMORY;
    if (tidy(calc) != OWF_OK && build(calc, 1) != OWF_OK)
        return OWF_ERR_MEMORY;
    calc->used[0] = calc->used[1] = -1;
    for (id = 0; id < calc->nnodes; id++) {
        calc_node *n = &calc->nodes[id];
        if (n->flags & NODE_FREE)
            continue;
        if (all)
            n->flags |= OWF_CALC_DIRTY;
        else if ((n->flags & OWF_CALC_VOLATILE) && !(n->flags & OWF_CALC_DIRTY)) {
            n->flags |= OWF_CALC_DIRTY;
            mark_from(calc, n->sheet, n->row, n->col);
        }
    }
    if (calc->stale)
        return OWF_ERR_MEMORY;
    for (id = 0; id < calc->nnodes; id++)
        nlist += (calc->nodes[id].flags & (NODE_FREE | OWF_CALC_DIRTY)) == OWF_CALC_DIRTY;
    calc->circular = 0;
    if (!nlist)
        return OWF_OK;
    list = malloc((size_t)nlist * sizeof *list);
    left = malloc((size_t)nlist * sizeof *left);
    waiting = malloc((size_t)nlist * sizeof *waiting);
    q.queue = malloc((size_t)nlist * sizeof *q.queue);
    if (!list || !left || !waiting || !q.queue) {
        free(list);
        free(left);
        free(waiting);
        free(q.queue);
        return OWF_ERR_MEMORY;
    }
    calc->in_recalc = 1;
    q.head = q.tail = 0;
    nlist = 0;
    for (id = 0; id < calc->nnodes; id++) {
        calc_node *n = &calc->nodes[id];
        if ((n->flags & (NODE_FREE | OWF_CALC_DIRTY)) == OWF_CALC_DIRTY) {
            n->flags = (unsigned short)((n->flags | NODE_IN_LIST) & ~(unsigned)(OWF_CALC_CIRCULAR | NODE_DONE | NODE_QUEUED));
            n->indeg = 0;
            list[nlist++] = id;
        }
    }
    /* how many dirty formulas each one waits for */
    for (i = 0; i < nlist; i++) {
        const calc_node *n = &calc->nodes[list[i]];
        each_dependent(calc, n->sheet, n->row, n->col, count_in, NULL);
    }
    for (i = 0; i < nlist; i++)
        if (!calc->nodes[list[i]].indeg) {
            calc->nodes[list[i]].flags |= NODE_QUEUED;
            q.queue[q.tail++] = list[i];
        }
    while (done < nlist) {
        while (q.head < q.tail) {
            calc_node *n;
            id = q.queue[q.head++];
            if ((result = evaluate(calc, id, force ? NULL : &wait)) != OWF_OK)
                goto out;
            if (!force && wait) {        /* after what its OFFSET or INDIRECT reached */
                waiting[nwaiting++] = id;
                continue;
            }
            n = &calc->nodes[id];
            n->flags = (unsigned short)((n->flags | NODE_DONE) & ~(unsigned)OWF_CALC_DIRTY);
            done++;
            each_dependent(calc, n->sheet, n->row, n->col, count_out, &q);
        }
        force = 0;
        if (done < nlist && nwaiting) {
            /* formulas that waited go again; when the last round got none
               of them done, they are worked out as they are */
            force = done == done_before;
            done_before = done;
            q.head = q.tail = 0;
            for (i = 0; i < nwaiting; i++)
                q.queue[q.tail++] = waiting[i];
            nwaiting = 0;
            continue;
        }
        if (done < nlist) {
            /* nothing is ready: loops. Those in one get #REF!, then on. */
            int nleft = 0;
            for (i = 0; i < nlist; i++)
                if (!(calc->nodes[list[i]].flags & NODE_DONE))
                    left[nleft++] = list[i];
            if ((result = find_loops(calc, left, nleft)) != OWF_OK)
                goto out;
            for (i = 0; i < nleft; i++) {
                calc_node *n = &calc->nodes[left[i]];
                owf_cell *cell;
                if (!(n->flags & OWF_CALC_CIRCULAR))
                    continue;
                cell = owf_sheet_cell(&calc->book->sheets[n->sheet], n->row, n->col, 0);
                if (cell && !(cell->type == OWF_VALUE_ERROR && cell->error == OWF_ERROR_REF)) {
                    owf_cell_set_error(cell, OWF_ERROR_REF);
                    if (calc->notify)
                        calc->notify(calc->notify_user, n->sheet, n->row, n->col);
                }
                n->flags = (unsigned short)((n->flags | NODE_DONE | NODE_QUEUED) & ~(unsigned)OWF_CALC_DIRTY);
                calc->circular++;
                done++;
            }
            q.head = q.tail = 0;         /* the queue's room is free again */
            for (i = 0; i < nleft; i++) {
                const calc_node *n = &calc->nodes[left[i]];
                if (n->flags & OWF_CALC_CIRCULAR)
                    each_dependent(calc, n->sheet, n->row, n->col, count_out, &q);
            }
            if (q.head == q.tail && done < nlist) {
                result = OWF_ERR_CORRUPT;  /* cannot happen: what is left after the loops has no loop */
                goto out;
            }
        }
    }
out:
    calc->in_recalc = 0;
    for (i = 0; i < nlist; i++)
        calc->nodes[list[i]].flags &= (unsigned short)~(unsigned)(NODE_IN_LIST | NODE_DONE | NODE_QUEUED);
    free(list);
    free(left);
    free(waiting);
    free(q.queue);
    calc_arena_reset(&calc->arena);
    return result;
}

int owf_calc_recalc(owf_calc *calc)
{
    return recalc(calc, 0);
}

int owf_calc_recalc_all(owf_calc *calc)
{
    return recalc(calc, 1);
}

/* ---- Typing into a cell ---- */

static int set_format(owf_book *book, owf_cell *cell, const char *format)
{
    owf_cellstyle style;
    int k, s;
    if (cell->style < 0 || cell->style >= book->nstyles || book->styles[cell->style].number_format != 0)
        return 0;
    k = owf_book_numfmt(book, format);
    if (k < 0)
        return -1;
    style = book->styles[cell->style];
    style.number_format = k;
    s = owf_book_style(book, &style);
    if (s < 0)
        return -1;
    cell->style = s;
    return 0;
}

/* The format a typed number asks for, or NULL. */
static const char *typed_format(owf_calc *calc, int kind, int decimals, const char *currency, char *buf, size_t size)
{
    switch (kind) {
    case NK_PERCENT:
        return decimals ? "0.00%" : "0%";
    case NK_CURRENCY:
        if (strlen(currency) + 12 >= size)
            return NULL;
        strcpy(buf, currency);
        strcat(buf, decimals ? "#,##0.00" : "#,##0");
        return buf;
    case NK_TIME:
        return (decimals & 2) ? ((decimals & 1) ? "h:mm:ss AM/PM" : "h:mm AM/PM") : ((decimals & 1) ? "h:mm:ss" : "h:mm");
    case NK_DATE:
    case NK_DATETIME:
        switch (decimals) {
        case 1: return kind == NK_DATE ? "dd-mmm-yy" : "dd-mmm-yy h:mm";
        case 2: return kind == NK_DATE ? "dd-mmm" : "dd-mmm h:mm";
        case 3: return "mmm-yy";
        case 4: return kind == NK_DATE ? "yyyy-mm-dd" : "yyyy-mm-dd h:mm";
        }
        if (calc->dmy == OWF_CALC_MDY)
            return kind == NK_DATE ? "mm/dd/yyyy" : "mm/dd/yyyy h:mm";
        return kind == NK_DATE ? "dd/mm/yyyy" : "dd/mm/yyyy h:mm";
    }
    return NULL;
}

static int error_word(const char *s)
{
    static const int errors[] = { OWF_ERROR_NULL, OWF_ERROR_DIV0, OWF_ERROR_VALUE, OWF_ERROR_REF, OWF_ERROR_NAME,
                                  OWF_ERROR_NUM, OWF_ERROR_NA };
    size_t i;
    for (i = 0; i < sizeof errors / sizeof errors[0]; i++)
        if (owf_same_word(s, owf_error_name(errors[i])))
            return errors[i];
    return -1;
}

int owf_calc_enter(owf_calc *calc, int sheet, unsigned row, unsigned col, const char *input)
{
    owf_sheet *s;
    owf_cell *cell;
    int old, id, result = OWF_OK;

    if (sheet < 0 || sheet >= calc->book->nsheets || row >= CALC_MAX_ROWS || col >= CALC_MAX_COLS || !input)
        return OWF_ERR_UNSUPPORTED;
    s = &calc->book->sheets[sheet];
    old = node_at(calc, sheet, row, col);
    if (input[0] == '=' && input[1]) {
        /* a formula, kept as Excel keeps one */
        char *text = calc_normalize(input + 1);
        if (!text)
            return OWF_ERR_MEMORY;
        if (!(cell = owf_sheet_cell(s, row, col, 1)) || owf_cell_set_formula(cell, text) < 0) {
            free(text);
            return OWF_ERR_MEMORY;
        }
        free(text);
        cell->flags &= ~(unsigned)OWF_CELL_ARRAY;
        if (old >= 0)
            remove_node(calc, old);
        result = add_node(calc, sheet, cell, 1, &id);
        if (result == OWF_ERR_MEMORY) {
            calc->stale = 1;
            return OWF_ERR_MEMORY;
        }
        calc->nodes[id].flags |= OWF_CALC_DIRTY;
        if (calc->nodes[id].flags & OWF_CALC_BAD)
            owf_cell_set_error(cell, OWF_ERROR_NAME);
    } else {
        double number;
        int kind, decimals, error;
        char currency[8], buf[32];
        const char *format = NULL;
        cell = owf_sheet_cell(s, row, col, *input != 0);
        if (!cell) {
            if (*input)
                return OWF_ERR_MEMORY;
            return OWF_OK;               /* clearing a cell that is not there */
        }
        if (old >= 0)
            remove_node(calc, old);
        owf_cell_set_formula(cell, NULL);
        cell->flags &= ~(unsigned)OWF_CELL_ARRAY;
        if (!*input) {
            owf_cell_set_number(cell, 0);     /* frees any text */
            cell->type = OWF_VALUE_EMPTY;
        } else if (input[0] == '\'') {
            if (owf_cell_set_text(cell, input + 1, strlen(input + 1)) < 0)
                return OWF_ERR_MEMORY;
        } else if (owf_same_word(input, "TRUE") || owf_same_word(input, "FALSE")) {
            owf_cell_set_bool(cell, owf_same_word(input, "TRUE"));
        } else if ((error = error_word(input)) >= 0) {
            owf_cell_set_error(cell, error);
        } else if (calc_parse_number(input, strlen(input), calc->dmy, calc->book->date1904, &number, &kind,
                                     &decimals, currency, sizeof currency)) {
            owf_cell_set_number(cell, number);
            format = typed_format(calc, kind, decimals, currency, buf, sizeof buf);
        } else if (owf_cell_set_text(cell, input, strlen(input)) < 0) {
            return OWF_ERR_MEMORY;
        }
        if (format && set_format(calc->book, cell, format) < 0)
            return OWF_ERR_MEMORY;
    }
    if (calc->used[0] >= 0) {
        if ((long)row >= calc->used[0])
            calc->used[0] = (long)row + 1;
        if ((long)col >= calc->used[1])
            calc->used[1] = (long)col + 1;
    }
    mark_from(calc, sheet, row, col);
    return calc->stale ? OWF_ERR_MEMORY : result;
}

/* ---- Working out a formula on its own ---- */

int owf_calc_evaluate(owf_calc *calc, int sheet, unsigned row, unsigned col, const char *formula, owf_cell *result)
{
    calc_prog prog;
    calc_ctx ctx;
    calc_value v;
    int r = OWF_OK;
    memset(result, 0, sizeof *result);
    result->row = row;
    result->col = col;
    result->type = OWF_VALUE_EMPTY;
    if (sheet < 0 || sheet >= calc->book->nsheets)
        return OWF_ERR_UNSUPPORTED;
    if (formula[0] == '=')
        formula++;
    r = calc_compile(calc->book, sheet, formula, 0, &prog, NULL);
    if (r != OWF_OK)
        return r;
    if (prog.flags & PROG_BADARGS) {     /* as typed: refused */
        calc_prog_free(&prog);
        return OWF_ERR_FORMAT;
    }
    calc_arena_reset(&calc->arena);
    calc_ctx_init(&ctx, calc, calc->book, &calc->arena, sheet, row, col);
    ctx.dmy = calc->dmy;
    calc_run(&ctx, &prog, &v);
    if (ctx.failed) {
        r = OWF_ERR_MEMORY;
        goto out;
    }
    switch (v.type) {
    case CV_NUM:
        result->type = OWF_VALUE_NUMBER;
        result->number = v.u.num;
        break;
    case CV_BOOL:
        result->type = OWF_VALUE_BOOL;
        result->number = v.u.num;
        break;
    case CV_ERR:
        result->type = OWF_VALUE_ERROR;
        result->error = v.u.err;
        break;
    case CV_STR:                         /* (text may be in the code: copied before it goes) */
        result->text = owf_malloc((size_t)v.u.str.len + 1);
        if (!result->text) {
            r = OWF_ERR_MEMORY;
            goto out;
        }
        memcpy(result->text, v.u.str.p, v.u.str.len);
        result->text[v.u.str.len] = 0;
        result->type = OWF_VALUE_TEXT;
        break;
    default:
        result->type = OWF_VALUE_NUMBER;
        result->number = 0;
        break;
    }
out:
    calc_prog_free(&prog);
    calc_arena_reset(&calc->arena);
    return r;
}

/* ---- Inserting and deleting rows and columns ---- */

typedef struct {
    int sheet, axis;
    long at, count;
} shift_job;

/* One edge (first..last) of a range along the axis shifted; 0 when all of
 * it went. */
static int shift_span(const shift_job *j, unsigned *first, unsigned *last, int single, long limit)
{
    long a = (long)*first, b = (long)*last, n = -j->count;
    if (j->count > 0) {
        if (a >= j->at)
            a += j->count;
        if (b >= j->at)
            b += j->count;
        if (a >= limit)
            return 0;
        if (b >= limit)
            b = limit - 1;
    } else {
        if (single) {
            if (a >= j->at && a < j->at + n)
                return 0;
            if (a >= j->at + n)
                a -= n;
            b = a;
        } else {
            a = a < j->at ? a : a >= j->at + n ? a - n : j->at;
            b = b < j->at ? b : b >= j->at + n ? b - n : j->at - 1;
            if (b < a)
                return 0;
        }
    }
    *first = (unsigned)a;
    *last = (unsigned)b;
    return 1;
}

static int shift_ref(void *user, int ref_sheet, int kind, unsigned *r0, unsigned *c0, unsigned *r1, unsigned *c1,
                     unsigned abs, int *to)
{
    const shift_job *j = user;
    (void)abs;
    (void)to;
    if (ref_sheet != j->sheet)
        return 1;
    if (j->axis == 0)
        return kind == RK_COLS ? 1 : shift_span(j, r0, r1, kind == RK_CELL, CALC_MAX_ROWS);
    return kind == RK_ROWS ? 1 : shift_span(j, c0, c1, kind == RK_CELL, CALC_MAX_COLS);
}

static int rewrite_text(owf_book *book, int sheet, char **text, shift_job *job)
{
    char *t = calc_rewrite(book, sheet, sheet, *text, shift_ref, job);
    if (!t)
        return 0;
    if (strcmp(t, *text)) {
        free(*text);
        *text = t;
    } else {
        free(t);
    }
    return 1;
}

int owf_calc_shift(owf_calc *calc, int sheet, int axis, unsigned at, int count)
{
    owf_book *book = calc->book;
    owf_sheet *s;
    shift_job job;
    long limit = axis ? (long)CALC_MAX_COLS : (long)CALC_MAX_ROWS;
    int k, i, out;

    if (sheet < 0 || sheet >= book->nsheets || (axis != 0 && axis != 1) || (long)at >= limit)
        return OWF_ERR_UNSUPPORTED;
    if (!count)
        return OWF_OK;
    s = &book->sheets[sheet];
    job.sheet = sheet;
    job.axis = axis;
    job.at = (long)at;
    job.count = count;
    if (count < 0 && (long)at - count > limit)
        job.count = (long)at - limit;
    /* nothing may go off the sheet, as Excel refuses */
    if (count > 0)
        for (i = 0; i < s->ncells; i++) {
            long v = axis ? (long)s->cells[i].col : (long)s->cells[i].row;
            if (v >= (long)at && v + count >= limit && s->cells[i].type != OWF_VALUE_EMPTY)
                return OWF_ERR_UNSUPPORTED;
        }
    /* the formulas and names that point there */
    for (k = 0; k < book->nsheets; k++)
        for (i = 0; i < book->sheets[k].ncells; i++)
            if (book->sheets[k].cells[i].formula &&
                !rewrite_text(book, k, &book->sheets[k].cells[i].formula, &job)) {
                calc->stale = 1;
                return OWF_ERR_MEMORY;
            }
    for (i = 0; i < book->nnames; i++)
        if (book->names[i].formula && !rewrite_text(book, book->names[i].sheet, &book->names[i].formula, &job)) {
            calc->stale = 1;
            return OWF_ERR_MEMORY;
        }
    /* the cells: the order stays, the deleted go */
    for (i = out = 0; i < s->ncells; i++) {
        owf_cell *c = &s->cells[i];
        unsigned *v = axis ? &c->col : &c->row, last = *v;
        if (!shift_span(&job, v, &last, 1, limit)) {
            free(c->text);
            free(c->formula);
            continue;
        }
        s->cells[out++] = *c;
    }
    s->ncells = out;
    if (axis == 0) {
        for (i = out = 0; i < s->nrows; i++) {
            unsigned last = s->rows[i].row;
            if (shift_span(&job, &s->rows[i].row, &last, 1, limit))
                s->rows[out++] = s->rows[i];
        }
        s->nrows = out;
    } else {
        for (i = out = 0; i < s->ncols; i++)
            if (shift_span(&job, &s->cols[i].first, &s->cols[i].last, 0, limit))
                s->cols[out++] = s->cols[i];
        s->ncols = out;
    }
    for (i = out = 0; i < s->nmerges; i++) {
        owf_range *m = &s->merges[i];
        int keep = axis ? shift_span(&job, &m->first_col, &m->last_col, 0, limit)
                        : shift_span(&job, &m->first_row, &m->last_row, 0, limit);
        if (keep && (m->first_row != m->last_row || m->first_col != m->last_col))
            s->merges[out++] = *m;
    }
    s->nmerges = out;
    /* conditional formats: their ranges as merged ones move, their formulas as the cells' */
    for (i = out = 0; i < s->ncondfmts; i++) {
        owf_condfmt *cf = &s->condfmts[i];
        int r, kept = 0;
        for (r = 0; r < cf->nranges; r++) {
            owf_range *m = &cf->ranges[r];
            if (axis ? shift_span(&job, &m->first_col, &m->last_col, 0, limit)
                     : shift_span(&job, &m->first_row, &m->last_row, 0, limit))
                cf->ranges[kept++] = *m;
        }
        cf->nranges = kept;
        for (r = 0; r < 2; r++)
            if (cf->formula[r] && !rewrite_text(book, sheet, &cf->formula[r], &job)) {
                calc->stale = 1;
                return OWF_ERR_MEMORY;
            }
        if (!kept) {
            owf_condfmt_free(cf);
            continue;
        }
        s->condfmts[out++] = *cf;
    }
    s->ncondfmts = out;
    return build(calc, 1);
}

/* ---- Cells the program changed itself ---- */

/* A cell's node again, from its formula now (the user's still the user's). */
static int resync_cell(owf_calc *calc, int sheet, unsigned row, unsigned col)
{
    int old = node_at(calc, sheet, row, col), typed = 0, id;
    const owf_cell *c = owf_sheet_find(&calc->book->sheets[sheet], row, col);
    if (old >= 0) {
        typed = (calc->nodes[old].flags & NODE_TYPED) != 0;
        remove_node(calc, old);
    }
    if (c && c->formula) {
        if (add_node(calc, sheet, c, typed, &id) == OWF_ERR_MEMORY) {
            calc->stale = 1;
            return OWF_ERR_MEMORY;
        }
        calc->nodes[id].flags |= OWF_CALC_DIRTY;
    }
    return OWF_OK;
}

static int overlaps(unsigned a0, unsigned a1, unsigned b0, unsigned b1)
{
    return a0 <= b1 && b0 <= a1;
}

/* Everything that reads any cell of a range, and what reads that... */
static void mark_range(owf_calc *calc, int sheet, const owf_range *r)
{
    unsigned i;
    calc->nwork = 0;
    for (i = 0; i < calc->buckets.cap; i++) {
        const hslot *slot = &calc->buckets.slots[i];
        int e, kind = (int)(slot->a >> 16);
        if (slot->value < 0 || (int)(slot->a & 0xFFFF) != sheet)
            continue;
        if (kind == KEY_CELL &&
            !(slot->b >= r->first_row && slot->b <= r->last_row && slot->c >= r->first_col && slot->c <= r->last_col))
            continue;
        if (kind == KEY_COLUMN && !(slot->b >= r->first_col && slot->b <= r->last_col))
            continue;
        for (e = slot->value; e >= 0; e = calc->entries[e].next) {
            const dep_entry *d = &calc->entries[e];
            if (d->node >= 0 && overlaps(d->r0, d->r1, r->first_row, r->last_row) &&
                overlaps(d->c0, d->c1, r->first_col, r->last_col))
                mark_one(calc, d->node, NULL);
        }
    }
    while (calc->nwork) {
        const calc_node *n = &calc->nodes[calc->work[--calc->nwork]];
        each_dependent(calc, n->sheet, n->row, n->col, mark_one, NULL);
    }
}

static int good_range(const owf_book *book, int sheet, const owf_range *r)
{
    return sheet >= 0 && sheet < book->nsheets && r && r->first_row <= r->last_row && r->first_col <= r->last_col &&
           r->last_row < CALC_MAX_ROWS && r->last_col < CALC_MAX_COLS;
}

int owf_calc_touch(owf_calc *calc, int sheet, const owf_range *range)
{
    const owf_sheet *s;
    int id, i;
    if (!good_range(calc->book, sheet, range))
        return OWF_ERR_UNSUPPORTED;
    calc->used[0] = calc->used[1] = -1;
    if (calc->stale)
        return build(calc, 1);
    /* the range's nodes go, its formulas come back */
    for (id = 0; id < calc->nnodes; id++) {
        const calc_node *n = &calc->nodes[id];
        if (!(n->flags & NODE_FREE) && n->sheet == sheet && n->row >= range->first_row &&
            n->row <= range->last_row && n->col >= range->first_col && n->col <= range->last_col)
            remove_node(calc, id);
    }
    s = &calc->book->sheets[sheet];
    for (i = 0; i < s->ncells; i++) {
        const owf_cell *c = &s->cells[i];
        if (c->formula && c->row >= range->first_row && c->row <= range->last_row && c->col >= range->first_col &&
            c->col <= range->last_col) {
            if (add_node(calc, sheet, c, 0, &id) == OWF_ERR_MEMORY) {
                calc->stale = 1;
                return OWF_ERR_MEMORY;
            }
            calc->nodes[id].flags |= OWF_CALC_DIRTY;
        }
    }
    mark_range(calc, sheet, range);
    return calc->stale ? OWF_ERR_MEMORY : OWF_OK;
}

/* ---- Renaming a sheet ---- */

typedef struct {
    char **where;                /* the text to replace */
    char *text;                  /* its new text */
    int sheet;                   /* a cell's: its node is taken again; -1 for a name */
    unsigned row, col;
} text_change;

typedef struct {
    text_change *items;
    int n, cap;
} changes;

static int add_change(changes *ch, char **where, char *text, int sheet, unsigned row, unsigned col)
{
    if (ch->n == ch->cap) {
        int want = ch->cap ? ch->cap * 2 : 32;
        text_change *p = realloc(ch->items, (size_t)want * sizeof *p);
        if (!p)
            return 0;
        ch->items = p;
        ch->cap = want;
    }
    ch->items[ch->n].where = where;
    ch->items[ch->n].text = text;
    ch->items[ch->n].sheet = sheet;
    ch->items[ch->n].row = row;
    ch->items[ch->n].col = col;
    ch->n++;
    return 1;
}

static void drop_changes(changes *ch)
{
    int i;
    for (i = 0; i < ch->n; i++)
        free(ch->items[i].text);
    free(ch->items);
}

/* Puts the new texts in; 1 when a defined name changed. */
static int apply_changes(changes *ch)
{
    int i, names = 0;
    for (i = 0; i < ch->n; i++) {
        free(*ch->items[i].where);
        *ch->items[i].where = ch->items[i].text;
        ch->items[i].text = NULL;
        names |= ch->items[i].sheet < 0;
    }
    return names;
}

/* Might the text name the sheet? (any case; a ' in the name may be
 * doubled in the text, as a quoted name has it) */
static int contains_nocase(const char *text, const char *word)
{
    for (; *text; text++) {
        const char *t = text, *w = word;
        while (*w && *t) {
            if (*w == '\'' && *t == '\'') {
                t += t[1] == '\'' ? 2 : 1;
                w++;
            } else if (owf_lower((unsigned char)*t) == owf_lower((unsigned char)*w)) {
                t++;
                w++;
            } else {
                break;
            }
        }
        if (!*w)
            return 1;
    }
    return 0;
}

int owf_calc_rename_sheet(owf_calc *calc, int sheet, const char *name)
{
    owf_book *book = calc->book;
    changes ch;
    char *new_name, *old_name;
    size_t n;
    int k, i, names = 0, result = OWF_OK;
    const char *p;

    if (sheet < 0 || sheet >= book->nsheets || !name)
        return OWF_ERR_UNSUPPORTED;
    n = strlen(name);
    if (!n || n > 31 * 4 || calc_utf8_count(name, (unsigned)n) > 31 || name[0] == '\'' || name[n - 1] == '\'')
        return OWF_ERR_FORMAT;
    for (p = name; *p; p++)
        if (strchr(":\\/?*[]", *p))
            return OWF_ERR_FORMAT;
    for (k = 0; k < book->nsheets; k++)
        if (k != sheet && owf_same_word(book->sheets[k].name, name))
            return OWF_ERR_FORMAT;
    old_name = book->sheets[sheet].name;
    if (!strcmp(old_name, name))
        return OWF_OK;
    new_name = malloc(n + 1);
    if (!new_name)
        return OWF_ERR_MEMORY;
    memcpy(new_name, name, n + 1);
    /* every new text first, so that running out of memory changes nothing */
    memset(&ch, 0, sizeof ch);
    for (k = 0; k < book->nsheets; k++)
        for (i = 0; i < book->sheets[k].ncells; i++) {
            owf_cell *c = &book->sheets[k].cells[i];
            char *t;
            if (!c->formula || !contains_nocase(c->formula, old_name))
                continue;
            if (!(t = calc_rename_sheet_in(c->formula, old_name, name)) ||
                (strcmp(t, c->formula) ? !add_change(&ch, &c->formula, t, k, c->row, c->col) : (free(t), 0))) {
                free(t);
                drop_changes(&ch);
                free(new_name);
                return OWF_ERR_MEMORY;
            }
        }
    for (i = 0; i < book->nnames; i++) {
        char *t;
        if (!book->names[i].formula || !contains_nocase(book->names[i].formula, old_name))
            continue;
        if (!(t = calc_rename_sheet_in(book->names[i].formula, old_name, name)) ||
            (strcmp(t, book->names[i].formula) ? !add_change(&ch, &book->names[i].formula, t, -1, 0, 0)
                                                : (free(t), 0))) {
            free(t);
            drop_changes(&ch);
            free(new_name);
            return OWF_ERR_MEMORY;
        }
    }
    names = apply_changes(&ch);
    drop_changes(&ch);
    book->sheets[sheet].name = new_name;
    free(old_name);
    /* references by the old name compile as before; those that named the
       new one (a sheet that was not there) find it now */
    if (names)
        return build(calc, 1);
    for (i = 0; i < book->nnames; i++)
        if (book->names[i].formula && contains_nocase(book->names[i].formula, name))
            return build(calc, 1);
    for (k = 0; k < calc->nnodes && result == OWF_OK; k++) {
        const calc_node *nd = &calc->nodes[k];
        const owf_cell *c;
        if (nd->flags & NODE_FREE)
            continue;
        c = owf_sheet_find(&book->sheets[nd->sheet], nd->row, nd->col);
        if (c && c->formula && contains_nocase(c->formula, name)) {
            int s = nd->sheet;
            unsigned r = nd->row, col = nd->col;
            result = resync_cell(calc, s, r, col);
            if (result == OWF_OK)
                mark_from(calc, s, r, col);
        }
    }
    return result;
}

/* ---- Cut and paste ---- */

typedef struct {
    int src, dst;                /* sheets */
    owf_range from, to;
    long drow, dcol;
    int moving;                  /* the formula being rewritten is one of those that move */
} move_job;

static int inside(const owf_range *r, unsigned r0, unsigned c0, unsigned r1, unsigned c1)
{
    return r0 >= r->first_row && r1 <= r->last_row && c0 >= r->first_col && c1 <= r->last_col;
}

static int move_ref_job(void *user, int ref_sheet, int kind, unsigned *r0, unsigned *c0, unsigned *r1, unsigned *c1,
                        unsigned abs, int *to)
{
    const move_job *j = user;
    (void)abs;
    (void)kind;
    if (ref_sheet == j->src && inside(&j->from, *r0, *c0, *r1, *c1)) {
        /* all of it moves: the reference follows */
        *r0 = (unsigned)((long)*r0 + j->drow);
        *r1 = (unsigned)((long)*r1 + j->drow);
        *c0 = (unsigned)((long)*c0 + j->dcol);
        *c1 = (unsigned)((long)*c1 + j->dcol);
        *to = j->dst;
        return 1;
    }
    if (ref_sheet == j->dst && inside(&j->to, *r0, *c0, *r1, *c1))
        return 0;                        /* replaced: #REF! */
    if (j->moving && ref_sheet >= 0)
        *to = ref_sheet;                 /* still where it was: named if the formula leaves its sheet */
    return 1;
}

static int keep_cell(const owf_range *r, const owf_cell *c)
{
    return !(c->row >= r->first_row && c->row <= r->last_row && c->col >= r->first_col && c->col <= r->last_col);
}

/* Takes a sheet's cells in a range out: into out (room for all of them),
 * or freed when out is NULL. How many there were. */
static int take_cells(owf_sheet *s, const owf_range *r, owf_cell *out)
{
    int i, k = 0, n = 0;
    for (i = 0; i < s->ncells; i++) {
        owf_cell *c = &s->cells[i];
        if (keep_cell(r, c)) {
            s->cells[k++] = *c;
        } else if (out) {
            out[n++] = *c;
        } else {
            free(c->text);
            free(c->formula);
            n++;
        }
    }
    s->ncells = k;
    return n;
}

int owf_calc_move_range(owf_calc *calc, int sheet, const owf_range *range, int dsheet, int drow, int dcol)
{
    owf_book *book = calc->book;
    move_job job;
    changes ch;
    owf_cell *moved = NULL;
    int nmoved = 0, k, i, names, result = OWF_OK;

    if (!good_range(book, sheet, range) || dsheet < 0 || dsheet >= book->nsheets)
        return OWF_ERR_UNSUPPORTED;
    job.src = sheet;
    job.dst = dsheet;
    job.from = *range;
    job.drow = drow;
    job.dcol = dcol;
    job.moving = 0;
    if ((long)range->first_row + drow < 0 || (long)range->last_row + drow >= (long)CALC_MAX_ROWS ||
        (long)range->first_col + dcol < 0 || (long)range->last_col + dcol >= (long)CALC_MAX_COLS)
        return OWF_ERR_UNSUPPORTED;
    job.to.first_row = (unsigned)((long)range->first_row + drow);
    job.to.last_row = (unsigned)((long)range->last_row + drow);
    job.to.first_col = (unsigned)((long)range->first_col + dcol);
    job.to.last_col = (unsigned)((long)range->last_col + dcol);
    if (sheet == dsheet && !drow && !dcol)
        return OWF_OK;
    /* the references in the book: every new text first */
    memset(&ch, 0, sizeof ch);
    for (k = 0; k < book->nsheets; k++)
        for (i = 0; i < book->sheets[k].ncells; i++) {
            owf_cell *c = &book->sheets[k].cells[i];
            int moving = k == sheet && !keep_cell(range, c);
            char *t;
            if (!c->formula || (k == dsheet && !moving && !keep_cell(&job.to, c)))
                continue;                /* (a cell about to be replaced) */
            job.moving = moving;
            t = calc_rewrite(book, k, moving ? dsheet : k, c->formula, move_ref_job, &job);
            if (!t || (strcmp(t, c->formula) ? !add_change(&ch, &c->formula, t, moving ? -2 : k, c->row, c->col)
                                             : (free(t), 0))) {
                free(t);
                drop_changes(&ch);
                return OWF_ERR_MEMORY;
            }
        }
    job.moving = 0;
    for (i = 0; i < book->nnames; i++) {
        char *t;
        if (!book->names[i].formula)
            continue;
        t = calc_rewrite(book, book->names[i].sheet, book->names[i].sheet, book->names[i].formula, move_ref_job, &job);
        if (!t || (strcmp(t, book->names[i].formula) ? !add_change(&ch, &book->names[i].formula, t, -1, 0, 0)
                                                     : (free(t), 0))) {
            free(t);
            drop_changes(&ch);
            return OWF_ERR_MEMORY;
        }
    }
    /* room for the cells that move, before anything changes */
    for (i = 0; i < book->sheets[sheet].ncells; i++)
        nmoved += !keep_cell(range, &book->sheets[sheet].cells[i]);
    moved = malloc(((size_t)nmoved + 1) * sizeof *moved);
    if (!moved) {
        drop_changes(&ch);
        return OWF_ERR_MEMORY;
    }
    /* the new texts, while their places in the sheets are as they were */
    names = apply_changes(&ch);
    nmoved = take_cells(&book->sheets[sheet], range, moved);
    /* the cells replaced go, the moved come */
    take_cells(&book->sheets[dsheet], &job.to, NULL);
    for (i = 0; i < nmoved; i++) {
        owf_cell *c = owf_sheet_cell(&book->sheets[dsheet], (unsigned)((long)moved[i].row + drow),
                                     (unsigned)((long)moved[i].col + dcol), 1);
        if (!c) {
            for (; i < nmoved; i++) {
                free(moved[i].text);
                free(moved[i].formula);
            }
            free(moved);
            drop_changes(&ch);
            calc->stale = 1;
            return OWF_ERR_MEMORY;
        }
        c->type = moved[i].type;
        c->number = moved[i].number;
        c->error = moved[i].error;
        c->text = moved[i].text;
        c->formula = moved[i].formula;
        c->style = moved[i].style;
        c->flags = moved[i].flags;
    }
    free(moved);
    if (names || calc->stale) {
        drop_changes(&ch);
        return build(calc, 1);
    }
    /* formulas elsewhere whose references changed: their nodes again */
    for (i = 0; i < ch.n && result == OWF_OK; i++)
        if (ch.items[i].sheet >= 0) {
            result = resync_cell(calc, ch.items[i].sheet, ch.items[i].row, ch.items[i].col);
            if (result == OWF_OK)        /* and what reads them */
                mark_from(calc, ch.items[i].sheet, ch.items[i].row, ch.items[i].col);
        }
    drop_changes(&ch);
    if (result == OWF_OK)
        result = owf_calc_touch(calc, sheet, range);
    if (result == OWF_OK)
        result = owf_calc_touch(calc, dsheet, &job.to);
    return result;
}
