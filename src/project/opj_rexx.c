/*
 * opj_rexx.c: the plan's ARexx commands for GFOpenProject's GFOPENPROJECT port (openproject_core.h). Each
 * command has a ReadArgs-style template, matched here by hand so that it runs (and is tested) on the PC as
 * well as the Amiga: quoted words, KEYWORD=value or KEYWORD value, /A needed, /N a number, /S a switch,
 * /K only by its keyword, /F the rest of the line. Edits go through the editing core, one Undo step each.
 * Return codes and LASTERROR as every Open app's port (amicradle-teams ARexx-RULE.md 3 and 4).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "openproject_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_ARGS 6
#define ARG_TEXT 1024

typedef struct {
    const char *name;
    const char *tmpl;
} command;

enum {
    C_HELP, C_LASTERROR, C_ISDIRTY, C_GETFILE, C_NEW, C_OPEN, C_SAVE, C_SAVEAS, C_EXPORT, C_TASKS, C_ADDTASK,
    C_DELETETASK, C_GETCELL, C_SETCELL, C_INDENT, C_OUTDENT, C_LINK, C_UNLINK, C_UNDO, C_REDO, C_BASELINE,
    C_RESOURCES, C_ADDRESOURCE, C_DELETERESOURCE, C_GETRESOURCE, C_SETRESOURCE, C_ASSIGN, C_USAGE, C_FINISH,
    C_CRITICAL, NCOMMANDS
};

static const command commands[NCOMMANDS] = {
    { "HELP", "COMMAND" },
    { "LASTERROR", "" },
    { "ISDIRTY", "" },
    { "GETFILE", "" },
    { "NEW", "START/K,FORCE/S" },
    { "OPEN", "FILE/A,FORCE/S" },
    { "SAVE", "" },
    { "SAVEAS", "FILE/A" },
    { "EXPORT", "FILE/A,FORMAT/K" },
    { "TASKS", "" },
    { "ADDTASK", "NAME/A,AT/K/N,DURATION/K" },
    { "DELETETASK", "ROW/A/N,COUNT/N" },
    { "GETCELL", "ROW/A/N,COLUMN/A" },
    { "SETCELL", "ROW/A/N,COLUMN/A,TEXT/A/F" },
    { "INDENT", "ROW/A/N,COUNT/N" },
    { "OUTDENT", "ROW/A/N,COUNT/N" },
    { "LINK", "ROW/A/N,COUNT/N" },
    { "UNLINK", "ROW/A/N,COUNT/N" },
    { "UNDO", "" },
    { "REDO", "" },
    { "BASELINE", "ROW/N,COUNT/N,CLEAR/S" },
    { "RESOURCES", "" },
    { "ADDRESOURCE", "NAME/A" },
    { "DELETERESOURCE", "ROW/A/N,COUNT/N" },
    { "GETRESOURCE", "ROW/A/N,COLUMN/A" },
    { "SETRESOURCE", "ROW/A/N,COLUMN/A,TEXT/A/F" },
    { "ASSIGN", "ROW/A/N,RESOURCE/A,UNITS/N" },
    { "USAGE", "RESOURCE/A,FROM/A,TO/A" },
    { "FINISH", "" },
    { "CRITICAL", "" },
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

/* The next word (a quoted one with its quotes off, "" for "" and *"" for a quote inside) into a's text. */
static const char *take_word(const char **s, args *a)
{
    const char *p = *s;
    char *out = a->text + a->used;
    size_t room = sizeof a->text - a->used, k = 0;
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
        }
        if (*p == '"') p++;
    } else
        while (*p && *p != ' ' && *p != '\t' && *p != '=') {
            if (k < room - 1) out[k++] = *p;
            p++;
        }
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
    while (end > p && (end[-1] == ' ' || end[-1] == '\t')) end--;
    if (*p == '"') {
        const char *q = p;
        const char *w = take_word(&q, a);
        if (*skip_space(q) == 0) {
            *s = q;
            return w;
        }
        a->used -= strlen(w) + 1;        /* more after the quotes: the whole line, quotes and all */
    }
    n = (size_t)(end - p);
    if (n > room - 1) n = room - 1;
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
            char *end;
            a->num[i] = strtol(v, &end, 10);
            if (end == v || *end) {
                snprintf(err, esize, "%.15s must be a number, not \"%.100s\"", a->f[i].name, v);
                return 0;
            }
        }
    }
    return 1;
}

/* ---- Columns and resources by name ---- */

/* Letters and digits, lower case, for matching "% Complete", "Start Var." and the like. */
static void squash(const char *s, char *out, size_t size)
{
    size_t k = 0;
    for (; *s && k < size - 1; s++) {
        int c = (unsigned char)*s;
        if (c >= 'A' && c <= 'Z') c += 32;
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) out[k++] = (char)c;
    }
    out[k] = 0;
}

static int find_column(const char *name, int resource_sheet)
{
    static const struct { const char *alias; int column; } tasks[] = {
        { "name", OPJ_COL_NAME }, { "percent", OPJ_COL_PERCENT }, { "complete", OPJ_COL_PERCENT },
        { "resources", OPJ_COL_RESOURCES }, { "startvariance", OPJ_COL_START_VARIANCE },
        { "finishvariance", OPJ_COL_FINISH_VARIANCE },
    }, res[] = {
        { "maxunits", OPJ_RCOL_MAX }, { "max", OPJ_RCOL_MAX }, { "rate", OPJ_RCOL_RATE },
        { "stdrate", OPJ_RCOL_RATE }, { "name", OPJ_RCOL_NAME },
    };
    char want[40], have[40];
    int i, n = resource_sheet ? OPJ_NRCOLS : OPJ_NCOLS;
    squash(name, want, sizeof want);
    if (!want[0]) return -1;
    for (i = 0; i < n; i++) {
        squash(resource_sheet ? opj_resource_column_title(i) : opj_column_title(i), have, sizeof have);
        if (strcmp(have, want) == 0) return i;
    }
    if (resource_sheet) {
        for (i = 0; i < (int)(sizeof res / sizeof *res); i++)
            if (strcmp(res[i].alias, want) == 0) return res[i].column;
    } else
        for (i = 0; i < (int)(sizeof tasks / sizeof *tasks); i++)
            if (strcmp(tasks[i].alias, want) == 0) return tasks[i].column;
    return -1;
}

/* A resource by its number (from 1) or its name (case ignored); its index or -1. */
static int find_resource(const owf_project *p, const char *s)
{
    char *end;
    long n = strtol(s, &end, 10);
    int i;
    if (end != s && !*end) return n >= 1 && n <= p->nresources ? (int)n - 1 : -1;
    for (i = 0; i < p->nresources; i++) {
        const char *a = p->resources[i].name, *b = s;
        if (!a) continue;
        while (*a && *b && upper((unsigned char)*a) == upper((unsigned char)*b)) a++, b++;
        if (!*a && !*b) return i;
    }
    return -1;
}

/* ---- Files ---- */

int opj_rexx_format(const char *file)
{
    const char *dot = file ? strrchr(file, '.') : NULL;
    char ext[8];
    if (!dot || strlen(dot) >= sizeof ext) return OWF_PFILE_GFPROJ;
    squash(dot, ext, sizeof ext);
    if (strcmp(ext, "xml") == 0) return OWF_PFILE_MSPDI;
    if (strcmp(ext, "csv") == 0) return OWF_PFILE_CSV;
    return OWF_PFILE_GFPROJ;
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

static int write_file(opj_rexx *rx, const char *name, int format)
{
    unsigned char *data;
    size_t length;
    FILE *f;
    int rc = owf_project_write(rx->doc->plan, format, &data, &length), ok;
    if (rc == OWF_ERR_MEMORY) {
        snprintf(rx->lasterror, sizeof rx->lasterror, "Out of memory writing %s", name);
        return OPJ_RX_FAIL;
    }
    if (rc != OWF_OK) {
        snprintf(rx->lasterror, sizeof rx->lasterror, "Could not make %s", name);
        return OPJ_RX_ERROR;
    }
    f = fopen(name, "wb");
    ok = f && fwrite(data, 1, length, f) == length;
    if (f && fclose(f) != 0) ok = 0;
    owf_free(data);
    if (!ok) {
        snprintf(rx->lasterror, sizeof rx->lasterror, "Could not write %s", name);
        return OPJ_RX_ERROR;
    }
    return OPJ_RX_OK;
}

static void set_file(opj_rexx *rx, const char *name)
{
    size_t n = strlen(name);
    if (n > sizeof rx->file - 1) n = sizeof rx->file - 1;
    memcpy(rx->file, name, n);
    rx->file[n] = 0;
}

/* ---- Commands ---- */

static int edit_rc(opj_rexx *rx, int rc, const char *what)
{
    if (rc == OWF_OK) return OPJ_RX_OK;
    if (rc == OWF_ERR_MEMORY) {
        snprintf(rx->lasterror, sizeof rx->lasterror, "%s: out of memory", what);
        return OPJ_RX_FAIL;
    }
    snprintf(rx->lasterror, sizeof rx->lasterror, "%s: not possible (out of range, bad text, or a loop of links)", what);
    return OPJ_RX_ERROR;
}

static void put(char *result, size_t size, const char *s)
{
    size_t n = strlen(s);
    if (n > size - 1) n = size - 1;
    memcpy(result, s, n);
    result[n] = 0;
}

static int run(opj_rexx *rx, int c, args *a, char *result, size_t size)
{
    opj_doc *doc = rx->doc;
    owf_project *p = doc->plan;
    char text[256];
    long row = a->value[0] && a->f[0].number ? a->num[0] - 1 : 0;
    long count = a->value[1] && a->f[1].number ? a->num[1] : 1;
    int col, r, rc;
    switch (c) {
    case C_HELP:
        if (a->value[0]) {
            int i;
            for (i = 0; i < NCOMMANDS; i++)
                if (word_at(a->value[0], commands[i].name, strlen(commands[i].name), 0)) {
                    snprintf(result, size, "%s %s", commands[i].name, commands[i].tmpl);
                    return OPJ_RX_OK;
                }
            snprintf(rx->lasterror, sizeof rx->lasterror, "HELP: no command %s", a->value[0]);
            return OPJ_RX_ERROR;
        } else {
            size_t at = 0;
            int i;
            static const char *const app[] = { "VERSION", "ACTIVATE", "DEACTIVATE", "FAULT", "PRINT", "QUIT" };
            result[0] = 0;
            for (i = 0; i < (int)(sizeof app / sizeof *app) + NCOMMANDS; i++) {
                const char *w = i < 6 ? app[i] : commands[i - 6].name;
                size_t n = strlen(w);
                if (at + n + 2 > size) break;
                if (at) result[at++] = ' ';
                memcpy(result + at, w, n + 1);
                at += n;
            }
        }
        return OPJ_RX_OK;
    case C_LASTERROR: put(result, size, rx->lasterror); return OPJ_RX_OK;
    case C_ISDIRTY: put(result, size, doc->dirty ? "1" : "0"); return OPJ_RX_OK;
    case C_GETFILE: put(result, size, rx->file); return OPJ_RX_OK;
    case C_NEW:
    case C_OPEN: {
        owf_project *plan = NULL;
        opj_doc *fresh;
        int force = a->value[1] != NULL;
        if (doc->dirty && !force) {
            snprintf(rx->lasterror, sizeof rx->lasterror, "%s: the plan has unsaved changes (FORCE drops them)",
                     commands[c].name);
            return OPJ_RX_NOW;
        }
        if (c == C_NEW) {
            owf_ptime start = p->start;
            if (a->value[0] && !opj_date_parse(a->value[0], 8 * 60, &start)) {
                snprintf(rx->lasterror, sizeof rx->lasterror, "NEW: \"%s\" is not a date", a->value[0]);
                return OPJ_RX_ERROR;
            }
            fresh = opj_doc_new(NULL, start);
        } else {
            unsigned char *data;
            size_t length;
            int got = read_file(a->value[0], &data, &length);
            if (got <= 0) {
                snprintf(rx->lasterror, sizeof rx->lasterror, got < 0 ? "OPEN: out of memory reading %s"
                                                                        : "OPEN: could not read %s", a->value[0]);
                return got < 0 ? OPJ_RX_FAIL : OPJ_RX_ERROR;
            }
            rc = owf_project_read(data, length, &plan);
            owf_free(data);
            if (rc != OWF_OK) {
                snprintf(rx->lasterror, sizeof rx->lasterror, rc == OWF_ERR_MEMORY ? "OPEN: out of memory reading %s"
                         : "OPEN: %s is not a plan GFOpenProject reads (.gfproj, MS Project XML, MPX, CSV)", a->value[0]);
                return rc == OWF_ERR_MEMORY ? OPJ_RX_FAIL : OPJ_RX_ERROR;
            }
            fresh = opj_doc_new(plan, 0);
        }
        if (!fresh) {
            snprintf(rx->lasterror, sizeof rx->lasterror, "%s: out of memory", commands[c].name);
            return OPJ_RX_FAIL;
        }
        opj_doc_free(doc);
        rx->doc = fresh;
        if (c == C_OPEN) set_file(rx, a->value[0]);
        else rx->file[0] = 0;
        return OPJ_RX_OK;
    }
    case C_SAVE:
        if (!rx->file[0]) {
            snprintf(rx->lasterror, sizeof rx->lasterror, "SAVE: the plan has no file yet (SAVEAS names one)");
            return OPJ_RX_NOW;
        }
        if ((rc = write_file(rx, rx->file, opj_rexx_format(rx->file))) == OPJ_RX_OK) opj_doc_saved(doc);
        return rc;
    case C_SAVEAS:
        if ((rc = write_file(rx, a->value[0], opj_rexx_format(a->value[0]))) == OPJ_RX_OK) {
            set_file(rx, a->value[0]);
            opj_doc_saved(doc);
        }
        return rc;
    case C_EXPORT: {
        int format = opj_rexx_format(a->value[0]);
        if (a->value[1]) {
            char f[16];
            squash(a->value[1], f, sizeof f);
            if (strcmp(f, "mspdi") == 0 || strcmp(f, "xml") == 0) format = OWF_PFILE_MSPDI;
            else if (strcmp(f, "csv") == 0) format = OWF_PFILE_CSV;
            else if (strcmp(f, "gfproj") == 0) format = OWF_PFILE_GFPROJ;
            else {
                snprintf(rx->lasterror, sizeof rx->lasterror, "EXPORT: FORMAT is MSPDI, CSV or GFPROJ, not %s", a->value[1]);
                return OPJ_RX_ERROR;
            }
        }
        return write_file(rx, a->value[0], format);
    }
    case C_TASKS: snprintf(result, size, "%d", p->ntasks); return OPJ_RX_OK;
    case C_ADDTASK: {
        long at = a->value[1] ? a->num[1] - 1 : p->ntasks;
        long minutes;
        int el, es;
        if (at < 0 || at > p->ntasks) return edit_rc(rx, OWF_ERR_FORMAT, "ADDTASK");
        if (a->value[2] && (owf_pduration_parse(p, a->value[2], &minutes, &el, &es) != OWF_OK || minutes < 0)) {
            snprintf(rx->lasterror, sizeof rx->lasterror, "ADDTASK: \"%s\" is not a duration", a->value[2]);
            return OPJ_RX_ERROR;
        }
        if ((rc = opj_insert_task(doc, (int)at, a->value[0])) != OWF_OK) return edit_rc(rx, rc, "ADDTASK");
        if (a->value[2] && (rc = opj_set_cell(doc, (int)at, OPJ_COL_DURATION, a->value[2])) != OWF_OK)
            return edit_rc(rx, rc, "ADDTASK DURATION");
        snprintf(result, size, "%ld", at + 1);
        return OPJ_RX_OK;
    }
    case C_DELETETASK: return edit_rc(rx, opj_delete_tasks(doc, (int)row, (int)count), "DELETETASK");
    case C_GETCELL:
    case C_SETCELL:
    case C_GETRESOURCE:
    case C_SETRESOURCE: {
        int res = c == C_GETRESOURCE || c == C_SETRESOURCE;
        if ((col = find_column(a->value[1], res)) < 0) {
            snprintf(rx->lasterror, sizeof rx->lasterror, "%s: no column %s", commands[c].name, a->value[1]);
            return OPJ_RX_ERROR;
        }
        if (row < 0 || row >= (res ? p->nresources : p->ntasks))
            return edit_rc(rx, OWF_ERR_FORMAT, commands[c].name);
        if (c == C_GETCELL) opj_cell_text(doc, (int)row, col, result, size);
        else if (c == C_GETRESOURCE) opj_resource_cell_text(doc, (int)row, col, result, size);
        else if (c == C_SETCELL) return edit_rc(rx, opj_set_cell(doc, (int)row, col, a->value[2]), "SETCELL");
        else return edit_rc(rx, opj_set_resource_cell(doc, (int)row, col, a->value[2]), "SETRESOURCE");
        return OPJ_RX_OK;
    }
    case C_INDENT:
    case C_OUTDENT:
        return edit_rc(rx, opj_indent(doc, (int)row, (int)count, c == C_INDENT ? 1 : -1), commands[c].name);
    case C_LINK: return edit_rc(rx, opj_link_rows(doc, (int)row, a->value[1] ? (int)count : 2), "LINK");
    case C_UNLINK: return edit_rc(rx, opj_unlink_rows(doc, (int)row, (int)count), "UNLINK");
    case C_UNDO:
    case C_REDO:
        if ((c == C_UNDO ? opj_undo(doc) : opj_redo(doc)) != OWF_OK) {
            snprintf(rx->lasterror, sizeof rx->lasterror, "%s: nothing to %s", commands[c].name, c == C_UNDO ? "undo" : "redo");
            return OPJ_RX_NOW;
        }
        return OPJ_RX_OK;
    case C_BASELINE: {
        int first = a->value[0] ? (int)row : 0, n = a->value[0] ? (int)count : -1;
        return edit_rc(rx, a->value[2] ? opj_clear_baseline(doc, first, n) : opj_set_baseline(doc, first, n), "BASELINE");
    }
    case C_RESOURCES: snprintf(result, size, "%d", p->nresources); return OPJ_RX_OK;
    case C_ADDRESOURCE:
        if ((rc = opj_add_resource(doc, a->value[0], &r)) != OWF_OK) return edit_rc(rx, rc, "ADDRESOURCE");
        snprintf(result, size, "%d", r + 1);
        return OPJ_RX_OK;
    case C_DELETERESOURCE: return edit_rc(rx, opj_delete_resources(doc, (int)row, (int)count), "DELETERESOURCE");
    case C_ASSIGN:
        if ((r = find_resource(p, a->value[1])) < 0) {
            snprintf(rx->lasterror, sizeof rx->lasterror, "ASSIGN: no resource %s", a->value[1]);
            return OPJ_RX_ERROR;
        }
        return edit_rc(rx, opj_set_assignment(doc, (int)row, r, a->value[2] ? a->num[2] : 100), "ASSIGN");
    case C_USAGE: {
        owf_ptime from, to;
        if ((r = find_resource(p, a->value[0])) < 0) {
            snprintf(rx->lasterror, sizeof rx->lasterror, "USAGE: no resource %s", a->value[0]);
            return OPJ_RX_ERROR;
        }
        if (!opj_date_parse(a->value[1], 0, &from) || !opj_date_parse(a->value[2], 0, &to) || to <= from) {
            snprintf(rx->lasterror, sizeof rx->lasterror, "USAGE: FROM and TO are dates, TO after FROM");
            return OPJ_RX_ERROR;
        }
        opj_usage_text(doc, r, -1, from, to, result, size, NULL);
        if (!result[0]) put(result, size, "0h");
        return OPJ_RX_OK;
    }
    case C_FINISH: {
        owf_ptime end = p->start;
        int i;
        for (i = 0; i < p->ntasks; i++)
            if (p->tasks[i].early_finish > end) end = p->tasks[i].early_finish;
        opj_date_text(end, 1, text);
        put(result, size, text);
        return OPJ_RX_OK;
    }
    case C_CRITICAL: {
        size_t at = 0;
        int i;
        result[0] = 0;
        for (i = 0; i < p->ntasks; i++) {
            if (!p->tasks[i].critical || p->tasks[i].summary) continue;
            sprintf(text, at ? " %d" : "%d", i + 1);
            if (at + strlen(text) + 1 > size) break;
            strcpy(result + at, text);
            at += strlen(text);
        }
        return OPJ_RX_OK;
    }
    }
    return OPJ_RX_FAIL;
}

int opj_rexx_command(opj_rexx *rx, const char *line, char *result, size_t size)
{
    char scratch[8];
    args a;
    int i, rc;
    if (!result || !size) result = scratch, size = sizeof scratch;
    result[0] = 0;
    if (!rx || !rx->doc || !line) return OPJ_RX_FAIL;
    line = skip_space(line);
    for (i = 0; i < NCOMMANDS; i++)
        if (word_at(line, commands[i].name, strlen(commands[i].name), 0)) break;
    if (i == NCOMMANDS) {
        size_t n = 0;
        while (line[n] && line[n] != ' ' && n < 40) n++;
        snprintf(rx->lasterror, sizeof rx->lasterror, "Unknown GFOpenProject ARexx command %.*s; HELP lists them",
                 (int)n, line);
        return OPJ_RX_FAIL;
    }
    parse_template(commands[i].tmpl, &a);
    if (!parse_args(line + strlen(commands[i].name), &a, rx->lasterror, sizeof rx->lasterror)) {
        char why[sizeof rx->lasterror];
        memcpy(why, rx->lasterror, sizeof why);
        snprintf(rx->lasterror, sizeof rx->lasterror, "%s: %.200s", commands[i].name, why);
        return OPJ_RX_ERROR;
    }
    rc = run(rx, i, &a, result, size);
    if (rc != OPJ_RX_OK) result[0] = 0;
    return rc;
}
