# GFOpenPresentation against OpenOffice Impress: stream P's score

Stream P of docs/openoffice-parity-plan.md (section 4, P). Each deliverable
re-scores the app here. The score is the workflow-weighted estimate the
9 November gap analysis used (docs/gfoffice/PARITY-GAPS-2026-11-09.md): an
ordinary user finishing Impress's documented workflow, with editing, save,
reload and Undo working. Feature rows are kept in
docs/gfoffice/Impress-Parity.md.

| Date | Version | Score | What changed |
| --- | --- | --- | --- |
| 9 Nov 2026 | 0.6.22 | 64% | The gap analysis's baseline |
| 11 Nov 2026 | 0.6.24 | 67% | P1: the Slide Sorter view |

Phase 1 target: 76% after P1 to P5.

## Phase 1 deliverables

| Deliverable | State | Notes |
| --- | --- | --- |
| P1 Slide sorter view | Done in 0.6.24 | Thumbnail grid; several slides picked (click, Shift, Ctrl); drag to move with an insertion mark; Duplicate, Delete, Hide, Show, Move Up/Down, Slide Transition on all picked slides, each one Undo step (Undo groups in `opr_edit.c`); Zoom sets the columns; keys, wheel and scroller; ARexx VIEW, PICK, PICKED, MOVEPICKED, COPYPICKED, DELETEPICKED, HIDEPICKED, SHOWPICKED. Host tests `make present-test present-check` (tests/deck/test_opr_sorter.c); lab: tests/arexx/presentation.rexx |
| P2 Notes view, notes editing, notes and handout printing | Next | The model already reads and writes notes pages |
| P3 Outline view | | |
| P4 PPTX save | | New writer under src/deck/ |
| P5 Multi-select, distribute, group/ungroup | | |

## What the sorter does not do yet

- A slide clipboard (cut, copy and paste whole slides between decks).
- Timings for several slides other than through Slide Transition's automatic
  advance.
- Picking slides with a drawn rectangle.
