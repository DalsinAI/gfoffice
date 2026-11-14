/*
 * owf_doutline.c: a mind map's tree as an outline, read and written (owf_diagram.h, "Outlines"): FreeMind's .mm
 * (a map element holding node elements with TEXT and FOLDED), OPML 2.0 (outline elements with text under body)
 * and a Markdown outline ("#" headings and nested list items). Written from the formats' public descriptions;
 * no code taken from anywhere.
 * GFOpenDiag step D5 (amicradle-teams designs/GFOPENDIAG-20261113.md).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "owf_diag_int.h"

#include <stdlib.h>
#include <string.h>

/* Depths 0 to 60: so that a tree written as OPML (opml, body, then an outline a level) still nests no deeper than
 * libowf's XML reader takes (64); deeper items are put at 60. */
#define MAX_DEPTH 61

typedef struct {
    owf_ditem *items;
    int n, room;
    int bad;                             /* memory ran out */
} list;

static char *copy(const char *s, size_t n)
{
    char *d = (char *)owf_malloc(n + 1);
    if (d) {
        memcpy(d, s, n);
        d[n] = 0;
    }
    return d;
}

/* An item added, its text copied (n bytes) with spaces trimmed off both ends; its depth at most one under the
 * item before. */
static void add(list *l, int depth, const char *text, size_t n, int folded)
{
    owf_ditem *it;
    while (n && (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n')) text++, n--;
    while (n && (text[n - 1] == ' ' || text[n - 1] == '\t' || text[n - 1] == '\r' || text[n - 1] == '\n')) n--;
    if (l->bad)
        return;
    if (l->n == l->room) {
        int room = l->room ? l->room * 2 : 16;
        owf_ditem *more = (owf_ditem *)owf_malloc((size_t)room * sizeof *more);
        if (!more) {
            l->bad = 1;
            return;
        }
        if (l->n)
            memcpy(more, l->items, (size_t)l->n * sizeof *more);
        owf_free(l->items);
        l->items = more;
        l->room = room;
    }
    if (depth > MAX_DEPTH - 1)
        depth = MAX_DEPTH - 1;
    if (l->n == 0)
        depth = 0;
    else if (depth > l->items[l->n - 1].depth + 1)
        depth = l->items[l->n - 1].depth + 1;
    if (depth < 0)
        depth = 0;
    it = &l->items[l->n];
    it->depth = depth;
    it->folded = folded;
    if (!(it->text = copy(text, n))) {
        l->bad = 1;
        return;
    }
    l->n++;
}

void owf_outline_free(owf_ditem *items, int n)
{
    int i;
    for (i = 0; i < n; i++)
        owf_free(items[i].text);
    owf_free(items);
}

/* One root: when the list has several items at depth 0 (or none came first), a root named title goes on top. */
static int one_root(list *l, const char *title)
{
    int i, tops = 0;
    for (i = 0; i < l->n; i++)
        tops += l->items[i].depth == 0;
    if (tops <= 1)
        return OWF_OK;
    for (i = 0; i < l->n; i++)
        if (l->items[i].depth < MAX_DEPTH - 1)
            l->items[i].depth++;
    add(l, 0, "", 0, 0);                 /* room for one more, then moved to the front */
    if (l->bad)
        return OWF_ERR_MEMORY;
    {
        owf_ditem root = l->items[l->n - 1];
        owf_free(root.text);
        title = title && *title ? title : "Outline";
        if (!(root.text = copy(title, strlen(title)))) {
            l->n--;
            return OWF_ERR_MEMORY;
        }
        memmove(&l->items[1], &l->items[0], (size_t)(l->n - 1) * sizeof *l->items);
        l->items[0] = root;
    }
    return OWF_OK;
}

/* ---- Reading ---- */

static int ends_with(const char *name, const char *end)
{
    size_t n = strlen(name), e = strlen(end), i;
    if (n < e)
        return 0;
    for (i = 0; i < e; i++) {
        char c = name[n - e + i];
        if (c >= 'A' && c <= 'Z')
            c = (char)(c - 'A' + 'a');
        if (c != end[i])
            return 0;
    }
    return 1;
}

static const char *find(const char *text, size_t length, const char *what)
{
    size_t n = strlen(what), i;
    for (i = 0; i + n <= length; i++)
        if (!memcmp(text + i, what, n))
            return text + i;
    return NULL;
}

owf_outline_kind owf_outline_guess(const char *name, const char *text, size_t length)
{
    size_t i = 0;
    if (name && ends_with(name, ".mm"))
        return OWF_OUTLINE_MM;
    if (name && ends_with(name, ".opml"))
        return OWF_OUTLINE_OPML;
    if (name && (ends_with(name, ".md") || ends_with(name, ".markdown")))
        return OWF_OUTLINE_MD;
    if (!text)
        return OWF_OUTLINE_MD;
    if (length >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF)
        i = 3;
    while (i < length && (text[i] == ' ' || text[i] == '\t' || text[i] == '\r' || text[i] == '\n')) i++;
    if (length - i >= 4 && !memcmp(text + i, "<map", 4))
        return OWF_OUTLINE_MM;
    if (length - i >= 1 && text[i] == '<' && find(text + i, length - i > 512 ? 512 : length - i, "<opml"))
        return OWF_OUTLINE_OPML;
    if (length - i >= 5 && !memcmp(text + i, "<?xml", 5) && find(text + i, length - i > 512 ? 512 : length - i, "<map"))
        return OWF_OUTLINE_MM;
    return OWF_OUTLINE_MD;
}

/* The text under n, tags taken out, runs of white space made one space (FreeMind's richcontent). */
static void text_under(const owf_xml_node *n, owf_buf *b)
{
    const owf_xml_node *k;
    for (k = n->first; k; k = k->next) {
        if (k->type == OWF_XML_TEXT) {
            size_t i;
            for (i = 0; i < k->length; i++) {
                char c = k->name[i];
                if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                    if (b->length && b->data[b->length - 1] != ' ')
                        owf_buf_putc(b, ' ');
                } else
                    owf_buf_putc(b, c);
            }
        } else {
            if ((owf_xml_is(k, "", "p") || owf_xml_is(k, "", "br") || owf_xml_is(k, "", "div")) && b->length &&
                b->data[b->length - 1] != ' ')
                owf_buf_putc(b, ' ');
            text_under(k, b);
        }
    }
}

/* FreeMind: node elements, nested; the text in TEXT, else in a richcontent of TYPE NODE. */
static void read_mm(list *l, const owf_xml_node *n, int depth)
{
    const owf_xml_node *k;
    for (k = owf_xml_child(n, "", "node"); k; k = owf_xml_next(k, "", "node")) {
        const char *text = owf_xml_attr(k, "", "TEXT"), *folded = owf_xml_attr(k, "", "FOLDED");
        if (text)
            add(l, depth, text, strlen(text), folded && !strcmp(folded, "true"));
        else {
            const owf_xml_node *rc;
            owf_buf b;
            owf_buf_init(&b);
            for (rc = owf_xml_child(k, "", "richcontent"); rc; rc = owf_xml_next(rc, "", "richcontent")) {
                const char *type = owf_xml_attr(rc, "", "TYPE");
                if (!type || !strcmp(type, "NODE")) {
                    text_under(rc, &b);
                    break;
                }
            }
            if (b.failed)
                l->bad = 1;
            add(l, depth, b.data ? (const char *)b.data : "", b.length, folded && !strcmp(folded, "true"));
            owf_buf_free(&b);
        }
        read_mm(l, k, depth + 1);
    }
}

static void read_opml(list *l, const owf_xml_node *n, int depth)
{
    const owf_xml_node *k;
    for (k = owf_xml_child(n, "", "outline"); k; k = owf_xml_next(k, "", "outline")) {
        const char *text = owf_xml_attr(k, "", "text");
        if (!text)
            text = owf_xml_attr(k, "", "title");
        if (!text)
            text = "";
        add(l, depth, text, strlen(text), 0);
        read_opml(l, k, depth + 1);
    }
}

/* Markdown: "#" to "######" headings, and list items under the last heading by their indent (a tab is four
 * spaces); an item's indent picks the deepest open level it is past. Other lines are left out. */
static void read_md(list *l, const char *text, size_t length)
{
    int indent[MAX_DEPTH], levels = 0, base = 0;
    size_t at = 0;
    while (at < length) {
        size_t end = at, i;
        int col = 0;
        while (end < length && text[end] != '\n') end++;
        i = at;
        while (i < end && (text[i] == ' ' || text[i] == '\t')) {
            col += text[i] == '\t' ? 4 - col % 4 : 1;
            i++;
        }
        if (i < end && text[i] == '#' && col < 4) {
            int h = 0;
            while (i < end && text[i] == '#') h++, i++;
            if (h <= 6 && (i == end || text[i] == ' ' || text[i] == '\t')) {
                size_t e = end, k;
                while (e > i && (text[e - 1] == ' ' || text[e - 1] == '\t' || text[e - 1] == '\r')) e--;
                for (k = e; k > i && text[k - 1] == '#'; k--)
                    ;
                if (k < e && (text[k - 1] == ' ' || text[k - 1] == '\t'))   /* a closing run: "# Title ##" */
                    e = k;                       /* text[i] is a space here, so k - 1 >= i */
                add(l, h - 1, text + i, e - i, 0);
                base = l->n ? l->items[l->n - 1].depth + 1 : 0;
                levels = 0;
            }
        } else if (i < end) {
            size_t m = i;
            int item = 0;
            if ((text[m] == '-' || text[m] == '*' || text[m] == '+') && m + 1 < end && (text[m + 1] == ' ' || text[m + 1] == '\t'))
                item = 1, m += 2;
            else {
                while (m < end && text[m] >= '0' && text[m] <= '9') m++;
                if (m > i && m - i <= 9 && m + 1 < end && (text[m] == '.' || text[m] == ')') &&
                    (text[m + 1] == ' ' || text[m + 1] == '\t'))
                    item = 1, m += 2;
            }
            if (item) {
                int level;
                while (levels > 0 && col < indent[levels - 1]) levels--;
                if (levels > 0 && col == indent[levels - 1])
                    level = levels - 1;
                else {
                    if (levels < MAX_DEPTH)
                        indent[levels++] = col;
                    level = levels - 1;
                }
                add(l, base + level, text + m, end - m, 0);
            }
        }
        at = end + 1;
    }
}

int owf_outline_read(const char *text, size_t length, owf_outline_kind kind, const char *title, owf_ditem **items,
                     int *n)
{
    list l = { NULL, 0, 0, 0 };
    char *owned = NULL;                  /* OPML's head title */
    int r = OWF_OK;
    *items = NULL;
    *n = 0;
    if (kind == OWF_OUTLINE_MD)
        read_md(&l, text, length);
    else {
        int error = OWF_OK;
        owf_xml *x = owf_xml_parse(text, length, &error);
        const owf_xml_node *root = x ? owf_xml_root(x) : NULL;
        if (!x)
            return error == OWF_ERR_MEMORY ? OWF_ERR_MEMORY : OWF_ERR_CORRUPT;
        if (kind == OWF_OUTLINE_MM && root && owf_xml_is(root, "", "map"))
            read_mm(&l, root, 0);
        else if (kind == OWF_OUTLINE_OPML && root && owf_xml_is(root, "", "opml")) {
            const owf_xml_node *body = owf_xml_child(root, "", "body");
            if (!title || !*title) {
                const owf_xml_node *head = owf_xml_child(root, "", "head"), *t = head ? owf_xml_child(head, "", "title") : NULL;
                owf_buf b;
                owf_buf_init(&b);
                if (t)
                    text_under(t, &b);
                if (b.failed || (b.length && !(owned = copy((const char *)b.data, b.length))))
                    l.bad = 1;
                title = owned;
                owf_buf_free(&b);
            }
            if (body)
                read_opml(&l, body, 0);
        } else
            r = OWF_ERR_FORMAT;
        owf_xml_free(x);
    }
    if (r == OWF_OK && l.bad)
        r = OWF_ERR_MEMORY;
    if (r == OWF_OK && l.n == 0)
        r = OWF_ERR_FORMAT;
    if (r == OWF_OK)
        r = one_root(&l, title);
    owf_free(owned);
    if (r != OWF_OK) {
        owf_outline_free(l.items, l.n);
        return r;
    }
    *items = l.items;
    *n = l.n;
    return OWF_OK;
}

/* ---- Writing ---- */

/* Text on one line: line breaks and tabs made spaces. */
static void put_line(owf_buf *b, const char *s)
{
    for (; *s; s++)
        owf_buf_putc(b, *s == '\n' || *s == '\r' || *s == '\t' ? ' ' : *s);
}

/* An item's depth as written: at most 60, as the reader takes, so a file written always opens again. */
#define DEPTH(i) (items[i].depth < MAX_DEPTH - 1 ? items[i].depth : MAX_DEPTH - 1)

int owf_outline_write(const owf_ditem *items, int n, owf_outline_kind kind, const char *title, char **data,
                      size_t *length)
{
    owf_buf b;
    int i, d;
    unsigned char *out;
    size_t len;
    *data = NULL;
    if (length)
        *length = 0;
    if (n < 1 || items[0].depth != 0)
        return OWF_ERR_FORMAT;
    for (i = 1; i < n; i++)
        if (items[i].depth < 1 || items[i].depth > items[i - 1].depth + 1)
            return OWF_ERR_FORMAT;
    owf_buf_init(&b);
    if (kind == OWF_OUTLINE_MD) {
        const char *root = items[0].text ? items[0].text : "";
        size_t rn = strlen(root);
        owf_buf_puts(&b, "# ");
        put_line(&b, root);
        if (rn && root[rn - 1] == '#')
            owf_buf_puts(&b, " #");          /* a closing run, so "C #" reads back with its own # */
        owf_buf_puts(&b, "\n\n");
        for (i = 1; i < n; i++) {
            for (d = 1; d < DEPTH(i); d++)
                owf_buf_puts(&b, "  ");
            owf_buf_puts(&b, "- ");
            put_line(&b, items[i].text ? items[i].text : "");
            owf_buf_putc(&b, '\n');
        }
    } else {
        const char *tag = kind == OWF_OUTLINE_MM ? "node" : "outline";
        if (kind == OWF_OUTLINE_MM)
            owf_buf_puts(&b, "<map version=\"1.0.1\">\n");
        else {
            owf_buf_puts(&b, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<opml version=\"2.0\">\n<head>\n<title>");
            owf_diag_put_value(&b, title ? title : items[0].text ? items[0].text : "");
            owf_buf_puts(&b, "</title>\n</head>\n<body>\n");
        }
        for (i = 0; i < n; i++) {
            int next = i + 1 < n ? DEPTH(i + 1) : -1;
            for (d = 0; d < DEPTH(i); d++)
                owf_buf_putc(&b, ' ');
            owf_buf_printf(&b, "<%s %s=\"", tag, kind == OWF_OUTLINE_MM ? "TEXT" : "text");
            owf_diag_put_value(&b, items[i].text ? items[i].text : "");
            owf_buf_putc(&b, '"');
            if (kind == OWF_OUTLINE_MM && items[i].folded && next > DEPTH(i))
                owf_buf_puts(&b, " FOLDED=\"true\"");
            if (next > DEPTH(i)) {
                owf_buf_puts(&b, ">\n");
                continue;
            }
            owf_buf_puts(&b, "/>\n");
            for (d = DEPTH(i) - 1; d >= (next < 0 ? 0 : next); d--) {   /* the open ones this ends */
                int s;
                for (s = 0; s < d; s++)
                    owf_buf_putc(&b, ' ');
                owf_buf_printf(&b, "</%s>\n", tag);
            }
        }
        owf_buf_puts(&b, kind == OWF_OUTLINE_MM ? "</map>\n" : "</body>\n</opml>\n");
    }
    if (owf_buf_take(&b, &out, &len) != OWF_OK)     /* it frees b when it fails */
        return OWF_ERR_MEMORY;
    *data = (char *)out;
    if (length)
        *length = len;
    return OWF_OK;
}
