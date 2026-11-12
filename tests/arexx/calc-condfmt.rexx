/* tests/arexx/calc-condfmt.rexx: GFOpenCalc's conditional formats (colour scale, data bar, icon set) through its ARexx port.
   rx tests/arexx/calc-condfmt.rexx   (GFOpenCalc running with a new workbook; prints PASS/FAIL lines, then
   ALL PASS or n FAILED). Leaves RAM:condfmt-test.xlsx and RAM:condfmt-test.ods, and the sheet showing them. */
OPTIONS RESULTS
port = 'GFOPENCALC'
IF ~SHOW('P', port) THEN DO
    SAY 'FAIL' port 'is not there: start GFOpenCalc first'
    EXIT 20
END
ADDRESS VALUE port
fails = 0

DO r = 1 TO 6
    CALL want 'SETCELL A'r (r - 1) * 20, 0
    CALL want 'SETCELL B'r (r - 1) * 20, 0
    CALL want 'SETCELL C'r (r - 1) * 20, 0
    CALL want 'SETCELL D'r (r - 1) * 20, 0
END
CALL want 'CONDFMT A1:A6 bar blue', 0
CALL want 'CONDFMT B1:B6 bar green only', 0
CALL want 'CONDFMT C1:C6 icons 3Arrows', 0
CALL want 'CONDFMT D1:D6 scale red yellow green', 0
CALL want 'CONDFMT D1:D6 icons 5Quarters reverse', 0

/* refused */
CALL want 'CONDFMT A1:A6 icons 6Arrows', 10
CALL want 'CONDFMT A1:A6 bar pink', 10
CALL want 'CONDFMT nowhere bar', 10

/* cleared and back */
CALL want 'CONDFMT C1:C6 CLEAR', 0
CALL want 'COMMAND UNDO', 0
CALL cell 'B3', '40'

CALL want 'SAVEAS RAM:condfmt-test.ods', 0
CALL want 'SAVEAS RAM:condfmt-test.xlsx', 0
CALL want 'OPEN RAM:condfmt-test.ods', 0
CALL cell 'C6', '100'
CALL want 'OPEN RAM:condfmt-test.xlsx', 0
CALL cell 'A6', '100'

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
    ELSE DO
        why = ''
        IF got ~= 0 THEN DO
            'LASTERROR'
            IF rc = 0 THEN why = result
        END
        CALL bad cmd 'RC' got '(wanted' rcwant')' why
    END
    RETURN

bad:
    PARSE ARG why
    SAY 'FAIL' why
    fails = fails + 1
    RETURN
