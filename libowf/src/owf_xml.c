/*
 * libowf: a small XML reader for ODT and DOCX. It parses a document into a
 * tree in one pass, in place in a copy of the text, with namespaces
 * resolved. Well-formed documents only; no DTDs, no external entities, so a
 * hostile document cannot make it read files or expand without end.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "owf_internal.h"

#include <stdlib.h>
#include <string.h>

#define BLOCK_SIZE 32768

typedef struct block {
    struct block *next;
    size_t used, size;
    /* data follows */
} block;

typedef struct {
    const char *prefix;      /* "" for the default namespace */
    const char *uri;
    int depth;
} binding;

struct owf_xml {
    char *text;              /* the copy we parse in place */
    block *blocks;
    owf_xml_node *root;
    binding *bindings;
    int nbindings, capbindings;
};

static void *arena(owf_xml *x, size_t size)
{
    block *b = x->blocks;
    size = (size + 7) & ~(size_t)7;
    if (!b || b->used + size > b->size) {
        size_t want = size > BLOCK_SIZE ? size : BLOCK_SIZE;
        b = malloc(sizeof(block) + want);
        if (!b)
            return NULL;
        b->next = x->blocks;
        b->used = 0;
        b->size = want;
        x->blocks = b;
    }
    b->used += size;
    return (char *)(b + 1) + b->used - size;
}

void owf_xml_free(owf_xml *x)
{
    block *b, *next;
    if (!x)
        return;
    for (b = x->blocks; b; b = next) {
        next = b->next;
        free(b);
    }
    free(x->bindings);
    free(x->text);
    free(x);
}

static int name_char(int c)
{
    return c && c != ' ' && c != '\t' && c != '\r' && c != '\n' && c != '/' && c != '>' && c != '=' &&
           c != '<' && c != '"' && c != '\'';
}

static int space(int c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/* Decodes entities in s, in place (the result is never longer); returns
 * the end. The result is NUL-terminated only when it got shorter. */
static char *decode(char *s, char *end)
{
    char *in = s, *out = s;
    while (in < end) {
        if (*in != '&') {
            *out++ = *in++;
            continue;
        }
        {
            char *semi = memchr(in, ';', (size_t)(end - in));
            size_t n = semi ? (size_t)(semi - in) : 0;
            unsigned long c = 0;
            if (n == 3 && !memcmp(in, "&lt", 3)) c = '<';
            else if (n == 3 && !memcmp(in, "&gt", 3)) c = '>';
            else if (n == 4 && !memcmp(in, "&amp", 4)) c = '&';
            else if (n == 5 && !memcmp(in, "&quot", 5)) c = '"';
            else if (n == 5 && !memcmp(in, "&apos", 5)) c = '\'';
            else if (n > 2 && in[1] == '#') {
                char *p = in + 2;
                int hex = (*p == 'x' || *p == 'X');
                if (hex)
                    p++;
                for (; p < semi; p++) {
                    int d;
                    if (*p >= '0' && *p <= '9') d = *p - '0';
                    else if (hex && *p >= 'a' && *p <= 'f') d = *p - 'a' + 10;
                    else if (hex && *p >= 'A' && *p <= 'F') d = *p - 'A' + 10;
                    else { c = 0xFFFD; break; }
                    c = c * (hex ? 16 : 10) + (unsigned long)d;
                    if (c > 0x10FFFF) { c = 0xFFFD; break; }
                }
                if (!c)
                    c = 0xFFFD;
            }
            if (!c) {
                *out++ = *in++;   /* not an entity we know: keep the & */
                continue;
            }
            /* As UTF-8: at most 4 bytes, and the entity was at least 4. */
            if (c < 0x80) {
                *out++ = (char)c;
            } else if (c < 0x800) {
                *out++ = (char)(0xC0 | (c >> 6));
                *out++ = (char)(0x80 | (c & 0x3F));
            } else if (c < 0x10000) {
                *out++ = (char)(0xE0 | (c >> 12));
                *out++ = (char)(0x80 | ((c >> 6) & 0x3F));
                *out++ = (char)(0x80 | (c & 0x3F));
            } else {
                *out++ = (char)(0xF0 | (c >> 18));
                *out++ = (char)(0x80 | ((c >> 12) & 0x3F));
                *out++ = (char)(0x80 | ((c >> 6) & 0x3F));
                *out++ = (char)(0x80 | (c & 0x3F));
            }
            in = semi + 1;
        }
    }
    /* Text runs up to the next '<', which must stay; values end in a NUL
     * already. */
    if (out < end)
        *out = 0;
    return out;
}

static int bind(owf_xml *x, const char *prefix, const char *uri, int depth)
{
    if (x->nbindings == x->capbindings) {
        int want = x->capbindings ? x->capbindings * 2 : 32;
        binding *b = realloc(x->bindings, (size_t)want * sizeof *b);
        if (!b)
            return 0;
        x->bindings = b;
        x->capbindings = want;
    }
    x->bindings[x->nbindings].prefix = prefix;
    x->bindings[x->nbindings].uri = uri;
    x->bindings[x->nbindings].depth = depth;
    x->nbindings++;
    return 1;
}

/* Splits "p:name" into its namespace URI and local name. */
static void resolve(owf_xml *x, char *qname, int attribute, const char **ns, const char **local)
{
    char *colon = strchr(qname, ':');
    const char *prefix = "";
    int i;

    if (colon) {
        *colon = 0;
        prefix = qname;
        *local = colon + 1;
    } else {
        *local = qname;
        if (attribute) {     /* attributes without a prefix have no namespace */
            *ns = "";
            return;
        }
    }
    if (colon && strcmp(prefix, "xml") == 0) {
        *ns = "http://www.w3.org/XML/1998/namespace";
        return;
    }
    for (i = x->nbindings - 1; i >= 0; i--)
        if (strcmp(x->bindings[i].prefix, prefix) == 0) {
            *ns = x->bindings[i].uri;
            return;
        }
    *ns = "";
}

static owf_xml_node *new_node(owf_xml *x, owf_xml_node *parent, int type)
{
    owf_xml_node *n = arena(x, sizeof *n);
    if (!n)
        return NULL;
    memset(n, 0, sizeof *n);
    n->type = type;
    n->parent = parent;
    if (parent) {
        if (parent->last)
            parent->last->next = n;
        else
            parent->first = n;
        parent->last = n;
    }
    return n;
}

static int add_text(owf_xml *x, owf_xml_node *parent, char *start, char *end)
{
    owf_xml_node *n;
    if (!parent || start == end)
        return 1;
    end = decode(start, end);
    n = new_node(x, parent, OWF_XML_TEXT);
    if (!n)
        return 0;
    n->name = start;
    n->length = (size_t)(end - start);
    return 1;
}

owf_xml *owf_xml_parse(const char *text, size_t length, int *error)
{
    owf_xml *x = calloc(1, sizeof *x);
    owf_xml_node *cur = NULL;
    char *p, *end;
    int depth = 0;

    *error = OWF_ERR_MEMORY;
    if (!x)
        return NULL;
    x->text = malloc(length + 1);
    if (!x->text || !bind(x, "", "", 0)) {
        owf_xml_free(x);
        return NULL;
    }
    memcpy(x->text, text, length);
    x->text[length] = 0;
    p = x->text;
    end = p + length;
    *error = OWF_ERR_CORRUPT;

    while (p < end) {
        char *lt = memchr(p, '<', (size_t)(end - p));
        if (!lt)
            lt = end;
        if (lt > p && !add_text(x, cur, p, lt))
            goto memory;
        p = lt;
        if (p >= end)
            break;
        if (!strncmp(p, "<?", 2)) {
            char *q = strstr(p + 2, "?>");
            if (!q)
                goto corrupt;
            p = q + 2;
        } else if (!strncmp(p, "<!--", 4)) {
            char *q = strstr(p + 4, "-->");
            if (!q)
                goto corrupt;
            p = q + 3;
        } else if (!strncmp(p, "<![CDATA[", 9)) {
            char *q = strstr(p + 9, "]]>");
            owf_xml_node *n;
            if (!q)
                goto corrupt;
            if (cur && q > p + 9) {
                n = new_node(x, cur, OWF_XML_TEXT);
                if (!n)
                    goto memory;
                *q = 0;
                n->name = p + 9;
                n->length = (size_t)(q - (p + 9));
            }
            p = q + 3;
        } else if (!strncmp(p, "<!", 2)) {
            /* A DOCTYPE: skipped, internal subset and all. */
            int brackets = 0;
            for (p += 2; p < end && (*p != '>' || brackets > 0); p++) {
                if (*p == '[') brackets++;
                else if (*p == ']') brackets--;
            }
            if (p >= end)
                goto corrupt;
            p++;
        } else if (p[1] == '/') {
            char *q = memchr(p, '>', (size_t)(end - p));
            if (!q || !cur)
                goto corrupt;
            while (x->nbindings > 1 && x->bindings[x->nbindings - 1].depth == depth)
                x->nbindings--;
            depth--;
            cur = cur->parent;
            p = q + 1;
            if (!cur)
                break;       /* the root element closed */
        } else {
            char *qname = p + 1, *c;
            owf_xml_node *n;
            owf_xml_attribute attrs[64];
            char *names[64];
            int nattrs = 0, i, empty = 0;

            for (c = qname; name_char(*c); c++)
                ;
            if (c == qname)
                goto corrupt;
            depth++;
            /* Attributes: name="value" or name='value'. */
            for (;;) {
                char *an, *av, quote, *close;
                while (space(*c))
                    *c++ = 0;
                if (*c == '>') {
                    *c++ = 0;
                    break;
                }
                if (c[0] == '/' && c[1] == '>') {
                    *c = 0;
                    c += 2;
                    empty = 1;
                    break;
                }
                if (!name_char(*c))
                    goto corrupt;
                an = c;
                while (name_char(*c))
                    c++;
                while (space(*c))
                    *c++ = 0;
                if (*c != '=')
                    goto corrupt;
                *c++ = 0;
                while (space(*c))
                    c++;
                quote = *c;
                if (quote != '"' && quote != '\'')
                    goto corrupt;
                av = ++c;
                close = strchr(av, quote);
                if (!close)
                    goto corrupt;
                *close = 0;
                decode(av, close);
                c = close + 1;
                if (!strncmp(an, "xmlns", 5) && (an[5] == 0 || an[5] == ':')) {
                    if (!bind(x, an[5] ? an + 6 : "", av, depth))
                        goto memory;
                } else if (nattrs < 64) {
                    names[nattrs] = an;
                    attrs[nattrs].value = av;
                    nattrs++;
                }
            }
            n = new_node(x, cur, OWF_XML_ELEMENT);
            if (!n)
                goto memory;
            resolve(x, qname, 0, &n->ns, &n->name);
            if (nattrs) {
                n->attrs = arena(x, (size_t)nattrs * sizeof *n->attrs);
                if (!n->attrs)
                    goto memory;
                for (i = 0; i < nattrs; i++) {
                    n->attrs[i].value = attrs[i].value;
                    resolve(x, names[i], 1, &n->attrs[i].ns, &n->attrs[i].name);
                }
                n->nattrs = nattrs;
            }
            if (!x->root)
                x->root = n;
            else if (!cur)
                goto corrupt;     /* a second root */
            p = c;
            if (empty) {
                while (x->nbindings > 1 && x->bindings[x->nbindings - 1].depth == depth)
                    x->nbindings--;
                depth--;
                if (!cur)
                    break;
            } else {
                cur = n;
            }
        }
    }
    if (!x->root || cur)
        goto corrupt;
    *error = OWF_OK;
    return x;

memory:
    *error = OWF_ERR_MEMORY;
corrupt:
    owf_xml_free(x);
    return NULL;
}

owf_xml_node *owf_xml_root(const owf_xml *x)
{
    return x->root;
}

int owf_xml_is(const owf_xml_node *n, const char *ns, const char *name)
{
    return n && n->type == OWF_XML_ELEMENT && strcmp(n->name, name) == 0 && strcmp(n->ns, ns) == 0;
}

const char *owf_xml_attr(const owf_xml_node *n, const char *ns, const char *name)
{
    int i;
    if (!n)
        return NULL;
    for (i = 0; i < n->nattrs; i++)
        if (strcmp(n->attrs[i].name, name) == 0 && strcmp(n->attrs[i].ns, ns) == 0)
            return n->attrs[i].value;
    return NULL;
}

owf_xml_node *owf_xml_child(const owf_xml_node *n, const char *ns, const char *name)
{
    owf_xml_node *c;
    if (!n)
        return NULL;
    for (c = n->first; c; c = c->next)
        if (owf_xml_is(c, ns, name))
            return c;
    return NULL;
}

owf_xml_node *owf_xml_next(const owf_xml_node *n, const char *ns, const char *name)
{
    owf_xml_node *c;
    if (!n)
        return NULL;
    for (c = n->next; c; c = c->next)
        if (owf_xml_is(c, ns, name))
            return c;
    return NULL;
}

/* ---- Values ---- */

int owf_parse_length(const char *s, int *twips)
{
    long long whole = 0, milli = 0, t;     /* 32-bit longs overflow past 1491in */
    long scale = 100;
    int negative = 0, digits = 0;

    if (!s)
        return 0;
    while (space(*s))
        s++;
    if (*s == '-') {
        negative = 1;
        s++;
    } else if (*s == '+') {
        s++;
    }
    while (*s >= '0' && *s <= '9') {
        if (whole < 1000000)
            whole = whole * 10 + (*s - '0');
        s++;
        digits++;
    }
    if (*s == '.') {
        s++;
        while (*s >= '0' && *s <= '9') {
            milli += (*s - '0') * scale;
            scale /= 10;
            s++;
            digits++;
        }
    }
    if (!digits)
        return 0;
    milli += whole * 1000;
    if (!strcmp(s, "in")) t = milli * 1440 / 1000;
    else if (!strcmp(s, "cm")) t = milli * 144 / 254;
    else if (!strcmp(s, "mm")) t = milli * 144 / 2540;
    else if (!strcmp(s, "pt")) t = milli * 20 / 1000;
    else if (!strcmp(s, "pc")) t = milli * 240 / 1000;
    else if (!strcmp(s, "px")) t = milli * 15 / 1000;
    else return 0;
    if (t > 1000000000)
        t = 1000000000;
    *twips = (int)(negative ? -t : t);
    return 1;
}

int owf_parse_colour(const char *s, unsigned long *colour)
{
    unsigned long c = 0;
    int i;
    if (!s)
        return 0;
    if (*s == '#')
        s++;
    for (i = 0; i < 6; i++) {
        int d;
        if (s[i] >= '0' && s[i] <= '9') d = s[i] - '0';
        else if (s[i] >= 'a' && s[i] <= 'f') d = s[i] - 'a' + 10;
        else if (s[i] >= 'A' && s[i] <= 'F') d = s[i] - 'A' + 10;
        else return 0;
        c = (c << 4) | (unsigned long)d;
    }
    if (s[6])
        return 0;
    *colour = c;
    return 1;
}

/* ---- Reading a big part as a stream ----
 * A sheet's cells can run to megabytes of XML; a tree of them would take
 * several times that. The scanner hands over one start tag, end tag or run
 * of text at a time instead, parsing in place in the caller's text, with
 * the same rules as the tree: namespaces resolved, entities decoded, no
 * DTDs. Its memory is the stack of open elements, so depth is capped. */

#define SCAN_MAX_DEPTH 256
#define SCAN_MAX_ATTRS 64

typedef struct {
    const char *ns, *name;
} scan_open;

struct owf_xml_scan {
    owf_xml x;               /* only its namespace bindings */
    char *p, *end;
    scan_open *open;
    int nopen, capopen;
    owf_xml_attribute attrs[SCAN_MAX_ATTRS];
    int pending_end;         /* an empty element's end comes next */
    int started, done, error;
};

owf_xml_scan *owf_xml_scan_new(char *text, size_t length)
{
    owf_xml_scan *s = calloc(1, sizeof *s);
    if (!s)
        return NULL;
    if (!bind(&s->x, "", "", 0)) {
        free(s);
        return NULL;
    }
    s->p = text;
    s->end = text + length;
    *s->end = 0;
    return s;
}

void owf_xml_scan_free(owf_xml_scan *s)
{
    if (!s)
        return;
    free(s->x.bindings);
    free(s->open);
    free(s);
}

int owf_xml_scan_error(const owf_xml_scan *s)
{
    return s->error;
}

static int scan_fail(owf_xml_scan *s, int error)
{
    s->error = error;
    s->done = 1;
    return -1;
}

/* Closes the innermost element: its bindings go, as the tree's do. */
static void scan_close(owf_xml_scan *s, owf_xml_event *e)
{
    int depth = s->nopen;
    e->type = OWF_XS_END;
    e->ns = s->open[depth - 1].ns;
    e->name = s->open[depth - 1].name;
    e->depth = depth;
    e->nattrs = 0;
    while (s->x.nbindings > 1 && s->x.bindings[s->x.nbindings - 1].depth == depth)
        s->x.nbindings--;
    s->nopen--;
    if (!s->nopen)
        s->done = 1;         /* the root element closed */
}

int owf_xml_scan_next(owf_xml_scan *s, owf_xml_event *e)
{
    memset(e, 0, sizeof *e);
    if (s->error)
        return -1;
    if (s->pending_end) {
        s->pending_end = 0;
        scan_close(s, e);
        return 1;
    }
    while (!s->done && s->p < s->end) {
        char *p = s->p, *lt = memchr(p, '<', (size_t)(s->end - p));
        if (!lt)
            lt = s->end;
        if (lt > p) {
            s->p = lt;
            if (!s->nopen)
                continue;    /* outside the root: only white space is allowed */
            e->type = OWF_XS_TEXT;
            e->text = p;
            e->length = (size_t)(decode(p, lt) - p);
            e->depth = s->nopen;
            return 1;
        }
        if (!strncmp(p, "<?", 2)) {
            char *q = strstr(p + 2, "?>");
            if (!q)
                return scan_fail(s, OWF_ERR_CORRUPT);
            s->p = q + 2;
        } else if (!strncmp(p, "<!--", 4)) {
            char *q = strstr(p + 4, "-->");
            if (!q)
                return scan_fail(s, OWF_ERR_CORRUPT);
            s->p = q + 3;
        } else if (!strncmp(p, "<![CDATA[", 9)) {
            char *q = strstr(p + 9, "]]>");
            if (!q)
                return scan_fail(s, OWF_ERR_CORRUPT);
            s->p = q + 3;
            if (s->nopen && q > p + 9) {
                e->type = OWF_XS_TEXT;
                e->text = p + 9;
                e->length = (size_t)(q - (p + 9));
                e->depth = s->nopen;
                return 1;
            }
        } else if (!strncmp(p, "<!", 2)) {
            int brackets = 0;
            for (p += 2; p < s->end && (*p != '>' || brackets > 0); p++) {
                if (*p == '[') brackets++;
                else if (*p == ']') brackets--;
            }
            if (p >= s->end)
                return scan_fail(s, OWF_ERR_CORRUPT);
            s->p = p + 1;
        } else if (p[1] == '/') {
            char *q = memchr(p, '>', (size_t)(s->end - p)), *c;
            const char *ns, *name;
            if (!q || !s->nopen)
                return scan_fail(s, OWF_ERR_CORRUPT);
            for (c = p + 2; name_char(*c); c++)
                ;
            while (c < q && space(*c))
                c++;
            if (c != q || c == p + 2)
                return scan_fail(s, OWF_ERR_CORRUPT);
            *q = 0;
            for (c = p + 2; name_char(*c); c++)
                ;
            *c = 0;
            resolve(&s->x, p + 2, 0, &ns, &name);
            if (strcmp(ns, s->open[s->nopen - 1].ns) || strcmp(name, s->open[s->nopen - 1].name))
                return scan_fail(s, OWF_ERR_CORRUPT);
            s->p = q + 1;
            scan_close(s, e);
            return 1;
        } else {
            char *qname = p + 1, *c;
            char *names[SCAN_MAX_ATTRS];
            int nattrs = 0, i, empty = 0, depth = s->nopen + 1;

            if (s->started && !s->nopen)
                return scan_fail(s, OWF_ERR_CORRUPT);    /* a second root */
            for (c = qname; name_char(*c); c++)
                ;
            if (c == qname)
                return scan_fail(s, OWF_ERR_CORRUPT);
            if (depth > SCAN_MAX_DEPTH)
                return scan_fail(s, OWF_ERR_CORRUPT);
            for (;;) {
                char *an, *av, quote, *close;
                while (space(*c))
                    *c++ = 0;
                if (*c == '>') {
                    *c++ = 0;
                    break;
                }
                if (c[0] == '/' && c[1] == '>') {
                    *c = 0;
                    c += 2;
                    empty = 1;
                    break;
                }
                if (!name_char(*c))
                    return scan_fail(s, OWF_ERR_CORRUPT);
                an = c;
                while (name_char(*c))
                    c++;
                while (space(*c))
                    *c++ = 0;
                if (*c != '=')
                    return scan_fail(s, OWF_ERR_CORRUPT);
                *c++ = 0;
                while (space(*c))
                    c++;
                quote = *c;
                if (quote != '"' && quote != '\'')
                    return scan_fail(s, OWF_ERR_CORRUPT);
                av = ++c;
                close = strchr(av, quote);
                if (!close)
                    return scan_fail(s, OWF_ERR_CORRUPT);
                *close = 0;
                decode(av, close);
                c = close + 1;
                if (!strncmp(an, "xmlns", 5) && (an[5] == 0 || an[5] == ':')) {
                    if (!bind(&s->x, an[5] ? an + 6 : "", av, depth))
                        return scan_fail(s, OWF_ERR_MEMORY);
                } else if (nattrs < SCAN_MAX_ATTRS) {
                    names[nattrs] = an;
                    s->attrs[nattrs].value = av;
                    nattrs++;
                }
            }
            if (s->nopen == s->capopen) {
                int want = s->capopen ? s->capopen * 2 : 16;
                scan_open *o = realloc(s->open, (size_t)want * sizeof *o);
                if (!o)
                    return scan_fail(s, OWF_ERR_MEMORY);
                s->open = o;
                s->capopen = want;
            }
            resolve(&s->x, qname, 0, &s->open[s->nopen].ns, &s->open[s->nopen].name);
            for (i = 0; i < nattrs; i++)
                resolve(&s->x, names[i], 1, &s->attrs[i].ns, &s->attrs[i].name);
            s->nopen = depth;
            s->started = 1;
            s->p = c;
            e->type = OWF_XS_START;
            e->ns = s->open[depth - 1].ns;
            e->name = s->open[depth - 1].name;
            e->attrs = s->attrs;
            e->nattrs = nattrs;
            e->depth = depth;
            e->empty = empty;
            s->pending_end = empty;
            return 1;
        }
    }
    if (s->nopen || !s->started)
        return scan_fail(s, OWF_ERR_CORRUPT);    /* cut short, or no element at all */
    s->done = 1;
    return 0;
}

const char *owf_xml_event_attr(const owf_xml_event *e, const char *ns, const char *name)
{
    int i;
    for (i = 0; i < e->nattrs; i++)
        if (strcmp(e->attrs[i].name, name) == 0 && strcmp(e->attrs[i].ns, ns) == 0)
            return e->attrs[i].value;
    return NULL;
}
