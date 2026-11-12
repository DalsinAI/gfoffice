/* tests/arexx/calc-textcols.rexx: GFOpenCalc's Data > Text to Columns and Remove Duplicates through its ARexx port.
   rx tests/arexx/calc-textcols.rexx   (GFOpenCalc running with a new workbook; prints PASS/FAIL lines, then
   ALL PASS or n FAILED). Leaves RAM:textcols-test.xlsx. */
OPTIONS RESULTS
port = 'GFOPENCALC'
IF ~SHOW('P', port) THEN DO
    SAY 'FAIL' port 'is not there: start GFOpenCalc first'
    EXIT 20
END
ADDRESS VALUE port
fails = 0

/* Text to Columns: commas, a quoted field, a formula reading the result */
CALL want 'SETCELL A1 North,12,"Smith, J"', 0
CALL want 'SETCELL A2 South,7', 0
CALL want 'SETCELL E1 =B1+B2', 0
CALL want 'TEXTTOCOLUMNS A1:A2 COMMA', 0
IF res ~= '3' THEN CALL bad 'TEXTTOCOLUMNS gave' res '(wanted 3)'
CALL cell 'A1', 'North'
CALL cell 'B1', '12'
CALL cell 'C1', 'Smith, J'
CALL cell 'E1', '19'
CALL want 'COMMAND UNDO', 0
CALL cell 'A2', 'South,7'
CALL cell 'B1', ''
CALL want 'COMMAND REDO', 0
CALL cell 'B2', '7'

/* spaces merged, the first column kept as text, fixed widths */
CALL want 'SETCELL A4 007   42  x', 0
CALL want 'TEXTTOCOLUMNS A4 SPACE MERGE TYPES TSG', 0
CALL cell 'A4', '007'
CALL cell 'B4', 'x'
CALL want 'SETCELL A5 AB12345XY', 0
CALL want 'TEXTTOCOLUMNS A5 WIDTHS 2,7', 0
CALL cell 'B5', '12345'
CALL cell 'C5', 'XY'

/* refused */
CALL want 'TEXTTOCOLUMNS A1:B2 COMMA', 10          /* two columns */
CALL want 'TEXTTOCOLUMNS A1:A2', 10                /* no delimiter */
CALL want 'TEXTTOCOLUMNS A1:A2 WIDTHS 5,2', 10

/* Remove Duplicates: text without case, a formula following the rows kept */
CALL want 'SETCELL A10 Name', 0
CALL want 'SETCELL A11 apple', 0
CALL want 'SETCELL A12 Pear', 0
CALL want 'SETCELL A13 APPLE', 0
CALL want 'SETCELL A14 Fig', 0
CALL want 'SETCELL B11 1', 0
CALL want 'SETCELL B12 2', 0
CALL want 'SETCELL B13 1', 0
CALL want 'SETCELL B14 5', 0
CALL want 'SETCELL E10 =B14', 0
CALL want 'REMOVEDUPS A10:B14 HEADER', 0
IF res ~= '1' THEN CALL bad 'REMOVEDUPS gave' res '(wanted 1)'
CALL cell 'A13', 'Fig'
CALL cell 'A14', ''
CALL cell 'E10', '5'
CALL want 'GETFORMULA E10', 0
IF res ~= '=B13' THEN CALL bad 'GETFORMULA E10 gave' res '(wanted =B13)'
CALL want 'COMMAND UNDO', 0
CALL cell 'A13', 'APPLE'
CALL want 'REMOVEDUPS A10:B14 HEADER COLUMNS B', 0
IF res ~= '1' THEN CALL bad 'REMOVEDUPS COLUMNS B gave' res '(wanted 1)'
CALL want 'REMOVEDUPS A10:B13 HEADER', 0
IF res ~= '0' THEN CALL bad 'REMOVEDUPS again gave' res '(wanted 0)'
CALL want 'REMOVEDUPS A10:B13 COLUMNS F', 10       /* outside the range */

CALL want 'SAVEAS RAM:textcols-test.xlsx', 0
CALL want 'OPEN RAM:textcols-test.xlsx', 0
CALL cell 'C1', 'Smith, J'
CALL cell 'A13', 'Fig'

IF fails = 0 THEN SAY 'ALL PASS'
ELSE SAY fails 'FAILED'
EXIT fails

cell: PROCEDURE EXPOSE fails res
    PARSE ARG ref, wantv
    CALL want 'GETCELL' ref, 0
    IF res ~= wantv THEN CALL bad 'GETCELL' ref 'gave' res '(wanted' wantv')'
    RETURN

want: PROCEDURE EXPOSE fails res
    PARSE ARG cmd, rcwant
    res = ''
    cmd
    got = rc
    IF got = 0 THEN res = result
    IF got = rcwant THEN SAY 'PASS' cmd 'RC' got
    ELSE CALL bad cmd 'RC' got '(wanted' rcwant')'
    RETURN

bad:
    PARSE ARG why
    SAY 'FAIL' why
    fails = fails + 1
    RETURN
