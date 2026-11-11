# GFOpenBase against OpenOffice Base

Stream B of [the parity plan](../openoffice-parity-plan.md). Re-scored with each
deliverable. Statuses as the plan's table: Done, Part, Gap.

**Score: 18%** (11 November 2026, after B0d; 15% after B0c, 10% after B0a
and B0b, 4% design only). GFOpenBase has a window: a project's tables, a
table designer with keys and indexes, a datasheet with a row editor and a
relationships view, checked on an Instance-32 lab copy. A row is saved when
it is added or changed; there is no undo yet.

| Feature (Base) | Status | Phase | Notes |
| --- | --- | --- | --- |
| Create/open a database file | Part | 1 | Core: `.oadb` (SQLite, `application_id` OADB, `user_version` 1) made, opened, checked (`quick_check` on open, `integrity_check`), copied (`VACUUM INTO`), compacted; plain SQLite files refused or adopted. Window: `GFOpenBase FILE [NEW]`, a plain SQLite file adopted after asking |
| Table designer, datasheet view with add/edit/delete | Part | 1 | Core: the 13 field types of the design, rules as named SQL constraints, create, change (copy and rename, one transaction), rename, drop; rows added, changed and deleted with every value checked, in transactions; datasheet pages by key. Window: table designer (every field type and rule, Show SQL), datasheet with scroller and keys, row editor; errors in plain words. No undo, no sort or filter yet |
| Primary keys, relationships | Part | 1 | Core: a Counter key, or a key of chosen fields (`PRIMARY KEY(...)`); indexes, unique or not, on up to 8 fields, kept through table changes; Lookups are real `FOREIGN KEY` clauses with refuse, cascade or clear on delete; `PRAGMA foreign_keys` on for every connection; renames follow; every relationship listed. Window: Key in the designer, an Indexes window, the relationships view (boxes and lines). Not yet: drawing a relationship in the view (it is made in the designer), a Lookup to a table keyed by its fields |
| SQL view, query designer | Gap | 2 | |
| Forms and subforms | Gap | 2 | |
| Reports | Gap | 2 | Generated as GFOpenWrite documents |
| CSV/DBF/Calc import and export | Gap | 2 | CSV first (B1) |
| Locking, recovery | Part | 1 | B0a on the lab: the dot-file lock refuses a second writer, `unix-none` takes none; the rollback journal recovers; journal mode TRUNCATE; the RAM: issue does not happen with today's build |
| ARexx | Gap | 2 | Per ARexx-RULE.md |
| Connect to other databases (ODBC/JDBC) | Gap | - | Not planned: no such stack on AmigaOS |

## Evidence

- B0d: `make base-test` and `make base-check` (274 checks each, ASan and
  UBSan clean): keys, indexes (made, read back, kept through renames and
  removals, a unique index the rows don't fit refused and undone),
  relationships, a design changed by another connection.

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
