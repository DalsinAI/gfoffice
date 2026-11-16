# GFOpenPressmaster against Microsoft Publisher 2021: score

GFOpenPressmaster was renamed from GFOpenPublisher on 16 November 2026 (0.3.31). Rows before that date use the old name.

The comparator is Microsoft Publisher 2021, the last perpetual desktop release. The score is the workflow-weighted estimate used by the 9 November gap analysis (docs/gfoffice/PARITY-GAPS-2026-11-09.md). It asks whether an ordinary user can finish Publisher's documented workflow, with editing, save, reload and Undo all working. A feature that only imports or displays gets less credit than one that edits, saves and reloads. Only code merged on main is scored. The plan is teams/team2/designs/GFOPENPUBLISHER-PARITY-PLAN-20261114.md in the team notes, and the .pub facts are in docs/gfoffice/PUB-FORMAT.md.

| Date | Version | Score | What changed |
| --- | --- | --- | --- |
| 9 Nov 2026 | 0.3.16 | 58% | The gap analysis baseline (0.3.22 on 14 Nov was still 58%) |
| 16 Nov 2026 | 0.3.27 | 63% | Original .pub formatting decoded on open: character runs (#209), paragraph styles and alignment (#211), frame fills, outlines and margins (#212), linked columns in chain order (#213), hyperlinks (#224); WMF and EMF pictures drawn (#232, #267, #278, #279). Scored at main 72b77cb1 |
| 16 Nov 2026 (12:30) | 0.3.30 | 68% | Mail and catalog merge from the Mailings menu, with fields you see, edit and delete, and ARexx MERGE* (#270, #273, #280, #283, #284); New from Template with eight built-in publications (#286, #287); .pub table cells, underline and text colour decoded on open (#227, #230, #234). Scored at main 3569cf3c |
| 16 Nov 2026 (15:00) | 0.3.31 | 71% | Project > Export PDF for a Printer... with crop marks and a 3 mm bleed, and ARexx EXPORT MARKS / BLEED (#295); Tools > Spelling and Spelling as You Type, which were already on main at 12:30 but were scored as a gap by mistake. The .pub save chain (#242, #245, #246, #249, #254) is merged but not scored, because the app's Save to .pub is still held. Scored at main ddf8a12 |

Code that is merged but not yet reachable from a menu or ARexx is not scored. That covers the compound file writer (#238), the .pub writer and its chain (#242, #245, #246, #249, #254, held until a Windows Publisher check) and the booklet page order in the PDF writer (#297, no menu yet).

## Feature rows

| Publisher 2021 workflow | State | Notes |
| --- | --- | --- |
| Open .pub (2002 and later) | Done | Pages, frames, linked stories, tables with their cells, pictures. Character and paragraph formatting with underline and text colour, frame fills, outlines, margins, columns and hyperlinks come from the file |
| Open .pub 98/2000 and earlier | Part | Text and frames; less of the formatting decoded |
| Save as .pub | Gap | The writer is merged (text, pictures, groups, WordArt, tables; the host tests write and read back 10 files). The app's Save to .pub is held until a Windows Publisher check |
| Text boxes, linked stories, overflow | Done | Linking, unlinking, middle and tail detach and insert, overflow badge, Undo, ODP save |
| Character and paragraph formatting | Done | Ranges, runs, indent ruler, alignment |
| Columns, margins, wrap | Done | 1-16 columns, gap, four margins, Square, Tight and Top/Bottom wrap |
| Tables | Part | Insert, edit, rows and columns, fills and borders. No merged cells, explicit row heights or table designs |
| Pictures and shapes | Part | Insert, crop, move, resize, rotate, arrange, align. WMF and EMF drawn. No recolour, transparency or custom wrap contour |
| Guides and grids | Part | Page grid, margin and centre guides, snapping. No ruler guides you drag out, and no baseline grid |
| Master pages and two-page spreads | Gap | Masters and facing pages are not read or edited |
| Templates and building blocks | Part | File > New from Template... and ARexx NEWTEMPLATE: Blank A4, Flyer, Postcard, Business Card, Folded Card, Newsletter, Letterhead and Catalog Page. No building blocks gallery, and no saving your own templates |
| Mail merge, catalog merge, e-mail merge | Part | Mailings menu: CSV data source, insert merge fields, step through records or show field names, merge to a new publication, and catalog merge (many records to a page). The ARexx MERGE* commands do the same. No e-mail merge, no merge straight to the printer, and no record filter or sort |
| Commercial print | Part | Project > Export PDF for a Printer...: crop marks and a 3 mm bleed on every page, and ARexx EXPORT MARKS / BLEED n (#295). The booklet page order (#297) has no menu yet. No design checker, no CMYK or spot colour, and no Pack and Go |
| Find, Replace, clipboard | Done | Wrapped Find, Replace All, FTXT and object clipboard |
| Spelling | Done | Tools > Spelling... checks each page's text word by word, and Spelling as You Type marks unknown words. It uses the suite dictionary (PROGDIR:Dictionaries/) |
| PDF, print, ODP save | Done | ODP save and reload; PDF; print |
| ARexx | Done | Port GFOPENPRESSMASTER (GFOPENPUBLISHER still answers for old scripts), return codes 0/5/10/20 |
| PageStream import | Gap | No sample documents yet |

## Merged but held (not scored)

- Save to .pub: unlocking it in the app after a Windows Publisher check adds about 5 points.
- Booklet PDF: a choice in Export PDF for a Printer adds about 1 point.

## Top gaps on main today

1. Save as .pub (merged; the app's Save is held until a Windows Publisher check).
2. Master pages and two-page spreads (waiting on Dale's sample files).
3. Commercial print: the booklet choice in the printer PDF, then the design checker, CMYK and Pack and Go.
