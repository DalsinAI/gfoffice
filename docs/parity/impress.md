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
| 12 Nov 2026 | 0.6.30 | 81% | P7: the Master view and Slide Design |
| 12 Nov 2026 | 0.6.31 | 83% | P8: slides on the clipboard, Insert Slides from File |
| 12 Nov 2026 | 0.6.32 | 84% | P9: Export as Pictures (PNG, IFF ILBM); Phase 2 done |
| 16 Nov 2026 | 0.6.32 | 85% | WMF and EMF pictures drawn on the slide and in the PDF (#232, #267, #278, #279): charts' and objects' replacement pictures and clip art from PPTX and ODP show as drawn, not left out |
| 16 Nov 2026 (2) | 0.6.33 | 88% | Spelling: Tools > Spelling..., underlining as you type and ARexx SPELL (#337, #342, #344, #352); slide charts kept as charts with their data, saved to PPTX and ODP, with an editing core and Undo (#348, #354, #355, #356, #358); the show on a screen of its own (#281) |
| 16 Nov 2026 (3) | 0.6.34 | 89% | Gradient fills (linear and radial) read, saved, printed and shown (gap 2.8) |
| 16 Nov 2026 (4) | 0.6.35 | 89% | Gradient slide and master backgrounds (gap 2.8) |

Phase 1 target: 76% after P1 to P5. Phase 2 (P6 to P9) aims at 84%.

Re-scored 16 Nov 2026 at 12:25 against main 3569cf3, counting only what
is merged. The first 16 Nov row (main 72b77cb) came from WMF and EMF
pictures in libowf. This one adds:

- **Spelling** (#337, #342, #344, #352): the deck's text and notes are
  checked with GFOpenWrite's dictionary. Tools > Spelling... has Ignore,
  Ignore All, Add, Change and suggestions, and each change is one Undo
  step. Tools > Spelling as You Type puts a wavy line under misspelt words.
  ARexx SPELL counts, lists, checks, suggests, adds and ignores.
- **Charts kept as charts** (#348, #354, #355, #356, #358): a chart read
  from PPTX or ODP keeps its type, series, data, titles and legend. It is
  saved back to PPTX (chart part plus its embedded workbook, so
  PowerPoint's Edit Data works) and to ODP and .fodp as a chart document.
  The editor's core (on GFOpenCalc's) and putting an edited chart back
  with Undo are in. The chart window is not, so charts are not yet edited
  or drawn as charts in the program: the slide shows their box, or their
  picture when the file has one.
- **The show on a screen of its own** (#281): on AGA, or a small or
  low-colour Workbench, the show opens its own screen in the slide's
  shape. Show > Show On picks Automatic, Its Own Screen or the Workbench.

**Waiting to merge, not scored:** #291 (clipping in WMF and EMF pictures)
and the chart editor window.

**Largest gaps, workflow-weighted:**
1. Fills: gradient (above), hatching, bitmap and transparency on objects
   and backgrounds, and shadows.
2. Charts: a window to edit them, and drawing them as charts on the slide
   and in the PDF; OLE objects other than charts are still kept only.
3. Tables: merged cells, row heights, designs and banding. Also no
   PowerPoint 97-2003 (.ppt).

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
| P7 Master view | Done in 0.6.30 | View > Master Slides shows each master as a slide: its art, its title, outline, date, footer and number boxes, and its background, edited with every slide command (move, resize, type, pictures, shapes, delete, Undo); New, Duplicate, Rename, Delete and Move make, name, remove and order masters. Leaving the view (or saving, printing, the show) writes the masters back as one Undo step; a master's slides follow it, and a deleted master's go to the first (`opr_master.c`, `opr_masters_set` in `opr_edit.c`). Slide > Slide Design... puts this slide, or the slides picked in the sorter, on a master. ARexx VIEW MASTER, MASTERS, MASTER [n], SETMASTER n. Host test tests/deck/test_opr_master.c; lab: tests/arexx/presentation.rexx |
| P8 Slide clipboard, Insert Slide from file | Done in 0.6.31 | In the Slide Sorter, Cut, Copy and Paste work on the picked slides: the copies keep their pictures, fonts, master, layout, notes, transition and effects, and paste after the focused slide in this deck or another one opened later, as one Undo step. Slide > Insert Slides from File... puts every slide of another ODP or PPTX after this one the same way. A master or layout of the same name is reused, otherwise it comes along; names already taken get " (2)". Charts and embedded objects kept only as file XML are left out and counted (`opr_clip.c`, `opr_slide_insert_owned` in `opr_edit.c`). ARexx COPYSLIDES, CUTSLIDES, PASTESLIDES, INSERTFILE path. Host test tests/deck/test_opr_clip.c; lab: tests/arexx/presentation.rexx |
| P9 Export slides as pictures | Done in 0.6.32 | File > Export as Pictures... writes this slide, or every slide in the show (hidden ones left out, named Name-01.png and on), painted as the show paints it, 1024 pixels wide. PNG, or IFF ILBM (24 planes, ByteRun1) when the name ends in .iff or .ilbm. PNG is deflated through z.library when it is there and written stored otherwise (`opr_picture.c`). Needs a graphics card screen (cybergraphics.library). ARexx EXPORTPICTURES path [ALL] [WIDTH n]. Host test tests/deck/test_opr_picture.c; lab: tests/arexx/presentation.rexx |
| P10 The show on its own screen | Done in 0.6.33 | The show opens a screen of its own, the slide's shape and at least 640 x 480 (BestModeID; 8 bits on AGA, 16 or 24 on RTG), when the Workbench screen has fewer than 65,536 colours or is smaller than 640 x 480, and a backdrop window on the Workbench screen otherwise. Show > Show On (Automatic, Its Own Screen, The Workbench Screen) chooses it and is kept in ENV: and ENVARC:. On its own screen the right button gives the show's menu: Next, Previous, Go to Slide (the first 40), Black Screen, White Screen and End Show. If no mode or screen can be had, the show goes on the Workbench screen (`opr_show_place`, `opr_show_screen_size`, `opr_show_screen_depth` in `opr_show.c`) |

## What Export as Pictures does not do yet

- JPEG, GIF, BMP and SVG; a size requester (the menu writes 1024 wide; ARexx WIDTH sets it).
- Pictures on a native (AGA) screen without cybergraphics.library.

## What the slide clipboard does not do yet

- It is GFOpenPresentation's own: slides do not go through the Amiga clipboard to other programs.
- Insert Slides from File puts in every slide; there is no list to pick some.
- Charts and embedded objects the file kept as XML are left out of copies (the status line counts them).

## What the Master view does not do yet

- Editing the masters' text levels (the outline's fonts, sizes and bullets) and the title's format: a master's boxes are moved and sized, their text styles stay as the file had them.
- Slide layouts as their own pages; notes and handout masters.
- Slide Design shows the first eight masters as buttons, not as pictures.

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
