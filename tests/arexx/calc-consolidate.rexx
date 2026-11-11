/* tests/arexx/calc-consolidate.rexx: GFOpenCalc's Data > Consolidate through its ARexx port.
   rx tests/arexx/calc-consolidate.rexx   (GFOpenCalc running with a new workbook; prints PASS/FAIL lines, then
   ALL PASS or n FAILED). Leaves RAM:consolidate-test.ods. */
OPTIONS RESULTS
port = 'GFOPENCALC'
IF ~SHOW('P', port) THEN DO
    SAY 'FAIL' port 'is not there: start GFOpenCalc first'
    EXIT 20
END
ADDRESS VALUE port
fails = 0

CALL want 'SETCELL B1 Jan', 0
CALL want 'SETCELL C1 Feb', 0
CALL want 'SETCELL A2 East', 0
CALL want 'SETCELL B2 10', 0
CALL want 'SETCELL C2 20', 0
CALL want 'SETCELL A3 West', 0
CALL want 'SETCELL B3 1', 0
CALL want 'SETCELL C3 2', 0
CALL want 'ADDSHEET Q2', 0
CALL want 'SETCELL B1 Feb', 0
CALL want 'SETCELL C1 Mar', 0
CALL want 'SETCELL A2 west', 0
CALL want 'SETCELL B2 5', 0
CALL want 'SETCELL C2 6', 0
CALL want 'SETCELL A3 North', 0
CALL want 'SETCELL B3 7', 0
CALL want 'SETCELL C3 8', 0
CALL want 'SHEET 1', 0

/* by both labels: East, West, North down; Jan, Feb, Mar across */
CALL want "CONSOLIDATE SUM A1:C3 'Q2'!A1:C3 TO E1 ROWLABELS COLUMNLABELS", 0
CALL cell 'H1', 'Mar'
CALL cell 'E4', 'North'
CALL cell 'G3', '7'
CALL cell 'H4', '8'
CALL want 'COMMAND UNDO', 0
CALL cell 'G3', ''

/* by position, linked */
CALL want "CONSOLIDATE SUM B2:C3 'Q2'!B2:C3 TO J1 LINK", 0
CALL cell 'J1', '15'
CALL cell 'K2', '10'
CALL want 'SETCELL B2 100', 0
CALL cell 'J1', '105'
CALL want 'CONSOLIDATE MAX B2:C3 "''Q2''!B2:C3" TO J5', 0
CALL cell 'J5', '100'

/* refused */
CALL want 'CONSOLIDATE SUM B2:C3 TO C3', 10       /* inside a source */
CALL want 'CONSOLIDATE MEDIAN B2:C3 TO J9', 10
CALL want 'CONSOLIDATE SUM B2:C3', 10

CALL want 'SAVEAS RAM:consolidate-test.ods', 0
CALL want 'OPEN RAM:consolidate-test.ods', 0
CALL cell 'J1', '105'

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
