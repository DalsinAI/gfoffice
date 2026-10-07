# OpenWrite 1.0.1 release notes

7 October 2026

OpenWrite 1.0.1 fixes what showed when Word and ONLYOFFICE documents were
opened in 1.0 on AmigaOS 3.2.3. Nothing in the file formats changed; 1.0's
notes (`RELEASE_NOTES_1.0.md`) still describe what OpenWrite does.

## Fixed

- **Bullets and other characters outside Latin-1** were drawn as UTF-8
  bytes (three wrong glyphs for a bullet). Text is now drawn and measured in
  the Amiga's ISO-8859-1: bullets as a middle dot; quotes, dashes, the
  ellipsis and special spaces as their nearest Latin-1 forms. The bullet-list
  button shows a dot too.
- **Tables**: each cell sat a line below the cell to its left. A row's cells
  now share a line, a cell with several paragraphs stacks them inside the
  cell, and every cell draws the row's height, so the row's lines meet.
- **Blank lines** where the layout's estimate expected a line to wrap: a
  paragraph now follows what was drawn before it, with its own spacing, and
  tables and pictures follow the text above them. Nothing is drawn over
  anything else.
- **Text colours** (a coloured heading) were drawn in the theme's text
  colour; they now show.
- **Small type** lost full stops and spaces ("AmigaOS 32", "PC,saved"):
  outline fonts such as CGTimes were asked for at a designed size, which
  returned one ready-made 10-pixel size for everything from 8 to 14 pixels.
  Outline fonts are now made at the exact size asked for; bitmap fonts keep
  their designed sizes.
- **Pictures with transparency** (a PNG logo) showed black where they are
  see-through; they are now blended onto the paper.
- **Text over a picture**: text after a picture is now placed below it.
  Floating pictures are placed in their paragraph; text does not flow round
  them yet.
- Saved ODT and DOCX files named their generator as version 0.1; they now
  say 1.0.1.

## Release validation

- native core/editor and spelling tests;
- core and spelling AddressSanitizer/UndefinedBehaviorSanitizer runs;
- 3,205 corrupt/cut-short format cases;
- m68k AmigaOS cross-build with Chromium's zlib 1.3.1;
- on AmigaOS 3.2.3 (an AmigaChrome instance with OpenRTG and the Open look),
  installed by OpenUp's OpenWrite part: a DOCX written for the test, two
  Microsoft Word for Mac documents (16 and 28 pages) and an ONLYOFFICE
  document (30 pages) opened and drawn, and each converted by OWConvert on
  the Amiga.

## Known limits

- The layout still estimates line widths to place paragraphs and page
  breaks, so page breaks can come earlier than in Word.
- Text does not wrap round floating pictures.
- Characters outside Latin-1 with no near equivalent are drawn as `?`.
