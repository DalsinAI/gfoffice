/* tests/arexx/publisher-catalog.rexx: GFOpenPublisher's catalog merge through its ARexx port (ARexx-RULE.md
   return codes 0/5/10/20, no requester holds up a command): the first page's first text box gets a Name field
   and is the item, repeated twice down the page, one record each.
   rx tests/arexx/publisher-catalog.rexx csv   (a fresh GFOpenPublisher with a publication open whose first page
   has a text box; csv: a data source with a Name column and 2 records; prints PASS/FAIL lines, then ALL PASS
   or n FAILED) */
OPTIONS RESULTS
PARSE ARG csv
port = 'GFOPENPUBLISHER'
IF ~SHOW('P', port) THEN DO
    SAY 'FAIL' port 'is not there: start GFOpenPublisher first'
    EXIT 20
END
ADDRESS VALUE port
fails = 0

CALL want 'HELP', 0
IF POS('MERGECATALOG', res) = 0 THEN CALL bad 'HELP does not list MERGECATALOG'
CALL want 'MERGECATALOG 0 0 100 100 1 1', 5       /* no data source yet */
CALL want 'MERGESOURCE' csv, 0
CALL want 'PAGES', 0
pages = res
CALL want 'OBJECTS', 0
IF WORDS(res) < 2 THEN CALL bad 'the first page has no text box'
n = WORD(res, 1)
t = WORD(res, 2)
CALL want 'MERGEFIELD' t 'Name', 0
CALL want 'OBJECTBOX' t, 0
PARSE VAR res x y w h
SAY '     OBJECTBOX:' res
CALL want 'MERGECATALOG', 10                      /* no area */
CALL want 'MERGECATALOG' x y w, 10
CALL want 'MERGECATALOG' x y w h 1 2, 5            /* unsaved changes */
CALL want 'MERGECATALOG 2000000 2000000 10 10 1 1 FORCE', 5   /* nothing inside */
CALL want 'MERGECATALOG' x y w h 0 2 'FORCE', 10   /* no such area */
CALL want 'MERGECATALOG' x y w h 1 2 'FORCE', 0
CALL same 'MERGECATALOG pages made, pages', res, '1' pages
CALL want 'OBJECTS', 0
CALL same 'OBJECTS: one more item', WORD(res, 1), n + 1
CALL want 'GETTEXT' t, 0
SAY '     GETTEXT first item:' res
CALL want 'GETTEXT' n + 1, 0
SAY '     GETTEXT second item:' res
CALL want 'OBJECTBOX' n + 1, 0
PARSE VAR res x2 y2 .
CALL same 'the second item one box down', x2 y2, x (y + h)
CALL want 'MERGEFIELDS', 0
CALL same 'MERGEFIELDS after the merge', res, '0 0'
CALL want 'GETFILE', 0
CALL same 'GETFILE after the merge (untitled)', res, ''

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
