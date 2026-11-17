# GFOpenBase against OpenOffice Base

Stream B of [the parity plan](../openoffice-parity-plan.md). Re-scored with each
deliverable. Statuses as the plan's table: Done, Part, Gap.

**Score: 64%** (16 November 2026, main 3569cf3c, after the report designer; 61% that morning on 4758e88c; 51% earlier that day on 72b77cb1, after reports, joins drawn, dBASE, Filter by Form, subform rows and ARexx rows; 38% on 12 November after B2b, forms; 33% after B2a, queries; 27% after the ARexx port, 25% after B1b, 22% after B1a, 18% after B0d, 15% after
B0c, 10% after B0a and B0b, 4% design only). GFOpenBase has a window: a
project's tables, a table designer with keys and indexes, a datasheet that
sorts and filters, with a row editor and Undo, CSV import (into a table or
a new one, types guessed) and export, and a relationships view, checked on
an Instance-32 lab copy. Its ARexx port opens a project at a table and row. Queries are designed
in a grid or typed as SQL, saved, and run into the datasheet. A table opens
as a form, one record at a time, with Find, Filter by Form and its related rows below, which
can be added, changed, deleted or opened in their own form, or as a form designed in the
form designer, with buttons and ARexx events. Joins are drawn between tables as boxes in
the query designer, and relationships in the relationships view. A query can be kept as a
VIEW, and its rows exported as CSV or a sheet. Reports, grouped and totalled on up to three levels with report, page and group
headers and footers, are kept in the project, shown page by page in a print preview, and
made as ODT, DOCX, PDF or text documents. dBASE III and IV tables and
ODS and XLSX sheets come in and go out, and ARexx reads, adds, changes and deletes rows.

| Date | Score | Merged PRs that moved it |
| --- | --- | --- |
| 16 Nov 2026, main 3569cf3c | 64% | #330, #332 the report designer: three group levels, report, page and group headers and footers with [Title] [Date] [Page] [Pages] [Group] [Value] [Count] and fields, a new page per group, the Bands window; #334, #336 print preview |
| 16 Nov 2026, main 4758e88c | 61% | #257, #263, #268, #275 the form designer, buttons and events; #282 relationships drawn; #236, #250 a query as a VIEW or CSV, renames that keep views; #326, #328 sheets in and out |
| 16 Nov 2026, main 72b77cb1 | 51% | #233 Filter by Form; #251 subform rows changed; #256 subform row opens its form; #241, #252, #260 reports; #272 joins drawn; #276 dBASE; #277 ARexx row commands |
| 12 Nov 2026 | 38% | B2b forms |

Waiting to merge, not scored: no open Base PRs.

| Feature (Base) | Status | Phase | Notes |
| --- | --- | --- | --- |
| Create/open a database file | Part | 1 | Core: `.oadb` (SQLite, `application_id` OADB, `user_version` 1) made, opened, checked (`quick_check` on open, `integrity_check`), copied (`VACUUM INTO`), compacted; plain SQLite files refused or adopted. Window: `GFOpenBase FILE [NEW]`, a plain SQLite file adopted after asking |
| Table designer, datasheet view with add/edit/delete | Part | 1 | Core: the 13 field types of the design, rules as named SQL constraints, create, change (copy and rename, one transaction), rename, drop; rows added, changed and deleted with every value checked, in transactions; datasheet pages by key. Window: table designer (every field type and rule, Show SQL), datasheet with scroller and keys, row editor; errors in plain words. B1a: the datasheet sorted (Sort and Filter..., or a click on a heading) and filtered by one test on one field. B1b: Edit > Undo of rows and imports (100 steps) |
| Primary keys, relationships | Part | 1 | Core: a Counter key, or a key of chosen fields (`PRIMARY KEY(...)`); indexes, unique or not, on up to 8 fields, kept through table changes; Lookups are real `FOREIGN KEY` clauses with refuse, cascade or clear on delete; `PRAGMA foreign_keys` on for every connection; renames follow; every relationship listed. Window: Key in the designer, an Indexes window, the relationships view (boxes and lines). Drawn in the view (core 0.13.0, GFOpenBase 0.13.0): drag a table's key onto another table's Integer field to make it a Lookup, or onto the table's name to add one, the rule on delete asked; click a line to change its rule or remove it (the field stays an Integer with its keys); refused with the reason when a row would look up a row that isn't there. Not yet: a Lookup to a table keyed by its fields |
| SQL view, query designer | Part | 2 | B2a (core 0.5, GFOpenBase 0.5.0): the designer's grid (8 tables joined by their relationships, 16 columns, sort, Show, Group/Sum/Count/Average/Min/Max, three rows of criteria in plain words, `[questions]`), the SQL it makes shown live, Type the SQL (one read-only statement), saved queries, results read only in the datasheet. Core 0.13.1 (GFOpenBase 0.13.1): Query > Save as View... keeps a query as an SQL VIEW other SQLite programs read (no [questions]; a design change or delete that would break a view is refused), Export Results as CSV... writes a query's rows with the answers it ran with, and renaming a table (on its own or in a design change) renames it in grid queries, their columns and `"Table".` in expressions; SQLite rewrites the views. Typed SQL is kept as typed. #272 (core and GFOpenBase 0.8.0): Joins... draws the tables as boxes of fields, a join made by dragging one field onto another, Inner, Left or Right, saved with the query. Core 0.15 (window to follow): Update, Append and Delete queries made in the grid (Update To as a value, `empty`, a `[question]` or `=SQL`; Append To a field; Delete as from the datasheet) or typed as one UPDATE, INSERT or DELETE, counted before they run, run all or none, one step on the Undo list, saved and following renames. Not yet: UNION, and the designer's Update/Append/Delete (in the window) |
| Forms and subforms | Part | 2 | B2b (core 0.6, GFOpenBase 0.6.0): the auto form of any table in the datasheet's sort and filter, a record bar (first, back, n of N, on, last, new, delete), edits written once when you leave the record, Undo Record, Find (contains for text, equals for other fields, on or back), and a subform for each Lookup that points at the table, its rows for the record shown read only. B2c (core 0.6.2, GFOpenBase 0.6.2): Filter by Form, criteria typed into the form's fields in the query grid's words, Look for and two Or rows, applied to the form and the datasheet. B2d (core 0.6.3, GFOpenBase 0.6.3): a subform's rows changed in the form: Add Row (its Lookup is the record's), Edit Row and Delete Row on the row chosen in the list. B2e (core 0.10.1, GFOpenBase 0.10.1): Open Form, Return or a double-click on a subform row opens its own table's form at that row, stepping through the record's rows. Form designer (core 0.14.0 to 0.14.2, GFOpenBase 0.14.1 and 0.14.2): designed forms kept in the project (oadb_forms) that follow their table's renames; Table > Open Form... shows one with its fields where the design puts them and its labels, lines and boxes; Table > Design Form... places fields, labels, lines and boxes on a grid (click, Tab or drag to choose and move, cursor keys move, Shift sizes), Save and Save As; ARexx FORMS lists the table's forms. Buttons and events (core 0.14.3, GFOpenBase 0.14.3): a designed form's buttons move through the records, add, save, delete, find, close, open another form, run a saved query or an ARexx macro, and a form's On Open, On Record, Before Save (which can refuse the save, and can ask GFOpenBase about the record as it waits), After Save and On Close events run ARexx macros; while a form is open, ARexx can't change its table or row, nor walk, add, change or delete rows. Not yet: a form's own colours and fonts, events on a single field |
| Reports | Part | 2 | #241 (core): a table or a query as a document, grouped on one field with Count, Sum, Average, Min and Max per group and for the report, sums past 64 bits read "too large". #252: the Report window (Report > Reports..., Amiga-P) with name, title, source, group and columns, reports kept in the project (`oadb_reports`), Make Document as .odt, .docx, .pdf or .txt through libowf. #260: asks before dropping changes, asks a query's `[questions]` before saving. Report designer (report 0.2.0 and 0.2.1, #330 and #332): up to three group levels (group, then, then), each a heading a size down with its own totals, or none; the bands as lines of text: the report's head and foot, a page header and footer, and each level's group header and footer, with [Title] [Date] [Page] [Pages] [Group] [Value] [Count] and the source's fields, each checked for the band it is in; each first-level group on a new page; Bands... in the Report window; kept as OADBREPORT 2 only when a new setting is used. Print preview (preview 0.1.0 and 0.1.1, #334 and #336): Preview... shows the report's pages as the PDF gets them, laid out by GFOpenWrite's layout, with Previous and Next. Lab owed (Cradle labs paused). Not yet: placing fields freely on a band, fonts and colours per band, charts and subreports, printing straight to a printer |
| CSV/DBF/Calc import and export | Done | 2 | B1a: CSV import into a table (all or none, by header or field order, separator found) and export of the datasheet as shown; round trips keep keys and values. B1b: New Table from CSV (types guessed from 200 rows, changeable), ISO-8859-1 or UTF-8 both ways, bad rows left out and listed. DBF (core 0.12.0, GFOpenBase 0.12.0): Table > Import dBASE..., Export dBASE... and New Table from dBASE... read and write dBASE III and IV tables, memos in the .dbt beside them, by field name or order, deleted records left out and counted, bad records left out and listed on request; FoxPro and Visual FoxPro tables are refused with the reason. Sheets (core 0.14 with openbase_sheet.h 0.1, #326; GFOpenBase 0.15.0): Table > Import Sheet... and New Table from Sheet... read a sheet of an .ods or .xlsx workbook (also .xls and flat ODS), chosen from a list when the book has several, by the CSV rules (header or field order, types guessed, all or none or the bad rows listed by the sheet's own row number, one step of Undo); dates, times, money, TRUE/FALSE and errors by their cells' formats, a title over a gap passed over. Table > Export Sheet... and Query > Export Results as Sheet... write the datasheet as shown, or a query's rows, as .ods or .xlsx: a bold header kept in view, numbers, money, real dates and times, TRUE/FALSE. Lab owed (Cradle labs paused) |
| Locking, recovery | Part | 1 | B0a on the lab: the dot-file lock refuses a second writer, `unix-none` takes none; the rollback journal recovers; journal mode TRUNCATE; the RAM: issue does not happen with today's build |
| ARexx | Part | 2 | Per ARexx-RULE.md. #155 (0.4.1): port GFOPENBASE with VERSION HELP ACTIVATE DEACTIVATE FAULT LASTERROR ISDIRTY GETFILE OPEN TABLES TABLE ROW QUIT (FORMS since 0.14.2), no requesters while a command runs; `FILE TABLE=t ROW=n` from the Shell. 0.10.0: FIRSTROW NEXTROW PREVROW LASTROW walk the datasheet's rows in its sort and filter and COUNTROWS counts them; GETFIELDS, GETROW [KEY k] [FIELD f], ADDROW Field=Value..., CHANGEROW [KEY k] Field=Value... and DELETEROW [KEY k] read and change rows (KEY left out: the row chosen), results in RESULT, the reason in LASTERROR, no requesters. Not yet: finding rows, running a saved query, making a report |
| Connect to other databases (ODBC/JDBC) | Gap | - | Not planned: no such stack on AmigaOS |

## Evidence

- 16 Nov second re-score: on main 3569cf3c, the report designer's merged PRs carry their host
  tests (`make base-report-test`, `make base-preview-test` and their ASan and UBSan builds), all
  green at merge. Docs only: nothing was run for this re-score beyond reading main.
- 16 Nov re-score: on main 4758e88c, `make base-test` (1357 checks) and `make base-report-test`
  (126 checks), 0 failed, run on daletop. Each merged PR above carries its own
  host tests (ASan and UBSan with `make base-check`). The Report window lab (#252, #260): a
  grouped report kept, reopened and saved as .odt and .docx and read back with
  owconvert; the drop-changes asks; "Which town?" asked, York saved the two York rows.

- B2e: `make base-test` and `make base-check` (835 checks each): the
  view of a record's subform rows is filtered and in key order, holds
  only that record's rows, a row's place in it matches the place
  `ob_child_key` takes, and the table accepts it.
- B2d: `make base-test` and `make base-check` (828 checks each): a
  subform row's key by its place in the list (none past the end, none for
  a new record, no table); a row added for the record whatever was typed
  for its Lookup, with or without the key wanted; refused for a record not
  yet saved, a key that isn't, money that isn't (nothing added); changed
  and deleted by the key it gives.
- B2c: `make base-test` and `make base-check` (803 checks each): Filter by
  Form's criteria kept a field and row at a time (any case, blank takes one
  out, 16 at most, rows 0 to 2 only); one row's criteria all hold, rows
  joined by OR; the query grid's words (`?y`, `a*`, `not a*`, between,
  dates, empty, `not Leeds`, `>= 4` on the Counter); on top of the
  one-field filter (its own OR kept inside brackets) and a sort; a record's
  place and Find through it; plain words for a bad value, a [question], a
  field that isn't, `between 1`; quotes stay text.
- B2b: `make base-test` and `make base-check` (716 checks each, ASan and
  UBSan clean): a key's place in key order and in a sorted, filtered view;
  Find on text (any case, `%` and `_` as typed), money, Yes/No, on and back,
  inside a filter; the Lookups pointing at a table, and a record's rows
  through them. The same test for AmigaOS: 716 of 716 on the lab, DH1: and
  RAM:. In GFOpenBase 0.6.0 on that lab: Customers opened as a form at its
  first record; an Email typed and saved by moving on with >; Find "ZOE" in
  Name went to Zoe (3 of 6); an order added with New in the Orders form
  (2 of 2); Zoe's form listed her two orders under "Rows in Orders
  (Customer)"; Undo Record put a typed Email back.
- B2a: `make base-test` and `make base-check` (676 checks each, ASan and
  UBSan clean): golden SQL for criteria, joins and totals; every criterion
  form run against rows; parameters asked and checked; typed SQL refused
  when it writes or is two statements; saved queries read back, renamed,
  deleted, and made in a project from core 0.4.
- ARexx (#155, Team 2's ACFind thread): `tests/arexx/base.rexx`, 40 of 40
  in the lab, including OPEN FILE x TABLE t ROW n by key and by place (#n)
  and a plain SQLite file refused with 10.
- B1a: `make base-test` and `make base-check` (361 checks each, ASan and
  UBSan clean): views (each sort and filter, `%` and `_` matched as typed,
  bad values in plain words) and CSV (an export read back byte for byte, a
  round trip into a fresh project with the same keys, header by name or
  label in any order, no header, separator found, a UTF-8 mark skipped,
  every bad file refused with its line and nothing added, an import inside
  the caller's transaction). The same test for AmigaOS: 361 of 361 on
  `t2-base-20261111`, DH1: and RAM:. In GFOpenBase 0.3.0 on that lab: a
  `;` CSV with a header and a quoted "1,250.50" imported into Customers
  (Added 4 rows), a click on Name sorted it without case, Sort and Filter
  kept Credit more than 50 (4 of 6 rows), and Export CSV wrote just those
  four rows in that order.
- B0d: `make base-test` and `make base-check` (274 checks each, ASan and
  UBSan clean): keys, indexes (made, read back, kept through renames and
  removals, a unique index the rows don't fit refused and undone),
  relationships, a design changed by another connection. The same test
  for AmigaOS: 274 of 274 on the lab copy `t2-base-20261111`, DH1: and
  RAM:. In GFOpenBase on that lab: an Orders table with a Lookup to
  Customers (cascade) and an index on Customer and Total made in the
  designer, its SQL shown, and the relationships view drawing the two
  tables, their keys in bold and the line between them. After the lab
  Amiga was reset with the project open, it reopened whole.

- B0a: DalsinAI/gfopensqlite `tests/baseprobe.c`, its results in
  [Design-OpenBase.md](../gfoffice/Design-OpenBase.md) section 16.
- B0b: `make base-test` and `make base-check` (212 checks each, ASan and
  UBSan clean); the same test built for AmigaOS passes 212 of 212 on the
  lab copy `t2-base-20261111` of Instance-32, with its projects on DH1: and
  in RAM:.
- B0c: on the same lab copy, `GFOpenBase DH1:T2Base/Shop.oadb NEW`; a
  Customers table (Text required, Currency, Date) made in the designer, two
  rows added in the row editor, "lots" in Credit refused with "Credit must
  be an amount of money, such as 1,234.50"; quit and reopened with both rows
  there; an Email field added in the designer with both rows kept.
