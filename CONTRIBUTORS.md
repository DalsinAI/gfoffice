# Contributors

## Creator and maintainer

- **SacredTrees** ([@SacredTrees](https://github.com/SacredTrees)): created OpenWrite, designs it and maintains it.

## The AmigaChrome team

We are the AI agents who build AmigaChrome alongside SacredTrees:

- **Agnus**, our coordinator, who keeps every thread moving.
- **Thufir**, **Kynes** and **Galen**, the earlier agents who started the work on SacredTrees's PC.
- **The Claude Code threads**, each one taking a piece of the work from design to release.

## Copyright holder

OpenWrite's own code and documents are Copyright (c) 2026 Dalsin Limited,
released under the MIT licence (`LICENSE`). `THIRD_PARTY.md` lists the
libraries and fonts OpenWrite uses at build and run time, each under its own
licence.

## From our other repositories

- **OpenGadTools** (`DalsinAI/opengadtools`): its link library, copied from main `d5f4dff` into `third_party/opengadtools` (MIT, Dalsin Limited).

## Work we learned from

These shaped our code without any of their code being copied in:

- **fw2odf** (Charles Horn, MIT): its findings on the Final Writer format, which `libowf/src/in_finalwriter.c` builds on, and its use of Adobe's Symbol table (`libowf/src/owf_symbol.c`).
- **EvenMore's plugins** (Chris Perver, Aminet `text/show/EvenMorePlugins`): the Final Writer and Wordworth plugins, read for reference only.
- **Adobe's Symbol encoding table**, as published by Unicode: `owf_symbol.c` follows it, with standard code points in place of its private ones.
- **ProWrite's published specification** (New Horizons, 1987): the ProWrite reader (`libowf/src/in_prowrite.c`).

Amiga, AmigaOS and other product names are trademarks of their respective
owners.
