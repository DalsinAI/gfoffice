/* tests/arexx/calc-scenario.rexx: GFOpenCalc's scenarios through its ARexx port (Data > Scenarios' work).
   rx tests/arexx/calc-scenario.rexx   (GFOpenCalc running with a new workbook; prints PASS/FAIL lines, then ALL PASS
   or n FAILED). Leaves RAM:scenario-test.ods and RAM:scenario-test.xlsx. */
OPTIONS RESULTS
port = 'GFOPENCALC'
IF ~SHOW('P', port) THEN DO
    SAY 'FAIL' port 'is not there: start GFOpenCalc first'
    EXIT 20
END
ADDRESS VALUE port
fails = 0

CALL want 'SETCELL B2 10', 0
CALL want 'SETCELL C2 20', 0
CALL want 'SETCELL B6 =B2+C2', 0
CALL want 'SCENARIO ADD Low B2:C2 the low case', 0
CALL want 'SETCELL B2 100', 0
CALL want 'SETCELL C2 200', 0
CALL want 'SCENARIO ADD "High case" B2:C2', 0
CALL want 'SCENARIO ADD low B2', 10             /* the name is taken */
CALL want 'SCENARIO LIST', 0
IF res ~= 'Low,High case' THEN CALL bad 'SCENARIO LIST gave' res
CALL cell 'B6', '300'
CALL want 'SCENARIO SHOW Low', 0
CALL cell 'B6', '30'
CALL want 'SCENARIO SHOW "High case"', 0
CALL cell 'B6', '300'
CALL want 'COMMAND UNDO', 0
CALL cell 'B6', '30'
CALL want 'SCENARIO SHOW Middle', 5             /* no such scenario */
CALL want 'SCENARIO ADD Bad nowhere', 10
CALL want 'SAVEAS RAM:scenario-test.ods', 0
CALL want 'SAVEAS RAM:scenario-test.xlsx', 0
CALL want 'OPEN RAM:scenario-test.ods', 0
CALL want 'SCENARIO LIST', 0
IF res ~= 'Low,High case' THEN CALL bad 'SCENARIO LIST after ODS gave' res
CALL want 'SCENARIO SHOW "High case"', 0
CALL cell 'B6', '300'
CALL want 'OPEN RAM:scenario-test.xlsx', 5      /* unsaved changes */
CALL want 'SCENARIO DELETE Low', 0
CALL want 'SCENARIO LIST', 0
IF res ~= 'High case' THEN CALL bad 'SCENARIO LIST after DELETE gave' res
CALL want 'SAVEAS RAM:scenario-test2.ods', 0
CALL want 'OPEN RAM:scenario-test.xlsx', 0
CALL want 'SCENARIO SHOW Low', 0
CALL cell 'B6', '30'

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
