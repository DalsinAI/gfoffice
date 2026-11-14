/*
 * owf_diagram.h: GFOpenDiag's diagram model and its native file, .drawio (diagrams.net's mxfile).
 * Plain C99 with integer arithmetic only (no FPU), built on any host.
 * Design: amicradle-teams designs/GFOPENDIAG-20261113.md (Dale's decisions, 13 Nov 2026), step D1.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 *
 * A diagram is pages; a page is cells, as mxGraph's model has them: cell "0" is the root, cell "1" the
 * default layer, and every shape (a vertex) and connector (an edge) names its parent. Sizes and places
 * are owf_dnum: hundredths of a pixel, so "10.25" comes back as "10.25". What GFOpenDiag does not use
 * (attributes it has no field for, a UserObject's own data) is kept and written back as it came.
 */
#ifndef OWF_DIAGRAM_H
#define OWF_DIAGRAM_H

#include <stddef.h>

#include "owf.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef long owf_dnum;                   /* hundredths of a pixel */
#define OWF_DNUM(px) ((owf_dnum)(px) * 100L)

/* Name and value pairs in the order they came; both UTF-8 from owf_malloc. */
typedef struct {
    char *name, *value;
} owf_dattr;
typedef struct {
    owf_dattr *v;
    int n, cap;
} owf_dattrs;

/* The value of name, or NULL. */
const char *owf_dattrs_get(const owf_dattrs *a, const char *name);
/* Sets name (a new one goes last); value NULL removes it. OWF_OK or OWF_ERR_MEMORY. */
int owf_dattrs_set(owf_dattrs *a, const char *name, const char *value);
void owf_dattrs_free(owf_dattrs *a);

typedef struct {
    owf_dnum x, y;
} owf_dpoint;

/* owf_dgeometry.flags */
#define OWF_DGEO_HAS          0x01u      /* the cell has an mxGeometry */
#define OWF_DGEO_RELATIVE     0x02u      /* relative="1": an edge's, or a label's on an edge */
#define OWF_DGEO_SOURCE_POINT 0x04u      /* an edge's loose end at its source */
#define OWF_DGEO_TARGET_POINT 0x08u      /* and at its target */
#define OWF_DGEO_OFFSET       0x10u      /* a label's offset */
#define OWF_DGEO_ALTERNATE    0x20u      /* a group's bounds when it is collapsed */
#define OWF_DGEO_POINTS       0x40u      /* an Array of points, even an empty one */

typedef struct {
    unsigned flags;
    owf_dnum x, y, width, height;
    owf_dpoint *points;                  /* an edge's bends, in order */
    int npoints, cappoints;
    owf_dpoint source_point, target_point, offset;
    owf_dnum alternate[4];               /* x, y, width, height */
    owf_dattrs extra;                    /* mxGeometry's other attributes */
} owf_dgeometry;

/* owf_dcell.kind */
enum { OWF_DCELL_PLAIN, OWF_DCELL_VERTEX, OWF_DCELL_EDGE };
/* owf_dcell.wrapper: what the mxCell came inside */
enum { OWF_DWRAP_NONE, OWF_DWRAP_USEROBJECT, OWF_DWRAP_OBJECT };

typedef struct {
    char *id;                            /* never NULL in a page */
    char *parent;                        /* NULL: none (the root) */
    char *value;                         /* the label (HTML when the style says html=1); NULL: none */
    char *style;                         /* "key=value;..." as draw.io writes it; NULL: none */
    char *source, *target;               /* an edge's ends: cells' ids; NULL: loose */
    int kind;                            /* OWF_DCELL_ */
    int wrapper;                         /* OWF_DWRAP_; the label is then the wrapper's */
    owf_dattrs data;                     /* the wrapper's attributes but id and label */
    owf_dattrs extra;                    /* mxCell's other attributes (connectable, collapsed, ...) */
    owf_dgeometry geo;
} owf_dcell;

typedef struct {
    char *id, *name;                     /* the diagram element's; NULL: none */
    owf_dattrs model;                    /* mxGraphModel's attributes (grid, page size, ...) */
    owf_dcell *cells;                    /* in file order, which is drawing order */
    int ncells, capcells;
    int compressed;                      /* it came compressed */
} owf_dpage;

typedef struct {
    owf_dattrs file;                     /* mxfile's attributes */
    owf_dpage *pages;
    int npages, cappages;
} owf_diagram;

void owf_diagram_init(owf_diagram *d);
void owf_diagram_free(owf_diagram *d);
/* A new page at the end with the root cell "0" and the layer "1"; NULL when memory ran out. A page's
 * cells move when cells are added: hold ids, not pointers, across an add. */
owf_dpage *owf_diagram_add_page(owf_diagram *d, const char *id, const char *name);

/* Copies every cell, attribute and the page's own fields from src into dst (which it frees first).
 * OWF_OK or OWF_ERR_MEMORY (dst then empty). */
int owf_dpage_copy(owf_dpage *dst, const owf_dpage *src);
/* Frees what a page holds (a page from owf_dpage_copy outside a diagram). */
void owf_dpage_free(owf_dpage *p);

/* The cell with that id, or NULL. */
owf_dcell *owf_dpage_find(const owf_dpage *p, const char *id);
/* Its index, or -1. */
int owf_dpage_index(const owf_dpage *p, const char *id);
/* A new cell at the end (on top); id NULL makes a new one. NULL when the id is taken or memory ran out. */
owf_dcell *owf_dpage_add(owf_dpage *p, const char *id, const char *parent, int kind);
/* Removes the cell, every cell inside it, and the edges joined to any of them; how many went. */
int owf_dpage_remove(owf_dpage *p, const char *id);
/* An id no cell has, into buf (at least 16 bytes). */
void owf_dpage_new_id(const owf_dpage *p, char *buf, size_t size);

/* Sets a cell's text field (id excepted) to a copy of value (NULL clears it). OWF_OK or OWF_ERR_MEMORY. */
int owf_dcell_set(char **field, const char *value);
/* Adds a bend to an edge. OWF_OK or OWF_ERR_MEMORY. */
int owf_dgeo_add_point(owf_dgeometry *g, owf_dnum x, owf_dnum y);

/* A style's value for key ("fillColor"), copied into buf; 1 when it has one, 0 when not. A bare word
 * ("ellipse;") is a key with no value and gives "". */
int owf_dstyle_get(const char *style, const char *key, char *buf, size_t size);
/* Sets key to value in *style (value NULL removes it), keeping the other keys in order. OWF_OK or
 * OWF_ERR_MEMORY. */
int owf_dstyle_set(char **style, const char *key, const char *value);

/* "12.5" into hundredths (rounded); 0 for text that is not a number. */
owf_dnum owf_dnum_parse(const char *s);
/* Hundredths as draw.io writes numbers: "12.5", "-3", "0.25". buf at least 24 bytes. */
void owf_dnum_format(owf_dnum n, char *buf, size_t size);

/* ---- .drawio ---- */

/* Reads an mxfile (or a bare mxGraphModel) into d, which owf_diagram_init has set up. Pages may be
 * plain XML or compressed (base64 of raw DEFLATE of the URI-encoded XML, as draw.io writes them when
 * asked). OWF_OK; OWF_ERR_FORMAT when it is not a diagram; OWF_ERR_CORRUPT; OWF_ERR_MEMORY. On an error
 * d holds what was read so far, for owf_diagram_free. */
int owf_drawio_read(const unsigned char *data, size_t length, owf_diagram *d);
/* Writes d as an mxfile into memory from owf_malloc, NUL after it. compressed: every page compressed
 * (deflated when zlib or z.library is there, in stored blocks when not); else plain XML, which draw.io
 * writes by default. OWF_OK or OWF_ERR_MEMORY. */
int owf_drawio_write(const owf_diagram *d, int compressed, unsigned char **data, size_t *length);

/* ---- Where cells are ---- */

/* A cell's box on the page in hundredths (x, y, width, height): its geometry, moved by every vertex it is
 * inside. 1 when it has one, 0 when not (no geometry, or a label placed along an edge). */
int owf_dcell_box(const owf_dpage *p, const owf_dcell *c, owf_dnum box[4]);
/* An edge's points on the page, from owf_malloc: its start, bends and end. An end joined to a box is at
 * the port its style names (exitX/exitY at the source, entryX/entryY at the target, fractions of the
 * box), or else where the line towards the next point leaves the box. NULL when an end is neither joined
 * nor loose, or memory ran out; *n is how many. */
owf_dpoint *owf_dedge_path(const owf_dpage *p, const owf_dcell *e, int *n);
/* 1 when the cell and every cell it is inside are shown (no visible="0"). */
int owf_dcell_shown(const owf_dpage *p, const owf_dcell *c);
/* 1 when the style names that shape: a bare word ("ellipse;") or shape=word. */
int owf_dstyle_is(const char *style, const char *word);
/* A label's text, from owf_malloc with a NUL after it: an HTML label's (html) tags taken out, its breaks and
 * blocks made '\n', its entities read; *length (when not NULL) its length. NULL when memory ran out. */
char *owf_dlabel_text(const char *value, int html, size_t *length);
/* The box round everything drawn on a page; 0 when nothing is. */
int owf_dpage_bounds(const owf_dpage *p, owf_dnum box[4]);

/* ---- SVG and PNG with the diagram inside ----
 * As diagrams.net exports them, so that a picture opens again as the diagram it came from: an SVG holds
 * the whole mxfile in its root's content attribute, a PNG in a tEXt chunk keyed "mxfile" (URI-encoded). */

/* One page drawn as SVG 1.1 (shapes, connectors, labels), with the whole diagram inside. OWF_OK;
 * OWF_ERR_FORMAT when there is no such page; OWF_ERR_MEMORY. */
int owf_dsvg_write(const owf_diagram *d, int page, unsigned char **data, size_t *length);
/* The diagram inside an SVG. OWF_ERR_FORMAT when it is not an SVG or holds none; else as owf_drawio_read. */
int owf_dsvg_read(const unsigned char *data, size_t length, owf_diagram *d);
/* A copy of the PNG with the diagram put in (after IHDR; any diagram it held is replaced). OWF_OK;
 * OWF_ERR_FORMAT when png is not a PNG; OWF_ERR_MEMORY. */
int owf_dpng_embed(const unsigned char *png, size_t png_length, const owf_diagram *d, unsigned char **data,
                   size_t *length);
/* The diagram inside a PNG (tEXt, or zTXt as older exports have it). OWF_ERR_FORMAT when it is not a PNG or
 * holds none; OWF_ERR_CORRUPT when a chunk is damaged; else as owf_drawio_read. */
int owf_dpng_read(const unsigned char *png, size_t length, owf_diagram *d);

/* ---- Outlines: a mind map's tree as FreeMind (.mm), OPML and Markdown write it ----
 * A tree as a list in reading order: each item one node, its depth from the root (0) down, each item at most one
 * deeper than the one before. Text is plain UTF-8 ('\n' between lines). */

typedef struct {
    int depth;
    char *text;                  /* owf_malloc'd */
    int folded;                  /* its branch shown folded (FreeMind's FOLDED="true"; OPML and Markdown have none) */
} owf_ditem;

typedef enum { OWF_OUTLINE_MM, OWF_OUTLINE_OPML, OWF_OUTLINE_MD } owf_outline_kind;

/* The kind a file is by its name's ending (.mm, .opml, .md or .markdown), else by its first bytes (<map, <opml
 * or <?xml with an opml element, else Markdown). */
owf_outline_kind owf_outline_guess(const char *name, const char *text, size_t length);
/* The tree in text, into *items (owf_outline_free them). One root always: several at the top go under a new
 * root named title (or "Outline"). OWF_ERR_FORMAT when it holds no node or isn't that kind; OWF_ERR_CORRUPT for
 * damaged XML; OWF_ERR_MEMORY. Markdown: "#" headings are depths 0 to 5, list items ("-", "*", "+", "1.")
 * go under the last heading by their indent; other lines are left out. Items deeper than 60 are put at 60. */
int owf_outline_read(const char *text, size_t length, owf_outline_kind kind, const char *title, owf_ditem **items,
                     int *n);
void owf_outline_free(owf_ditem *items, int n);
/* The items as that kind, into owf_malloc'd *data with a NUL after it, items deeper than 60 put at 60 (so the
 * file reads back). title is OPML's head title (NULL: the root's text). OWF_ERR_FORMAT when the list isn't a tree with one root; OWF_ERR_MEMORY. */
int owf_outline_write(const owf_ditem *items, int n, owf_outline_kind kind, const char *title, char **data,
                      size_t *length);

#ifdef __cplusplus
}
#endif

#endif
