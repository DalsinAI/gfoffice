# GFOpenPresentation: design

Version 0.1, draft for Dale's approval before code | 25 October 2026 | AmiCradle Team, MIT

Dale, 25 October 2026, 01:10: "ok, get going, we will need to enhance
openwrite, but go". The suite is the **GFOffice Suite** (Dale, 01:31): the
GFOpen apps GFOpenWrite, GFOpenCalc, GFOpenPresentation and GFOpenBase. GFOpenPresentation
is its slide program. This document says what it is, what it is
built from, and in what order. The pieces the four programs share (owf.library,
the PDF writer, charts, the clipboard, spelling, the Open requester) are in
[Design-Suite-Shared.md](Design-Suite-Shared.md); this document points there
instead of repeating them.

It is written from scratch. No LibreOffice, OpenOffice or other office code is
read, copied or ported. The formats come from their published specifications:
OASIS ODF 1.2 for ODP, ECMA-376 (PresentationML and DrawingML) for PPTX.

Every statement about existing code was checked against `main` of
openamigawrite (`51fb1d2`), gfopenlayout (`48e5cfc`) and gfopenprint
(`47eddaa`) on 25 October 2026.

## 1. What it is

A native presentation program for AmigaOS 3.2 that:

- opens and saves ODP and PPTX, with slides, layouts, masters, text, pictures,
  basic shapes, tables, notes and transitions;
- exports PDF, and prints slides and handouts through GFOpenPrint;
- shows a deck full screen, on its own screen or on the Workbench screen;
- has an ARexx port, so a script can build or run a deck.

It is built like GFOpenWrite:

| Layer | GFOpenWrite | GFOpenPresentation |
| --- | --- | --- |
| Model and formats | libowf `owf_doc` | libowf `owf_deck` (new), reusing `owf_doc`'s paragraphs and runs |
| Geometry | GFOpenLayout 0.2 pages, through `src/core/ow_layout.c` | GFOpenLayout frames, one page per slide |
| Portable core, host-tested | `src/core` | `src/deck` (new) |
| Amiga program | `app/openwrite2.c` (GFOpenGadTools) | `app/openpresent*.c` (new, GFOpenGadTools) |
| Output | PDF to `C:GFOpenPrint` (`app/ow_print.c`) | the same route |

It targets what GFOpenWrite targets (README, Requirements): AmigaOS 3.2, 68040
code, no FPU, usable at 8 MB of Fast RAM, comfortable at 16-32 MB.

## 2. What exists to build on

| Piece | Where (checked) | What it gives GFOpenPresentation | What it lacks for slides |
| --- | --- | --- | --- |
| Paragraphs and runs | `owf.h`: `owf_para`, `owf_run`, `owf_story` | A text frame's text, with fonts, sizes, colours, alignment, spacing, links, fields, inline pictures | A paragraph's list level and bullet: lists are kept as visible bullet text today (`FORMATS.md` 1, `ow_editor_toggle_list`) |
| The builder | `owf_internal.h`: `owf_builder_init(b, doc, story)` | Importers can write into any story, so into a frame | Nothing |
| Packages and XML | `owf_unzip.c`, `owf_zip.c`, `owf_xml.c`, `owf_parse_length`, `owf_parse_colour`, `owf_style.c` (style inheritance) | Reading and writing ODP and PPTX packages | The drawing and presentation parts of the two standards |
| ODT and DOCX readers | `in_odt.c`, `in_docx.c` | ODF text and styles, DrawingML theme fonts, `draw:frame` pictures | Nothing presentation-specific |
| Tables | `owf_para.table_id`, `table_row`, `table_col`, `table_cols`, `cell_shading` | Slide tables as native editable tables | Column widths (the model has none) |
| PDF writer | `out_pdf.c` | PDF objects, base-14 fonts, JPEG passed through and PNG decoded | It lays out one `owf_doc` by itself, top to bottom; it cannot place a frame at a position (Shared, E4) |
| GFOpenLayout 0.2 | gfopenlayout `include/openlayout.h` | Frames out of the flow (`OL_BOX_ABSOLUTE`), pages, grid tables, tab stops, `ol_scale` and `ol_draw_page` | Display ops are text, filled rectangle, image and rule only: no ellipses, rounded corners, lines at an angle or polygons; no rotation ("rotated frames" is on its still-to-come list); no vertical anchoring; no clipping |
| GFOpenWrite's layout | `src/core/ow_layout.c` | An `owf_doc` as a GFOpenLayout tree, the caret and hit-testing | It lays out `doc->body` only (Shared, E5) |
| GFOpenWrite's editor core | `src/core/editor.c` | Caret, selection, typing, formatting, tables, undo | It edits `doc->body` only; undo keeps up to 16 whole-document copies, picture bytes included (`clone_doc`, `OW_UNDO_LIMIT`) (Shared, E6 and E7) |
| Printing | `app/ow_print.c` | A PDF in `T:`, then `C:GFOpenPrint <file>` started on its own | Nothing |
| GFOpenGadTools copy | `third_party/opengadtools` | Toolbar, list (thumbnails), page view, inspector sections, tabs, status line, ruler | Nothing for P1-P3 |

## 3. The deck model: `owf_deck` in libowf

### 3.1 Rules

- The same style as `owf_doc` and `owf_book`: plain C structs, arrays with a
  count and a capacity, lengths in twips, text in UTF-8, memory from
  `owf_malloc`.
- **A frame's text is an `owf_story`.** The deck's fonts, pictures and default
  text format live in one `owf_doc` of its own (`res`), whose stories stay
  empty. So libowf's builder and paragraph calls, GFOpenWrite's layout and its
  editor all work on a frame's text unchanged (once Shared E5 and E6 are done).
- **Pictures stay compressed in memory**, as an `owf_doc`'s do (`owf_image`
  holds the file's bytes). They are decoded only to be drawn (section 10).
- **What we don't edit, we keep where we can.** An object or transition we
  can't draw keeps the file's own XML (`keep`), and is written back on save.
  The import report names it, as GFOpenWrite's does.
- Slide sizes are twips. PPTX measures in EMU: 914,400 to the inch, so a twip
  is exactly 635 EMU. ODP measures in cm, mm, in or pt, which
  `owf_parse_length` already turns into twips.

| Slide size | Inches | Twips |
| --- | --- | --- |
| 4:3 | 10 x 7.5 | 14,400 x 10,800 |
| 16:9 (PowerPoint's default) | 13.333 x 7.5 | 19,200 x 10,800 |
| 16:10 | 10 x 6.25 | 14,400 x 9,000 |
| A4 landscape | 11.69 x 8.27 | 16,838 x 11,906 |

### 3.2 The structures

Appended to `owf.h` after the workbook section, as a new "Decks" section. This
is the shape to agree on, not final code.

```c
/* ---- Decks: the presentation's model ---- */

typedef enum {
    OWF_OBJ_TEXT, OWF_OBJ_PICTURE, OWF_OBJ_SHAPE, OWF_OBJ_TABLE,
    OWF_OBJ_CHART, OWF_OBJ_GROUP,
    OWF_OBJ_OTHER                /* kept for save, drawn as a labelled box */
} owf_obj_kind;

typedef enum {
    OWF_PH_NONE, OWF_PH_TITLE, OWF_PH_SUBTITLE, OWF_PH_BODY, OWF_PH_OBJECT,
    OWF_PH_PICTURE, OWF_PH_TABLE, OWF_PH_CHART, OWF_PH_DATE, OWF_PH_FOOTER,
    OWF_PH_NUMBER, OWF_PH_NOTES, OWF_PH_SLIDE_IMAGE
} owf_placeholder;

typedef enum {
    OWF_SHAPE_RECT, OWF_SHAPE_ROUNDRECT, OWF_SHAPE_ELLIPSE, OWF_SHAPE_TRIANGLE,
    OWF_SHAPE_DIAMOND, OWF_SHAPE_LINE, OWF_SHAPE_ARROW, OWF_SHAPE_POLYGON
} owf_shape;

typedef struct {
    unsigned long fill;          /* OWF_SHADE(0xRRGGBB); 0: none */
    unsigned long line;          /* OWF_SHADE(0xRRGGBB); 0: none */
    int line_width;              /* twips */
    unsigned char line_style;    /* OWF_LINE_..., the workbook's border numbers */
    unsigned char arrows;        /* OWF_ARROW_START | OWF_ARROW_END */
} owf_look;

typedef struct {
    owf_obj_kind kind;
    long id;                     /* the file's own id: links and animations use it */
    int parent;                  /* a group's index in the same array; -1: on the slide */
    int x, y, width, height;     /* twips from the slide's top left */
    int rotation;                /* tenths of a degree: kept, drawn unrotated (5.3) */
    unsigned flags;              /* OWF_OBJ_INHERIT_BOX, _HIDDEN, _FLIP_H, _FLIP_V */
    owf_placeholder placeholder;
    int ph_index;                /* which of its layout's placeholders of that kind */
    owf_look look;
    owf_shape shape;             /* OWF_OBJ_SHAPE */
    int npoints, *points;        /* OWF_SHAPE_POLYGON: x, y pairs inside the box */
    owf_story text;              /* text, a shape's text, or a table's cells */
    int *col_widths;             /* OWF_OBJ_TABLE: twips; NULL: equal */
    unsigned char valign;        /* OWF_VALIGN_TOP, _CENTRE, _BOTTOM */
    unsigned char autofit;       /* 0: none; 1: shrink the text; 2: grow the box */
    int inset[4];                /* twips: left, top, right, bottom */
    int image;                   /* OWF_OBJ_PICTURE: into res->images; -1: none */
    int crop[4];                 /* thousandths cut from left, top, right, bottom */
    struct owf_chart *chart;     /* OWF_OBJ_CHART (Design-Suite-Shared.md, 4) */
    char *name, *alt;            /* UTF-8, may be NULL */
    int keep;                    /* into deck->kept; -1: none */
} owf_obj;

typedef enum {
    OWF_TRANS_NONE, OWF_TRANS_FADE, OWF_TRANS_FADE_BLACK, OWF_TRANS_WIPE,
    OWF_TRANS_PUSH, OWF_TRANS_COVER, OWF_TRANS_UNCOVER, OWF_TRANS_SPLIT,
    OWF_TRANS_BLINDS, OWF_TRANS_DISSOLVE,
    OWF_TRANS_OTHER              /* shown as a cut, kept for save */
} owf_transition_kind;

typedef struct {
    owf_transition_kind kind;
    unsigned char direction;     /* OWF_DIR_LEFT, _RIGHT, _UP, _DOWN */
    int duration;                /* milliseconds; 0: the program's default */
    int advance_after;           /* milliseconds; -1: on a click or a key only */
    int keep;                    /* OWF_TRANS_OTHER: into deck->kept */
} owf_transition;

typedef struct {
    char *name;
    int layout;                  /* into deck->layouts; -1: none */
    int nobjs, capobjs;
    owf_obj *objs;               /* back to front */
    owf_story notes;
    owf_transition transition;
    unsigned long background;    /* OWF_SHADE(0xRRGGBB); 0: the layout's, then the master's */
    int background_image;        /* into res->images; -1: none */
    int hidden;                  /* left out of the show */
    int keep_timing;             /* the file's animations (3.4): into deck->kept; -1 */
} owf_slide;

/* How one level of text looks in a master. */
typedef struct {
    owf_charfmt chars;
    owf_parafmt para;
    unsigned long bullet;        /* a Unicode character; 0: none; OWF_BULLET_NUMBER */
    int bullet_font;             /* into res->fonts; -1: the text's */
    int bullet_size;             /* percent of the text's size */
} owf_textlevel;

typedef struct {
    char *name;
    int nobjs, capobjs;
    owf_obj *objs;               /* its placeholders, and fixed art: a logo, a band */
    unsigned long background;
    int background_image;
    owf_textlevel title, body[9], other;
} owf_master;

typedef struct {
    char *name;                  /* "Title and Content" */
    int master;
    int nobjs, capobjs;
    owf_obj *objs;               /* its placeholders with their boxes, and fixed art */
} owf_slide_layout;

typedef struct owf_deck {
    int width, height;           /* the slide, twips */
    int nslides, capslides;
    owf_slide *slides;
    int nlayouts, caplayouts;
    owf_slide_layout *layouts;
    int nmasters, capmasters;
    owf_master *masters;
    owf_doc *res;                /* fonts, pictures, the default format, the title */
    owf_page notes_page;         /* the notes and handout page */
    int nkept, capkept;
    char **kept;                 /* XML kept for save */
    int kept_format;             /* whose XML: 1 ODP, 2 PPTX */
} owf_deck;

owf_deck *owf_deck_new(int width, int height);   /* one master, the standard layouts */
void owf_deck_free(owf_deck *deck);
owf_slide *owf_deck_add_slide(owf_deck *deck, int at, int layout);   /* at -1: the end */
owf_slide *owf_deck_duplicate_slide(owf_deck *deck, int index, int at);
int owf_deck_delete_slide(owf_deck *deck, int index);
int owf_deck_move_slide(owf_deck *deck, int from, int to);
owf_obj *owf_slide_add(owf_slide *slide, owf_obj_kind kind);
int owf_slide_delete(owf_slide *slide, int index);
/* What an object takes from its layout and master: its box (when
   OWF_OBJ_INHERIT_BOX), its background, its text levels. */
int owf_deck_resolve(const owf_deck *deck, int slide, int obj,
                     owf_obj *box, const owf_textlevel **levels);

/* Deck formats, as the document and workbook ones. */
typedef struct owf_deck_format {
    const char *name;            /* "odp", "pptx", "pdf" */
    const char *description;
    const char *extensions;
    int (*detect)(const unsigned char *data, size_t length);
    int (*import)(const unsigned char *data, size_t length, owf_deck *deck, owf_report *report);
    int (*export)(const owf_deck *deck, unsigned char **data, size_t *length, owf_report *report);
} owf_deck_format;
/* owf_deck_format_count, _at, _named, _for_filename, owf_import_deck_memory,
   owf_import_deck_file, owf_export_deck_file: as the workbook calls */
```

### 3.3 Placeholders, layouts and masters

Both formats work the same way, so one model serves both:

1. A **master** holds the background, fixed art, and the look of each text
   level (title, body levels 1 to 9, other text).
2. A **layout** belongs to a master and lists placeholders with their boxes:
   "Title and Content" has a title and a body.
3. A **slide** uses a layout. Its objects that fill placeholders name the kind
   and index (`placeholder`, `ph_index`). Where the slide doesn't give a box
   (`OWF_OBJ_INHERIT_BOX`), the layout's box is used, then the master's.

`owf_deck_resolve` does that lookup, once, for every reader of the model: the
layout to GFOpenLayout, the PDF, the show.

**Lists.** Body text needs a level and a bullet on each paragraph. `owf_para`
has neither: GFOpenWrite keeps lists as visible bullet and number text. Shared
E8 adds `level` and `bullet` to `owf_parafmt`. That changes a structure that
programs index through owf.library, so it goes into owf.library 2 with the
other structure changes (Design-Suite-Shared.md, 1).

### 3.4 What is kept, and what is only shown

| Content | P1-P4 | How |
| --- | --- | --- |
| Text, fonts, sizes, colours, alignment, spacing, levels, bullets | Edited | `owf_story` and the master's text levels |
| Pictures (any format a datatype reads; PDF takes JPEG and PNG) | Edited | `res->images`, as `owf_doc` holds them |
| Rectangles, rounded rectangles, ellipses, triangles, diamonds, lines, arrows, polygons | Edited | `owf_obj` shapes |
| Other preset shapes (PPTX has many) | Shown as their box, kept | `OWF_OBJ_OTHER` with `keep`; the report says "drawn as a rectangle" |
| Tables | Edited | Native table cells in `text`, column widths in `col_widths` |
| Charts | Shown, data edited later | `owf_chart` (Shared, 4) |
| Groups | Moved as one | `parent` |
| Notes | Edited | `owf_slide.notes` |
| Transitions | Edited | `owf_transition` |
| Animations (builds) | Kept, not played until P4; P4 plays "appear" and "fade in" | `keep_timing`: written back while the slide's objects keep their ids; dropped with a report note when they don't |
| Rotation and flips | Kept, drawn unrotated | `rotation`, `flags` |
| SmartArt, embedded media, OLE objects, comments | Kept where the format allows, shown as a labelled box | `OWF_OBJ_OTHER` |
| Fonts embedded in a PPTX | Not used | Report note |

## 4. Formats

| Format | Open | Save | Knowledge | Phase |
| --- | --- | --- | --- | --- |
| ODP, OpenDocument Presentation (`.odp`), its template (`.otp`) and flat XML (`.fodp`) | Yes | Yes | Spec: OASIS ODF 1.2 (presentation and drawing) | P1 |
| PPTX, Office Open XML (`.pptx`), its template (`.potx`) and show (`.ppsx`) | Yes | Yes | Spec: ECMA-376 Part 1, PresentationML and DrawingML | P2 |
| PDF | No | Export and print | Our page-drawing PDF writer (Shared, E4) | P1 |
| PowerPoint 97-2003 (`.ppt`) | Recognised, with a clear message (as `.doc` is) | No | Spec: [MS-PPT], later | Later |
| A drawer of pictures | "New deck from pictures": one slide each | Export slides as pictures (IFF ILBM through datatypes) | Datatypes | P2 (import), P4 (export) |
| Old Amiga presentation programs (Scala and others) | Not in P1-P4 | No | We know of no published specification. Only with samples from the format lab (`FORMATS.md` 4) | Later, if samples exist |

**ODP in short.** `content.xml` holds `office:presentation`, a `draw:page` per
slide naming its master page and layout. Objects are `draw:frame` elements
with a `presentation:class` (title, subtitle, outline, graphic, table, object,
notes) holding a `draw:text-box`, a `draw:image` or a `table:table`; shapes
are `draw:rect`, `draw:ellipse`, `draw:line`, `draw:polygon` and
`draw:custom-shape`. Masters are `style:master-page` in `styles.xml`. Notes
are `presentation:notes` in each page. Transitions are attributes of the
page's drawing-page style. The ODT reader already reads `draw:frame` and
`draw:image` (`in_odt.c`), and its namespaces are defined there.

**PPTX in short.** `presentation.xml` gives the slide size (`p:sldSz`, in EMU)
and the slide list; each slide, layout, master and notes page is its own part,
joined by relationships. Shapes are `p:sp` with `p:nvPr/p:ph` for a
placeholder, text in `p:txBody`, pictures in `p:pic`, tables in a
`p:graphicFrame` holding `a:tbl`, charts in a `p:graphicFrame` pointing to a
chart part. Transitions are `p:transition`. Theme colours and fonts come from
the theme part, which `in_docx.c` already reads for DOCX's theme fonts.

**Saving.** Opening a PPTX and saving does not turn it into ODP, as GFOpenWrite
does not turn DOCX into ODT (`UX_DESIGN.md`, File workflow). PPTX saving writes
a complete package: content types, relationships, presentation, one master
per master we hold, our layouts, a theme made from the master's colours and
fonts, slides, notes slides with their notes master, and media. Theme colours
are resolved to fixed colours on import; the report says so.

**The default for a new deck** is a question for Dale (section 14).

## 5. Layout through GFOpenLayout 0.2

### 5.1 A slide is one GFOpenLayout page

- One `ol_document` per slide, laid out with `ol_layout_pages` on an `ol_page`
  the slide's size, margins 0.
- The root holds one frame per object, back to front: a block with
  `OL_BOX_ABSOLUTE` and `box.frame` set to the object's box (twips times
  `OL_UNITS_PER_TWIP`, 4). GFOpenLayout lays frames out "last, above the flow"
  (`openlayout.c`, `layout_block` and `layout_galley`), in tree order, so
  drawing order is the deck's order.
- A text frame holds its story's paragraphs as blocks, made by GFOpenWrite's
  paragraph code (Shared E5). The insets become the frame's padding.
- A picture is an image node in its frame (`ol_node_set_image`).
- A table is a grid table node in its frame (`ol_node_set_columns`), as
  `ow_layout.c`'s `add_table` makes for GFOpenWrite.
- A filled and outlined rectangle needs nothing new: the frame's
  `style.background` and its box border (`border_width`, `border_argb`,
  `border_sides`) draw it.
- Thumbnails, the slide view, the show and the PDF all draw the same display
  list through `ol_scale` and `ol_draw_page`. Only the scale differs
  (`ol_scale_fit` for a thumbnail, `ol_scale_dpi` for the canvas).

A slide is laid out again only when it changes. The show lays the next slide
out while the current one is shown.

### 5.2 What GFOpenLayout needs: 0.3

GFOpenLayout's design says its display list will gain "clip push/pop,
transformed image, glyph run and path primitives where evidence requires
them" (`DESIGN.md` 7), and its rule is to "add layout capability because a
real GFOpen app needs it". Slides are that evidence:

| Need | Why | Proposal for GFOpenLayout 0.3 | Without it |
| --- | --- | --- | --- |
| Shapes other than rectangles | Ellipses, rounded rectangles, lines at an angle, arrows, polygons; chart wedges and lines (Shared, 4) | `OL_OP_SHAPE`: a kind, a box, points, fill and stroke colour, stroke width; made for a frame given a shape style | P1 draws only rectangles; other shapes are their box |
| Vertical anchoring | Titles are centred top to bottom in their box | `OL_BOX_VALIGN_CENTRE` and `_BOTTOM` flags | The deck lays out, reads the content height, moves the content |
| Clipping | Text that overflows a fixed box | `OL_BOX_CLIP` | Overflow is drawn |
| Rotation | Rotated text and pictures | Not in 0.3 | Drawn unrotated, kept for save, named in the report |

These additions are small and keep GFOpenLayout's ABI rule (new calls and flags
after the old ones). They are asked of gfopenlayout, not made in a fork.

### 5.3 Autofit

"Shrink text on overflow" is done by the deck, not by GFOpenLayout: lay out,
and if the text is taller than the box, lay out again at 90% of the size, up
to four times, then stop. That is at most five layouts of one frame, only
when its text changes.

## 6. The editor

### 6.1 The window

The Open-app rules from `UX_DESIGN.md` hold: 800 x 600 on first opening,
icons-only toolbar, every action in a menu, GFOpenLook's frame, a compact
status line.

```
+---------------------------------------------------------------------+
| GFOpenPresentation - Talk.pptx                                 system |
+---------------------------------------------------------------------+
| Project Edit View Insert Slide Format Arrange Show Tools Help       |
+---------------------------------------------------------------------+
| New Open Save Print | Undo Redo | Text Picture Shape Table | Show   |
+---------------------------------------------------------------------+
| Title | Font | 32 | B I U | L C R | bullets | level < >             |
+-----------+-------------------------------------+-------------------+
| Slides    |                                     | Slide             |
|  [  1  ]  |                                     | Object            |
|  [  2  ]  |            the slide                | Text              |
|  [  3  ]  |                                     | Datatypes         |
|           |                                     |                   |
|           +-------------------------------------+  inspector        |
|           | notes for this slide                |                   |
+-----------+-------------------------------------+-------------------+
| Slide 2 of 14 | Title and Content | PPTX | Ready.   GFOpenRTG | 75%   |
+---------------------------------------------------------------------+
```

- **Left, the slides:** thumbnails drawn from each slide's display list
  (GFOpenGadTools' list, as GFOpenWrite's page navigator draws pages). Drag to
  reorder; Delete deletes; Amiga-D duplicates.
- **Centre, the slide:** GFOpenGadTools' page view with one page.
- **Below it, the notes:** the slide's notes story, edited with GFOpenWrite's
  core.
- **Right, the inspector** (`ogt_sections`, as GFOpenWrite's): Slide (layout,
  background, transition, timing, hidden), Object (position, size, fill, line,
  crop, alt text), Text (level, bullet, alignment, spacing, font), Datatypes
  (the picture's type, size and details, as GFOpenWrite shows them).
- Below about 900 pixels the inspector hides; below about 560 the slide list
  hides too, as in GFOpenWrite.

### 6.2 Views

| View | What it shows | Phase |
| --- | --- | --- |
| Normal | Slides, the slide, notes, inspector | P3 |
| Slide Sorter | A grid of thumbnails; drag to reorder; transitions and timings set for several slides at once | P3 |
| Outline | Each slide's title and body text as one indented text; typing there changes the slides | P4 |
| Master | Masters and layouts, edited like slides (placeholders and fixed art only) | P4 |
| Notes | The notes pane made large | P3 |

### 6.3 Editing

GFOpenPresentation 0.3.4 delivers the first object-editing slice from this
design: a click selects one slide object; dragging moves it; eight handles
resize it; arrow keys nudge by one screen pixel and Shift-arrow by ten. A
completed gesture is one reusable `opr_obj_set_box` undo step, and ODP
round-trip tests cover its explicit geometry. Multi-selection, drag selection,
aspect-ratio locking, Tab traversal, arrange/align/group commands and the
inspector described below remain planned.

GFOpenPresentation 0.3.5 moves rectangles, rounded rectangles, ellipses,
triangles, diamonds, block arrows, lines and polygons into OpenLayout 13's
normalized shape operations. The Amiga screen and PDF renderer now consume the
same geometry, fill, stroke, dash and arrow metadata; GFOpenPublisher inherits
that path without a Publisher-specific drawing implementation.

GFOpenPresentation 0.3.8 adds the first authored drawing-object lifecycle:
the Insert menu creates any supported primitive shape in the reusable editor
core, selects it for the existing move/resize/rotation tools, and Object >
Delete removes an ungrouped object. Insertion and deletion are ownership-safe
undo steps; undoing a deletion restores the complete object, and ODP round-trip
tests cover the authored shape.

GFOpenPresentation 0.3.9 adds Object > Duplicate to that lifecycle. The
portable libowf 1.4.2 model deep-copies every allocation owned by an object,
clears file-local IDs, and the presentation core resolves inherited geometry,
offsets and clamps the independent copy, selects it, and records the insertion
as one undoable presence change. GFOpenPublisher 0.1.4 consumes the same path.

GFOpenPresentation 0.4.0 and GFOpenPublisher 0.1.5 add the four drawing-order
commands: Bring to Front, Bring Forward, Send Backward and Send to Back. The
portable core moves one top-level object as a single undo step, repairs group
parent indexes, and crosses a group only as a complete block. Group and child
selection remain deliberately refused until whole-group editing is delivered.

GFOpenPresentation 0.4.1 and GFOpenPublisher 0.1.6 add Align Left, Centre,
Right, Top, Middle and Bottom against the slide/page. The reusable core
resolves an inherited box, changes only the required axis through the existing
one-step geometry history, and makes the result explicit for ODP persistence.
No-op alignment consumes no undo entry. Multi-object alignment remains later.

GFOpenPresentation 0.5.0 and GFOpenPublisher 0.1.7 add Object > Appearance for
a selected top-level text box or shape. The shared requester edits solid fill,
stroke colour and width, solid/dashed/dotted/dash-dot line style and start/end
arrowheads. The portable core validates and swaps the complete `owf_look` as
one undo/redo step; ODP round-trip tests cover every edited field. Group-wide
appearance and richer fills remain later work.

GFOpenPresentation 0.5.1 and GFOpenPublisher 0.1.8 expose the existing reusable
box-edit operation as Object > Position and Size. A selected top-level object's
resolved left, top, width and height are entered exactly in slide/page twips,
validated against the page bounds, and committed as one undo step. An inherited
placeholder becomes an explicit box. Multi-selection and aspect locking remain
later work.

GFOpenPresentation 0.6.0 and GFOpenPublisher 0.2.0 add Insert > Text Box. The
portable core creates an owned `OWF_OBJ_TEXT` with validated page geometry,
insets, vertical alignment, fill/stroke look and an independent UTF-8 story as
one object-presence undo step. The native shared window selects it and enters
typing immediately. ODP round-trip coverage verifies the authored frame rather
than only imported text.

GFOpenPresentation 0.6.1 and GFOpenPublisher 0.2.1 add Insert > Picture. The
shared native requester reads up to 32 MB, asks datatypes for the natural
dimensions, preserves the aspect ratio while fitting the picture to the
slide/page and calls `opr_obj_insert_picture`. That portable operation owns
the compressed resource bytes, name, MIME type, alt text, geometry and crop,
and commits object plus resource transactionally as one undoable presence
change. The authored picture uses the existing OpenLayout crop/display data,
datatype screen cache, PDF renderer and ODP round trip.

GFOpenPresentation 0.6.2 and GFOpenPublisher 0.2.4 add shared paragraph-range
rich-text commands in portable core 0.9.0. Bold, italic and underline toggle
as one text-snapshot Undo step; font size and left, centre, right or justified
alignment are set explicitly. The native Format menu applies them to the
current caret paragraph. Existing OpenLayout run/paragraph styling renders the
result and ODP round-trip tests verify its effective appearance.

GFOpenPresentation 0.6.3 and GFOpenPublisher 0.2.5 add equal text-frame columns
through shared core 0.10.0 and GFOpenLayout 0.15. The one/two/three-column
Format commands, Undo/Redo snapshot, resolved object geometry, ODP
`style:columns` persistence and PPTX DrawingML import are common. The deck
adapter passes column count and gap to the layout box; the applications do not
split lines or position column glyphs themselves.

GFOpenPresentation 0.6.4 and GFOpenPublisher 0.2.7 share object wrap metadata,
Undo/Redo, ODP interchange and deck-to-layout translation through portable
core 0.12.0. Publisher exposes the authoring menu; both applications render
imported square and top-and-bottom exclusions through GFOpenLayout 0.16. The
deck adapter supplies only overlapping geometry and does not implement a
second line fitter.

GFOpenPresentation 0.6.5 and GFOpenPublisher 0.2.9 add shared Text Frame
settings through portable core 0.13.0: column count, exact column gap and four
internal margins change atomically. Existing ODP padding/column persistence and
GFOpenLayout frame geometry remain the single interchange and layout paths.

GFOpenPresentation 0.6.6 and GFOpenPublisher 0.3.0 add exact RGB text colour
through portable core 0.14.0, the existing character-run renderer and ODP
interchange. It is undoable and reaches screen and PDF from the same model.

GFOpenPresentation 0.6.7 and GFOpenPublisher 0.3.1 add named Font Family
authoring through portable core 0.15.0 and the deck's existing shared font
table. The existing screen/PDF font resolver and ODP font-face interchange
remain authoritative.

GFOpenPresentation 0.6.8 and GFOpenPublisher 0.3.2 add character-range
selection through portable core 0.16.0. Drag selection, Shift+Arrow and Select
All share one anchor/focus model; replacement typing and cross-paragraph
deletion use the existing text snapshot Undo. Exact run slices receive bold,
italic, underline, size, RGB colour and font family, while paragraph alignment
continues to apply to the paragraphs touched by the range. Highlight geometry
comes from GFOpenLayout's existing caret positions.

GFOpenPresentation 0.6.9 carries shared core 0.17.0, whose ordered linked-frame
membership now supports Publisher's middle detach and insertion commands. This
does not add a Presentation-facing flow menu or an OpenLayout API; it keeps the
common deck model and ODP continuation semantics aligned between the apps.

- A click selects an object; Shift-click adds to the selection; a drag on the
  empty slide draws a selection box.
- A drag moves; eight handles resize; Shift keeps the shape; the arrow keys
  move one pixel, Shift and an arrow ten.
- A double-click or Return on a text object starts typing in it: GFOpenWrite's
  editor core on that frame's story (Shared E6). Esc stops typing.
- Tab and Shift-Tab step through the slide's objects.
- Insert: New Slide (with a layout chooser), Duplicate Slide, Text Box,
  Picture..., Shape, Table..., Chart from Sheet... (Shared, 3 and 4).
- Arrange: Bring to Front, Forward, Backward, Send to Back; Align left,
  centre, right, top, middle, bottom; Distribute; Group, Ungroup; a snap grid.
- **Undo** is per slide: before an edit, a copy of that slide's objects and
  stories, never of the pictures (they are shared by index, and a picture no
  longer used is dropped only on save). Sixteen steps, as GFOpenWrite.
- **Clipboard:** objects and slides go out as FTXT (their text, for every
  Amiga program) with GFOffice's private chunk inside the same FORM, so
  another GFOffice program gets them whole (Shared, 5). A picture alone goes out
  as ILBM through its datatype, which any paint program reads.

## 7. The show

GFOpenPresentation 0.3.1 delivers the first player backend: a borderless
public-screen window using the editor's exact slide painter, with cut,
directional wipe, push and deterministic dissolve transitions. Two off-screen
friend bitmaps are used when they fit; allocation failure or an over-budget
frame becomes a cut rather than a slow crawl. `src/deck/opr_show.c` is the
reusable, host-tested state machine and transition-geometry layer
for visible/hidden navigation, looping and automatic timing; the later
own-screen and ARexx backends use it rather than duplicating those
rules. The remaining subsections describe that delivered base and the still
planned own-screen extension.

GFOpenPresentation 0.3.2 adds the editor side: Slide > Slide Transition sets
the effect, direction, duration and optional automatic advance delay for one
slide or every slide. `opr_slide_set_transition` validates and applies that
state as one reusable undo step, including apply-to-all restoration of each
slide's former setting. ODP save/readback and host tests cover the edited
state; the player consumes the same model without a second settings layer.

GFOpenPresentation 0.3.3 completes the modelled transition player set. The
portable core produces complete per-frame plans for wipe, push, cover,
uncover, centre split and eight-strip blinds, including each direction, and a
four-row ordered-dither mask for crossfade and fade-through-black. The Amiga
backend executes those plans against the two rendered slide bitmaps; dissolve
retains its deterministic shuffled tiles. This keeps the semantics host-
testable while supporting planar and RTG screens without requiring an alpha
compositor. Allocation failure or a frame that misses its budget still falls
back to the complete new slide.

### 7.1 Where it shows

| Choice | When | How |
| --- | --- | --- |
| Its own screen | The default on AGA, and when the Workbench screen has fewer than 256 colours or is smaller than 640 x 480 | `BestModeID` for the slide's shape, at least 640 x 480; 8 bits on AGA, 16 or 24 on RTG |
| The Workbench screen | The default when Workbench is an RTG screen of 16 bits or more | A borderless backdrop window covering the screen; no screen switch |

Prefs can choose either. The show starts with View > Show (F5) from the first
slide, or Shift-F5 from the current one, or from the Shell or an icon
(`SHOW` and `LOOP` in section 9).

### 7.2 Drawing

Each slide is drawn into an off-screen bitmap like the screen's, then copied
on. The next slide is drawn while the current one is shown, so a key press
shows it at once. Two bitmaps cost:

| Screen | One bitmap | Two |
| --- | --- | --- |
| 640 x 480, 8 bits (AGA) | 300 KB | 600 KB |
| 800 x 600, 16 bits | 938 KB | 1.9 MB |
| 1024 x 768, 16 bits | 1.5 MB | 3 MB |
| 1920 x 1080, 32 bits | 7.9 MB | 15.8 MB |

When two don't fit in a quarter of free memory, the show draws one slide at a
time, and the next appears when it is drawn.

### 7.3 Keys and pointer

| Key | Does |
| --- | --- |
| Space, Return, Right, Down, Page Down, left click | Next (the next build in P4, else the next slide) |
| Backspace, Left, Up, Page Up | Previous |
| Home, End | First, last slide |
| A number, then Return | That slide |
| B, W | Black screen, white screen; again to come back |
| H | Show the next hidden slide |
| Esc | End the show |

The pointer hides after two seconds without moving. On its own screen the
show has a menu (right button): Next, Previous, Go to Slide, Black, End.

### 7.4 Timings

- A slide advances by itself after `advance_after`, from the file or set in
  the Slide inspector.
- **Rehearse Timings** runs the show and records how long each slide stayed.
- **Loop** repeats until Esc, for a kiosk.
- Timing comes from timer.device, so it doesn't drift with drawing.

### 7.5 Transitions

| Transition | How it is drawn | AGA | RTG |
| --- | --- | --- | --- |
| Cut | One copy | Yes | Yes |
| Wipe, Push, Cover, Uncover, Split, Blinds | Rectangles copied from the two bitmaps (`BltBitMapRastPort`), 12 to 20 steps | Yes | Yes |
| Dissolve | 16 x 16 blocks in a fixed shuffled order | Yes | Yes |
| Fade through black | A four-row ordered mask fills black, then mask-blits the new bitmap (`BltMaskBitMapRastPort`) | Yes | Yes |
| Fade (one slide into the next) | The same monotonically growing ordered mask blits the new bitmap over the old | Yes | Yes |
| Others in the file | Shown as a cut, kept for save | | |

Each step is timed. When a step takes longer than its share of the duration,
the rest are skipped and the slide appears: a slow machine shows a cut, never
a slow crawl. Nothing new is asked of GFOpenRTG: the show uses graphics.library
and cybergraphics.library calls, as GFOpenGadTools already does. If GFOpenGfx
(gfopengpu.library) later offers blending, fades use it. GFOpenRTG is Team 4's
area, and this design doesn't touch it.

## 8. Printing and PDF

- **Export PDF** writes one page per slide at the slide's size, or handouts.
  It uses the shared page-drawing PDF writer (Shared E4), fed by the same
  display list as the screen, so a PDF looks like the slide. Text stays
  searchable (base-14 fonts, as now); JPEG goes in as it is and PNG is
  decoded, as `out_pdf.c` does today.
- **Print...** does what GFOpenWrite does (`app/ow_print.c`): the PDF to
  `T:<name>.pdf`, then `C:GFOpenPrint <file>` started on its own. GFOpenPrint's
  window chooses the printer, copies and pages. Without GFOpenPrint, Print says
  it needs GFOpenPrint and Export PDF still works. There is no printer.device
  text route for slides: their text alone is not a printout.
- **Handouts:**

| Handout | Page |
| --- | --- |
| 1, 2, 4, 6 or 9 slides a page | Slides in a grid, framed, numbered |
| 3 slides a page | Slides on the left, ruled lines for notes on the right |
| Notes pages | The slide on the top half, its notes below |
| Outline | Titles and body text as a GFOpenWrite document, then its PDF |

A4 or Letter, portrait or landscape, a header and footer with the date and
page number (`notes_page`).

## 9. ARexx

The port is `GFOPENPRESENTATION`, following GFOffice's conventions (Shared, 6):
commands read with `ReadArgs` templates, results in `RESULT`, return codes 0,
5, 10 and 20.

| Command | Template | Does |
| --- | --- | --- |
| VERSION | | The version string |
| OPEN | FILE/A | Opens a deck (refused with unsaved changes, as GFOpenWrite) |
| SAVE / SAVEAS | SAVEAS FILE/A | Saves |
| EXPORT | FILE/A,FORMAT/K | PDF, ODP, PPTX, or ILBM pictures to a drawer (P4) |
| PRINT | HANDOUT/K/N,NOTES/S | Through GFOpenPrint |
| SLIDECOUNT | | The number of slides |
| GOTOSLIDE | NUMBER/A/N | Shows a slide in the editor |
| NEWSLIDE | LAYOUT/K,AT/K/N | Adds a slide |
| DELETESLIDE, MOVESLIDE | NUMBER/A/N; FROM/A/N,TO/A/N | |
| SETTEXT, GETTEXT | SLIDE/A/N,PLACEHOLDER/A,TEXT/F | A placeholder's text (title, subtitle, body, notes); newlines make paragraphs, leading tabs make levels |
| INSERTPICTURE | FILE/A,SLIDE/K/N | A picture, fitted to the body placeholder |
| SHOW | FROM/K/N,LOOP/S | Starts the show |
| NEXT, PREVIOUS, ENDSHOW | | Drives a running show |
| QUIT | FORCE/S | |

Enough for a script to turn a drawer of photos into a captioned deck, or to
run a kiosk.

## 10. Memory and speed

| Machine | Decks | Show |
| --- | --- | --- |
| 8 MB Fast RAM | Up to about 30 slides with a few small pictures | 640 x 480 at 8 bits |
| 16 MB | About 100 slides with photos | 800 x 600 or 1024 x 768 at 16 bits |
| 32 MB | Larger decks, full-screen photos | Up to 1920 x 1080 at 16 bits, one slide ahead |

These are targets. P1 measures what a real deck costs on the host; P3
measures on the lab's 68040.

**Pictures are the cost.** Text and structure are small: a 40-slide text deck
is tens of KB. A picture is held compressed, as `owf_doc` holds it. To draw
it, GFOpenWrite today writes it to `T:`, decodes it with a datatype and reads
it as ARGB at full size, `width x height x 4` bytes, keeping up to 16 such
pictures (`app/openwrite2.c`, `image_cache_get`). A 12-megapixel phone photo
is 48 MB that way, more than a 32 MB machine has. So:

1. Before decoding, read the size from the picture's header (the datatype's
   `BitMapHeader`, as GFOpenWrite does on Insert).
2. Decode only if the full size fits in a quarter of the largest free block.
   Scale it at once to the size the slide needs on this screen; free the full
   size.
3. Keep the scaled copies in a cache with a budget (a quarter of free Fast
   RAM at start, at least 512 KB). The show keeps the current and the next
   slide's pictures.
4. A picture too big to decode is drawn as a labelled box ("Photo.jpg, 4000 x
   3000: too big for this machine's memory"). Insert Picture offers to store
   a smaller copy instead.

This picture cache is the same one GFOpenWrite needs (Shared E9). Whether to
write our own JPEG decoder that decodes at 1/2, 1/4 or 1/8 size (so a big
photo never needs its full size in memory) is a question for Dale.

**Thumbnails** are 128 x 96 at 8 bits (12 KB each), drawn for the slides in
view and kept in a cache with a limit.

**Size.** The same goal as GFOpenWrite (`NATIVE_CORE.md` 12): the program under
2 MB, with owf.library shared.

## 11. Where the code lives

For now, in openamigawrite beside GFOpenWrite and GFOpenCalc, as GFOpenCalc is:

| What | Where |
| --- | --- |
| Model and formats | `libowf/src/owf_deck.c`, `in_odp.c`, `out_odp.c`, `in_pptx.c`, `out_pptx.c` |
| Portable core: editing, slide layout to GFOpenLayout, show timing, handout geometry | `src/deck/` |
| Amiga program | `app/openpresent*.c` |
| Tests | `tests/deck/` (`make deck-test`, `make deck-check`) |

A repository of its own can come later (section 14).

## 12. Phases

| Phase | Delivers | Tested by | Needs first (Shared) |
| --- | --- | --- | --- |
| **P1, the model and ODP** | `owf_deck`; ODP open and save; PDF export of slides and handouts; `OWConvert Talk.odp Talk.pdf`. No window | Host tests: fixtures made by `make_fixtures.py` (a flat ODP written by hand, then saved as ODP and PPTX by LibreOffice, as `tests/book/make_fixtures.py` does for `book.xls`); ODP to ODP round trips; every cut-short copy of each sample gives an answer, never a crash (as `tests/run_tests.py` does); ASan and UBSan; the PDFs checked with `qpdf --check` and `pdfinfo` | E1, E3 (owf.library 2), E4, E5, E8 |
| **P2, PPTX and the player** | PPTX open and save; GFOpenLayout 0.3 shapes; a player: open, full-screen show with cut, wipe, push and dissolve, keys, timings, loop; Print through GFOpenPrint; New deck from pictures | Host: PPTX fixtures and round trips, PPTX to ODP to PPTX; slide geometry checked numerically (op positions per slide, as GFOpenLayout's `tests/test_document.c` does); the show's timing logic host-tested. Lab: a PPTX made in PowerPoint shown on an RTG screen and on AGA | E2 (inflate), E9 |
| **P3, the editor** | Normal, Slide Sorter and Notes views; inspector; text editing with GFOpenWrite's core; pictures, shapes, tables; per-slide undo; clipboard; save | Host: editing operations and undo in `src/deck` tests. Lab: build a 10-slide deck from nothing, save as PPTX and ODP, reopen | E6, E7, E10 |
| **P4, the rest** | Master and Outline views; every transition in 7.5 including fades; builds (appear, fade in); Rehearse Timings; every handout; the whole ARexx port; slides as ILBM pictures; AGA tuning | Host: the ARexx command parser; handout geometry. Lab: fades timed on RTG and AGA; a script that builds and shows a deck | E14 |

## 13. Risks

- **PPTX is wide.** We aim at the decks people make (titles, bullets,
  pictures, simple shapes, tables), test against real files, and name in the
  report what was dropped or only kept.
- **Fonts.** A PPTX in Calibri is drawn with the Amiga fonts GFOpenWrite maps
  to (`document_font_file`: courier, CGTimes, helvetica, CGTriumvirate). Lines
  may break differently from PowerPoint and text may overflow its box.
  Autofit and the report help; metric-compatible fonts are a later step.
- **Photos and memory.** Section 10. The labelled box keeps the program alive;
  the decoder question decides how far we go.
- **Fades on RTG.** Measured in P4; a slow fade becomes a cut.
- **Animations.** Kept and written back, but not played beyond appear and fade
  in. A deck that relies on them shows its final state.

## 14. Questions for Dale

1. A new deck saves as ODP (as GFOpenWrite's default is ODT), or as PPTX?
2. The code lives in openamigawrite beside GFOpenWrite and GFOpenCalc for now, or in a new repository (`DalsinAI/openamigapresentation`)?
3. Keep the name GFOpenPresentation and the ARexx port `GFOPENPRESENTATION`?
4. Ship the player (P2: open, show, print) before the editor (P3)?
5. The show's default place: its own screen, or the Workbench screen when that is RTG?
6. Photos too big for memory: refuse with a message, or write our own JPEG decoder that decodes at reduced size (about 1,000 lines, from ITU-T T.81)?
7. Do you have Scala or other Amiga presentation programs or files for the format lab? If not, we leave them out.
8. May we add the shape op, vertical anchoring and clipping to gfopenlayout as GFOpenLayout 0.3?
9. PPTX and ODP output must be checked in PowerPoint or LibreOffice: will you open our test files, or may we use LibreOffice on your PC (as `make_fixtures.py` does)?
10. Builds: only "appear" and "fade in" played in P4, the rest kept but not played. Is that enough?
