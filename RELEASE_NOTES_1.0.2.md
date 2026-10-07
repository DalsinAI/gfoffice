# OpenWrite 1.0.2 release notes

7 October 2026

**Please update from 1.0.1.** DOCX and ODT files saved by OpenWrite 1.0.1
on the Amiga are corrupt and cannot be opened again. 1.0.2 saves them
correctly. Files saved by 1.0 and 1.0.1 on the PC (OWConvert there) are not
affected, and nothing in the file formats changed.

The other fixes come from loading a real design document (Word for Mac,
17 tables, a cover picture, a footer) on AmigaOS 3.2.3 and comparing what
OpenWrite kept, drew and exported with the original.

## Fixed

- **Saved DOCX and ODT files were corrupt on the Amiga.** The compiler
  used for the Amiga build got part of zlib's compression wrong at the
  optimisation it was built with, so every compressed file OpenWrite wrote
  there was damaged. That part of zlib is now built so it is right; a test
  on the Amiga compresses and reads back data of every kind, and OpenWrite
  reopens the DOCX and ODT files it saves.
- **Find** selected the match but left the page where it was. It now turns
  to the match's page and scrolls it into view.
- **Table and rule lines** were drawn green on true-colour screens; they are
  in the document's colours.
- **Shading and borders**: paragraph shading, paragraph borders and table
  cell shading were lost. They are kept when reading and writing DOCX and
  ODT, and shown in HTML.
- **PDF export**:
  - spaces between differently formatted words were lost ("Purposeand");
    each line is now one piece of text, with even word spacing on
    justified lines;
  - only a header's or footer's first paragraph was written; now all of it;
  - table cells sit side by side in their rows;
  - pictures were left out; JPEG and PNG pictures are now included, with
    their transparency. Pictures in other formats are still left out, and
    the export says so.
- **HTML export** left out tables and pictures; they are written now, with
  cell shading, and the pictures are embedded in the page.

## Release validation

- native core/editor and spelling tests, with new tests for shading,
  borders, cells and pictures in DOCX, ODT, HTML and PDF;
- 3,206 corrupt/cut-short format cases;
- m68k AmigaOS cross-build with Chromium's zlib 1.3.1;
- on AmigaOS 3.2.3 (an AmigaChrome instance with OpenRTG): the design
  document opened and searched (five searches, each shown on its page);
  its DOCX, ODT, PDF and HTML saved on the Amiga and checked on the PC;
  the saved DOCX reopened on the Amiga.

## Known limits

- Table columns are all one width and rows are taller than their text;
  tab stops are not used. These come with the new layout engine.
- Lists are kept as text with their bullets, not as lists.
- At 100% wide pages run past the window's edge; Fit Width shows them.
- The layout still estimates line widths, so page breaks can come earlier
  than in Word, and text does not wrap round floating pictures.
