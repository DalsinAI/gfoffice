# OpenWrite native editor architecture

Version 0.2 draft | 6 October 2026

## 1. Product intent

OpenWrite (OpenAmigaWriter) is a native Amiga word processor with the lightness
of Ted and the integration expected from the OpenAmiga family. It is not a
desktop Linux office suite transplanted onto AmigaOS. The main editor is a
small C implementation built around the existing libowf document model.

The design deliberately separates four things:

1. the document model and file compatibility (libowf);
2. the editor and layout engine (OpenWrite core);
3. the Amiga user interface (OpenGadTools);
4. services supplied by OpenDatatypes, OpenPrint and OpenRTG.

The result should boot quickly, edit ordinary documents on modest 68k systems,
and scale upward on accelerated/RTG machines without changing document
semantics.

## 2. Non-goals

- No embedded browser engine in the baseline editor.
- No GTK, Qt, X11 or Motif compatibility layer.
- No JavaScript runtime requirement.
- No assumption that the machine has an FPU.
- No requirement for hundreds of megabytes of RAM.
- No dependency on proprietary Microsoft libraries.

A WebCore-backed rich view may remain an optional future component, but it is
not the baseline OpenWrite editor.

## 3. Architecture

    OpenWrite application
      |
      +-- OpenGadTools shell
      |     menus, toolbar, inspectors, requesters, shortcuts, ARexx
      |
      +-- OpenWrite native editor core
      |     selection, caret, undo, commands, layout, pagination
      |
      +-- libowf
      |     canonical document model and import/export filters
      |
      +-- service adapters
            OpenRTG       drawing/compositing/scrolling
            OpenDatatypes embedded media decode/encode/metadata
            OpenPrint     print/PDF/network output

All platform-specific code sits behind narrow interfaces. The native core is
host-buildable and testable on Linux without Amiga headers.

## 4. Canonical document model

libowf remains the canonical interchange model. DOCX, ODT, Final Writer,
Wordworth, ProWrite and future filters all map through one representation.

OpenWrite adds editor-side state that does not belong in the file model:

- stable object IDs;
- caret and selection anchors;
- dirty ranges;
- cached line/page layout;
- undo transactions;
- view state and zoom;
- spelling annotations;
- temporary composition state.

The editor must never require conversion through HTML to save a file.

## 5. Native layout engine

The native layout engine is intentionally small and page-oriented.

### 5.1 Pipeline

    owf_doc
      -> style resolution
      -> paragraph shaping
      -> line breaking
      -> block placement
      -> page breaking
      -> display list
      -> OpenRTG/AGA renderer

The first implementation supports UTF-8 text, proportional fonts, basic
character formatting, paragraph alignment, margins/indents, paragraph
spacing, line spacing, tab stops, lists, page breaks, headers/footers, simple
tables and inline images/datatype objects.

Later phases add floating objects, columns, footnotes and advanced section
layout.

### 5.2 Display lists

Layout produces a compact display list rather than painting while laying out.
Commands include glyph run, rule, rectangle, bitmap/object, clip, link region
and caret/selection overlays.

Benefits:

- OpenRTG can accelerate scrolling and blits;
- AGA can render the same list using a lower-colour backend;
- print/PDF can consume the same page geometry;
- tests can compare layout numerically without screenshots.

### 5.3 Incremental reflow

The editor tracks the earliest dirty paragraph. Reflow starts there and stops
when page/line geometry converges with the previous cached layout. Typing in a
20-page document should not relayout all 20 pages.

## 6. OpenRTG

OpenRTG is the preferred screen renderer, not a requirement for document
semantics.

Use it for chunky 16/24/32-bit document canvases, anti-aliased glyph
compositing, off-screen page caches, fast scrolling, scaled thumbnails,
datatype compositing and smooth zoom where hardware allows it.

The renderer advertises capabilities to the core. The same editor runs with a
planar/AGA fallback using dithered image output and integer zoom steps.

## 7. OpenGadTools

OpenGadTools owns the application chrome.

Primary window:

- menu strip;
- compact labelled toolbar;
- format toolbar;
- optional left navigator: Pages / Outline / Styles / Assets;
- centre page canvas;
- optional right inspector: Properties / Styles / Datatypes / OpenPrint;
- status bar.

The document canvas itself is a custom OpenGadTools gadget backed by the
OpenWrite editor core.

All commands are addressable by menu, gadget, keyboard shortcut and ARexx.

## 8. OpenDatatypes

Embedded non-text content is represented in libowf as document objects and is
resolved through OpenDatatypes.

Initial object types include ILBM/PBM, PNG/JPEG through installed datatype
support, vector/drawing datatypes where available, linked images, and opaque
embedded objects with previews.

The adapter provides identify, metadata, intrinsic size, preview bitmap,
render-at-size and optional portable export.

Unknown objects remain preserved when possible and appear as labelled
placeholders rather than disappearing.

## 9. OpenPrint

OpenPrint receives paginated OpenWrite pages plus job metadata.

One pagination model drives screen Page Layout view, print preview, physical
print and PDF export.

OpenPrint owns printer discovery, profiles, IPP/network transport, duplex,
booklet and device-specific output. OpenWrite owns document pagination,
headers/footers and page geometry.

## 10. DOCX compatibility

DOCX is a first-class compatibility format, but not the internal format.

The existing libowf OOXML work is the base. Compatibility is tracked by
feature, not by the vague claim "supports DOCX".

### Tier A: must round-trip for 0.5

- paragraphs and runs;
- Unicode text;
- common fonts and font substitution;
- bold/italic/underline/strike;
- font size and colour;
- alignment;
- indents and spacing;
- tabs;
- numbered/bulleted lists;
- page size/margins/orientation;
- page breaks;
- headers/footers;
- page/date fields;
- simple tables;
- inline images;
- hyperlinks;
- named paragraph styles.

### Tier B: preserve or approximate

- section changes;
- table borders/shading;
- floating images;
- multi-column sections;
- footnotes/endnotes;
- TOC fields;
- character styles.

### Tier C: preserve-but-not-edit initially

- comments;
- tracked changes;
- embedded Office objects;
- equations;
- SmartArt;
- macros;
- advanced drawing shapes.

Unsupported material must be named in the import report and, where feasible,
preserved as original package parts for lossless pass-through on save.

## 11. File formats and default format

ODT remains the preferred open exchange format until an OpenWrite container is
justified. DOCX is offered prominently alongside it.

Opening a DOCX and saving it again must not silently switch formats.

## 12. Performance targets

The native editor target is deliberately much smaller than the former
WebCore-first design:

- 68020 baseline;
- no FPU required by OpenWrite core;
- usable on 8 MB Fast RAM for small documents;
- comfortable target: 16-32 MB Fast RAM;
- editor executable goal: under 2 MB before optional fonts/dictionaries;
- ordinary 10-page document open-to-edit target: under two seconds on a fast
  68030-class system;
- keystroke-to-paint must remain interactive without whole-document reflow.

Accelerated 040/060/PiStorm/AC-class machines gain richer OpenRTG caching,
larger images, smooth zoom and larger document comfort.

## 13. Inspiration and licensing

We take product and architectural inspiration from lightweight word
processors such as Ted and from historical Amiga applications, but OpenWrite
remains an independent implementation.

No GPL source from Ted, Siag or AbiWord is copied into the MIT OpenWrite core.

## 14. First coding milestones

N1. Native core API and host test harness.
N2. Paragraph measurement and line breaking.
N3. Display list and null/test renderer.
N4. OpenRTG renderer adapter.
N5. OpenGadTools document gadget and caret.
N6. Editing operations plus undo/redo.
N7. DOCX Tier A gap closure in libowf.
N8. OpenDatatypes inline images.
N9. OpenPrint preview/PDF/print path.
N10. AGA fallback and low-memory tuning.

The port is successful when the same libowf document can be edited, displayed,
printed and saved without requiring WebCore or a Unix GUI compatibility layer.
