# OpenPresentation and Apache OpenOffice Impress: a feature checklist

Version 0.1 | 25 October 2026 | AmigaChrome Team, MIT

Dale, 25 October 2026: "The spec we are chasing is to mirror what Apache
OpenOffice does in all its apps." This is the list for Impress, which
OpenPresentation answers to. It is made from Impress's documented,
user-facing behaviour: Apache OpenOffice 4.1's Help, the AOO4 user guides
on wiki.openoffice.org (*Getting Started with Impress*, the *Impress Guide*)
and OASIS ODF 1.2 for what the files hold. No OpenOffice or LibreOffice code
was read. Parity is in features, not in Impress's size or look: the program
stays a small 68040 one with the Amiga's look (OpenGadTools).

Each row says what Impress does, the phase that brings it to
OpenPresentation (Design-OpenPresentation.md, section 12), and where phase
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
| New slide, delete, duplicate, rename | P3 | Model (`owf_deck_add_slide`, `_delete_slide`; names kept) |
| Move slides (Slide Sorter, drag) | P3 | Model (`owf_deck_move_slide`) |
| Hide and show a slide | P2 (show), P3 (edit) | Yes: read, written, left out of the PDF |
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

| Impress | Phase | P1 |
| --- | --- | --- |
| Text boxes, placeholder text | P1 | Yes |
| Typing in a text box: click to put the caret, type, Return, Backspace, Del, arrow keys | P3 | No in P1. P3's first part: Yes, in OpenPresentation 0.2 (`opr_edit.c`); selecting text and changing its look next |
| Font, size, bold, italic, underline, strike-through, raised and lowered, colour | P1 | Yes |
| Shadow, outline, relief, character spacing, highlighting, case | Later | No |
| Paragraph alignment, indents, spacing above and below, line spacing, tabs | P1 | Yes |
| Paragraph background and borders | P1 | Yes (as OpenWrite holds them) |
| Bullets and numbering: character bullets, numbers (1, a, A, i, I) with prefix and suffix, start value, size, colour, bullet font | P1 | Yes |
| Picture bullets | Later | Approx.: a round bullet |
| Outline levels 1-9, promote and demote | P1 (levels), P3 (commands) | Yes (until Shared E8 puts levels in paragraphs, a frame holds its paragraphs' levels) |
| Autofit: shrink text on overflow | P1 | Yes: read, written, laid out (up to four times by 90%) |
| Autofit: grow the box with the text, fit text to frame | P1 (grow), Later (fit) | Grow: Model; fit: No |
| Vertical anchoring (top, middle, bottom), text insets | P1 | Yes |
| Hyperlinks in text | P1 | Yes, and links in the PDF |
| Fields: date, time, slide number, slide count, author, file name | P1 (date, number, count) | Date, number and count yes; others No |
| Text animation (scrolling text), Fontwork | Later | No (Fontwork shapes are kept) |
| Find and replace, spelling, autocorrect, thesaurus, hyphenation | P3, P4 (Shared E11) | No |
| Pictures inside text | Later | No (reported) |

## 5. Drawing objects

| Impress | Phase | P1 |
| --- | --- | --- |
| Rectangle, square, rounded rectangle | P1 | Yes |
| Ellipse, circle | P1 | Yes |
| Arcs, segments, sections of circles | Later | Kept |
| Lines, with arrowheads | P1 | Yes |
| Basic shapes: triangle, diamond | P1 | Yes |
| Block arrows: right and left | P1 | Yes |
| Other basic shapes, symbol shapes, block arrows, flowchart, callouts, stars and banners | Later | Kept (drawn as their box with their fill) |
| Polygons | P1 | Yes |
| Polylines, curves, freeform lines | Later | Kept |
| Connectors, glue points | Later | Kept |
| 3-D objects, extrusion | Not planned | Kept |
| Line: colour, width, solid or dashed; area: colour | P1 | Yes |
| Area: gradient, hatching, bitmap, transparency; shadow | Later | Approx.: one colour; transparency and shadow No |
| Position and size | P1 | Yes |
| Rotation | P1 (kept), Later (drawn) | Model: kept and written back; drawn unrotated |
| Flip, slant | P1 (flip of lines and arrows) | Lines and arrows yes; slant: Approx. (drawn in its box) |
| Text in shapes | P1 | Yes |
| Group, ungroup, enter group | P1 (groups), P3 (commands) | Yes |
| Arrange (front, back), align, distribute | P3 | Model (back-to-front order) |
| Combine, split, shape operations (merge, subtract, intersect) | Not planned | No |
| Name and description (alt text) | P1 | Yes |
| Snap, grid, guides, rulers | P3 | No |

## 6. Pictures

| Impress | Phase | P1 |
| --- | --- | --- |
| Insert picture from file | P3 | Model |
| Crop | P1 | Yes: read, written, drawn in the PDF |
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
| Print slides, notes, handouts, outline (through OpenPrint) | P2 (slides), P4 | No (PDF export only). P2: slides, through OpenPrint (OpenPresentation 0.2) |

## 10. Slide show

| Impress | Phase | P1 |
| --- | --- | --- |
| Start from the first slide, from the current | P2 | No |
| Next, previous, first, last, go to a slide by number, black and white screen, end | P2 | No |
| Advance on click, or automatically after a time | P2 | Model |
| Rehearse timings | P4 | No |
| Slide show settings: range, loop with a pause, pointer visible, pen, navigator, animations allowed, slides change on click, always on top | P2, P4 | Kept |
| Custom slide shows | Later | Kept |
| Hidden slides left out of the show | P2 | Model (and left out of the PDF) |
| Presenter console | Not planned | No |
| Interactions (on click: go to a slide, open a file, play a sound, end the show) | Later | No |

## 11. Transitions

| Impress | Phase | P1 |
| --- | --- | --- |
| Transitions per slide, apply to all | P1 (data), P3 (editing) | Model |
| Cut, wipe, push, cover, uncover, split, blinds, dissolve | P2 (wipe, push, dissolve), P4 (the rest) | Model |
| Fade smoothly, fade through black | P4 | Model |
| The other effects (wheel, shape, box, checkerboard, circle, newsflash, random...) | Later | Kept, shown as a cut |
| Speed (slow, medium, fast) | P2 | Model |
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
| Undo and redo | P3 (per slide) | No. P3's first part: typing in text boxes undone and redone (`opr_edit.c`) |
| Clipboard: objects and slides | P3 (Shared E10) | No |
| Macros (Basic) | Not planned: ARexx instead (Design 9) | No |
| ARexx port `OPENPRESENTATION` | P4 | No |

## Sources

- Apache OpenOffice, *Impress Guide* and *Getting Started with Impress*
  (AOO4 user guides), wiki.openoffice.org: "Using Slide Transitions" and
  "Setting Up the Slide Show", among others.
- Apache OpenOffice 4.1 Help, Impress.
- OASIS, *Open Document Format for Office Applications (OpenDocument)
  Version 1.2*, Part 1, sections 9 (drawing pages), 10 (shapes), 16 (styles)
  and 20 (formatting properties).
