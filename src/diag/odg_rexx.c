/*
 * odg_rexx.c: GFOpenDiag's ARexx commands for its GFOPENDIAG port (opendiag_rexx.h). Each command has a
 * ReadArgs-style template, matched here by hand so that it runs (and is tested) on the PC as well as the Amiga:
 * quoted words, KEYWORD=value or KEYWORD value, /A needed, /N a number (page pixels, decimals allowed), /S a
 * switch, /K only by its keyword, /F the rest of the line. The template matching is GFOpenProject's
 * (src/project/opj_rexx.c). Edits go through the editing core, one Undo step each. Return codes and LASTERROR as
 * every Open app's port (amicradle-teams ARexx-RULE.md 3 and 4).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "opendiag_rexx.h"
#include "opendiag_mind.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_ARGS 8
#define ARG_TEXT 2048
#define NUM_LIMIT 200000000L             /* two million pixels, as owf_dnum_parse keeps them */
#define MAX_IDS 64

typedef struct {
    const char *name;
    const char *tmpl;
} command;

enum {
    C_HELP, C_LASTERROR, C_ISDIRTY, C_GETFILE, C_NEW, C_OPEN, C_SAVE, C_SAVEAS, C_EXPORT, C_PAGES, C_PAGE,
    C_ADDPAGE, C_CELLS, C_ADDSHAPE, C_CONNECT, C_MOVE, C_RESIZE, C_DELETE, C_SETVALUE, C_GETVALUE, C_STYLE,
    C_GETSTYLE, C_GETBOX, C_HIT, C_GRID, C_UNDO, C_REDO, C_LAYERS, C_LAYER, C_ADDLAYER, C_GETLAYER, C_SHOWLAYER,
    C_LOCKLAYER, C_RENAMELAYER, C_MAPNEW, C_MAPCHILD, C_MAPSIBLING, C_MAPDELETE, C_MAPMOVE, C_MAPFOLD, C_MAPLAYOUT,
    C_MAPROOT, C_MAPPARENT, C_MAPCHILDREN, C_MAPSTEP, C_MAPEXPORT, C_MAPIMPORT, NCOMMANDS
};

static const command commands[NCOMMANDS] = {
    { "HELP", "COMMAND" },
    { "LASTERROR", "" },
    { "ISDIRTY", "" },
    { "GETFILE", "" },
    { "NEW", "FORCE/S" },
    { "OPEN", "FILE/A,FORCE/S" },
    { "SAVE", "COMPRESSED/S" },
    { "SAVEAS", "FILE/A,COMPRESSED/S" },
    { "EXPORT", "FILE/A,FORMAT/K" },
    { "PAGES", "" },
    { "PAGE", "NUMBER/A/N" },
    { "ADDPAGE", "NAME" },
    { "CELLS", "" },
    { "ADDSHAPE", "X/A/N,Y/A/N,WIDTH/A/N,HEIGHT/A/N,STYLE/K,TEXT/K" },
    { "CONNECT", "FROM/A,TO/A,STYLE/K" },
    { "MOVE", "ID/A,DX/A/N,DY/A/N" },
    { "RESIZE", "ID/A,X/A/N,Y/A/N,WIDTH/A/N,HEIGHT/A/N" },
    { "DELETE", "ID/A" },
    { "SETVALUE", "ID/A,TEXT/F" },
    { "GETVALUE", "ID/A" },
    { "STYLE", "ID/A,KEY/A,VALUE" },
    { "GETSTYLE", "ID/A,KEY" },
    { "GETBOX", "ID/A" },
    { "HIT", "X/A/N,Y/A/N,TOLERANCE/K/N" },
    { "GRID", "SIZE/N" },
    { "UNDO", "" },
    { "REDO", "" },
    { "LAYERS", "" },
    { "LAYER", "NUMBER/N" },
    { "ADDLAYER", "NAME" },
    { "GETLAYER", "NUMBER/A/N" },
    { "SHOWLAYER", "NUMBER/A/N,OFF/S" },
    { "LOCKLAYER", "NUMBER/A/N,OFF/S" },
    { "RENAMELAYER", "NUMBER/A/N,NAME/A" },
    { "MAPNEW", "X/A/N,Y/A/N,TEXT/F" },
    { "MAPCHILD", "NODE/A,TEXT/F" },
    { "MAPSIBLING", "NODE/A,ABOVE/S,TEXT/F" },
    { "MAPDELETE", "NODE/A" },
    { "MAPMOVE", "NODE/A,UP/S" },
    { "MAPFOLD", "NODE/A,OFF/S" },
    { "MAPLAYOUT", "NODE/A,BOTH/S,RIGHT/S" },
    { "MAPROOT", "NODE/A" },
    { "MAPPARENT", "NODE/A" },
    { "MAPCHILDREN", "NODE/A" },
    { "MAPSTEP", "NODE/A,WAY/A" },
    { "MAPEXPORT", "NODE/A,FILE/A,FORMAT/K" },
    { "MAPIMPORT", "FILE/A,X/N,Y/N,FORMAT/K" },
};

/* ---- Words ---- */

static int upper(int c)
{
    return c >= 'a' && c <= 'z' ? c - 32 : c;
}

static const char *skip_space(const char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

/* s starts with the word w (case ignored) followed by the end, a space or (eq) '='. */
static int word_at(const char *s, const char *w, size_t n, int eq)
{
    size_t i;
    for (i = 0; i < n; i++)
        if (!s[i] || upper((unsigned char)s[i]) != upper((unsigned char)w[i])) return 0;
    return !s[n] || s[n] == ' ' || s[n] == '\t' || (eq && s[n] == '=');
}

/* ---- Templates ---- */

typedef struct {
    char name[16];
    int need, number, sw, key, rest;
} field;

typedef struct {
    int n;
    field f[MAX_ARGS];
    const char *value[MAX_ARGS];         /* NULL when not given; a switch: "" when given */
    long num[MAX_ARGS];
    char text[ARG_TEXT];
    size_t used;
} args;

static void parse_template(const char *t, args *a)
{
    memset(a, 0, sizeof *a);
    while (*t && a->n < MAX_ARGS) {
        field *f = &a->f[a->n++];
        size_t k = 0;
        while (*t && *t != '/' && *t != ',') {
            if (k < sizeof f->name - 1) f->name[k++] = *t;
            t++;
        }
        while (*t == '/') {
            switch (t[1]) {
            case 'A': f->need = 1; break;
            case 'N': f->number = 1; break;
            case 'S': f->sw = 1; break;
            case 'K': f->key = 1; break;
            case 'F': f->rest = 1; break;
            }
            t += 2;
        }
        if (*t == ',') t++;
    }
}

/* The next word (a quoted one with its quotes off, "" for "" and *"" for a quote inside) into a's text; NULL
 * when it doesn't fit, so a long word is refused, never cut. */
static const char *take_word(const char **s, args *a)
{
    const char *p = *s;
    char *out = a->text + a->used;
    size_t room = sizeof a->text - a->used, k = 0;
    int cut = 0;
    if (room < 2) return NULL;
    if (*p == '"') {
        p++;
        while (*p && *p != '"') {
            char c = *p++;
            if (c == '*' && *p) {
                c = *p++;
                if (c == 'N' || c == 'n') c = '\n';
            }
            if (k < room - 1) out[k++] = c;
            else cut = 1;
        }
        if (*p == '"') p++;
    } else
        while (*p && *p != ' ' && *p != '\t' && *p != '=') {
            if (k < room - 1) out[k++] = *p;
            else cut = 1;
            p++;
        }
    if (cut) return NULL;
    out[k] = 0;
    a->used += k + 1;
    *s = p;
    return out;
}

/* The rest of the line as the value of a /F field: as it is, or one quoted word unquoted. */
static const char *take_rest(const char **s, args *a)
{
    const char *p = *s, *end = p + strlen(p);
    char *out = a->text + a->used;
    size_t n, room = sizeof a->text - a->used;
    if (room < 2)                        /* the words before filled the room: too long (parse_args says so) */
        return NULL;
    while (end > p && (end[-1] == ' ' || end[-1] == '\t')) end--;
    if (*p == '"') {
        const char *q = p;
        const char *w = take_word(&q, a);
        if (!w)
            return NULL;
        if (*skip_space(q) == 0) {
            *s = q;
            return w;
        }
        a->used -= strlen(w) + 1;        /* more after the quotes: the whole line, quotes and all */
    }
    n = (size_t)(end - p);
    if (n > room - 1)                    /* longer than the room: refused, not cut */
        return NULL;
    memcpy(out, p, n);
    out[n] = 0;
    a->used += n + 1;
    *s = p + strlen(p);
    return out;
}

/* 1, or 0 with the reason in err. */
static int parse_args(const char *line, args *a, char *err, size_t esize)
{
    const char *s = skip_space(line);
    int i;
    while (*s) {
        int which = -1;
        size_t len;
        for (i = 0; i < a->n; i++) {
            len = strlen(a->f[i].name);
            if (word_at(s, a->f[i].name, len, 1)) {
                which = i;
                s = skip_space(s + len);
                break;
            }
        }
        if (which >= 0 && a->f[which].sw) {
            a->value[which] = "";
            continue;
        }
        if (which >= 0) {
            if (*s == '=') s = skip_space(s + 1);
            if (!*s) {
                snprintf(err, esize, "%.15s needs a value", a->f[which].name);
                return 0;
            }
        } else {
            for (i = 0; i < a->n; i++)
                if (!a->value[i] && !a->f[i].sw && !a->f[i].key) break;
            if (i == a->n) {
                snprintf(err, esize, "Too many arguments at \"%.100s\"", s);
                return 0;
            }
            which = i;
        }
        a->value[which] = a->f[which].rest ? take_rest(&s, a) : take_word(&s, a);
        if (!a->value[which]) {
            snprintf(err, esize, "The arguments are too long");
            return 0;
        }
        s = skip_space(s);
    }
    for (i = 0; i < a->n; i++) {
        const char *v = a->value[i];
        if (a->f[i].need && !v) {
            snprintf(err, esize, "%.15s is needed", a->f[i].name);
            return 0;
        }
        if (v && a->f[i].number) {
            /* a page place: whole or with decimals, kept as hundredths */
            const char *p = v;
            int digits = 0;
            if (*p == '-' || *p == '+') p++;
            while (*p >= '0' && *p <= '9') p++, digits++;
            if (*p == '.') {
                p++;
                while (*p >= '0' && *p <= '9') p++, digits++;
            }
            if (!digits || *p) {
                snprintf(err, esize, "%.15s must be a number, not \"%.100s\"", a->f[i].name, v);
                return 0;
            }
            a->num[i] = owf_dnum_parse(v);
            if (a->num[i] > NUM_LIMIT) a->num[i] = NUM_LIMIT;
            if (a->num[i] < -NUM_LIMIT) a->num[i] = -NUM_LIMIT;
        }
    }
    return 1;
}

/* ---- Files ---- */

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

static int ext_is(const char *file, const char *ext)
{
    const char *dot = file ? strrchr(file, '.') : NULL;
    if (!dot || strlen(dot + 1) != strlen(ext)) return 0;
    for (dot++; *dot; dot++, ext++)
        if (lower((unsigned char)*dot) != *ext) return 0;
    return 1;
}

/* MM, OPML or MD (MARKDOWN) into *kind; 0 for another word. */
static int outline_kind(const char *w, owf_outline_kind *kind)
{
    if (word_at(w, "MM", 2, 0)) *kind = OWF_OUTLINE_MM;
    else if (word_at(w, "OPML", 4, 0)) *kind = OWF_OUTLINE_OPML;
    else if (word_at(w, "MD", 2, 0) || word_at(w, "MARKDOWN", 8, 0)) *kind = OWF_OUTLINE_MD;
    else return 0;
    return 1;
}

int odg_rexx_format(const char *file)
{
    if (ext_is(file, "svg")) return ODG_FILE_SVG;
    if (ext_is(file, "png")) return ODG_FILE_PNG;
    return ODG_FILE_DRAWIO;
}

static int read_file(const char *name, unsigned char **data, size_t *length)
{
    FILE *f = fopen(name, "rb");
    unsigned char *buf = NULL;
    size_t n = 0, cap = 0;
    if (!f) return 0;
    for (;;) {
        size_t got;
        if (n == cap) {
            unsigned char *grown;
            cap = cap ? cap * 2 : 65536;
            if (cap > 64UL * 1024 * 1024 || !(grown = owf_realloc(buf, cap))) {
                owf_free(buf);
                fclose(f);
                return -1;
            }
            buf = grown;
        }
        got = fread(buf + n, 1, cap - n, f);
        n += got;
        if (got == 0) break;
    }
    fclose(f);
    *data = buf;
    *length = n;
    return 1;
}

/* The diagram (or, for an SVG, its page) written to name. */
static int write_file(odg_rexx *rx, const char *name, int format, int compressed)
{
    unsigned char *data = NULL;
    size_t length = 0;
    FILE *f;
    int rc, ok;
    if (format == ODG_FILE_PNG) {
        snprintf(rx->lasterror, sizeof rx->lasterror, "%.150s: a PNG needs the drawn picture, which the app's Export makes", name);
        return ODG_RX_ERROR;
    }
    rc = format == ODG_FILE_SVG ? owf_dsvg_write(&rx->d, rx->doc.page, &data, &length)
                                : owf_drawio_write(&rx->d, compressed, &data, &length);
    if (rc == OWF_ERR_MEMORY) {
        snprintf(rx->lasterror, sizeof rx->lasterror, "Out of memory writing %.200s", name);
        return ODG_RX_FAIL;
    }
    if (rc != OWF_OK) {
        snprintf(rx->lasterror, sizeof rx->lasterror, "Could not make %.200s", name);
        return ODG_RX_ERROR;
    }
    f = fopen(name, "wb");
    ok = f && fwrite(data, 1, length, f) == length;
    if (f && fclose(f) != 0) ok = 0;
    owf_free(data);
    if (!ok) {
        snprintf(rx->lasterror, sizeof rx->lasterror, "Could not write %.200s", name);
        return ODG_RX_ERROR;
    }
    return ODG_RX_OK;
}

static int name_fits(odg_rexx *rx, const char *what, const char *name)
{
    if (strlen(name) < sizeof rx->file) return 1;
    snprintf(rx->lasterror, sizeof rx->lasterror, "%s: the file name is longer than %d bytes", what,
             (int)sizeof rx->file - 1);
    return 0;
}

static void set_file(odg_rexx *rx, const char *name)
{
    size_t n = strlen(name);
    if (n > sizeof rx->file - 1) n = sizeof rx->file - 1;
    memcpy(rx->file, name, n);
    rx->file[n] = 0;
}

/* ---- The diagram ---- */

int odg_rexx_init(odg_rexx *rx)
{
    memset(rx, 0, sizeof *rx);
    owf_diagram_init(&rx->d);
    if (!owf_diagram_add_page(&rx->d, "page-1", "Page-1")) {
        owf_diagram_free(&rx->d);
        return OWF_ERR_MEMORY;
    }
    odg_init(&rx->doc, &rx->d, 0);
    return OWF_OK;
}

void odg_rexx_free(odg_rexx *rx)
{
    odg_free(&rx->doc);
    owf_diagram_free(&rx->d);
}

/* rx takes d (a fresh diagram with a page) in place of its own */
static void replace(odg_rexx *rx, owf_diagram *d)
{
    odg_free(&rx->doc);
    owf_diagram_free(&rx->d);
    rx->d = *d;
    odg_init(&rx->doc, &rx->d, 0);
}

/* ---- Commands ---- */

static int edit_rc(odg_rexx *rx, int rc, const char *what, const char *why)
{
    if (rc == OWF_OK) {
        rx->changes++;
        return ODG_RX_OK;
    }
    if (rc == OWF_ERR_MEMORY) {
        snprintf(rx->lasterror, sizeof rx->lasterror, "%s: out of memory", what);
        return ODG_RX_FAIL;
    }
    snprintf(rx->lasterror, sizeof rx->lasterror, "%s: %s", what, why);
    return ODG_RX_ERROR;
}

/* A layer's number (1 the bottom) as given, into *i from 0; 0 with LASTERROR when there is no such layer */
static int layer_arg(odg_rexx *rx, const char *what, long num, int *i)
{
    int n = odg_layer_count(&rx->doc);
    if (num % 100 || num < 100 || num / 100 > n) {
        snprintf(rx->lasterror, sizeof rx->lasterror, "%s: there are layers 1 to %d", what, n);
        return 0;
    }
    *i = (int)(num / 100 - 1);
    return 1;
}

static void put(char *result, size_t size, const char *s)
{
    size_t n = strlen(s ? s : "");
    if (n > size - 1) n = size - 1;
    memcpy(result, s ? s : "", n);
    result[n] = 0;
}

/* Words joined by spaces into result, as many as fit. */
static void put_word(char *result, size_t size, size_t *at, const char *w)
{
    size_t n = strlen(w);
    if (*at + n + 2 > size) return;
    if (*at) result[(*at)++] = ' ';
    memcpy(result + *at, w, n + 1);
    *at += n;
}

/* ID given as "a,b,c" into ids (pointing into buf); how many, or -1 (and LASTERROR set) past MAX_IDS. */
static int split_ids(odg_rexx *rx, const char *what, const char *s, char *buf, size_t size, const char **ids)
{
    int n = 0;
    char *p;
    put(buf, size, s);
    for (p = buf; *p;) {
        char *comma = strchr(p, ',');
        if (comma) *comma = 0;
        if (*p) {
            if (n == MAX_IDS) {
                snprintf(rx->lasterror, sizeof rx->lasterror, "%s: more than %d ids", what, MAX_IDS);
                return -1;
            }
            ids[n++] = p;
        }
        if (!comma) break;
        p = comma + 1;
    }
    return n;
}

/* 1 when new cells can go on the current layer; else 0 with LASTERROR saying why. */
static int layer_open(odg_rexx *rx, const char *what)
{
    int i = odg_layer_current(&rx->doc);
    if (i >= 0 && !odg_layer_shown(&rx->doc, i))
        snprintf(rx->lasterror, sizeof rx->lasterror, "%s: layer %d is hidden (SHOWLAYER %d shows it)", what, i + 1, i + 1);
    else if (i >= 0 && odg_layer_locked(&rx->doc, i))
        snprintf(rx->lasterror, sizeof rx->lasterror, "%s: layer %d is locked (LOCKLAYER %d OFF unlocks it)", what, i + 1,
                 i + 1);
    else
        return 1;
    return 0;
}

static const owf_dcell *find_cell(odg_rexx *rx, const char *what, const char *id)
{
    const owf_dcell *c = owf_dpage_find(odg_page(&rx->doc), id);
    if (!c) snprintf(rx->lasterror, sizeof rx->lasterror, "%s: no cell %.100s on the page", what, id);
    return c;
}

static int run(odg_rexx *rx, int c, args *a, char *result, size_t size)
{
    odg_doc *doc = &rx->doc;
    static char idbuf[ARG_TEXT];         /* off the Amiga's small stack, as args is */
    char text[64];
    const char *ids[MAX_IDS];
    int rc, n;
    switch (c) {
    case C_HELP:
        if (a->value[0]) {
            int i;
            for (i = 0; i < NCOMMANDS; i++)
                if (word_at(a->value[0], commands[i].name, strlen(commands[i].name), 0)) {
                    snprintf(result, size, "%s %s", commands[i].name, commands[i].tmpl);
                    return ODG_RX_OK;
                }
            snprintf(rx->lasterror, sizeof rx->lasterror, "HELP: no command %.100s", a->value[0]);
            return ODG_RX_ERROR;
        } else {
            static const char *const app[] = { "VERSION", "ACTIVATE", "DEACTIVATE", "FAULT", "PRINT", "QUIT", "ZOOM",
                                               "FIT", "TOOL", "SELECT", "GETSELECTION", "SHAPE" };
            size_t at = 0;
            int i;
            result[0] = 0;
            for (i = 0; i < (int)(sizeof app / sizeof app[0]); i++) put_word(result, size, &at, app[i]);
            for (i = 0; i < NCOMMANDS; i++) put_word(result, size, &at, commands[i].name);
        }
        return ODG_RX_OK;
    case C_LASTERROR: put(result, size, rx->lasterror); return ODG_RX_OK;
    case C_ISDIRTY: put(result, size, doc->changed ? "1" : "0"); return ODG_RX_OK;
    case C_GETFILE: put(result, size, rx->file); return ODG_RX_OK;
    case C_NEW:
    case C_OPEN: {
        owf_diagram d;
        int force = c == C_NEW ? a->value[0] != NULL : a->value[1] != NULL;
        if (c == C_OPEN && !name_fits(rx, "OPEN", a->value[0])) return ODG_RX_ERROR;
        if (doc->changed && !force) {
            snprintf(rx->lasterror, sizeof rx->lasterror, "%s: the diagram has unsaved changes (FORCE drops them)",
                     commands[c].name);
            return ODG_RX_NOW;
        }
        owf_diagram_init(&d);
        if (c == C_NEW)
            rc = owf_diagram_add_page(&d, "page-1", "Page-1") ? OWF_OK : OWF_ERR_MEMORY;
        else {
            unsigned char *data;
            size_t length;
            int got = read_file(a->value[0], &data, &length), format = odg_rexx_format(a->value[0]);
            if (got <= 0) {
                snprintf(rx->lasterror, sizeof rx->lasterror, got < 0 ? "OPEN: out of memory reading %.200s"
                                                                        : "OPEN: could not read %.200s", a->value[0]);
                return got < 0 ? ODG_RX_FAIL : ODG_RX_ERROR;
            }
            rc = format == ODG_FILE_SVG ? owf_dsvg_read(data, length, &d)
                 : format == ODG_FILE_PNG ? owf_dpng_read(data, length, &d) : owf_drawio_read(data, length, &d);
            owf_free(data);
            if (rc == OWF_OK && d.npages < 1)
                rc = owf_diagram_add_page(&d, "page-1", "Page-1") ? OWF_OK : OWF_ERR_MEMORY;
        }
        if (rc != OWF_OK) {
            owf_diagram_free(&d);
            if (rc == OWF_ERR_MEMORY) {
                snprintf(rx->lasterror, sizeof rx->lasterror, "%s: out of memory", commands[c].name);
                return ODG_RX_FAIL;
            }
            snprintf(rx->lasterror, sizeof rx->lasterror,
                     "OPEN: %.150s is not a diagram GFOpenDiag reads (.drawio, or an SVG or PNG with one inside)",
                     a->value[0]);
            return ODG_RX_ERROR;
        }
        replace(rx, &d);
        if (c == C_OPEN) set_file(rx, a->value[0]);
        else rx->file[0] = 0;
        rx->changes++;
        return ODG_RX_OK;
    }
    case C_SAVE:
        if (!rx->file[0]) {
            snprintf(rx->lasterror, sizeof rx->lasterror, "SAVE: the diagram has no file yet (SAVEAS names one)");
            return ODG_RX_NOW;
        }
        if ((rc = write_file(rx, rx->file, odg_rexx_format(rx->file), a->value[0] != NULL)) == ODG_RX_OK)
            odg_saved(doc);
        return rc;
    case C_SAVEAS:
        if (!name_fits(rx, "SAVEAS", a->value[0])) return ODG_RX_ERROR;
        if ((rc = write_file(rx, a->value[0], odg_rexx_format(a->value[0]), a->value[1] != NULL)) == ODG_RX_OK) {
            set_file(rx, a->value[0]);
            odg_saved(doc);
        }
        return rc;
    case C_EXPORT: {
        int format = odg_rexx_format(a->value[0]);
        if (a->value[1]) {
            if (word_at(a->value[1], "SVG", 3, 0)) format = ODG_FILE_SVG;
            else if (word_at(a->value[1], "DRAWIO", 6, 0)) format = ODG_FILE_DRAWIO;
            else if (word_at(a->value[1], "PNG", 3, 0)) format = ODG_FILE_PNG;
            else {
                snprintf(rx->lasterror, sizeof rx->lasterror, "EXPORT: FORMAT is SVG or DRAWIO, not %.100s", a->value[1]);
                return ODG_RX_ERROR;
            }
        }
        return write_file(rx, a->value[0], format, 0);
    }
    case C_PAGES: snprintf(result, size, "%d", rx->d.npages); return ODG_RX_OK;
    case C_PAGE: {
        long page = a->num[0];
        if (page % 100 || page < 100 || page / 100 > rx->d.npages) {
            snprintf(rx->lasterror, sizeof rx->lasterror, "PAGE: there are pages 1 to %d", rx->d.npages);
            return ODG_RX_ERROR;
        }
        if (page / 100 - 1 != doc->page) {
            int changed = doc->changed;
            odg_free(doc);                    /* each page has its own Undo */
            odg_init(doc, &rx->d, (int)(page / 100 - 1));
            doc->changed = changed;
            rx->changes++;
        }
        return ODG_RX_OK;
    }
    case C_ADDPAGE: {
        char name[32], id[32];
        int changed = doc->changed, was = doc->page, k = rx->d.npages + 1, i;
        /* an id no page has */
        for (;; k++) {
            snprintf(id, sizeof id, "page-%d", k);
            for (i = 0; i < rx->d.npages; i++)
                if (rx->d.pages[i].id && !strcmp(rx->d.pages[i].id, id)) break;
            if (i == rx->d.npages) break;
        }
        snprintf(name, sizeof name, "Page-%d", rx->d.npages + 1);
        /* the new page's cells live in d->pages, which may move: the doc is set up again on it */
        odg_free(doc);
        if (!owf_diagram_add_page(&rx->d, id, a->value[0] ? a->value[0] : name)) {
            odg_init(doc, &rx->d, was < rx->d.npages ? was : 0);
            doc->changed = changed;
            snprintf(rx->lasterror, sizeof rx->lasterror, "ADDPAGE: out of memory");
            return ODG_RX_FAIL;
        }
        odg_init(doc, &rx->d, rx->d.npages - 1);
        doc->changed = 1 | changed;
        rx->changes++;
        snprintf(result, size, "%d", rx->d.npages);
        return ODG_RX_OK;
    }
    case C_CELLS: {
        const owf_dpage *p = odg_page(doc);
        size_t at = 0;
        int i;
        result[0] = 0;
        for (i = 0; i < p->ncells; i++)
            if (p->cells[i].kind != OWF_DCELL_PLAIN) put_word(result, size, &at, p->cells[i].id);
        return ODG_RX_OK;
    }
    case C_ADDSHAPE:
        if (!layer_open(rx, "ADDSHAPE")) return ODG_RX_ERROR;
        rc = odg_add_shape(doc, a->num[0], a->num[1], a->num[2], a->num[3], a->value[4] ? a->value[4] : "",
                           a->value[5], text);
        if ((rc = edit_rc(rx, rc, "ADDSHAPE", "not possible")) == ODG_RX_OK) put(result, size, text);
        return rc;
    case C_CONNECT:
        if (!layer_open(rx, "CONNECT")) return ODG_RX_ERROR;
        rc = odg_add_edge(doc, a->value[0], a->value[1], NULL, NULL, a->value[2] ? a->value[2] : "", text);
        if ((rc = edit_rc(rx, rc, "CONNECT", "FROM and TO must be shapes on the page")) == ODG_RX_OK)
            put(result, size, text);
        return rc;
    case C_MOVE:
        if ((n = split_ids(rx, "MOVE", a->value[0], idbuf, sizeof idbuf, ids)) < 0) return ODG_RX_ERROR;
        return edit_rc(rx, odg_move(doc, ids, n, a->num[1], a->num[2]), "MOVE", "no such shape or connector");
    case C_RESIZE:
        return edit_rc(rx, odg_resize(doc, a->value[0], a->num[1], a->num[2], a->num[3], a->num[4]), "RESIZE",
                       "not a shape, or the size is not above 0");
    case C_DELETE:
        if ((n = split_ids(rx, "DELETE", a->value[0], idbuf, sizeof idbuf, ids)) < 0) return ODG_RX_ERROR;
        return edit_rc(rx, odg_delete(doc, ids, n), "DELETE", "no such cell (the page and its layers stay)");
    case C_SETVALUE:
        return edit_rc(rx, odg_set_value(doc, a->value[0], a->value[1]), "SETVALUE", "no such cell");
    case C_GETVALUE: {
        const owf_dcell *cell = find_cell(rx, "GETVALUE", a->value[0]);
        if (!cell) return ODG_RX_ERROR;
        put(result, size, cell->value);
        return ODG_RX_OK;
    }
    case C_STYLE:
        if ((n = split_ids(rx, "STYLE", a->value[0], idbuf, sizeof idbuf, ids)) < 0) return ODG_RX_ERROR;
        return edit_rc(rx, odg_set_style(doc, ids, n, a->value[1], a->value[2]), "STYLE", "no such cell");
    case C_GETSTYLE: {
        const owf_dcell *cell = find_cell(rx, "GETSTYLE", a->value[0]);
        if (!cell) return ODG_RX_ERROR;
        if (!a->value[1]) put(result, size, cell->style);
        else if (!owf_dstyle_get(cell->style, a->value[1], result, size)) result[0] = 0;
        return ODG_RX_OK;
    }
    case C_GETBOX: {
        const owf_dcell *cell = find_cell(rx, "GETBOX", a->value[0]);
        owf_dnum box[4];
        char nums[4][24];
        int i;
        if (!cell) return ODG_RX_ERROR;
        if (!owf_dcell_box(odg_page(doc), cell, box)) {
            snprintf(rx->lasterror, sizeof rx->lasterror, "GETBOX: %.100s has no box (a connector or a layer)", a->value[0]);
            return ODG_RX_ERROR;
        }
        for (i = 0; i < 4; i++) owf_dnum_format(box[i], nums[i], sizeof nums[i]);
        snprintf(result, size, "%s %s %s %s", nums[0], nums[1], nums[2], nums[3]);
        return ODG_RX_OK;
    }
    case C_HIT:
        if (!odg_hit(doc, a->num[0], a->num[1], a->value[2] ? a->num[2] : OWF_DNUM(3), result, size)) result[0] = 0;
        return ODG_RX_OK;
    case C_GRID:
        if (a->value[0]) {
            if (a->num[0] < 0) {
                snprintf(rx->lasterror, sizeof rx->lasterror, "GRID: SIZE is 0 (no snapping) or more");
                return ODG_RX_ERROR;
            }
            doc->grid = a->num[0];
            rx->changes++;
        }
        owf_dnum_format(doc->grid, text, sizeof text);
        put(result, size, text);
        return ODG_RX_OK;
    case C_LAYERS: snprintf(result, size, "%d", odg_layer_count(doc)); return ODG_RX_OK;
    case C_MAPNEW:
        if (!layer_open(rx, "MAPNEW")) return ODG_RX_ERROR;
        rc = edit_rc(rx, odg_mind_new(doc, a->num[0], a->num[1], a->value[2] ? a->value[2] : "", text), "MAPNEW",
                     "not possible");
        if (rc == ODG_RX_OK) put(result, size, text);
        return rc;
    case C_MAPCHILD:
    case C_MAPSIBLING:
    case C_MAPDELETE:
    case C_MAPMOVE:
    case C_MAPFOLD:
    case C_MAPLAYOUT: {
        const char *node = a->value[0], *why;
        int both = odg_mind_both(doc, node);
        odg_mind_key key = ODG_MKEY_LAYOUT;
        text[0] = 0;
        if (c == C_MAPCHILD) {
            key = ODG_MKEY_CHILD;
            rc = odg_mind_add_child(doc, node, a->value[1] ? a->value[1] : "", both, text);
        } else if (c == C_MAPSIBLING) {
            key = ODG_MKEY_SIBLING;
            rc = odg_mind_add_sibling(doc, node, a->value[1] != NULL, a->value[2] ? a->value[2] : "", both, text);
        } else if (c == C_MAPDELETE) {
            key = ODG_MKEY_DELETE;
            rc = odg_mind_delete(doc, node, both);
        } else if (c == C_MAPMOVE) {
            key = ODG_MKEY_MOVE_DOWN;
            rc = odg_mind_move(doc, node, a->value[1] ? -1 : 1, both);
        } else if (c == C_MAPFOLD) {
            key = ODG_MKEY_FOLD_ON;
            rc = odg_mind_fold(doc, node, a->value[1] == NULL, both);
        } else
            rc = odg_mind_layout(doc, node, a->value[1] ? 1 : a->value[2] ? 0 : both);
        why = rc == OWF_ERR_FORMAT ? odg_mind_why(doc, node, key) : "not possible";
        if ((rc = edit_rc(rx, rc, commands[c].name, why)) == ODG_RX_OK && text[0]) put(result, size, text);
        if (rc == ODG_RX_OK && c == C_MAPLAYOUT && !odg_mind_whole(doc, node)) {   /* RC 5, as MAPCHILDREN */
            snprintf(rx->lasterror, sizeof rx->lasterror, "MAPLAYOUT: the map has more than %d nodes; the rest are "
                     "where they were", ODG_MIND_MAX);
            return ODG_RX_NOW;
        }
        return rc;
    }
    case C_MAPROOT:
    case C_MAPPARENT:
    case C_MAPCHILDREN: {
        const owf_dcell *got;
        if (!odg_mind_is_node(owf_dpage_find(odg_page(doc), a->value[0]))) {
            snprintf(rx->lasterror, sizeof rx->lasterror, "%s: no map node %.100s", commands[c].name, a->value[0]);
            return ODG_RX_ERROR;
        }
        result[0] = 0;
        if (c == C_MAPCHILDREN) {           /* every child; RC 5 when the answer can't hold them all */
            size_t at = 0, was;
            int i, n = odg_mind_children(doc, a->value[0], NULL, 0), cut = 0;
            {
                const owf_dcell **kids = n ? (const owf_dcell **)malloc((size_t)n * sizeof *kids) : NULL;
                if (n && !kids) {
                    snprintf(rx->lasterror, sizeof rx->lasterror, "MAPCHILDREN: out of memory");
                    return ODG_RX_FAIL;
                }
                odg_mind_children(doc, a->value[0], kids, n);
                for (i = 0; i < n; i++) {
                    was = at;
                    put_word(result, size, &at, kids[i]->id);
                    if (at == was) cut = 1;
                }
                free(kids);
            }
            if (cut) {
                snprintf(rx->lasterror, sizeof rx->lasterror, "MAPCHILDREN: %d children, more than the answer holds", n);
                return ODG_RX_NOW;
            }
            return ODG_RX_OK;
        }
        got = c == C_MAPROOT ? odg_mind_root(doc, a->value[0]) : odg_mind_parent(doc, a->value[0]);
        if (c == C_MAPROOT && !got) {
            snprintf(rx->lasterror, sizeof rx->lasterror, "MAPROOT: the map loops back on itself");
            return ODG_RX_ERROR;
        }
        if (got) put(result, size, got->id);
        return ODG_RX_OK;
    }
    case C_MAPSTEP: {
        static const char *const ways[4] = { "PARENT", "CHILD", "UP", "DOWN" };
        int w;
        for (w = 0; w < 4 && !word_at(a->value[1], ways[w], strlen(ways[w]), 0); w++)
            ;
        if (w == 4) {
            snprintf(rx->lasterror, sizeof rx->lasterror, "MAPSTEP: WAY is PARENT, CHILD, UP or DOWN");
            return ODG_RX_ERROR;
        }
        if (!odg_mind_is_node(owf_dpage_find(odg_page(doc), a->value[0]))) {
            snprintf(rx->lasterror, sizeof rx->lasterror, "MAPSTEP: no map node %.100s", a->value[0]);
            return ODG_RX_ERROR;
        }
        result[0] = 0;
        if (odg_mind_step(doc, a->value[0], (odg_mind_dir)w, text)) put(result, size, text);
        return ODG_RX_OK;
    }
    case C_MAPEXPORT:
    case C_MAPIMPORT: {
        const char *name = a->value[c == C_MAPEXPORT ? 1 : 0], *fmt = a->value[c == C_MAPEXPORT ? 2 : 3];
        owf_outline_kind kind;
        owf_ditem *items = NULL;
        int n = 0;
        unsigned char *data = NULL;
        size_t length = 0;
        if (fmt && !outline_kind(fmt, &kind)) {
            snprintf(rx->lasterror, sizeof rx->lasterror, "%s: FORMAT is MM, OPML or MD, not %.100s",
                     commands[c].name, fmt);
            return ODG_RX_ERROR;
        }
        if (c == C_MAPEXPORT) {
            char *out = NULL;
            FILE *f;
            int ok;
            if (!fmt) kind = owf_outline_guess(name, NULL, 0);
            rc = odg_mind_items(doc, a->value[0], &items, &n);
            if (rc == OWF_OK) rc = owf_outline_write(items, n, kind, NULL, &out, &length);
            owf_outline_free(items, n);
            if (rc != OWF_OK) {
                snprintf(rx->lasterror, sizeof rx->lasterror, rc == OWF_ERR_MEMORY ? "MAPEXPORT: out of memory"
                         : "MAPEXPORT: %.100s is not in a map (or its map loops back on itself)", a->value[0]);
                return rc == OWF_ERR_MEMORY ? ODG_RX_FAIL : ODG_RX_ERROR;
            }
            f = fopen(name, "wb");
            ok = f && fwrite(out, 1, length, f) == length;
            if (f && fclose(f) != 0) ok = 0;
            owf_free(out);
            if (!ok) {
                snprintf(rx->lasterror, sizeof rx->lasterror, "MAPEXPORT: could not write %.200s", name);
                return ODG_RX_ERROR;
            }
            if (!odg_mind_whole(doc, a->value[0])) {
                snprintf(rx->lasterror, sizeof rx->lasterror, "MAPEXPORT: the map has more than %d nodes; the "
                         "first %d were written", ODG_MIND_MAX, ODG_MIND_MAX);
                return ODG_RX_NOW;
            }
            return ODG_RX_OK;
        } else {
            owf_dnum x = a->num[1], y = a->num[2], box[4];
            int got;
            if (!layer_open(rx, "MAPIMPORT")) return ODG_RX_ERROR;
            got = read_file(name, &data, &length);
            if (got <= 0) {
                snprintf(rx->lasterror, sizeof rx->lasterror, got < 0 ? "MAPIMPORT: out of memory reading %.200s"
                         : "MAPIMPORT: could not read %.200s", name);
                return got < 0 ? ODG_RX_FAIL : ODG_RX_ERROR;
            }
            if (!fmt) kind = owf_outline_guess(name, (const char *)data, length);
            if (!a->value[1] || !a->value[2]) {     /* no place given: right of what the page has */
                if (owf_dpage_bounds(odg_page(doc), box)) {
                    if (!a->value[1]) x = box[0] + box[2] + OWF_DNUM(40);
                    if (!a->value[2]) y = box[1];
                } else {
                    if (!a->value[1]) x = OWF_DNUM(40);
                    if (!a->value[2]) y = OWF_DNUM(40);
                }
            }
            rc = owf_outline_read((const char *)data, length, kind, NULL, &items, &n);
            owf_free(data);
            if (rc == OWF_OK) rc = odg_mind_import(doc, x, y, items, n, 0, text);
            owf_outline_free(items, n);
            if (rc == OWF_ERR_MEMORY) {
                snprintf(rx->lasterror, sizeof rx->lasterror, "MAPIMPORT: out of memory");
                return ODG_RX_FAIL;
            }
            if (rc != OWF_OK) {
                snprintf(rx->lasterror, sizeof rx->lasterror, "MAPIMPORT: %.150s is not an outline GFOpenDiag "
                         "reads (FreeMind .mm, OPML, or Markdown headings and lists)", name);
                return ODG_RX_ERROR;
            }
            rx->changes++;
            put(result, size, text);
            if (!odg_mind_whole(doc, text)) {
                snprintf(rx->lasterror, sizeof rx->lasterror, "MAPIMPORT: the map has more than %d nodes; the rest "
                         "are not laid out", ODG_MIND_MAX);
                return ODG_RX_NOW;
            }
            return ODG_RX_OK;
        }
    }
    case C_LAYER: {
        int i, n = odg_layer_count(doc);
        if (a->value[0]) {
            if (!layer_arg(rx, "LAYER", a->num[0], &i)) return ODG_RX_ERROR;
            doc->layer = i;
        }
        snprintf(result, size, "%d", doc->layer >= 0 && doc->layer < n ? doc->layer + 1 : 1);
        return ODG_RX_OK;
    }
    case C_ADDLAYER: {
        int at = 0;
        rc = edit_rc(rx, odg_layer_add(doc, a->value[0], &at), "ADDLAYER", "the page has no root cell");
        if (rc == ODG_RX_OK) snprintf(result, size, "%d", at + 1);
        return rc;
    }
    case C_GETLAYER: {
        int i;
        if (!layer_arg(rx, "GETLAYER", a->num[0], &i)) return ODG_RX_ERROR;
        put(result, size, odg_layer_at(doc, i)->value);
        return ODG_RX_OK;
    }
    case C_SHOWLAYER:
    case C_LOCKLAYER:
    case C_RENAMELAYER: {
        int i;
        if (!layer_arg(rx, commands[c].name, a->num[0], &i)) return ODG_RX_ERROR;
        if (c == C_SHOWLAYER) rc = odg_layer_show(doc, i, a->value[1] == NULL);
        else if (c == C_LOCKLAYER) rc = odg_layer_lock(doc, i, a->value[1] == NULL);
        else rc = odg_layer_rename(doc, i, a->value[1]);
        return edit_rc(rx, rc, commands[c].name, "no such layer");
    }
    case C_UNDO:
    case C_REDO:
        rc = c == C_UNDO ? odg_undo(doc) : odg_redo(doc);
        if (rc == OWF_ERR_FORMAT) {
            snprintf(rx->lasterror, sizeof rx->lasterror, "%s: nothing to %s", commands[c].name, c == C_UNDO ? "undo" : "redo");
            return ODG_RX_NOW;
        }
        return edit_rc(rx, rc, commands[c].name, "not possible");
    }
    return ODG_RX_FAIL;
}

int odg_rexx_command(odg_rexx *rx, const char *line, char *result, size_t size)
{
    char scratch[8];
    static args a;                       /* 2 KB of argument text: off the Amiga's small stack */
    int i, rc;
    if (!result || !size) result = scratch, size = sizeof scratch;
    result[0] = 0;
    if (!rx || !line) return ODG_RX_FAIL;
    line = skip_space(line);
    for (i = 0; i < NCOMMANDS; i++)
        if (word_at(line, commands[i].name, strlen(commands[i].name), 0)) break;
    if (i == NCOMMANDS) {
        size_t n = 0;
        while (line[n] && line[n] != ' ' && n < 40) n++;
        snprintf(rx->lasterror, sizeof rx->lasterror, "Unknown GFOpenDiag ARexx command %.*s; HELP lists them",
                 (int)n, line);
        return ODG_RX_FAIL;
    }
    parse_template(commands[i].tmpl, &a);
    if (!parse_args(line + strlen(commands[i].name), &a, rx->lasterror, sizeof rx->lasterror)) {
        char why[sizeof rx->lasterror];
        memcpy(why, rx->lasterror, sizeof why);
        snprintf(rx->lasterror, sizeof rx->lasterror, "%s: %.200s", commands[i].name, why);
        return ODG_RX_ERROR;
    }
    rc = run(rx, i, &a, result, size);
    if (rc != ODG_RX_OK) result[0] = 0;
    return rc;
}
