/* tests/arexx/diag.rexx: GFOpenDiag's ARexx port on the Amiga, against amicradle-teams ARexx-RULE.md
   (return codes 0/5/10/20, no requester holds up a command): the app's own commands (VERSION, FAULT, QUIT
   without FORCE, ZOOM, FIT, TOOL, SELECT, GETSELECTION), then a diagram drawn, joined, moved, undone, saved,
   opened again and exported, the steps tests/diag/test_odg_rexx.c runs on the PC. The window redraws after
   each command, so watching it shows the canvas drawing too.
   rx tests/arexx/diag.rexx [DIR]   (GFOpenDiag running; DIR for the files, RAM: when not given;
   prints PASS/FAIL lines, then ALL PASS or n FAILED) */
OPTIONS RESULTS
PARSE ARG dir
IF dir = '' THEN dir = 'RAM:'
port = 'GFOPENDIAG'
IF ~SHOW('P', port) THEN DO
    SAY 'FAIL' port 'is not there: start GFOpenDiag first'
    EXIT 20
END
ADDRESS VALUE port
fails = 0

CALL want 'VERSION', 0
SAY '     VERSION:' res
CALL want 'HELP', 0
IF POS('GETSELECTION', res) = 0 THEN CALL bad 'HELP does not list GETSELECTION'
CALL want 'FAULT 205', 0
CALL want 'FAULT', 10
CALL want 'DEACTIVATE', 5
CALL want 'PRINT', 5
CALL want 'NOSUCHCOMMAND', 20
CALL want 'LASTERROR', 0
IF res = '' THEN CALL bad 'LASTERROR after an unknown command is empty'
CALL want 'ACTIVATE', 0

/* Two shapes and a connector between them. */
CALL want 'NEW FORCE', 0
CALL want 'ADDSHAPE 40 40 120 60 STYLE "rounded=1;whiteSpace=wrap;html=1;fillColor=#dae8fc;" TEXT Start', 0
a = res
CALL want 'ADDSHAPE 320 40 100 80 STYLE "ellipse;whiteSpace=wrap;html=1;" TEXT "End here"', 0
b = res
CALL want 'ADDSHAPE 180 200 100 100 STYLE "rhombus;whiteSpace=wrap;html=1;" TEXT Choose', 0
c = res
CALL want 'CONNECT' a b 'STYLE "edgeStyle=orthogonalEdgeStyle;html=1;"', 0
e = res
CALL want 'CONNECT' a c 'STYLE "edgeStyle=orthogonalEdgeStyle;dashed=1;html=1;"', 0
CALL want 'CELLS', 0
IF WORDS(res) ~= 5 THEN CALL bad 'CELLS =' res '(wanted 5 ids)'
CALL want 'GETBOX' b, 0
CALL same 'GETBOX' b, res, '320 40 100 80'
CALL want 'GETVALUE' b, 0
CALL same 'GETVALUE' b, res, 'End here'
CALL want 'HIT 100 70', 0
CALL same 'HIT 100 70', res, a

/* The canvas: zoom, fit, tools, selection. */
CALL want 'ZOOM 200', 0
CALL want 'ZOOM', 0
CALL same 'ZOOM', res, 200
CALL want 'ZOOM 5', 10
CALL want 'ZOOM 900', 10
CALL want 'FIT', 0
CALL want 'ZOOM', 0
IF res > 100 THEN CALL bad 'FIT zoomed past 100% (' res ')'
CALL want 'TOOL Ellipse', 0
CALL want 'TOOL', 0
CALL same 'TOOL', res, 'Ellipse'
CALL want 'TOOL Spline', 10
CALL want 'TOOL Select', 0
CALL want 'SELECT' a','b, 0
CALL want 'GETSELECTION', 0
CALL same 'GETSELECTION', res, a','b
CALL want 'SELECT nosuchid', 10
CALL want 'GETSELECTION', 0
CALL same 'GETSELECTION after a bad SELECT', res, ''
CALL want 'SELECT' e, 0
CALL want 'DELETE' e, 0
CALL want 'GETSELECTION', 0
CALL same 'GETSELECTION after DELETE', res, ''
CALL want 'UNDO', 0
CALL want 'CELLS', 0
IF WORDS(res) ~= 5 THEN CALL bad 'CELLS after UNDO =' res '(wanted 5 ids)'

/* Edits: move, resize, label, style; Undo and Redo. */
CALL want 'MOVE' b '0 -10', 0
CALL want 'GETBOX' b, 0
CALL same 'GETBOX after MOVE', res, '320 30 100 80'
CALL want 'RESIZE' b '320 30 140 80', 0
CALL want 'GETBOX' b, 0
CALL same 'GETBOX after RESIZE', res, '320 30 140 80'
CALL want 'UNDO', 0
CALL want 'GETBOX' b, 0
CALL same 'GETBOX after UNDO', res, '320 30 100 80'
CALL want 'REDO', 0
CALL want 'SETVALUE' a 'Begin', 0
CALL want 'STYLE' a 'fillColor #f8cecc', 0
CALL want 'GETSTYLE' a 'fillColor', 0
CALL same 'GETSTYLE fillColor', res, '#f8cecc'
CALL want 'GRID 10', 0
CALL want 'MOVE nosuch 1 1', 10

/* Files: a changed diagram won't QUIT or OPEN without FORCE; save, open again, export. */
CALL want 'ISDIRTY', 0
CALL same 'ISDIRTY', res, 1
CALL want 'QUIT', 5
CALL want 'SAVEAS "'dir'lab-diag.drawio"', 0
CALL want 'ISDIRTY', 0
CALL same 'ISDIRTY after SAVEAS', res, 0
CALL want 'SETVALUE' a 'Changed', 0
CALL want 'OPEN "'dir'lab-diag.drawio"', 5
CALL want 'OPEN "'dir'lab-diag.drawio" FORCE', 0
CALL want 'GETVALUE' a, 0
CALL same 'GETVALUE after OPEN', res, 'Begin'
CALL want 'SAVE COMPRESSED', 0
CALL want 'OPEN "'dir'lab-diag.drawio" FORCE', 0
CALL want 'GETBOX' b, 0
CALL same 'GETBOX after a compressed SAVE', res, '320 30 140 80'
CALL want 'EXPORT "'dir'lab-diag.svg"', 0
CALL want 'OPEN "'dir'lab-diag.svg"', 0
CALL want 'GETVALUE' b, 0
CALL same 'GETVALUE from the SVG', res, 'End here'
CALL want 'ADDPAGE Second', 0
CALL want 'PAGE 2', 0
CALL want 'CELLS', 0
CALL same 'CELLS on page 2', res, ''
CALL want 'PAGE 1', 0
CALL want 'PAGE 3', 10
CALL want 'OPEN "'dir'no such diagram.drawio" FORCE', 10
CALL want 'NEW FORCE', 0

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
