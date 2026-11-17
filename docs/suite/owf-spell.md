# owfspell.library: one spell checker for GFOffice

17 November 2026 | Team 2 | MIT

Dale's rule: spelling is one common library that every GFOffice program
uses, not code copied into each program.

## What it is

- **Source:** `libowf/src/owf_spell.c`, with its header
  `libowf/include/owf_spell.h`. It was GFOpenWrite's
  `src/core/ow_spell_dict.c` before this change.
- **Library:** `owfspell.library` 1.1, built by `build-library.sh` from
  `libowf/library/owfspell_lib.sfd`. It has 11 calls, all named
  `owf_spell_*`. 1.1 added `owf_spell_command`, the ARexx SPELL word
  commands every program shares. It doesn't have the four document libraries' 17 shared
  calls, so any file can call it beside owfwrite, owfcalc, owfpub or
  owfpresent.library.
- **One word list in memory.** Every open of the same file shares one
  loaded list, even across programs, because the library keys it by the
  file's full name. en_GB.words has 120,980 words, about 1.7 MB once
  loaded. Before this change, GFOpenWrite, GFOpenPresentation and
  GFOpenPressmaster each loaded their own copy. The user's words and Ignore
  All belong to each opener. An Exec semaphore guards the shared lists.
- **The user's words** go in `PROGDIR:Dictionaries/user.words`. Adding a
  word reads the file again before writing it, so a word that another
  program added in the meantime is kept.

## How a program uses it

1. Build with `-DOWF_USE_SPELL_LIBRARY`, and link
   `libowf/amiga/owf_spell_base.c` (`$SPELL` and `$SPELL_SRC` in
   `build-os3.sh`).
2. Call `owf_spell_library()` before the first spelling call. It opens
   the library and returns 0 when the library isn't in LIBS:.
3. Call `owf_spell_open_default(status, size)` to open the first list
   found: `PROGDIR:Dictionaries/en_GB.words`, then en_US, then
   `SYS:Dictionaries/en_GB.words`.
4. Call `owf_spell_scan`, `owf_spell_words`, `owf_spell_suggest`,
   `owf_spell_add` and `owf_spell_ignore` as needed. Walking the document
   stays in each program's core, for example `app/ow_spell.c` (Write) and
   `src/deck/opr_spell.c` (Presentation and Pressmaster).
5. Pass ARexx SPELL's CHECK, SUGGEST, ADD and IGNORE to
   `owf_spell_command`, so they work the same in every program. A program
   keeps COUNT and LIST, which walk its own document.
6. Call `owf_spell_close`, then `owf_spell_library_close()` when the
   program quits.

The host tests link `owf_spell.c` directly. No Amiga program links a copy:
`build-os3.sh` drops it from the libowf sources.

## Who uses it

| Program | Spelling | On owfspell.library |
| --- | --- | --- |
| GFOpenWrite (and the 1.x window) | Spell Check | yes |
| GFOpenPresentation | Spelling, as you type, ARexx SPELL | yes |
| GFOpenPressmaster | the same, through Presentation's core | yes |
| GFOpenCalc | Edit > Spelling..., ARexx SPELL (`src/calc/oc_spell.c`) | yes |
| GFOpenBase | the form designer's Spelling... (labels, boxes' titles, buttons' words), ARexx SPELL over the saved forms (`src/base/spell/ob_spell.c`) | yes |
| GFOpenDiag, GFOpenProject | none yet | next, one PR each |
| GFOpenDraw | none: a paint program, with no text of its own | - |

## Tests

- `make spell-lib-test` and `make spell-lib-check` (ASan) run
  `tests/spell/test_owf_spell.c`. It covers the list formats (CRLF,
  Hunspell flags, a count line, unsorted input), the word rules,
  scanning, suggestions, the user's words with two openers, Ignore All, a
  list shared by two openers, the ARexx word commands, and the shipped
  en_GB.words.
- `make calc-spell-test` and `make calc-spell-check` (ASan) run
  `tests/calc_core/test_calc_spell.c`: Calc's walk over typed text only,
  wrapping round, Change with Undo, and ARexx SPELL.
- `make base-spell-test` and `make base-spell-check` run
  `tests/base/test_base_spell.c`: a form's own words in order, wrapping
  round, changes kept to a label's size, and ARexx SPELL over the saved
  forms.
- `make sfd-check` checks owfspell_lib.sfd against owf_spell.h, the
  generated glue and the SDK's offsets.
- `OWFSpellTest`, built by `build-library.sh` into `tests/`, opens the
  library twice on the Amiga. It checks that the second open shares the
  first one's list, then runs every call. This needs a lab, because a
  host test can't open an Amiga library.
