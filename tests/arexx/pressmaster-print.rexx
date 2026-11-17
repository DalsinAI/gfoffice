/* tests/arexx/pressmaster-print.rexx: GFOpenPressmaster's PDF for a printer through its ARexx port (ARexx-RULE.md
   return codes 0/5/10/20, no requester holds up a command): EXPORT MARKS and BLEED n make each PDF page larger by
   the bleed and the crop marks' room, and refuse what cannot be.
   rx tests/arexx/pressmaster-print.rexx   (GFOpenPressmaster running with a publication open; writes RAM:pree-*.pdf;
   prints PASS/FAIL lines, then ALL PASS or n FAILED) */
OPTIONS RESULTS
port = 'GFOPENPRESSMASTER'
IF ~SHOW('P', port) THEN DO
    SAY 'FAIL' port 'is not there: start GFOpenPressmaster first'
    EXIT 20
END
ADDRESS VALUE port
fails = 0

CALL want 'EXPORT BLEED RAM:pree-x.pdf', 10                 /* no number */
CALL want 'EXPORT BLEED 99999 RAM:pree-x.pdf', 10           /* more than an inch */
CALL want 'EXPORT BLEED 12x RAM:pree-x.pdf', 10
CALL want 'EXPORT MARKS NOTES RAM:pree-x.pdf', 10           /* slides only */
CALL want 'EXPORT MARKS', 10                                /* no path */
CALL want 'EXPORT RAM:pree-plain.pdf', 0
CALL want 'EXPORT MARKS BLEED 170 RAM:pree-print.pdf', 0
CALL want 'EXPORT BLEED 170 RAM:pree-bleed.pdf', 0
plain = mediabox('RAM:pree-plain.pdf')
print = mediabox('RAM:pree-print.pdf')
bleed = mediabox('RAM:pree-bleed.pdf')
SAY '     MediaBox plain' plain', marks + bleed' print', bleed' bleed
IF plain = '' | print = '' | bleed = '' THEN CALL bad 'a PDF without a MediaBox'
ELSE DO
    /* points: marks + 3 mm bleed add 2 * (170 + 360 + 60) twips = 59 points, the bleed alone 17 */
    CALL same 'width with marks and bleed', FORMAT(WORD(print, 1) - WORD(plain, 1), , 0), 59
    CALL same 'height with marks and bleed', FORMAT(WORD(print, 2) - WORD(plain, 2), , 0), 59
    CALL same 'width with the bleed', FORMAT(WORD(bleed, 1) - WORD(plain, 1), , 0), 17
END
CALL want 'EXPORT RAM:pree-plain2.pdf', 0                   /* the next export is plain again */
CALL same 'a plain export after', mediabox('RAM:pree-plain2.pdf'), plain
ADDRESS COMMAND 'Delete RAM:pree-#?.pdf QUIET'

IF fails = 0 THEN SAY 'ALL PASS'
ELSE SAY fails 'FAILED'
EXIT fails > 0

/* The first page's MediaBox width and height in points, or '' */
mediabox: PROCEDURE
    PARSE ARG path
    IF ~OPEN('f', path, 'R') THEN RETURN ''
    text = ''
    DO WHILE ~EOF('f')
        text = text || READCH('f', 16384)
    END
    CALL CLOSE('f')
    at = POS('/MediaBox [0 0 ', text)
    IF at = 0 THEN RETURN ''
    PARSE VALUE SUBSTR(text, at + 15, 40) WITH w h ']' .
    RETURN w h

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
