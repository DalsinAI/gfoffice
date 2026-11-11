# GFOpenCalc: the formula engine

Version 0.3 | 9 November 2026 | AmiCradle Team, MIT

The formula engine works out the formulas of a workbook (`owf_book`) for
GFOpenCalc, the spreadsheet of the GFOffice Suite. It is part of libowf: the
calls are in `libowf/include/owf_calc.h`, the sources in `libowf/calc/`.
A program that calculates compiles `libowf/calc/*.c` with libowf's own
sources and links the maths library. GFOpenWrite, OWConvert and owf.library
do not take the engine in yet (their builds take `libowf/src/*.c` only);
owf.library gets its calls later.

It is plain C99 with doubles and `<math.h>` (soft maths on an Amiga
without an FPU), written for a 68040 with a small stack and 8 to 32 MB.

## Goal Seek

GFOpenCalc 0.1.60's Data > Goal Seek is a client of the same dependency and
recalculation engine. Reusable portable-core API
`oc_core_goal_seek` (`include/opencalc_core.h` 0.25.0) takes a numeric formula
cell, a target value and one empty or numeric constant cell. It deep-copies the
workbook, then uses safeguarded secant steps and a bracketed fallback while
the ordinary engine recalculates the formula graph for every candidate.

The live workbook is never used as numerical scratch space. A failed or
cancelled search changes neither cells nor Undo; an accepted result goes
through `oc_core_set_input` as one normal undoable edit. The API supports
formula and variable cells on different sheets and returns the candidate,
the value actually achieved and the iteration count. Formula variables,
non-numeric results, non-finite values and searches which do not converge are
reported rather than committed.

## Solver

Data > Solver makes a target cell as large or as small as it can be, or
equal to a value, by changing variable cells (up to 200), under up to 32
constraints: a cell or range `<=`, `=` or `>=` a number, a cell or a range of
the same size, or variable cells that must be whole numbers or binary (0 or
1). Variables are kept from going negative unless that is turned off.

`oc_core_solve` (`src/calc/oc_solver.c`) works on a deep copy of the book.
It reads the model as a linear programme: every cell the model reads is
worked out at the variables' present values and then with each variable one
more, which gives each formula's coefficients. Two more points, with every
variable moved by a different amount, check that the formulas really are
linear; a model that is not (a product of two variables, say) is reported by
the first cell that gave it away, and nothing is solved wrongly. After
solving, the answer is put into the copy and every constraint is checked on
the sheet itself.

`src/calc/oc_lp.c` solves the programme: a dense two-phase simplex (the
largest gain enters; after 50 steps that gain nothing, Bland's rule, which
cannot cycle), with rows that hold one variable turned into bounds first and
variables with no lower limit split in two. Whole-number variables are found
by branch and bound, depth first on the most fractional variable, nearer
side first, pruned by the best answer so far, up to 5000 programmes. The
tableau is at most 512K numbers (4 MB). Values within 1e-9 of a whole number
are written as that number.

The window keeps the model on the sheet whether or not the answer is kept,
as hidden names named as Excel names its Solver models (`solver_opt`,
`solver_typ`, `solver_val`, `solver_adj`, `solver_num`, `solver_lhsN`,
`solver_relN`, `solver_rhsN`, `solver_neg`, with `solver_eng` 2 for a linear
model), so a model made in Excel opens in GFOpenCalc and the other way round.
ODS keeps them as named expressions (ODF has no hidden names, which the save
notes). Keeping the answer is one Undo step.

ARexx: `SOLVE [MAX|MIN cell | VALUE cell n] [BY cells] [lhs<=rhs ...]
[INT cells] [BIN cells] [ANY] [KEEP]` gives "target cell=value ..."; with no
model words it solves the sheet's own model. RC 5 when the model has no
answer (no values meet it, no limit, or not linear), 10 when it does not
make sense; LASTERROR says why. Test script: tests/arexx/calc-solver.rexx.

## Scenarios

Data > Scenarios keeps named sets of values for some cells of a sheet (its
changing cells: up to 32 ranges and 2048 cells), each with a comment, up to
32 a sheet. Add keeps the cells' present inputs (numbers, text; a formula as
the value it has now, as Excel keeps it, since its references would not
follow rows put in later); Show puts a scenario's back in its cells and marks it as the one
shown; Update keeps the present inputs afresh; Delete takes one away. Each
is one Undo step (`src/calc/oc_scenario.c`).

A sheet's scenarios are its hidden names (`libowf/include/owf_scenario.h`,
code in `libowf/calc/owf_scenariodef.h`, shared as the pivot definitions'
is): `_GF_SCENARIO_k` holds the name, comment, whether it was shown last and
the inputs (escaped as the pivot text is), and `_GF_SCENARIO_k_Rn` each
range, so the ranges follow inserted and deleted rows and columns as any
name does. A scenario whose ranges no longer hold as many cells as it has
inputs is left alone (not shown) rather than shown wrongly.

Files: ODS writes each scenario as ODF does, a scenario table after its
sheet (`table:scenario` with its ranges, comment and whether it is active,
and the scenario's cells at their own addresses; `gfocalc:scenario-name`
keeps a name the table had to change to stay unique), and reads scenario
tables back into their sheet's scenarios, so they are not sheets in
GFOpenCalc. XLSX writes the sheet's `scenarios` element (each `scenario`
with its `inputCells` and their values, `current` and `show` for the one
shown) and reads Excel's (values, as GFOpenCalc keeps them too).

ARexx: `SCENARIO ADD name cells [comment]`, `SCENARIO UPDATE name [cells
[comment]]`, `SCENARIO SHOW name`, `SCENARIO DELETE name`, `SCENARIO LIST`
(the names, with commas between); a name with spaces in double quotes. RC 5
when there is no such scenario. Test script: tests/arexx/calc-scenario.rexx.

## Consolidate

Data > Consolidate puts up to 16 source ranges, on any sheets, together into
one table at a destination cell, with one of the eleven functions (Sum,
Count, Average, Max, Min, Product, CountA, StdDev, StdDevP, Var, VarP). By
position, each source's cell goes with the cells at the same place in the
others. With labels in the left column, the top row or both, cells go
together by their labels instead (case aside), and the table has each label
once, in the order the labels first come; a label only some sources have
still gets its row or column. Linked, each of the table's cells is a formula
over the source cells that went into it (`=SUM('Q1'!$B$2,'Q2'!$B$2)`), so
it follows them; otherwise the table is values. The destination may not be
inside a source. The work is one Undo step (`src/calc/oc_consolidate.c`),
and the settings stay on the sheet as a hidden name, `_GF_CONSOLIDATE`, so
the window offers them again (and they survive XLSX and ODS as any name
does).

## Multiple Operations

GFOpenCalc 0.1.67 supports the column, row and two-variable forms of Apache
Calc's Data > Multiple Operations. Portable-core API
`oc_core_multiple_operations`
(`opencalc_core.h` 0.32.0) takes a formula cell and one input cell while the
current selection supplies one column of up to 65,536 numeric trial values.
`oc_core_multiple_operations_2d` takes a formula plus distinct row and column
input cells. Its selected matrix supplies row-input trials across the top after
the corner, column-input trials down the left after the corner, and receives
results in its interior, up to 262,144 output cells.

`oc_core_multiple_operations_multi` takes one input plus a horizontal or
vertical formula range. For column trials, the selected table holds trials in
its first column and one output column per formula. For row trials, it holds
trials in its first row and one output row per formula. This matches Apache's
simultaneous-formulas table arrangement and supports up to 262,144 results.

A second deep copy receives the complete result column immediately to the
right of one-variable trials, or the complete two-variable matrix interior.
Only after every trial and write succeeds does that book replace the live one,
as one Undo step. Formula and input cells may be on a different sheet, their
original values are not changed, and an invalid trial or nonnumeric result
leaves the live book untouched. Output remains ordinary numeric cells. Hidden
sheet-scoped names retain each table's formula and input references, so
**Data > Refresh Multiple Operations** can rebuild it after model changes.
Those durable definitions and results round-trip through XLSX and ODS, and a
refresh is another atomic Undo step. Unlike Apache Calc's native formula
arrays, refresh is currently manual rather than part of ordinary workbook
recalculation.

## Grouped subtotal authoring

GFOpenCalc 0.1.61's Data > Subtotals reuses the engine's existing `SUBTOTAL`
implementation rather than calculating totals in the window. Portable-core
API `oc_core_subtotals` (`opencalc_core.h` 0.26.0) takes the current headed
table selection, an absolute group column, one or more value columns and any
SUBTOTAL function from AVERAGE through VARP. Contiguous equal keys become
groups. It inserts a bold labelled formula row after every group and can add a
grand total whose SUBTOTAL formulas safely ignore the nested group totals.

The transformation is built and recalculated on a deep copy. Only a complete
result replaces the live workbook, as one whole-book Undo step. Ordinary row
shift semantics update formulas and names elsewhere in the workbook,
including cross-sheet references; allocation, formula or recalculation
failure leaves the live workbook untouched.

## Pivot-summary aggregation

GFOpenCalc 0.1.62's Data > Pivot Summary is the first DataPilot-style vertical
slice. Portable-core API `oc_core_pivot_summary` (`opencalc_core.h` 0.27.0)
takes a selected headed table, a row field, an optional column field, a value
field and any of the eleven aggregate modes used by Subtotals. Categories are
typed values and retain first-occurrence order; blank categories participate.
The result is a cross-tab on a new sheet with bold headings and row, column and
grand totals. A one-dimensional form is available when no column field is
wanted.

Aggregation uses numerically stable online mean and variance accumulation.
The new sheet contains ordinary text and numeric cells, so it survives XLSX
and ODS save/reload without a private format. The source table is untouched
and the entire sheet creation is one whole-book Undo step. Version 0.1.62
initially made this a static summary; native ODF/OOXML pivot-cache interchange
and interactive field filtering remain work for full DataPilot parity.

GFOpenCalc 0.1.63 and portable core 0.28.0 add refresh. A generated sheet owns
two internal sheet-scoped defined names: one holds the quoted absolute source
range and one holds the row, optional column, value and aggregate fields. The
formula engine's normal reference rewriting keeps the source range aligned as
rows or columns move. Both names round-trip through XLSX and ODS, allowing a
reloaded summary to refresh without application-private file parts.

`oc_core_refresh_pivot` rebuilds the active summary on a deep workbook copy,
clears stale cells when the result shrinks and swaps the complete result only
after success. Refresh is one whole-book Undo step.

### Pivot tables (field layout)

Data > Pivot Table... (core: `oc_core_pivot_create`, `oc_core_pivot_get`,
`oc_core_pivot_set_layout`, `oc_core_refresh_pivot`) replaces the summary's
three requesters with a field layout window. The definition is
`owf_pivotdef` (`libowf/include/owf_pivot.h`; its code is
`libowf/calc/owf_pivotdef.h`, compiled into `owf_book.c` and, on
owfcalc.library, into the core's `oc_pivotdef.c`):

- up to 8 row fields and 8 column fields, nested outer first;
- up to 8 data fields, each with one of the eleven functions; a field may be
  a data field as well as a row, column or page field;
- up to 4 page fields, each keeping every item or one item (a text, number,
  logical value or blank), which filter the source rows;
- a grand total row and a grand total column, each optional.

Each field's items keep their first-occurrence order and nested keys are
grouped by their outer items. Items and keys are hashed, so making a table
is linear in the source's rows. The output has the page fields at its top
left, a blank row, the column items' heading rows (with a row of data field
names when there are several), then one row per row key; outer row items are
written once per group. Headings and totals are bold; values take their data
field's source cell style. At most 65536 values are made. The table's
place and size are kept, so a refresh or a new layout clears only the old
table's rectangle (an older summary with no size clears its sheet as
before). A table may share its source's sheet if it does not cover the
source.

`_GF_PIVOT_CONFIG` now holds `"2;r=...;c=...;d=field:function,...;p=field:item,...;g=...;t=row,col,rows,cols"`
(fields counted from the source's first column; items with `%XX` for the
characters the text uses). The first form, `"row,column,value,function"`
with absolute columns, still reads.

Files carry the pivot table natively, and the readers turn it back into the
two names:

- **ODS**: `table:data-pilot-tables/table:data-pilot-table` with its target
  range, `table:source-cell-range`, one `table:data-pilot-field` per field
  (`table:orientation` row, column, data with `table:function`, or page with
  `table:selected-page`), and `table:grand-total`. libowf adds
  `gfocalc:field` and `gfocalc:position` (the field's source column and
  order) and the output's size; without them a file's fields are found by
  their headings, so an OpenOffice or LibreOffice pivot table reads too.
- **XLSX**: a pivot cache (`pivotCacheDefinition`: worksheet source, cache
  fields with the shared items of row, column and page fields, no records,
  `refreshOnLoad`) and a `pivotTableDefinition` on the output sheet (pivot
  fields, row/column/page/data fields, grand totals). Excel's own pivot
  tables read when their source is a range of cells; one table a sheet.

The two names are not written beside the native pivot table, and a file that
has both (an older GFOpenCalc's names and a newer table) takes the table.

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
- `owf_calc_move_range` is cut and paste, as Excel does it: the cells of
  a range move (to another sheet too) and replace what was there. Formulas
  that move keep pointing where they pointed (a reference left on the old
  sheet gets its sheet's name); every reference in the book to cells all
  inside the range follows them; a reference to a replaced cell becomes
  `#REF!`. Only the formulas that changed and what reads the two places
  are worked out again.
- `owf_calc_rename_sheet` renames a sheet and every reference to it in
  formulas and defined names, quoting the name as it needs
  (`'Q1 o''clock'!A1`); it refuses the names Excel refuses.
- `owf_calc_touch` is for cells the program changed itself (an undo that
  puts cells back): the engine takes that range's formulas again and marks
  dirty what is there and what reads it, without parsing the rest of the
  book.
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

## The functions (277)

An unknown function gives `#NAME?` (a file's cell keeps its value, see
above). These are the engine's, with Excel's names and argument counts;
the first 119 (owf_calc_fn.c):

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

The second batch, 158 more (in files beside owf_calc_fn.c, one for each
kind, sharing its helpers through owf_calc_fn.h; the table of all of them
stays in owf_calc_fn.c, in alphabetical order):

- **Maths and trigonometry (32, owf_calc_fnmath.c):** ACOSH, ARABIC,
  ASINH, ATANH, BASE, CEILING.MATH, CEILING.PRECISE, COMBIN, COMBINA,
  DECIMAL, FACTDOUBLE, FLOOR.MATH, FLOOR.PRECISE, GCD, ISO.CEILING, LCM,
  MDETERM, MINVERSE, MMULT, MROUND, MULTINOMIAL, PERMUT, PERMUTATIONA,
  QUOTIENT, RANDBETWEEN, ROMAN, SERIESSUM, SQRTPI, SUBTOTAL, SUMX2MY2,
  SUMX2PY2, SUMXMY2.
- **Statistics (66, owf_calc_fnstat.c):** AVEDEV, AVERAGEA, BINOM.DIST,
  BINOMDIST, CORREL, COVAR, COVARIANCE.P, COVARIANCE.S, DEVSQ,
  EXPON.DIST, EXPONDIST, FORECAST, FORECAST.LINEAR, FREQUENCY, GEOMEAN,
  HARMEAN, INTERCEPT, MAXA, MAXIFS, MINA, MINIFS, MODE, MODE.MULT,
  MODE.SNGL, NORM.DIST, NORM.INV, NORM.S.DIST, NORM.S.INV, NORMDIST,
  NORMINV, NORMSDIST, NORMSINV, PEARSON, PERCENTILE, PERCENTILE.EXC,
  PERCENTILE.INC, PERCENTRANK, PERCENTRANK.EXC, PERCENTRANK.INC, POISSON,
  POISSON.DIST, QUARTILE, QUARTILE.EXC, QUARTILE.INC, RANK, RANK.AVG,
  RANK.EQ, RSQ, SLOPE, STANDARDIZE, STDEV.P, STDEV.S, STDEVA, STDEVPA,
  T.DIST, T.DIST.2T, T.DIST.RT, T.INV, T.INV.2T, TDIST, TINV, TRIMMEAN,
  VAR.P, VAR.S, VARA, VARPA.
- **Text (7) and logic and information (12, owf_calc_fntext.c):** CONCAT,
  DOLLAR, FIXED, NUMBERVALUE, TEXTJOIN, UNICHAR, UNICODE; CELL, ERROR.TYPE,
  IFS, ISEVEN, ISFORMULA, ISODD, ISREF, SHEET, SHEETS, SWITCH, TYPE, XOR.
- **Dates and times (14, owf_calc_fndate.c):** DATEDIF, DATEVALUE, DAYS,
  DAYS360, EDATE, EOMONTH, ISOWEEKNUM, NETWORKDAYS, NETWORKDAYS.INTL,
  TIMEVALUE, WEEKNUM, WORKDAY, WORKDAY.INTL, YEARFRAC.
- **Lookup and reference (10, owf_calc_fnref.c):** ADDRESS, AREAS,
  FORMULATEXT, HYPERLINK, INDIRECT, LOOKUP, OFFSET, TRANSPOSE, XLOOKUP,
  XMATCH.
- **Money (17, owf_calc_fnfin.c):** CUMIPMT, CUMPRINC, DB, DDB, EFFECT,
  FVSCHEDULE, IPMT, IRR, MIRR, NOMINAL, NPER, PPMT, RATE, SLN, SYD, XIRR,
  XNPV.

Notes on the second batch (where Excel's documentation and OpenFormula
differ, the engine follows Excel's, as the .xlsx files expect):

- **OFFSET and INDIRECT** are volatile, as in Excel. The references they
  make are not in the dependency index, so each tells the engine what it
  reached (`calc_note_reach`): when that holds a formula this recalc has
  still to work out, the formula waits and goes again after it; a round in
  which none of the waiting ones can go works them out as they are (a loop
  through INDIRECT, which Excel would call circular). INDIRECT reads A1
  text (with sheets, 3-D, whole rows and columns, and defined names that
  stand for a reference) and R1C1 text (`R2C3`, `R[-1]C`, `C2`, `R1:R3`).
- **Distributions** are our own from their definitions, with only `exp`,
  `log`, `sqrt`, `pow` and the trigonometric functions (the Amiga's maths
  library has no `erf` or `lgamma`): log gamma by Stirling's series after
  the recurrence; erfc by its series and continued fraction; Student's t
  through the regularised incomplete beta function's continued fraction
  (DLMF 8.17.22, Lentz's method); NORM.S.INV from Abramowitz and Stegun
  26.2.23 refined by Halley's method, T.INV by Newton's method in a
  bracket. The t functions truncate the degrees of freedom, as Excel's.
  The binomial is exact as far as the binomial coefficient fits a double
  (n up to 1000), then through the incomplete beta function; the
  cumulative Poisson sums out from its largest term (past a mean of 10^8,
  the normal approximation).
- **RATE, IRR, XIRR** use Newton's method from the guess (10%) to a step
  under 1e-10, with Excel's documented limits of 20, 20 and 100 tries;
  past them, `#NUM!`. RATE works on its equation divided by
  ((1+r)^n - 1)/r (a payment), which is nearly straight for long loans: a
  30-year loan converges in four steps from 10%.
- **DAYS360** (US): a start on its month's last day is the 30th; an end on
  the 31st is the 30th when the start is the 30th, as Excel's
  documentation gives it. **YEARFRAC** bases 0 and 1 follow Excel's
  behaviour as OpenFormula describes it (the February rules of 30/360,
  and actual/actual's 365, 366 or the years' average). **DATEDIF**'s "MD"
  can be negative, as Excel's documentation warns.
- **SUBTOTAL** passes over cells holding SUBTOTAL or AGGREGATE; 101 to 111
  pass over hidden rows (the book keeps no filters, so 1 to 11 count
  every row).
- **ROMAN**'s forms 1 to 4 allow subtractive pairs one numeral smaller at
  each step (499 is CDXCIX, LDVLIV, XDIX, VDIV, ID, as Excel's
  documentation shows). **DOLLAR** is Excel's United States form,
  "$1,234.57" and "($1,234.57)"; **NUMBERVALUE**'s separators default to
  "." and ",".
- **CELL** knows "address", "col", "row", "contents", "type", "format",
  "prefix", "width", "protect", "parentheses", "color" and "filename"; the
  book's file name is not known to the engine, so "filename" is "" (as
  Excel gives for a book not saved) and an address on another sheet has
  its sheet but no book.
- **XLOOKUP** and **XMATCH** have all four match modes and search modes;
  XLOOKUP gives a reference when its return array is one
  (`SUM(XLOOKUP(...))` works). MMULT, MINVERSE, TRANSPOSE, FREQUENCY and
  MODE.MULT give arrays: an array formula's cells show them, and INDEX,
  SUM and the like take them in any formula (the engine has no spilled
  arrays yet).
- MAXIFS, MINIFS, SUMIFS and AVERAGEIFS want their first range the size
  of the criteria ranges (else `#VALUE!`, as Excel).

## Tests

`make calc-test` (and `make calc-check` under AddressSanitizer and
UBSan; both in `make test` and `make check`) runs `tests/calc/test_calc.c`:

- the parser (what parses, what does not, precedence, nesting limits);
- every function, with its errors, from case tables: the first batch's
  in `test_calc.c`, the second's in `tests/calc/fn2_cases.h`, whose
  expected values `tests/calc/fn2_expect.py` works out from the
  functions' definitions with Python's math, statistics, fractions,
  decimal and datetime (no spreadsheet program's output; `make test`
  checks the header is what the script makes). The test checks that the
  table is in order and every function in it has a case; it also calls
  every function with random arguments of every kind, under ASan and
  UBSan in `make check`;
- values, conversions and error rules; references, names, implicit
  intersection, array formulas;
- recalculation (only what reads a change is worked out again; chains of
  20,000; volatile functions), circular references (loops of two, of one,
  of 5,000; what reads a loop);
- typed input and its formats, `owf_calc_move_formula`, `owf_calc_shift`
  (rows and columns, inserted and deleted, other sheets, names, merges,
  row heights), `owf_calc_rebuild`, kept values, number formats;
- `owf_calc_touch`, `owf_calc_rename_sheet` and `owf_calc_move_range`
  (references followed, kept, named, made `#REF!`; names; quoting; moves
  to another sheet and onto themselves);
- random books: 2,400 random edits (values, formulas, clears, inserted
  and deleted rows and columns, cells changed behind the engine's back and
  touched, sheets renamed, cuts and pastes), each followed by a recalc,
  and every 20 a full recalc that must change nothing (`CALC_SEED` runs it
  with another seed); and 12,000 random and broken formulas, bytes and
  number formats through the parser, the machine, the mover and the
  formatter;
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
index and code; less with the Amiga's 4-byte pointers). With its 277
functions its code is about 225 KB at -O2 and 160 KB at -Os on x86-64,
with 11.5 KB of tables (`size` of the eleven objects; the first 119
functions' engine was 91 KB and 5 KB at -Os); the 68040 build needs the
stove to measure. The second batch did not change the speeds above. The largest
stack frame is `calc_format`'s, under 1 KB; parsing goes about 400 bytes
deeper for each defined name inside a name (eight at most).

## Not yet

- The engine's calls in owf.library (`owf_lib.sfd`).
- Typing an array formula (Ctrl+Shift+Enter); Excel 365's spilled arrays.
- R1C1 references in formulas (INDIRECT reads R1C1 text), table
  references, and the functions Excel has and the engine does not: those
  that need spilled arrays (SORT, FILTER, UNIQUE, SEQUENCE, RANDARRAY,
  TEXTSPLIT, VSTACK, LET, LAMBDA...), AGGREGATE, the engineering functions
  (DEC2HEX, CONVERT...), the database functions (DSUM...), VDB, the
  bond and security functions (PRICE, YIELD, ACCRINT...), and the
  chi-squared, F, gamma, beta and lognormal distributions (the incomplete
  beta function they need is there).
- 3-D references are not adjusted when rows or columns go in or out.
- A defined name changed by the program needs `owf_calc_rebuild`.
