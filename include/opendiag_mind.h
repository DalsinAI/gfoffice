/*
 * opendiag_mind.h: GFOpenDiag's mind-map mode (src/diag/odg_mind.c), on the editing core (opendiag_core.h). A map
 * is a tree in an ordinary diagram: its nodes are shapes whose style has treeFolding=1 (as draw.io's own mind-map
 * shapes have), a node's children are the nodes its connectors run to, in the order the connectors come in the
 * page, and a folded node has collapsed="1" with its branch hidden (visible="0"). So moving between the modes
 * keeps everything, and draw.io opens the map as the same tree. Free shapes and connectors beside the tree are
 * left alone.
 * Each edit is one Undo step and lays the map out again (right of the root, or both sides). Plain C99, integer
 * arithmetic, no Amiga headers: tested on the host (tests/diag/test_odg_mind.c).
 * Design: amicradle-teams designs/GFOPENDIAG-20261113.md, section 3 and step D5.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef OPENDIAG_MIND_H
#define OPENDIAG_MIND_H

#include "opendiag_core.h"

#define ODG_MIND_MAX 512                 /* nodes in one map that a walk takes; the rest are left as they are */
#define ODG_MIND_DEPTH 64                /* levels a walk up or down goes */
#define ODG_MIND_HGAP OWF_DNUM(40)       /* across from a node to its children */
#define ODG_MIND_VGAP OWF_DNUM(10)       /* between one branch and the next */
#define ODG_MIND_ID 64                   /* an id's bytes with its NUL, as ODG_EDIT_ID: the size of the ids handed back */

/* The styles new nodes and branches get. */
#define ODG_MIND_ROOT_STYLE "ellipse;whiteSpace=wrap;html=1;treeFolding=1;treeMoving=1;"
#define ODG_MIND_NODE_STYLE "rounded=1;whiteSpace=wrap;html=1;treeFolding=1;treeMoving=1;"
#define ODG_MIND_EDGE_STYLE "edgeStyle=entityRelationEdgeStyle;startArrow=none;endArrow=none;html=1;curved=1;"

typedef enum { ODG_MIND_PARENT, ODG_MIND_CHILD, ODG_MIND_PREV, ODG_MIND_NEXT } odg_mind_dir;

/* Reading the tree. The pointers are into the page and last until the next edit. */
/* 1 when the cell is a map node. */
int odg_mind_is_node(const owf_dcell *c);
/* A node's parent node; NULL for a root (or when id isn't a node). */
const owf_dcell *odg_mind_parent(const odg_doc *doc, const char *id);
/* A node's children, in order, into out (at most max); how many it has, which may be more than max. */
int odg_mind_children(const odg_doc *doc, const char *id, const owf_dcell **out, int max);
/* The root of the map the node is in; NULL when it isn't a node, or the walk up loops. */
const owf_dcell *odg_mind_root(const odg_doc *doc, const char *id);
/* 1 when the map the node is in is laid out on both sides: a child of its root sits left of it. The edits' both
 * argument takes this to keep a map the way it is. */
int odg_mind_both(const odg_doc *doc, const char *id);
/* 1 when the node is folded (its branch hidden). */
int odg_mind_folded(const owf_dcell *c);
/* The arrow keys: the node next to id that way (the parent, the first child, the sibling above or below) into
 * out (ODG_MIND_ID bytes); 0 when there is none, or its id is longer than out holds. A folded node's children are not stepped into. */
int odg_mind_step(const odg_doc *doc, const char *id, odg_mind_dir dir, char *out);

/* Edits: one Undo step each, the map laid out again after it (both sides when both is 1). Ids come back in id (at
 * ODG_MIND_ID bytes) when it isn't NULL. OWF_ERR_FORMAT when a node named isn't one, or is hidden or locked;
 * OWF_ERR_MEMORY. */
/* A new map: its root at x, y on the current layer. */
int odg_mind_new(odg_doc *doc, owf_dnum x, owf_dnum y, const char *text, char *id);
/* A new last child of parent (a folded parent is unfolded). */
int odg_mind_add_child(odg_doc *doc, const char *parent, const char *text, int both, char *id);
/* A new sibling of node, below it (above 0) or above it; a root has none (OWF_ERR_FORMAT). */
int odg_mind_add_sibling(odg_doc *doc, const char *node, int above, const char *text, int both, char *id);
/* The node and its whole branch gone (and the branches' connectors). */
int odg_mind_delete(odg_doc *doc, const char *node, int both);
/* The node moved up (by -1) or down (by 1) among its siblings; OWF_ERR_FORMAT at the end of the row. */
int odg_mind_move(odg_doc *doc, const char *node, int by, int both);
/* The node's branch folded (hidden) or unfolded; OWF_ERR_FORMAT when it has no children. */
int odg_mind_fold(odg_doc *doc, const char *node, int folded, int both);
/* The map node is in laid out again. */
int odg_mind_layout(odg_doc *doc, const char *node, int both);

#endif
