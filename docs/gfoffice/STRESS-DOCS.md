# GFOffice's stress documents

26 October 2026 | Team 5 (W142) | AmigaChrome Team, MIT

Dale, 26 October 2026: "generate a really complicated pptx with animations
etc to test presenter ... Same for Publisher and PageStream, Final Writer
etc." The deck is `tests/deck/make_stress_pptx.py` (gfoffice #103). This
page is the rest: for every other format GFOffice reads or is to read, a
document that holds as much of what the format can hold as we could make or
find, and what our reader gets out of it. Each table is that reader's gap
list.

Nothing here is from a private document. Where each file came from, and its
licence, is in `tests/amiga/fixtures/PROVENANCE.md` and
`tests/pub/stress/PROVENANCE.md`.

## 1. The documents

| Format | Stress document | How it was made | In the repository |
|---|---|---|---|
| Microsoft Publisher (`.pub`) | `SampleNewsletter.pub`: 4 pages, 99 frames, 10 linked-frame chains, 9 pictures, WordArt; `govdocs1-923566.pub`: a US government newsletter, 4 US Letter pages, 74 frames, 9 linked-frame chains, a table, 37 fonts; with the corpus's other samples for tables, hyperlinks, landscape pages and the 98, 2000 and 2007-2013 formats | Real: Apache POI's test data, saved by Publisher 2003; govdocs1, saved by Publisher 2003 (11.9999) | Yes: `tests/pub/stress/` (Apache-2.0; govdocs1's terms) |
| PageStream (2 to 5) | 17 real documents: club newsletters with embedded pictures and articles over several pages, a periodic table (182 table cells), CD covers, character sheets, labels | Real: Aminet | No (licences not stated); listed, fetched by `tests/amiga/fetch_stress_samples.sh` |
| Final Writer | 32 real documents: the four Amiga C Encyclopedia volumes (to 950 KB, 10 sections each), a manual with 32 pictures, 23 templates (text frames, tables, master pages), a table example | Real: Aminet | No; listed and fetched as above |
| Wordworth | 10 real documents: two 300 KB manuals, text boxes linked in chains, drawings with text, a calendar | Real: Aminet | No; listed and fetched as above |
| ProWrite | `prowrite-stress.pw`, every chunk of the 1987 specification; and New Horizons' own `Convert Notes` (`convert-notes.pw`) | Generated (`make_stress_prowrite.py`); real: Aminet, shipped with ProWrite File Convert | Both: `tests/amiga/fixtures/` |
| IFF FTXT | `ftxt-stress.ftxt` (and a `LIST` and a `CAT` of FTXT), every rule of the specification | Generated (`make_stress_ftxt.py`) | Yes: `tests/amiga/fixtures/` |

Why some are real and some made: FTXT and ProWrite have published
specifications, so their documents were written from them and checked by
other programs (sections 6 and 7). Publisher, PageStream, Final Writer and
Wordworth have none: a file we wrote would hold only what we already believe,
so real files from the programs are the test.

## 2. Microsoft Publisher

**What exists:** the P0 reader (`libowf/src/in_pub.c`, through
`build/host/pub-dump`; OWConvert names a `.pub` but doesn't convert it yet),
the clean-room notes ([PUB-FORMAT.md](PUB-FORMAT.md)) and the corpus
(`tests/pub/CORPUS.md`). No .pub was written by us: the format has no
specification, and the clean room ([Design-OpenPublisher.md](Design-OpenPublisher.md),
section 2) learns it only from Publisher's own files. For the same reason no
sample was opened in LibreOffice (libmspub) to compare: the corpus has never
been through another .pub reader.

**The stress set:** `SampleNewsletter.pub`, now checked in, is the most
complicated freely licensed .pub we know of. `govdocs1-923566.pub`, the one
Publisher file in govdocs1, is checked in beside it: the only sample saved
by Publisher 2003's last build (11.9999) and a real document from outside
POI's test data (`tests/pub/stress/PROVENANCE.md`). The rest of the set is the
corpus already in `tests/pub/samples` (and fetched by
`tests/pub/fetch_samples.sh`), each for what it adds:

| Feature | Sample |
|---|---|
| Several pages, linked text frames (10 chains, 29 frames), WMF and JPEG pictures, WordArt, groups, lines | `SampleNewsletter.pub` (2003) |
| A real newsletter on US Letter: one master page, 74 frames (54 text), 9 linked-frame chains, a table, a group, 37 fonts, WMF, JPEG and PNG pictures | `govdocs1-923566.pub` (2003, 11.9999) |
| A4 landscape, 2 pages, GIF, JPEG and WMF, tabbed text | `SampleBrochure.pub` (2003) |
| A table (3 x 2), two pages, a hyperlink | `Sample.pub` (2007), `Sample_2010.pub`, `Sample98.pub`, `Sample2000.pub` (the same document in four formats) |
| Many shapes and lines in groups, US Letter | `60685.pub` (2013), `51318.pub` (2003) |
| Hyperlinks at given places | `LinkAt*.pub` (2007, fetched) |
| Damaged and cut-short files | the `clusterfuzz-*` files, `mimetype-pub.pub` |

**What `pub-dump` gets from `SampleNewsletter.pub`:**

| In the file | Read | Notes |
|---|---|---|
| Version, page size (A4), 4 pages, master page | Yes | |
| 99 frames: 63 text, 10 pictures, 13 shapes, 9 groups, 4 lines | Yes | Boxes to 0.1 mm, kinds, groups' members |
| Text of 45 stories, tabs and line breaks in it | Yes | |
| Linked text frames | No | Each of a chain's frames shows the whole story: 29 frames in 10 chains repeat their text |
| WordArt ("Liberty") | Text only | Not its shape or font |
| 9 pictures (8 WMF, 1 JPEG) | Yes, as data | Not drawn yet (P1) |
| Fonts | The list | Not which text uses which |
| Character and paragraph formatting, colours, wrap, columns | No | P1 (PUB-FORMAT.md, section 12) |

**What no free sample holds, so nothing tests yet:** master pages with
frames on them, headers and footers, page-number fields, rotated text and
shapes, merged table cells, unusual page sizes (a business card, a banner),
catalogs and mail merge, and the Publisher 2002 and 2016-or-later formats.
Only Publisher can make these (section 9).

## 3. PageStream

**What exists:** nothing reads PageStream yet (Design-OpenPublisher.md,
section 9: import in P2). OWConvert answers "not a document format
OpenWrite knows" for all 17 documents. What the samples show of the
containers, for whoever writes the reader:

- **PageStream 1 and 2** start with the bytes `07 23 19 92` (7 files).
- **PageStream 3, 4 and 5** are IFF: `FORM DOC ` with `VERS`, `DOC `,
  `NAME`, `AUTH`, `(C)`, `FILE`, `CNUM`, `PNUM` and `CTAG` (the tags,
  colours and styles), `FORM MPAG` master pages (`PGDM` page size, `MRGN`,
  `GRID`, left, centre and right masters with guides), `FORM PAGE` pages
  holding `FORM ILUS` layers of `IOBJ` objects (nested in `FORM WRAP`
  groups), `FORM ARTI` articles (the text, in `FORM CTXT`), `FORM CELL`
  table cells, and pictures embedded (`FORM ILBM` with `XBMI` and `DPI`) or
  linked (`FILE`) (10 files).

| Sample | Version | What it has |
|---|---|---|
| `FLAK9505/.../GAC_NL_9505` and `.cvr` | 3 | A club newsletter and its cover: 477 and 653 KB, embedded pictures, articles over many pages |
| `FLAK9701/.../GAC_NL_9701` | 3.2 | Another issue: 9 pages, 17 articles |
| `PeriodicTable-FR.pgs`, `-NL.pgs` | 4.1 or later | A table of 182 cells, 265 articles |
| `CDCover/Pagestream/CD Cover - AminetGames1` | 3 or later | Pictures, 6 pages |
| `CharacterSheet/Character.p4` and `.p2` | 4 and 2 | The same form in both versions |
| `BusinessCards.pgs`, `ParchmentMFSample.DOC` | 3 | A label sheet; a font sample with a character grid |
| `January93`, `February93`, `March93.pgs`, `AmigaBreeze.Jan93`, `8mm Labels`, `GoldenImage_Mousepad` | 2 | Club newsletters, labels, a mouse mat |

## 4. Final Writer

*The table below is the reader before #105, which closed most of its gaps; #105's description has the before-and-after counts.*

**What exists:** `libowf/src/in_finalwriter.c` and
[../formats/finalwriter.md](../formats/finalwriter.md), from documents on
Aminet. A document we wrote would only hold what that page already says, so
the test is the real documents. MultiRead (a 1996 Final Writer and ProWrite
reader, freeware, run in the lab) gives a second reading.

| In the documents | Read | Notes |
|---|---|---|
| Text (`CHRS`), fonts (`FDTA`), sizes, underline (`ATTR`), bold and italic from font names, the Symbol font | Yes | In a document of one section |
| **Documents of several sections** (`SHD1`, `TBDY`, `RMST`, `LMST` again for each) | **No: only the first section's body** | After the first section's left master page the reader puts text nowhere, and nothing reports it. The four ACE volumes hold 109,589-360,781 bytes of text; OWConvert's text output has 1-2% of it (ACE-System: 2,435 characters; MultiRead gets 112,056). `GenTree.Doc`: 102 characters of 92,814 (MultiRead: 94,066) |
| Text frames (`TXOB`) | As paragraphs at the start | Not placed |
| Table cells (`CLLE`; `CELL`, `SCLS`, `STRY` in 3 files) | As paragraphs | No table |
| Master pages | The right one as the header; the left one left out | Labels and covers keep all their text there, so their body text is empty |
| Tab stops (`TABS`: 6,375 in 11 files) | No | Default stops |
| Paragraphs (`RULE`): alignment, indents, spacing | No | Not worked out |
| Pictures (`FORM ILBM`: 90 in 6 files) | No | Counted and reported |
| `GINF` (527 in 32 files: drawn objects, a guess) | No | Not reported |
| Page setup (`PAG3`, `SEC1`, `DOC `: not worked out) | No | The default page |
| Contents, index, outline, bibliography (`TOCD`, `IDXD`, `OUTD`, `BIBD`), endnotes' text | No | Endnote marks kept |

## 5. Wordworth

*The table below is the reader before #108, which closed most of its gaps; #108's description has the before-and-after counts.*

**What exists:** `libowf/src/in_wordworth.c` and
[../formats/wordworth.md](../formats/wordworth.md).

| In the documents | Read | Notes |
|---|---|---|
| Body text (`WTXT`), fonts (`WFNT`), styles and colours (`WPAR`, `WFSC`, `WKCO`), page size (`WDOC`), page breaks, headers and footers | Yes | The two 300 KB manuals come through whole (200,542 of 200,645 and 229,055 of 229,055 characters) |
| **Text boxes and drawings** (`FORM GTID`: `FORM WOWO` text boxes, `PTXT`, `RECT`, `RRCT`, `ELLP`, `CIRC`, `LINE`, `GROU`, `PICT`) | **No** | Counted as one picture. Seven of the ten documents keep text there: `ZIP_template.ww7` (810 characters in 37 text boxes with links), `LabelTape`, `WW-Farben` and the two "Powered by Amiga" logos come out empty, `Calendario2008` without 894 of its 1,263 characters, `PolicesWordworth.ww7` without 146 |
| Linked text boxes (`WLNK`) | No | |
| Paragraph styles (`WSTY`, `WSTT`), bullets (`WBUL`), tab stops (`WTAB`), indents, margins | No | Tab stops reported |
| Palette (`WPAL`), document settings (`WINF`, `WSMD`, `WPF0`-`WPF2`, `WGRD`) | No | |

## 6. ProWrite

**What exists:** `libowf/src/in_prowrite.c`, written from New Horizons'
published specification. `prowrite-stress.pw` holds every chunk that
specification names; `Convert Notes` (`convert-notes.pw`, New Horizons' own
file, checked in) is a real ProWrite 3 file, and `test_prowrite.py` reads
it whole. New
Horizons' own ProWrite 3.2.3 demo opens the stress file in a lab and shows
it as meant (fonts, colours, the picture, Roman page numbers from VII,
footer and header), and MultiRead (1996) reads all its text.

| In the documents | Read | Notes |
|---|---|---|
| Fonts and sizes, bold, italic, underline, raised and lowered text, font changes inside a paragraph (120 in one) | Yes | |
| Colours | Yes, since #106 | `COLR` holds ISO colour numbers 30-37 (ProWrite's own file has `1E 25 1F 24 20 23 22 21`); before #106 the reader took them as 0-7, so black text came out cyan and blue came out green |
| Alignment (all four), double spacing, left indents, first-line and hanging indents | Yes | |
| Right margin | Approximate | What it is measured from isn't said; ProWrite 3's own file has 360 where only "from the right" makes sense |
| Tab stops: left, centre, right, decimal | Yes | A section without its own `TABS` keeps the previous section's |
| Page number, date and time characters | Yes, as fields | |
| Start page and its numbering (VII, Roman) | Yes | DOCX keeps both; ODT keeps the numbering, not the start |
| Page breaks | Yes | Two in a row leave an empty page, since #106 |
| Headers for left and for right pages | Approximate | Both become one header, reported |
| Pictures (`PCTS`, `PINF`, `BODY`: packed, with a transparent colour or a mask) | No | Counted and reported |
| ProWrite 3's `PSET`, `PREC`, `SECT` (not in the 1987 specification) | `PSET` yes, since #106; `PREC`, `SECT` no | `PSET` holds the page in decipoints (6120 x 7920, US Letter, in `Convert Notes`); a file without one, as the stress file, gets the default page, reported |

## 7. IFF FTXT

*The table below is the reader before #107, which closed most of its gaps; #107's description has the before-and-after counts.*

**What exists:** `libowf/src/in_ftxt.c`, written from the specification;
FTXT is also the clipboard's format. `ftxt-stress.ftxt` holds each rule of
the specification in a labelled line; `ftxt-stress.txt` is what a reader
following it shows, and an independent stripper written from section 8 of
the specification gets exactly that. The lab's FTXT DataType (AmigaOS
3.2.3) opens it too, with styles and colours but not fonts.

| In the document | Read | Notes |
|---|---|---|
| Fonts 0-9 (`FONS`), chosen by SGR 10-19; one renamed between two `CHRS` | Yes | |
| Text with no font chosen: font 1 | No | The document's default font instead |
| Bold, italic, underline, crossed out; on, off and reset (SGR 0 and `CSI m`) | Yes | |
| Inverse, background colours | Reported, left out | |
| Faint, double underline, blink | No | Not reported |
| Colours 30-37, 39 | Yes | |
| Latin-1 (A0-FF), LF line ends, CR skipped | Yes | |
| One paragraph spread over two `CHRS` chunks | Split in two | The specification makes all of a FORM's `CHRS` one stream (formatting carrying over is read) |
| ESC sequences with intermediates (`ESC ( B`) | No | Their last letters show as text |
| DCS, OSC, PM and APC strings | No | Their contents show as text |
| CSI with a private parameter (`CSI ? 7 h`) | No | `7h` shows |
| A CSI cut by a letter or a line end | No | The letter is lost; the line end is eaten and two lines join |
| Tab, form feed, vertical tab | Read as tab, page break and line break | The specification leaves them unused; libowf's choice, kept for real files |
| ISO 6429 sizes and alignment (`GSS`, `QUAD`) | Skipped | The specification allows a reader to skip them |
| A nested `FORM ILBM` | Skipped | Not reported |
| `LIST FTXT` with `PROP FTXT`; `CAT FTXT` | No | "Not a document format": only a file that starts `FORM FTXT` is read |

## 8. Running them

```sh
make build/host/owconvert build/host/pub-dump
python3 tests/amiga/make_stress_ftxt.py tests/amiga/fixtures       # the same bytes again
python3 tests/amiga/make_stress_prowrite.py tests/amiga/fixtures
build/host/owconvert --report tests/amiga/fixtures/prowrite-stress.pw /tmp/pw.odt
build/host/owconvert --report tests/amiga/fixtures/ftxt-stress.ftxt /tmp/ftxt.txt
diff tests/amiga/fixtures/ftxt-stress.txt /tmp/ftxt.txt              # the FTXT gaps
build/host/pub-dump tests/pub/stress/SampleNewsletter.pub
PUB_CORPUS=tests/pub/stress make pub-check                           # both .pub, and changed copies, under ASan
python3 tests/amiga/test_prowrite.py build/host/owconvert            # Convert Notes and ProWrite's chunks
sh tests/amiga/fetch_stress_samples.sh --extract DIR                 # the real documents, on a PC
```

None of them is wired into `make test` yet: they measure the readers, and
most rows above are still gaps. A test can be added as each gap is closed.

## 9. What only Dale can do

- **Publisher:** if a PC with Publisher is at hand, one document made to a
  recipe would cover what no free sample does: a master page with a header,
  a footer and a page-number field; a business-card page size; a text frame
  linked over three pages; rotated text and a rotated picture; a table with
  merged cells and borders; a catalog page. Saved in every format that
  Publisher offers, with a PDF of each, and committed if Dale agrees; a
  Publisher 2016 or later would also give the first sample of that format.
- **PageStream:** PageStream 5 is sold today (Grasshopper LLC; a trial runs
  on Linux, Windows and Mac). Making documents needs it bought, or Dale's
  own copy of an Amiga version; the Aminet demo (2.2) can't save.
- **Final Writer and Wordworth:** no demo that saves was found on Aminet
  (Wordworth 2's cover-disk demo doesn't save; no Final Writer demo is
  there). Documents made to a recipe need Dale's own copies.
- **Licences:** the real documents are kept out of the repository because
  their readmes don't say they may be redistributed. Dale decided on 27
  October that `Convert Notes` (New Horizons' file, whose readme allows
  distribution with the program) and govdocs1's Publisher file are checked
  in, with their provenance; the others stay listed.
