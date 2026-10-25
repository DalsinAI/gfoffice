/*
 * libowf: decks, OpenPresentation's model, and their formats.
 *
 * A deck holds slides; each slide uses a layout and a master. Masters hold
 * the background, fixed art, the placeholders' boxes and the look of each
 * text level; layouts list placeholders with their boxes; slides hold
 * objects (text frames, pictures, shapes, tables, groups) back to front,
 * speaker notes and a transition. owf_deck_resolve says what an object
 * takes from its layout and master, once, for every reader of the model
 * (the PDF, the show, the editor).
 *
 * The rules are owf_doc's and owf_book's: plain C structs, arrays with a
 * count and a capacity, lengths in twips, text in UTF-8, memory from
 * owf_malloc. A frame's text is an owf_story; the deck's fonts, pictures
 * and default format live in one owf_doc of its own (res), whose stories
 * stay empty, so libowf's paragraph calls and OpenWrite's layout work on a
 * frame's text unchanged. What the model cannot edit is kept as the file's
 * own XML and written back on save.
 *
 * docs/gfoffice/Design-OpenPresentation.md, section 3, is the design.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#ifndef OWF_DECK_H
#define OWF_DECK_H

#include "owf.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Objects ---- */

typedef enum {
    OWF_OBJ_TEXT, OWF_OBJ_PICTURE, OWF_OBJ_SHAPE, OWF_OBJ_TABLE,
    OWF_OBJ_CHART,               /* not read yet: charts are OWF_OBJ_OTHER for now */
    OWF_OBJ_GROUP,               /* its objects follow it, with parent set to it */
    OWF_OBJ_OTHER                /* kept for save (keep), drawn as its box */
} owf_obj_kind;

typedef enum {
    OWF_PH_NONE, OWF_PH_TITLE, OWF_PH_SUBTITLE, OWF_PH_BODY, OWF_PH_OBJECT,
    OWF_PH_PICTURE, OWF_PH_TABLE, OWF_PH_CHART, OWF_PH_DATE, OWF_PH_FOOTER,
    OWF_PH_NUMBER, OWF_PH_NOTES, OWF_PH_SLIDE_IMAGE,
    OWF_PH_TEXT,                 /* a plain text placeholder (ODP's "text") */
    OWF_PH_HEADER                /* a notes or handout page's header */
} owf_placeholder;

typedef enum {
    OWF_SHAPE_RECT, OWF_SHAPE_ROUNDRECT, OWF_SHAPE_ELLIPSE, OWF_SHAPE_TRIANGLE,
    OWF_SHAPE_DIAMOND, OWF_SHAPE_LINE, OWF_SHAPE_ARROW, OWF_SHAPE_POLYGON
} owf_shape;

/* owf_look.arrows */
#define OWF_ARROW_START 1
#define OWF_ARROW_END 2

/* owf_obj.flags */
#define OWF_OBJ_INHERIT_BOX 1    /* the box is the layout's, then the master's */
#define OWF_OBJ_HIDDEN 2
#define OWF_OBJ_FLIP_H 4         /* a line runs from the right; a shape is mirrored */
#define OWF_OBJ_FLIP_V 8         /* a line runs from the bottom */
#define OWF_OBJ_EMPTY 16         /* a placeholder showing its prompt, not text of its own */

/* owf_obj.autofit */
#define OWF_AUTOFIT_NONE 0
#define OWF_AUTOFIT_SHRINK 1     /* the text shrinks to fit the box */
#define OWF_AUTOFIT_GROW 2       /* the box grows with the text */

typedef struct {
    unsigned long fill;          /* OWF_SHADE(0xRRGGBB); 0: none */
    unsigned long line;          /* OWF_SHADE(0xRRGGBB); 0: none */
    int line_width;              /* twips; 0: the thinnest line */
    unsigned char line_style;    /* OWF_LINE_..., the workbook's border numbers */
    unsigned char arrows;        /* OWF_ARROW_START | OWF_ARROW_END */
} owf_look;

/* How one level of text looks: in a master, and in a frame's own list. */
#define OWF_BULLET_NUMBER 0x80000000UL   /* | '1', 'a', 'A', 'i' or 'I': numbered */

typedef struct {
    owf_charfmt chars;           /* font into res->fonts; size twips */
    owf_parafmt para;            /* indent_left: where the text starts; indent_first
                                    (below 0): where a bullet hangs from it */
    unsigned long bullet;        /* a Unicode character; 0: none; OWF_BULLET_NUMBER | format */
    int bullet_font;             /* into res->fonts; -1: the text's */
    int bullet_size;             /* percent of the text's size; 0: 100 */
    unsigned long bullet_colour; /* OWF_SHADE(0xRRGGBB); 0: the text's */
    int start;                   /* a number's first value; 0: 1 */
    char prefix[8], suffix[8];   /* around a number: "", "." */
} owf_textlevel;

#define OWF_TEXT_LEVELS 9

typedef struct {
    owf_obj_kind kind;
    long id;                     /* the file's own id ("id3" in ODP is 3); 0: none */
    char *xml_id;                /* an ODP id that is not "id<number>", or NULL */
    int parent;                  /* a group's index in the same array; -1: on the slide */
    int x, y, width, height;     /* twips from the slide's top left, unrotated */
    int rotation;                /* tenths of a degree, anticlockwise: kept, drawn unrotated */
    unsigned flags;              /* OWF_OBJ_... */
    owf_placeholder placeholder;
    int ph_index;                /* which of its layout's placeholders of that kind */
    owf_look look;
    owf_shape shape;             /* OWF_OBJ_SHAPE */
    int radius;                  /* OWF_SHAPE_ROUNDRECT: twips */
    int npoints, *points;        /* OWF_SHAPE_POLYGON: x, y pairs, twips inside the box */
    owf_story text;              /* text, a shape's text, or a table's cells */
    /* Each paragraph's list level, until Shared E8 puts it in owf_parafmt:
       0 not in a list, 1-9 a list item's level; NULL: none in a list.
       caplevels entries: owf_obj_level and owf_obj_set_level look after it. */
    unsigned char *levels;
    int caplevels;
    owf_textlevel *list;         /* OWF_TEXT_LEVELS levels of the frame's own list; NULL: its master's */
    int ncols, *col_widths;      /* OWF_OBJ_TABLE: twips; NULL: equal */
    unsigned char valign;        /* OWF_VALIGN_TOP, _CENTRE, _BOTTOM */
    unsigned char autofit;       /* OWF_AUTOFIT_... */
    int inset[4];                /* twips: left, top, right, bottom */
    int image;                   /* OWF_OBJ_PICTURE: into res->images; -1: none */
    int crop[4];                 /* thousandths cut from left, top, right, bottom */
    struct owf_chart *chart;     /* OWF_OBJ_CHART (Design-Suite-Shared.md, 4): NULL until charts come */
    char *name, *alt;            /* UTF-8, may be NULL */
    int keep;                    /* into deck->kept; -1: none */
} owf_obj;

/* ---- Slides, masters, layouts ---- */

typedef enum {
    OWF_TRANS_NONE, OWF_TRANS_FADE, OWF_TRANS_FADE_BLACK, OWF_TRANS_WIPE,
    OWF_TRANS_PUSH, OWF_TRANS_COVER, OWF_TRANS_UNCOVER, OWF_TRANS_SPLIT,
    OWF_TRANS_BLINDS, OWF_TRANS_DISSOLVE,
    OWF_TRANS_OTHER              /* shown as a cut, kept for save */
} owf_transition_kind;

/* owf_transition.direction */
enum { OWF_DIR_LEFT, OWF_DIR_RIGHT, OWF_DIR_UP, OWF_DIR_DOWN };

typedef struct {
    owf_transition_kind kind;
    unsigned char direction;     /* OWF_DIR_...: where the new slide comes from */
    int duration;                /* milliseconds; 0: the program's default */
    int advance_after;           /* milliseconds; -1: on a click or a key only */
    int keep;                    /* OWF_TRANS_OTHER: into deck->kept; -1: none */
} owf_transition;

/* owf_slide.show: which of the master's footer fields the slide shows */
#define OWF_SHOW_FOOTER 1
#define OWF_SHOW_NUMBER 2
#define OWF_SHOW_DATE 4

typedef struct {
    char *name;
    int master;                  /* into deck->masters */
    int layout;                  /* into deck->layouts; -1: none */
    int nobjs, capobjs;
    owf_obj *objs;               /* back to front */
    owf_story notes;
    owf_transition transition;
    unsigned long background;    /* OWF_SHADE(0xRRGGBB); 0: the master's */
    int background_image;        /* into res->images; -1: none */
    int hidden;                  /* left out of the show */
    int keep_timing;             /* the file's animations: into deck->kept; -1: none */
    unsigned show;               /* OWF_SHOW_... */
    char *footer;                /* the footer's text, or NULL */
    char *date_text;             /* a fixed date's text; NULL: today's, when shown */
} owf_slide;

typedef struct {
    char *name;
    int nobjs, capobjs;
    owf_obj *objs;               /* its placeholders, and fixed art: a logo, a band */
    unsigned long background;    /* OWF_SHADE(0xRRGGBB); 0: white */
    int background_image;        /* into res->images; -1: none */
    owf_textlevel title, subtitle, body[OWF_TEXT_LEVELS], notes, other;
} owf_master;

typedef struct {
    char *name;                  /* "Title, Content" */
    int master;                  /* into deck->masters; -1: any */
    int nobjs, capobjs;
    owf_obj *objs;               /* its placeholders with their boxes */
} owf_slide_layout;

/* XML the model doesn't edit, kept for save: the element as the file had
 * it, with the namespaces it uses declared on it. */
typedef enum {
    OWF_KEPT_OBJECT,             /* an object (owf_obj.keep) */
    OWF_KEPT_TIMING,             /* a slide's animations (owf_slide.keep_timing) */
    OWF_KEPT_STYLE,              /* an automatic style kept objects use */
    OWF_KEPT_DEFINITION,         /* a gradient, hatch, marker, dash or fill picture kept objects use */
    OWF_KEPT_ATTRIBUTES,         /* attributes, as name="value" pairs (a transition's) */
    OWF_KEPT_SETTINGS,           /* the show's settings and custom shows */
    OWF_KEPT_GRAPHIC             /* the graphic properties of the file's "standard" style, which
                                    kept objects' styles inherit */
} owf_kept_kind;

typedef struct {
    owf_kept_kind kind;
    char *xml;
} owf_kept;

/* A member of the file's package that kept XML refers to (an embedded
 * chart's parts, a sound): its path, its media type and bytes. A folder
 * ("Object 1/") has a type and no bytes. */
typedef struct {
    char *name, *mime;
    unsigned char *data;
    size_t length;
} owf_deck_part;

#define OWF_KEPT_ODP 1
#define OWF_KEPT_PPTX 2

typedef struct owf_deck {
    int width, height;           /* the slide, twips; landscape when wider than tall */
    int nslides, capslides;
    owf_slide *slides;
    int nlayouts, caplayouts;
    owf_slide_layout *layouts;
    int nmasters, capmasters;
    owf_master *masters;
    owf_doc *res;                /* fonts, pictures, the default format, the title */
    owf_page notes_page;         /* the notes and handout page */
    int nkept, capkept;
    owf_kept *kept;
    int kept_format;             /* whose XML: OWF_KEPT_ODP or OWF_KEPT_PPTX; 0: none */
    int nparts, capparts;
    owf_deck_part *parts;
    size_t kept_bytes;           /* what kept XML and parts take, for the limit */
} owf_deck;

/* The usual sizes, twips. */
#define OWF_DECK_4_3_WIDTH 14400
#define OWF_DECK_4_3_HEIGHT 10800
#define OWF_DECK_16_9_WIDTH 19200
#define OWF_DECK_16_9_HEIGHT 10800

/* A new deck: one master ("Default") with its title, body, date, footer
 * and number placeholders and its text levels, and the standard layouts
 * ("Blank Slide", "Title Slide", "Title, Content", "Title and 2 Content",
 * "Title Only", "Centered Text"). No slides. NULL when memory runs out. */
owf_deck *owf_deck_new(int width, int height);
/* An empty deck: no master, no layout (importers fill it). */
owf_deck *owf_deck_new_empty(void);
void owf_deck_free(owf_deck *deck);

/* Building and changing a deck. NULL or -1 when memory runs out or an
 * index is out of range; the deck stays valid. A pointer to a slide or an
 * object stays good until the next one is added to the same array. */
owf_slide *owf_deck_add_slide(owf_deck *deck, int at, int layout);   /* at -1: the end; layout -1: none */
int owf_deck_delete_slide(owf_deck *deck, int index);
int owf_deck_move_slide(owf_deck *deck, int from, int to);
owf_master *owf_deck_add_master(owf_deck *deck, const char *name);
owf_slide_layout *owf_deck_add_layout(owf_deck *deck, const char *name, int master);
/* Objects of a slide, a master or a layout. */
owf_obj *owf_slide_add(owf_slide *slide, owf_obj_kind kind);
owf_obj *owf_master_add(owf_master *master, owf_obj_kind kind);
owf_obj *owf_layout_add(owf_slide_layout *layout, owf_obj_kind kind);
int owf_slide_delete(owf_slide *slide, int index);
void owf_obj_init(owf_obj *obj, owf_obj_kind kind);
void owf_obj_clear(owf_obj *obj);     /* frees what it holds; init again to reuse */
void owf_textlevel_init(owf_textlevel *level);
/* A paragraph's list level (0 for none), and setting it (grows levels). */
int owf_obj_level(const owf_obj *obj, int para);
int owf_obj_set_level(owf_obj *obj, int para, int level);
int owf_deck_set_text(char **field, const char *utf8);      /* a name, footer...: copied; NULL clears */
int owf_deck_keep(owf_deck *deck, owf_kept_kind kind, const char *xml, size_t length);  /* its index */
int owf_deck_add_part(owf_deck *deck, const char *name, const char *mime, const void *data, size_t length);

/* What an object takes from its layout and master: *box gets a copy of
 * the object (sharing its text) with its box from the layout's
 * placeholder of the same kind and index, then the master's, when it has
 * OWF_OBJ_INHERIT_BOX; *levels the look of its text: the master's 9 body
 * levels for a body or text placeholder, else one level (the title's,
 * subtitle's, notes' or other text's). Returns how many levels (1 or 9),
 * -1 when slide or obj is out of range. box and levels may be NULL. */
int owf_deck_resolve(const owf_deck *deck, int slide, int obj,
                     owf_obj *box, const owf_textlevel **levels);
/* The look of one paragraph of an object: its own list's level for a list
 * paragraph, else the levels owf_deck_resolve gives (by its list level for
 * body text). para -1: the object's first level. */
const owf_textlevel *owf_deck_para_level(const owf_deck *deck, const owf_master *master,
                                         const owf_obj *obj, int para);
/* A new paragraph at the end of an object's text, at list level 0 to 9,
 * with that level's paragraph format (owf_deck_para_level's). Runs added to
 * it with font -1, size 0 and OWF_COLOUR_AUTO take the level's. NULL when
 * memory runs out. */
owf_para *owf_deck_add_para(const owf_deck *deck, const owf_master *master, owf_obj *obj, int level);
/* A slide's master, never NULL for a slide of a deck with a master. */
const owf_master *owf_deck_slide_master(const owf_deck *deck, int slide);

/* ---- Formats, as the document and workbook ones ---- */

typedef struct owf_deck_format {
    const char *name;            /* "odp", "fodp" */
    const char *description;
    const char *extensions;
    int (*detect)(const unsigned char *data, size_t length);
    int (*import)(const unsigned char *data, size_t length, owf_deck *deck, owf_report *report);
    int (*export)(const owf_deck *deck, unsigned char **data, size_t *length, owf_report *report);
} owf_deck_format;

int owf_deck_format_count(void);
const owf_deck_format *owf_deck_format_at(int index);
const owf_deck_format *owf_deck_format_named(const char *name);
/* The writable deck format for a file name's extension, or NULL. */
const owf_deck_format *owf_deck_format_for_filename(const char *filename);
/* The best reader for these bytes, or NULL (to tell decks from documents). */
const owf_deck_format *owf_deck_format_detect(const unsigned char *data, size_t length);
int owf_import_deck_memory(const unsigned char *data, size_t length, const char *format,
                           owf_deck **deck, owf_report *report, const owf_deck_format **used);
int owf_import_deck_file(const char *path, const char *format, owf_deck **deck,
                         owf_report *report, const owf_deck_format **used);
int owf_export_deck_file(const owf_deck *deck, const char *path, const char *format, owf_report *report);

#ifdef __cplusplus
}
#endif

#endif
