/*
 * OWFLibsTest: owfdiag.library (built with OWF_USE_DIAG_LIBRARY): the
 * shared calls and the diagram structures' sizes, then diagrams: a page
 * edited through the editing core (shapes, a connector, Undo and Redo),
 * saved as draw.io (plain and compressed) and read back, written as SVG,
 * PDF, EPS, Mermaid, DOT, GraphML and Excalidraw; a mind map; the
 * stencils; odg_port_point on top of odg_port_xy; and ARexx commands
 * (ADDSHAPE, GETVALUE, SAVEAS, OPEN) through odg_rexx_command, writing
 * and reading a file in T: with the library's own files.
 * With OWF_LIBS_HOST the same checks run on the PC with libowf linked in
 * (owf_libs_host.h; make diagram-library-test), so a check is known good
 * before an Amiga runs it.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "owf.h"
#include "owf_open.h"
#include "owf_diagram.h"
#include "opendiag_core.h"
#include "opendiag_mind.h"
#include "opendiag_stencil.h"
#include "opendiag_rexx.h"
#ifdef OWF_LIBS_HOST
#include "owf_libs_host.h"
#else
#include "owf_libs_shared.h"
#endif

static void sizes(const owf_libs_env *e)
{
    LCHECK(e, owf_struct_size(OWF_DSTRUCT_DIAGRAM) == sizeof(owf_diagram) &&
           owf_struct_size(OWF_DSTRUCT_DCELL) == sizeof(owf_dcell) &&
           owf_struct_size(OWF_DSTRUCT_DOC) == sizeof(odg_doc) && owf_struct_size(OWF_DSTRUCT_REXX) == sizeof(odg_rexx) &&
           owf_struct_size(OWF_DSTRUCT_END) == 0 && owf_struct_size(OWF_DSTRUCT_FIRST - 1) == 0,
           "%s: the diagram structures' sizes (owf_open checked them)", e->tag);
}

static int starts(const unsigned char *data, size_t length, const char *with)
{
    size_t n = strlen(with);
    return data && length >= n && !memcmp(data, with, n);
}

static void edited_page(const owf_libs_env *e)
{
    typedef int (*writer)(const owf_diagram *, int, unsigned char **, size_t *);
    static const struct { const char *name; writer write; int page; const char *start; } out[] = {
        { "SVG", owf_dsvg_write, 0, "<?xml" }, { "PDF", owf_dpdf_write, -1, "%PDF-" },
        { "EPS", owf_deps_write, 0, "%!PS-Adobe" }, { "Mermaid", owf_dmermaid_write, 0, "---\ntitle:" },
        { "DOT", owf_ddot_write, 0, "digraph" }, { "GraphML", owf_dgraphml_write, 0, "<?xml" },
        { "Excalidraw", owf_dexcal_write, 0, "{" },
    };
    owf_diagram d, back;
    odg_doc *doc = (odg_doc *)owf_calloc(1, sizeof *doc);
    char a[32] = "", b[32] = "", edge[32] = "";
    unsigned char *data = NULL;
    size_t length = 0;
    int r, i, cells = 0, compressed;

    owf_diagram_init(&d);
    if (!doc || !owf_diagram_add_page(&d, "page-1", "Page-1")) {
        LCHECK(e, 0, "%s: a new diagram", e->tag);
        owf_diagram_free(&d);
        owf_free(doc);
        return;
    }
    odg_init(doc, &d, 0);
    r = odg_add_shape(doc, OWF_DNUM(40), OWF_DNUM(40), OWF_DNUM(120), OWF_DNUM(60), "rounded=1;", "Start", a);
    if (r == OWF_OK) r = odg_add_shape(doc, OWF_DNUM(240), OWF_DNUM(40), OWF_DNUM(120), OWF_DNUM(60), NULL, "End", b);
    if (r == OWF_OK) r = odg_add_edge(doc, a, b, NULL, NULL, "edgeStyle=orthogonalEdgeStyle;", edge);
    cells = d.pages[0].ncells;
    LCHECK(e, r == OWF_OK && *a && *b && *edge && owf_dpage_find(odg_page(doc), edge) &&
           owf_dpage_find(odg_page(doc), edge)->kind == OWF_DCELL_EDGE, "%s: two shapes and a connector (%d cells)",
           e->tag, cells);
    r = odg_undo(doc);
    LCHECK(e, r == OWF_OK && !owf_dpage_find(odg_page(doc), edge) && odg_can_redo(doc) && odg_redo(doc) == OWF_OK &&
           owf_dpage_find(odg_page(doc), edge) && d.pages[0].ncells == cells, "%s: Undo and Redo", e->tag);

    for (compressed = 0; compressed < 2; compressed++) {
        r = owf_drawio_write(&d, compressed, &data, &length);
        owf_diagram_init(&back);
        if (r == OWF_OK) r = owf_drawio_read(data, length, &back);
        LCHECK(e, r == OWF_OK && back.npages == 1 && back.pages[0].ncells == cells &&
               owf_dpage_find(&back.pages[0], a) && !strcmp(owf_dpage_find(&back.pages[0], a)->value, "Start"),
               "%s: saved as draw.io%s (%lu bytes) and read back", e->tag, compressed ? ", compressed" : "",
               (unsigned long)length);
        owf_diagram_free(&back);
        owf_free(data);
        data = NULL;
    }
    for (i = 0; i < (int)(sizeof out / sizeof out[0]); i++) {
        r = out[i].write(&d, out[i].page, &data, &length);
        LCHECK(e, r == OWF_OK && starts(data, length, out[i].start), "%s: written as %s, %lu bytes", e->tag, out[i].name,
               (unsigned long)(r == OWF_OK ? length : 0));
        owf_free(data);
        data = NULL;
    }
    odg_free(doc);
    owf_free(doc);
    owf_diagram_free(&d);
}

static void mind_map(const owf_libs_env *e)
{
    owf_diagram d;
    odg_doc *doc = (odg_doc *)owf_calloc(1, sizeof *doc);
    const owf_dcell *kids[4];
    char root[32] = "", child[32] = "";
    int r = OWF_ERR_MEMORY, n = 0;

    owf_diagram_init(&d);
    if (doc && owf_diagram_add_page(&d, "page-1", "Page-1")) {
        odg_init(doc, &d, 0);
        r = odg_mind_new(doc, OWF_DNUM(100), OWF_DNUM(100), "Plan", root);
        if (r == OWF_OK) r = odg_mind_add_child(doc, root, "Step one", 0, child);
        if (r == OWF_OK) n = odg_mind_children(doc, root, kids, 4);
        odg_free(doc);
    }
    LCHECK(e, r == OWF_OK && n == 1 && kids[0] && !strcmp(kids[0]->id, child), "%s: a mind map, a root and a child",
           e->tag);
    owf_free(doc);
    owf_diagram_free(&d);
}

static void stencils_and_ports(const owf_libs_env *e)
{
    static const owf_dnum box[4] = { OWF_DNUM(10), OWF_DNUM(20), OWF_DNUM(100), OWF_DNUM(50) };
    const odg_stencil *first = odg_stencil_at(0);
    owf_dpoint east = odg_port_point(box, ODG_PORT_E);
    LCHECK(e, odg_stencil_count() > 10 && first && odg_stencil_find(first->name) == 0 && !odg_stencil_at(-1),
           "%s: %d stencils, found by name", e->tag, odg_stencil_count());
    LCHECK(e, east.x == OWF_DNUM(110) && east.y == OWF_DNUM(45), "%s: odg_port_point (through odg_port_xy)", e->tag);
}

static void rexx(const owf_libs_env *e)
{
    odg_rexx *rx = (odg_rexx *)owf_calloc(1, sizeof *rx);
    char result[256], id[64], cmd[160], path[96];
    int r;

    if (!rx || odg_rexx_init(rx) != OWF_OK) {
        LCHECK(e, 0, "%s: odg_rexx_init", e->tag);
        owf_free(rx);
        return;
    }
    r = odg_rexx_command(rx, "ADDSHAPE 20 20 100 40 TEXT=Hello", id, sizeof id);
    snprintf(cmd, sizeof cmd, "GETVALUE %s", id);
    LCHECK(e, r == ODG_RX_OK && *id && odg_rexx_command(rx, cmd, result, sizeof result) == ODG_RX_OK &&
           !strcmp(result, "Hello"), "%s: ARexx ADDSHAPE and GETVALUE (%s)", e->tag, id);
    snprintf(path, sizeof path, "%sowflibstest-%s.drawio", e->temp, e->tag);
    for (char *s = path; *s; s++)
        if (*s == ' ') *s = '-';
    snprintf(cmd, sizeof cmd, "SAVEAS \"%s\"", path);
    r = odg_rexx_command(rx, cmd, result, sizeof result);
    if (r == ODG_RX_OK) {
        odg_rexx_command(rx, "NEW FORCE", result, sizeof result);
        snprintf(cmd, sizeof cmd, "OPEN \"%s\"", path);
        r = odg_rexx_command(rx, cmd, result, sizeof result);
    }
    snprintf(cmd, sizeof cmd, "GETVALUE %s", id);
    LCHECK(e, r == ODG_RX_OK && odg_rexx_command(rx, cmd, result, sizeof result) == ODG_RX_OK &&
           !strcmp(result, "Hello"), "%s: ARexx SAVEAS and OPEN through the library's files (%s)", e->tag,
           r == ODG_RX_OK ? "read back" : rx->lasterror);
    unit_delete_file(path);
    LCHECK(e, odg_rexx_format("x.svg") == ODG_FILE_SVG && odg_rexx_format("x.drawio") == ODG_FILE_DRAWIO,
           "%s: odg_rexx_format", e->tag);
    odg_rexx_free(rx);
    owf_free(rx);
}

static void run(const owf_libs_env *e)
{
    shared_checks(e, OWF_KIND_UNKNOWN);
    sizes(e);
    edited_page(e);
    mind_map(e);
    stencils_and_ports(e);
    rexx(e);
}

OWF_LIBS_UNIT(owf_libs_diag, run);
