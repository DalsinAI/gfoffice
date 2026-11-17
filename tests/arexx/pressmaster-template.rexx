/* tests/arexx/pressmaster-template.rexx: GFOpenPressmaster's New from Template through its ARexx port (ARexx-RULE.md
   return codes 0/5/10/20, no requester holds up a command): the template list, a new publication from a
   template, unsaved changes kept, and the Catalog Page template through Catalog Merge.
   rx tests/arexx/pressmaster-template.rexx csv   (GFOpenPressmaster running; csv: a data source with a Name column
   and 2 records; prints PASS/FAIL lines, then ALL PASS or n FAILED) */
OPTIONS RESULTS
PARSE ARG csv
port = 'GFOPENPRESSMASTER'
IF ~SHOW('P', port) THEN DO
    SAY 'FAIL' port 'is not there: start GFOpenPressmaster first'
    EXIT 20
END
ADDRESS VALUE port
fails = 0

CALL want 'HELP', 0
IF POS('NEWTEMPLATE', res) = 0 THEN CALL bad 'HELP does not list NEWTEMPLATE'
CALL want 'NEWTEMPLATE Flyer FORCE', 0              /* whatever was open goes */
CALL want 'NEWTEMPLATE', 0
IF POS('Flyer', res) = 0 | POS('Catalog Page', res) = 0 THEN CALL bad 'the list lacks Flyer or Catalog Page:' res
CALL want 'NEWTEMPLATE No Such Thing', 5
CALL want 'NEWTEMPLATE flyer', 0
CALL same 'NEWTEMPLATE flyer pages', res, 1
CALL want 'GETFILE', 0
CALL same 'GETFILE (untitled)', res, ''
CALL want 'NEWTEMPLATE "Folded Card"', 0
CALL same 'Folded Card pages', res, 4
CALL want 'PAGES', 0
CALL same 'PAGES', res, 4
CALL want 'NEWPAGE', 0                            /* now there are unsaved changes */
CALL want 'NEWTEMPLATE Newsletter', 5
CALL want 'PAGES', 0
CALL same 'PAGES kept', res, 5
CALL want 'NEWTEMPLATE Newsletter FORCE', 0
CALL same 'Newsletter pages', res, 2

/* the Catalog Page's first item through Catalog Merge */
CALL want 'NEWTEMPLATE Catalog Page', 0
CALL want 'MERGESOURCE' csv, 0
CALL want 'OBJECTS', 0
SAY '     OBJECTS:' res
n = WORD(res, 1)
CALL same 'Catalog Page objects', n, 5
CALL want 'MERGEFIELD 4 Name', 0
CALL want 'OBJECTBOX 2', 0
PARSE VAR res x y w h                             /* a group's box as drawn: its children's; the group's own is */
x = x - 300; y = y - 300; w = w + 600; h = h + 600  /* a little wider, so take a margin to have the group too */
CALL want 'MERGECATALOG' x y w h 2 1 'FORCE', 0
CALL same 'MERGECATALOG made, pages', res, '1 1'
CALL want 'OBJECTS', 0
CALL same 'objects after the merge', WORD(res, 1), n + 4
CALL want 'GETTEXT 4', 0
SAY '     GETTEXT first item:' res
CALL want 'MERGEFIELDS', 0
CALL same 'MERGEFIELDS after the merge', res, '0 0'

IF fails = 0 THEN SAY 'ALL PASS'
ELSE SAY fails 'FAILED'
EXIT fails > 0

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
