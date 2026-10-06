# OpenWrite user interface design

Version 0.2 | 6 October 2026

## Design thesis

OpenWrite should look like a word processor that evolved on the Amiga, not a
Windows or Linux office suite dropped on top of it.

The operating system supplies the visual identity. The document gets the
modern rendering.

That means:

- OpenLook owns the title bar, window border, system gadgets and system theme.
- OpenGadTools owns application controls, toolbars, fields, tabs and layout.
- OpenRTG owns the high-quality page canvas and accelerated compositing path.
- OpenDatatypes owns document objects and media.
- OpenPrint owns print preview, printer/PDF profiles and output.
- libowf owns document compatibility.
- OpenWrite owns editing, pagination, selection and document workflow.

No OpenWrite-specific skin is painted over the operating system.

## Visual character

The default marketing/reference appearance is the Open light theme. OpenWrite
must also look correct in Graphite, Ember, Clear, dark variants and Classic.

The current OpenLook conventions are part of the product:

- proportional window titles;
- CGTriumvirate 13 as the preferred proportional system font;
- approximately three pixels of breathing room above and below title text;
- theme-owned borders, close/depth/size gadgets, scroller tracks and knobs;
- system theme accent rather than an OpenWrite accent colour;
- Classic remains the OS's own appearance.

A screenshot used to review OpenWrite should always show an OpenGadTools theme,
unless the screenshot is explicitly demonstrating Classic compatibility.

## Primary window

At normal desktop widths:

    +-------------------------------------------------------------------+
    | OpenWrite - document.docx                                  system |
    +-------------------------------------------------------------------+
    | Project Edit View Insert Format Layout Datatypes Tools Help        |
    +-------------------------------------------------------------------+
    | New Open Save Print | Undo Redo | Image Table | PDF               |
    +-------------------------------------------------------------------+
    | Body Text | Font | 12 | B I U | L C R J | bullets | numbering     |
    +--------------+-----------------------------------+----------------+
    | Pages        | ruler                             | Properties     |
    | Outline      +-----------------------------------+ Styles         |
    | Styles       |                                   | Datatypes      |
    | Assets       |          document page            | OpenPrint      |
    |              |                                   |                |
    | thumbnails   |                                   | inspector      |
    |              |                                   |                |
    +--------------+-----------------------------------+----------------+
    | Page 1 of N | words | format | status     OpenRTG | zoom          |
    +-------------------------------------------------------------------+

The page is the visual focus. Application chrome is compact.

## Toolbar

The main toolbar follows the Open-app rule:

- Icons and text is the default.
- View can select icons and text, icons only, or text only.
- Every action remains available from a menu.
- No essential action exists only as a keyboard shortcut.
- Help bubbles name every toolbar action.
- Commands that open another requester end in "...".

First toolbar group:

- New
- Open...
- Save
- Print...

Second:

- Undo
- Redo

Third:

- Image...
- Table...

Fourth:

- PDF...

The toolbar automatically falls back to icons-only when the window is too
narrow rather than hiding important commands.

## Format row

The format row stays deliberately small. It is not a ribbon.

Initial controls:

- paragraph style;
- typeface;
- point size;
- bold;
- italic;
- underline;
- left / centre / right / justify;
- bullets;
- numbering.

More specialised controls belong in the Format menu or Properties inspector.

## Navigator

The left navigator has four views:

- Pages
- Outline
- Styles
- Assets

On a wide screen these appear as tabs. On narrow screens the navigator uses a
single compact "Pages v" selector so labels do not collapse into unreadable
fragments.

Page thumbnails are rendered from the real page display list, preferably
through OpenRTG. They are not separate document interpretations.

The navigator may be hidden from View.

## Inspector

The right inspector has:

- Properties
- Styles
- Datatypes
- OpenPrint

It appears automatically where there is enough width and may be hidden by the
user.

Properties contains page/document controls using ordinary sunken OpenGadTools
fields with aligned labels rather than large cards.

Datatypes shows object type, dimensions, metadata, wrapping, alt text and
rendering information for the selected embedded object.

OpenPrint shows the active printer/profile and provides a direct route into
print preview and output settings.

## Canvas and ruler

The canvas is visually quiet.

The document sits on a neutral workspace with:

- white paper;
- one-pixel page edge;
- subtle page shadow on RTG;
- horizontal ruler;
- margin and tab marks in later milestones.

OpenRTG should be used for:

- true-colour page caches;
- anti-aliased/scalable document text;
- smooth scroll;
- scaled page thumbnails;
- datatype object compositing;
- zoom;
- selection/caret overlays.

The AGA path consumes the same display list with lower-colour rendering and
integer zoom.

## Responsive behaviour

OpenWrite must be genuinely usable on classic screen sizes.

At approximately 900 pixels and wider:

- navigator shown;
- inspector shown.

Between approximately 560 and 899 pixels:

- navigator shown;
- inspector hidden;
- compact navigator selector;
- toolbar may become icons-only.

Below approximately 560 pixels:

- both side panels hide;
- menus continue to expose every action.

The document model and feature set do not change with screen size.

## Status line

The bottom status line is always visible and compact:

    Page 1 of 4 | 1,254 words | DOCX | Ready.      OpenRTG | 100%

It reports state without becoming a notification panel.

## File workflow

DOCX and ODT are prominent first-class choices.

Opening a document keeps its original format. Save does not silently convert a
DOCX into ODT.

Save As chooses the format by extension through libowf.

Classic Amiga formats are opened through the same libowf import path. Where a
legacy format is import-only, Save As offers a writable modern format rather
than pretending the legacy file can be reproduced losslessly.

Compatibility notes are surfaced in plain language.

## OpenDatatypes

Image and object insertion uses OpenDatatypes. The user sees one Insert
workflow rather than a list of decoder libraries.

Unknown objects are preserved where feasible and displayed as labelled
placeholders rather than discarded.

## OpenPrint

Print..., PDF... and print preview converge on OpenPrint.

OpenWrite owns page geometry and pagination. OpenPrint owns device discovery,
profiles, PDF generation, network/IPP transport, duplex and booklet policy.

The same page display list should feed screen preview and output.

## Performance personality

The visual design must never make the program feel heavy.

- window opens before optional services are queried;
- page caches are lazy;
- side panels do not force document reflow;
- page thumbnails are generated incrementally;
- typing invalidates only affected layout ranges;
- OpenRTG acceleration is an enhancement, not a semantic requirement.

The target feeling is immediate classic software with modern output quality.

## Current first build

The first native shell now builds as an m68020 AmigaOS program and has been
run on the scratch OS 3.2.3/OpenRTG bench.

Implemented in the first shell:

- OpenLook-native window;
- OpenGadTools main toolbar;
- compact format row;
- responsive Pages navigator;
- native page canvas and ruler;
- status line;
- wide-screen inspector layout;
- New / Open / Save / Save As wiring;
- libowf document loading and saving;
- DOCX / ODT / classic-Amiga compatibility path;
- responsive toolbar mode;
- Open, Graphite, Ember, Clear and Classic theme selection;
- first native page renderer with basic paragraph wrapping.

Not yet claimed complete:

- caret and selection;
- editing;
- undo/redo;
- final font shaping;
- real tables and embedded objects;
- OpenDatatypes insertion;
- OpenPrint preview/output;
- scrolling and multi-page navigation;
- final page-layout engine.

Those are implementation milestones, not reasons to add a heavier editor
engine.
