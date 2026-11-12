# GFOpenBase against OpenOffice Base

Stream B of [the parity plan](../openoffice-parity-plan.md). Re-scored with each
deliverable. Statuses as the plan's table: Done, Part, Gap.

**Score: 25%** (12 November 2026, after B1b; 22% after B1a, 18% after B0d, 15% after
B0c, 10% after B0a and B0b, 4% design only). GFOpenBase has a window: a
project's tables, a table designer with keys and indexes, a datasheet that
sorts and filters, with a row editor and Undo, CSV import (into a table or
a new one, types guessed) and export, and a relationships view, checked on
an Instance-32 lab copy.

| Feature (Base) | Status | Phase | Notes |
| --- | --- | --- | --- |
| Create/open a database file | Part | 1 | Core: `.oadb` (SQLite, `application_id` OADB, `user_version` 1) made, opened, checked (`quick_check` on open, `integrity_check`), copied (`VACUUM INTO`), compacted; plain SQLite files refused or adopted. Window: `GFOpenBase FILE [NEW]`, a plain SQLite file adopted after asking |
| Table designer, datasheet view with add/edit/delete | Part | 1 | Core: the 13 field types of the design, rules as named SQL constraints, create, change (copy and rename, one transaction), rename, drop; rows added, changed and deleted with every value checked, in transactions; datasheet pages by key. Window: table designer (every field type and rule, Show SQL), datasheet with scroller and keys, row editor; errors in plain words. B1a: the datasheet sorted (Sort and Filter..., or a click on a heading) and filtered by one test on one field. B1b: Edit > Undo of rows and imports (100 steps) |
| Primary keys, relationships | Part | 1 | Core: a Counter key, or a key of chosen fields (`PRIMARY KEY(...)`); indexes, unique or not, on up to 8 fields, kept through table changes; Lookups are real `FOREIGN KEY` clauses with refuse, cascade or clear on delete; `PRAGMA foreign_keys` on for every connection; renames follow; every relationship listed. Window: Key in the designer, an Indexes window, the relationships view (boxes and lines). Not yet: drawing a relationship in the view (it is made in the designer), a Lookup to a table keyed by its fields |
| SQL view, query designer | Gap | 2 | |
| Forms and subforms | Gap | 2 | |
| Reports | Gap | 2 | Generated as GFOpenWrite documents |
| CSV/DBF/Calc import and export | Part | 2 | B1a: CSV import into a table (all or none, by header or field order, separator found) and export of the datasheet as shown; round trips keep keys and values. B1b: New Table from CSV (types guessed from 200 rows, changeable), ISO-8859-1 or UTF-8 both ways, bad rows left out and listed. Not yet: DBF, sheets |
| Locking, recovery | Part | 1 | B0a on the lab: the dot-file lock refuses a second writer, `unix-none` takes none; the rollback journal recovers; journal mode TRUNCATE; the RAM: issue does not happen with today's build |
| ARexx | Gap | 2 | Per ARexx-RULE.md |
| Connect to other databases (ODBC/JDBC) | Gap | - | Not planned: no such stack on AmigaOS |

## Evidence

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
