/*
 * GFOpenDiag's mouse on the PC (src/diag/odg_edit.c, opendiag_edit.h): the selection, the lasso, moving, resizing
 * by a handle (inside a group too), drawing shapes and joining them with connectors, as the app's canvas hands it
 * presses, drags and releases in the view's pixels. Each place below was worked out by hand. Run under ASan and
 * UBSan by make check.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <stdio.h>
#include <string.h>
#include "opendiag_edit.h"

static int fails, checks;

#define CHECK(cond) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_N(got, want) do { long g_ = (long)(got), w_ = (long)(want); checks++; if (g_ != w_) { fails++; \
    printf("FAIL %s:%d: %s is %ld, want %ld\n", __FILE__, __LINE__, #got, g_, w_); } } while (0)
#define CHECK_S(got, want) do { const char *g_ = (got), *w_ = (want); checks++; if (!g_ || strcmp(g_, w_)) { fails++; \
    printf("FAIL %s:%d: %s is \"%s\", want \"%s\"\n", __FILE__, __LINE__, #got, g_ ? g_ : "(null)", w_); } } while (0)
#define PX(n) OWF_DNUM(n)

static owf_diagram dg;
static odg_doc doc;
static odg_view v;
static odg_edit e;

static void fresh(int grid)
{
    owf_dpage *p;
    owf_diagram_init(&dg);
    p = owf_diagram_add_page(&dg, "p", "Page-1");
    if (grid) {
        owf_dattrs_set(&p->model, "grid", "1");
        owf_dattrs_set(&p->model, "gridSize", "10");
    }
    odg_init(&doc, &dg, 0);
    odg_view_init(&v, 400, 300);
    odg_edit_init(&e);
}

static void done(void)
{
    odg_free(&doc);
    owf_diagram_free(&dg);
}

static owf_dcell *cell(const char *id)
{
    return owf_dpage_find(odg_page(&doc), id);
}

/* a press, a drag through the middle and a release, as the canvas sends them */
static int drag(long x0, long y0, long x1, long y1, int shift)
{
    odg_edit_press(&e, &v, &doc, x0, y0, shift);
    odg_edit_motion(&e, (x0 + x1) / 2, (y0 + y1) / 2);
    odg_edit_motion(&e, x1, y1);
    return odg_edit_release(&e, &v, &doc, x1, y1);
}

static void test_tools(void)
{
    CHECK_S(odg_tool_name(ODG_TOOL_SELECT), "Select");
    CHECK_S(odg_tool_name(ODG_TOOL_CONNECTOR), "Connector");
    CHECK_S(odg_tool_name(ODG_NTOOLS), "");
    CHECK_S(odg_tool_style(ODG_TOOL_BOX), "rounded=0;whiteSpace=wrap;html=1;");
    CHECK_S(odg_tool_style(ODG_TOOL_ELLIPSE), "ellipse;whiteSpace=wrap;html=1;");
    CHECK_S(odg_tool_style(ODG_TOOL_RHOMBUS), "rhombus;whiteSpace=wrap;html=1;");
    CHECK(strstr(odg_tool_style(ODG_TOOL_CONNECTOR), "edgeStyle=orthogonalEdgeStyle") != NULL);
    CHECK_S(odg_tool_style((odg_tool)-1), "");
}

static void test_selection(void)
{
    char a[24], b[24], longid[ODG_EDIT_ID + 8];
    int i;
    fresh(0);
    odg_add_shape(&doc, PX(10), PX(10), PX(40), PX(20), "", NULL, a);
    odg_add_shape(&doc, PX(100), PX(10), PX(40), PX(20), "", NULL, b);
    CHECK(odg_edit_select(&e, a, 0));
    CHECK(odg_edit_has(&e, a));
    CHECK(!odg_edit_has(&e, b));
    CHECK(odg_edit_select(&e, b, 1));
    CHECK_N(e.n, 2);
    CHECK(odg_edit_select(&e, a, 1));          /* already there: kept once */
    CHECK_N(e.n, 2);
    CHECK(odg_edit_select(&e, b, 0));          /* not adding: the only one */
    CHECK_N(e.n, 1);
    CHECK_S(e.ids[0], b);
    odg_edit_select(&e, a, 1);
    odg_edit_drop(&e, b);
    CHECK_N(e.n, 1);
    CHECK_S(e.list[0], a);
    odg_edit_show(&e, &v);
    CHECK_N(v.nselected, 1);
    CHECK(v.selected == e.list);
    odg_edit_clear(&e);
    odg_edit_show(&e, &v);
    CHECK_N(v.nselected, 0);
    CHECK(v.selected == NULL);

    memset(longid, 'x', sizeof longid - 1);    /* an id too long to hold: refused, not cut */
    longid[sizeof longid - 1] = 0;
    CHECK(!odg_edit_select(&e, longid, 0));
    CHECK_N(e.n, 0);
    for (i = 0; i < ODG_EDIT_MAX; i++) {
        char id[16];
        snprintf(id, sizeof id, "c%d", i);
        CHECK_N(odg_edit_select(&e, id, 1), 1);
    }
    CHECK(!odg_edit_select(&e, "one-more", 1));
    CHECK_N(e.n, ODG_EDIT_MAX);
    odg_edit_prune(&e, &doc);                  /* none of c0 .. c63 is on the page */
    CHECK_N(e.n, 0);

    CHECK_N(odg_edit_select_all(&e, &doc), 2);
    CHECK_S(e.ids[0], a);
    CHECK_S(e.ids[1], b);
    odg_edit_select(&e, "1", 1);               /* the layer is not a cell to keep selected */
    odg_edit_prune(&e, &doc);
    CHECK_N(e.n, 2);
    CHECK_N(odg_edit_delete(&e, &doc), OWF_OK);
    CHECK_N(e.n, 0);
    CHECK(!cell(a) && !cell(b));
    CHECK_N(odg_edit_delete(&e, &doc), OWF_ERR_FORMAT);
    CHECK_N(odg_undo(&doc), OWF_OK);
    CHECK(cell(a) && cell(b));
    done();
}

static void test_hit(void)
{
    char a[24], ed[24], id[ODG_EDIT_ID], longid[100];
    owf_dpoint p0 = { PX(100), PX(200) }, p1 = { PX(300), PX(200) };
    owf_dcell *c;
    fresh(0);
    odg_add_shape(&doc, PX(10), PX(10), PX(40), PX(20), "", NULL, a);
    odg_add_edge(&doc, NULL, NULL, &p0, &p1, "", ed);
    CHECK(odg_edit_hit(&v, &doc, 30, 20, 0, id));
    CHECK_S(id, a);
    CHECK(odg_edit_hit(&v, &doc, 150, 203, 0, id));      /* 3 pixels off the line: still on it */
    CHECK_S(id, ed);
    CHECK(!odg_edit_hit(&v, &doc, 150, 206, 0, id));
    CHECK(!odg_edit_hit(&v, &doc, 150, 200, 1, id));     /* shapes only: the connector is passed over */
    CHECK(odg_edit_hit(&v, &doc, 30, 20, 1, id));
    CHECK(!odg_edit_hit(&v, &doc, 5, 5, 0, id));
    v.zoom = 200;                                         /* 4 pixels at 200% are 2 on the page */
    CHECK(odg_edit_hit(&v, &doc, 300, 403, 0, id));
    CHECK(!odg_edit_hit(&v, &doc, 300, 406, 0, id));
    v.zoom = 100;
    /* a shape whose id is too long to hold is not picked at all (a cut id would name another cell) */
    memset(longid, 'k', sizeof longid - 1);
    longid[sizeof longid - 1] = 0;
    c = owf_dpage_add(odg_page(&doc), longid, "1", OWF_DCELL_VERTEX);
    c->geo.flags = OWF_DGEO_HAS;
    c->geo.x = PX(300);
    c->geo.y = PX(10);
    c->geo.width = PX(20);
    c->geo.height = PX(20);
    CHECK(!odg_edit_hit(&v, &doc, 310, 20, 0, id));
    CHECK(!odg_edit_hit(&v, &doc, 310, 20, 1, id));
    done();
}

static void test_create(void)
{
    owf_dcell *c;
    oldraw_rect r;
    fresh(0);
    e.tool = ODG_TOOL_BOX;
    CHECK_N(odg_edit_press(&e, &v, &doc, 20, 30, 0), ODG_DRAG_CREATE);
    CHECK(!odg_edit_feedback(&e, &v, &doc, &r));         /* not moved yet: nothing to draw */
    odg_edit_motion(&e, 80, 70);
    CHECK(odg_edit_feedback(&e, &v, &doc, &r));
    CHECK_N(r.x, 20); CHECK_N(r.y, 30); CHECK_N(r.w, 60); CHECK_N(r.h, 40);
    CHECK_N(odg_edit_release(&e, &v, &doc, 80, 70), OWF_OK);
    CHECK(e.made[0] != 0);
    c = cell(e.made);
    CHECK(c && c->kind == OWF_DCELL_VERTEX);
    CHECK_N(c->geo.x, PX(20)); CHECK_N(c->geo.y, PX(30));
    CHECK_N(c->geo.width, PX(60)); CHECK_N(c->geo.height, PX(40));
    CHECK_S(c->style, "rounded=0;whiteSpace=wrap;html=1;");
    CHECK_N(e.tool, ODG_TOOL_SELECT);                     /* back to selecting, the new shape selected */
    CHECK_N(e.n, 1);
    CHECK_S(e.ids[0], e.made);
    CHECK_N(e.drag, ODG_DRAG_NONE);

    e.tool = ODG_TOOL_ELLIPSE;                            /* drawn up and left: the same box */
    CHECK_N(drag(150, 120, 100, 100, 0), OWF_OK);
    c = cell(e.made);
    CHECK_N(c->geo.x, PX(100)); CHECK_N(c->geo.y, PX(100));
    CHECK_N(c->geo.width, PX(50)); CHECK_N(c->geo.height, PX(20));
    CHECK_S(c->style, "ellipse;whiteSpace=wrap;html=1;");

    e.tool = ODG_TOOL_RHOMBUS;                            /* a click: the usual size, its corner there */
    odg_edit_press(&e, &v, &doc, 5, 6, 0);
    CHECK_N(odg_edit_release(&e, &v, &doc, 6, 7), OWF_OK);
    c = cell(e.made);
    CHECK_N(c->geo.x, PX(5)); CHECK_N(c->geo.y, PX(6));
    CHECK_N(c->geo.width, PX(120)); CHECK_N(c->geo.height, PX(60));

    e.tool = ODG_TOOL_TEXT;
    odg_edit_press(&e, &v, &doc, 200, 200, 0);
    CHECK_N(odg_edit_release(&e, &v, &doc, 200, 200), OWF_OK);
    c = cell(e.made);
    CHECK_N(c->geo.width, PX(60)); CHECK_N(c->geo.height, PX(30));
    CHECK_S(c->value, "Text");
    CHECK(owf_dstyle_is(c->style, "text"));

    CHECK_N(odg_undo(&doc), OWF_OK);                      /* each shape is one step */
    odg_edit_prune(&e, &doc);
    CHECK_N(e.n, 0);
    done();

    fresh(1);                                             /* on a grid of 10 both corners snap */
    e.tool = ODG_TOOL_BOX;
    CHECK_N(drag(13, 17, 58, 44, 0), OWF_OK);
    c = cell(e.made);
    CHECK_N(c->geo.x, PX(10)); CHECK_N(c->geo.y, PX(20));
    CHECK_N(c->geo.width, PX(50)); CHECK_N(c->geo.height, PX(20));
    e.tool = ODG_TOOL_BOX;                                /* too thin to snap to anything: a grid step */
    CHECK_N(drag(100, 100, 140, 103, 0), OWF_OK);
    c = cell(e.made);
    CHECK_N(c->geo.width, PX(40)); CHECK_N(c->geo.height, PX(10));
    done();
}

static void test_move(void)
{
    char a[24], b[24];
    owf_dcell *c;
    oldraw_rect r;
    fresh(0);
    odg_add_shape(&doc, PX(10), PX(10), PX(40), PX(20), "", NULL, a);
    odg_add_shape(&doc, PX(100), PX(10), PX(40), PX(20), "", NULL, b);
    CHECK_N(odg_edit_press(&e, &v, &doc, 20, 20, 0), ODG_DRAG_MOVE);
    CHECK_N(e.n, 1);
    CHECK_S(e.ids[0], a);
    odg_edit_motion(&e, 50, 40);
    CHECK(odg_edit_feedback(&e, &v, &doc, &r));           /* the box where it will go */
    CHECK_N(r.x, 40); CHECK_N(r.y, 30); CHECK_N(r.w, 39); CHECK_N(r.h, 19);
    CHECK_N(odg_edit_release(&e, &v, &doc, 50, 40), OWF_OK);
    c = cell(a);
    CHECK_N(c->geo.x, PX(40)); CHECK_N(c->geo.y, PX(30));
    CHECK_N(cell(b)->geo.x, PX(100));

    /* a press that moves less than the slop is a click: nothing moves, nothing to undo */
    CHECK_N(odg_undo(&doc), OWF_OK);
    CHECK_N(doc.nundo, 2);                                /* the two shapes */
    odg_edit_press(&e, &v, &doc, 20, 20, 0);
    odg_edit_motion(&e, 22, 21);
    CHECK_N(odg_edit_release(&e, &v, &doc, 22, 21), OWF_OK);
    CHECK_N(doc.nundo, 2);
    CHECK_N(cell(a)->geo.x, PX(10));

    /* Shift adds the other; dragging either takes both */
    CHECK_N(odg_edit_press(&e, &v, &doc, 110, 20, 1), ODG_DRAG_MOVE);
    CHECK_N(e.n, 2);
    odg_edit_motion(&e, 110, 70);
    CHECK(odg_edit_feedback(&e, &v, &doc, &r));           /* round both */
    CHECK_N(r.x, 10); CHECK_N(r.y, 60); CHECK_N(r.w, 129); CHECK_N(r.h, 19);
    CHECK_N(odg_edit_release(&e, &v, &doc, 110, 70), OWF_OK);
    CHECK_N(cell(a)->geo.y, PX(60));
    CHECK_N(cell(b)->geo.y, PX(60));
    CHECK_N(odg_undo(&doc), OWF_OK);                      /* one step for both */
    CHECK_N(cell(a)->geo.y, PX(10));
    CHECK_N(cell(b)->geo.y, PX(10));

    /* Shift on a selected one takes it out, and doesn't drag */
    CHECK_N(odg_edit_press(&e, &v, &doc, 110, 20, 1), ODG_DRAG_NONE);
    CHECK_N(e.n, 1);
    CHECK_S(e.ids[0], a);
    odg_edit_motion(&e, 200, 200);
    CHECK_N(odg_edit_release(&e, &v, &doc, 200, 200), OWF_OK);
    CHECK_N(cell(b)->geo.x, PX(100));

    /* at 200% a drag of 40 pixels is 20 on the page; the pressed shape lands on the grid */
    v.zoom = 200;
    doc.grid = PX(10);
    drag(40, 40, 85, 40, 0);
    CHECK_N(cell(a)->geo.x, PX(30));
    v.zoom = 100;
    doc.grid = 0;

    CHECK_N(odg_edit_nudge(&e, &doc, PX(1), PX(-1)), OWF_OK);
    CHECK_N(cell(a)->geo.x, PX(31));
    CHECK_N(cell(a)->geo.y, PX(9));
    odg_edit_clear(&e);
    CHECK_N(odg_edit_nudge(&e, &doc, PX(1), 0), OWF_ERR_FORMAT);
    done();
}

static void test_resize(void)
{
    char a[24], g[24];
    owf_dcell *c, *kid;
    oldraw_rect r;
    fresh(0);
    odg_add_shape(&doc, PX(10), PX(10), PX(40), PX(20), "", NULL, a);
    odg_edit_select(&e, a, 0);
    CHECK_N(odg_edit_press(&e, &v, &doc, 49, 29, 0), ODG_DRAG_RESIZE);   /* the SE handle */
    CHECK_N(e.handle, ODG_HANDLE_SE);
    odg_edit_motion(&e, 59, 39);
    CHECK(odg_edit_feedback(&e, &v, &doc, &r));
    CHECK_N(r.x, 10); CHECK_N(r.y, 10); CHECK_N(r.w, 49); CHECK_N(r.h, 29);
    CHECK_N(odg_edit_release(&e, &v, &doc, 59, 39), OWF_OK);
    c = cell(a);
    CHECK_N(c->geo.x, PX(10)); CHECK_N(c->geo.y, PX(10));
    CHECK_N(c->geo.width, PX(50)); CHECK_N(c->geo.height, PX(30));
    CHECK_N(e.n, 1);                                      /* still selected */

    /* the NW handle: the corner moves, the far side stays */
    drag(10, 10, 0, 5, 0);
    CHECK_N(c->geo.x, 0); CHECK_N(c->geo.y, PX(5));
    CHECK_N(c->geo.width, PX(60)); CHECK_N(c->geo.height, PX(35));
    done();

    /* inside a group the core takes the place from the group's corner: it must stay there, not jump */
    fresh(0);
    odg_add_shape(&doc, PX(100), PX(100), PX(200), PX(150), "swimlane;", "G", g);
    kid = owf_dpage_add(odg_page(&doc), "kid", g, OWF_DCELL_VERTEX);
    kid->geo.flags = OWF_DGEO_HAS;
    kid->geo.x = PX(20);
    kid->geo.y = PX(40);
    kid->geo.width = PX(40);
    kid->geo.height = PX(20);
    odg_edit_select(&e, "kid", 0);
    CHECK_N(odg_edit_press(&e, &v, &doc, 159, 149, 0), ODG_DRAG_RESIZE); /* the E handle at 120 + 39, 140 + 9 */
    CHECK_N(e.handle, ODG_HANDLE_E);
    CHECK_N(odg_edit_release(&e, &v, &doc, 169, 149), OWF_OK);
    kid = cell("kid");
    CHECK_N(kid->geo.x, PX(20)); CHECK_N(kid->geo.y, PX(40));
    CHECK_N(kid->geo.width, PX(50)); CHECK_N(kid->geo.height, PX(20));
    done();
}

static void test_connect(void)
{
    char a[24], b[24], made[ODG_EDIT_ID];
    owf_dcell *c;
    oldraw_rect r;
    fresh(0);
    odg_add_shape(&doc, PX(10), PX(10), PX(40), PX(20), "", NULL, a);
    odg_add_shape(&doc, PX(200), PX(10), PX(40), PX(20), "", NULL, b);
    e.tool = ODG_TOOL_CONNECTOR;
    CHECK_N(odg_edit_press(&e, &v, &doc, 30, 20, 0), ODG_DRAG_CONNECT);
    CHECK_S(e.from, a);
    odg_edit_motion(&e, 220, 25);
    CHECK(odg_edit_feedback(&e, &v, &doc, &r));           /* the line's ends */
    CHECK_N(r.x, 30); CHECK_N(r.y, 20); CHECK_N(r.w, 190); CHECK_N(r.h, 5);
    CHECK_N(odg_edit_release(&e, &v, &doc, 220, 25), OWF_OK);
    c = cell(e.made);
    CHECK(c && c->kind == OWF_DCELL_EDGE);
    CHECK_S(c->source, a);
    CHECK_S(c->target, b);
    CHECK(owf_dstyle_is(c->style, "edgeStyle") || strstr(c->style, "orthogonalEdgeStyle") != NULL);
    CHECK_N(e.tool, ODG_TOOL_CONNECTOR);                  /* the connector tool stays for the next line */
    CHECK_N(e.n, 1);
    CHECK_S(e.ids[0], e.made);
    snprintf(made, sizeof made, "%s", e.made);

    /* let go on nothing: the far end is left loose where the mouse was */
    CHECK_N(drag(30, 20, 120, 150, 0), OWF_OK);
    c = cell(e.made);
    CHECK(c && strcmp(e.made, made) != 0);
    CHECK_S(c->source, a);
    CHECK(c->target == NULL);
    CHECK_N(c->geo.target_point.x, PX(120));
    CHECK_N(c->geo.target_point.y, PX(150));

    /* from a shape back to itself, and a click on nothing: no connector */
    CHECK_N(drag(20, 15, 40, 25, 0), OWF_OK);
    CHECK(e.made[0] == 0);
    odg_edit_press(&e, &v, &doc, 300, 200, 0);
    CHECK_N(odg_edit_release(&e, &v, &doc, 300, 200), OWF_OK);
    CHECK(e.made[0] == 0);
    /* a click on a shape without a drag either */
    odg_edit_press(&e, &v, &doc, 220, 20, 0);
    CHECK_N(odg_edit_release(&e, &v, &doc, 220, 20), OWF_OK);
    CHECK(e.made[0] == 0);
    /* from nothing to a shape: a loose start */
    CHECK_N(drag(100, 200, 210, 20, 0), OWF_OK);
    c = cell(e.made);
    CHECK(c && c->source == NULL);
    CHECK_S(c->target, b);
    CHECK_N(c->geo.source_point.x, PX(100));
    done();
}

static void test_lasso(void)
{
    char a[24], b[24], ed[24];
    owf_dpoint p0 = { PX(20), PX(80) }, p1 = { PX(60), PX(80) };
    fresh(0);
    odg_add_shape(&doc, PX(10), PX(10), PX(40), PX(20), "", NULL, a);
    odg_add_shape(&doc, PX(200), PX(10), PX(40), PX(20), "", NULL, b);
    odg_add_edge(&doc, NULL, NULL, &p0, &p1, "", ed);
    odg_edit_select(&e, b, 0);
    CHECK_N(odg_edit_press(&e, &v, &doc, 0, 0, 0), ODG_DRAG_LASSO);
    CHECK_N(e.n, 0);                                      /* a press on nothing drops the selection */
    CHECK_N(drag(0, 0, 100, 100, 0), OWF_OK);             /* round a and the line, not b */
    CHECK_N(e.n, 2);
    CHECK(odg_edit_has(&e, a));
    CHECK(odg_edit_has(&e, ed));
    CHECK(!odg_edit_has(&e, b));
    CHECK_N(drag(190, 0, 260, 40, 1), OWF_OK);            /* Shift: b too */
    CHECK_N(e.n, 3);
    CHECK_N(drag(0, 0, 30, 30, 0), OWF_OK);               /* a only partly inside: not taken */
    CHECK_N(e.n, 0);
    CHECK_N(doc.nundo, 3);                                /* selecting is not an edit */
    odg_edit_press(&e, &v, &doc, 300, 250, 0);            /* a release elsewhere after the window went */
    odg_edit_motion(&e, 350, 280);
    odg_edit_cancel(&e);
    CHECK_N(e.drag, ODG_DRAG_NONE);
    CHECK(!odg_edit_feedback(&e, &v, &doc, &(oldraw_rect){ 0, 0, 0, 0 }));
    done();
}

static void test_locked_and_hidden(void)
{
    char a[24], b[24], id[ODG_EDIT_ID];
    int at;
    fresh(0);
    odg_add_shape(&doc, PX(10), PX(10), PX(40), PX(20), "", NULL, a);
    odg_layer_add(&doc, "Top", &at);
    odg_add_shape(&doc, PX(100), PX(10), PX(40), PX(20), "", NULL, b);
    odg_layer_lock(&doc, 0, 1);                           /* the bottom layer locked: a can't be picked */
    CHECK(!odg_edit_hit(&v, &doc, 20, 20, 0, id));
    CHECK(!odg_edit_hit(&v, &doc, 20, 20, 1, id));
    CHECK(odg_edit_hit(&v, &doc, 110, 20, 0, id));
    CHECK_N(odg_edit_press(&e, &v, &doc, 20, 20, 0), ODG_DRAG_LASSO);   /* a press on it is a press on nothing */
    CHECK_N(drag(0, 0, 300, 100, 0), OWF_OK);
    CHECK_N(e.n, 1);
    CHECK_S(e.ids[0], b);
    CHECK_N(odg_edit_select_all(&e, &doc), 1);
    e.tool = ODG_TOOL_CONNECTOR;                          /* nor joined to */
    CHECK_N(drag(110, 20, 20, 20, 0), OWF_OK);
    CHECK(cell(e.made)->target == NULL);
    odg_layer_lock(&doc, 0, 0);
    odg_layer_show(&doc, 1, 0);                           /* the top layer hidden: b can't be picked */
    CHECK(!odg_edit_hit(&v, &doc, 110, 20, 0, id));
    CHECK(odg_edit_hit(&v, &doc, 20, 20, 0, id));
    CHECK_N(odg_edit_select_all(&e, &doc), 1);
    CHECK_S(e.ids[0], a);
    done();
}

int main(void)
{
    test_tools();
    test_selection();
    test_hit();
    test_create();
    test_move();
    test_resize();
    test_connect();
    test_lasso();
    test_locked_and_hidden();
    printf("diagram edit: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
