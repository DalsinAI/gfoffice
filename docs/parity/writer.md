# Writer parity (GFOpenWrite)

Stream W's score sheet against OpenOffice Writer (docs/openoffice-parity-plan.md,
section 2). Status words are the plan's: **Done** = edits, saves, reloads and
undoes; **Part** = some of it; **Gap** = missing.

## Score

| Date | GFOpenWrite | Deliverable | Practical parity |
| --- | --- | --- | ---: |
| 9 Nov 2026 | 2.0.26 | Gap analysis | 63% |
| 11 Nov 2026 | 2.0.27 | W1 track changes | 66% |

Track changes was the largest single gap for reviewing documents, so it carries
about a third of the nine points Phase 1 is meant to add to Writer (target 72%
after W1 to W4).

## Stream W features

| Feature | Status | Since | Notes |
| --- | --- | --- | --- |
| Track changes: record, show, accept/reject | Done | 2.0.27 | Tools > Track Changes. Typed and pasted text, Return, Backspace and Delete are recorded under the review author (ENV USER, or Review Author...). Insertions underlined and deletions struck through, coloured by author. Next/Previous, Accept/Reject (one, the selection, or all), all undoable. Rec shows in the status bar. ARexx TRACKCHANGES, CHANGES, NEXTCHANGE, PREVCHANGE, ACCEPTCHANGE, REJECTCHANGE, ACCEPTALL, REJECTALL, REVIEWAUTHOR. |
| Track changes: ODT | Done | 2.0.27 | text:tracked-changes with insertion and deletion regions, office:change-info (author and date), change-start/end and change marks, deleted paragraph ends, insertions across paragraphs, the record setting. |
| Track changes: DOCX | Done | 2.0.27 | w:ins, w:del with w:delText, w:moveTo/w:moveFrom (read as insertion/deletion), inserted and deleted paragraph marks, w:trackRevisions. |
| Track changes: other formats | Done | 2.0.27 | RTF, plain text, HTML and the Amiga formats are saved with every change accepted, and the save report says so. |
| Tracked format changes (w:rPrChange, w:pPrChange, ODT format-change) | Gap | | Read as the new format, with a note in the import report. |
| Tracked picture, frame and table-structure changes | Gap | | Pictures and frames typed while recording are plain insertions; table row and column deletes are not recorded. |
| Show/hide changes (final view while recording) | Gap | | |
| Comments (insert, margin, reply; ODT + DOCX) | Gap | | W2 |
| Fields (doc info, cross-references, user fields, chapter) | Part | | W3. Page, page count, date and time exist. |
| Footnotes and endnotes | Gap | | W4 |
| Mail merge, indexes, AutoCorrect | Gap | | Phase 2 |
| Document compare | Gap | | Phase 3; will build on track changes |
