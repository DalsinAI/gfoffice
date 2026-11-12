/* tests/arexx/calc-protect.rexx: GFOpenCalc's sheet and cell protection through its ARexx port.
   rx tests/arexx/calc-protect.rexx   (GFOpenCalc running with a new workbook; prints PASS/FAIL lines, then
   ALL PASS or n FAILED). Leaves RAM:protect-test.xlsx and RAM:protect-test.ods. */
OPTIONS RESULTS
port = 'GFOPENCALC'
IF ~SHOW('P', port) THEN DO
    SAY 'FAIL' port 'is not there: start GFOpenCalc first'
    EXIT 20
END
ADDRESS VALUE port
fails = 0

CALL want 'SETCELL A1 10', 0
CALL want 'SETCELL B1 =A1*2', 0
CALL want 'SETCELL C1 =A1+1', 0
CALL want 'CELLPROTECT B1 UNLOCKED', 0
CALL want 'CELLPROTECT C1 LOCKED HIDDEN', 0
CALL want 'CELLPROTECT C1 SIDEWAYS', 10
CALL want 'PROTECT PASSWORD test ALLOW INSERTROWS', 0
CALL want 'PROTECT', 5                               /* protected already */
CALL want 'ISPROTECTED', 0
IF res ~= '1 67 1' THEN CALL bad 'ISPROTECTED gave' res '(wanted 1 67 1)'

DO pass = 1 TO 3
    CALL want 'SETCELL A1 5', 10
    CALL want 'SETCELL B1 7', 0
    CALL cell 'B1', '7'
    CALL want 'SETCELL B1 =A1*2', 0
    CALL want 'GETFORMULA C1', 0
    IF res ~= '' THEN CALL bad 'GETFORMULA C1 gave' res '(wanted nothing)'
    CALL want 'CELLPROTECT A1 UNLOCKED', 10
    CALL cell 'A1', '10'
    IF pass = 1 THEN DO
        CALL want 'SAVEAS RAM:protect-test.xlsx', 0
        CALL want 'OPEN RAM:protect-test.xlsx', 0
    END
    IF pass = 2 THEN DO
        CALL want 'SAVEAS RAM:protect-test.ods', 0
        CALL want 'OPEN RAM:protect-test.ods', 0
    END
END

CALL want 'UNPROTECT wrong', 10
CALL want 'UNPROTECT test', 0
CALL want 'UNPROTECT test', 5                        /* not protected */
CALL want 'SETCELL A1 5', 0
CALL cell 'B1', '10'
CALL want 'GETFORMULA C1', 0
IF res ~= '=A1+1' THEN CALL bad 'GETFORMULA C1 gave' res '(wanted =A1+1)'

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
