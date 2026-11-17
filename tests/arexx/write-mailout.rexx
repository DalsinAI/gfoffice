/* tests/arexx/write-mailout.rexx: GFOpenWrite 2.0.37's gap 4.2 through its ARexx port: a sheet of labels of
   one's own, labels from the mail merge on it, records for Merge to Printer refused when wrong, the envelope
   with a return address, the printer's feed. Nothing is printed and no requester opens.
   rx tests/arexx/write-mailout.rexx DRAWER   (GFOpenWrite running on a new document; DRAWER holds people.csv,
   the files are saved there; prints PASS/FAIL lines, then ALL PASS or n FAILED) */
OPTIONS RESULTS
PARSE ARG dir
IF dir = '' THEN dir = 'RAM:'
IF RIGHT(dir, 1) ~= ':' & RIGHT(dir, 1) ~= '/' THEN dir = dir'/'
port = 'GFOPENWRITE'
IF ~SHOW('P', port) THEN DO
    SAY 'FAIL' port 'is not there: start GFOpenWrite first'
    EXIT 20
END
ADDRESS VALUE port
fails = 0

CALL want 'VERSION', 0
IF POS('2.0.37', res) = 0 THEN CALL bad 'VERSION is' res
CALL want 'INSERTTEXT To ', 0
CALL want 'MERGESOURCE' dir'people.csv', 0
IF res ~= 3 THEN CALL bad 'MERGESOURCE records' res
CALL want 'INSERTMERGEFIELD Name', 0
CALL want 'LABELSIZE 148 210 2 2 70 100 4 5 70 100', 0
SAY '     LABELSIZE:' res
IF LEFT(res, 15) ~= 'page 148 x 210 ' THEN CALL bad 'LABELSIZE answered' res
CALL want 'LABELSIZE 210 297 4 4 70 50 4 5', 10        /* four 70 mm labels across 210 mm: refused */
CALL want 'LABELSIZE', 0                               /* the good one kept */
IF LEFT(res, 15) ~= 'page 148 x 210 ' THEN CALL bad 'LABELSIZE kept' res
CALL want 'LABELSHEETS', 0
IF RIGHT(res, 7) ~= '|Custom' THEN CALL bad 'LABELSHEETS ends' res
n = 1 + LENGTH(res) - LENGTH(COMPRESS(res, '|'))
CALL want 'LABELS' n dir'mailout-labels.odt', 0
IF res ~= 3 THEN CALL bad 'LABELS made' res 'labels'
CALL want 'MERGE PRINT 9', 10                          /* no record 9: refused before anything prints */
CALL want 'MERGE PRINT two', 10
CALL want 'RETURNADDRESS J Bloggs|2 Low Road|York', 0
CALL want 'ENVELOPE C6', 0
IF res ~= '9184 6463' THEN CALL bad 'ENVELOPE C6 is' res
CALL want 'RETURNADDRESS', 0
IF res ~= 'J Bloggs|2 Low Road|York' THEN CALL bad 'RETURNADDRESS kept' res
CALL want 'FEED 2', 0
SAY '     FEED:' res
IF LEFT(res, 2) ~= '2 ' THEN CALL bad 'FEED answered' res
CALL want 'FEED 9', 10
CALL want 'SAVEAS' dir'mailout-envelope.odt', 0
CALL want 'FEED 0', 0
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
