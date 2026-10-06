# OpenWrite

A native word processor for AmigaOS 3.2. Its working format layer opens and
saves today's documents (DOCX and ODT) and already opens ProWrite, Final
Writer, Wordworth, IFF FTXT and text. The project is extending that coverage
toward the other Amiga word processors. The native editor now has a caret,
selection, text entry, paragraph editing and bounded undo/redo. OpenPrint,
clipboard integration, ARexx and richer layout remain implementation milestones.

We, 4 October 2026: "let's do the OpenWrite project", a word processor "that
can import any of the Amiga formats, along with DOCX and the OpenDocument
formats".

Status, 6 October 2026: the native editor direction is underway. `libowf`
opens and saves ODT and DOCX; opens Wordworth, Final Writer, ProWrite, IFF
FTXT, ANSI text and plain text; and saves HTML, FTXT and plain text.

**OpenWrite 0.3-dev is now a usable native text-document word processor on
AmigaOS 3.2.3.** It remains a small m68020 C application, but now includes
caret/selection editing, formatting and type size/font controls, paragraph
styles/alignment/indents/spacing, bullets and numbering, clipboard cut/copy/
paste, find/replace, automatic pagination, explicit page breaks, page setup,
multi-page navigation, scrolling, actual-size/Fit Page/Fit Width zoom,
DOCX/ODT round-tripping, searchable direct PDF export, and `printer.device`
printing (including OpenAmigaPrint/OpenPrint when selected). See `docs/architecture/NATIVE_CORE.md` and
`docs/UX_DESIGN.md`. WebCore is no longer a baseline requirement; it may be an
optional rich component later.

The file formats remain a separate C library, **libowf**, with the Shell
command **C:OWConvert**, which still targets any 68000. The OpenWrite UI
targets the A1200 baseline (68020), with no FPU required.

Part of the Open family: OpenGadTools supplies the application UI, OpenRTG is
the accelerated document-canvas path, OpenDatatypes supplies embedded media,
and OpenPrint is the output path.

## Building and testing

On Linux (the tests need Python 3 and zlib):

```
make            # build/host/owconvert
make core-test  # native editor tests
make test       # the format/filter tests
make check      # filters + editor under AddressSanitizer and UBSan
```

For AmigaOS 3.x with bebbo's amiga-gcc 6.5 (the `os32` stove):

```
ZLIB_SRC=/path/to/zlib-1.3 ./build-os3.sh      # build/os3/OWConvert + build/os3/OpenWrite
```

`OWConvert` remains 68000-compatible; `OpenWrite` targets m68020. Without
`ZLIB_SRC`, DOCX/ODT input is unavailable, although stored output remains
possible.

## Licence

MIT, Copyright (c) 2026 Dalsin Limited (`LICENSE`). The parts it builds on
keep their own licences (`THIRD_PARTY.md`).

If you use or build on this work, we ask (we do not require) that you credit
Dalsin Limited and AmigaChrome, for example "based on OpenWrite by
Dalsin Limited".
