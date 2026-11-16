/* tests/arexx/calc-track.rexx: GFOpenCalc's tracked changes (Edit > Track Changes) through its ARexx port.
   rx tests/arexx/calc-track.rexx   (GFOpenCalc running with a new workbook; prints PASS/FAIL lines, then
   ALL PASS or n FAILED). Changes in files (XLSX, ODS) come with their own pull requests. */
OPTIONS RESULTS
port = 'GFOPENCALC'
IF ~SHOW('P', port) THEN DO
    SAY 'FAIL' port 'is not there: start GFOpenCalc first'
    EXIT 20
END
ADDRESS VALUE port
fails = 0

CALL want 'SETCELL A1 5', 0                   /* before recording: no change */
CALL want 'TRACKCHANGES', 0
IF res ~= '0' THEN CALL bad 'TRACKCHANGES gave' res '(wanted 0)'
CALL want 'CHANGEAUTHOR Kill Crazy', 0
CALL want 'TRACKCHANGES ON', 0
CALL want 'SETCELL A1 7', 0
CALL want 'SETCELL B2 =A1*2', 0
CALL want 'CHANGES', 0
IF res ~= 2 THEN CALL bad 'CHANGES gave' res '(wanted 2)'
CALL want 'CHANGE 1', 0
IF LEFT(res, 34) ~= 'Cell Sheet1.A1 changed from 5 to 7' | POS('(Kill Crazy, ', res) = 0 THEN CALL bad 'CHANGE 1 gave' res
CALL want 'CHANGE 3', 5                       /* no such change */
CALL want 'SHOWCHANGE 2', 0
/* reject the formula: B2 empty again; accept A1: it stays 7 */
CALL want 'REJECTCHANGE 2', 0
IF res ~= 1 THEN CALL bad 'REJECTCHANGE 2 left' res '(wanted 1)'
CALL cell 'B2', ''
CALL want 'ACCEPTCHANGE 1', 0
CALL cell 'A1', '7'
CALL want 'CHANGES', 0
IF res ~= 0 THEN CALL bad 'CHANGES after accept gave' res
/* a row inserted, then all rejected */
CALL want 'GOTO A1', 0
CALL want 'COMMAND INSERTROWS', 0
CALL want 'SETCELL A1 new', 0
CALL want 'REJECTCHANGE ALL', 0
CALL cell 'A1', '7'
CALL want 'REJECTCHANGE ALL', 5               /* nothing left */
CALL want 'SETCELL C3 9', 0
/* off drops the changes, so it wants FORCE */
CALL want 'TRACKCHANGES OFF', 5
CALL want 'TRACKCHANGES OFF FORCE', 0
CALL want 'CHANGES', 0
IF res ~= 0 THEN CALL bad 'CHANGES after OFF FORCE gave' res
CALL cell 'C3', '9'                           /* the sheet stays as it is */
CALL want 'TRACKCHANGES MAYBE', 10

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
