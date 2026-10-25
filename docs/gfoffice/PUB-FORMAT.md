# Microsoft Publisher's .pub format: the clean-room notes

OpenPublisher's record of what we know about .pub and where each fact came
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
| 0x10 | a table | 8 (8) | [V 8]: Sample's table, checked on its text (6.3) |
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
- **Formatting** (bold, sizes, which font, colours, paragraph alignment)
  lives in `STSH` (style sheets [H]), `FDPC` and `FDPP` (character and
  paragraph runs in 512-byte pages [H]), `BTEC` and `BTEP` (their indexes
  [H]): not worked out yet. Table cells: `TCD ` [H]. Fields and hyperlinks:
  `TOKN` [H].

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
| 0x181 | fill colour; 0x1C0 line colour [H] | colours: 0x00BBGGRR, red in the low byte [V by hue on christmas: 0x0000CCFF shows yellow, 0xCC0000 red, 0x006600 green; 5 exact matches elsewhere]. A top byte of 0x08 (47 values) marks a colour from the scheme [H] |
| 0x81-0x84 | 0x8EE0 (36576 EMU, 0.04 inch) in most text boxes | [H: the text's margins] |

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

## 12. Not known yet (phase 1's work)

- Character and paragraph formatting, styles, colour schemes (`STSH`, `FDPC`,
  `FDPP`; `Contents` kinds 0x46-0x8C).
- Linked text frames (one story through several frames), columns, text
  wrap, layering beyond the back-to-front order, rotation and flips.
- Tables' cell borders, merged cells, and which paragraph goes in which cell
  (`TCD `).
- Master pages' own frames (the samples' masters are empty), headers and
  footers, page numbers.
- Hyperlinks and fields (`TOKN`), catalog and mail-merge data.
- Shape numbers beyond those in 7.3, gradients, picture crops and fills.
- The 98/2000 form's masters and pictures; the older form (section 10).
- Publisher 2002 and 2016-or-later files (no samples).

## 13. PageStream

PageStream (Soft-Logik, later Grasshopper) is the Amiga's desktop
publisher: versions 1-2 (the `.pgs` era), 3, 4 and 5. OpenPublisher will
import it (Design-OpenPublisher.md, section 9).

No PageStream document was found on GitHub, the only place this session's
shell reaches (tests/pub/CORPUS.md). So nothing is known yet: no container,
magic bytes, version marks, or where the text and pages are. The same
clean-room rules will apply to samples from Dale or from Aminet:
`tests/pub/fetch_samples.sh` fetches Aminet's PageStream demo packages on a
PC.
