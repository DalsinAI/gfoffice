# GFOpenPublisher against Microsoft Publisher 2021: score

The comparator is Microsoft Publisher 2021, the last perpetual desktop release. The score is the workflow-weighted estimate used by the 9 November gap analysis (docs/gfoffice/PARITY-GAPS-2026-11-09.md). It asks whether an ordinary user can finish Publisher's documented workflow, with editing, save, reload and Undo all working. A feature that only imports or displays gets less credit than one that edits, saves and reloads. Only code merged on main is scored. The plan is teams/team2/designs/GFOPENPUBLISHER-PARITY-PLAN-20261114.md in the team notes, and the .pub facts are in docs/gfoffice/PUB-FORMAT.md.

| Date | Version | Score | What changed |
| --- | --- | --- | --- |
| 9 Nov 2026 | 0.3.16 | 58% | The gap analysis baseline (0.3.22 on 14 Nov was still 58%) |
| 16 Nov 2026 | 0.3.27 | 63% | Original .pub formatting decoded on open: character runs (#209), paragraph styles and alignment (#211), frame fills, outlines and margins (#212), linked columns in chain order (#213), hyperlinks (#224); WMF and EMF pictures drawn (#232, #267, #278, #279). Scored at main 72b77cb1 |

Libraries that are merged but not yet behind a menu are not scored: the compound file writer (#238), the shared merge table (#264) and the publication merge (#266).

## Feature rows

| Publisher 2021 workflow | State | Notes |
| --- | --- | --- |
| Open .pub (2002 and later) | Done | Pages, frames, linked stories, tables, pictures. Character and paragraph formatting, frame fills, outlines, margins, columns and hyperlinks come from the file |
| Open .pub 98/2000 and earlier | Part | Text and frames; less of the formatting decoded |
| Save as .pub | Gap | The writer is in review (waiting to merge below). Save is held in the app until a Windows Publisher check |
| Text boxes, linked stories, overflow | Done | Linking, unlinking, middle and tail detach and insert, overflow badge, Undo, ODP save |
| Character and paragraph formatting | Done | Ranges, runs, indent ruler, alignment |
| Columns, margins, wrap | Done | 1-16 columns, gap, four margins, Square, Tight and Top/Bottom wrap |
| Tables | Part | Insert, edit, rows and columns, fills and borders. No merged cells, explicit row heights or table designs |
| Pictures and shapes | Part | Insert, crop, move, resize, rotate, arrange, align. WMF and EMF drawn. No recolour, transparency or custom wrap contour |
| Guides and grids | Part | Page grid, margin and centre guides, snapping. No ruler guides you drag out, and no baseline grid |
| Master pages and two-page spreads | Gap | Masters and facing pages are not read or edited |
| Templates and building blocks | Gap | Built-in templates are in review (waiting to merge below). No building blocks gallery |
| Mail merge, catalog merge, e-mail merge | Gap | The library has merged (#264 #266). The Mailings menu, fields and catalog merge are in review |
| Commercial print | Gap | Bleed, crop marks and booklet PDF are in review. No design checker, no CMYK or spot colour, no Pack and Go |
| Find, Replace, clipboard | Done | Wrapped Find, Replace All, FTXT and object clipboard |
| Spelling | Gap | Not hooked to the suite speller yet |
| PDF, print, ODP save | Done | ODP save and reload; PDF; print |
| ARexx | Done | Port GFOPENPUBLISHER, return codes 0/5/10/20 |
| PageStream import | Gap | No sample documents yet |

## Waiting to merge (not scored)

- The .pub save chain: #227 table cells, #230 underline, #234 text colour (reader), then #242 writer, #245 pictures, #246 groups, #249 WordArt and #254 decks to .pub. All are approved.
- The merge and print chain: #270 fields, #273 Mailings menu and ARexx, #280 field delete, #283 and #284 catalog merge, #286 and #287 templates, #290 and #295 bleed, crop marks and Export PDF for a Printer, and #297 booklet PDF. All are approved.

When both chains land, the expected score is about 74% (save +5, merge +3, templates +1, print +2). It will be scored once they have merged.

## Top gaps on main today

1. Save as .pub (in review; the app's Save is held until a Windows Publisher check).
2. Master pages and two-page spreads (waiting on Dale's sample files).
3. Mail and catalog merge, and commercial print (in review), then the design checker.
