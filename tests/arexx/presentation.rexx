/* tests/arexx/presentation.rexx: GFOpenPresentation's ARexx port against amicradle-teams ARexx-RULE.md
   (return codes 0/5/10/20, no requester holds up a command): open, edit, save, export, print, reopen,
   the slide sorter's commands on several slides, speaker notes (notes pages, handouts) and the outline.
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
/* the slide sorter: pick several slides, move, copy, hide, delete, each one Undo step */
CALL want 'VIEW', 0
CALL same 'VIEW', res, 'NORMAL'
CALL want 'VIEW SORTER', 0
CALL want 'VIEW', 0
CALL same 'VIEW after VIEW SORTER', res, 'SORTER'
CALL want 'VIEW SIDEWAYS', 10
CALL want 'PICK 1', 0
CALL want 'PICKED', 0
CALL same 'PICKED', res, '1'
CALL want 'MOVEPICKED 3', 0                    /* slide 1 to after the last */
CALL want 'PICKED', 0
CALL same 'PICKED after MOVEPICKED', res, '2'
CALL want 'VIEW NORMAL', 0
CALL want 'SLIDE 2', 0
CALL want 'GETTEXT' t, 0
CALL same 'GETTEXT of the moved slide', res, 'Hello from ARexx'
CALL want 'VIEW SORTER', 0
CALL want 'UNDO', 0
CALL want 'VIEW NORMAL', 0
CALL want 'SLIDE 1', 0
CALL want 'GETTEXT' t, 0
CALL same 'GETTEXT after undoing the move', res, 'Hello from ARexx'
CALL want 'VIEW SORTER', 0
CALL want 'PICK ALL', 0
CALL same 'PICK ALL', res, 2
CALL want 'COPYPICKED', 0
CALL same 'COPYPICKED', res, 4
CALL want 'PICKED', 0
CALL same 'PICKED after COPYPICKED', res, '3 4'
CALL want 'UNDO', 0
CALL want 'SLIDES', 0
CALL same 'SLIDES after one UNDO of two copies', res, 2
CALL want 'PICK 2', 0
CALL want 'HIDEPICKED', 0
CALL want 'SHOWPICKED', 0
CALL want 'PICK 1 2', 0
CALL want 'DELETEPICKED', 5                    /* the last slide stays */
CALL want 'PICK 2', 0
CALL want 'DELETEPICKED', 0
CALL same 'DELETEPICKED', res, 1
CALL want 'UNDO', 0
CALL want 'SLIDES', 0
CALL same 'SLIDES after UNDO of DELETEPICKED', res, 2
CALL want 'PICK 0', 10
CALL want 'PICK', 10
CALL want 'MOVEPICKED 9', 10
CALL want 'VIEW NORMAL', 0
/* speaker notes: set, read, Undo, the Notes view, notes pages and handouts as PDF */
CALL want 'SLIDE 1', 0
CALL want 'GETNOTES', 0
CALL same 'GETNOTES of a new slide', res, ''
CALL want 'SETNOTES Say hello\nThen the demo', 0
CALL want 'GETNOTES', 0
CALL same 'GETNOTES after SETNOTES', res, 'Say hello\nThen the demo'
CALL want 'UNDO', 0
CALL want 'GETNOTES', 0
CALL same 'GETNOTES after UNDO', res, ''
CALL want 'REDO', 0
CALL want 'VIEW', 0
CALL same 'VIEW after undoing notes', res, 'NOTES'
CALL want 'VIEW NOTES', 0
CALL want 'GETNOTES', 0
CALL same 'GETNOTES after REDO', res, 'Say hello\nThen the demo'
CALL want 'EXPORT NOTES RAM:arexx-notes.pdf', 0
IF ~EXISTS('RAM:arexx-notes.pdf') THEN CALL bad 'EXPORT NOTES wrote no RAM:arexx-notes.pdf'
CALL want 'EXPORT HANDOUTS 4 RAM:arexx-handouts.pdf', 0
IF ~EXISTS('RAM:arexx-handouts.pdf') THEN CALL bad 'EXPORT HANDOUTS wrote no RAM:arexx-handouts.pdf'
CALL want 'EXPORT HANDOUTS 5 RAM:arexx-handouts.pdf', 10
CALL want 'SAVE', 0
CALL want 'OPEN RAM:arexx-test.odp', 0
CALL want 'SLIDE 1', 0
CALL want 'GETNOTES', 0
CALL same 'GETNOTES after saving and reopening', res, 'Say hello\nThen the demo'
/* the outline: rows, a title typed, a slide added, levels, each one Undo step */
CALL want 'VIEW OUTLINE', 0
CALL want 'VIEW', 0
CALL same 'VIEW after VIEW OUTLINE', res, 'OUTLINE'
CALL want 'OUTLINE', 0
rows = res
IF rows < 2 THEN CALL bad 'OUTLINE has' rows 'rows (wanted 2 or more)'
CALL want 'OUTLINE 1', 0
CALL same 'OUTLINE 1', res, '0 Hello from ARexx'
CALL want 'OUTLINE SET 1 Outline title', 0
CALL want 'OUTLINE 1', 0
CALL same 'OUTLINE 1 after OUTLINE SET', res, '0 Outline title'
CALL want 'GETTEXT' t, 0
CALL same 'GETTEXT after OUTLINE SET', res, 'Outline title'
CALL want 'UNDO', 0
CALL want 'OUTLINE 1', 0
CALL same 'OUTLINE 1 after UNDO', res, '0 Hello from ARexx'
CALL want 'OUTLINE INSERT 1', 0
CALL want 'SLIDES', 0
CALL same 'SLIDES after OUTLINE INSERT on a title', res, 3
CALL want 'UNDO', 0
CALL want 'SLIDES', 0
CALL same 'SLIDES after UNDO', res, 2
CALL want 'OUTLINE DEMOTE 1', 5                 /* the first title joins nothing */
CALL want 'OUTLINE PROMOTE 1', 5                /* a title goes no higher */
CALL want 'OUTLINE 99', 10
CALL want 'OUTLINE SET 0 x', 10
CALL want 'VIEW NORMAL', 0
CALL want 'SLIDE 1', 0
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
