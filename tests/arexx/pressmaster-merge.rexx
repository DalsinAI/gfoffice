/* tests/arexx/pressmaster-merge.rexx: GFOpenPressmaster's mail merge through its ARexx port (ARexx-RULE.md return
   codes 0/5/10/20, no requester holds up a command): a data source, a merge field, records, merge to pages.
   rx tests/arexx/pressmaster-merge.rexx csv   (GFOpenPressmaster running with a publication open whose first page
   has a text box; csv: a data source with a Name column and 2 records; prints PASS/FAIL lines, then ALL PASS
   or n FAILED) */
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
IF POS('MERGESOURCE', res) = 0 THEN CALL bad 'HELP does not list MERGESOURCE'
CALL want 'MERGEFIELD 1 Name', 5                  /* no data source yet */
CALL want 'MERGEPAGES', 5
CALL want 'MERGESOURCE', 10
CALL want 'MERGESOURCE RAM:no-such-file.csv', 10
CALL want 'MERGESOURCE' csv, 0
SAY '     MERGESOURCE:' res
CALL want 'MERGERECORD', 0
CALL same 'MERGERECORD', res, '1 2'
CALL want 'OBJECTS', 0
IF WORDS(res) < 2 THEN CALL bad 'the first page has no text box'
t = WORD(res, 2)
CALL want 'MERGEFIELD' t, 10                      /* no column */
CALL want 'MERGEFIELD 999 Name', 10
CALL want 'MERGEFIELD' t 'Name', 0
CALL want 'MERGEFIELD' t 'NoSuchColumn', 0
CALL want 'MERGEFIELDS', 0
CALL same 'MERGEFIELDS', res, '2 1'
CALL want 'GETTEXT' t, 0
SAY '     GETTEXT:' res
CALL want 'MERGERECORD NEXT', 0
CALL same 'MERGERECORD NEXT', res, '2 2'
CALL want 'MERGERECORD NEXT', 0
CALL same 'MERGERECORD NEXT at the last', res, '2 2'
CALL want 'MERGERECORD 3', 5
CALL want 'MERGERECORD NAMES', 0
CALL same 'MERGERECORD NAMES', res, '0 2'
CALL want 'MERGERECORD PREVIOUS', 0
CALL same 'MERGERECORD PREVIOUS', res, '1 2'
CALL want 'MERGERECORD 0', 0
CALL same 'MERGERECORD 0 is the names', res, '0 2'
CALL want 'MERGERECORD 1', 0
CALL want 'MERGERECORD -1', 5
CALL want 'MERGERECORD -5', 5
CALL want 'MERGERECORD abc', 5
CALL want 'MERGERECORD 1x', 5
CALL want 'MERGERECORD', 0
CALL same 'MERGERECORD after the refusals', res, '1 2'
CALL want 'PAGES', 0
pages = res
CALL want 'MERGEPAGES', 5                         /* unsaved changes */
CALL want 'MERGEPAGES FORCE', 0
CALL same 'MERGEPAGES FORCE', res, pages * 2
CALL want 'PAGES', 0
CALL same 'PAGES after the merge', res, pages * 2
CALL want 'GETFILE', 0
CALL same 'GETFILE after the merge (untitled)', res, ''
CALL want 'MERGEFIELDS', 0
SAY '     MERGEFIELDS after the merge:' res

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
