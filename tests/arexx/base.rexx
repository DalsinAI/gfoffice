/* tests/arexx/base.rexx: GFOpenBase's ARexx port against amicradle-teams ARexx-RULE.md (return codes
   0/5/10/20, no requester holds up a command), and opening a row hit as ACFind and GFOpenBench do:
   OPEN FILE x TABLE t ROW n.
   rx tests/arexx/base.rexx project   (GFOpenBase running; project a copy of a GFOpenBase project whose
   tables are Customers with rows 1 and 118, logins and Orders: task 54's lab database, adopted;
   prints PASS/FAIL lines, then ALL PASS or n FAILED) */
OPTIONS RESULTS
PARSE ARG project
IF project = '' THEN project = 'RAM:Customers.oadb'
port = 'GFOPENBASE'
IF ~SHOW('P', port) THEN DO
    SAY 'FAIL' port 'is not there: start GFOpenBase first'
    EXIT 20
END
ADDRESS VALUE port
fails = 0

CALL want 'VERSION', 0
SAY '     VERSION:' res
CALL want 'HELP', 0
CALL want 'FAULT 205', 0
CALL want 'FAULT', 10
CALL want 'NOSUCHCOMMAND', 20
CALL want 'LASTERROR', 0
IF res = '' THEN CALL bad 'LASTERROR after an unknown command is empty'
CALL want 'OPEN', 10
CALL want 'OPEN FILE "RAM:no such project.oadb"', 10
CALL want 'LASTERROR', 0
SAY '     LASTERROR:' res
CALL want 'OPEN FILE "'project'" TABLE "Customers" ROW 118', 0
CALL want 'GETFILE', 0
CALL same 'GETFILE', res, project
CALL want 'TABLE', 0
CALL same 'TABLE', res, 'Customers'
CALL want 'ROW', 0
CALL same 'ROW', res, 118
CALL want 'TABLES', 0
SAY '     TABLES:' res
CALL want 'ROW 1', 0
CALL want 'ROW', 0
CALL same 'ROW after ROW 1', res, 1
CALL want 'ROW #2', 0
CALL want 'ROW', 0
CALL same 'ROW after ROW #2', res, 118
CALL want 'ROW 99999', 5
CALL want 'ROW #0', 5
CALL want 'TABLE "no such table"', 10
CALL want 'OPEN TABLE "orders"', 0                 /* any case; the project already open */
CALL want 'TABLE', 0
CALL same 'TABLE after OPEN TABLE', res, 'Orders'
CALL want 'OPEN FILE="'project'" TABLE=Customers ROW=#1', 0
CALL want 'ROW', 0
CALL same 'ROW after OPEN ROW=#1', res, 1
CALL want 'ISDIRTY', 0
CALL same 'ISDIRTY', res, 0
CALL want 'DEACTIVATE', 5
CALL want 'ACTIVATE', 0

/* rows (0.10.0): read, add, change, delete and walk, no requesters */
CALL want 'OPEN TABLE "Customers"', 0
CALL want 'GETFIELDS', 0
SAY '     GETFIELDS:' res
CALL want 'ADDROW "Name=ARexx Test" Town=Hull', 0
added = res
CALL want 'GETROW KEY' added 'FIELD town', 0
CALL same 'GETROW FIELD town', res, 'Hull'
CALL want 'CHANGEROW KEY' added '"Town=York"', 0
CALL want 'GETROW FIELD Town', 0                  /* the row changed is the one chosen */
CALL same 'GETROW after CHANGEROW', res, 'York'
CALL want 'GETROW KEY' added, 0
SAY '     GETROW:' TRANSLATE(res, '|', '09'x)
CALL want 'ADDROW Nosuch=1', 10
CALL want 'LASTERROR', 0
SAY '     LASTERROR:' res
CALL want 'GETROW KEY 99999', 5
CALL want 'ROW 1', 0                               /* every row, key order */
CALL want 'COUNTROWS', 0
rows = res
CALL want 'FIRSTROW', 0
CALL same 'FIRSTROW', res, 1
CALL want 'PREVROW', 5
CALL want 'LASTROW', 0
CALL same 'LASTROW', res, added
CALL want 'NEXTROW', 5
CALL want 'PREVROW', 0
CALL want 'DELETEROW KEY' added, 0
CALL want 'GETROW KEY' added, 5
CALL want 'COUNTROWS', 0
CALL same 'COUNTROWS after DELETEROW', res, rows - 1
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

same: PROCEDURE EXPOSE fails
    PARSE ARG what, got, wanted
    IF got == wanted THEN SAY 'PASS' what '=' got
    ELSE CALL bad what '=' got '(wanted' wanted')'
    RETURN

bad:
    PARSE ARG why
    SAY 'FAIL' why
    fails = fails + 1
    RETURN
