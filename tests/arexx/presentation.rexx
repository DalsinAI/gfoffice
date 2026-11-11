/* tests/arexx/presentation.rexx: GFOpenPresentation's ARexx port against amicradle-teams ARexx-RULE.md
   (return codes 0/5/10/20, no requester holds up a command): open, edit, save, export, print, reopen.
   rx tests/arexx/presentation.rexx   (GFOpenPresentation running; prints PASS/FAIL lines, then ALL PASS or n FAILED) */
OPTIONS RESULTS
port = 'GFOPENPRESENTATION'
IF ~SHOW('P', port) THEN DO
    SAY 'FAIL' port 'is not there: start GFOpenPresentation first'
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
ELSE SAY '     LASTERROR:' res
CALL want 'OPEN', 10
CALL want 'SLIDES', 0
CALL same 'SLIDES', res, 1
CALL want 'OBJECTS', 0
SAY '     OBJECTS:' res
IF WORDS(res) < 2 THEN CALL bad 'the new title slide has no object to type in'
t = WORD(res, 2)
CALL want 'SETTEXT' t 'Hello from ARexx', 0
CALL want 'GETTEXT' t, 0
CALL same 'GETTEXT' t, res, 'Hello from ARexx'
CALL want 'ISDIRTY', 0
CALL same 'ISDIRTY', res, 1
CALL want 'SETTEXT 99 x', 10                   /* no such object */
CALL want 'NEWSLIDE', 0
CALL want 'SLIDES', 0
CALL same 'SLIDES after NEWSLIDE', res, 2
CALL want 'OBJECTS', 0
IF WORDS(res) >= 2 THEN DO
    b = WORD(res, 2)
    CALL want 'SETTEXT' b 'Second slide', 0
    CALL want 'UNDO', 0
    CALL want 'GETTEXT' b, 0
    CALL same 'GETTEXT after UNDO', res, ''
    CALL want 'REDO', 0
    CALL want 'GETTEXT' b, 0
    CALL same 'GETTEXT after REDO', res, 'Second slide'
END
CALL want 'SLIDE 9', 10
CALL want 'SLIDE 1', 0
CALL want 'SLIDE', 0
CALL same 'SLIDE', res, 1
CALL want 'OPEN RAM:x.odp', 5                  /* unsaved changes kept, no Lose Them? requester */
CALL want 'QUIT', 5                            /* the same */
CALL want 'SAVE', 5                            /* never saved: needs SAVEAS */
CALL want 'SAVEAS RAM:no/such/drawer/x.odp', 10  /* no requester: LASTERROR says why */
CALL want 'LASTERROR', 0
SAY '     LASTERROR:' res
CALL want 'SAVEAS RAM:arexx-test.odp', 0
CALL want 'ISDIRTY', 0
CALL same 'ISDIRTY after SAVEAS', res, 0
CALL want 'GETFILE', 0
CALL same 'GETFILE', res, 'RAM:arexx-test.odp'
CALL want 'SAVE', 0
CALL want 'EXPORT RAM:arexx-test.pdf', 0
IF ~EXISTS('RAM:arexx-test.pdf') THEN CALL bad 'EXPORT wrote no RAM:arexx-test.pdf'
'PRINT'                                        /* 0 with C:GFOpenPrint, 5 without it */
IF rc = 0 | rc = 5 THEN SAY 'PASS PRINT RC' rc
ELSE CALL bad 'PRINT RC' rc '(wanted 0 or 5)'
CALL want 'OPEN RAM:no/such/file.odp', 10
CALL want 'LASTERROR', 0
SAY '     LASTERROR:' res
CALL want 'OPEN RAM:arexx-test.odp', 0
CALL want 'SLIDES', 0
CALL same 'SLIDES after reopening', res, 2
CALL want 'SLIDE 1', 0
CALL want 'GETTEXT' t, 0
CALL same 'GETTEXT after reopening', res, 'Hello from ARexx'
CALL want 'DELETESLIDE', 0
CALL want 'DELETESLIDE', 5                     /* the last slide stays */
CALL want 'DEACTIVATE', 5                      /* no icon to go to */
CALL want 'ACTIVATE', 0
CALL want 'QUIT', 5
CALL want 'QUIT FORCE', 0

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
