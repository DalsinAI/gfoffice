/*
 * GFOpenDiag's ARexx commands on the PC (src/diag/odg_rexx.c, opendiag_rexx.h): each command, its answer, its
 * return code and LASTERROR, files written and read back (.drawio, compressed, SVG), Undo through the port, and
 * the templates' quoting. diag.rexx runs the same commands in the lab, through the app's port. Files go in
 * build/host. Run under ASan and UBSan by make check.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <stdio.h>
#include <string.h>
#include "opendiag_rexx.h"

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
    CHECK(strstr(ok("HELP"), "VERSION ACTIVATE") == res && strstr(res, " CONNECT ") && strstr(res, " REDO"));
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
    CHECK_N(rx_do("EXPORT build/host/x FORMAT PDF"), ODG_RX_ERROR);
    CHECK_S(rx.lasterror, "EXPORT: FORMAT is SVG or DRAWIO, not PDF");

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

/* A line whose first words fill the argument room: refused, never written past it (Hollister's #193 review). */
static void test_long_lines(void)
{
    static char line[4200];
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
    n = strlen("SETVALUE x ");                    /* a short id and a 4 KB label: cut to the room, not past it */
    memcpy(line, "SETVALUE x ", n);
    memset(line + n, 'c', sizeof line - n - 1);
    line[sizeof line - 1] = 0;
    CHECK(rx_do(line) != ODG_RX_OK);                /* no cell x; the point is it ends cleanly under ASan */
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

int main(void)
{
    test_edits();
    test_errors();
    test_files();
    test_long_lines();
    test_many_ids();
    printf("diagram ARexx: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
