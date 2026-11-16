/* tests/arexx/calc-extdata.rexx: GFOpenCalc's external data (Data > External Data) through its ARexx port.
   rx tests/arexx/calc-extdata.rexx   (GFOpenCalc running with a new workbook; prints PASS/FAIL lines, then
   ALL PASS or n FAILED). Makes and deletes RAM:extdata-page.html; leaves RAM:extdata-book.xlsx. A database
   is tried only when GFOpenCalc was built with them (EXTDATAADD DATABASE then gives a number). */
OPTIONS RESULTS
port = 'GFOPENCALC'
IF ~SHOW('P', port) THEN DO
    SAY 'FAIL' port 'is not there: start GFOpenCalc first'
    EXIT 20
END
fails = 0
CALL page '<table id=rates><caption>Rates</caption><tr><th>Currency<th>Rate<tr><td>Euro<td>1.17<tr><td>Yen<td>182.5</table>'
ADDRESS VALUE port

CALL want 'EXTDATA', 0
IF res ~= '0' THEN CALL bad 'EXTDATA gave' res '(wanted 0)'
CALL want 'EXTDATAADD WEB RAM:extdata-page.html rates AT B2', 0
IF res ~= '1' THEN CALL bad 'EXTDATAADD gave' res '(wanted 1)'
CALL cell 'B2', 'Currency'
CALL cell 'C3', '1.17'
CALL cell 'C4', '182.5'
CALL want 'EXTDATAINFO 1', 0
IF res ~= 'WEB|B2:C4|RAM:extdata-page.html|rates|1 0 0' THEN CALL bad 'EXTDATAINFO gave' res
CALL want 'EXTDATAINFO 2', 5
CALL want 'SETCELL E1 =SUM(C3:C4)', 0
CALL cell 'E1', '183.67'

/* the page changes: refreshed, the range shrinks */
CALL page '<table id=rates><tr><th>Currency<th>Rate<tr><td>Euro<td>1.2</table>'
ADDRESS VALUE port
CALL want 'EXTDATAREFRESH 1', 0
CALL cell 'C3', '1.2'
CALL cell 'B4', ''
CALL cell 'E1', '1.2'
/* it grows onto a cell that holds something: refused, then written over */
CALL want 'SETCELL B5 mine', 0
CALL page '<table id=rates><tr><th>Currency<th>Rate<tr><td>A<td>1<tr><td>B<td>2<tr><td>C<td>3</table>'
ADDRESS VALUE port
CALL want 'EXTDATAREFRESH 1', 10
CALL cell 'B5', 'mine'
CALL want 'EXTDATAREFRESH ALL OVERWRITE', 0
CALL cell 'B5', 'C'
CALL want 'EXTDATAREFRESH 3', 5
CALL want 'UNDO', 0
CALL cell 'B5', 'mine'
CALL want 'REDO', 0
/* a table the page lacks: the last values stay */
CALL want 'EXTDATACHANGE 1 WEB RAM:extdata-page.html nope', 10
CALL cell 'B3', 'A'
CALL want 'EXTDATACHANGE 1 WEB RAM:extdata-page.html HTML_1 ONOPEN EVERY 60', 0
CALL want 'EXTDATAINFO 1', 0
IF res ~= 'WEB|B2:C5|RAM:extdata-page.html|HTML_1|1 1 60' THEN CALL bad 'EXTDATAINFO gave' res

/* saved and opened again: kept; an ARexx OPEN reads nothing (the window asks first), EXTDATAREFRESH does */
CALL want 'SAVEAS RAM:extdata-book.xlsx', 0
CALL page '<table><tr><th>Currency<th>Rate<tr><td>Kept<td>9</table>'
ADDRESS VALUE port
CALL want 'OPEN RAM:extdata-book.xlsx', 0
CALL want 'EXTDATA', 0
IF res ~= '1' THEN CALL bad 'EXTDATA gave' res '(wanted 1)'
CALL cell 'B3', 'A'
CALL want 'EXTDATAREFRESH ALL', 0
CALL cell 'B3', 'Kept'
CALL cell 'C3', '9'

/* a database, when this GFOpenCalc has them */
'EXTDATAADD DATABASE RAM:none.oadb Products AT H1'
IF rc = 0 THEN CALL bad 'EXTDATAADD of a database that is not there worked'
ELSE SAY 'PASS EXTDATAADD DATABASE (not there) RC' rc

/* removed: the values stay, or go */
CALL want 'EXTDATAREMOVE 1', 0
CALL cell 'B3', 'Kept'
CALL want 'EXTDATA', 0
IF res ~= '0' THEN CALL bad 'EXTDATA gave' res '(wanted 0)'
CALL want 'UNDO', 0
CALL want 'EXTDATAREMOVE 1 CLEAR', 0
CALL cell 'B3', ''
CALL want 'EXTDATAREMOVE 1', 5
ADDRESS COMMAND 'Delete RAM:extdata-page.html QUIET'

IF fails = 0 THEN SAY 'ALL PASS'
ELSE SAY fails 'FAILED'
EXIT fails

page: PROCEDURE
    PARSE ARG html
    IF OPEN('f', 'RAM:extdata-page.html', 'W') THEN DO
        CALL WRITELN 'f', html
        CALL CLOSE 'f'
    END
    RETURN

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

bad: PROCEDURE EXPOSE fails
    PARSE ARG why
    SAY 'FAIL' why
    fails = fails + 1
    RETURN
