/*
 * GFOpenDiag's ARexx commands on the PC (src/diag/odg_rexx.c, opendiag_rexx.h): each command, its answer, its
 * return code and LASTERROR, files written and read back (.drawio, compressed, SVG), Undo through the port, and
 * the templates' quoting. diag.rexx runs the same commands in the lab, through the app's port. Files go in
 * build/host. Run under ASan and UBSan by make check.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "opendiag_rexx.h"
#include "opendiag_mind.h"

static int fails, checks;

#define CHECK(cond) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_N(got, want) do { long g_ = (long)(got), w_ = (long)(want); checks++; if (g_ != w_) { fails++; \
    printf("FAIL %s:%d: %s is %ld, want %ld (%s)\n", __FILE__, __LINE__, #got, g_, w_, rx.lasterror); } } while (0)
#define CHECK_S(got, want) do { const char *g_ = (got), *w_ = (want); checks++; if (strcmp(g_, w_) != 0) { \
    fails++; printf("FAIL %s:%d: %s is \"%s\", want \"%s\"\n", __FILE__, __LINE__, #got, g_, w_); } } while (0)

static odg_rexx rx;
static char res[512];

static int rx_do(const char *line) { return odg_rexx_command(&rx, line, res, sizeof res); }

/* a command that must work; its answer */
static const char *ok(const char *line)
{
    int rc = rx_do(line);
    if (rc != ODG_RX_OK) {
        fails++;
        printf("FAIL: %s gave %d: %s\n", line, rc, rx.lasterror);
    }
    checks++;
    return res;
}

static void test_edits(void)
{
    char a[24], b[24], e[24], line[128];
    CHECK_N(odg_rexx_init(&rx), OWF_OK);
    CHECK_S(ok("ISDIRTY"), "0");
    CHECK_S(ok("PAGES"), "1");
    CHECK_S(ok("CELLS"), "");
    snprintf(a, sizeof a, "%s", ok("ADDSHAPE 10 20 120 60 STYLE \"rounded=1;fillColor=#dae8fc;\" TEXT Start"));
    CHECK(a[0] != 0);
    snprintf(b, sizeof b, "%s", ok("ADDSHAPE X=300 Y=20 WIDTH=80.5 HEIGHT=80 STYLE=ellipse; TEXT=\"End here\""));
    CHECK(b[0] && strcmp(a, b));
    CHECK_S(ok("ISDIRTY"), "1");
    snprintf(line, sizeof line, "GETBOX %s", b);
    CHECK_S(ok(line), "300 20 80.5 80");
    snprintf(line, sizeof line, "GETVALUE %s", b);
    CHECK_S(ok(line), "End here");
    snprintf(line, sizeof line, "GETSTYLE %s fillColor", a);
    CHECK_S(ok(line), "#dae8fc");
    snprintf(line, sizeof line, "GETSTYLE %s", b);
    CHECK_S(ok(line), "ellipse;");

    /* a connector between them, then moving one: the connector follows */
    snprintf(line, sizeof line, "CONNECT %s %s STYLE \"edgeStyle=orthogonalEdgeStyle;\"", a, b);
    snprintf(e, sizeof e, "%s", ok(line));
    CHECK(e[0] != 0);
    snprintf(line, sizeof line, "CELLS");
    {
        char want[96];
        snprintf(want, sizeof want, "%s %s %s", a, b, e);
        CHECK_S(ok(line), want);
    }
    snprintf(line, sizeof line, "MOVE %s 0 -10", b);
    ok(line);
    snprintf(line, sizeof line, "GETBOX %s", b);
    CHECK_S(ok(line), "300 10 80.5 80");
    /* hits: the shape, the connector's line, nothing */
    snprintf(line, sizeof line, "HIT 40 40");
    CHECK_S(ok(line), a);
    CHECK_S(ok("HIT 900 900"), "");
    /* two at once by "a,b" */
    snprintf(line, sizeof line, "MOVE %s,%s 5 5", a, b);
    ok(line);
    snprintf(line, sizeof line, "GETBOX %s", a);
    CHECK_S(ok(line), "15 25 120 60");
    snprintf(line, sizeof line, "RESIZE %s 15 25 200 40", a);
    ok(line);
    snprintf(line, sizeof line, "GETBOX %s", a);
    CHECK_S(ok(line), "15 25 200 40");
    snprintf(line, sizeof line, "RESIZE %s 0 0 0 40", a);
    CHECK_N(rx_do(line), ODG_RX_ERROR);
    CHECK_S(rx.lasterror, "RESIZE: not a shape, or the size is not above 0");

    /* labels and styles */
    snprintf(line, sizeof line, "SETVALUE %s Begin *\"now*\"", a);
    ok(line);
    snprintf(line, sizeof line, "GETVALUE %s", a);
    CHECK_S(ok(line), "Begin *\"now*\"");                 /* /F: the rest of the line as it is */
    snprintf(line, sizeof line, "SETVALUE %s \"Begin\"", a);
    ok(line);
    snprintf(line, sizeof line, "GETVALUE %s", a);
    CHECK_S(ok(line), "Begin");
    snprintf(line, sizeof line, "STYLE %s,%s strokeColor #ff0000", a, b);
    ok(line);
    snprintf(line, sizeof line, "GETSTYLE %s strokeColor", b);
    CHECK_S(ok(line), "#ff0000");
    snprintf(line, sizeof line, "STYLE %s strokeColor", b);
    ok(line);                                            /* no VALUE: the key goes */
    snprintf(line, sizeof line, "GETSTYLE %s strokeColor", b);
    CHECK_S(ok(line), "");

    /* Undo and Redo through the port */
    ok("UNDO");
    snprintf(line, sizeof line, "GETSTYLE %s strokeColor", b);
    CHECK_S(ok(line), "#ff0000");
    ok("REDO");
    CHECK_S(ok(line), "");
    CHECK_N(rx_do("REDO"), ODG_RX_NOW);
    CHECK_S(rx.lasterror, "REDO: nothing to redo");

    /* the grid snaps what comes after */
    CHECK_S(ok("GRID"), "0");
    CHECK_S(ok("GRID 10"), "10");
    snprintf(line, sizeof line, "%s", ok("ADDSHAPE 13 27 20 20"));
    {
        char q[160];
        snprintf(q, sizeof q, "GETBOX %s", line);
        CHECK_S(ok(q), "10 30 20 20");
        snprintf(q, sizeof q, "DELETE %s", line);
        ok(q);
    }
    CHECK_N(rx_do("GRID -1"), ODG_RX_ERROR);
    ok("GRID 0");

    /* deleting a shape takes its connector */
    snprintf(line, sizeof line, "DELETE %s", b);
    ok(line);
    CHECK_S(ok("CELLS"), a);
    CHECK_N(rx_do("DELETE 1"), ODG_RX_ERROR);            /* the layer stays */
    CHECK_N(rx_do("DELETE nope"), ODG_RX_ERROR);
    ok("UNDO");
    {
        char want[96];
        snprintf(want, sizeof want, "%s %s %s", a, b, e);
        CHECK_S(ok("CELLS"), want);
    }
}

static void test_errors(void)
{
    CHECK_N(rx_do("FROB 1 2"), ODG_RX_FAIL);
    CHECK_S(rx.lasterror, "Unknown GFOpenDiag ARexx command FROB; HELP lists them");
    CHECK_N(rx_do("ADDSHAPE 10 20 30"), ODG_RX_ERROR);
    CHECK_S(rx.lasterror, "ADDSHAPE: HEIGHT is needed");
    CHECK_N(rx_do("ADDSHAPE 10 20 thirty 40"), ODG_RX_ERROR);
    CHECK_S(rx.lasterror, "ADDSHAPE: WIDTH must be a number, not \"thirty\"");
    CHECK_N(rx_do("ADDSHAPE 1 2 3 4 5"), ODG_RX_ERROR);
    CHECK_S(rx.lasterror, "ADDSHAPE: Too many arguments at \"5\"");
    CHECK_N(rx_do("CONNECT nope nada"), ODG_RX_ERROR);
    CHECK_S(rx.lasterror, "CONNECT: FROM and TO must be shapes on the page");
    CHECK_N(rx_do("GETVALUE nope"), ODG_RX_ERROR);
    CHECK_S(rx.lasterror, "GETVALUE: no cell nope on the page");
    CHECK_N(rx_do("GETBOX 1"), ODG_RX_ERROR);
    CHECK_N(rx_do("PAGE 2"), ODG_RX_ERROR);
    CHECK_S(rx.lasterror, "PAGE: there are pages 1 to 1");
    CHECK_N(rx_do("PAGE 1.5"), ODG_RX_ERROR);
    CHECK_S(ok("HELP ADDSHAPE"), "ADDSHAPE X/A/N,Y/A/N,WIDTH/A/N,HEIGHT/A/N,STYLE/K,TEXT/K");
    CHECK(strstr(ok("HELP"), "VERSION ACTIVATE") == res && strstr(res, " CONNECT ") && strstr(res, " REDO") &&
          strstr(res, " TOOL SELECT GETSELECTION "));
    CHECK_N(rx_do("HELP FROB"), ODG_RX_ERROR);
    CHECK_N(rx_do("SAVE"), ODG_RX_NOW);
    CHECK_S(rx.lasterror, "SAVE: the diagram has no file yet (SAVEAS names one)");
    CHECK_S(ok("LASTERROR"), "SAVE: the diagram has no file yet (SAVEAS names one)");
    CHECK_N(odg_rexx_command(&rx, NULL, res, sizeof res), ODG_RX_FAIL);
    CHECK_N(odg_rexx_command(&rx, "PAGES", NULL, 0), ODG_RX_OK);
}

static void test_files(void)
{
    char a[24], line[128];
    CHECK_N(odg_rexx_format("x.drawio"), ODG_FILE_DRAWIO);
    CHECK_N(odg_rexx_format("x.SVG"), ODG_FILE_SVG);
    CHECK_N(odg_rexx_format("x.png"), ODG_FILE_PNG);
    CHECK_N(odg_rexx_format("noext"), ODG_FILE_DRAWIO);
    ok("SAVEAS build/host/odg-rexx.drawio");
    CHECK_S(ok("GETFILE"), "build/host/odg-rexx.drawio");
    CHECK_S(ok("ISDIRTY"), "0");
    ok("EXPORT build/host/odg-rexx.svg");
    CHECK_S(ok("GETFILE"), "build/host/odg-rexx.drawio");  /* EXPORT keeps the diagram's file */
    ok("EXPORT build/host/odg-rexx-svg.out FORMAT SVG");
    CHECK_N(rx_do("EXPORT build/host/odg-rexx.png"), ODG_RX_ERROR);
    ok("EXPORT build/host/odg-rexx.pdf");                 /* every page as PDF */
    {
        FILE *f = fopen("build/host/odg-rexx.pdf", "rb");
        char head[9] = "";
        CHECK(f != NULL);
        if (f) {
            CHECK_N(fread(head, 1, 8, f), 8);
            fclose(f);
        }
        CHECK_S(head, "%PDF-1.4");
    }
    ok("EXPORT build/host/odg-rexx-pdf.out FORMAT pdf");
    CHECK_S(ok("GETFILE"), "build/host/odg-rexx.drawio");  /* EXPORT keeps the diagram's file */
    CHECK_N(rx_do("SAVEAS build/host/odg-rexx-as.pdf"), ODG_RX_ERROR);
    CHECK_S(rx.lasterror, "SAVEAS: a PDF doesn't open again as a diagram (EXPORT writes one; SAVEAS takes .drawio, .svg or .png)");
    CHECK_S(ok("GETFILE"), "build/host/odg-rexx.drawio");
    CHECK_N(rx_do("EXPORT build/host/x FORMAT DOC"), ODG_RX_ERROR);
    CHECK_S(rx.lasterror, "EXPORT: FORMAT is SVG, PDF, PNG, ILBM or DRAWIO, not DOC");

    /* OPEN won't drop unsaved changes without FORCE */
    snprintf(a, sizeof a, "%s", ok("ADDSHAPE 500 500 10 10"));
    CHECK_N(rx_do("OPEN build/host/odg-rexx.drawio"), ODG_RX_NOW);
    CHECK_N(rx_do("NEW"), ODG_RX_NOW);
    ok("OPEN build/host/odg-rexx.drawio FORCE");
    snprintf(line, sizeof line, "GETBOX %s", a);
    CHECK_N(rx_do(line), ODG_RX_ERROR);                   /* not in the saved file */
    CHECK_S(ok("ISDIRTY"), "0");
    CHECK(strlen(ok("CELLS")) > 0);
    /* compressed, and read back the same */
    {
        char before[512];
        snprintf(before, sizeof before, "%s", ok("CELLS"));
        ok("SAVE COMPRESSED");
        ok("OPEN build/host/odg-rexx.drawio");
        CHECK_S(ok("CELLS"), before);
        /* the SVG holds the whole diagram: it opens as the diagram */
        ok("OPEN build/host/odg-rexx.svg");
        CHECK_S(ok("CELLS"), before);
        CHECK_S(ok("GETFILE"), "build/host/odg-rexx.svg");
    }
    CHECK_N(rx_do("OPEN build/host/no-such.drawio"), ODG_RX_ERROR);
    CHECK_S(rx.lasterror, "OPEN: could not read build/host/no-such.drawio");
    {
        FILE *f = fopen("build/host/odg-rexx-bad.drawio", "wb");
        if (f) {
            fputs("<html>not a diagram</html>", f);
            fclose(f);
        }
        CHECK_N(rx_do("OPEN build/host/odg-rexx-bad.drawio"), ODG_RX_ERROR);
        CHECK(strstr(rx.lasterror, "is not a diagram GFOpenDiag reads") != NULL);
    }
    CHECK_N(rx_do("SAVEAS build/host/odg-rexx.png"), ODG_RX_ERROR);

    /* pages: each its own cells and Undo */
    CHECK_S(ok("ADDPAGE Second"), "2");
    CHECK_S(ok("PAGES"), "2");
    CHECK_S(ok("CELLS"), "");
    snprintf(a, sizeof a, "%s", ok("ADDSHAPE 0 0 10 10"));
    ok("PAGE 1");
    CHECK(strstr(ok("CELLS"), a) == NULL || strcmp(a, "2") == 0);
    ok("PAGE 2");
    CHECK_S(ok("CELLS"), a);
    CHECK_N(rx_do("UNDO"), ODG_RX_NOW);                   /* the page change began its Undo afresh */
    ok("SAVEAS build/host/odg-rexx-2.drawio");
    ok("NEW");
    CHECK_S(ok("PAGES"), "1");
    CHECK_S(ok("GETFILE"), "");
    ok("OPEN build/host/odg-rexx-2.drawio");
    CHECK_S(ok("PAGES"), "2");
    odg_rexx_free(&rx);
    remove("build/host/odg-rexx.drawio");
    remove("build/host/odg-rexx-2.drawio");
    remove("build/host/odg-rexx.svg");
    remove("build/host/odg-rexx-svg.out");
    remove("build/host/odg-rexx-bad.drawio");
}

static void test_layers(void)
{
    char a[24], line[96];
    CHECK_N(odg_rexx_init(&rx), OWF_OK);
    CHECK_N(rx_do("SHOWLAYER 1 OFF"), ODG_RX_OK);                     /* no drawing on a hidden or locked layer */
    CHECK_N(rx_do("ADDSHAPE 0 0 10 10"), ODG_RX_ERROR);
    CHECK_S(rx.lasterror, "ADDSHAPE: layer 1 is hidden (SHOWLAYER 1 shows it)");
    CHECK_N(rx_do("SHOWLAYER 1"), ODG_RX_OK);
    CHECK_N(rx_do("LOCKLAYER 1"), ODG_RX_OK);
    CHECK_N(rx_do("ADDSHAPE 0 0 10 10"), ODG_RX_ERROR);
    CHECK_S(rx.lasterror, "ADDSHAPE: layer 1 is locked (LOCKLAYER 1 OFF unlocks it)");
    CHECK_N(rx_do("CONNECT a b"), ODG_RX_ERROR);
    CHECK_S(rx.lasterror, "CONNECT: layer 1 is locked (LOCKLAYER 1 OFF unlocks it)");
    CHECK_N(rx_do("LOCKLAYER 1 OFF"), ODG_RX_OK);
    CHECK_S(ok("CELLS"), "");
    CHECK_S(ok("LAYERS"), "1");
    CHECK_S(ok("LAYER"), "1");
    CHECK_S(ok("ADDLAYER Notes"), "2");
    CHECK_S(ok("LAYERS"), "2");
    CHECK_S(ok("LAYER"), "2");                     /* the new one is current */
    CHECK_S(ok("GETLAYER 2"), "Notes");
    snprintf(a, sizeof a, "%s", ok("ADDSHAPE 10 10 40 20"));
    CHECK_S(ok("HIT 20 20"), a);
    CHECK_S(ok("SHOWLAYER 2 OFF"), "");
    CHECK_S(ok("HIT 20 20"), "");                  /* hidden: not hit */
    ok("SHOWLAYER 2");
    CHECK_S(ok("HIT 20 20"), a);
    ok("LOCKLAYER 1");
    snprintf(line, sizeof line, "GETSTYLE %s", "1");
    CHECK_S(ok(line), "locked=1;");
    ok("LOCKLAYER 1 OFF");
    CHECK_S(ok(line), "");
    ok("RENAMELAYER 1 \"Back ground\"");
    CHECK_S(ok("GETLAYER 1"), "Back ground");
    CHECK_S(ok("LAYER 1"), "1");
    CHECK_N(rx_do("LAYER 3"), ODG_RX_ERROR);
    CHECK_S(rx.lasterror, "LAYER: there are layers 1 to 2");
    CHECK_N(rx_do("LAYER 1.5"), ODG_RX_ERROR);
    CHECK_N(rx_do("GETLAYER 0"), ODG_RX_ERROR);
    CHECK_N(rx_do("SHOWLAYER 9"), ODG_RX_ERROR);
    CHECK_N(rx_do("RENAMELAYER 1"), ODG_RX_ERROR);
    CHECK_S(ok("HELP SHOWLAYER"), "SHOWLAYER NUMBER/A/N,OFF/S");
    ok("UNDO");                                     /* the rename */
    CHECK_S(ok("GETLAYER 1"), "");
    odg_rexx_free(&rx);
}

/* A line whose first words fill the argument room: refused, never written past it (Hollister's #193 review). */
static void test_long_lines(void)
{
    static char line[4200];
    char id[64];
    size_t n;
    CHECK_N(odg_rexx_init(&rx), OWF_OK);
    memcpy(line, "SETVALUE ", 9);
    memset(line + 9, 'a', 2100);
    strcpy(line + 9 + 2100, " hello world");
    CHECK_N(rx_do(line), ODG_RX_ERROR);
    CHECK(strstr(rx.lasterror, "too long") != NULL);
    memcpy(line, "SETVALUE ", 9);                 /* the id quoted, then more */
    line[9] = '"';
    memset(line + 10, 'b', 2100);
    strcpy(line + 10 + 2100, "\" \"quoted rest\"");
    CHECK_N(rx_do(line), ODG_RX_ERROR);
    CHECK(strstr(rx.lasterror, "too long") != NULL);
    CHECK_N(rx_do("ADDSHAPE 0 0 10 10 TEXT keep"), ODG_RX_OK);
    strcpy(id, res);
    n = (size_t)snprintf(line, sizeof line, "SETVALUE %s ", id);   /* a 4 KB label: refused, not cut */
    memset(line + n, 'c', sizeof line - n - 1);
    line[sizeof line - 1] = 0;
    CHECK_N(rx_do(line), ODG_RX_ERROR);
    CHECK(strstr(rx.lasterror, "too long") != NULL);
    line[n] = '"';                                /* the same quoted: refused too */
    memset(line + n + 1, 'c', 3000);
    strcpy(line + n + 3001, "\"");
    CHECK_N(rx_do(line), ODG_RX_ERROR);
    snprintf(line, sizeof line, "GETVALUE %s", id);
    CHECK_N(rx_do(line), ODG_RX_OK);
    CHECK(strcmp(res, "keep") == 0);                /* the label as it was */
    n = (size_t)snprintf(line, sizeof line, "SETVALUE %s ", id); /* a label that just fits is taken whole */
    memset(line + n, 'e', 2000);
    line[n + 2000] = 0;
    CHECK_N(rx_do(line), ODG_RX_OK);
    CHECK_N(strlen(owf_dpage_find(odg_page(&rx.doc), id)->value), 2000);
    CHECK_N(rx_do("SETVALUE \"\" text"), ODG_RX_ERROR);           /* a quoted empty id: no such cell */
    CHECK_N(rx_do("SETVALUE \"\""), ODG_RX_ERROR);
    CHECK_N(rx_do("GETVALUE \"\""), ODG_RX_ERROR);
    memcpy(line, "SETVALUE ", 9);                 /* an id of 2047 bytes, the room less its NUL, then text */
    memset(line + 9, 'd', 2047);
    strcpy(line + 9 + 2047, " text");
    CHECK_N(rx_do(line), ODG_RX_ERROR);
    odg_rexx_free(&rx);
}

/* MOVE, DELETE and STYLE take 64 ids; a 65th is refused, not dropped. */
static void test_many_ids(void)
{
    static char line[2048];
    char id[64];
    size_t at;
    int i;
    CHECK_N(odg_rexx_init(&rx), OWF_OK);
    CHECK_N(rx_do("ADDSHAPE 10 10 20 20 TEXT one"), ODG_RX_OK);
    strcpy(id, res);
    for (i = 0, at = 0; i < 65; i++) at += (size_t)snprintf(line + at, sizeof line - at, "%s%s", i ? "," : "MOVE ", id);
    strcpy(line + at, " 5 5");
    CHECK_N(rx_do(line), ODG_RX_ERROR);
    CHECK(strstr(rx.lasterror, "more than 64 ids") != NULL);
    for (i = 0, at = 0; i < 65; i++) at += (size_t)snprintf(line + at, sizeof line - at, "%s%s", i ? "," : "DELETE ", id);
    CHECK_N(rx_do(line), ODG_RX_ERROR);
    CHECK(strstr(rx.lasterror, "DELETE: more than 64 ids") != NULL);
    CHECK_N(rx_do("CELLS"), ODG_RX_OK);
    CHECK(strcmp(res, id) == 0);              /* nothing deleted */
    for (i = 0, at = 0; i < 64; i++) at += (size_t)snprintf(line + at, sizeof line - at, "%s%s", i ? "," : "MOVE ", id);
    strcpy(line + at, " 5 5");
    CHECK_N(rx_do(line), ODG_RX_OK);
    odg_rexx_free(&rx);
}

/* The mind-map commands: a map made, read, changed and undone through the port. */
static void test_map(void)
{
    char r[24], a[24], b[24], a1[24], line[128];
    CHECK_N(odg_rexx_init(&rx), OWF_OK);
    strcpy(r, ok("MAPNEW 100 200 The plan"));
    CHECK(r[0] != 0);
    snprintf(line, sizeof line, "MAPCHILD %s Alpha", r);
    strcpy(a, ok(line));
    snprintf(line, sizeof line, "MAPCHILD %s Beta", r);
    strcpy(b, ok(line));
    snprintf(line, sizeof line, "MAPCHILD %s One", a);
    strcpy(a1, ok(line));
    snprintf(line, sizeof line, "GETVALUE %s", r);
    CHECK_S(ok(line), "The plan");
    snprintf(line, sizeof line, "GETBOX %s", a);
    CHECK_S(ok(line), "260 185 100 40");
    snprintf(line, sizeof line, "MAPCHILDREN %s", r);
    {
        char want[64];
        snprintf(want, sizeof want, "%s %s", a, b);
        CHECK_S(ok(line), want);
    }
    snprintf(line, sizeof line, "MAPPARENT %s", a1); CHECK_S(ok(line), a);
    snprintf(line, sizeof line, "MAPPARENT %s", r); CHECK_S(ok(line), "");
    snprintf(line, sizeof line, "MAPROOT %s", a1); CHECK_S(ok(line), r);
    snprintf(line, sizeof line, "MAPSTEP %s down", a); CHECK_S(ok(line), b);
    snprintf(line, sizeof line, "MAPSTEP %s CHILD", a); CHECK_S(ok(line), a1);
    snprintf(line, sizeof line, "MAPSTEP %s UP", a); CHECK_S(ok(line), "");
    snprintf(line, sizeof line, "MAPSTEP %s SIDEWAYS", a);
    CHECK_N(rx_do(line), ODG_RX_ERROR);
    CHECK_S(rx.lasterror, "MAPSTEP: WAY is PARENT, CHILD, UP or DOWN");

    snprintf(line, sizeof line, "MAPSIBLING %s ABOVE Zero", a1);
    CHECK(ok(line)[0] != 0);
    snprintf(line, sizeof line, "MAPSIBLING %s No", r);
    CHECK_N(rx_do(line), ODG_RX_ERROR);
    CHECK_S(rx.lasterror, "MAPSIBLING: a map's root has no siblings");
    snprintf(line, sizeof line, "MAPMOVE %s UP", b); ok(line);
    snprintf(line, sizeof line, "MAPMOVE %s UP", b);
    CHECK_N(rx_do(line), ODG_RX_ERROR);
    CHECK_S(rx.lasterror, "MAPMOVE: it is at the end of its row already");
    snprintf(line, sizeof line, "MAPFOLD %s", a); ok(line);
    snprintf(line, sizeof line, "MAPFOLD %s", b);
    CHECK_N(rx_do(line), ODG_RX_ERROR);
    CHECK_S(rx.lasterror, "MAPFOLD: it has no children");
    snprintf(line, sizeof line, "MAPFOLD %s OFF", a); ok(line);

    /* Both sides: kept by the edits after it, until RIGHT. */
    snprintf(line, sizeof line, "MAPLAYOUT %s BOTH", r); ok(line);
    snprintf(line, sizeof line, "GETBOX %s", a);
    CHECK(strncmp(ok(line), "-40 ", 4) == 0);             /* Beta first now, so Alpha went left */
    snprintf(line, sizeof line, "MAPCHILD %s Gamma", b); ok(line);
    snprintf(line, sizeof line, "GETBOX %s", a);
    CHECK(strncmp(ok(line), "-40 ", 4) == 0);
    snprintf(line, sizeof line, "MAPLAYOUT %s RIGHT", r); ok(line);
    snprintf(line, sizeof line, "GETBOX %s", a);
    CHECK(strncmp(ok(line), "260 ", 4) == 0);

    snprintf(line, sizeof line, "MAPDELETE %s", a); ok(line);
    snprintf(line, sizeof line, "GETVALUE %s", a1);
    CHECK_N(rx_do(line), ODG_RX_ERROR);
    CHECK_N(rx_do("UNDO"), ODG_RX_OK);
    snprintf(line, sizeof line, "GETVALUE %s", a1);
    CHECK_S(ok(line), "One");
    CHECK_N(rx_do("MAPCHILD nosuch x"), ODG_RX_ERROR);
    CHECK_S(rx.lasterror, "MAPCHILD: no map node by that id");
    CHECK_N(rx_do("MAPPARENT nosuch"), ODG_RX_ERROR);
    CHECK_N(rx_do("MAPSTEP nosuch UP"), ODG_RX_ERROR);
    CHECK_S(rx.lasterror, "MAPSTEP: no map node nosuch");
    {                                                     /* more children than the answer holds: RC 5 */
        int i, made = 0;
        snprintf(line, sizeof line, "MAPCHILD %s Many", b);
        for (i = 0; i < 200; i++) made += rx_do(line) == ODG_RX_OK;
        CHECK_N(made, 200);
        snprintf(line, sizeof line, "MAPCHILDREN %s", b);
        CHECK_N(rx_do(line), ODG_RX_NOW);
        CHECK(strstr(rx.lasterror, "201 children") != NULL);
    }
    snprintf(line, sizeof line, "MAPLAYOUT %s", r);
    CHECK_N(rx_do(line), ODG_RX_OK);
    {                                                     /* past ODG_MIND_MAX nodes: MAPLAYOUT gives RC 5 */
        owf_dpage *p = odg_page(&rx.doc);
        const char *layer = owf_dpage_find(p, b)->parent;
        char id[24];
        int i;
        for (i = 0; i < ODG_MIND_MAX; i++) {
            owf_dcell *c, *e;
            snprintf(id, sizeof id, "big%d", i);
            c = owf_dpage_add(p, id, layer, OWF_DCELL_VERTEX);
            owf_dcell_set(&c->style, ODG_MIND_NODE_STYLE);
            layer = owf_dpage_find(p, b)->parent;     /* cells move on add */
            snprintf(id, sizeof id, "bige%d", i);
            e = owf_dpage_add(p, id, layer, OWF_DCELL_EDGE);
            owf_dcell_set(&e->style, ODG_MIND_EDGE_STYLE);
            owf_dcell_set(&e->source, a1);
            snprintf(id, sizeof id, "big%d", i);
            owf_dcell_set(&e->target, id);
            layer = owf_dpage_find(p, b)->parent;
        }
        snprintf(line, sizeof line, "MAPLAYOUT %s", r);
        CHECK_N(rx_do(line), ODG_RX_NOW);
        CHECK(strstr(rx.lasterror, "more than 512 nodes") != NULL);
        CHECK(owf_dpage_find(odg_page(&rx.doc), "big0") != NULL);
    }
    CHECK_N(rx_do("LOCKLAYER 1"), ODG_RX_OK);
    snprintf(line, sizeof line, "MAPCHILD %s x", a);
    CHECK_N(rx_do(line), ODG_RX_ERROR);
    CHECK_S(rx.lasterror, "MAPCHILD: not a map node that can be changed (hidden or locked?)");
    CHECK_N(rx_do("MAPNEW 0 0 Idea"), ODG_RX_ERROR);
    CHECK_S(rx.lasterror, "MAPNEW: layer 1 is locked (LOCKLAYER 1 OFF unlocks it)");
    odg_rexx_free(&rx);
}

/* MAPEXPORT and MAPIMPORT: a map out as each outline and in again; the format by name or FORMAT; errors. */
static const char *slurp(const char *name)
{
    static char buf[4096];
    FILE *f = fopen(name, "rb");
    size_t n = f ? fread(buf, 1, sizeof buf - 1, f) : 0;
    if (f) fclose(f);
    buf[n] = 0;
    return buf;
}

static void test_map_outline(void)
{
    char r[24], a[24], line[200], r2[24];
    CHECK_N(odg_rexx_init(&rx), OWF_OK);
    strcpy(r, ok("MAPNEW 100 200 Trip"));
    snprintf(line, sizeof line, "MAPCHILD %s Pack", r);
    strcpy(a, ok(line));
    snprintf(line, sizeof line, "MAPCHILD %s Socks", a);
    ok(line);
    snprintf(line, sizeof line, "MAPCHILD %s Tickets", r);
    ok(line);
    snprintf(line, sizeof line, "MAPFOLD %s", a);
    ok(line);

    snprintf(line, sizeof line, "MAPEXPORT %s build/host/odg-map.md", a);
    ok(line);
    CHECK_S(slurp("build/host/odg-map.md"), "# Trip\n\n- Pack\n  - Socks\n- Tickets\n");
    snprintf(line, sizeof line, "MAPEXPORT %s build/host/odg-map.txt FORMAT mm", r);
    ok(line);
    CHECK_S(slurp("build/host/odg-map.txt"), "<map version=\"1.0.1\">\n<node TEXT=\"Trip\">\n"
            " <node TEXT=\"Pack\" FOLDED=\"true\">\n  <node TEXT=\"Socks\"/>\n </node>\n"
            " <node TEXT=\"Tickets\"/>\n</node>\n</map>\n");
    snprintf(line, sizeof line, "MAPEXPORT %s build/host/odg-map.opml", r);
    ok(line);
    CHECK(strstr(slurp("build/host/odg-map.opml"), "<outline text=\"Socks\"/>") != NULL);

    strcpy(r2, ok("MAPIMPORT build/host/odg-map.txt 600 200"));   /* .txt read as FreeMind by its first bytes */
    snprintf(line, sizeof line, "MAPCHILDREN %s", r2);
    {
        char kids[64], first[24];
        const char *sp;
        strcpy(kids, ok(line));
        sp = strchr(kids, ' ');
        CHECK(sp != NULL);
        if (sp) {
            snprintf(first, sizeof first, "%.*s", (int)(sp - kids), kids);
            snprintf(line, sizeof line, "GETVALUE %s", first);
            CHECK_S(ok(line), "Pack");
            CHECK(odg_mind_folded(owf_dpage_find(odg_page(&rx.doc), first)));
        }
    }
    snprintf(line, sizeof line, "GETBOX %s", r2);
    CHECK(strncmp(ok(line), "600 200 ", 8) == 0);
    CHECK(ok("UNDO") != NULL);                            /* one step */
    CHECK(owf_dpage_find(odg_page(&rx.doc), r2) == NULL);
    strcpy(r2, ok("MAPIMPORT build/host/odg-map.md"));      /* no place: right of the page's drawing */
    {
        owf_dnum box[4];
        const owf_dcell *c = owf_dpage_find(odg_page(&rx.doc), r2);
        CHECK(c != NULL);
        if (c) {
            owf_dcell_box(odg_page(&rx.doc), owf_dpage_find(odg_page(&rx.doc), r), box);
            CHECK(c->geo.x > box[0]);
            snprintf(line, sizeof line, "GETVALUE %s", r2);
            CHECK_S(ok(line), "Trip");
        }
    }
    strcpy(r2, ok("MAPIMPORT build/host/odg-map.opml 0 900 FORMAT OPML"));
    CHECK(r2[0] != 0);

    CHECK_N(rx_do("MAPEXPORT nosuch build/host/odg-map.md"), ODG_RX_ERROR);
    CHECK_S(rx.lasterror, "MAPEXPORT: nosuch is not in a map (or its map loops back on itself)");
    snprintf(line, sizeof line, "MAPEXPORT %s build/host/odg-map.md FORMAT DOC", r);
    CHECK_N(rx_do(line), ODG_RX_ERROR);
    CHECK_S(rx.lasterror, "MAPEXPORT: FORMAT is MM, OPML or MD, not DOC");
    snprintf(line, sizeof line, "MAPEXPORT %s build/host/no/such/dir/x.md", r);
    CHECK_N(rx_do(line), ODG_RX_ERROR);
    CHECK_S(rx.lasterror, "MAPEXPORT: could not write build/host/no/such/dir/x.md");
    CHECK_N(rx_do("MAPIMPORT build/host/no-such-outline.mm"), ODG_RX_ERROR);
    CHECK_S(rx.lasterror, "MAPIMPORT: could not read build/host/no-such-outline.mm");
    {
        FILE *f = fopen("build/host/odg-map-words.md", "wb");
        if (f) { fputs("just words\n", f); fclose(f); }
    }
    CHECK_N(rx_do("MAPIMPORT build/host/odg-map-words.md"), ODG_RX_ERROR);
    CHECK(strstr(rx.lasterror, "is not an outline GFOpenDiag reads") != NULL);
    CHECK_N(rx_do("LOCKLAYER 1"), ODG_RX_OK);
    CHECK_N(rx_do("MAPIMPORT build/host/odg-map.md"), ODG_RX_ERROR);
    CHECK_S(rx.lasterror, "MAPIMPORT: layer 1 is locked (LOCKLAYER 1 OFF unlocks it)");
    odg_rexx_free(&rx);
}

/* ---- D6: the page as a picture (EXPORT to PNG and IFF ILBM through the app's drawer) ---- */

static int pic_calls;
static owf_dnum pic_box[4];
static int pic_w, pic_h, pic_fail;

/* stands in for the app: each pixel's colour from its place, so the file's pixels can be checked */
static unsigned char *fake_picture(void *user, const owf_dnum box[4], int width, int height)
{
    unsigned char *rgb;
    int x, y;
    (void)user;
    pic_calls++;
    memcpy(pic_box, box, sizeof pic_box);
    pic_w = width;
    pic_h = height;
    if (pic_fail) return NULL;
    rgb = (unsigned char *)malloc((size_t)width * (size_t)height * 3);
    if (!rgb) return NULL;
    for (y = 0; y < height; y++)
        for (x = 0; x < width; x++) {
            unsigned char *p = rgb + ((size_t)y * (size_t)width + (size_t)x) * 3;
            p[0] = (unsigned char)(x * 7);
            p[1] = (unsigned char)(y * 13);
            p[2] = (unsigned char)(x < width / 2 ? 0x5a : 0xc3);
        }
    return rgb;
}

static unsigned char *pic_read(const char *name, size_t *n)
{
    FILE *f = fopen(name, "rb");
    unsigned char *buf;
    long len;
    *n = 0;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = len > 0 ? (unsigned char *)malloc((size_t)len) : NULL;
    if (buf && fread(buf, 1, (size_t)len, f) != (size_t)len) { free(buf); buf = NULL; }
    fclose(f);
    if (buf) *n = (size_t)len;
    return buf;
}

static unsigned long be32(const unsigned char *p)
{
    return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16) | ((unsigned long)p[2] << 8) | p[3];
}

/* The ILBM's BMHD size and its BODY (24 planes, ByteRun1) back to RGB rows; the pixels that differ from
 * fake_picture's, or -1 when the file isn't such an ILBM. */
static long ilbm_differs(const unsigned char *f, size_t n, int *w, int *h)
{
    size_t at = 12, rowbytes, need;
    const unsigned char *body = NULL;
    size_t body_len = 0;
    long bad = 0;
    int x, y, c, b;
    *w = *h = 0;
    if (n < 12 || memcmp(f, "FORM", 4) || memcmp(f + 8, "ILBM", 4) || be32(f + 4) + 8 != n) return -1;
    while (at + 8 <= n) {
        unsigned long len = be32(f + at + 4);
        if (at + 8 + len > n) return -1;
        if (!memcmp(f + at, "BMHD", 4) && len >= 20) {
            *w = (f[at + 8] << 8) | f[at + 9];
            *h = (f[at + 10] << 8) | f[at + 11];
            if (f[at + 16] != 24 || f[at + 18] != 1) return -1;     /* 24 planes, ByteRun1 */
        } else if (!memcmp(f + at, "BODY", 4)) {
            body = f + at + 8;
            body_len = len;
        }
        at += 8 + len + (len & 1);
    }
    if (!body || *w < 1 || *h < 1) return -1;
    rowbytes = (size_t)((*w + 15) / 16) * 2;
    need = rowbytes * 24 * (size_t)*h;
    {
        unsigned char *planes = (unsigned char *)calloc(need, 1);
        size_t i = 0, o = 0;
        if (!planes) return -1;
        while (o < need && i < body_len) {
            signed char k = (signed char)body[i++];
            if (k >= 0) {
                size_t m = (size_t)k + 1;
                if (i + m > body_len || o + m > need) break;
                memcpy(planes + o, body + i, m);
                i += m;
                o += m;
            } else if (k != -128) {
                size_t m = (size_t)(1 - k);
                if (i >= body_len || o + m > need) break;
                memset(planes + o, body[i++], m);
                o += m;
            }
        }
        if (o != need) { free(planes); return -1; }
        for (y = 0; y < *h; y++)
            for (x = 0; x < *w; x++) {
                unsigned char got[3] = { 0, 0, 0 }, want[3];
                for (c = 0; c < 3; c++)
                    for (b = 0; b < 8; b++)
                        if (planes[((size_t)y * 24 + (size_t)(c * 8 + b)) * rowbytes + (size_t)(x >> 3)] &
                            (0x80 >> (x & 7)))
                            got[c] |= (unsigned char)(1 << b);
                want[0] = (unsigned char)(x * 7);
                want[1] = (unsigned char)(y * 13);
                want[2] = (unsigned char)(x < *w / 2 ? 0x5a : 0xc3);
                if (memcmp(got, want, 3)) bad++;
            }
        free(planes);
    }
    return bad;
}

static void test_pictures(void)
{
    char a[24], b[24], line[160], cells[256];
    unsigned char *f;
    size_t n;
    int w, h;
    CHECK_N(odg_rexx_init(&rx), OWF_OK);
    CHECK_N(odg_rexx_format("x.iff"), ODG_FILE_ILBM);
    CHECK_N(odg_rexx_format("x.ILBM"), ODG_FILE_ILBM);
    CHECK_N(odg_rexx_format("x.png"), ODG_FILE_PNG);

    /* no app to draw the page: PNG and ILBM are refused, nothing is written */
    remove("build/host/odg-pic.iff");
    snprintf(a, sizeof a, "%s", ok("ADDSHAPE 10 20 120 60 STYLE \"rounded=1;\" TEXT Start"));
    CHECK_N(rx_do("EXPORT build/host/odg-pic.iff"), ODG_RX_ERROR);
    CHECK_S(rx.lasterror, "build/host/odg-pic.iff: a picture needs the drawn page, which the app makes");
    f = pic_read("build/host/odg-pic.iff", &n);
    CHECK(f == NULL);
    free(f);

    /* the app's drawer: the page's bounds with a 10-pixel margin, at 100% */
    rx.picture = fake_picture;
    snprintf(b, sizeof b, "%s", ok("ADDSHAPE 200 40 80 40 STYLE ellipse; TEXT End"));
    snprintf(line, sizeof line, "CONNECT %s %s", a, b);
    ok(line);
    pic_calls = 0;
    ok("EXPORT build/host/odg-pic.iff");
    CHECK_N(pic_calls, 1);
    CHECK_N(pic_box[0], OWF_DNUM(0));
    CHECK_N(pic_box[1], OWF_DNUM(10));
    CHECK_N(pic_box[2], OWF_DNUM(290));
    CHECK_N(pic_box[3], OWF_DNUM(80));
    CHECK_N(pic_w, 290);
    CHECK_N(pic_h, 80);
    f = pic_read("build/host/odg-pic.iff", &n);
    CHECK(f != NULL);
    if (f) {
        CHECK_N(ilbm_differs(f, n, &w, &h), 0);          /* every pixel read back from the 24 planes */
        CHECK_N(w, 290);
        CHECK_N(h, 80);
        free(f);
    }
    ok("EXPORT build/host/odg-pic-iff.out FORMAT ILBM");
    f = pic_read("build/host/odg-pic-iff.out", &n);
    CHECK(f && n > 12 && !memcmp(f + 8, "ILBM", 4));
    free(f);
    CHECK_S(ok("GETFILE"), "");                        /* EXPORT names no file */

    /* PNG: the page drawn with the diagram inside, so it opens again as the same diagram */
    snprintf(cells, sizeof cells, "%s", ok("CELLS"));
    ok("EXPORT build/host/odg-pic.png");
    f = pic_read("build/host/odg-pic.png", &n);
    CHECK(f && n > 8 && !memcmp(f, "\x89PNG\r\n\x1a\n", 8));
    if (f && n > 24) {
        CHECK_N(be32(f + 16), 290);                    /* IHDR width and height */
        CHECK_N(be32(f + 20), 80);
    }
    free(f);
    ok("NEW FORCE");
    ok("OPEN build/host/odg-pic.png");
    CHECK_S(ok("CELLS"), cells);
    snprintf(line, sizeof line, "GETBOX %s", a);
    CHECK_S(ok(line), "10 20 120 60");
    snprintf(line, sizeof line, "GETVALUE %s", a);
    CHECK_S(ok(line), "Start");

    /* SAVEAS: a PNG opens again, so it can be the diagram's file; an ILBM can't */
    CHECK_N(rx_do("SAVEAS build/host/odg-pic-as.iff"), ODG_RX_ERROR);
    CHECK_S(rx.lasterror, "SAVEAS: an IFF picture doesn't open again as a diagram (EXPORT writes one; SAVEAS takes "
                          ".drawio, .svg or .png)");
    ok("SAVEAS build/host/odg-pic-as.png");
    CHECK_S(ok("GETFILE"), "build/host/odg-pic-as.png");
    CHECK_S(ok("ISDIRTY"), "0");

    /* the drawer failing is out of memory; an empty page and a page too big for a picture are errors */
    pic_fail = 1;
    CHECK_N(rx_do("EXPORT build/host/odg-pic2.iff"), ODG_RX_FAIL);
    pic_fail = 0;
    ok("NEW FORCE");
    pic_calls = 0;
    CHECK_N(rx_do("EXPORT build/host/odg-pic3.iff"), ODG_RX_ERROR);
    CHECK_S(rx.lasterror, "build/host/odg-pic3.iff: the page has nothing on it to draw");
    ok("ADDSHAPE 0 0 5000 10");
    CHECK_N(rx_do("EXPORT build/host/odg-pic3.png"), ODG_RX_ERROR);
    CHECK(strstr(rx.lasterror, "the page is 5020 by 30 pixels; a picture is 4096 at most") != NULL);
    CHECK_N(pic_calls, 0);
    odg_rexx_free(&rx);
}

int main(void)
{
    test_edits();
    test_errors();
    test_files();
    test_layers();
    test_long_lines();
    test_many_ids();
    test_map();
    test_map_outline();
    test_pictures();
    printf("diagram ARexx: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
