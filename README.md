# OpenWrite

OpenWrite 1.0 is a native word processor for AmigaOS 3.2.

It is a small m68020 C application with no FPU requirement and no browser,
Unix GUI or JavaScript runtime dependency. The document/file layer is the
separate C library **libowf**, and **C:OWConvert** remains usable on a 68000.

OpenWrite 1.0 includes:

- caret/selection editing with bounded grouped undo/redo;
- bold, italic, underline, font family and point size;
- paragraph styles, alignment, indents, spacing and line spacing;
- bullets and numbering;
- native simple tables with row/column editing and Tab/Shift-Tab navigation;
- embedded inline images, decoded for display through Amiga datatypes;
- headers and footers in the document model and rendered pages;
- clipboard.device cut/copy/paste using FTXT-compatible text;
- Find, Find Again, Find Previous, Replace and Replace All;
- automatic pagination and explicit page breaks;
- A4/Letter page setup, portrait/landscape and margins;
- multi-page navigation, scrolling, 100%, Fit Page and Fit Width views;
- spelling against the bundled en_GB dictionary plus a user dictionary;
- 60-second recovery autosave and interrupted-session recovery;
- an ARexx port for scripted open/save/export/print/text/search/edit commands;
- DOCX and ODT formatted import/export, including simple tables and inline images;
- searchable direct PDF export;
- standard printer.device output, including OpenAmigaPrint/OpenPrint when selected.

OpenWrite also opens ProWrite, Final Writer/Final Copy-family SWRT documents,
Wordworth, IFF FTXT, ANSI text and plain text. Import reports name
approximations and unsupported material instead of silently pretending it was
preserved.

The current native architecture is documented in
`docs/architecture/NATIVE_CORE.md`; the interaction design is in
`docs/UX_DESIGN.md`; the format matrix is in `docs/FORMATS.md`.

## Open family integration

- **OpenGadTools/OpenLook**: application UI and themes.
- **OpenRTG**: accelerated document-canvas path where available.
- **OpenDatatypes / Amiga datatypes**: embedded image/media decoding.
- **OpenPrint / printer.device**: print and output path.

The same editor remains usable without accelerated RTG hardware; AGA/planar
rendering is a supported fallback path.

## Requirements

OpenWrite:

- AmigaOS 3.2 or later;
- 68020 or later;
- no FPU required;
- small documents are intended to remain usable around 8 MB Fast RAM;
- 16-32 MB Fast RAM is the comfortable target for ordinary work.

OWConvert targets any 68000 and does not require the OpenWrite GUI.

## Building and testing

On Linux, with Python 3 and zlib:

```
make
make core-test
make spell-test
make test
make check
```

For AmigaOS 3.x with bebbo's amiga-gcc 6.5:

```
ZLIB_SRC=/path/to/zlib-1.3 ./build-os3.sh
```

This builds:

```
build/os3/OpenWrite
build/os3/OWConvert
```

Without `ZLIB_SRC`, DOCX/ODT input is unavailable although stored output
remains possible.

## 1.0 validation

The 1.0 release gate includes:

- native editor/core tests;
- spelling tests;
- AddressSanitizer and UndefinedBehaviorSanitizer runs;
- the libowf normal and sanitizer corruption/cut-short corpus;
- m68k AmigaOS cross-build;
- real AmigaOS 3.2.3/OpenRTG bench validation.

## Licence

MIT, Copyright (c) 2026 Dalsin Limited (`LICENSE`).

Third-party components and data retain their own licences and notices; see
`THIRD_PARTY.md` and `licenses/`.

If you use or build on this work, we ask, but do not require, that you credit
Dalsin Limited and AmigaChrome, for example: "based on OpenWrite by Dalsin
Limited".

## Contributors

OpenWrite is created and maintained by
[SacredTrees](https://github.com/SacredTrees), copyright Dalsin Limited.
Everyone whose work it includes is credited in `CONTRIBUTORS.md`.
