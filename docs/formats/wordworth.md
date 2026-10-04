# Wordworth (Digita): IFF FORM WOWO

What we know of Wordworth's document format, from real documents (Aminet:
`docs/misc/pwrdbamy`, `docs/misc/TapeLabel`, `docs/misc/Ww2008CalendarITA`,
`dev/e/E_Ref.ww4-7`), with EvenMore's Wordworth plugin (Chris Perver,
Aminet `text/show/EvenMorePlugins`) read for reference. Digita did not
publish the format. Numbers are big-endian; offsets are into the chunk's
data. "Sure" means every sample agrees; "guess" means it fits the samples
but needs a document made to test it.

| Chunk | Holds | How sure |
| --- | --- | --- |
| `WVRN` | Version (long 5 in the Wordworth 6 and 7 samples) | Sure |
| `WFNT` | A font: number (byte 0; 0xFF for the default), a flag byte, size in points (word at 2), name from 4 (NUL-terminated; `IF_` marks an Intellifont outline font) | Sure |
| `WDOC` | The document: page width (long at 4) and height (long at 8) in millipoints (1/1000 point); A4 is 595440 × 843336. Longs from 12 look like margins | Size sure; margins guess |
| `WKCO` | Colour palette: count at 1, then RGB triples from 2 | Sure of the layout; which pen is "black" is a guess |
| `WSTY`, `WSTT` | Paragraph styles: the name first, the style's font name inside | Layout not worked out |
| `WPAR` (36 bytes) | Format for the paragraphs that follow: alignment at 18 (0 left, 1 centre; 2 right and 3 justify are a guess), font number at 21, style at 22 (1 underline, 2 bold, 4 italic), misc at 23, pen at 24, paper at 25. Four longs at 0-15 (36000 = half an inch in millipoints) are probably indents | Font and style sure; alignment 0/1 sure; indents guess |
| `WTAB` | Tab stops for the paragraphs that follow (empty in every sample) | Not worked out |
| `WTXT` | Text, ISO-8859-1. Byte 0x0F ends a paragraph; one chunk can hold several paragraphs. Tab is 0x09 | Sure |
| `WFSC` | Format changes inside the `WTXT` before it: 12-byte entries, offset into that `WTXT` (long at 0), font (4), style (5), misc (6), pen (7), paper (8) | Sure of offset, font and style |
| `WSPC` | Follows `WFSC`, 6 bytes per change | Not worked out |
| `WPAG` | Page break | Sure |
| `WHED`, `WFOT` | The header and footer start here (both at the end of the file): page type at 0 (3 = every page), "on the first page" at 1 | Sure, as ProWrite's `HEAD` |
| `FORM` (`GTID`, `ILBM`) | Pictures and drawings | Not read yet |
