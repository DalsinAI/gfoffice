/* tests/arexx/calc-names.rexx: GFOpenCalc's names manager and advanced filter through its ARexx port.
   rx tests/arexx/calc-names.rexx   (GFOpenCalc running with a new workbook; prints PASS/FAIL lines, then ALL PASS
   or n FAILED). Leaves RAM:names-test.ods. */
OPTIONS RESULTS
port = 'GFOPENCALC'
IF ~SHOW('P', port) THEN DO
    SAY 'FAIL' port 'is not there: start GFOpenCalc first'
    EXIT 20
END
ADDRESS VALUE port
fails = 0

/* names */
CALL want 'SETCELL B2 2', 0
CALL want 'SETCELL B3 3', 0
CALL want "NAMEDEFINE Sales 'Sheet1'!$B$2:$B$3", 0
CALL want 'NAMEDEFINE Rate 0.5', 0
CALL want 'SETCELL D1 =SUM(Sales)+Rate', 0
CALL cell 'D1', '5.5'
CALL want 'NAMERENAME Sales Revenue', 0
CALL want 'GETFORMULA D1', 0
IF res ~= '=SUM(Revenue)+Rate' THEN CALL bad 'GETFORMULA D1 gave' res
CALL cell 'D1', '5.5'
CALL want 'NAMES', 0
IF res ~= "Revenue='Sheet1'!$B$2:$B$3,Rate=0.5" THEN CALL bad 'NAMES gave' res
CALL want 'NAMERENAME Revenue Rate', 10          /* taken */
CALL want 'NAMEDELETE Missing', 5
CALL want 'NAMEDELETE Rate', 0
CALL want 'COMMAND UNDO', 0
CALL want 'NAMES', 0
IF res ~= "Revenue='Sheet1'!$B$2:$B$3,Rate=0.5" THEN CALL bad 'NAMES after UNDO gave' res

/* advanced filter: list F1:G5, criteria I1:I2 */
CALL want 'SETCELL F1 Region', 0
CALL want 'SETCELL G1 Sales', 0
CALL want 'SETCELL F2 East', 0
CALL want 'SETCELL G2 10', 0
CALL want 'SETCELL F3 West', 0
CALL want 'SETCELL G3 25', 0
CALL want 'SETCELL F4 Eastern', 0
CALL want 'SETCELL G4 40', 0
CALL want 'SETCELL F5 East', 0
CALL want 'SETCELL G5 10', 0
CALL want 'SETCELL I1 Region', 0
CALL want 'SETCELL I2 East', 0
CALL want 'ADVFILTER F1:G5 I1:I2 TO K1 UNIQUE', 0
IF res ~= '2' THEN CALL bad 'ADVFILTER kept' res
CALL cell 'K2', 'East'
CALL cell 'K3', 'Eastern'
CALL cell 'K4', ''
CALL want 'ADVFILTER F1:G5 I1:I2', 0
IF res ~= '3' THEN CALL bad 'ADVFILTER in place kept' res
CALL want 'ADVFILTER F1:G5 J1:J2', 0              /* no headings: every row */
CALL want 'SETCELL J1 Colour', 0
CALL want 'ADVFILTER F1:G5 I1:J2', 10            /* not one of the list's headings */
CALL want 'SAVEAS RAM:names-test.ods', 0
CALL want 'OPEN RAM:names-test.ods', 0
CALL want 'NAMES', 0
IF res ~= "Revenue='Sheet1'!$B$2:$B$3,Rate=0.5" THEN CALL bad 'NAMES after ODS gave' res

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
