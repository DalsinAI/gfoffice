# GFOpenBase against OpenOffice Base

Stream B of [the parity plan](../openoffice-parity-plan.md). Re-scored with each
deliverable. Statuses as the plan's table: Done, Part, Gap.

**Score: 10%** (11 November 2026, after B0a and B0b; was 4%, design only).
The core makes, opens, checks and changes projects and their rows, tested
on the PC and on an Instance-32 lab copy, but there is no window yet, so a
user can't do any of it.

| Feature (Base) | Status | Phase | Notes |
| --- | --- | --- | --- |
| Create/open a database file | Part | 1 | Core: `.oadb` (SQLite, `application_id` OADB, `user_version` 1) made, opened, checked (`quick_check` on open, `integrity_check`), copied (`VACUUM INTO`), compacted; plain SQLite files refused or adopted. No window (B0c) |
| Table designer, datasheet view with add/edit/delete | Part | 1 | Core: the 13 field types of the design, rules as named SQL constraints, create, change (copy and rename, one transaction), rename, drop; rows added, changed and deleted with every value checked, in transactions; datasheet pages by key. No window (B0c) |
| Primary keys, relationships | Part | 1 | Core: a Counter key per table; Lookups are real `FOREIGN KEY` clauses with refuse, cascade or clear on delete; `PRAGMA foreign_keys` on for every connection; renames follow. Composite keys, indexes and the relationships view are B0d |
| SQL view, query designer | Gap | 2 | |
| Forms and subforms | Gap | 2 | |
| Reports | Gap | 2 | Generated as GFOpenWrite documents |
| CSV/DBF/Calc import and export | Gap | 2 | CSV first (B1) |
| Locking, recovery | Part | 1 | B0a on the lab: the dot-file lock refuses a second writer, `unix-none` takes none; the rollback journal recovers; journal mode TRUNCATE; the RAM: issue does not happen with today's build |
| ARexx | Gap | 2 | Per ARexx-RULE.md |
| Connect to other databases (ODBC/JDBC) | Gap | - | Not planned: no such stack on AmigaOS |

## Evidence

- B0a: DalsinAI/gfopensqlite `tests/baseprobe.c`, its results in
  [Design-OpenBase.md](../gfoffice/Design-OpenBase.md) section 16.
- B0b: `make base-test` and `make base-check` (212 checks each, ASan and
  UBSan clean); the same test built for AmigaOS passes 212 of 212 on the
  lab copy `t2-base-20261111` of Instance-32, with its projects on DH1: and
  in RAM:.
