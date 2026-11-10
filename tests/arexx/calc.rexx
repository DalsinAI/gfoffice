/* tests/arexx/calc.rexx: GFOpenCalc's ARexx port against amicradle-teams ARexx-RULE.md
   (return codes 0/5/10/20, no requester holds up a command).
   rx tests/arexx/calc.rexx   (GFOpenCalc running; prints PASS/FAIL lines, then ALL PASS or n FAILED) */
OPTIONS RESULTS
port = 'GFOPENCALC'
IF ~SHOW('P', port) THEN DO
    SAY 'FAIL' port 'is not there: start GFOpenCalc first'
    EXIT 20
END
ADDRESS VALUE port
fails = 0

CALL want 'VERSION', 0
CALL want 'HELP', 0
CALL want 'FAULT 205', 0
CALL want 'FAULT', 10
CALL want 'NOSUCHCOMMAND', 20
CALL want 'LASTERROR', 0
IF res = '' THEN CALL bad 'LASTERROR after an unknown command is empty'
ELSE SAY '     LASTERROR:' res
CALL want 'OPEN', 10
CALL want 'SETCELL A1 42', 0
CALL want 'GETCELL A1', 0
IF res ~= '42' THEN CALL bad 'GETCELL A1 gave' res
CALL want 'GETCELL notacell', 10
CALL want 'OPEN RAM:x.ods', 5                  /* unsaved changes kept, no Discard? requester */
CALL want 'QUIT', 5                            /* the same */
CALL want 'SAVEAS RAM:no/such/drawer/x.ods', 10  /* no requester: LASTERROR says why */
CALL want 'LASTERROR', 0
SAY '     LASTERROR:' res
CALL want 'SAVEAS RAM:arexx-test.ods', 0
CALL want 'OPEN RAM:no/such/file.ods', 10
CALL want 'LASTERROR', 0
SAY '     LASTERROR:' res
CALL want 'DEACTIVATE', 5                      /* no icon to go to */
CALL want 'ACTIVATE', 0
CALL want 'QUIT', 0

IF fails = 0 THEN SAY 'ALL PASS'
ELSE SAY fails 'FAILED'
EXIT fails

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
