# GFOpenBase: design

Version 0.2 | 11 November 2026 (0.1: 25 October 2026) | AmiCradle Team, MIT

0.2: work started on Dale's word of 11 November 2026 (the GFOffice parity plan,
`docs/openoffice-parity-plan.md`, stream B). B0's answers are in section 16;
sections 3.2, 11 and 12 say what they changed.

## 0. This is the first written design

GFOpenBase's README (DalsinAI/gfopenbase) says: "**Design complete.
Implementation queued behind GFOpenWrite.**" There was no design document behind
it:

- The repository's whole history, on both of its branches (`main` and
  `docs/licence-20261023`), holds five files: `README.md`, `CONTRIBUTORS.md`,
  `LICENSE`, `restart.md` and the repo-guard workflow. Checked by cloning it in
  full on 25 October 2026.
- amicradle's `docs/project/Project-openamigabase.md` (7 October) searched
  too and found none: "Where the design lives is unknown."

So this is GFOpenBase's first written design. The README's "Direction" list
(SQLite `.oadb` projects, visual table, relationship, query, form and report
designers, Datatype object fields, GFOpenWrite and GFOpenPrint for output, ARexx,
a runtime mode) is its starting point. Everything else here is new, and is
for Dale to approve before code. The README's "design complete" line should
point here once this is merged.

GFOpenBase is written from scratch. No LibreOffice Base, OpenOffice or other
database front-end code is read, copied or ported. SQLite is used as a
library, built by gfopensqlite from the pinned upstream amalgamation.

The pieces GFOpenBase shares with GFOpenWrite, GFOpenCalc and GFOpenPresentation
(owf.library, the PDF writer, the clipboard, spelling, the Open requester) are
in [Design-Suite-Shared.md](Design-Suite-Shared.md).

## 1. What it is

The relational database and front-end builder of the GFOffice Suite (the
GFOpen apps GFOpenWrite, GFOpenCalc, GFOpenPresentation and GFOpenBase), for AmigaOS
3.2:

- a project is one SQLite file, `.oadb`, holding the data and the design;
- tables with typed fields, keys and relationships, including fields that
  hold any Datatype object (a picture, a sound, a document);
- a visual query designer that writes SQL, and shows it;
- forms (fields bound to a table or query, subforms for related rows) and
  banded reports, printed and exported through GFOpenWrite's document model,
  GFOffice's PDF writer and GFOpenPrint;
- CSV and DBF import, CSV export, and sheets to and from GFOpenCalc;
- an ARexx port, and a runtime mode for finished front ends.

Its targets are GFOffice's: AmigaOS 3.2, 68040 code, no FPU required, usable
at 8 MB, comfortable at 16-32 MB.

### 1.1 The README's gate is met

The README queues GFOpenBase until GFOpenWrite has "a stable native baseline".
Each item of its gate, as of 25 October 2026:

| Gate item | State | Evidence |
| --- | --- | --- |
| Native application and editor shell | Done | GFOpenWrite 2.0.1 (`app/openwrite2.c`), in GFOpenUp's GFOpenWrite Part |
| GFOpenGadTools UI conventions | Done | `docs/UX_DESIGN.md`; `third_party/opengadtools` |
| DOCX and ODT core workflow | Done | `README.md`, `docs/FORMATS.md` |
| GFOpenPrint integration | Done | openamigawrite #32 merged: Print hands GFOpenPrint the PDF (`app/ow_print.c`) |
| Shared build and tooling conventions | Done | GCC 16 only (#25), 68040 only (#24), host tests and sanitizers |

## 2. What exists to build on

| Piece | Where (checked) | Gives GFOpenBase |
| --- | --- | --- |
| SQLite 3.53.4 | gfopensqlite: `build.sh`, `SOURCES` (pinned by SHA-256), `patches/`, `compat/` | The database engine (section 11 for what must change) |
| `owf_doc` and its exporters | openamigawrite `libowf`: ODT, DOCX, HTML, PDF, text | Reports as documents |
| `owf_book` and its formats | `libowf`: XLS in, CSV out; ODS and XLSX coming (W106) | Sheets in and out |
| GFOpenWrite's editor core | `src/core/editor.c` | A multi-line memo field (GadTools has none) |
| GFOpenGadTools copy | `third_party/opengadtools`: `ogt_sections` (labelled field rows), `ogt_list` (owner-drawn rows), `ogt_tabs`, `ogt_toolbar`, `ogt_status`, `ogt_pageview` | Forms, datasheets, designers, report preview |
| Datatypes | The OS; GFOpenWrite's way of decoding a picture from memory (`app/openwrite2.c`, `image_cache_get`: to `T:`, then `NewDTObject`) | Datatype object fields |
| GFOpenPrint | `C:GFOpenPrint <pdf> [printer-uri]` (gfopenprint `src/amiga/main.c`), started as GFOpenWrite's `app/ow_print.c` does | Printing reports |

## 3. Projects: `.oadb` files

### 3.1 One file

A project is one SQLite database file named `<name>.oadb`.

- **Your tables are ordinary SQL tables.** Any SQLite tool on any computer
  reads the data.
- **GFOpenBase's own design** is in tables whose names start with `oadb_`:

| Table | Holds |
| --- | --- |
| `oadb_meta` | The file's format version, the project's title, its start form, its options |
| `oadb_fields` | What SQL can't say about a field: its GFOpenBase type (Currency, Date, Datatype object...), its label, its display format, its choice list |
| `oadb_queries` | Each saved query: its designer grid and its SQL |
| `oadb_forms` | Each form's layout, as text |
| `oadb_reports` | Each report's bands and fields, as text |
| `oadb_menus` | The runtime menu (section 10) |
| `oadb_scripts` | ARexx scripts kept in the project, run by forms (6.5) |

- `PRAGMA application_id` is set to `0x4F414442` ("OADB"), so a file is known
  as a project by its header, and `PRAGMA user_version` holds the format
  version.
- Relationships are real `FOREIGN KEY` clauses, so SQLite enforces them;
  GFOpenBase turns `PRAGMA foreign_keys` on for each connection (SQLite has it
  off by default).

### 3.2 Safety

- **Locking.** gfopensqlite's default VFS is now `unix-dotfile` with an Amiga
  lock: an exclusive `Lock()` on `<database>.lck`, gone after a reset
  (gfopensqlite README, "Locking"). B0 checked it on the lab (section 16): a
  second connection that wants to write while the first holds a write
  transaction gets `SQLITE_BUSY`, and writes once the first commits.
  `unix-none` takes no lock and makes no `.lck`. GFOpenBase opens projects
  with the default (dot-file) VFS; a program that keeps its own Exec
  semaphore may open with `unix-none` (`OB_OPEN_NOLOCK`). The lock is taken
  per statement outside a transaction, which costs: 1,000 lookups by key
  took about 0.8 s on DH1: against 0.27 s for the 10,000-row insert
  (section 16). Datasheets therefore read a page in one statement.
- **Journal.** SQLite's rollback journal stays (WAL is left out of our
  build). GFOpenBase sets `journal_mode=TRUNCATE` for every project
  (`OB_JOURNAL_MODE`, src/base/ob_core.c): on the lab's DH1: a commit took
  6 ms with TRUNCATE and 7 to 20 ms with DELETE, which makes and deletes the
  journal file each time (section 16). The journal file stays beside the
  project, empty, between commits.
- **Checks.** `PRAGMA quick_check` when a project opens; `integrity_check`
  from Tools.
- **Copies.** Project > Save a Copy uses `VACUUM INTO`; Tools > Compact uses
  `VACUUM`.
- **RAM:.** The "file is not a database" failure of a fresh database in
  `RAM:` (gfopensqlite README, Known issues) does not happen with today's
  build: B0 made, wrote, closed, reopened and checked fresh databases in
  `RAM:` and `T:` with all three builds (section 16). The likeliest cause of
  the old failure is the one gfopensqlite's seek patch fixed (a write past
  the end of a file), which came after the note. Projects may be in RAM:.

## 4. Tables, fields, keys and relationships

### 4.1 Field types

| GFOpenBase type | SQL column | Stored as | Form gadget | Notes |
| --- | --- | --- | --- | --- |
| Counter | `INTEGER PRIMARY KEY` | 64-bit integer, SQLite's rowid | Text, read only | Every new table gets one, named `id` |
| Text | `TEXT` | UTF-8 | String | A maximum length is a `CHECK (length(x) <= n)` |
| Memo | `TEXT` | UTF-8, any length | GFOpenWrite's editor core in a box | Plain text in B2; formatted text later |
| Integer | `INTEGER` | 64-bit integer | Integer | |
| Number | `REAL` | IEEE double | String, checked as a number | Soft-float maths on a machine without an FPU (11.3) |
| Currency | `INTEGER` | Pence or cents | String, shown as 1,234.50 | Exact, and no floating point at all |
| Date | `TEXT` | `YYYY-MM-DD` | String, with a date format | SQLite's `date()` functions read it |
| Time, Date and time | `TEXT` | `HH:MM:SS`, `YYYY-MM-DD HH:MM:SS` | String | As Date |
| Yes/No | `INTEGER` | 0 or 1, with a `CHECK` | Checkbox | |
| Choice | `TEXT` | One of a list, with a `CHECK (x IN (...))` | Cycle (up to about 8), else a list | Or a lookup into another table (4.2) |
| Lookup | `INTEGER` | The other table's key | A list of the other table's rows | A relationship |
| Datatype object | `BLOB`, plus its name and type in `oadb_fields`' companion columns | The file's bytes | A box showing it (4.3) | Any picture, sound, text or animation a datatype reads |

Required means `NOT NULL`; a default is a `DEFAULT`; a rule is a `CHECK`.
GFOpenBase writes these for you and shows its errors in plain words ("Price
must be a number", not "CHECK constraint failed").

### 4.2 Keys and relationships

- Each table has a primary key: the Counter by default, or chosen fields
  (the designer's Key: `PRIMARY KEY(...)` in field order, each field `NOT
  NULL`). A table keyed by its fields still has SQLite's rowid, which the
  datasheet pages by; a Lookup can only point at a table with a Counter.
  (Done in B0d.)
- Indexes: unique or not, on one field or up to eight, from the table
  designer's Indexes window. They are real `CREATE INDEX` statements, read
  back from the file (`pragma_index_list`, `origin = 'c'`), and made again
  after a table change; a unique index the rows don't fit undoes the
  change. An index's name is the project's, like a table's. (Done in B0d.)
- The **relationships view** draws tables as boxes and relationships as lines
  between them, each line labelled with its rule for deletes: refuse, delete
  the related rows too (cascade), or clear them. B0d draws them; a
  relationship is made in the table designer (a Lookup field), and drawing
  a line in the view to make one comes later.
- **Changing a table.** SQLite's `ALTER TABLE` adds, renames and drops
  columns, but can't change a column's type or rules. For those, GFOpenBase does
  what SQLite's documentation describes: make the new table, copy the rows,
  drop the old, rename the new, in one transaction, with foreign-key checks
  off and checked again after.

### 4.3 Datatype object fields

- The field holds the file's bytes in a `BLOB`.
- To show it, GFOpenBase streams the blob to a file in `T:` with
  `sqlite3_blob_open` and `sqlite3_blob_read`, in pieces, so it is never
  in memory twice, then opens it with `NewDTObject`, as GFOpenWrite does with a
  picture.
- On a form it is drawn by its group: a picture scaled into its box (through
  GFOffice's picture cache, Shared E9); a sound with a Play button; anything
  else as an icon with Show (GFOpenView).
- Load..., Save As..., Paste and Copy (ILBM, through the datatype) on its menu.
- A size limit per object is in Prefs, 4 MB to start, so one photo can't fill
  an 8 MB machine.

## 5. Queries

### 5.1 The model

A query is: its tables, the joins between them, its columns (a field or an
expression, a name, shown or not, a sort, a total), and its criteria. The
designer edits that model; a generator writes SQL from it; the SQL is shown
beside the grid and can be edited.

### 5.2 The designer

```
+---------------------------------------------------------------+
|  +-----------+          +-------------+                       |
|  | Customers |          | Orders      |                       |
|  | id        |----------| customer    |                       |
|  | Name      |          | Date        |                       |
|  | Town      |          | Total       |                       |
|  +-----------+          +-------------+                       |
+---------------------------------------------------------------+
| Field    | Name      | Town      | Date        | Total        |
| Table    | Customers | Customers | Orders      | Orders       |
| Total    |           |           |             | Sum          |
| Sort     | Ascending |           |             |              |
| Show     | [x]       | [x]       | [ ]         | [x]          |
| Criteria |           | Leeds     | >= 2026-01-01 |            |
| or       |           | York      |             |              |
+---------------------------------------------------------------+
| SELECT Customers.Name, Customers.Town, sum(Orders.Total) ...  |
+---------------------------------------------------------------+
```

- Tables are dragged in from the object browser. Joins come from the
  relationships, or are drawn between fields.
- Criteria in one row are joined by AND; the rows by OR.
- A Total row makes a `GROUP BY` query (Group, Sum, Count, Average, Min, Max).

| The user types | SQL |
| --- | --- |
| `Leeds` | `= 'Leeds'` |
| `> 10`, `<= 2026-01-01` | as written, the value quoted when the field is text or a date |
| `Smith*` | `LIKE 'Smith%'` |
| `between 1 and 5` | `BETWEEN 1 AND 5` |
| `empty`, `not empty` | `IS NULL`, `IS NOT NULL` |
| `[Which town?]` | A parameter `?`, asked for in a requester when the query runs |

### 5.3 SQL both ways

- The SQL view is always there. A query the grid can't show (a sub-select, a
  `UNION`) is kept as SQL only, and the grid says so.
- GFOpenBase reads back only SQL it wrote itself, by keeping the grid with it in
  `oadb_queries`. It doesn't try to parse any SQL into a grid.
- Saving a query can also make a `VIEW`, so other SQLite tools see it.

### 5.4 Results

A datasheet (`ogt_list`'s owner-drawn rows) reads rows a page at a time, by
key where it can (`WHERE id > ? LIMIT 100`) and by `LIMIT`/`OFFSET` where it
can't. A result is never held whole, so a 100,000-row table opens as fast as
a 10-row one.

A datasheet can be sorted by one field (A to Z or Z to A, text without case,
ties in key order) and filtered by one test on one field: Equals, Is not,
Less than, More than, Contains, Starts with, Is empty, Is not empty. The
value is typed in the field's own form and checked like a row's
("1,234.50", 11/11/2026), and Contains and Starts with escape `%` and `_`,
so what is typed is what is matched. The core calls this a view (`ob_view`,
core 0.3); a sorted view pages by `LIMIT`/`OFFSET`. Table > Sort and
Filter... sets it, a click on a column's heading sorts by that column (again
for Z to A, a third time for key order), and Show All Rows clears it.

Edit > Undo (core 0.4, `ob_undo`) takes back the last row added, changed or
deleted, or the last CSV import, up to 100 steps. Each step keeps the row's
stored values as they were (`sqlite3_value_dup`), so an undone delete comes
back with its key. Steps hold only while every table's design stays as it
was (`PRAGMA schema_version`); a delete that cascades or clears rows
elsewhere can't be undone and clears the list, as does a rollback past
where it began.

### 5.5 Tests

The generator is plain C and host-tested: grids in, SQL out, compared with the
expected text; then the SQL run on the same SQLite on the PC against a small
fixture database, and the rows compared.

## 6. Forms

### 6.1 Auto forms

One click on a table makes a form: one row a field, label on the left, as
GFOpenWrite's inspector lays out its fields (`ogt_sections`). `ogt_sections`
holds 8 sections of 12 rows (`OGT_SEC_MAX`, `OGT_SEC_ROWS`); a table with more
fields gets tabs (`ogt_tabs`).

### 6.2 Designed forms

- The form designer places fields, labels, lines, boxes, pictures and buttons
  on a grid measured in the font's character width and line height, not in
  pixels, so a form survives a change of font or theme.
- A form is saved as text in `oadb_forms`.
- A field's gadget follows its type (4.1): GadTools string, integer, checkbox,
  cycle and listview gadgets in the GFOpenGadTools look; a memo is GFOpenWrite's
  editor core in a box; a Datatype object is its own box.

### 6.3 Bound fields

- A form's records come from a table or a query.
- Edits are kept for the current record and written with one `UPDATE` when
  you leave the record or Save; Esc or Undo Record puts them back.
- A navigation bar: first, previous, record n of N, next, last, new, delete,
  find.
- Find searches one field (`LIKE`); Filter by Form takes criteria typed into
  the form's own fields, as the query grid does.

### 6.4 Subforms

A subform shows the rows of a related table that belong to the current
record (an order's lines, a customer's orders) as a datasheet inside the form.
Its rows are added, changed and deleted there; Return on a row can open that
table's own form.

### 6.5 Buttons and events

GFOpenBase has no language of its own. A button, or a form's "on open", "before
save" and "after save", runs an ARexx script: one kept in the project
(`oadb_scripts`) or a file. The script talks back to GFOpenBase through its port
(section 9). A built-in action (go to a record, open a form, run a report,
print) needs no script.

## 7. Reports

### 7.1 What a report is

Bands: report header, page header, a header and footer for each grouping,
detail, page footer, report footer. Each band holds fields, labels and
totals (Sum, Count, Average, Min, Max, over the group or the report). A
report's records come from a table or a query, sorted and grouped.

### 7.2 Output through GFOpenWrite's document model

Running a report builds an `owf_doc`:

- each band's text becomes paragraphs; detail rows become a native table's
  rows (`owf_para.table_id`, `table_row`, `table_col`);
- the report's page setup becomes the document's `owf_page`; a page number in
  a band is an `OWF_RUN_FIELD`.

Then the document goes where the user asks:

| To | How |
| --- | --- |
| The screen | A preview, drawn as GFOpenWrite draws pages (GFOpenLayout) |
| PDF | GFOffice's page-drawing PDF writer (Shared E4). Today's `out_pdf.c` would put a body table's cells one under another, not side by side, so reports wait for E4 |
| A printer | That PDF to `C:GFOpenPrint`, as GFOpenWrite prints |
| ODT or DOCX | libowf's exporters, as they are |
| GFOpenWrite | Saved as ODT in `T:` and opened there, to edit before sending |

### 7.3 Labels and letters

- **Labels** are a report whose detail is a grid of cells on the page (3 x 8,
  2 x 7...), built as a table.
- **Letters** (mail merge) are an ODT or DOCX made in GFOpenWrite with named
  merge fields. GFOpenBase fills them in, one letter a record, separated by page
  breaks, or one file each. This needs merge fields in the document model
  (Shared E13).

### 7.4 Big reports

A report is built whole in memory as an `owf_doc` before output. That is
simple and reuses everything, but limits a report on an 8 MB machine to a few
thousand rows. B3 measures the limit. Writing PDF page by page, without the
whole document, is a later step if the limit bites.

## 8. Import and export

| Format | Import | Export | Phase | Notes |
| --- | --- | --- | --- | --- |
| CSV (RFC 4180) | Yes | Yes | B1 | Core 0.3-0.4 (`ob_csv_import_with`, `ob_csv_export_with`, `ob_csv_guess`): into an existing table or a new one (Table > New Table from CSV...), whose fields are named from the header (an odd header kept as the label, so the import still finds it) and typed from the first 200 rows (Yes/No, Integer, Currency, Number, Date, Time, Date and time, Text, Memo past 255 characters), each type changeable before it is made; a column called id holding whole numbers becomes the Counter and keeps its keys. Separator, character set (UTF-8 found by its mark or its bytes and turned into ISO-8859-1) and header found or chosen. All or none, or bad rows left out and listed ("Line 4: Price must be a number"). Export writes the datasheet as shown, in ISO-8859-1 or UTF-8 with its mark, CR LF. The table and its rows are made together or not at all |
| Sheets: XLS today; ODS and XLSX when W106 lands | Yes, through `owf_book` | Yes, through `owf_book` | B3 | Each sheet becomes a table, its first row the field names, its cells' types the fields' types. A table or a query's result becomes a one-sheet book, saved by libowf's book formats |
| DBF (dBase III and IV, FoxPro) | Yes | No | B3 | Worth it: a simple fixed-record format, and the one old Amiga and DOS databases most often exported. Memo files (`.dbt`, `.fpt`) later |
| SQL text | Yes | Yes | B4 | `CREATE` and `INSERT` statements, our own writer |
| Superbase and other Amiga database programs | No | No | Later | No specification we know of; only with samples from the format lab |

**GFOpenCalc.** Send to GFOpenCalc makes a book of the current table or query,
saves it as ODS in `T:`, and opens it in GFOpenCalc (by ARexx when GFOpenCalc is
running, else by starting it), as the Open requester hands a file on (Shared,
2). From GFOpenCalc to GFOpenBase is the import above, from the saved sheet, or
from the clipboard (Shared, 5).

## 9. ARexx

The port is `GFOPENBASE`, following GFOffice's conventions (Shared, 6).

| Command | Template | Does |
| --- | --- | --- |
| VERSION | | |
| OPEN, CLOSE | FILE/A; | A project |
| QUERY | SQL/A,STEM/K | Runs a `SELECT`; rows into `stem.0` (the count), `stem.1.NAME`... Without STEM, the first row's first value in RESULT |
| EXEC | SQL/A | Runs any other statement; RESULT is the number of rows changed |
| RUNQUERY | NAME/A,STEM/K | A saved query |
| FORM | NAME/A | Opens a form |
| GETFIELD, SETFIELD | FIELD/A; FIELD/A,VALUE/A | The open form's current record |
| GOTO, FIND | RECORD/A/N; FIELD/A,TEXT/A | Moves the open form |
| NEWRECORD, SAVERECORD, DELETERECORD | | |
| REPORT | NAME/A,TO/K,FORMAT/K,PRINT/S | To a PDF, ODT or DOCX file, or to GFOpenPrint |
| IMPORT, EXPORT | FILE/A,TABLE/A,FORMAT/K | CSV, DBF, sheets |
| QUIT | FORCE/S | |

Queries and statements from ARexx use bound parameters where the script gives
values (`QUERY "SELECT * FROM People WHERE Town = ?" ARGS Leeds`), so a quote
in a value can't break the SQL.

## 10. Runtime mode

A finished front end opens like an application:

- `GFOpenBase Shop.oadb RUNTIME`, or the project's icon with the tool type
  `RUNTIME`, or a project option, opens its start form (`oadb_meta`).
- The design menus, the object browser and the designers are gone; the menu
  strip holds the project's own menu (`oadb_menus`: its forms, reports and
  scripts), and Quit.
- The window title is the project's title.
- **GFOpenBaseRun**, a second, smaller program without the designers, can be
  given away with a front end. Whether to make it is a question (section 15).
- `READONLY` opens the database read only (`SQLITE_OPEN_READONLY`), for a
  kiosk or a CD.
- There are no passwords and no encryption: SQLite's free build has none, and
  any SQLite tool can open a `.oadb`. A front end keeps honest people from
  the design, not more.

## 11. SQLite on a 68040: what it costs

### 11.1 The build we have

From gfopensqlite (`README.md`, `build.sh`, `restart.md`):

| | Today |
| --- | --- |
| Version | 3.53.4, the amalgamation, pinned by SHA-256 |
| CPU | `-m68040 -m68881`: hard float, so a 68040 with its FPU (not a 68LC040) |
| Threads | `SQLITE_THREADSAFE=1`, linked with libpthread; `THREADSAFE=0` (no libpthread) for GFOpenBase, into `out-st/` and `out-soft-st/` |
| Left out | Extension loading, WAL, memory mapping, deprecated calls |
| Locking | `unix-dotfile` with an Amiga `Lock()` (3.2); `unix-none` on request |
| AmigaDOS names | A patch keeps names as given (`T:a.db`) |
| Tested | The smoke test on Instance-24 (AC090, 68040 with FPU, 256 MB), 4 October 2026; B0's probe on an Instance-32 lab copy, 11 November 2026 (section 16); never on real hardware |
| Known issue | None open (the RAM: failure no longer happens, 3.2) |
| Owner | The GFOpenBrowser (JSC) thread (`restart.md`) |

The smoke test resets the FPU's control register first, because "ROM
mathieeesingbas.library leaves the FPU in single precision" in every task that
opens it (`tests/sqlitetest.c`).

### 11.2 What GFOpenBase needs changed

| Change | Why |
| --- | --- |
| A soft-float flavour of the build, with GFOpenWrite's specs (`specs/m68k-soft-m68040.specs`) | GFOffice runs on a 68040 without an FPU. Done: `FLOAT=soft` |
| `SQLITE_THREADSAFE=0` | GFOpenBase uses one connection from one task; no libpthread to link. Done: `THREADSAFE=0` |
| `SQLITE_OMIT_JSON`, and other options B0 shows are safe to leave out | Smaller code |
| `sqlite3_temp_directory` set by GFOpenBase | SQLite's Unix code looks for a temporary folder through Unix paths and environment variables, which mean nothing on AmigaDOS; GFOpenBase names a folder on disk (beside the project, or `T:` once the RAM: issue is understood) |
| A stack of at least 64 KB | SQLite's parser and planner recurse; GFOpenPrint asks for 65,536 bytes the same way (`oap_main_with_stack`) |

The soft-float flavour goes into gfopensqlite as a second build
(`FLOAT=soft`), so GFOpenBrowser's hard-float build is untouched.

### 11.3 Size, memory and speed

- **Code size.** Measured in B0: `libsqlite3.a` is 992,912 bytes hard float
  and 997,776 soft float (`THREADSAFE=0`, `-O2`); the probe program that links
  it is 1,005,204 to 1,012,900 bytes. So about 1 MB of GFOpenBase is SQLite,
  as planned.
- **Memory.** SQLite's page cache defaults to about 2 MB (`cache_size`
  -2000). GFOpenBase sets it from free memory when a project opens: 256 KB on
  an 8 MB machine, 1 to 2 MB at 16 to 32 MB. `sqlite3_soft_heap_limit64` caps
  SQLite as a whole. Datasheets read rows a page at a time (5.4), and blobs
  are streamed (4.3), so a big table or a big picture doesn't spike memory.
- **Speed.** Measured in B0 on the lab's emulated 68040 (section 16 has
  every number): none of these is slow enough to shape the design. The
  tests:

| Test | Measures |
| --- | --- |
| Insert 10,000 rows of five fields in one transaction | Bulk import |
| The same with a commit after every row, `DELETE` and `TRUNCATE` journals | Typing into a form |
| 1,000 lookups by key | Forms and lookups |
| A scan of 10,000 rows with a `WHERE` on a field without an index | Unindexed queries |
| A join of 10,000 orders to 1,000 customers, grouped and summed | Reports |
| Sort 10,000 rows by a text field | Sorted datasheets |

- **Soft float** costs only where `REAL` values are added, compared or turned
  into text and back. Integer, text and date work is unchanged, and Currency
  is an integer on purpose. B0 also runs the smoke test's `REAL` sums on the
  soft build, and checks how SQLite turns numbers into text there.

### 11.4 Limits

| Limit | Value |
| --- | --- |
| Rows in a table | SQLite's limits are far beyond an Amiga's disks |
| A Datatype object | The Prefs limit, 4 MB to start (4.3) |
| Fields on an auto form | 96 (`ogt_sections`: 8 sections of 12 rows), then tabs |
| A report | A few thousand rows on 8 MB, to be measured (7.4) |
| Programs on one project at once | One (3.2) |

## 12. Where the code lives

In DalsinAI/gfoffice, beside the suite's other apps (the parity plan,
`docs/openoffice-parity-plan.md`, section 4, stream B; it replaces
DalsinAI/gfopenbase, which the README had named):

| What | Where |
| --- | --- |
| Portable core: projects, tables, field types, rows (B0b); later the query model and SQL generator, CSV and DBF, report building into an `owf_doc` | `src/base/`, `include/openbase_core.h`, host-tested with SQLite on the PC |
| Amiga program | `app/openbase.c` (GFOpenGadTools), its link line in `build.d/base.sh` |
| Tests | `tests/base/`, run by `mk/base.mk` (`make base-test`, `make base-check`, and from `make test` and `make check`) |
| SQLite itself | DalsinAI/gfopensqlite: `FLOAT=soft THREADSAFE=0 ./build.sh` |

It links owf.library (reports, sheets) and the soft-float SQLite. On the PC,
the tests link the same pinned amalgamation, fetched at build time (into
`build/sqlite`, checked by its SHA-256) and never committed, as the Team's
third-party rule says.

## 13. Phases

| Phase | Delivers | Tested by |
| --- | --- | --- |
| **B0, SQLite for GFOpenBase** (done, 11 November 2026; B0b-B0d also brought the core, the window, keys, indexes and the relationships view forward from B1 and B2) | The soft-float build flavour; `unix-dotfile` locking tried; the RAM: issue looked into; the size and speed of 11.3 measured | gfopensqlite's `tests/baseprobe.c` on an Instance-32 lab copy, hard, soft and no-FPU builds; results in section 16 |
| **B1, the core** | `.oadb` projects; field types; table create and change (the copy-and-rename way); relationships; CSV import and export; the query model and SQL generator. No window | Host: schema changes keep every row; CSV round trips; SQL golden files; ASan and UBSan; every cut-short `.oadb` and CSV gives an error, never a crash |
| **B2, the program** | The object browser; table designer; datasheets; auto forms; relationships view; Datatype object fields | Lab: make a project, add tables and rows, a relationship that refuses an orphan, a picture field; quit and reopen |
| **B3, designers and output** | Query designer; form designer with subforms; reports to PDF, ODT, DOCX and GFOpenPrint; labels; DBF import; sheets in and out | Host: report building into an `owf_doc` compared with expected paragraphs; DBF fixtures. Lab: an invoice report printed through GFOpenPrint to a saved PDF |
| **B4, scripting and runtime** | The ARexx port; form events; runtime mode; GFOpenBaseRun if wanted; letters (merge fields); SQL text; the GFOpenUp Part | Host: the ARexx command parser. Lab: a script that imports a CSV, runs a report to PDF and quits; a front end in runtime mode |

What B1-B4 need from the shared work: E3 (owf.library with the workbook
calls) for sheets, E4 (the PDF writer) for reports, E6 (the editor on any
story) for memo fields, E9 (the picture cache) for Datatype fields, E13
(merge fields) for letters (Design-Suite-Shared.md, 13).

## 14. Risks

- **SQLite's speed on a 68040 is unknown.** B0 comes first so we learn it
  before building on it.
- **No locking.** Section 3.2's semaphore protects programs on one machine
  only; a database on a network share shared by two Amigas is not safe.
- **Soft float.** A `REAL`-heavy database is slower without an FPU; Currency
  as integers avoids the common case.
- **Reports in memory.** Section 7.4.
- **Scope.** Access and Base grew over decades. GFOpenBase aims at the jobs
  people did with Superbase on the Amiga: a catalogue, a club's members, a
  shop's stock and invoices, labels and letters.

## 15. Questions for Dale

1. Start GFOpenBase now, in DalsinAI/gfopenbase (the README's gate is met, section 1.1)?
2. Add a soft-float, single-thread flavour to gfopensqlite's build for GFOpenBase?
3. Locking: try SQLite's dot-file locking, else one program per project through an Exec semaphore. Agreed?
4. Scripting in forms through ARexx only, with no language of GFOpenBase's own?
5. Runtime: a separate smaller GFOpenBaseRun as well, or only a RUNTIME switch on GFOpenBase?
6. DBF: import only, or export too?
7. Do you have Superbase or other Amiga database files for the format lab?
8. Reports built whole in memory as a GFOpenWrite document for B3, with page-by-page PDF later if needed?
9. Currency stored as whole pence or cents (exact, no floating point)?
10. No passwords or encryption in `.oadb` files. Agreed?

## 16. B0 results (11 November 2026)

Measured with gfopensqlite's `tests/baseprobe.c` (branch
`team2/base-probe-20261111`) on `t2-base-20261111`, a lab copy of Instance-32
(Kickstart 47.2, Workbench 47.100, emulated 68040). The lab's `DH1:` is a
host folder, not an FFS partition, so disk times on a real hard disk or CF
card will differ. Three builds of SQLite 3.53.4: hard float, soft float
(`m68k-soft-m68040.specs`, the stove's FPU-detecting wrappers) and no FPU at
all (`-DNO_FPUAUTO`), each single-threaded (`THREADSAFE=0`).

### 16.1 What passed

Every probe passed on `DH1:`, `RAM:` and `T:` with all three builds:

- a fresh database, with the default `unix-dotfile` VFS and with `unix-none`;
- a second writer refused while the first holds the lock, and let in after;
  `unix-none` makes no `.lck` file;
- 100 commits each in journal modes DELETE, TRUNCATE and PERSIST;
- temporary files in the database's own drawer and in `T:`;
- foreign keys: off by default, an orphan refused once on, cascade deletes;
- the `.oadb` header (`application_id`, `user_version`), `quick_check`,
  `VACUUM INTO`;
- 13 `REAL` answers (sums, averages, rounding, 2^53, a large `printf`)
  compared with the PC's, the same on all three builds.

The `RAM:` failure seen in October (section 3.2) did not happen with any
build.

### 16.2 Speed (milliseconds)

100 journal commits; 1,000 lookups by key; 10,000 rows for the rest
(2,000 on `T:`). A second run, the table below, varied by up to 4 times on
`DH1:` (the host folder), so read the disk columns as a guide.

| Test | hard DH1 | hard RAM | soft DH1 | soft RAM | no FPU DH1 | no FPU RAM | T: |
| --- | --- | --- | --- | --- | --- | --- | --- |
| journal DELETE | 844 | 135 | 724 | 128 | 2012 | 126 | 100 |
| journal TRUNCATE | 631 | 122 | 649 | 129 | 621 | 122 | 104 |
| journal PERSIST | 656 | 153 | 649 | 148 | 690 | 130 | 147 |
| insert | 266 | 99 | 209 | 102 | 288 | 82 | 18 |
| lookup | 831 | 278 | 880 | 227 | 710 | 180 | 182 |
| scan | 14 | 11 | 11 | 13 | 5 | 6 | 2 |
| join, group, sum | 58 | 77 | 65 | 79 | 49 | 49 | 14 |
| sort text | 30 | 34 | 31 | 29 | 27 | 34 | 6 |
| whole probe | 4833 | 1221 | 4998 | 1213 | 5739 | 1038 | 880 |

1,000 lookups by key in a 10,000-row table:

| Build and disk | each its own transaction | all in one transaction | `unix-none`, own transactions |
| --- | --- | --- | --- |
| hard DH1 | 1104 | 6 | 669 |
| soft DH1 | 1026 | 5 | 860 |
| no FPU DH1 | 1730 | 5 | 808 |
| soft RAM | 477 | 13 | 489 |

What this means for GFOpenBase:

- **Group reads.** Each statement outside a transaction opens and checks the
  file again, which costs far more than the lock does. A datasheet page, a
  form's lookups and a report read inside one transaction.
- **TRUNCATE** is the journal mode: as fast as PERSIST, never leaves a stale
  journal that looks like a hot one, and much faster than DELETE on a disk.
- **Soft float costs nothing that shows** in these tests, so GFOpenBase links
  the soft-float build and runs without an FPU.

### 16.3 Size

`libsqlite3.a` is 992,912 bytes hard float and 997,776 bytes soft. Each probe
is about 1.0 MB; GFOpenBase with its window is 1,065,120 bytes.
