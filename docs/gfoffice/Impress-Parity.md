# GFOpenPresentation and Apache OpenOffice Impress: a feature checklist

Version 0.1 | 25 October 2026 | AmiCradle Team, MIT

Dale, 25 October 2026: "The spec we are chasing is to mirror what Apache
OpenOffice does in all its apps." This is the list for Impress, which
GFOpenPresentation answers to. It is made from Impress's documented,
user-facing behaviour: Apache OpenOffice 4.1's Help, the AOO4 user guides
on wiki.openoffice.org (*Getting Started with Impress*, the *Impress Guide*)
and OASIS ODF 1.2 for what the files hold. No OpenOffice or LibreOffice code
was read. Parity is in features, not in Impress's size or look: the program
stays a small 68040 one with the Amiga's look (GFOpenGadTools).

Each row says what Impress does, the phase that brings it to
GFOpenPresentation (Design-OpenPresentation.md, section 12), and where phase
P1 (the deck model, ODP open and save, PDF export: no window yet) leaves it.

**P1 column:**
- **Yes**: in the model, read and written in ODP, and drawn in the PDF
  where it shows.
- **Model**: in the model and in ODP both ways; nothing uses it yet (the
  player and editor come in P2 and P3).
- **Kept**: read and written back as the file had it, not edited (and drawn
  only as a box, if at all).
- **Approx.**: read, and drawn or saved the nearest way; the import report
  says so.
- **No**: not yet.

## 1. Files

| Impress | Phase | P1 |
| --- | --- | --- |
| Open and save ODF Presentation (`.odp`) | P1 | Yes |
| ODF Presentation template (`.otp`) | P1 | Yes (opened; saved as `.otp` by name) |
| Flat XML ODF Presentation (`.fodp`) | P1 | Yes; embedded objects' parts are not kept in flat XML (reported) |
| Microsoft PowerPoint 2007-365 (`.pptx`, `.potx`, `.ppsx`) open | P2 | No in P1. P2: Yes (`in_pptx.c`; what is approximated is reported, see FORMATS.md); saved as ODP |
| Save as PowerPoint 2007-365 | P2 | No |
| PowerPoint 97-2003 (`.ppt`, `.pps`, `.pot`) open and save | Later | No |
| OpenOffice.org 1.x (`.sxi`), StarImpress | Later, if samples appear | No |
| Export as PDF (Export as PDF..., Export Directly as PDF) | P1 | Yes: a page a slide, searchable text, JPEG and PNG pictures, hidden slides left out |
| PDF options: page range, notes pages, handouts, hidden slides, bookmarks, tagged PDF, image compression | P2-P4 | No |
| Export slides as pictures (PNG, JPEG, GIF, BMP, SVG...) | P4 (IFF ILBM through datatypes) | No |
| Export as HTML (Web Page wizard), Flash | Not planned | No |
| Document properties: title, subject, keywords | P1 (title) | Title yes; the rest No |
| Templates (New from template, save as template) | P3 | Model (masters and layouts come with a deck) |
| Send by e-mail | Not planned | No |

## 2. Slides and pages

| Impress | Phase | P1 |
| --- | --- | --- |
| New slide, delete, duplicate, rename | P3 | Yes in GFOpenPresentation 0.2.3; all undoable, with deep-copy duplication in libowf |
| Move slides (Slide Sorter, drag) | P3 | Move up/down: Yes in GFOpenPresentation 0.2.3, undoable through `opr_slide_move`; drag: No |
| Hide and show a slide | P2 (show), P3 (edit) | Yes in GFOpenPresentation 0.3.0: read, written, omitted from PDF and normal show navigation, and edited with undo/redo |
| Slide layouts (Blank, Title Slide, Title Content, Title and 2 Content, Title Only, Centered Text, and the rest) | P3 | Model: a new deck has six; a file's layouts are read and written with their placeholders |
| Page setup: slide size (Screen 4:3, 16:9, 16:10, A4, Letter, custom), orientation, numbering format | P3 | Model: any size, orientation from it; numbering format No |
| Slide background: colour | P1 | Yes |
| Background: gradient, hatching | Later | Approx.: drawn and saved in one colour |
| Background: bitmap | P1 | Yes (stretched) |
| Insert slide from file, expand slide, summary slide | Later | No |
| Header and footer: date (fixed or current), footer text, slide number, per slide or all | P1 (data), P3 (dialog) | Yes: read, written, drawn in the master's boxes |

## 3. Masters and styles

| Impress | Phase | P1 |
| --- | --- | --- |
| Slide masters (several in a deck), apply a master to slides | P1 (data), P4 (Master view) | Yes: read, written, slides keep theirs |
| Master's fixed art (logos, bands) | P1 | Yes |
| Master's placeholders: title, outline, date, footer, number | P1 | Yes |
| Presentation styles: title, subtitle, outline 1-9, notes, background objects | P1 | Yes: each a text level of the master |
| Graphic styles | P1 (as looks) | Approx.: resolved into each object's look; the file's defaults kept |
| Notes master, handout master | P2-P4 | Notes page size; the rest No |

## 4. Text

GFOpenPresentation 0.6.2 edits the current caret paragraph's bold, italic,
underline, font size and alignment through reusable core 0.9.0, with Undo and
ODP persistence. Character-range selection and font/colour requesters remain.
GFOpenPresentation 0.6.3 adds one, two and three equal columns to selected text
frames through shared core 0.10.0 and GFOpenLayout 0.15, including Undo/Redo,
ODP save/reload and PPTX column import.
GFOpenPresentation 0.6.4 shares core 0.12.0's object-wrap model, ODP
interchange and GFOpenLayout 0.16 exclusion rendering with Publisher. Publisher
exposes the authoring menu; Presentation renders imported settings.
GFOpenPresentation 0.6.5 adds a shared core 0.13.0 Text Frame dialog for one to
sixteen columns, exact gap and four internal margins, with one-step Undo/Redo
and the existing ODP/GFOpenLayout paths.
GFOpenPresentation 0.6.6 adds exact RGB Text Colour through shared core 0.14.0,
with Undo/Redo, screen/PDF rendering and ODP round trip.
GFOpenPresentation 0.6.7 adds named Font Family authoring through shared core
0.15.0, the existing deck font table and ODP font-face round trip.
GFOpenPresentation 0.6.8 adds exact character-range selection through shared
core 0.16.0: mouse drag, Shift+Arrow, Select All Text, replacement typing,
cross-paragraph deletion, run-level formatting, Undo/Redo and ODP round trip.
GFOpenPresentation 0.6.9 carries shared core 0.17.0's ordered linked-frame
membership for Publisher; it adds no Impress-facing command.
GFOpenPresentation 0.6.14 carries shared core 0.22.0 and reuses the
OpenGadTools ruler for undoable left, first-line/hanging and right paragraph
indents, including ODP persistence.
GFOpenPresentation 0.6.15 carries shared core 0.23.0 and exchanges one
supported top-level object with Publisher through the Amiga system clipboard,
including font and embedded-picture resource remapping and Undo/Redo.
GFOpenPresentation 0.6.16 carries shared core 0.24.0 and adds Replace plus an
atomic, case-insensitive Replace All for the active text box. Both use the
existing exact selection/story model and OpenLayout reflow.
GFOpenPresentation 0.6.17 carries shared core 0.25.0 and adds optional
half-inch grid display plus magnetic snapping for object moves and all eight
resize handles. The grid is editor chrome and is not exported.

| Impress | Phase | P1 |
| --- | --- | --- |
| Text boxes, placeholder text | P1 | Yes; GFOpenPresentation 0.6.0 adds native Insert > Text Box with immediate typing, one-step undo/redo and ODP persistence |
| Typing in a text box: click to put the caret, select, type, Return, Backspace, Del, arrow keys | P3 | Yes: caret typing plus drag/Shift+Arrow/Select All ranges, replacement typing and exact run formatting in GFOpenPresentation 0.6.8 (`opr_edit.c`) |
| Font, size, bold, italic, underline, strike-through, raised and lowered, colour | P1 | Yes |
| Shadow, outline, relief, character spacing, highlighting, case | Later | No |
| Paragraph alignment, indents, spacing above and below, line spacing, tabs | P1 | Yes |
| Paragraph background and borders | P1 | Yes (as GFOpenWrite holds them) |
| Bullets and numbering: character bullets, numbers (1, a, A, i, I) with prefix and suffix, start value, size, colour, bullet font | P1 | Yes |
| Picture bullets | Later | Approx.: a round bullet |
| Outline levels 1-9, promote and demote | P1 (levels), P3 (commands) | Yes (until Shared E8 puts levels in paragraphs, a frame holds its paragraphs' levels) |
| Autofit: shrink text on overflow | P1 | Yes: read, written, laid out (up to four times by 90%) |
| Autofit: grow the box with the text, fit text to frame | P1 (grow), Later (fit) | Grow: Model; fit: No |
| Vertical anchoring (top, middle, bottom), text insets | P1 | Yes |
| Hyperlinks in text | P1 | Yes, and links in the PDF |
| Fields: date, time, slide number, slide count, author, file name | P1 (date, number, count) | Date, number and count yes; others No |
| Text animation (scrolling text), Fontwork | Later | No (Fontwork shapes are kept) |
| Find and replace, spelling, autocorrect, thesaurus, hyphenation | P3, P4 (Shared E11) | Find/Find Next across the deck and Replace/Replace All in the active text box: Yes in 0.6.16; spelling, autocorrect, thesaurus and hyphenation: No |
| Pictures inside text | Later | No (reported) |

## 5. Drawing objects

| Impress | Phase | P1 |
| --- | --- | --- |
| Rectangle, square, rounded rectangle | P1 | Yes; rectangle and rounded rectangle can also be inserted and deleted with undo/redo in 0.3.8 |
| Ellipse, circle | P1 | Yes; ellipse can also be inserted and deleted with undo/redo in 0.3.8 |
| Arcs, segments, sections of circles | Later | Kept |
| Lines, with arrowheads | P1 | Yes |
| Basic shapes: triangle, diamond | P1 | Yes; both can also be inserted and deleted with undo/redo in 0.3.8 |
| Block arrows: right and left | P1 | Yes; a right arrow can also be inserted and deleted with undo/redo in 0.3.8 |
| Other basic shapes, symbol shapes, block arrows, flowchart, callouts, stars and banners | Later | Kept (drawn as their box with their fill) |
| Polygons | P1 | Yes |
| Polylines, curves, freeform lines | Later | Kept |
| Connectors, glue points | Later | Kept |
| 3-D objects, extrusion | Not planned | Kept |
| Line: colour, width, solid or dashed; area: colour | P1 | Yes in the model and renderer; GFOpenPresentation 0.5.0 adds undoable UI editing of solid fill, line colour/width, solid/dashed/dotted/dash-dot style and start/end arrowheads for one top-level text box or shape |
| Area: gradient, hatching, bitmap, transparency; shadow | Later | Approx.: one colour; transparency and shadow No |
| Position and size | P1 | Yes: GFOpenPresentation 0.3.4 added single-object drag move, eight resize handles, one/ten-pixel keyboard nudge and undo/redo; 0.3.9 added independent deep-copy duplication with a clamped offset; 0.5.1 adds exact numeric left, top, width and height; 0.6.17 adds an optional half-inch grid with move/resize snapping. Multi-selection, arbitrary guides and aspect lock remain |
| Rotation | P1 | Shape outlines are rotated through OpenLayout 0.14 and saved to ODP; GFOpenPresentation 0.3.7 adds undoable 15-degree left/right rotation for a selected shape. Shape text, pictures, groups, exact angle entry and rotation handles remain |
| Flip, slant | P1 (flip of lines and arrows) | Lines and arrows yes; slant: Approx. (drawn in its box) |
| Text in shapes | P1 | Yes |
| Group, ungroup, enter group | P1 (groups), P3 (commands) | Yes |
| Arrange (front, back), align, distribute | P3 | Bring to Front, Bring Forward, Send Backward and Send to Back: Yes in 0.4.0 for one top-level object, undoable and group-block safe. Align Left/Centre/Right/Top/Middle/Bottom to the slide/page: Yes in 0.4.1. Multi-object align and distribute: No |
| Combine, split, shape operations (merge, subtract, intersect) | Not planned | No |
| Name and description (alt text) | P1 | Yes |
| Snap, grid, guides, rulers | P3 | Text-indent ruler: Yes; snap, grid and guides: No |

## 6. Pictures

| Impress | Phase | P1 |
| --- | --- | --- |
| Insert picture from file | P3 | Yes in GFOpenPresentation 0.6.1: datatype-supported files are embedded, aspect-fitted, selected, undoable and saved to ODP through the shared Presentation/Publisher core |
| Crop | P1 | Yes: read, written, and drawn on screen and in PDF from shared OpenLayout metadata |
| Resize, keep ratio | P3 | Model |
| Colour modes (greyscale, black and white, watermark), filters, transparency | Later | No |
| Linked pictures | Later | No (reported) |
| A picture in several versions (SVG with a PNG) | P1 | Approx.: the one the PDF can draw is kept |
| Pictures other than JPEG and PNG in the PDF | P2 (datatypes) | No (left out, reported) |
| Image map | Not planned | No |

## 7. Tables

| Impress | Phase | P1 |
| --- | --- | --- |
| Insert a table, type in its cells | P1 (data), P3 (editing) | Yes |
| Column widths | P1 | Yes |
| Cell fill | P1 | Yes |
| Merge and split cells | Later | Approx.: shown apart |
| Borders per cell, table designs (templates), banding | Later | Approx.: thin borders on every cell |
| Insert and delete rows and columns, row heights | P3 | No |

## 8. Charts, objects, media

| Impress | Phase | P1 |
| --- | --- | --- |
| Charts (column, bar, line, area, pie, XY and others), their data | P2-P4 (Shared, section 4) | Kept, with their parts; drawn as their replacement picture |
| Spreadsheet, formula, drawing objects (OLE) | Later | Kept, as charts are |
| Sound and video | Later | Kept, shown as a box |
| Floating frames, plug-ins, applets | Not planned | Kept |

## 9. Notes, handouts, outline, printing

| Impress | Phase | P1 |
| --- | --- | --- |
| Speaker notes, per slide | P1 | Yes (in the model and ODP) |
| Notes view, notes pages printed | P3, P4 | No |
| Handouts: 1, 2, 3, 4, 6 or 9 slides a page | P4 (P1 design asks for them) | No |
| Outline view | P4 | No |
| Print slides, notes, handouts, outline (through GFOpenPrint) | P2 (slides), P4 | Slides: Yes, through GFOpenPrint; notes, handouts and outline: No |

## 10. Slide show

| Impress | Phase | P1 |
| --- | --- | --- |
| Start from the first slide, from the current | P2 | Yes: Show menu, F5 and Shift-F5 |
| Next, previous, first, last, go to a slide by number, black and white screen, end | P2 | Yes in the borderless public-screen player; H explicitly visits the next hidden slide |
| Advance on click, or automatically after a time | P2 | Yes, through the host-tested reusable `opr_show` state machine |
| Rehearse timings | P4 | No |
| Slide show settings: range, loop with a pause, pointer visible, pen, navigator, animations allowed, slides change on click, always on top | P2, P4 | Loop and first/current range: Yes; the rest kept or No |
| Custom slide shows | Later | Kept |
| Hidden slides left out of the show | P2 | Yes; normal movement skips them and H visits the next one explicitly |
| Presenter console | Not planned | No |
| Interactions (on click: go to a slide, open a file, play a sound, end the show) | Later | No |

## 11. Transitions

| Impress | Phase | P1 |
| --- | --- | --- |
| Transitions per slide, apply to all | P1 (data), P3 (editing) | Yes in GFOpenPresentation 0.3.2: effect, direction, duration and automatic delay are editable per slide or for every slide as one undoable operation, and saved in ODP |
| Cut, wipe, push, cover, uncover, split, blinds, dissolve | P2 (wipe, push, dissolve), P4 (the rest) | Yes in GFOpenPresentation 0.3.3: directional geometric effects and deterministic dissolve are animated by reusable frame plans |
| Fade smoothly, fade through black | P4 | Yes in GFOpenPresentation 0.3.3, as ordered masked fades that work on planar and RTG screens |
| The other effects (wheel, shape, box, checkerboard, circle, newsflash, random...) | Later | Kept, shown as a cut |
| Speed (slow, medium, fast) | P2 | Yes as an exact editable duration in milliseconds |
| Sound with a transition | Later | No |

## 12. Custom animation

| Impress | Phase | P1 |
| --- | --- | --- |
| Entrance, emphasis, exit effects, motion paths | P4 ("appear" and "fade in"), Later | Kept, written back while objects keep their ids |
| Start on click, with previous, after previous; timing; triggers | P4 | Kept |

## 13. Views and tools

| Impress | Phase | P1 |
| --- | --- | --- |
| Normal view (slides, slide, notes) | P3 | No. P2: the slide at the window's size with the slides beside it; no notes pane |
| Slide Sorter | P3 | No. P2: a strip of slides to pick from; moving them is P3 |
| Master view | P4 | No |
| Zoom, panels that hide on narrow screens | P3 | No |
| Navigator, gallery, styles, sidebar decks | Later | No |
| Undo and redo | P3 (per slide) | Text editing, slide insertion/deletion/movement, and object insertion/duplication/deletion/position/size/shape rotation/drawing order/alignment/appearance: Yes (`opr_edit.c`); other object edits: No |
| Clipboard: objects and slides | P3 (Shared E10) | Top-level supported objects: system Cut/Copy/Paste Yes in 0.6.15; groups/charts/opaque objects and slide clipboard: No |
| Macros (Basic) | Not planned: ARexx instead (Design 9) | No |
| ARexx port `GFOPENPRESENTATION` | P4 | No |

## Sources

- Apache OpenOffice, *Impress Guide* and *Getting Started with Impress*
  (AOO4 user guides), wiki.openoffice.org: "Using Slide Transitions" and
  "Setting Up the Slide Show", among others.
- Apache OpenOffice 4.1 Help, Impress.
- OASIS, *Open Document Format for Office Applications (OpenDocument)
  Version 1.2*, Part 1, sections 9 (drawing pages), 10 (shapes), 16 (styles)
  and 20 (formatting properties).
