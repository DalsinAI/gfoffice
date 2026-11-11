/* tests/arexx/calc-chart.rexx: GFOpenCalc on a sheet with a chart, through its ARexx port.
   rx tests/arexx/calc-chart.rexx features-openpyxl.xlsx   (GFOpenCalc running with no unsaved changes; the file is
   tests/book/fixtures/features-openpyxl.xlsx, whose 8th sheet "Chart" has a bar chart; prints PASS/FAIL lines,
   then ALL PASS or n FAILED). Shows every sheet, the chart's sheet more than once, moves the chart and undoes
   it, saves, and opens the file again: each frees what the chart was drawn from. Leaves RAM:chart-test.xlsx. */
OPTIONS RESULTS
PARSE ARG file
IF file = '' THEN file = 'features-openpyxl.xlsx'
port = 'GFOPENCALC'
IF ~SHOW('P', port) THEN DO
    SAY 'FAIL' port 'is not there: start GFOpenCalc first'
    EXIT 20
END
ADDRESS VALUE port
fails = 0

DO pass = 1 TO 2
    'OPEN' file                                    /* RC 10 when the file had things it could not keep */
    IF rc ~= 0 & rc ~= 10 THEN CALL bad 'OPEN' file 'RC' rc
    CALL want 'SHEETS', 0
    IF res ~= '8' THEN CALL bad 'SHEETS gave' res
    DO s = 1 TO 8
        CALL want 'SHEET' s, 0
    END
    CALL cell 'A1', 'North'
    CALL cell 'B4', '15'
    CALL want 'CHARTSELECT 1', 0
    CALL want 'CHARTSELECT', 0
    IF res ~= '1' THEN CALL bad 'CHARTSELECT gave' res
    CALL want 'SHEET 1', 0
    CALL want 'SHEET 8', 0
    CALL want 'SHEET 7', 0
    CALL want 'SHEET 8', 0
    CALL want 'CHARTSELECT 1', 0
    CALL want 'CHARTMOVE BY 2 1', 0
    CALL want 'UNDO', 0
    CALL want 'REDO', 0
    CALL want 'UNDO', 0
    CALL want 'SHEET 1', 0
    CALL want 'SHEET 8', 0
    CALL cell 'A1', 'North'
    CALL want 'SAVEAS RAM:chart-test.xlsx', 0       /* saved: the next OPEN has no unsaved changes to refuse */
END

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
