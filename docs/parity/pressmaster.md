# GFOpenPressmaster against Microsoft Publisher 2021: score

GFOpenPressmaster was renamed from GFOpenPublisher on 16 November 2026 (0.3.31). Rows before that date use the old name.

The comparator is Microsoft Publisher 2021, the last perpetual desktop release. The score is the workflow-weighted estimate used by the 9 November gap analysis (docs/gfoffice/PARITY-GAPS-2026-11-09.md). It asks whether an ordinary user can finish Publisher's documented workflow, with editing, save, reload and Undo all working. A feature that only imports or displays gets less credit than one that edits, saves and reloads. Only code merged on main is scored. The plan is teams/team2/designs/GFOPENPUBLISHER-PARITY-PLAN-20261114.md in the team notes, and the .pub facts are in docs/gfoffice/PUB-FORMAT.md.

| Date | Version | Score | What changed |
| --- | --- | --- | --- |
| 9 Nov 2026 | 0.3.16 | 58% | The gap analysis baseline (0.3.22 on 14 Nov was still 58%) |
| 16 Nov 2026 | 0.3.27 | 63% | Original .pub formatting decoded on open: character runs (#209), paragraph styles and alignment (#211), frame fills, outlines and margins (#212), linked columns in chain order (#213), hyperlinks (#224); WMF and EMF pictures drawn (#232, #267, #278, #279). Scored at main 72b77cb1 |
| 16 Nov 2026 (12:30) | 0.3.30 | 68% | Mail and catalog merge from the Mailings menu, with fields you see, edit and delete, and ARexx MERGE* (#270, #273, #280, #283, #284); New from Template with eight built-in publications (#286, #287); .pub table cells, underline and text colour decoded on open (#227, #230, #234). Scored at main 3569cf3c |

Libraries that are merged but not yet behind a menu are not scored: the compound file writer (#238), and crop marks and bleed in the PDF writer (#290, whose menu and ARexx are #295).

## Feature rows

| Publisher 2021 workflow | State | Notes |
| --- | --- | --- |
| Open .pub (2002 and later) | Done | Pages, frames, linked stories, tables with their cells, pictures. Character and paragraph formatting with underline and text colour, frame fills, outlines, margins, columns and hyperlinks come from the file |
| Open .pub 98/2000 and earlier | Part | Text and frames; less of the formatting decoded |
| Save as .pub | Gap | The writer is in review (waiting to merge below). Save is held in the app until a Windows Publisher check |
| Text boxes, linked stories, overflow | Done | Linking, unlinking, middle and tail detach and insert, overflow badge, Undo, ODP save |
| Character and paragraph formatting | Done | Ranges, runs, indent ruler, alignment |
| Columns, margins, wrap | Done | 1-16 columns, gap, four margins, Square, Tight and Top/Bottom wrap |
| Tables | Part | Insert, edit, rows and columns, fills and borders. No merged cells, explicit row heights or table designs |
| Pictures and shapes | Part | Insert, crop, move, resize, rotate, arrange, align. WMF and EMF drawn. No recolour, transparency or custom wrap contour |
| Guides and grids | Part | Page grid, margin and centre guides, snapping. No ruler guides you drag out, and no baseline grid |
| Master pages and two-page spreads | Gap | Masters and facing pages are not read or edited |
| Templates and building blocks | Part | File > New from Template... and ARexx NEWTEMPLATE: Blank A4, Flyer, Postcard, Business Card, Folded Card, Newsletter, Letterhead and Catalog Page. No building blocks gallery, and no saving your own templates |
| Mail merge, catalog merge, e-mail merge | Part | Mailings menu: CSV data source, insert merge fields, step through records or show field names, merge to a new publication, and catalog merge (many records to a page). The ARexx MERGE* commands do the same. No e-mail merge, no merge straight to the printer, and no record filter or sort |
| Commercial print | Gap | Crop marks and bleed are in the PDF writer (#290), but nothing reaches them yet: the menu (#295) and booklet PDF (#297) are in review. No design checker, no CMYK or spot colour, no Pack and Go |
| Find, Replace, clipboard | Done | Wrapped Find, Replace All, FTXT and object clipboard |
| Spelling | Gap | Not hooked to the suite speller yet |
| PDF, print, ODP save | Done | ODP save and reload; PDF; print |
| ARexx | Done | Port GFOPENPRESSMASTER (GFOPENPUBLISHER still answers for old scripts), return codes 0/5/10/20 |
| PageStream import | Gap | No sample documents yet |

## Waiting to merge (not scored)

- The .pub save chain: #242 writer, #245 pictures, #246 groups, #249 WordArt and #254 decks to .pub. Main is merged into each, and re-approval has been asked.
- Print: #295 Export PDF for a Printer (crop marks and bleed), then #297 booklet PDF.

When these land, the expected score is about 75% (save +5, print +2). It will be scored once they have merged.

## Top gaps on main today

1. Save as .pub (in review; the app's Save is held until a Windows Publisher check).
2. Master pages and two-page spreads (waiting on Dale's sample files).
3. Commercial print: printer PDF and booklet (in review), then the design checker, CMYK and Pack and Go.
