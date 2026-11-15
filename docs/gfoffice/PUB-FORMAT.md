# Microsoft Publisher's .pub format: the clean-room notes

GFOpenPublisher's record of what we know about .pub and where each fact came
from. Microsoft never published a specification of .pub. Everything below
was worked out from sample files only: their bytes, their structure, and
what Publisher itself shows for them (the thumbnail Publisher saves in each
file, section 11). No other implementation was read: not LibreOffice's
libmspub, not Apache POI's or Tika's readers, not Aspose's, and no
documentation derived from them. The container is an OLE2 compound file,
read by our own `libowf/src/owf_cfb.c` after Microsoft's [MS-CFB]; the
summary streams are read after Microsoft's [MS-OLEPS].

**Marks.** **[V n]**: verified on n samples (the corpus, `tests/pub/CORPUS.md`:
27 whole files, of which 24 are in the 2002-or-later format). **[H]**: a
hypothesis, consistent with the samples but not proven by them. Offsets are
hexadecimal, from the start of the stream named. Sample names are the
corpus's (POI's `Simple.pub` and so on); "Simple 0x1FEA" means offset 0x1FEA
of that sample's stream.

## 1. The lab: how to look

- `make build/host/pub-streams`, then `build/host/pub-streams FILE DIR` lists
  a file's storages and streams and writes each stream under DIR. It uses
  only `owf_cfb.c`.
- `tools/publab/*.py` read the streams pub-streams writes: `hx.py` (hex),
  `tlv.py` (one tagged-value block, section 4), `idx.py` (the chunk index of
  `Contents`, section 5), `chunk.py` (the chunks of some kinds), `esc.py`
  (the drawing's records, section 7), `quill.py` (the text storage, section
  6), `old.py` (the 98/2000 directory, section 9), `props.py` (summary
  property sets, [MS-OLEPS]), `thumbdib.py` (the thumbnail's bitmap as a
  PNG, section 11). Run them with `python3 -I`.
- `make build/host/pub-dump`: what the reader (`libowf/src/in_pub.c`) gets
  out of a file.

## 2. The container [V 27]

Every whole sample is a compound file with 512-byte sectors.

The 2002-or-later files and the 98/2000-format files (26 samples) all have
this tree:

| Path | What | Mark |
|---|---|---|
| `Contents` | the document: pages, frames, the rest (sections 5, 9) | [V 26] |
| `Quill/QuillSub/CONTENTS` | all the text (section 6) | [V 26] |
| `Quill/QuillSub/\x01CompObj` | names the class "Quill96 Story Group Class" | [V 26] |
| `Escher/EscherStm` | the drawing: shapes and their boxes (section 7) | [V 26] (empty in the 98/2000 files) |
| `Escher/EscherDelayStm` | the pictures' bytes (section 8) | [V 26] (often empty) |
| `Objects` (storage) | embedded OLE objects, one storage each ("Object 0"...) | [V 26]; filled only in a fuzzer's file |
| `VBA` (storage) | macros; empty in every sample | [V 26] |
| `Envelope` | 0 bytes in every sample | [V 26] |
| `\x01CompObj` | "Microsoft Publisher 3.0", ProgID "MSPublisher.3", in every version | [V 26] |
| `\x03Internal` | 12 bytes, `00 00 01 00` then zeros | [V 26] |
| `\x05SummaryInformation`, `\x05DocumentSummaryInformation` | [MS-OLEPS] property sets; the thumbnail and the writer's version (sections 3, 11) | [V 26] |

The root storage's class id is `01 12 02 00 00 00 00 00 C0 00 00 00 00 00 00 46`
[V 27]; QuillSub's is `DA F6 C8 08 9D 96 D1 11 8E 02 00 C0 4F B6 FE CE` [V 26].

The older file (`fdo59355-1`) has only `Objects`, `\x01CompObj`, `Contents`
and `\x03Internal` [V 1].

Since every version's CompObj says "Publisher 3.0", CompObj can't tell
versions apart [V 27].

## 3. Telling the versions apart

`Contents` starts `E8 AC` in every sample [V 27]. The next bytes split it in two:

| `Contents` bytes 2-5 | Form | Samples |
|---|---|---|
| `2C 00 E8 03` (44, 1000) | the 2002-or-later form (sections 4, 5) | [V 24] |
| `22 00 2C 01` (34, 300) | the 98/2000 form (section 9) | [V 3] |

In the 2002-or-later form, byte 0x0C of `Contents` is a build mark. The
DocumentSummaryInformation's PIDDSI_VERSION ([MS-OLEPS] property 0x17,
VT_I4: the major version in the high word) names the program that wrote
the file. In every sample they agree:

| `Contents` 0x0C | PIDDSI_VERSION | Format | Samples |
|---|---|---|---|
| 0x13 | 11.5606, 11.5703, 11.9999 | Publisher 2003 (and probably 2002) | [V 3]; 2002 itself [H], no sample |
| 0x15 | 12.0 | Publisher 2007 | [V 12] |
| 0x18 | 14.0 | Publisher 2010 | [V 6] |
| 0x1A | 15.0 | Publisher 2013 or later | [V 3]; 2016 and later (16.0) [H], no sample |

In the 98/2000 form, the version program's PIDDSI_VERSION is 12.0 in both
samples, so Publisher 2007 saved them in the older formats. The document
chunk's mark (section 9) tells them apart: `D2 B2` for the 98 format, `E8 B2`
for the 2000 format [V 1 each], and `78 6A` in the older file [V 1].

`Contents` byte 6 (05 or 06) does not follow the version [V 24]; its meaning
is unknown.

## 4. Tagged values (the 2002-or-later `Contents`, and Escher's client records)

Data come as fields: an id byte, a type byte, then what the type says
[V 24: all 1436 indexed chunks of the 24 samples, and their headers and
index, are used up exactly with this table and nothing else]:

| Type | What follows |
|---|---|
| 0x00, 0x08, 0x78 | nothing (0x08: a flag that is set; 0x78: an empty slot in an array) |
| 0x10, 0x18 | 2 bytes |
| 0x20, 0x68, 0x70, 0xB8 | 4 bytes (0x68 and 0x70 hold another chunk's number, section 5; 0xB8 an offset in the stream) |
| 0x28 | 8 bytes |
| 0x38 | 16 bytes |
| 0x88, 0x90, 0x98, 0xA0, 0xC0 | a u32 length (counting itself), then that many bytes less 4. 0x88 holds fields; 0x90, 0x98 and 0xA0 hold an array (its items have id 0); 0xC0 holds bytes (often UTF-16 text) |

Little-endian throughout. A block (a chunk, say) is a u32 length, then fields.

Source: Simple 0x2C-0xD7 (the header block), Simple 0x1FEA-0x264D (the index),
every chunk of every sample (`tlv.py`, `idx.py`).

## 5. `Contents`, the 2002-or-later form

### 5.1 The header [V 24]

| Offset | What |
|---|---|
| 0x00 | u16 0xACE8 |
| 0x02, 0x04 | u16 0x2C, u16 0x03E8 |
| 0x08 | u32: the stream's length [V 27, both forms] |
| 0x0C | the build mark (section 3) |
| 0x1A | u32: the chunk index's offset (the same as header field 0x0A below) |
| 0x1E | u32: another block's offset (header field 0x0B) |
| 0x2C | the header block (tagged values) |

The header block's fields (Simple 0x2C): 0x01 = 0xACE8; 0x02 = 0x100; 0x03,
0x04, 0x05, 0x07 = the build mark with 1000 in the high word (0x03E80015
in a 2007 file); 0x08 = the stream's length; 0x09 = the UTF-16 path of a
temporary file on the author's PC [V 24] (personal data: don't show it,
don't keep it on save); 0x0A = the chunk index's offset; 0x0B = the second
block's offset; 0x0C = 1; 0x0D-0x10 = 0x18A4 [V 24, values seen; meaning of
0x0C-0x10 unknown]. The second block (Simple 0xD8: field 0x01 = 0x8F, then
an array of small blocks numbered 1, 2, 3...) is not understood [H: a table
of ids].

### 5.2 The chunk index [V 24]

At the header's 0x0A offset (Simple 0x1FEA, Sample 0x256A): a block with 0x01
(the number of items, 0x128 in Simple), 0x02 (one less) and 0x03, an array
of items. An item is either 0x78 (an empty slot) or a 0x88 block with:

| Field | What | Mark |
|---|---|---|
| 0x02 (u16) | the chunk's kind | [V 24] |
| 0x04 (0xB8) | the chunk's offset in `Contents`: a block (u32 length, fields) starts there | [V 24: every item] |
| 0x05 (0x68) | its parent: another item's number | [V 24] |
| 0x06 (u16) | low byte 0x02 or 0x03, high byte rising through the stream | [H] |
| 0x08, 0x0B | a flag, a u16 | unknown |

An item's number is its place in the array. Items 0-255 are always empty and
the document's own chunk is item 256 (0x100) [V 24]. Where a field holds
"another chunk" (types 0x68 and 0x70), it holds such an item number: 0x107
is item 263.

### 5.3 The kinds of chunk

Counts are over the 24 samples (in how many samples).

| Kind | What | Count | Mark |
|---|---|---|---|
| 0x44 | the document (item 256) | 24 (24) | [V 24] |
| 0x43 | a page: master pages, the document's pages and the program's own (5.4) | 153 (24) | [V 24] |
| 0x01 | a frame or shape | 370 (24) | [V 24] |
| 0x10 | a table | 8 (8) | [V 8]: Sample's table, checked on its text (6.2) |
| 0x63 | a table's child (its grid, [H]) | 8 (8) | [H] |
| 0x60, 0x77 | one of each per page (the page's fields 0x09, 0x0B) | 153 each (24) | what they hold: unknown |
| others | 0x46, 0x4A, 0x4B, 0x4C, 0x4F, 0x54, 0x5B, 0x5C, 0x61, 0x65, 0x6C, 0x72, 0x73, 0x7F (one each per file); 0x20, 0x30, 0x31, 0x4D, 0x58, 0x59, 0x5A, 0x66, 0x70, 0x7D, 0x83, 0x8A, 0x8B, 0x8C | | unknown: styles, colour schemes, fonts and the like are likely [H] |

**The document (0x44)** [V 24]: 0x01 = how many pages the list holds; 0x02 =
the page list (an array of 0x70 item numbers); 0x12 = the page size, a
block with 0x01 the width and 0x02 the height in EMU (914400 an inch).
Page sizes seen: 7560000 x 10692000 (A4, 210 x 297 mm), 7772400 x 10058400
(US Letter, 8.5 x 11 in) and 10692000 x 7560000 (A4 landscape,
SampleBrochure) [V 24: each agrees with the shape of the file's thumbnail,
section 11]. Source: Sample 0x1024.

**A page (0x43)** [V 24] (Sample 0x10F6 and 0x129E):

| Field | What | Mark |
|---|---|---|
| 0x01, 0x02 | the number of frames, then the frame list (an array of 0x70 item numbers), back to front | [V 24] |
| 0x05 | the page's place on the workspace: a block, 0x01 x, 0x02 y | [V 24], see 5.4 |
| 0x06 (8 bytes) | two u32: 1 or 2 on the document's pages, then the page's number from 0; 0 on the program's own pages | [V 24], with an exception (5.4) |
| 0x09, 0x0B | the page's 0x60 and 0x77 chunks | [V 24] |
| 0x0D (0x68) | the page's master page (an item number) | [V 24] |
| 0x0E, 0x0F | a master page's letter and name, UTF-16 with a NUL ("A", "Master Page A"; "R", "Default Master" in the 2003 files) | [V 24] |
| 0x10 | 3 on a master page, 5 on the others | [V 24] |
| 0x08 | (2003 files only) a chunk | unknown |

**A frame (0x01)** [V 24] (Sample 0x1FB8): 0x27 = the frame's text id (6.2);
0xAA and 0xAB = its width and height in EMU (equal to its drawing box's,
section 7, in 289 of 290 frames); 0x02, 0x03 flags; 0x04 = 3; 0x0C and 0x0D
(8 bytes each) zero in every sample. Its position is in the drawing
(section 7), not here.

**Linked frames** [V 1: SampleNewsletter, 13 chains of 2 or 3 frames, every
one left to right on its page as its thumbnail's columns are]: frames that
share a text id are one story flowing through them. 0x28 = the frame's place
in its chain (none: the first, then 1, 2); 0x36 = the chunk of the frame
before it and 0x37 the one after. The chunks, and the drawing, may list a
chain's frames in another order (page 2: middle, left, right), so the reader
orders them by 0x28. 0x46 and the array 0x47 (items of type 0x68, chunk
numbers) [H: the objects the frame's text wraps around; 0x46 their count is
not always the array's length].

**A table (0x10)** [V 8]: 0x27 the text id; 0x66 the rows and 0x67 the
columns (3 and 2 in Sample, whose thumbnail shows its table's six cells'
text, in that order); 0x68 and 0x69 two lengths in EMU [H: width and
height]; 0x6B its 0x63 child; 0x6D an array of (x, y) pairs [H: the
columns' widths and rows' heights].

### 5.4 Which pages are the document's

The document's page list holds, in this order [V 24]: the master pages
(field 0x10 = 3), the document's pages in order, then four pages of the
program's own. The last are items 269, 272, 275 and 279 in the 2007-and-
later files, and 269, 273 and 277 with a 0x59 chunk between them in the
2003 files [V 24].

One of the program's own pages lies half a page left of and above the
others on the workspace: its field 0x05's y is the master's y minus half the
page height [V 24]. In halloween-flyer it holds one shape, which the
thumbnail doesn't show. This is the scratch area, the space around the page
[H].

The rule the reader uses [V 24, on the page counts the samples show: Sample
2, SampleBrochure 2, SampleNewsletter 4, the others 1]: a page is the
document's when it is not a master, its field 0x06's first u32 is not 0,
and its 0x05 y equals the master's. The y test is needed because in the
2003 files the scratch area's 0x06 is (2, 1), as a document page's can be
[V 3].

### 5.5 The colour scheme [V 6]

One chunk of kind 0x5C in each 2002-or-later sample [V 6]: field 0x01 is 8;
0x02 an array of 8 items, each a block whose field 0x01 is a colour, its
bytes red, green, blue then 0 (the opposite order to the drawing's), or an
empty item (0x78) for black; 0x06 the scheme's name in UTF-16 ("Bluebird",
"Waterfall", "Black & White") [V 6]. A drawing colour with 0x08 in its top
byte names a place in this list: SampleBrochure's 0x08000002 fills its
heading box with its third colour, #9999CC, which its thumbnail shows
light blue-grey; its 0x08000000 lines are its first, #000080, navy [V 2].
Empty items as black: [H, though "Black & White" has two].

## 6. The text: `Quill/QuillSub/CONTENTS`

### 6.1 Its directory [V 26]

The stream starts `CHNKINK ` [V 26]. Its directory is a chain of blocks. The
first block's 8-byte head is at 0x18: u16 0x1F8, u16 the entries in this
block, u32 the next block's offset (0xFFFFFFFF: none). The total number of
entries is the u16 at 0x0C. Entries follow each head, 24 bytes each:

| Offset | What |
|---|---|
| +0 | u16 0x18 |
| +2 | the chunk's name, 4 letters (`TEXT`, `STRS`, `FONT`...) |
| +6 | u16 its index (several `TOKN`, `STSH`, `FDPC` chunks are numbered) |
| +8 | u32 1 |
| +12 | its type, 4 letters (`PLC ` for some) |
| +16, +20 | u32 offset, u32 length in the stream |

Most files fit in the first block. SampleNewsletter has 46 entries: 19 at
0x20, 20 at 0xAE08, 7 at 0xB008 [V 1]. christmas has 20, the last at 0x6E08
[V 1].

Chunks seen (in how many of the 26): `TEXT`, `STRS`, `SYID`, `FONT`, `STSH`,
`FDPP`, `FDPC`, `BTEP`, `BTEC`, `SGP `, `INK `, `MCLD` (26), `PL  ` (24),
`TOKN` (18), `TCD ` (10).

### 6.2 The text [V 26]

- **`TEXT`**: all the document's text, UTF-16LE. 0x0D ends a paragraph [V 26:
  the paragraphs match the thumbnails' lines in Simple and Sample, section
  11]. 0x09 is a tab [V: SampleBrochure's opening hours, "Saturday\t 10 A.M.",
  and SampleNewsletter's by-line]. 0x0A occurs inside paragraphs ("Van
  Gogh, \nDa Vinci") [H: a line break]. Text
  that starts with "#" (Sample's "#This is a link to Apache POI") may mark a
  hyperlink [H].
- **`STRS`**: the stories (each frame's text is one story). u32 count N, u32
  8, u32 0xFF, then N u32 lengths in characters, then N records each
  starting with its own u32 length. The lengths add up to the whole of
  `TEXT` [V 26]. The stories are `TEXT`'s slices in order.
- **`SYID`**: u32 (the next free id [H]), u32 N, then N u32 text ids: story
  n's id. A frame's text id (its chunk's field 0x27, or its drawing's 0xF00D
  record) names its story through this list [V: 189 of 189 text ids in the
  drawings of 24 samples are in their file's list; the texts land in the
  frames the thumbnails show them in, Simple and Sample].
- **`FONT`**: five u32 (the second is the count), then an offset per font
  counted from +0x14, then at each offset a u16 length and the name in
  UTF-16, then a u32 [V 26]. The list holds the fonts the document uses,
  plus many script fonts every file lists ("Sylfaen", "Mangal"...).
- **Formatting** lives in `FDPC` (character runs, 6.4), `FDPP` (paragraph
  runs, 6.5), `STSH` (paragraph styles, 6.6) and `BTEC` and `BTEP` (the run
  pages' indexes [H]). Table cells: `TCD ` [H]. Fields and hyperlinks:
  `TOKN` [H].

### 6.3 Fields in the text storage [V 8]

The property blocks in `FDPC`, `FDPP` and `STSH` use section 4's types, but
an id has 11 bits: the low 3 bits of the type byte are the id's high bits.
`0C 22` is id 0x20C, type 0x20 (4 bytes); `02 02` is id 0x202, type 0x00
[V 8: all 316 FDPC and FDPP property blocks of the 8 checked-in samples
with text are used up exactly this way; the fetched corpus is still to be
run]. Source: Sample `FDPC` (stream 0x0800 on).

### 6.4 Character runs: `FDPC` [V 8]

One or more `FDPC` chunks (numbered from 0, in the text's order), each one
or more 512-byte pages. A page: u16 the number of runs n, u16, u32, then n
u32 run ends, then n u16 offsets in the page, then (at the page's end, back
to front) the runs' property blocks: a u32 length counting itself, then
fields (6.3). A run's end is a byte offset in the stream: TEXT's own offset
(0x200 in all 8) plus twice the characters [V 8: the ends land on
the paragraph and story ends that `STRS` and 0x0D give]. Runs follow on from
one page and chunk to the next. The fields seen:

| Id | What | Mark |
|---|---|---|
| 0x202 | bold: type 0x00 (no value) or a value not 0 | [V 3: Sample's "This is in bold and italic" in 2007, 2010, 2000 and 98; SampleBrochure's headings, as its thumbnail shows them] |
| 0x203 | italic, the same way | [V 1 (in 4 saves): Sample] |
| 0x20C | the size in EMU (12700 a point) | [V 1 (in 4 saves): Sample's "Arial, 20 point font" is 254000; its 18-point text 228600] |
| 0x224 | the typeface: an array of slots, each giving a place in `FONT`'s list (6.2). 2002 or later: slot blocks (0x88, ids 0, 1, 2...) each holding one 0x18 field; 98/2000: 0x18 fields with ids 0, 8, 0x10... The reader takes slot 0, else the first | [V 1 (in 4 saves): Sample's "It's Arial" names place 27, "Arial"]; what the other slots are for: [H] (scripts: East Asian, complex) |
| 0x237, 0x238, 0x239 | beside 0x202, 0x203 and 0x20C with the same values in the 2002-or-later files; not in the 98/2000 files | [V 6]; [H]: complex-script bold, italic and size |
| 0x212, 0x23E | 0x809 (English, UK), 0x409, 0xC09 | [H]: languages (Windows LCIDs) |
| 0x22D | set on SampleBrochure's en dashes | unknown |
| 0x21E | underline: type 0x10, its low byte 1 for a single line (0: none); the high byte 8 or 4, unknown. Only 1 has been seen: the reader takes any low byte but 0 as a single line, so other kinds (double, dotted), if Publisher has them, read as single | [V 2: every hyperlink's text in Sample (2007, 2010, 2000, 98) and the Link files has 0x801, as Publisher underlines its links; govdocs1-923566's e-mail addresses and its "New Positions" headings have 0x401]; read as underline from 0.3.29 |
| 0x244 | the colour: a block whose 0x200 is a place in the text's colour list, `PL` (6.9) | [V 3: christmas's "C", "Toy" (place 2, red 0xFF) and "Drive" (place 4, 0x6600 green) as its thumbnail shows them; Sample's hyperlinks (place 1, scheme place 5, #0066FF blue); halloween-flyer's "..." (0xC0, dark red)]; 0xFFFFFFFE seen too: not known, left as the default; read from 0.3.30 |
| 0x248, 0x22E, 0x200 | on Sample's hyperlinks (0x248 a block holding 0x200 = 1; the 2000 form 0x22E = 0xE0E40005) | unknown ([H]: 0x248 a followed link's colour; 0x22E the 98/2000 colour) |

A run's fields are laid over its paragraph style's (6.6): Sample's plain
text says it is Times New Roman 10 point, has no fields of its own, and its
default style (style 0) is exactly that [V 1 in 4 saves].

### 6.5 Paragraph runs: `FDPP` [V 8]

The same pages as `FDPC`, one run per paragraph. The fields seen:

| Id | What | Mark |
|---|---|---|
| 0x219 | the paragraph's style: k is style k of 6.6. None: style 0 | [V 2: 60685's headings are 4, its body 5, which give its thumbnail's italic headings and centred text; SampleBrochure's back cover is 0xB, centred 9 point, as its thumbnail shows] |
| 0x204 | the alignment, over the style's: its low byte (0: left, 1: right, 2: centred) | [V 3 for 2: as above, and 51318's styles, 0x5A02, whose headline and line its thumbnail shows centred; the high byte: unknown]; [V 1 for 0 and 1: SampleNewsletter's "Page #" lines are 1 (right, as a page number in a header's right corner), its "Volume 1, Issue 1" says 0 over its style's 1 and sits at its frame's left in the thumbnail; govdocs1-923566 has 0xFF00 and 0xFF01]; SampleBrochure's 6 on its banner's lines: unknown |
| 0x230, 0x234, 0x213, 0x23C | | unknown ([H]: 0x213 a space in EMU, 76200 = 6 pt) |

### 6.6 Paragraph styles: `STSH` [V 8]

Three `STSH` chunks, numbered 0, 1 and 2. Each starts with u32 its length,
u32 a count n, u32 and u32 at +0x0C (0 or 4), u32 at +0x10 (the +0x08 and
+0x10 values are the same in all three of a file), then n u32 offsets
counted from +0x14 [V 8]. In `STSH` 1, entry 2k holds style k's
character properties and entry 2k+1 its paragraph properties; each is a
u16 (the entry's length in u16s, less one), a block (u32 length counting
itself, then 6.3's fields) and a u16 [V 8: every entry's block ends 2 bytes
before the next entry]. `STSH` 0 has one 6-byte entry per style (u16 0,
then an i32: -1 for style 0 [H: Publisher's built-in style numbers]);
`STSH` 2 has one 16-byte entry per `STSH` 1 entry: u16 7, i32 -1, then
u32 the style's number [V 8].

- Character properties: 6.4's ids. Style 0 is Times New Roman 10 point in
  Simple, Sample and SampleBrochure [V 3].
- Paragraph properties: 6.5's ids; also 0x202, 0x206, 0x20C (negative) and
  0x20D in 60685's style 3, a list-like style ([H]: spaces and a hanging
  indent, EMU).
- No style seems to name another as its parent [H: the `STSH` 2 i32 that
  could is -1 in all 8]. A style without a size or typeface takes style 0's
  [H].

### 6.7 Hyperlinks: `TOKN` [V 7: Sample's stories 4 and 5, and POI's six LinkAt files, whose names say where their links are]

`TOKN` n (a `PLC` chunk) holds story n's links. The six LinkAt files settle
the places: `LinkAt10` has one link at character 10, `LinkAt0And10` two at
0 and 10, and so on, and each file's text shows "LINK" there.

| Offset | What |
|---|---|
| 0x00 | u32 n, the number of links |
| 0x04, 0x08 | u32 12, u32 0x1FFFF [unknown] |
| 0x0C | n + 1 u32: each link's first character in the story, then the story's length |

Then n blocks (u32 length counting itself, then fields with 11-bit ids,
6.3), one per link. Each holds only what changed from the link before it,
so an empty block (length 4) is a link like the one before:

| Field | What | Mark |
|---|---|---|
| 0x200 | 0x8C0, in the first block only | unknown |
| 0x201 | the link's length in characters | [V 7: "LINK" 4, "LINK2" 5, "Link to a file" 14, "email link" 10, "Within doc to page 1" 20] |
| 0x202 | the kind: 1 a web address or a file, 3 a place in the publication, 4 an e-mail address | [V 3; the e-mail one is a `mailto:`] |

Then n more blocks, one per link, each with 0x200, the link's place in the
table that follows [V 3: `LinkAt10And20And30`'s three links go to its
three addresses in order; Sample's story 5 goes 1, 0, 2].

The table: u32 the size of the offsets and strings, u32 the count, 12 bytes
(a u32 that changes from file to file, then 8 zero bytes) [unknown], then
count u32 offsets counted from the first offset, and at each a u16 length
and that many UTF-16 characters [V 7: all their addresses]. A place in the
publication is one character (0x010A in Sample) [H: the page's chunk
number]; the reader keeps those links' words as plain text for now.

### 6.8 Table cells: `TCD ` [V 4: Sample, Sample98 and Sample2000's 3 x 2 table, govdocs1-923566's 4 x 2 contents table]

`TCD ` n (a `PLC` chunk, its name ending in a space) says which of story n's
paragraphs go in which cell of the table whose text it is:

| Offset | What |
|---|---|
| 0x00 | u32 n: the cells less one |
| 0x04, 0x08 | u32 0, u32 0xFF00 [unknown] |
| 0x0C | n + 1 u32: each cell's last character in the story, its last paragraph's mark; the last cell's is the story's length |

A paragraph is in the cell whose end is the first at or after its start.
Cells go across a row, then down: Sample's six paragraphs are "Table on
page 2" and "Top right", then "P2 table left" and "P2 table right", then
"Bottom Left" and "Bottom Right", the 2 x 3 its thumbnail shows; the
table's chunk (0x67, 5.3) gives the columns. The 98 and 2000 forms have the
same chunk. Merged cells: not seen yet.

### 6.9 Text colours: `PL  ` [V 3: christmas, Sample, halloween-flyer, as 6.4 says]

`PL  ` number 0 (in a later directory block in christmas and govdocs1-923566,
5 and 8 entries; Sample has 2) lists the colours the text uses; a character
run's 0x244 names one by its place:

| Offset | What |
|---|---|
| 0x00 | u32 n: the colours |
| 0x04, 0x08 | u32 0x1C (0x28 in easter-egg-hunt-flyer), u32 0 [unknown] |
| 0x0C | n blocks, one after another, each a u32 length counting itself and fields |

A block's 0x201 is the colour: 0x00BBGGRR (red in the low byte), or 0x08 in
the top byte and a place in the colour scheme (5.5), as the drawing's
colours are. Block 0 is the same in every sample (0x201 = scheme place 0,
0x202 and 0x204-0x205 = 0xFFFFFFFF, 0x203 = 0x20000000); govdocs1-923566's
block 1 has 0x202 and 0x203 as well [unknown: tints or a second colour].
Each block ends with an empty 0x206 block. The 98 and 2000 forms have no
`PL`: their colour (0x22E, 6.4) is not read yet.

## 7. The drawing: `Escher/EscherStm`

### 7.1 Records [V 24]

The stream is a run of records. Each record is u16 (a version in the low 4
bits, an "instance" number above them), u16 a type (0xF000-0xF1FF), u32 a
length, then the data. A record of version 0xF holds records. The run is:
a 0xF000 record (the drawing group), then three 0xF002 records (drawings),
each before its drawing a u32 (2, 1, 0 in every sample) [V 24]. The
drawings' 0xF008 record has instances 3, 2 and 1 in that order [V 24], and
only the third (instance 1) holds shapes that are frames in `Contents`
[V 24]: every page's frames are there (the samples' master pages have
none, so where a master's frames go is not known yet).

In a drawing, 0xF003 holds shapes (0xF004 each) and groups (0xF003 again);
a group's first 0xF004 is the group itself [V: 51318's groups and 60685's].

### 7.2 A shape's records [V 24]

| Record | What | Mark |
|---|---|---|
| 0xF00A | u32 the shape's id, u32 flags; its instance is the shape's kind ("shape number", 7.3) | [V 24] |
| 0xF00B, 0xF122 | properties: (u16 id, u32 value) times the instance; an id with bit 0x8000 has its data after the table, in table order, the value being its length | [V: WordArt's text read this way in 4 shapes] |
| 0xF010 | the box: tagged values (section 4) after a u32 length, 0x01-0x04 = left, top, right, bottom, i32 EMU, measured from the page's centre | [V: right minus left and bottom minus top equal the frame chunk's 0xAA and 0xAB in 289 of 290 frames; boxes agree with the thumbnails, Simple and Sample to 2 mm, 51318, halloween-flyer, christmas by eye] |
| 0xF009 | a group's own coordinates: 4 i32 (left, top, right, bottom) | [V: 51318, 60685, halloween] |
| 0xF00F | a member's box in its group's coordinates: 4 i32 | [V: same] |
| 0xF011 | tagged values, 0x01 = the shape's chunk in `Contents` (an item number) | [V: 473 of 473 point at an indexed chunk] |
| 0xF00D | tagged values, 0x01 = the shape's text id | [V: 189 of 189] |

So a frame's box on the page is its 0xF010 box plus half the page's width
and height. A group member's is mapped from its group's 0xF009 rectangle
onto the group's box.

### 7.3 Shape numbers seen

| Number | What the thumbnails show there | Count (samples) | Mark |
|---|---|---|---|
| 202 | text boxes | 166 (24) | [V] |
| 201 | tables (their 0xF011 chunk is kind 0x10) | 8 (8) | [V] |
| 1 | rectangles, and picture frames (those with property 0x4104) | 189 (24) | [V] |
| 20 | lines (51318's page border, 60685's dashes) | 53 (24) | [V 2 by eye] |
| 0 | groups (with 0xF009), and freeforms: christmas's rays, halloween's ground | 134 (24) | [V 2 by eye] |
| 3 | ellipses (christmas's baubles) | 47 (3) | [H] |
| 6 | 51318's corner triangles | 13 (3) | [H] |
| 136, 144 | WordArt (property 0xC0C0 holds the text) | 1, 3 (3) | [V 4] |
| 2, 12 | | 1, 2 | unknown |

Other shapes can hold text too: christmas's ellipses have (empty) text ids
[V 1].

### 7.4 Properties worked out

| Id | What | Mark |
|---|---|---|
| 0x4104 | the picture: its number (from 1) in the picture store (section 8) | [V: 51318's one picture frame and its one picture; 68 such properties in 24 samples, against reference counts adding up to 77 in the stores, so 9 references come from elsewhere [H: fills, masters]] |
| 0xC0C0 | WordArt's text, UTF-16 with a NUL | [V 4: "Haunted" and "Halloween" (halloween-flyer), "Easter Egg Hunt" (easter-egg-hunt-flyer), "Liberty" (SampleNewsletter), each where its thumbnail shows it] |
| 0xC0C5 | WordArt's font name, UTF-16 | [V 1: "skullphabet"] |
| 0x181 | fill colour; 0x1C0 line colour | colours: 0x00BBGGRR, red in the low byte [V by hue on christmas: 0x0000CCFF shows yellow, 0xCC0000 red, 0x006600 green; 5 exact matches elsewhere]. A top byte of 0x08 marks a place in the colour scheme (5.5) [V 2: 51318 and SampleBrochure, every fill and line their thumbnails show] |
| 0x1BF | fill switches: bit 4 filled, counted only when bit 20 is set; filled with no 0x181 is white, [MS-ODRAW]'s default | [V 2: as 0x181; 0x1F001C filled, 0x100000 not] |
| 0x1FF | line switches: bit 3 outlined, counted only when bit 19 is set | [V 2: 0xF000E and 0x80008 outlined (lines, borders), 0xF0006 and 0x80000 not] |
| 0x1CB | line width, EMU | [V 2: 51318's border 38100 (3 pt) and header 12700 (1 pt); SampleBrochure's panel lines 3175] |
| 0x81-0x84 | a text box's margins, EMU: left, top, right, bottom | 0x8D63 (36195) in SampleBrochure's and 51318's text boxes, 0x8EE0 (36576, 0.04 inch) in shapes [H: measured nowhere yet] |

These ids and their meanings are the ones Microsoft's published [MS-ODRAW]
specification gives its drawing properties (2.3: fill, line and text
properties); the marks say where the samples agree with it. The reader reads
only these and 0x4104 and 0xC0C0. Seen but not read: 0x384-0x387 (0x8EE0 in
most shapes), 0x17F, 0x1C2, 0x201, 0x23F, 0x33F, 0x37F, 0x3BF, and a second
table (0xF122) in most shapes.

## 8. Pictures [V 24]

The drawing group (0xF000) holds a 0xF001 store of 0xF007 entries, one per
picture. An entry: byte 0 and byte 1 the picture's type (for Windows, for
the Mac), 16 bytes an id, u16 a tag, then at +20 u32 the size, +24 u32 how
many shapes use it, +28 u32 its offset in `EscherDelayStm`. There a record
starts (u16, u16 type, u32 length) whose length plus 8 is the entry's size
[V: 38 of 38 pictures].

| Record | Types | Then | Seen |
|---|---|---|---|
| 0xF01E | 6, 6 | 16-byte id, a tag byte (0xFF), then a PNG or a GIF | PNG in 6 samples, GIF in SampleBrochure [V] |
| 0xF01D | 5, 5 | 16-byte id, a tag byte, then a JPEG | [V 2] |
| 0xF01F | 7, 7 | 16-byte id, a tag byte, then a DIB (BITMAPINFOHEADER, 40 bytes) | [V 1: easter-egg-hunt-flyer, 2048 x 2048, 1 bit] |
| 0xF01B | 3, 4 | 16-byte id, then 34 bytes: u32 the full size, 4 i32 a box, 2 i32 a size (EMU), u32 the packed size, u8 0, u8 0xFE; then zlib data: a Windows metafile | [V: 13 of 13 inflate to exactly their full size and start `01 00 09 00`] |
| 0xF01A | | probably the same for EMF | [H], not seen |

A picture frame's property 0x4104 gives the entry's number (from 1).

## 9. `Contents`, the 98/2000 form [V 2, Sample98 and Sample2000]

The same document as Sample (2007) saved in both older formats: each fact
below was checked against Sample's.

- **Header:** `E8 AC 22 00 2C 01`, u32 0x08 the length, u32 0x12 a trailer's
  offset, u32 0x16 the directory's offset.
- **Directory** (Sample98 0x3AC): u16 count, then 10-byte entries: u16 flags,
  u16 the chunk's id, u16 its parent's id, u32 its offset. The ids are the
  same numbers as the 2002-or-later items (0x100 the document, 0x107 the
  master, 0x10A and 0x127 the pages, 0x125 and 0x126 page 1's frames).
- **Chunks** start with u16 a type and two mark bytes (`1E 1E` for a page in
  98, `7E 20` in 2000).
- **The document** (id 0x100): the page's width and height, u32 EMU, at +0x14
  and +0x18.
- **Pages** (type 0x14, parent 0x100): u32 at +0x14 is not 0 on the
  document's pages (1 and 6 here; 0 on the master and the program's own),
  in the directory's order. The frame list sits after an owner id at +0x1C
  (u16 in 98, u32 in 2000): u16 count, u16, u16, u32, then u16 ids.
- **Frames:** u16 type 0x08 is a text frame, 0x0A a table, 0x05 a rectangle;
  left, top, right and bottom at +6, i32 EMU from the page's centre (the
  same values as Sample's drawing). A text frame's text id is the u16 at
  +0x58. A table's rows are the u16 at +0x56, its text id the u16 at +0x66,
  its columns the u16 at +0x50 ([H]: +0x4E holds the same 2), and two lengths
  at +0x5C and +0x60 equal Sample's 0x68 and 0x69.
- **The text** is in the same Quill storage as section 6 [V 2].
- Not found yet: master pages' contents and pictures in this form.

## 10. The older form [V 1: fdo59355-1; H for what it is]

`Contents` starts like the 98/2000 form (`E8 AC 22 00 2C 01`), and its
directory reads the same way. The document chunk's mark is `78 6A`, and it
gives an A4 page at +0x14 (7560000 x 10692000). There is no Quill storage,
and the text appears neither as UTF-16 nor as 8-bit text. Frame boxes look
absolute (large positive values) rather than measured from the page's
centre. The font names are 8-bit text (at 0x1077: "Times New Roman"...).
Publisher 97 or older [H]. Not read yet.

## 11. The summary, the thumbnail, and how facts were checked

`\x05SummaryInformation` ([MS-OLEPS]) holds property 17 (PIDSI_THUMBNAIL),
VT_CF with clipboard format -1, then 3 (CF_METAFILEPICT): a Win16
METAFILEPICT (mapping mode 8, x and y extents), then a Windows metafile
whose one picture record (META_DIBBITBLT) is a bitmap of page 1, about
100 x 140 pixels [V 26]; its extents have the page's shape [V 24]. This is Publisher's own drawing of the page, and
it was the reference for this work: page shapes (A4, Letter, landscape),
where text and pictures sit (measured on Simple and Sample to within 2 mm),
and what kind each shape is. `tools/publab/thumbdib.py` writes it as a PNG.

The author's name is property 4 [V 26]: personal data, like the temporary
path in section 5.1.

## 11b. Writing (out_pub.c, step 3)

`owf_pub_write` (libowf/src/out_pub.c, owfpub.library 3.3) writes only the
2007 form that sections 4-7 describe, from an `owf_pub` the reader made:
`Contents` (header, index, document, pages, masters, frames, tables, scheme),
`Escher/EscherStm` (one F004 per frame: shape number, look, anchor in EMU
about the page centre, F011 chunk, F00D text id), `Quill/QuillSub/CONTENTS`
(TEXT, FDPC/FDPP pages, STRS, SYID, FONT, an empty STSH 0/1, TOKN, TCD, PL)
and a DocumentSummaryInformation that says 12.0. The compound file is
libowf/src/owf_cfbw.c (MS-CFB v3, 512-byte sectors, deterministic). Not
written yet, each named in the report: pictures (written as empty frames),
WordArt, groups' own records (their frames are kept, flat) and the scratch
area. tests/pub/test_pub_save.py reads every generated file back through
our own reader; whether Publisher itself opens these files has not been
checked yet.

## 12. Not known yet (phase 1's work)

- Paragraph spacing, indents and the alignments other than left, right and
  centred (6.5, 6.6; justified not seen yet, SampleBrochure's 6 unknown); style names; colour schemes (`Contents` kinds 0x46-0x8C); the
  98/2000 text colour (0x22E) and underline kinds other than single in `FDPC` (6.4).
- A frame's own text columns (none of the samples has one: their columns
  are linked frames, 5.3), text wrap (5.3's 0x47 is a guess), layering beyond the back-to-front order, rotation and flips.
- Tables' cell borders and merged cells.
- Master pages' own frames (the samples' masters are empty), headers and
  footers, page numbers.
- Links to places in the publication (6.7), fields, catalog and mail-merge
  data.
- Shape numbers beyond those in 7.3, gradients, picture crops and fills;
  rotation and flips (not looked for yet); columns and wrap.
- The 98/2000 form's masters and pictures; the older form (section 10).
- Publisher 2002 and 2016-or-later files (no samples).

## 13. PageStream

PageStream (Soft-Logik, later Grasshopper) is the Amiga's desktop
publisher: versions 1-2 (the `.pgs` era), 3, 4 and 5. GFOpenPublisher will
import it (Design-OpenPublisher.md, section 9).

No PageStream document was found on GitHub, the only place this session's
shell reaches (tests/pub/CORPUS.md). So nothing is known yet: no container,
magic bytes, version marks, or where the text and pages are. The same
clean-room rules will apply to samples from Dale or from Aminet:
`tests/pub/fetch_samples.sh` fetches Aminet's PageStream demo packages on a
PC.
