/*
 * GFOpenDiag's shapes palette on the PC (src/diag/odg_stencil.c): the first eight keep their numbers, every name is
 * its own, each style is one the shapes' outlines or the old drawing code draws and names back to draw.io's word,
 * and the sets hold every shape once. Run under ASan and UBSan by make check.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <stdio.h>
#include <string.h>
#include "opendiag_stencil.h"
#include "owf_diagram.h"

static int fails, checks;

#define CHECK(cond) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_N(got, want) do { long g_ = (long)(got), w_ = (long)(want); checks++; if (g_ != w_) { fails++; \
    printf("FAIL %s:%d: %s is %ld, want %ld\n", __FILE__, __LINE__, #got, g_, w_); } } while (0)
#define CHECK_S(got, want) do { const char *g_ = (got), *w_ = (want); checks++; if (!g_ || strcmp(g_, w_) != 0) { \
    fails++; printf("FAIL %s:%d: %s is \"%s\", want \"%s\"\n", __FILE__, __LINE__, #got, g_ ? g_ : "(null)", w_); } } while (0)

/* The words the old drawing code (odg_view.c, owf_dsvg.c, owf_dpdf.c) draws without an outline. */
static int old_shape(const char *style)
{
    static const char *const words[] = { "rhombus", "ellipse", "triangle", "text", "shape=parallelogram",
                                         "shape=process", "shape=hexagon" };
    int i;
    for (i = 0; i < (int)(sizeof words / sizeof words[0]); i++)
        if (!strncmp(style, words[i], strlen(words[i])))
            return 1;
    return !strncmp(style, "rounded=", 8) || !strncmp(style, "whiteSpace=", 11);
}

static void test_first_eight(void)
{
    static const char *const names[] = { "Process", "Decision", "Terminator", "Data", "Predefined Process",
                                         "Preparation", "Triangle", "Circle" };
    int i;
    for (i = 0; i < 8; i++) {
        CHECK_S(odg_stencil_at(i)->name, names[i]);
        CHECK_N(odg_stencil_at(i)->set, ODG_SET_FLOWCHART);
    }
    CHECK_N(odg_stencil_find("predefined process"), 4);
    CHECK(odg_stencil_at(-1) == NULL);
    CHECK(odg_stencil_at(odg_stencil_count()) == NULL);
}

static void test_every_shape(void)
{
    owf_dnum box[4] = { 0, 0, OWF_DNUM(100), OWF_DNUM(60) };
    static owf_dshape s;
    int i, j, outlines = 0;
    CHECK(odg_stencil_count() >= 40);
    for (i = 0; i < odg_stencil_count(); i++) {
        const odg_stencil *st = odg_stencil_at(i);
        const char *word = owf_dshape_name(st->style);
        checks++;
        if (!st->style[0] || strstr(st->style, ";;") || st->width <= 0 || st->height <= 0 || st->set < 0 ||
            st->set >= ODG_NSETS) {
            fails++;
            printf("FAIL: %s: style \"%s\", %d by %d, set %d\n", st->name, st->style, st->width, st->height, st->set);
        }
        CHECK_N(odg_stencil_find(st->name), i);           /* its own name, and no other shape's (any case) */
        for (j = 0; j < i; j++) {
            checks++;
            if (!strcmp(odg_stencil_at(j)->name, st->name)) {
                fails++;
                printf("FAIL: %s twice\n", st->name);
            }
        }
        checks++;
        if (word) {                                       /* drawn by its outline, its word draw.io's */
            char want[80];
            outlines++;
            snprintf(want, sizeof want, "shape=%s;", word);
            int turned = !strcmp(word, "triangle");      /* "triangle;direction=..." is the word, no shape= */
            if (owf_dshape_outline(st->style, box, &s) != 1 ||
                (!turned && (!strstr(st->style, want) || !owf_dshape_name(want) ||
                             strcmp(owf_dshape_name(want), word)))) {
                fails++;
                printf("FAIL: %s (%s) doesn't map both ways to %s\n", st->name, st->style, word);
            }
        } else if (!old_shape(st->style)) {
            fails++;
            printf("FAIL: %s (%s) is drawn by nothing\n", st->name, st->style);
        }
    }
    CHECK(outlines >= 30);
}

static void test_sets(void)
{
    int set, nth, total = 0;
    CHECK_S(odg_stencil_set_name(ODG_SET_FLOWCHART), "Flowchart");
    CHECK_S(odg_stencil_set_name(ODG_SET_BASIC), "Basic");
    CHECK(odg_stencil_set_name(ODG_NSETS) == NULL);
    for (set = 0; set < ODG_NSETS; set++) {
        int last = -1;
        for (nth = 0; odg_stencil_in_set((odg_stencil_set)set, nth) >= 0; nth++) {
            int i = odg_stencil_in_set((odg_stencil_set)set, nth);
            CHECK(i > last);                              /* in the palette's order */
            CHECK_N(odg_stencil_at(i)->set, set);
            last = i;
        }
        CHECK(nth > 10);
        total += nth;
    }
    CHECK_N(total, odg_stencil_count());                  /* every shape in one set */
    CHECK_N(odg_stencil_in_set(ODG_SET_BASIC, 0), odg_stencil_find("Rectangle"));
    CHECK_N(odg_stencil_in_set(ODG_SET_BASIC, -1), -1);
    CHECK_N(odg_stencil_in_set(ODG_NSETS, 0), -1);
    CHECK_N(odg_stencil_find("Manual Operation") >= 0 &&
            strstr(odg_stencil_at(odg_stencil_find("Manual Operation"))->style, "flipV=1") != NULL, 1);
}

int main(void)
{
    test_first_eight();
    test_every_shape();
    test_sets();
    printf("diagram stencil: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
