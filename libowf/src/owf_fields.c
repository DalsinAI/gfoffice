/*
 * libowf 1.7: fields with an argument or a shown value, the document's author and subject, user fields
 * (owf_fields.h).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "owf_fields.h"
#include "owf_track.h"

#include <stdlib.h>
#include <string.h>

static char *copy_string(const char *s)
{
    size_t n;
    char *out;
    if (!s)
        return NULL;
    n = strlen(s);
    out = malloc(n + 1);
    if (out)
        memcpy(out, s, n + 1);
    return out;
}

int owf_para_add_field(owf_para *para, const owf_charfmt *fmt, owf_field field, const char *arg, const char *shown)
{
    owf_run *run;
    char *a = NULL, *t = NULL;
    if (!para || !fmt)
        return -1;
    if ((arg && *arg && !(a = copy_string(arg))) || (shown && !(t = copy_string(shown)))) {
        free(a);
        return -1;
    }
    if (owf_para_add_special(para, fmt, OWF_RUN_FIELD, field) < 0) {
        free(a);
        free(t);
        return -1;
    }
    run = &para->runs[para->nruns - 1];
    run->href = a;
    run->text = t;
    return 0;
}

void owf_builder_field(owf_builder *b, owf_field field, const char *arg, const char *shown)
{
    owf_para *p = owf_builder_para(b);               /* owf_track.h's: flushes the text before it */
    if (!p || owf_para_add_field(p, &b->charfmt, field, arg, shown) < 0)
        b->failed = 1;
}

static int set_string(char **at, const char *utf8)
{
    char *s = NULL;
    if (utf8 && *utf8 && !(s = copy_string(utf8)))
        return -1;
    free(*at);
    *at = s;
    return 0;
}

int owf_doc_set_author(owf_doc *doc, const char *utf8)
{
    return doc ? set_string(&doc->author, utf8) : -1;
}

int owf_doc_set_subject(owf_doc *doc, const char *utf8)
{
    return doc ? set_string(&doc->subject, utf8) : -1;
}

int owf_doc_set_user_field(owf_doc *doc, const char *name, const char *value)
{
    owf_userfield *u;
    char *v;
    int i;
    if (!doc || !name || !*name)
        return -1;
    if (!(v = copy_string(value ? value : "")))
        return -1;
    for (i = 0; i < doc->nuserfields; ++i)
        if (!strcmp(doc->userfields[i].name, name)) {
            free(doc->userfields[i].value);
            doc->userfields[i].value = v;
            return 0;
        }
    if (doc->nuserfields == doc->capuserfields) {
        int want = doc->capuserfields ? doc->capuserfields * 2 : 8;
        owf_userfield *grown = realloc(doc->userfields, (size_t)want * sizeof *grown);
        if (!grown) {
            free(v);
            return -1;
        }
        doc->userfields = grown;
        doc->capuserfields = want;
    }
    u = &doc->userfields[doc->nuserfields];
    if (!(u->name = copy_string(name))) {
        free(v);
        return -1;
    }
    u->value = v;
    doc->nuserfields++;
    return 0;
}

int owf_doc_set_merge_source(owf_doc *doc, const char *path)
{
    return doc ? set_string(&doc->merge_source, path) : -1;
}

const char *owf_doc_user_field(const owf_doc *doc, const char *name)
{
    int i;
    for (i = 0; doc && name && i < doc->nuserfields; ++i)
        if (!strcmp(doc->userfields[i].name, name))
            return doc->userfields[i].value;
    return NULL;
}

const char *owf_field_name(owf_field field)
{
    static const char *const names[] = { "PAGE", "PAGES", "DATE", "TIME", "TITLE", "AUTHOR", "SUBJECT", "FILENAME",
                                         "WORDS", "CHAPTER", "USER", "REF", "REFPAGE", "MERGE", "INDEX" };
    return (int)field >= 0 && (int)field <= OWF_FIELD_LAST ? names[field] : "";
}
