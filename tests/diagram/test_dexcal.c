/*
 * The Excalidraw writer (libowf/diagram/owf_dexcal.c) on the PC: shapes as rectangles, ellipses and diamonds
 * at their places with colours, dashes and rounded corners; labels as text bound inside them; a container's
 * label at its top; bare text; arrows along their points, bound to both ends, with their arrowheads, colour,
 * dashes and labels; a loose arrow; a hidden shape and its arrows left out; JSON escaping; the brackets
 * balanced; and no such page. Run under ASan and UBSan by make diagram-check. Leaves
 * build/host/test-dexcal.excalidraw for excalidraw.com to open.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <stdio.h>
#include <string.h>
#include "owf_diagram.h"

static int fails, checks;

#define CHECK(cond) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_N(got, want) do { long g_ = (long)(got), w_ = (long)(want); checks++; if (g_ != w_) { fails++; \
    printf("FAIL %s:%d: %s is %ld, want %ld\n", __FILE__, __LINE__, #got, g_, w_); } } while (0)

static owf_dcell *shape(owf_dpage *p, const char *id, const char *style, long x, long y, long w, long h,
                        const char *value)
{
    owf_dcell *c = owf_dpage_add(p, id, "1", OWF_DCELL_VERTEX);
    c->geo.flags = OWF_DGEO_HAS;
    c->geo.x = OWF_DNUM(x);
    c->geo.y = OWF_DNUM(y);
    c->geo.width = OWF_DNUM(w);
    c->geo.height = OWF_DNUM(h);
    owf_dcell_set(&c->style, style);
    if (value)
        owf_dcell_set(&c->value, value);
    return c;
}

static owf_dcell *edge(owf_dpage *p, const char *id, const char *from, const char *to, const char *style,
                       const char *value)
{
    owf_dcell *e = owf_dpage_add(p, id, "1", OWF_DCELL_EDGE);
    if (from) owf_dcell_set(&e->source, from);
    if (to) owf_dcell_set(&e->target, to);
    owf_dcell_set(&e->style, style);
    if (value) owf_dcell_set(&e->value, value);
    e->geo.flags = OWF_DGEO_HAS | OWF_DGEO_RELATIVE;
    return e;
}

static int count(const char *hay, size_t n, const char *needle)
{
    size_t k = strlen(needle), i;
    int got = 0;
    for (i = 0; i + k <= n; i++)
        if (!memcmp(hay + i, needle, k))
            got++;
    return got;
}

static const char *find(const char *hay, size_t n, const char *needle)
{
    size_t k = strlen(needle), i;
    for (i = 0; i + k <= n; i++)
        if (!memcmp(hay + i, needle, k))
            return hay + i;
    return NULL;
}

/* The element whose id is that, from its "{" to the end of its line. */
static int element_has(const char *s, size_t n, const char *id, const char *what)
{
    char key[32];
    const char *at, *end;
    snprintf(key, sizeof key, "{\"id\": \"%s\",", id);
    if (!(at = find(s, n, key)))
        return 0;
    end = strchr(at, '\n');
    return find(at, end ? (size_t)(end - at) : strlen(at), what) != NULL;
}

/* JSON that holds together: brackets matched outside strings, strings closed, no raw control characters. */
static int json_ok(const char *s, size_t n)
{
    char stack[64];
    int depth = 0, in = 0;
    size_t i;
    for (i = 0; i < n; i++) {
        char c = s[i];
        if (in) {
            if (c == '\\') i++;
            else if (c == '"') in = 0;
            else if ((unsigned char)c < 32) return 0;
        } else if (c == '"') in = 1;
        else if (c == '{' || c == '[') {
            if (depth == (int)sizeof stack) return 0;
            stack[depth++] = c;
        } else if (c == '}' || c == ']') {
            if (!depth || stack[--depth] != (c == '}' ? '{' : '[')) return 0;
        }
    }
    return !in && !depth;
}

static void save(const char *name, const unsigned char *data, size_t n)
{
    FILE *f = fopen(name, "wb");
    if (f) {
        fwrite(data, 1, n, f);
        fclose(f);
    }
}

static void build(owf_diagram *d)
{
    owf_dpage *p;
    owf_dcell *c;
    owf_diagram_init(d);
    p = owf_diagram_add_page(d, "p1", "Scene");
    shape(p, "start", "rounded=1;whiteSpace=wrap;html=1;fillColor=#dae8fc;strokeColor=#6c8ebf;fontSize=16;", 0, 0, 120,
          60, "Start &amp; <b>go</b>");                                   /* cell 2 (0 and 1 are the roots) */
    shape(p, "lane", "swimlane;html=1;", 200, 0, 300, 120, "Shop");       /* 3 */
    c = shape(p, "pay", "ellipse;html=1;dashed=1;", 20, 30, 60, 60, "Pay \"now\"\\");   /* 4 */
    owf_dcell_set(&c->parent, "lane");
    shape(p, "ask", "rhombus;fontColor=#ff0000;", 600, 0, 80, 80, "OK?\tyes");          /* 5 */
    shape(p, "note", "text;html=1;", 0, 200, 100, 20, "two<br>lines");                  /* 6 */
    shape(p, "gone", "rounded=0;", 0, 300, 10, 10, "Hidden");                           /* 7 */
    owf_dattrs_set(&owf_dpage_find(p, "gone")->extra, "visible", "0");
    shape(p, "blank", "rounded=0;", 700, 0, 50, 50, NULL);                              /* 8 */
    edge(p, "e1", "start", "pay", "endArrow=classic;html=1;", "go \"on\"");             /* 9 */
    edge(p, "e2", "pay", "ask", "dashed=1;startArrow=block;endArrow=none;strokeColor=#00aa00;", NULL);   /* 10 */
    c = shape(p, "lbl", "edgeLabel;html=1;", 0, 0, 0, 0, "riding");                    /* 11 */
    owf_dcell_set(&c->parent, "e2");
    c = edge(p, "e3", "ask", NULL, "", NULL);                                           /* 12: loose */
    c->geo.flags |= OWF_DGEO_TARGET_POINT;
    c->geo.target_point.x = OWF_DNUM(900);
    c->geo.target_point.y = OWF_DNUM(40);
    edge(p, "e4", "blank", "gone", "", "to hidden");                                     /* 13 */
}

static void test_scene(void)
{
    owf_diagram d;
    unsigned char *out = NULL;
    size_t n = 0;
    const char *s;
    build(&d);
    CHECK_N(owf_dexcal_write(&d, 0, &out, &n), OWF_OK);
    s = (const char *)out;
    save("build/host/test-dexcal.excalidraw", out, n);
    CHECK(n > 0 && s[n] == 0 && strlen(s) == n);
    CHECK(json_ok(s, n));
    CHECK(!strncmp(s, "{\n  \"type\": \"excalidraw\",\n  \"version\": 2,\n  \"source\": \"GFOpenDiag\",\n  \"elements\": [\n", 70));
    CHECK(find(s, n, "\n  ],\n  \"appState\": {\"viewBackgroundColor\": \"#ffffff\", \"gridSize\": null},\n  \"files\": {}\n}\n")
          != NULL);
    /* a rounded, filled rectangle with its label bound inside, at its size, and its arrow */
    CHECK(element_has(s, n, "s3", "\"type\": \"rectangle\", \"x\": 0, \"y\": 0, \"width\": 120, \"height\": 60,"));
    CHECK(element_has(s, n, "s3", "\"strokeColor\": \"#6c8ebf\", \"backgroundColor\": \"#dae8fc\""));
    CHECK(element_has(s, n, "s3", "\"roundness\": {\"type\": 3}"));
    CHECK(element_has(s, n, "s3", "\"roughness\": 0"));
    CHECK(element_has(s, n, "s3", "\"boundElements\": [{\"type\": \"text\", \"id\": \"t3\"}, {\"type\": \"arrow\", \"id\": \"a10\"}]}"));
    CHECK(element_has(s, n, "t3", "\"text\": \"Start & go\", \"originalText\": \"Start & go\", \"fontSize\": 16,"));
    CHECK(element_has(s, n, "t3", "\"verticalAlign\": \"middle\""));
    CHECK(element_has(s, n, "t3", "\"containerId\": \"s3\"}"));
    CHECK(element_has(s, n, "t3", "\"x\": 16, \"y\": 20, \"width\": 88, \"height\": 20,"));   /* centred */
    /* the swimlane: its title at the top; the shape in it where it is on the page */
    CHECK(element_has(s, n, "s4", "\"roundness\": null"));
    CHECK(element_has(s, n, "t4", "\"y\": 4,"));
    CHECK(element_has(s, n, "t4", "\"verticalAlign\": \"top\""));
    CHECK(element_has(s, n, "s5", "\"type\": \"ellipse\", \"x\": 220, \"y\": 30,"));
    CHECK(element_has(s, n, "s5", "\"strokeStyle\": \"dashed\""));
    CHECK(element_has(s, n, "t5", "\"text\": \"Pay \\\"now\\\"\\\\\","));          /* quotes and backslash escaped */
    CHECK(element_has(s, n, "s6", "\"type\": \"diamond\""));
    CHECK(element_has(s, n, "t6", "\"strokeColor\": \"#ff0000\""));               /* fontColor */
    CHECK(element_has(s, n, "t6", "\"text\": \"OK?\\tyes\""));
    /* bare text is a text element of its own, its <br> a line break */
    CHECK(element_has(s, n, "t7", "\"text\": \"two\\nlines\""));
    CHECK(element_has(s, n, "t7", "\"containerId\": null}"));
    CHECK(element_has(s, n, "t7", "\"height\": 30,"));                              /* two lines of 12 at 1.25 */
    CHECK(find(s, n, "\"id\": \"s7\"") == NULL && find(s, n, "Hidden") == NULL);
    CHECK(element_has(s, n, "s9", "\"boundElements\": []}"));                       /* its arrow went to the hidden one */
    CHECK(find(s, n, "\"id\": \"t9\"") == NULL);                                    /* no label, no text */
    /* arrows: along their points, bound to both ends, heads either way, label bound to the arrow */
    CHECK(element_has(s, n, "a10", "\"type\": \"arrow\", \"x\": 120,"));
    CHECK(element_has(s, n, "a10", "\"points\": [[0, 0], [100, "));
    CHECK(element_has(s, n, "a10", "\"startBinding\": {\"elementId\": \"s3\", \"focus\": 0, \"gap\": 1}, \"endBinding\": {\"elementId\": \"s5\""));
    CHECK(element_has(s, n, "a10", "\"startArrowhead\": null, \"endArrowhead\": \"arrow\""));
    CHECK(element_has(s, n, "a10", "\"boundElements\": [{\"type\": \"text\", \"id\": \"t10\"}]}"));
    CHECK(element_has(s, n, "t10", "\"text\": \"go \\\"on\\\"\""));
    CHECK(element_has(s, n, "t10", "\"containerId\": \"a10\"}"));
    CHECK(element_has(s, n, "a11", "\"strokeColor\": \"#00aa00\""));
    CHECK(element_has(s, n, "a11", "\"strokeStyle\": \"dashed\""));
    CHECK(element_has(s, n, "a11", "\"startArrowhead\": \"arrow\", \"endArrowhead\": null"));
    CHECK(element_has(s, n, "t11", "\"text\": \"riding\""));                       /* the label riding on it */
    CHECK(find(s, n, "\"id\": \"s12\"") == NULL && find(s, n, "\"id\": \"t12\"") == NULL);
    /* the loose one: bound at its start only, drawn to its loose end */
    CHECK(element_has(s, n, "a13", "\"x\": 680, \"y\": 40, \"width\": 220, \"height\": 0,"));
    CHECK(element_has(s, n, "a13", "\"points\": [[0, 0], [220, 0]]"));
    CHECK(element_has(s, n, "a13", "\"endBinding\": null"));
    CHECK(element_has(s, n, "s6", "{\"type\": \"arrow\", \"id\": \"a13\"}]}"));
    CHECK(find(s, n, "\"id\": \"a14\"") == NULL && find(s, n, "to hidden") == NULL);
    CHECK_N(count(s, n, "\"type\": \"arrow\", \"x\""), 3);
    CHECK_N(count(s, n, "\"type\": \"text\", \"x\""), 7);
    CHECK_N(count(s, n, "\n    {\"id\": "), 15);                                     /* one element a line */
    owf_free(out);
    CHECK_N(owf_dexcal_write(&d, 1, &out, &n), OWF_ERR_FORMAT);
    CHECK(out == NULL && n == 0);
    CHECK_N(owf_dexcal_write(&d, -1, &out, &n), OWF_ERR_FORMAT);
    owf_diagram_free(&d);
}

/* An empty page: a scene with no elements. A part pixel left of the page, and a control character. */
static void test_small(void)
{
    owf_diagram d;
    unsigned char *out = NULL;
    size_t n = 0;
    owf_dpage *p;
    owf_dcell *c;
    owf_diagram_init(&d);
    p = owf_diagram_add_page(&d, "e", NULL);
    CHECK_N(owf_dexcal_write(&d, 0, &out, &n), OWF_OK);
    CHECK(find((const char *)out, n, "\"elements\": [],\n") != NULL);
    CHECK(json_ok((const char *)out, n));
    owf_free(out);
    c = shape(p, "a", "rounded=0;", 0, 0, 1, 1, "a\001b");
    c->geo.x = -350;
    c->geo.y = 7;
    c->geo.width = 1250;
    CHECK_N(owf_dexcal_write(&d, 0, &out, &n), OWF_OK);
    CHECK(element_has((const char *)out, n, "s3", "\"x\": -3.5, \"y\": 0.07, \"width\": 12.5,"));
    CHECK(element_has((const char *)out, n, "t3", "\"text\": \"a\\u0001b\""));
    CHECK(json_ok((const char *)out, n));
    owf_free(out);
    owf_diagram_free(&d);
}

/* Bytes that aren't UTF-8 come out as U+FFFD and good UTF-8 as it is; a huge label's size is clamped, not
 * overflowed (UBSan in diagram-check watches the arithmetic). */
static void test_bytes(void)
{
    owf_diagram d;
    unsigned char *out = NULL;
    size_t n = 0;
    owf_dpage *p;
    static char big[30001];
    owf_diagram_init(&d);
    p = owf_diagram_add_page(&d, "e", NULL);
    /* é (C3 A9), a stray FF, a cut-short euro (E2 82), an overlong slash (C0 AF), a surrogate (ED A0 80), U+1F600 */
    shape(p, "a", "rounded=0;", 0, 0, 10, 10, "\303\251|\377|\342\202|\300\257|\355\240\200|\360\237\230\200");
    memset(big, 'x', sizeof big - 1);
    shape(p, "b", "rounded=0;fontSize=400;", 0, 0, 10, 10, big);
    CHECK_N(owf_dexcal_write(&d, 0, &out, &n), OWF_OK);
    CHECK(element_has((const char *)out, n, "t3", "\"text\": \"\303\251|\357\277\275|\357\277\275\357\277\275|"
                                              "\357\277\275\357\277\275|\357\277\275\357\277\275\357\277\275|\360\237\230\200\""));
    CHECK(element_has((const char *)out, n, "t4", "\"width\": 4400000, \"height\": 500,"));   /* 20000 x 400 x 0.55 */
    CHECK(json_ok((const char *)out, n));
    owf_free(out);
    owf_diagram_free(&d);
}

int main(void)
{
    test_scene();
    test_small();
    test_bytes();
    printf("diagram Excalidraw: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
