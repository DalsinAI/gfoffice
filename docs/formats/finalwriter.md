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
| `TBDY` | The body's text starts | Sure |
| `RULE` (24 bytes) | A paragraph starts. Byte 9 is 1 in nearly every sample and 2 once (alignment?); bytes 15, 19 and 23 vary | Paragraph start sure; contents guess |
| `TABS` (8 bytes, one per stop) | A tab stop: type at 0-1?, position (long at 4; units not worked out) | Guess |
| `ATTR` (22 bytes) | Format of the `CHRS` that follows: its length (long at 0), font number (word at 4), size in points (7), style (9: bit 0 underline), byte 11 = 1 for a tab, 17 for an endnote mark (its number as a long at 12). Bold and italic are in the font's name (`SoftSans_Bold`) | Sure for length, font, size, underline, tab |
| `CHRS` | Text, ISO-8859-1; in the Symbol font, Symbol's characters | Sure |
| `RMST`, `LMST` | The right and left master pages; text after them is page furniture (disk labels keep everything there) | Sure |
| `FORM` (`ILBM`) | Pictures | Not read yet |

Many other chunks (`HYPH`, `SPEL`, `DOC `, `SEC1`, `PAG3`, `TOCD`, `IDXD`,
`OUTD`, `BIBD`...) hold settings, the table of contents, the index, the
outline and the bibliography; they are not read.
