/* tests/arexx/calc-pivot.rexx: GFOpenCalc's pivot tables through its ARexx port (Data > Pivot Table's work).
   rx tests/arexx/calc-pivot.rexx   (GFOpenCalc running with a new workbook; prints PASS/FAIL lines, then ALL PASS
   or n FAILED). Leaves RAM:pivot-test.ods and RAM:pivot-test.xlsx. */
OPTIONS RESULTS
port = 'GFOPENCALC'
IF ~SHOW('P', port) THEN DO
    SAY 'FAIL' port 'is not there: start GFOpenCalc first'
    EXIT 20
END
ADDRESS VALUE port
fails = 0

rows = 'Region Product Year Sales Units;East A 2025 10 1;East B 2025 20 2;West A 2025 5 3;West A 2026 15 4;West B 2026 25 5;North B 2026 30 6'
DO r = 1 TO 7
    PARSE VAR rows line ';' rows
    DO c = 1 TO 5
        CALL want 'SETCELL' SUBSTR('ABCDE', c, 1) || r WORD(line, c), 0
    END
END
CALL want 'PIVOT A1:E7 ROWS A,B COLUMNS C DATA D:sum,E:max', 0
CALL want 'SHEET', 0
IF res ~= '2' THEN CALL bad 'the pivot table is not on sheet 2 but' res
CALL cell 'C1', '2025'
CALL cell 'A3', 'East'
CALL cell 'C3', '10'
CALL cell 'G5', '20'
CALL cell 'A8', 'Grand Total'
CALL cell 'G8', '105'
CALL cell 'H8', '6'
CALL want 'PIVOTINFO', 0
SAY '     PIVOTINFO:' res
CALL want 'SAVEAS RAM:pivot-test.ods', 0
CALL want 'SAVEAS RAM:pivot-test.xlsx', 0
CALL want 'SHEET 1', 0
CALL want 'SETCELL D2 100', 0
CALL want 'PIVOTREFRESH', 5                    /* sheet 1 has no pivot table */
CALL want 'SHEET 2', 0
CALL want 'PIVOTREFRESH', 0
CALL cell 'C3', '100'
CALL cell 'G8', '195'
CALL want 'COMMAND UNDO', 0
CALL cell 'G8', '105'
CALL want 'SHEET 1', 0
CALL want 'PIVOT A1:E7 ROWS B DATA D:sum PAGE A=West', 0
CALL cell 'B1', 'West'
CALL cell 'B6', '45'
CALL want 'PIVOT A1:E7 ROWS Q DATA D:sum', 10
CALL want 'PIVOT A1:E7 ROWS A DATA D:median', 10
CALL want 'PIVOT A1:E7 ROWS A', 10            /* no data field */
CALL want 'OPEN RAM:pivot-test.ods', 5        /* unsaved changes */
CALL want 'SAVEAS RAM:pivot-test2.ods', 0
CALL want 'OPEN RAM:pivot-test.xlsx', 0
CALL want 'SHEET 2', 0
CALL want 'PIVOTINFO', 0
SAY '     PIVOTINFO (XLSX):' res
CALL want 'PIVOTREFRESH', 0
CALL cell 'G8', '105'
CALL want 'OPEN RAM:pivot-test.ods', 5
CALL want 'SAVEAS RAM:pivot-test3.xlsx', 0
CALL want 'OPEN RAM:pivot-test.ods', 0
CALL want 'SHEET 2', 0
CALL want 'PIVOTREFRESH', 0
CALL cell 'H8', '6'

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
