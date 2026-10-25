# OpenPublisher: design

Version 0.1, draft for Dale's approval | 25 October 2026 | AmigaChrome Team, MIT

OpenPublisher is the GFOffice Suite's desktop-publishing program. Dale, 25
October 2026, 01:36-01:40: it is "a microsoft publisher 100% compatible app".
Its first phase must open every .pub version faithfully, view, print and
export to PDF, then edit and save in our own format; saving .pub comes
later. And at 01:38: "OpenPublisher should import PageStream files too."

Why now: Microsoft removed Publisher from Microsoft 365 on 1 October 2026 and
ended the perpetual version's support on 13 October 2026, so people hold .pub
files they can no longer open.

The pieces the suite shares (owf.library, the page-drawing PDF writer,
fonts, the picture cache, printing, the Open requester) are in
[Design-Suite-Shared.md](Design-Suite-Shared.md); OpenPresentation's deck
model is in [Design-OpenPresentation.md](Design-OpenPresentation.md). This
document points there instead of repeating them. What we know of the .pub
format, and where each fact came from, is
[PUB-FORMAT.md](PUB-FORMAT.md).

## 1. What it is

A native program for AmigaOS 3.2 (68040, no FPU, usable at 8 MB of Fast RAM,
comfortable at 16-32 MB, as OpenWrite) that:

- opens Microsoft Publisher files of every version, 98 to 365, and shows
  their pages as Publisher does;
- prints them through OpenPrint and exports PDF;
- edits a publication: text, frames, pictures, shapes, tables, pages and
  master pages, and saves it in our own format;
- imports PageStream documents (import only);
- later, if Dale wants it, saves .pub.

Apache OpenOffice, the suite's feature target, has no desktop-publishing
program. So OpenPublisher's checklist is Publisher's own features (section
10). Where they overlap with drawing (shapes, layers, guides), AOO Draw's
documented behaviour is the reference, as it will be for OpenDraw.

## 2. The clean room

.pub has no published specification. The rules (Dale, 01:36-01:40):

- The format is learnt only from sample files: their bytes, their structure,
  and what Publisher shows for them. Publisher's own thumbnail inside each
  file is one such view (PUB-FORMAT.md, section 11); Dale's PDFs or
  screenshots are another.
- No code of another implementation is read, copied or ported: not
  LibreOffice's libmspub, not Apache POI's, Apache Tika's or Aspose's
  readers. No documentation derived from them is read either, nor their
  bug trackers' discussions. Their sample files may be used, as files.
- Public descriptions of the container are fine: [MS-CFB] for the compound
  file, [MS-OLEPS] for the summary streams.
- Every fact goes into PUB-FORMAT.md with its sample and offsets, marked
  verified (on how many samples) or a hypothesis.

The same rules hold for PageStream.

## 3. Phases

| Phase | What | Done when | Proof |
| --- | --- | --- | --- |
| **P0, the format lab and a reader** (this PR) | The corpus (`tests/pub/CORPUS.md`); PUB-FORMAT.md; `libowf/src/in_pub.c`: the version, page size, pages and masters, frames with boxes and kinds, text, tables', WordArt's, fonts, pictures; `pub-dump`; `make pub-test`, `make pub-check` | Every sample reads; dumps match what each file shows; cut and changed files fail safely under ASan and UBSan | Host tests. A stove build of OWConvert and OpenCalc, which now link `in_pub.c` |
| **P1, faithful view, print and PDF** | Formatting (fonts, sizes, colours, alignment, spacing), styles and colour schemes; linked frames; columns; wrap; master pages' frames; tables' cells and borders; shapes and lines; pictures with crops; WordArt; layering. OpenPublisher's window to view pages; print and PDF export; OWConvert `.pub` to PDF | Each sample's page 1 matches its thumbnail; Dale's samples match his PDFs, page by page | Host: the model compared with expected dumps; PDFs rendered and compared with the thumbnails and Dale's PDFs. Lab: open, page through, print to a saved PDF |
| **P2, edit and our own format** | Editing (text, frames, pages, masters, pictures, shapes, tables, guides); save and open our own format; undo; PageStream import (when samples arrive) | Round trip: .pub, then our format, then reopened, gives the same model | Host round-trip tests. Lab: edit, save, reopen |
| **P3, save .pub** (if Dale wants it) | Writing .pub for Publisher users | Publisher opens our files (needs a PC with Publisher) | Dale's PC |

P1 is large, so it lands in steps, each its own PR: P1a the window, page
view and PDF of what P0 reads (boxes, text in plain style, pictures);
P1b text formatting and styles; P1c linked frames, columns and wrap
(OpenLayout's part, section 5); P1d shapes, WordArt, tables' look; P1e the
98/2000 and older forms.

## 4. The model

### 4.1 P0: `owf_pub` (libowf/include/owf_pub.h)

P0 keeps a small model of its own, because the shared page model was not on
main yet:

- `owf_pub`: the version, the page size (EMU and twips), the pages and
  master pages, the stories (every text, as `owf_story`), and `res`, an
  `owf_doc` holding the fonts and the pictures;
- `owf_pub_page`: a master's name, a page's master, its frames back to front;
- `owf_pub_frame`: a kind (text, picture, table, line, shape, group, other),
  its group, its box (EMU, as the file has it, and twips), the shape's
  number, its text (a story), its picture, a table's rows and columns, a
  WordArt flag.

### 4.2 P1: onto OpenPresentation's deck

OpenPresentation's `owf_deck` (`owf_deck.h`, W111, branch
`team5/openpresentation-p1-20261025`) is a deck of pages with masters, each
page a list of objects (`owf_obj`: kind, box in twips from the top left,
text as an `owf_story`, picture into `res->images`, groups by `parent`). A
publication is the same thing with a few more properties. So the proposal is
one model, not two:

| .pub | owf_deck |
| --- | --- |
| A page | a slide (`owf_slide`) |
| A master page | a master (`owf_master`) |
| A frame | an object (`owf_obj`), same kinds |
| The page size | `deck->width`, `deck->height` |
| Fonts, pictures | `deck->res` (an `owf_doc`), as now |

What desktop publishing adds, in a small extension beside the deck
(`owf_pubext`, one per deck, NULL for slides): linked text frames (each
object's next frame in its story's chain); text wrap (kind, distances);
columns in a text frame; layout and ruler guides; facing pages and spreads;
the scratch area's objects; the colour and font schemes; catalog and
mail-merge fields.

OpenWrite's `owf_doc` stays the model of flowing documents. Stories are the
same `owf_story`, so OpenWrite's paragraph code, editor core and layout work
on a frame's text unchanged (as OpenPresentation does, Design-
OpenPresentation 3.1).

This needs W111's agreement and Dale's (section 13, question 1).

## 5. Layout: OpenLayout

OpenLayout 0.2 already places frames out of the flow (`OL_BOX_ABSOLUTE`) on
pages; OpenPresentation lays a slide out that way (Design-OpenPresentation
5.1), and a publication's page is laid out the same way. Publishing needs
more from OpenLayout than slides do:

| Need | Why | Ask of OpenLayout (with OpenPresentation's 0.3 list) | Without it |
| --- | --- | --- | --- |
| Linked frames | A story flows from frame to frame across pages | A galley that continues in the next frame of a chain, and says where it stopped | The story is cut at each frame's end, with an overflow mark |
| Text wrap | Text flows around pictures and shapes | Exclusion boxes (a frame's box plus distances) that lines avoid; tight wrap later | Text ignores other frames |
| Columns | A text frame with 2 or 3 columns | Columns inside a frame | One column |
| Vertical anchoring, clipping, shapes | As OpenPresentation | As OpenPresentation 5.2 | As OpenPresentation |
| Rotation | Rotated frames | Not in 0.3 | Drawn unrotated, named in the report |

These are asked of openamigalayout, not made in a fork.

## 6. Fonts

.pub files name Windows fonts: Times New Roman, Arial, Courier New,
Symbol, Wingdings, script fonts every file lists (Sylfaen, Mangal...),
and decorative ones (halloween-flyer's WordArt uses "skullphabet"). An
Amiga has none of them. The plan, through the shared font module
(Design-Suite-Shared 10, E12):

- **Substitution by name, then by kind.** Times New Roman to the serif
  font, Arial and the sans faces to the sans font, Courier New to the
  monospaced font. Symbol through `owf_symbol.c`, which already maps
  Symbol's characters to Unicode; Wingdings needs the same kind of table
  (P1). Unknown names go by the font's kind, serif or sans, guessed from its
  name. The report names each font
  substituted.
- **Metrics.** A faithful view breaks lines where Publisher did. That needs
  fonts with the same widths: Liberation Serif, Sans and Mono have Times
  New Roman's, Arial's and Courier New's (`owf_font_modern` already names
  them). They are SIL OFL fonts; shipping them as outline fonts for
  openfont.library is question 3.
- **WordArt and decorative fonts** that can't be matched are drawn in the
  sans font, styled as close as we can.
- **PDF.** The page-drawing PDF writer places each line where the layout
  put it (Shared 9.2), so the PDF breaks where the screen does, whatever its
  font.

## 7. Pictures

- **Through datatypes**, as OpenWrite does today, and through the shared
  picture cache (Shared 11, E9): read the size first, decode at the size
  needed, within a memory budget. PNG, JPEG and GIF go to their datatypes.
  A DIB gets a BMP file header and goes to the BMP datatype.
- **Windows metafiles** (WMF, and EMF [H]) are common in older files
  (SampleBrochure, SampleNewsletter). We know of no Windows metafile datatype for OS 3.2,
  so P1 needs a small metafile player that turns the records into OpenLayout's
  drawing operations and the PDF writer's paths. Until then the frame shows
  its box and the report says why.
- **Embedded objects** (Excel tables and the like, in the `Objects`
  storage) are shown by the preview picture saved with them.
- **Memory:** pictures stay in the file's bytes until shown; christmas.pub
  holds 0.5 MB of PNGs, florist-flyer.pub 1.3 MB.

## 8. Printing and PDF

As the rest of the suite (Shared 9): pages are drawn into the page-drawing
PDF writer, which is the same display list the screen draws. Export PDF
writes it. Print writes it to `T:` and starts `C:OpenPrint` on it, as
OpenWrite does (`app/ow_print.c`). Publisher's print options come later
(P2): crop marks, bleeds, several pages to a sheet, booklets.

## 9. PageStream import (Dale, 01:38)

PageStream (Soft-Logik, later Grasshopper) is the Amiga's own desktop
publisher. Its versions: 1 and 2 (the `.pgs` era), 3, 4 and 5.

- **Import only.** OpenPublisher opens PageStream documents into the same
  model and saves them in our own format. It never writes PageStream.
- **What to import:** pages and their size, master pages, text frames and
  their linked chains, text with its formatting, pictures (placed IFF ILBM
  and other files, through datatypes), lines, boxes and ellipses, colours.
  PageStream's own drawing objects and effects come where the samples show
  them.
- **Which phase:** P2, once samples arrive. Its notes start as a
  "PageStream" section of PUB-FORMAT.md (section 13 there), with the same
  verified and hypothesis marks.
- **Samples:** none was found on GitHub, the only place this session's
  shell reaches. `tests/pub/fetch_samples.sh` fetches Aminet's PageStream
  demos (pgs22demo, pgs3, pgs3f, pgsmodam) on a PC. **Needed: PageStream
  samples from Dale**, every version he has, with a picture of each page.
- **Clean room:** as for .pub. Learn from the files and what PageStream
  shows, not from any other program's code.

## 10. Publisher's features, and which phase covers them

"View" means P1 shows and prints it as Publisher does. "Edit" is P2.

| Feature | P0 (read) | View | Edit | Notes |
| --- | --- | --- | --- | --- |
| Every version: 98, 2000, 2002/2003, 2007, 2010, 2013-365 | 2003-2013 fully; 98 and 2000 without masters and pictures; older: version and page size | P1 (P1e for 98, 2000, older) | P2 | Samples needed: 2002, 2016-365 (PUB-FORMAT 3) |
| Page size and orientation | Yes | P1 | P2 | |
| Pages, page order | Yes | P1 | P2 | |
| Facing pages, two-page spreads | Not yet (SampleNewsletter's pages alternate left and right on the workspace: a hypothesis) | P1 | P2 | |
| Master pages | Names, which page uses which | P1 (their frames) | P2 | The samples' masters are empty: samples needed |
| Text boxes | Boxes and text | P1 | P2 | |
| Linked text frames (overflow chains) | No | P1c | P2 | OpenLayout's galley chain (5) |
| Columns in a text box | No | P1c | P2 | |
| Character and paragraph formatting, styles | No (fonts listed) | P1b | P2 | Quill's STSH, FDPC, FDPP (PUB-FORMAT 6.2) |
| Colour schemes, font schemes | Scheme colours seen | P1b | P2 | |
| Pictures (PNG, JPEG, GIF, DIB, WMF) | Yes, bytes and formats | P1 (WMF: P1d) | P2 | Datatypes, metafile player (7) |
| Picture crop, recolour, transparency | No | P1d | P2 | |
| Shapes (AutoShapes), lines, arrows | Kinds and boxes | P1d | P2 | OpenLayout shapes (5) |
| Fills, gradients, line styles | Colour values seen | P1d | P2 | |
| WordArt | Text | P1d (styled text in its box), later its shapes | P2 | |
| Tables | Rows, columns, cell text | P1d (cells, borders, shading) | P2 | |
| Text wrap around frames | No | P1c | P2 | OpenLayout exclusions (5) |
| Layering (back to front), grouping | Order and groups | P1 | P2 | |
| Rotation, flips | No | Later (drawn unrotated, reported) | Later | OpenLayout |
| Rulers, layout and ruler guides, baseline guides, snap | No | P2 (shown in the editor) | P2 | |
| Scratch area | Found, counted | P2 (shown in the editor) | P2 | |
| Headers, footers, page numbers, fields | No | P1b | P2 | Quill's TOKN [H] |
| Hyperlinks | No | P1b (live in the PDF) | P2 | |
| Catalog merge, mail and e-mail merge | No | P1 shows the field names | Later | Merge data from CSV or OpenBase (question 6) |
| Embedded OLE objects | No | P1 (their saved preview) | Later | |
| Print: bleeds, crop marks, booklets, several to a sheet | | P2 | | |
| PDF export | | P1a | | |
| Spelling | | | P2 | Shared 7 |
| Save .pub | | | P3 | If Dale wants it |
| PageStream import | | | P2 | Samples needed (9) |

## 11. Where the code lives

| Path | What |
| --- | --- |
| `libowf/src/in_pub.c`, `libowf/include/owf_pub.h` | The .pub reader (P0); in P1 it fills the deck model instead |
| `libowf/src/owf_cfb.c` | Compound files; P0 added entries by number and storages by path |
| `tests/pub/` | Samples, expected dumps, `pub_dump.c`, `test_pub.py`, `CORPUS.md`, `fetch_samples.sh` |
| `tools/publab/` | The format lab's tools (PUB-FORMAT 1) |
| `src/pub/` (P1) | OpenPublisher's portable core, host-tested, as `src/calc` |
| `app/openpublisher*.c` (P1) | The Amiga program (OpenGadTools) |

`in_pub.c` needs `owf_cfb.c`, which OpenWrite's own build and owf.library
leave out, so P0 leaves `in_pub.c` out of those two builds too
(`build-os3.sh`, `build-library.sh`). OWConvert and OpenCalc link it today
(they link all of libowf).

## 12. Risks

| Risk | What we do |
| --- | --- |
| The format is larger than the samples show | Dale's files, and every public sample; each new sample is dumped and compared with its thumbnail before code changes |
| Line breaks differ from Publisher's, so text overflows differently | Metric-compatible fonts (6); the report says when a frame overflows |
| Memory: large pictures, many pages | The picture cache's budget (7); pages laid out only when shown |
| A hypothesis turns out wrong on a new file | The reader checks every length and number it reads and reports what it couldn't read; never a crash (P0's fuzzing) |
| Formatting (P1b) is the hardest part to learn from bytes alone | Pairs of samples that differ by one change, made by Dale in Publisher (question 4) |

## 13. Questions for Dale

1. **One model?** Should OpenPublisher's pages share OpenPresentation's deck
   model, with a small extension for publishing (4.2), or keep a model of
   their own?
2. **Our own format (P2).** Should we use OpenDocument Graphics (`.odg`,
   with pages: what AOO Draw writes, so files open elsewhere) or a GFOffice
   format of our own? We propose `.odg`.
3. **Fonts.** May we ship the Liberation fonts (SIL OFL) as outline fonts, so
   pages break lines as Publisher did?
4. **Samples.** Your .pub files, especially Publisher 2002 and 2016-365, with
   master pages that hold frames, linked text frames, catalogs or mail
   merge, and the PDF or a screenshot of each page. If you still have
   Publisher, pairs of files that differ in one thing (one word made bold,
   one frame moved) would teach us formatting fastest.
5. **PageStream.** Which versions do you have documents from, and can you
   send some, with pictures of their pages? The Aminet demos can also be
   fetched on your PC (`tests/pub/fetch_samples.sh`).
6. **Mail and catalog merge.** Is merging data in scope, and from what: CSV
   files, OpenBase?
7. **P3.** Do you want .pub saving at all, given Publisher is gone?
8. **The scratch area.** Should OpenPublisher show the objects Publisher
   keeps beside the page?
9. **OWConvert.** Should P1 add `.pub` to PDF and `.pub` to ODT (text) in
   OWConvert, for converting many files from the Shell?
10. **Personal data.** Files hold the author's name and a path on the
    author's PC (PUB-FORMAT 5.1, 11). Should our own format and PDF export
    leave them out by default?
