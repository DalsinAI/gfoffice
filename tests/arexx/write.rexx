/* tests/arexx/write.rexx: GFOpenWrite's ARexx port against amicradle-teams ARexx-RULE.md
   (return codes 0/5/10/20, no requester holds up a command).
   rx tests/arexx/write.rexx   (GFOpenWrite running; prints PASS/FAIL lines, then ALL PASS or n FAILED) */
OPTIONS RESULTS
port = 'GFOPENWRITE'
IF ~SHOW('P', port) THEN DO
    SAY 'FAIL' port 'is not there: start GFOpenWrite first'
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
CALL want 'INSERTTEXT hello', 0
CALL want 'FIND nosuchtext', 5
CALL want 'OPEN RAM:x.odt', 5                  /* unsaved changes kept, no Discard? requester */
CALL want 'QUIT', 5                            /* the same */
CALL want 'SAVEAS RAM:no/such/drawer/x.odt', 10  /* no requester: LASTERROR says why */
CALL want 'LASTERROR', 0
SAY '     LASTERROR:' res
CALL want 'SAVEAS RAM:arexx-test.odt', 0
CALL want 'OPEN RAM:no/such/file.odt', 10
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
