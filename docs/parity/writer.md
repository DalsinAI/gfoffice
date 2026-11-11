# Writer parity (GFOpenWrite)

Stream W's score sheet against OpenOffice Writer (docs/openoffice-parity-plan.md,
section 2). Status words are the plan's: **Done** = edits, saves, reloads and
undoes; **Part** = some of it; **Gap** = missing.

## Score

| Date | GFOpenWrite | Deliverable | Practical parity |
| --- | --- | --- | ---: |
| 9 Nov 2026 | 2.0.26 | Gap analysis | 63% |
| 11 Nov 2026 | 2.0.27 | W1 track changes | 66% |
| 11 Nov 2026 | 2.0.28 | W2 comments | 68% |
| 11 Nov 2026 | 2.0.29 | W3 fields | 70% |
| 11 Nov 2026 | 2.0.30 | W4 footnotes and endnotes | 72% |

Track changes was the largest single gap for reviewing documents, so it carries
about a third of the nine points Phase 1 is meant to add to Writer (target 72%
after W1 to W4). Comments, the other half of reviewing, add two more,
fields two more, and footnotes and endnotes the last two: Phase 1's target.

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
| Comments: insert, reply, edit, done, delete | Done | 2.0.28 | Insert > Comment... on the selection (a range) or at the caret (a point), under the review author. Tools > Comments: Next/Previous (replies passed over), Reply..., Edit Comment..., Mark Done, Delete Comment (with its replies), all undoable. Typing at a comment's place goes after it. ARexx ADDCOMMENT, COMMENTS, NEXTCOMMENT, PREVCOMMENT, GETCOMMENT, REPLYCOMMENT, EDITCOMMENT, RESOLVECOMMENT, DELETECOMMENT. |
| Comments: margin display | Done | 2.0.28 | Notes in the right margin beside their line, stacked, initials and date, the first line of the text; the one at the caret outlined, replies indented, done ones grey. Commented text is highlighted. |
| Comments: DOCX | Done | 2.0.28 | comments.xml (author, initials, date, paragraphs), commentRangeStart/End and commentReference, commentsExtended for replies and done. |
| Comments: ODT | Part | 2.0.28 | office:annotation and office:annotation-end, dc:creator, dc:date, initials. Replies and done are saved as plain comments (the save report says so). |
| Comments: other formats | Done | 2.0.28 | RTF, plain text, HTML and the Amiga formats leave comments out, and the save report says so. |
| Comments: edit text in the margin, filter by author, print in the margin | Gap | | Edited through a requester; printing leaves comments out. |
| Fields: document info, file name, word count, chapter | Done | 2.0.29 | Insert > Field: Title, Author, Subject (Project > Document Info...), File Name, Word Count and Chapter (the heading above), next to Page, Page Count, Date and Time. Values are worked out again on load, on save and by Tools > Update Fields. ARexx INSERTFIELD, DOCINFO, UPDATEFIELDS; GETTEXT shows the values. |
| Fields: user fields | Done | 2.0.29 | Tools > User Fields... sets, changes and removes named values; Insert > Field > User Field... places one. ARexx USERFIELD. |
| Fields: cross-references | Done | 2.0.29 | Insert > Cross-reference... picks a heading and shows its text or its page; the heading gets a bookmark. ARexx CROSSREF. |
| Fields: DOCX and ODT | Done | 2.0.29 | DOCX TITLE, AUTHOR, SUBJECT, FILENAME, NUMWORDS, STYLEREF, DOCVARIABLE, REF and PAGEREF (simple and complex fields), docProps/core.xml and w:docVars. ODT text:title, initial-creator, subject, file-name, word-count, chapter, user-field-get with user-field-decls, bookmark-ref and reference-ref, and the meta.xml title, creator and subject. The file's shown value is kept until fields are updated. |
| Fields: other formats | Done | 2.0.29 | RTF, plain text, HTML and PDF get the shown value. |
| Fields: number ranges, conditional and hidden text, input fields, references to tables and figures | Gap | | Cross-references point at headings only. |
| Footnotes and endnotes: insert, edit, delete | Done | 2.0.30 | Insert > Footnote... and Endnote... at the caret; Tools > Notes: Next/Previous Note, Edit Note..., Footnote <-> Endnote, Delete Note, all undoable. Typing at a mark goes after it. ARexx ADDFOOTNOTE, ADDENDNOTE, NOTES, NEXTNOTE, PREVNOTE, GETNOTE, EDITNOTE, CONVERTNOTE, DELETENOTE; GETTEXT shows the marks. |
| Footnotes and endnotes: on the page | Done | 2.0.30 | Marks numbered as they come (footnotes 1, 2, 3; endnotes i, ii, iii) or with their own label. Footnotes at the foot of the page their mark is on, under a short rule, the page's text making room for them (GFOpenLayout's ol_layout_pages_notes); endnotes after the text. |
| Footnotes and endnotes: DOCX and ODT | Done | 2.0.30 | DOCX footnotes.xml and endnotes.xml (with Word's separators), footnoteReference and endnoteReference. ODT text:note with note-class, note-citation (and its label) and note-body. |
| Footnotes and endnotes: other formats | Done | 2.0.30 | RTF its own \footnote (and \ftnalt for endnotes); HTML links each mark to its note after the text; plain text and PDF list the notes after the text. |
| Footnotes and endnotes: formatted note text, notes continued onto the next page, footnotes in columns, numbering settings (restart each page, symbols), RTF notes read | Gap | | Note text is plain paragraphs; a note taller than the room left runs on below the foot; in a document with columns footnotes go after the text; RTF notes are not read yet. |
| Mail merge, indexes, AutoCorrect | Gap | | Phase 2 |
| Document compare | Gap | | Phase 3; will build on track changes |
