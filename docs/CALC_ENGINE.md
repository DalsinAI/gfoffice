# OpenCalc: the formula engine

Version 0.1 | 25 October 2026 | AmigaChrome Team, MIT

The formula engine works out the formulas of a workbook (`owf_book`) for
OpenCalc, the spreadsheet of the GFOffice Suite. It is part of libowf: the
calls are in `libowf/include/owf_calc.h`, the sources in `libowf/calc/`.
A program that calculates compiles `libowf/calc/*.c` with libowf's own
sources and links the maths library. OpenWrite, OWConvert and owf.library
do not take the engine in yet (their builds take `libowf/src/*.c` only);
owf.library gets its calls later.

It is plain C99 with doubles and `<math.h>` (soft maths on an Amiga
without an FPU), written for a 68040 with a small stack and 8 to 32 MB.

## How it works

- **Parsing.** A formula is Excel's A1 text as the files keep it, without
  the "=". An operator-precedence (shunting-yard) parser, whose stacks are
  on the heap, turns it into bytecode in reverse Polish order. Defined
  names are compiled into the formulas that use them.
- **Working out.** A loop over the bytecode with a stack of values: no
  recursion. Text and arrays made on the way come from an arena emptied
  after each formula; past 1 MB a formula gives `#NUM!`.
- **Dependencies.** For each formula the engine keeps the areas it reads,
  in an index: one bucket per cell for single cells, one per column for
  ranges up to four columns wide, one per sheet for wider ones. Typing a
  cell marks what reads it dirty (a worklist).
- **Recalculation.** `owf_calc_recalc` works out the dirty formulas, and
  the volatile ones (RAND, NOW, TODAY) with what reads them, in dependency
  order (Kahn's algorithm). When no formula is ready and some are left,
  those are in loops or after them: Tarjan's algorithm, with a stack of its
  own, finds the loops.
- **Circular references.** Every cell in a loop (a group of formulas that
  read each other, or one that reads itself) gets **`#REF!`** and the
  state `OWF_CALC_CIRCULAR`; `owf_calc_circular_count` says how many. The
  cells that read a loop are worked out from its `#REF!` (so
  `ISERROR(A1)` of a looped A1 is TRUE). Nothing hangs: a loop of 5,000
  cells is found like a loop of two.
- **Files.** `owf_calc_new` parses a book and calculates nothing: the
  cells show the results the file saved. A formula the engine cannot work
  out (a function it does not have, a reference to another book) keeps the
  file's value (`OWF_CALC_KEPT`), and what reads it uses that value, until
  the user types the cell again; then it is `#NAME?` (another book's
  reference: `#REF!`).

## Formulas

- Numbers (`1.5`, `.5`, `1E+20`), text in double quotes (`"a""b"`),
  `TRUE` and `FALSE`, the errors `#NULL!`, `#DIV/0!`, `#VALUE!`, `#REF!`,
  `#NAME?`, `#NUM!`, `#N/A`, and array constants (`{1,2;3,4}`).
- Cells and ranges, relative or absolute: `A1`, `$A$1`, `A$1`, `A1:B2`,
  whole columns `A:C`, whole rows `2:5`, up to `XFD1048576`.
- Other sheets: `Sheet2!A1`, `'Q1 sales'!A1:B2`, 3-D `Sheet1:Sheet3!A1`
  (and `'Sheet1:My sheet'!A1`); another book's (`[Book.xls]Sheet1!A1`)
  is not read (`#REF!`, or the file's value).
- Defined names, the book's and a sheet's own (`Sheet2!Total`).
- Operators, tightest first: `:` (range), space (intersection), `,`
  (union, inside brackets), unary `-` and `+`, `%`, `^`, `*` `/`, `+` `-`,
  `&`, `=` `<>` `<` `<=` `>` `>=`. As in Excel, `-2^2` is 4 and `2^3^2`
  is 64.
- Functions by Excel's names (any case; `_xlfn.` is taken off), up to 255
  arguments, arguments left out (`IF(A1,,2)`).
- Limits: 8192 bytes of formula, 64 brackets and calls inside each other,
  defined names eight deep.
- A typed formula is kept as Excel keeps it: `=sum(a1:b2)` is stored as
  `SUM(A1:B2)`.

## Values

- **Kinds:** number (a double), text (UTF-8, at most 32,767 characters),
  boolean, error, and empty (a cell with nothing in it).
- **Conversions,** as Excel makes them. Arithmetic takes booleans as 1
  and 0, an empty cell as 0, and text that reads as a number ("1,234.5",
  "50%", "$3", "(12)", "25/10/2026", "13:45"); other text is `#VALUE!`.
  `&` takes numbers as General text (15 significant digits), booleans as
  TRUE and FALSE.
- **Comparisons:** numbers before text before booleans; text without
  case; an empty cell is 0, "" or FALSE, as the other side is. Numbers
  equal but for the last bits are equal (`0.1+0.2=0.3` is TRUE), and a sum
  that close to 0 is 0.
- **Errors** pass on: of two, the left one. Functions that take ranges
  (SUM, AVERAGE...) pass on an error in the range; COUNT and COUNTA do not.
- **Ranges as arguments:** SUM, AVERAGE, MIN, MAX and the like take only
  the numbers in a range or array (text, booleans and empty cells are
  passed over) but every number, boolean or number-like text given
  directly (`SUM("2",3)` is 5).
- **One value from a range** (implicit intersection): `=B1:B3*2` in row
  2 is `B2*2`; outside the range's rows, `#VALUE!`.
- **Arrays.** An array formula (read from a file; typing one comes later)
  works its ranges element by element and each cell of its range shows
  its element (`#N/A` past the result's end). SUMPRODUCT works its
  arguments as arrays in any formula. Array constants work element by
  element anywhere; a single row or column stretches to fit.
- **Dates** are serial numbers: days since 30 December 1899, with Excel's
  29 February 1900 (serial 60), or since 1 January 1904 when the book's
  `date1904` is set. Times are fractions of a day.
- **Rounding** (ROUND, ROUNDUP, ROUNDDOWN, TRUNC, INT, MOD, number
  formats) goes by the 15 significant digits Excel shows: `ROUND(2.675,2)`
  is 2.68.

## Editing

- `owf_calc_enter` reads typed input as Excel does: `=` starts a formula;
  numbers with thousands separators, signs, brackets, `%`, `$`, `£`, `€`,
  exponents; dates (`25/10/2026`, `2026-10-25`, `25-Oct-2026`,
  `25 October 2026`, `Oct 25, 2026`, `25/10`, `Oct-26`), times (`13:45`,
  `1:45 PM`, `13:45:30`) and both; TRUE and FALSE; the errors; `'` before
  text that would read as something else; `""` clears. Day or month first:
  `owf_calc_set_date_order` (day first by default). A typed percentage,
  currency, date or time gives a General cell a format to match ("0%",
  "£#,##0.00", "dd/mm/yyyy", "h:mm"...).
- `owf_calc_shift` inserts or deletes rows or columns: the cells, row
  heights, column widths and merges move, and every formula and defined
  name in the book that refers to the sheet is adjusted as Excel adjusts
  them. A range grows when rows go in inside it (not at its first row) and
  shrinks when some of its rows go; a reference to a deleted cell, or a
  range all deleted, becomes `#REF!` (`Sheet2!#REF!` with its sheet). 3-D
  references are not adjusted. Nothing may be pushed off the sheet (the
  call refuses, as Excel does).
- `owf_calc_move_formula` moves a formula's relative references for copy
  and fill; a reference moved off the sheet becomes `#REF!`.
- `owf_calc_evaluate` works out a formula without changing the book (a
  status line's sum of the selection).

## Number formats

`owf_calc_format` shows a cell's value through its number format;
`owf_calc_format_value` takes a format's text, a column width for General
numbers, and gives the section's colour.

- General (15 significant digits; fitted to a width: fewer decimals, then
  scientific, then `#`), `0`, `0.00`, `#,##0`, `#,##0.00`, `0%`, `0.00%`,
  `0.00E+00` and engineering `##0.0E+0`, `#` and `?` placeholders,
  thousands scaling (`#,##0,`), fractions (`# ?/?`, `?/8`).
- Dates and times: `d dd ddd dddd`, `m mm mmm mmmm mmmmm`, `yy yyyy`,
  `h hh`, `m mm` as minutes after `h` or before `s`, `s ss`, `.0` to
  `.000` seconds, `AM/PM` and `A/P`, elapsed `[h]` `[mm]` `[ss]`. A date
  that cannot be shown (below 0, past 9999) is `########`.
- Text `@`, literals in quotes, `\x`, `_x` (a space), `*x` (dropped),
  `[$€-407]` (the symbol).
- Up to four sections (positive; negative; zero; text), conditions
  (`[>100]`), colours (`[Red]` and the other seven, `[Color1]` to
  `[Color8]`). A number in a one-section format gets a minus sign only when
  what shows is not all zeros.
- Month and day names are English.

## The functions (119)

An unknown function gives `#NAME?` (a file's cell keeps its value, see
above). These are the engine's, with Excel's names and argument counts:

- **Maths and statistics (56):** ABS, ACOS, ASIN, ATAN, ATAN2, AVERAGE,
  AVERAGEIF, AVERAGEIFS, CEILING, COS, COSH, COUNT, COUNTA, COUNTBLANK,
  COUNTIF, COUNTIFS, DEGREES, EVEN, EXP, FACT, FLOOR, INT, LARGE, LN, LOG,
  LOG10, MAX, MEDIAN, MIN, MOD, ODD, PI, POWER, PRODUCT, RADIANS, RAND,
  ROUND, ROUNDDOWN, ROUNDUP, SIGN, SIN, SINH, SMALL, SQRT, STDEV, STDEVP,
  SUM, SUMIF, SUMIFS, SUMPRODUCT, SUMSQ, TAN, TANH, TRUNC, VAR, VARP.
- **Logic and information (18):** AND, FALSE, IF, IFERROR, IFNA, ISBLANK,
  ISERR, ISERROR, ISLOGICAL, ISNA, ISNONTEXT, ISNUMBER, ISTEXT, N, NA, NOT,
  OR, TRUE.
- **Text (21):** CHAR, CLEAN, CODE, CONCATENATE, EXACT, FIND, LEFT, LEN,
  LOWER, MID, PROPER, REPLACE, REPT, RIGHT, SEARCH, SUBSTITUTE, T, TEXT,
  TRIM, UPPER, VALUE.
- **Lookup and reference (9):** CHOOSE, COLUMN, COLUMNS, HLOOKUP, INDEX,
  MATCH, ROW, ROWS, VLOOKUP.
- **Dates and times (11):** DATE, DAY, HOUR, MINUTE, MONTH, NOW, SECOND,
  TIME, TODAY, WEEKDAY, YEAR.
- **Money (4):** FV, NPV, PMT, PV.

Notes:

- Criteria (COUNTIF, SUMIF, AVERAGEIF and their -IFS): a number, `">5"`,
  `"<>a"`, `"<=2026-01-01"`, text with wildcards (`?`, `*`, `~`), `""`
  for empty cells, `"<>"` for cells that are not; an empty criteria cell
  is 0. Text compares without case; `5` or `"=5"` also matches text that
  reads as 5.
- VLOOKUP, HLOOKUP and MATCH: exact matching takes wildcards in text;
  approximate matching (sorted data) searches by halves, as Excel, passing
  over cells of another kind.
- INDEX gives a reference (`SUM(A1:INDEX(B1:B9,3))` works); `0` for a row
  or column gives the whole column or row.
- CEILING and FLOOR are Excel 2010's (a negative number with a positive
  significance is allowed).
- PMT, PV and FV work `(1 + rate)^n` through log(1+x) and exp(x)-1, so
  they keep their digits for small rates.
- LEN, LEFT, MID, RIGHT, FIND and SEARCH count characters (Unicode code
  points); Excel counts a character beyond U+FFFF (an emoji) as two.
- UPPER, LOWER, PROPER and comparisons without case know Latin
  (Latin-1 and Latin Extended-A), Greek and Cyrillic letters.
- CHAR and CODE use Windows-1252, as Excel on Windows.
- IF and CHOOSE work out all their arguments (Excel only the one it
  picks); the result is the same.

## Tests

`make calc-test` (and `make calc-check` under AddressSanitizer and
UBSan; both in `make test` and `make check`) runs `tests/calc/test_calc.c`:

- the parser (what parses, what does not, precedence, nesting limits);
- every function, with its errors, from a case table (the test checks
  that each function in the table has a case);
- values, conversions and error rules; references, names, implicit
  intersection, array formulas;
- recalculation (only what reads a change is worked out again; chains of
  20,000; volatile functions), circular references (loops of two, of one,
  of 5,000; what reads a loop);
- typed input and its formats, `owf_calc_move_formula`, `owf_calc_shift`
  (rows and columns, inserted and deleted, other sheets, names, merges,
  row heights), `owf_calc_rebuild`, kept values, number formats;
- random books: 2,400 random edits (values, formulas, clears, inserted
  and deleted rows and columns), each followed by a recalc, and every 20
  a full recalc that must change nothing; and 6,000 random and broken
  formulas through the parser, the machine and the mover;
- the oracle: `tests/book/fixtures/book.xls` and
  `tests/calc/fixtures/calc.xls` are read with the results their writer
  saved, every formula is worked out again and compared.
  `tests/calc/make_calc_fixtures.py` makes `calc.xls`: 272 formulas
  covering the functions, written in Excel's syntax, worked out and saved
  by LibreOffice (which only makes the test file).

The oracle's results (25 October 2026): of 320 formulas, 304 give the
saved result (2 of them to 13 significant digits rather than 15: PV and FV,
where the engine's value is the exact one), and 16 differ for known
reasons, each listed with its reason in `test_calc.c`:

- 6 in book.xls, where LibreOffice saved book.fods' placeholder instead
  of working the cell out (five boolean cells, one date cell; F4 is saved
  as FALSE though D4 is 240 and the formula is `D4>50`);
- 10 in calc.xls where LibreOffice's rule is not Excel's: text given
  directly to SUM and PRODUCT, `#NUM!` against LibreOffice's `#VALUE!` for
  SQRT(-1), LN(0) and LARGE past the end, `#DIV/0!` for POWER(0,-1),
  `#REF!` for VLOOKUP past the table, and booleans (LibreOffice's are the
  numbers 1 and 0: `CONCATENATE("a",1,TRUE)`, `TRUE>"z"`, and
  COUNTIF(range,TRUE) over booleans it saved as numbers).

Speed and size, on the PC (x86-64, gcc 13 -O2): 10,000 formulas (ten a row
on 1,000 rows, with running SUMs, VLOOKUPs over the 1,000 rows and
COUNTIFs over a column) are worked out in about 0.14 s and parsed in under
0.01 s; 10,000 light ones (arithmetic, a SUM of the row, IF, ROUND) in
about 0.03 s. The engine holds about 2 MB for 10,000 formulas (nodes,
index and code; less with the Amiga's 4-byte pointers). Its code is about
120 KB at -O2 and 85 KB at -Os on x86-64, with 5 KB of tables (`size` of
the five objects); the 68040 build needs the stove to measure. The largest
stack frame is `calc_format`'s, under 1 KB; parsing goes about 400 bytes
deeper for each defined name inside a name (eight at most).

## Not yet

- The engine's calls in owf.library (`owf_lib.sfd`).
- Typing an array formula (Ctrl+Shift+Enter); Excel 365's spilled arrays.
- R1C1 references, table references, INDIRECT, OFFSET and the other
  functions Excel has and the engine does not.
- 3-D references are not adjusted when rows or columns go in or out.
- A defined name changed by the program needs `owf_calc_rebuild`.
