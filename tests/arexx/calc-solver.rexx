/* tests/arexx/calc-solver.rexx: GFOpenCalc's Solver through its ARexx port (Data > Solver's work).
   rx tests/arexx/calc-solver.rexx   (GFOpenCalc running with a new workbook; prints PASS/FAIL lines, then ALL PASS
   or n FAILED). Leaves RAM:solver-test.ods and RAM:solver-test.xlsx. */
OPTIONS RESULTS
port = 'GFOPENCALC'
IF ~SHOW('P', port) THEN DO
    SAY 'FAIL' port 'is not there: start GFOpenCalc first'
    EXIT 20
END
ADDRESS VALUE port
fails = 0

/* two products made in B2 and C2; profit B5 = 3 B2 + 5 C2; hours B3 <= 4, C3 <= 12, B4 <= 18 */
CALL want 'SETCELL B2 0', 0
CALL want 'SETCELL C2 0', 0
CALL want 'SETCELL B3 =B2', 0
CALL want 'SETCELL C3 =2*C2', 0
CALL want 'SETCELL B4 =3*B2+2*C2', 0
CALL want 'SETCELL B5 =3*B2+5*C2', 0
CALL want 'SETCELL D3 4', 0
CALL want 'SOLVE MAX B5 BY B2:C2 B3<=D3 C3<=12 B4<=18', 0
IF res ~= '36 B2=2 C2=6' THEN CALL bad 'SOLVE gave' res
CALL cell 'B5', '0'                            /* not kept */
CALL want 'SOLVE MAX B5 BY B2:C2 B3<=D3 C3<=12 B4<=18 KEEP', 0
CALL cell 'B2', '2'
CALL cell 'C2', '6'
CALL cell 'B5', '36'
CALL want 'COMMAND UNDO', 0
CALL cell 'B5', '0'
CALL want 'COMMAND REDO', 0
CALL cell 'B5', '36'
CALL want 'SOLVE', 0                           /* the sheet's own model, kept with it */
IF res ~= '36 B2=2 C2=6' THEN CALL bad 'SOLVE (kept model) gave' res
CALL want 'SOLVE MIN B5 BY B2:C2 B4>=7 INT B2', 0
IF res ~= '8.5 B2=2 C2=0.5' THEN CALL bad 'SOLVE MIN with a whole number gave' res
CALL want 'SOLVE VALUE B5 20 BY B2:C2 C2=1', 0
IF res ~= '20 B2=5 C2=1' THEN CALL bad 'SOLVE VALUE gave' res
CALL want 'SOLVE MAX B5 BY B2:C2 B3<=D3 C3<=12 BIN B2:C2', 0
IF res ~= '8 B2=1 C2=1' THEN CALL bad 'SOLVE with binaries gave' res
CALL want 'SOLVE MAX B5 BY B2:C2', 5           /* no limit */
CALL want 'LASTERROR', 0
SAY '     LASTERROR:' res
CALL want 'SOLVE MAX B5 BY B2:C2 B4<=5 B4>=6', 5   /* no answer */
CALL want 'SETCELL B6 =B2*C2', 0
CALL want 'SOLVE MAX B6 BY B2:C2 B4<=18', 5     /* not linear */
CALL want 'LASTERROR', 0
SAY '     LASTERROR:' res
CALL want 'SOLVE MAX D3 BY B2:C2', 10           /* the target holds no formula */
CALL want 'SOLVE MAX B5 BY B3', 10              /* a variable holds a formula */
CALL want 'SOLVE MAX B5 BY B2:C2 B4<D3', 10     /* < is not a relation */
CALL want 'SOLVE MAX B5 BY B2:C2 B3<=D3 C3<=12 B4<=18 KEEP', 0
CALL want 'SAVEAS RAM:solver-test.xlsx', 0
CALL want 'SAVEAS RAM:solver-test.ods', 0
CALL want 'OPEN RAM:solver-test.xlsx', 0
CALL want 'SOLVE', 0
IF res ~= '36 B2=2 C2=6' THEN CALL bad 'SOLVE after XLSX gave' res
CALL want 'OPEN RAM:solver-test.ods', 0
CALL want 'SOLVE', 0
IF res ~= '36 B2=2 C2=6' THEN CALL bad 'SOLVE after ODS gave' res

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
