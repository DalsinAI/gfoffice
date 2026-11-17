/* GFOpenPressmaster: facing pages, two to a spread (src/pub/opp_spread.c). */
#include "openpressmaster_spread.h"

#include <stdio.h>
#include <string.h>

static int checks, failures;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_N(got, want) do { long g_ = (long)(got), w_ = (long)(want); checks++; if (g_ != w_) { failures++; \
    printf("FAIL %s:%d: %s is %ld, want %ld\n", __FILE__, __LINE__, #got, g_, w_); } } while (0)

/* A two-page master is two masters of one name; that makes the pages face. */
static void test_facing(void)
{
    owf_deck *d = owf_deck_new(11906, 16838);
    int before;
    CHECK(d != NULL);
    if (!d) return;
    CHECK_N(opp_spread_facing(NULL), 0);
    CHECK_N(opp_spread_facing(d), 0);
    before = d->nmasters;
    CHECK(owf_deck_add_master(d, "Master A") != NULL);
    CHECK_N(opp_spread_facing(d), 0);
    CHECK(owf_deck_add_master(d, "Master B") != NULL);
    CHECK_N(opp_spread_facing(d), 0);                  /* two one-page masters */
    CHECK(owf_deck_add_master(d, "Master A") != NULL);
    CHECK_N(d->nmasters, before + 3);
    CHECK_N(opp_spread_facing(d), 1);                  /* Master A's left and right page */
    owf_deck_free(d);
    d = owf_deck_new(11906, 16838);
    CHECK(d != NULL);
    if (!d) return;
    CHECK(owf_deck_add_master(d, "") != NULL);
    CHECK(owf_deck_add_master(d, "") != NULL);
    CHECK_N(opp_spread_facing(d), 0);                  /* no name is not one name */
    owf_deck_free(d);
}

/* Page 1 alone on the right; then 2-3, 4-5; an even count's last page alone on the left. */
static void test_pairs(void)
{
    int i;
    CHECK_N(opp_spread_side(0), OPP_SPREAD_RIGHT);
    CHECK_N(opp_spread_side(1), OPP_SPREAD_LEFT);
    CHECK_N(opp_spread_side(2), OPP_SPREAD_RIGHT);
    CHECK_N(opp_spread_side(3), OPP_SPREAD_LEFT);
    CHECK_N(opp_spread_side(-1), OPP_SPREAD_RIGHT);
    CHECK_N(opp_spread_partner(5, 0), -1);
    CHECK_N(opp_spread_partner(5, 1), 2);
    CHECK_N(opp_spread_partner(5, 2), 1);
    CHECK_N(opp_spread_partner(5, 3), 4);
    CHECK_N(opp_spread_partner(5, 4), 3);
    CHECK_N(opp_spread_partner(4, 3), -1);             /* the last page of four: a left page alone */
    CHECK_N(opp_spread_partner(1, 0), -1);
    CHECK_N(opp_spread_partner(2, 1), -1);
    CHECK_N(opp_spread_partner(5, 5), -1);
    CHECK_N(opp_spread_partner(5, -1), -1);
    CHECK_N(opp_spread_partner(0, 0), -1);
    CHECK_N(opp_spread_first(5, 0), 0);
    CHECK_N(opp_spread_first(5, 1), 1);
    CHECK_N(opp_spread_first(5, 2), 1);
    CHECK_N(opp_spread_first(5, 3), 3);
    CHECK_N(opp_spread_first(5, 4), 3);
    CHECK_N(opp_spread_first(4, 3), 3);
    CHECK_N(opp_spread_first(5, 5), -1);
    CHECK_N(opp_spread_first(5, -1), -1);
    /* Partners are each other's, on opposite sides, the left one first; every page is in one spread. */
    for (i = 0; i < 9; i++) {
        int p = opp_spread_partner(9, i), f = opp_spread_first(9, i);
        if (p >= 0) {
            CHECK_N(opp_spread_partner(9, p), i);
            CHECK(opp_spread_side(p) != opp_spread_side(i));
            CHECK_N(opp_spread_first(9, p), f);
            CHECK_N(opp_spread_side(f), OPP_SPREAD_LEFT);
            CHECK(f == i || f == p);
        } else {
            CHECK_N(f, i);
        }
    }
}

static void test_place(void)
{
    opp_spread_box l, r;
    /* A4 portrait pages in a wide view: the height limits. 400 high, 400 * 11906 / 16838 = 282 wide each,
     * centred across: (1000 - 564) / 2 = 218. */
    CHECK_N(opp_spread_place(1000, 400, 11906, 16838, &l, &r), 282);
    CHECK(l.x == 218 && l.y == 0 && l.w == 282 && l.h == 400);
    CHECK(r.x == 500 && r.y == 0 && r.w == 282 && r.h == 400);     /* touching at the spine */
    /* A narrow view: the width limits. 150 wide each, 150 * 16838 / 11906 = 212 high, centred down. */
    CHECK_N(opp_spread_place(300, 600, 11906, 16838, &l, &r), 150);
    CHECK(l.x == 0 && r.x == 150 && l.w == 150 && l.h == 212 && l.y == 194 && r.y == 194 && r.h == 212);
    /* An odd width leaves the odd pixel at the right. */
    CHECK_N(opp_spread_place(301, 600, 100, 100, &l, &r), 150);
    CHECK(l.x == 0 && r.x == 150 && r.x + r.w == 300);
    /* Hits. */
    CHECK_N(opp_spread_hit(&l, &r, 0, 225), OPP_SPREAD_LEFT);
    CHECK_N(opp_spread_hit(&l, &r, 149, 374), OPP_SPREAD_LEFT);
    CHECK_N(opp_spread_hit(&l, &r, 150, 225), OPP_SPREAD_RIGHT);
    CHECK_N(opp_spread_hit(&l, &r, 299, 300), OPP_SPREAD_RIGHT);
    CHECK_N(opp_spread_hit(&l, &r, 300, 300), -1);
    CHECK_N(opp_spread_hit(&l, &r, 10, 224), -1);
    CHECK_N(opp_spread_hit(&l, &r, 10, 375), -1);
    CHECK_N(opp_spread_hit(&l, NULL, 200, 300), -1);
    CHECK_N(opp_spread_hit(NULL, &r, 200, 300), OPP_SPREAD_RIGHT);
    /* No room, or nonsense: 0 and empty boxes. */
    l.w = r.w = 9;
    CHECK_N(opp_spread_place(14, 600, 100, 100, &l, &r), 0);
    CHECK(l.w == 0 && r.w == 0 && l.h == 0 && r.h == 0);
    CHECK_N(opp_spread_place(600, 7, 100, 100, &l, &r), 0);
    CHECK_N(opp_spread_place(0, 600, 100, 100, &l, &r), 0);
    CHECK_N(opp_spread_place(600, 600, 0, 100, &l, &r), 0);
    CHECK_N(opp_spread_place(600, 600, 100, -1, &l, &r), 0);
    CHECK_N(opp_spread_hit(&l, &r, 0, 0), -1);
    CHECK_N(opp_spread_place(600, 600, 100, 100, NULL, NULL), 300);
}

int main(void)
{
    test_facing();
    test_pairs();
    test_place();
    printf("test-pressmaster-spread: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
