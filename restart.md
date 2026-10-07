# Restart: OpenWrite

_Written 7 October 2026 during the 1.0 release pass._

## What this repo is

OpenWrite is the native AmigaOS 3.2 word processor. The GUI targets 68020+
without an FPU requirement. libowf and C:OWConvert provide the shared document
format/conversion layer, with OWConvert retaining a 68000 target.

## Current state

The `release/1.0-testing` branch contains the 1.0 feature stack and release
documentation.

1.0 adds the object/productivity tier on top of the merged 0.3 editor:
native simple tables, embedded inline images, headers/footers, spelling,
recovery autosave, ARexx scripting and expanded DOCX/ODT round-tripping.

The release gate on 7 October passed native core/spell tests, ASan/UBSan,
3,206 corrupt/cut-short format cases, the m68k cross-build, and an
AmigaOS 3.2.3 + OpenRTG/OpenLook SpeedLab smoke of the 1.0 binary.

## Release sequence

1. Keep `release/1.0-testing` green.
2. Build the final m68k binaries with the 1.0 version stamp.
3. Re-run the SpeedLab smoke against those exact binaries.
4. Produce the Aminet/GitHub binary archive and checksum.
5. Merge to `main`, tag `v1.0.0`, and publish the release artifacts.

## Post-1.0

Do not refactor OpenWrite into the future shared OpenLayout engine as part of
the 1.0 release. OpenWrite 1.0 is the regression oracle for that extraction.

Post-1.0 layout work includes the shared display-list/incremental-reflow
architecture, richer shaping/typography, fully unified graphics printing and
advanced page-layout features.
