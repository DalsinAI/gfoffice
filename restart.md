# Restart: OpenWrite

_Written 6 October 2026 at about 23:55 UTC, while all work is paused on @SacredTrees's word (23:28 UTC). Read this first when work resumes; the newest capsule and the live PR list win if they disagree._

## What this repo is

OpenWrite: Thufir's word processor for AmigaOS 3.2 that opens and saves DOCX and ODT and opens the Amiga word processors' documents; libowf and C:OWConvert run on any Amiga.

## Where it stands

OpenWrite 0.3 is merged. The team reviewed it at 1eca729: the filters and OWConvert are solid; the editor is early (no clipboard, no scrolling past page 1, no print). @SacredTrees chose "Not yet": OpenWrite and OWConvert stay out of OpenUp for now.

## Merged lately

- #6 (c98ce67, 2026-10-06): Credit who made OpenWrite: CONTRIBUTORS.md
- #5 (59af3c2, 2026-10-06): OpenWrite 0.3: usable office editor, PDF and print
- #4 (1eca729, 2026-10-06): OpenWrite: lightweight native editor core architecture
- #1 (72e759c, 2026-10-04): libowf: save as DOCX
- #2 (aa77cdf, 2026-10-04): libowf: open ODT and DOCX
- #3 (8951f55, 2026-10-04): libowf: open Wordworth and Final Writer documents

## Open pull requests

- None.

## Next step

1. Thufir: clipboard, scrolling and print. Then re-offer the OpenUp fold (OWConvert first).

## Waiting on @SacredTrees

- Share the redesign mockups with Thufir.
- A later yes/no on folding it into OpenUp.

## Who owns it

Thufir (code); OpenWrite UI redesign thread (review).

## Capsules

Restart capsules for this repo's workstreams, in amigachrome's `capjumps/` shelf:

- [`20261006_AmigaChrome_OpenWrite_Restart_Capsule.zip`](https://github.com/DalsinAI/amigachrome/tree/main/capjumps)

Team rules that still hold: commits as SacredTrees with no co-author lines; third-party code only on "yes with review" (licence checked, commit and sha256 pinned, fetched at build, never committed); deploys with deploy_dev.py only, on a typed line.
