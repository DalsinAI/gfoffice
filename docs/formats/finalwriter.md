# Final Writer and Final Copy (SoftWood): IFF FORM SWRT

What we know of Final Writer's document format, from real documents
(Aminet: `text/fwrit/Vorlagen.FW`, `text/fwrit/FWEtikett3350`,
`text/dtp/CDCover`), building on fw2odf (Charles Horn, MIT,
github.com/hornc/fw2odf) and with EvenMore's Final Writer plugin read for
reference. SoftWood did not publish the format. Numbers are big-endian;
offsets are into the chunk's data.

| Chunk | Holds | How sure |
| --- | --- | --- |
| `FDTA` | A font's name (NUL-terminated at 0). `ATTR`'s font number counts from the last `FDTA`: number *n* is the (*count* − 1 − *n*)th | Sure (fw2odf) |
| `TXOB` | A text frame: font name at 4 (in a 140-byte field), size in points at 147, text length (word) at 176, text from 178 | Sure in the samples |
| `CLLE` | Ends a table cell; the `RULE`/`ATTR`/`CHRS` before it are the cell's text (label sheets keep their text this way, before `TBDY`) | Sure |
| `SHD1`, `TBDY` | A section starts, then its body's text. A document has one `SHD1` per section, each followed by its body and its own `RMST` and `LMST` (the ACE volumes have 10) | Sure (10 sections in each of 4 files; MultiRead finds the same text) |
| `RULE` (24 bytes) | A paragraph starts. Word at 0: the left indent; word at 4: the first line's indent (both decipoints from the margin: 900 and 180 in bulleted lists with a tab stop at 900, 180 and 180 in program listings). Byte 9: the alignment, as `TABS`' kinds: 1 left (21,193 paragraphs), 2 right (42: the ACE volumes' chapter titles), 4 centre (86: labels, spines and covers), 8 justified (4); bits 0x20 and 0x40 also occur (headings), meaning unknown. Long at 12: the paragraph's length in characters. Word at 6 is 180 in every sample | Paragraph start and length sure; indents likely; alignment a guess for 2, 4 and 8 |
| `TABS` (8 bytes, one per stop) | A tab stop: kind (word at 0: 1 left, 6,276 stops; 2 right, 90, the contents' page numbers; 4 centre, 9, a column of single punctuation marks; 8 decimal, not seen), a word always 1, then the position (long at 4, decipoints from the margin) | Position sure; kinds likely |
| `PAG3` (112 bytes) | The page: width and height in decipoints at 2 and 4 (US Letter 6120 x 7920 in the ACE volumes, A4 5953 x 8419, a US #10 envelope 6840 x 2970, a B5 envelope 7086 x 4989) | Size sure; the rest not worked out |
| `ATTR` (22 bytes) | Format of the `CHRS` that follows: its length (long at 0), font number (word at 4), size in points (7), style (9: bit 0 underline), byte 11 = 1 for a tab, 17 for an endnote mark (its number as a long at 12). Bold and italic are in the font's name (`SoftSans_Bold`) | Sure for length, font, size, underline, tab |
| `CHRS` | Text, ISO-8859-1; in the Symbol font, Symbol's characters | Sure |
| `RMST`, `LMST` | The right and left master pages; text after them is page furniture (disk labels keep everything there) | Sure |
| `FORM` (`ILBM`) | Pictures | Not read yet |

Many other chunks (`HYPH`, `SPEL`, `DOC `, `SEC1`, `TOCD`, `IDXD`,
`OUTD`, `BIBD`...) hold settings, the table of contents, the index, the
outline and the bibliography; they are not read.
