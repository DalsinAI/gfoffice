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
| 11 Nov 2026 | 0.6.25 | 69% | P2: the Notes view, notes pages and handouts |
| 11 Nov 2026 | 0.6.26 | 71% | P3: the Outline view |

Phase 1 target: 76% after P1 to P5.

## Phase 1 deliverables

| Deliverable | State | Notes |
| --- | --- | --- |
| P1 Slide sorter view | Done in 0.6.24 | Thumbnail grid; several slides picked (click, Shift, Ctrl); drag to move with an insertion mark; Duplicate, Delete, Hide, Show, Move Up/Down, Slide Transition on all picked slides, each one Undo step (Undo groups in `opr_edit.c`); Zoom sets the columns; keys, wheel and scroller; ARexx VIEW, PICK, PICKED, MOVEPICKED, COPYPICKED, DELETEPICKED, HIDEPICKED, SHOWPICKED. Host tests `make present-test present-check` (tests/deck/test_opr_sorter.c); lab: tests/arexx/presentation.rexx |
| P2 Notes view, notes editing, notes and handout printing | Done in 0.6.25 | View > Notes: the slide small at the top of its notes page and the speaker notes typed in below it, with the text calls, Format, Undo and the clipboard (`owf_slide.notes` is a text object since owfpresent.library 3.6; obj `OPR_NOTES`). Export PDF and Print in Notes view give notes pages; Project > Export Handouts gives 1, 2, 3, 4, 6 or 9 slides a page (`opr_notes.c`, `opr_pdf.c`). ARexx VIEW NOTES, GETNOTES, SETNOTES, EXPORT/PRINT NOTES and HANDOUTS n. Host test tests/deck/test_opr_notes.c |
| P3 Outline view | Done in 0.6.26 | View > Outline: each slide's title and its outline box's paragraphs as one list; Return edits a line, Shift+Return adds a line (a new slide after a title), Tab and Shift+Tab move a line in and out (a title moved in joins the slide before, a top-level line moved out starts a slide), Del deletes a line or, on a title, its slide; each change one Undo step (`opr_outline.c`). ARexx VIEW OUTLINE, OUTLINE [SET, INSERT, DEMOTE, PROMOTE, DELETE] n. Host test tests/deck/test_opr_outline.c |
| P4 PPTX save | | New writer under src/deck/ |
| P5 Multi-select, distribute, group/ungroup | | |

## What the outline does not do yet

- Typing straight into the list: a line is edited in a requester.
- Formatting in the outline (bold, font) and dragging lines; moving slides
  is in the sorter.
- Printing or exporting the outline.

## What the notes do not do yet

- A notes master or handout master to edit (header, footer, page number on
  notes pages and handouts).
- Lines to write on beside handout slides, as Impress's three a page has.
- Notes in a pane under the slide in Normal view; Find does not search them.
- PPTX notes are read but saved only through ODP until P4.

## What the sorter does not do yet

- A slide clipboard (cut, copy and paste whole slides between decks).
- Timings for several slides other than through Slide Transition's automatic
  advance.
- Picking slides with a drawn rectangle.
