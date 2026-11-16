/* tests/arexx/calc-links.rexx: GFOpenCalc's links to other workbooks (Data > Edit Links) through its ARexx port.
   rx tests/arexx/calc-links.rexx   (GFOpenCalc running with a new workbook; prints PASS/FAIL lines, then
   ALL PASS or n FAILED). Leaves RAM:links-main.xlsx; makes and deletes RAM:links-src.xlsx. */
OPTIONS RESULTS
port = 'GFOPENCALC'
IF ~SHOW('P', port) THEN DO
    SAY 'FAIL' port 'is not there: start GFOpenCalc first'
    EXIT 20
END
ADDRESS VALUE port
fails = 0

/* the source: Sheet1!A1 is 21 */
CALL want 'SETCELL A1 21', 0
CALL want 'SAVEAS RAM:links-src.xlsx', 0
/* the book: a formula on the source, which it does not keep yet: #REF! */
CALL want "SETCELL A1 ='[links-src.xlsx]Sheet1'!A1*2", 0
CALL cell 'A1', '#REF!'
CALL want 'LINKS', 0
IF res ~= '' THEN CALL bad 'LINKS gave' res '(wanted nothing)'
/* saved and opened again, the book links the source; updated, the formula has its value */
CALL want 'SAVEAS RAM:links-main.xlsx', 0
CALL want 'OPEN RAM:links-main.xlsx', 0
CALL want 'LINKS', 0
IF res ~= 'links-src.xlsx=links-src.xlsx' THEN CALL bad 'LINKS gave' res '(wanted links-src.xlsx=links-src.xlsx)'
CALL want 'UPDATELINK 1', 0
CALL cell 'A1', '42'
CALL want 'UPDATELINK 2', 5                          /* no such link */
CALL want 'UPDATELINK ALL', 0
/* the source gone: the last values stay */
ADDRESS COMMAND 'Delete RAM:links-src.xlsx QUIET'
ADDRESS VALUE port
CALL want 'UPDATELINK 1', 10
CALL cell 'A1', '42'
/* saved with the values, and opened again: still 42, though the source is not there */
CALL want 'SAVE', 0
CALL want 'OPEN RAM:links-main.xlsx', 0
CALL want 'RECALC', 0
CALL cell 'A1', '42'

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
