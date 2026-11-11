/* tests/arexx/calc-multiop.rexx: GFOpenCalc's Multiple Operations kept live through its ARexx port.
   rx tests/arexx/calc-multiop.rexx   (GFOpenCalc running with a new workbook; prints PASS/FAIL lines, then ALL PASS
   or n FAILED). Leaves RAM:multiop-test.ods. */
OPTIONS RESULTS
port = 'GFOPENCALC'
IF ~SHOW('P', port) THEN DO
    SAY 'FAIL' port 'is not there: start GFOpenCalc first'
    EXIT 20
END
ADDRESS VALUE port
fails = 0

CALL want 'SETCELL A1 1', 0
CALL want 'SETCELL A2 2', 0
CALL want 'SETCELL A3 3', 0
CALL want 'SETCELL D1 0', 0
CALL want 'SETCELL E1 1', 0
CALL want 'SETCELL C1 =D1*D1+E1', 0
CALL want 'MULTIOP A1:A3 C1 D1', 0
CALL cell 'B3', '10'
CALL want 'SETCELL E1 10', 0                   /* the model changes: the table follows */
CALL cell 'B1', '11'
CALL cell 'B3', '19'
CALL want 'SETCELL A3 4', 0                    /* a trial changes */
CALL cell 'B3', '26'
CALL want 'COMMAND UNDO', 0
CALL cell 'B3', '19'
CALL want 'COMMAND REDO', 0
CALL cell 'B3', '26'
CALL want 'SAVEAS RAM:multiop-test.ods', 0
CALL want 'OPEN RAM:multiop-test.ods', 0
CALL want 'SETCELL E1 100', 0                  /* still live after the file */
CALL cell 'B1', '101'
CALL want 'MULTIOP A1:A3 nowhere D1', 10

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
