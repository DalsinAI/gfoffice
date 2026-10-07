# OpenWrite 1.0 release notes

7 October 2026

OpenWrite 1.0 is the first public native-office release of the AmigaOS 3.2
editor and libowf document stack.

## Highlights

- native m68020 AmigaOS 3.2 editor; no FPU required;
- DOCX and ODT open/save with common formatting, headers/footers, simple tables,
  hyperlinks and inline images;
- ProWrite, Final Writer/Final Copy-family SWRT, Wordworth, IFF FTXT, ANSI and
  plain-text import;
- character and paragraph formatting, styles, lists and page setup;
- native simple table insertion and row/column editing;
- embedded image insertion and datatype-backed display;
- clipboard cut/copy/paste, find/replace and grouped undo/redo;
- automatic pagination, page navigation, scrolling and zoom modes;
- bundled en_GB spelling dictionary and persistent user dictionary;
- recovery autosave after interrupted sessions;
- searchable PDF export and standard printer.device output;
- ARexx port for scripted document/open/save/export/print/text/search commands;
- C:OWConvert for scripted conversion on any 68000.

## Release validation

The 1.0 candidate passed:

- native core/editor tests;
- spelling tests;
- core and spelling AddressSanitizer/UndefinedBehaviorSanitizer runs;
- 3,206 corrupt/cut-short format cases under the normal and sanitizer gates;
- m68k AmigaOS cross-build with zlib;
- AmigaOS 3.2.3 + OpenRTG/OpenLook SpeedLab smoke using the release binary and
  a multi-page document, with no release-blocking alert/fault reported.

Release binary sizes at qualification:

- OpenWrite: approximately 302 KiB;
- OWConvert: approximately 162 KiB.

## Known 1.0 boundaries

OpenWrite 1.0 intentionally does not claim full desktop-publishing layout.

- simple tables are supported; merged/nested/advanced Office tables may be
  approximated;
- images are inline; floating/wrapped images are post-1.0 work;
- advanced section layout, multi-column sections, footnotes/endnotes and
  generated TOCs remain post-1.0;
- Word 97-2003 `.doc`, RTF, Markdown and AmigaGuide import are not 1.0 filters;
- tracked changes are accepted on DOCX import rather than edited as revision
  objects;
- unsupported advanced Office objects are reported rather than silently
  claimed as editable;
- printer.device output is the standard system print path; a fully unified
  graphics/WYSIWYG print-display pipeline remains later work;
- grammar checking is not included.

## Licence

OpenWrite is MIT, Copyright (c) 2026 Dalsin Limited.

Third-party code/data keep their own licences and notices; see
`THIRD_PARTY.md` and `licenses/`.
