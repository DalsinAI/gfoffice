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
| 11 Nov 2026 | 0.6.27 | 74% | P4: PPTX save |
| 11 Nov 2026 | 0.6.28 | 76% | P5: several objects picked, distribute, group and ungroup |
| 12 Nov 2026 | 0.6.29 | 79% | P6: custom animation (entrance and exit effects, played in the show) |

Phase 1 target: 76% after P1 to P5. Phase 2 (P6 to P9) aims at 84%.

## Phase 1 deliverables

| Deliverable | State | Notes |
| --- | --- | --- |
| P1 Slide sorter view | Done in 0.6.24 | Thumbnail grid; several slides picked (click, Shift, Ctrl); drag to move with an insertion mark; Duplicate, Delete, Hide, Show, Move Up/Down, Slide Transition on all picked slides, each one Undo step (Undo groups in `opr_edit.c`); Zoom sets the columns; keys, wheel and scroller; ARexx VIEW, PICK, PICKED, MOVEPICKED, COPYPICKED, DELETEPICKED, HIDEPICKED, SHOWPICKED. Host tests `make present-test present-check` (tests/deck/test_opr_sorter.c); lab: tests/arexx/presentation.rexx |
| P2 Notes view, notes editing, notes and handout printing | Done in 0.6.25 | View > Notes: the slide small at the top of its notes page and the speaker notes typed in below it, with the text calls, Format, Undo and the clipboard (`owf_slide.notes` is a text object since owfpresent.library 3.6; obj `OPR_NOTES`). Export PDF and Print in Notes view give notes pages; Project > Export Handouts gives 1, 2, 3, 4, 6 or 9 slides a page (`opr_notes.c`, `opr_pdf.c`). ARexx VIEW NOTES, GETNOTES, SETNOTES, EXPORT/PRINT NOTES and HANDOUTS n. Host test tests/deck/test_opr_notes.c |
| P3 Outline view | Done in 0.6.26 | View > Outline: each slide's title and its outline box's paragraphs as one list; Return edits a line, Shift+Return adds a line (a new slide after a title), Tab and Shift+Tab move a line in and out (a title moved in joins the slide before, a top-level line moved out starts a slide), Del deletes a line or, on a title, its slide; each change one Undo step (`opr_outline.c`). ARexx VIEW OUTLINE, OUTLINE [SET, INSERT, DEMOTE, PROMOTE, DELETE] n. Host test tests/deck/test_opr_outline.c |
| P4 PPTX save | Done in 0.6.27 | Save and Save As write PowerPoint (.pptx) as well as ODP: slides with their objects where they are drawn, text with its runs, levels and fields, pictures (cropped), shapes, backgrounds, hidden slides, transitions and speaker notes; a slide master and layout for each deck master with its text levels (`opr_pptx.c`). Save writes a PPTX it opened back as PPTX. ARexx SAVEAS name.pptx. Host test tests/deck/test_opr_pptx.c (saved, read back with the PPTX reader, compared) |
| P5 Multi-select, distribute, group/ungroup | Done in 0.6.28 | Shift+click picks several objects, a drag over the slide picks those it goes round; they move together (drag, arrow keys), align to the box round them, space evenly across or down (Object > Distribute), delete together; Object > Group makes them one group that moves, aligns and deletes as one, Ungroup takes it apart; each change one Undo step (`opr_multi.c`, groups in `opr_edit.c`). ARexx PICKOBJECTS, PICKEDOBJECTS, OBJECTBOX, MOVEOBJECTS, ALIGN, DISTRIBUTE, GROUP, UNGROUP, DELETEOBJECTS. Host test tests/deck/test_opr_multi.c |

## Phase 2 deliverables

| Deliverable | State | Notes |
| --- | --- | --- |
| P6 Custom animation | Done in 0.6.29 | Slide > Custom Animation...: a list of the slide's effects with Add, Change, Remove, Move Up and Move Down; each effect is an object, an effect (Appear, Fade In, Fly In, Wipe, Dissolve In and the same five out), a direction for fly and wipe, a start (on click, with previous, after previous), a duration and a delay; each change one Undo step (`opr_anim.c`, `opr_edit.c`). The show plays them: objects with an entrance wait hidden, each click runs the next step and its with/after effects, then the next slide. ODP open reads Impress's main sequence (effects it has no player for are kept and written back as they were) and Save writes the timing tree Impress reads; owfpresent.library 3.7 adds the effect list to a slide (`owf_anim`). ARexx ANIMATIONS, ANIMATION n, ADDANIMATION, CHANGEANIMATION, DELETEANIMATION, MOVEANIMATION, ANIMSTEPS, ANIMVISIBLE. Host test tests/deck/test_opr_anim.c; lab: tests/arexx/presentation.rexx |
| P7 Master view | Next | |
| P8 Slide clipboard, Insert Slide from file | Next | |
| P9 Export slides as pictures | Next | |

## What custom animation does not do yet

- Emphasis effects and motion paths: kept from the file and written back, not played or edited.
- Animating text one paragraph or one letter at a time; sounds; triggers (start on clicking an object).
- PPTX save leaves animations out (ODP keeps them).
- A preview in the editor: the show plays them.

## What picking several objects does not do yet

- Resizing several objects or a group together (handles are for one object).
- Entering a group to edit one object in it; typing in a grouped text box
  needs Ungroup first.
- Groups inside groups made here, and the layout's boxes (title, text) in a
  group; Copy, Paste and Duplicate of several objects or a group.
- PPTX save writes a group's objects ungrouped.

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

## What PPTX save does not do yet

- Tables are saved as text boxes and groups as their objects, ungrouped
  (the save says so); charts and objects PowerPoint has no kind for are
  left out.
- One layout a master: a PPTX opened with many layouts saves each slide
  with its boxes placed, under its master's one layout.
- Animations (ODP keeps them), and ODP parts kept from the file, are not written.
- Templates and shows (.potx, .ppsx) and PowerPoint 97-2003 (.ppt).

## What the sorter does not do yet

- A slide clipboard (cut, copy and paste whole slides between decks).
- Timings for several slides other than through Slide Transition's automatic
  advance.
- Picking slides with a drawn rectangle.
