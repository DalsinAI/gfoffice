/*
 * The Mermaid and DOT writers (libowf/diagram/owf_dgraph.c) on the PC: shapes as nodes with the nearest shape,
 * a container as a subgraph or cluster round its shapes, edges with labels (their own or a riding label),
 * dashes and arrowheads either way, colours, quotes and line breaks escaped, a loose connector and a hidden
 * shape left out, the page's name and direction, and no such page. Run under ASan and UBSan by make
 * diagram-check. Leaves build/host/test-dgraph.mmd and .dot for the real tools to read.
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

static int starts(const char *s, const char *prefix)
{
    return !strncmp(s, prefix, strlen(prefix));
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
    p = owf_diagram_add_page(d, "p1", "Order \"flow\"");
    /* wider than tall: left to right */
    shape(p, "start", "rounded=1;whiteSpace=wrap;html=1;fillColor=#dae8fc;strokeColor=#6c8ebf;", 0, 0, 120, 60,
          "Start &amp; <b>go</b>");
    shape(p, "lane", "swimlane;html=1;", 200, 0, 300, 120, "Shop");
    c = shape(p, "pay", "ellipse;html=1;", 20, 30, 60, 60, "Pay #1");
    owf_dcell_set(&c->parent, "lane");
    c = shape(p, "ship", "shape=hexagon;html=1;dashed=1;", 120, 30, 100, 40, "Ship<br>it");
    owf_dcell_set(&c->parent, "lane");
    shape(p, "ask", "rhombus;", 600, 0, 80, 80, "OK?");
    shape(p, "data", "shape=parallelogram;fontColor=#ff0000;", 700, 0, 80, 40, "Data");
    shape(p, "db", "shape=cylinder3;", 800, 0, 60, 80, "DB");
    shape(p, "sub", "shape=process;", 900, 0, 80, 40, "Sub");
    shape(p, "gone", "rounded=0;", 0, 100, 10, 10, "Hidden");
    owf_dattrs_set(&owf_dpage_find(p, "gone")->extra, "visible", "0");
    edge(p, "e1", "start", "pay", "endArrow=classic;html=1;", "pay \"now\"");
    edge(p, "e2", "pay", "ship", "dashed=1;endArrow=none;", NULL);
    c = shape(p, "lbl", "edgeLabel;html=1;", 0, 0, 0, 0, "riding");
    owf_dcell_set(&c->parent, "e2");
    edge(p, "e3", "ship", "ask", "startArrow=classic;endArrow=classic;strokeColor=#00aa00;", NULL);
    edge(p, "e4", "ask", "data", "startArrow=block;endArrow=none;", "back");
    edge(p, "e5", "lane", "db", "", NULL);
    edge(p, "e6", "db", NULL, "", "loose");
    edge(p, "e7", "sub", "gone", "", "to hidden");
}

static void test_mermaid(void)
{
    owf_diagram d;
    unsigned char *out = NULL;
    size_t n = 0;
    const char *s;
    build(&d);
    CHECK_N(owf_dmermaid_write(&d, 0, &out, &n), OWF_OK);
    s = (const char *)out;
    save("build/host/test-dgraph.mmd", out, n);
    CHECK(n > 0 && s[n] == 0 && strlen(s) == n);
    CHECK(starts(s, "---\ntitle: \"Order #quot;flow#quot;\"\n---\nflowchart LR\n"));
    CHECK(find(s, n, "    n1(\"Start & go\")\n") != NULL);              /* rounded; HTML taken out */
    CHECK(find(s, n, "    style n1 fill:#dae8fc,stroke:#6c8ebf\n") != NULL);
    CHECK(find(s, n, "    subgraph n2 [\"Shop\"]\n        n3((\"Pay #35;1\"))\n") != NULL);   /* round ellipse */
    CHECK(find(s, n, "        n4{{\"Ship<br>it\"}}\n        style n4 stroke-dasharray:5 5\n    end\n") != NULL);
    CHECK(find(s, n, "    n5{\"OK?\"}\n") != NULL);
    CHECK(find(s, n, "    n6[/\"Data\"/]\n    style n6 color:#ff0000\n") != NULL);
    CHECK(find(s, n, "    n7[(\"DB\")]\n") != NULL);
    CHECK(find(s, n, "    n8[[\"Sub\"]]\n") != NULL);
    CHECK(find(s, n, "Hidden") == NULL);
    CHECK(find(s, n, "    n1 -->|\"pay #quot;now#quot;\"| n3\n") != NULL);
    CHECK(find(s, n, "    n3 -.-|\"riding\"| n4\n") != NULL);           /* a riding label, dashed, no arrow */
    CHECK(find(s, n, "    n4 <--> n5\n    linkStyle 2 stroke:#00aa00\n") != NULL);   /* the third link */
    CHECK(find(s, n, "    n6 -->|\"back\"| n5\n") != NULL);             /* start arrow only: turned round */
    CHECK(find(s, n, "    n2 --> n7\n") != NULL);                       /* from the subgraph */
    CHECK(find(s, n, "loose") == NULL && find(s, n, "to hidden") == NULL);
    CHECK_N(count(s, n, "-->") + count(s, n, "<-->") + count(s, n, "-.-"), 6);
    owf_free(out);
    CHECK_N(owf_dmermaid_write(&d, 1, &out, &n), OWF_ERR_FORMAT);
    CHECK(out == NULL && n == 0);
    CHECK_N(owf_dmermaid_write(&d, -1, &out, &n), OWF_ERR_FORMAT);
    owf_diagram_free(&d);
}

static void test_dot(void)
{
    owf_diagram d;
    unsigned char *out = NULL;
    size_t n = 0;
    const char *s;
    build(&d);
    CHECK_N(owf_ddot_write(&d, 0, &out, &n), OWF_OK);
    s = (const char *)out;
    save("build/host/test-dgraph.dot", out, n);
    CHECK(starts(s, "digraph \"Order \\\"flow\\\"\" {\n    rankdir=LR;\n    compound=true;\n"
                    "    graph [fontname=\"Helvetica\"];\n"));
    CHECK(find(s, n, "    n1 [shape=box, label=\"Start & go\", style=\"filled,rounded\", fillcolor=\"#dae8fc\", "
                     "color=\"#6c8ebf\"];\n") != NULL);
    CHECK(find(s, n, "    subgraph cluster_n2 {\n        label=\"Shop\";\n        n3 [shape=circle, label=\"Pay #1\"];\n")
          != NULL);
    CHECK(find(s, n, "        n4 [shape=hexagon, label=\"Ship\\nit\", style=\"dashed\"];\n    }\n") != NULL);
    CHECK(find(s, n, "    n5 [shape=diamond, label=\"OK?\"];\n") != NULL);
    CHECK(find(s, n, "    n6 [shape=parallelogram, label=\"Data\", fontcolor=\"#ff0000\"];\n") != NULL);
    CHECK(find(s, n, "    n7 [shape=cylinder, label=\"DB\"];\n") != NULL);
    CHECK(find(s, n, "Hidden") == NULL);
    CHECK(find(s, n, "    n1 -> n3 [label=\"pay \\\"now\\\"\"];\n") != NULL);
    CHECK(find(s, n, "    n3 -> n4 [label=\"riding\", style=dashed, dir=none];\n") != NULL);
    CHECK(find(s, n, "    n4 -> n5 [dir=both, color=\"#00aa00\"];\n") != NULL);
    CHECK(find(s, n, "    n5 -> n6 [label=\"back\", dir=back];\n") != NULL);
    CHECK(find(s, n, "    n3 -> n7 [ltail=cluster_n2];\n") != NULL);   /* from the cluster, by its first shape */
    CHECK(find(s, n, "loose") == NULL && find(s, n, "to hidden") == NULL);
    CHECK(n > 2 && !strcmp(s + n - 2, "}\n"));
    owf_free(out);
    CHECK_N(owf_ddot_write(&d, 3, &out, &n), OWF_ERR_FORMAT);
    owf_diagram_free(&d);

    owf_diagram_init(&d);                /* an empty, unnamed page: a graph with nothing in it, top to bottom */
    owf_diagram_add_page(&d, "e", NULL);
    CHECK_N(owf_ddot_write(&d, 0, &out, &n), OWF_OK);
    CHECK(starts((const char *)out, "digraph \"Diagram\" {\n    rankdir=TB;\n"));
    owf_free(out);
    CHECK_N(owf_dmermaid_write(&d, 0, &out, &n), OWF_OK);
    CHECK(!strcmp((const char *)out, "flowchart TD\n"));
    owf_free(out);
    owf_diagram_free(&d);
}

/* Shapes that are each other's parent (a file can say so): no hang, every shape written once, the loop cut at
 * the top, and an edge to them still drawn. */
static void test_loops(void)
{
    owf_diagram d;
    owf_dpage *p;
    owf_dcell *c;
    unsigned char *out = NULL;
    size_t n = 0;
    int k, i;
    owf_diagram_init(&d);
    p = owf_diagram_add_page(&d, "p", "Loop");
    c = shape(p, "A", "rounded=0;", 0, 0, 40, 40, "Ay");
    owf_dcell_set(&c->parent, "B");
    c = shape(p, "B", "rounded=0;", 0, 0, 40, 40, "Bee");
    owf_dcell_set(&c->parent, "A");
    c = shape(p, "S", "rounded=0;", 0, 0, 40, 40, "Self");
    owf_dcell_set(&c->parent, "S");
    shape(p, "C", "rounded=0;", 100, 0, 40, 40, "Sea");
    edge(p, "e", "C", "A", "", NULL);
    edge(p, "f", "C", "S", "", NULL);
    for (i = 0; i < 70; i++) {            /* a chain of containers deeper than the cut */
        char id[16], parent[16];
        snprintf(id, sizeof id, "d%d", i);
        snprintf(parent, sizeof parent, i ? "d%d" : "1", i - 1);
        c = shape(p, id, "swimlane;", 0, 0, 10, 10, "deep");
        owf_dcell_set(&c->parent, parent);
    }
    for (k = 0; k < 2; k++) {
        const char *s;
        CHECK_N((k ? owf_ddot_write : owf_dmermaid_write)(&d, 0, &out, &n), OWF_OK);
        s = (const char *)out;
        CHECK_N(count(s, n, "\"Ay\""), 1);
        CHECK_N(count(s, n, "\"Bee\""), 1);
        CHECK_N(count(s, n, "\"Self\""), 1);
        CHECK_N(count(s, n, "\"deep\""), 70);
        CHECK(find(s, n, k ? "n4 -> " : "n4 --> ") != NULL);   /* C's edges are there */
        owf_free(out);
    }
    owf_diagram_free(&d);
}

int main(void)
{
    test_loops();
    test_mermaid();
    test_dot();
    printf("diagram graphs: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
