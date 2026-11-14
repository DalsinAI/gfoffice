/* tests/arexx/project.rexx: GFOpenProject's ARexx port on the Amiga, against amicradle-teams ARexx-RULE.md
   (return codes 0/5/10/20, no requester holds up a command): the app's own commands (VERSION, FAULT,
   QUIT without FORCE), then a plan made, scheduled, baselined, saved, opened again and exported, the same
   steps tests/project/test_opj_rexx.c runs on the PC.
   rx tests/arexx/project.rexx [DIR]   (GFOpenProject running; DIR for the files, RAM: when not given;
   prints PASS/FAIL lines, then ALL PASS or n FAILED) */
OPTIONS RESULTS
PARSE ARG dir
IF dir = '' THEN dir = 'RAM:'
port = 'GFOPENPROJECT'
IF ~SHOW('P', port) THEN DO
    SAY 'FAIL' port 'is not there: start GFOpenProject first'
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

/* A plan from Monday 16 November 2026: Plan 2d, Build 3d after it, Done a milestone after that. */
CALL want 'NEW START 16/11/26 FORCE', 0
CALL want 'ADDTASK Plan DURATION=2d', 0
CALL same 'ADDTASK Plan', res, 1
CALL want 'ADDTASK Build DURATION=3d', 0
CALL want 'ADDTASK Done DURATION=0d', 0
CALL want 'LINK 1 COUNT 3', 0
CALL want 'TASKS', 0
CALL same 'TASKS', res, 3
CALL want 'GETCELL 2 Start', 0
CALL same 'GETCELL 2 Start', res, 'Wed 18/11/26'
CALL want 'FINISH', 0
CALL same 'FINISH', res, 'Fri 20/11/26 17:00'
CALL want 'CRITICAL', 0
CALL same 'CRITICAL', res, '1 2 3'
CALL want 'ADDRESOURCE Dale', 0
CALL want 'SETRESOURCE 1 rate 20/h', 0
CALL want 'ASSIGN 1 Dale', 0
CALL want 'GETCELL 1 Resources', 0
CALL same 'GETCELL 1 Resources', res, 'Dale'
CALL want 'GETRESOURCE 1 Cost', 0
CALL same 'GETRESOURCE 1 Cost', res, '320.00'
CALL want 'BASELINE', 0
CALL want 'SETCELL 1 Duration 3d', 0
CALL want 'GETCELL 3 "Finish Var."', 0
CALL same 'GETCELL 3 Finish Var.', res, '1d'
CALL want 'UNDO', 0
CALL want 'FINISH', 0
CALL same 'FINISH after UNDO', res, 'Fri 20/11/26 17:00'

/* Bad rows and numbers are refused, never wrapped (gfoffice #183). */
CALL want 'DELETETASK 2 COUNT 2147483647', 10
CALL want 'GETCELL 99 Name', 10

/* Files: save, a dirty plan won't QUIT or OPEN without FORCE, open it again, export. */
CALL want 'ISDIRTY', 0
CALL same 'ISDIRTY', res, 1
CALL want 'QUIT', 5
CALL want 'SAVEAS "'dir'lab-plan.gfproj"', 0
CALL want 'ISDIRTY', 0
CALL same 'ISDIRTY after SAVEAS', res, 0
CALL want 'SETCELL 1 Name Changed', 0
CALL want 'OPEN "'dir'lab-plan.gfproj"', 5
CALL want 'OPEN "'dir'lab-plan.gfproj" FORCE', 0
CALL want 'GETCELL 1 Name', 0
CALL same 'GETCELL 1 Name after OPEN', res, 'Plan'
CALL want 'GETCELL 1 "Baseline Start"', 0
CALL same 'GETCELL 1 Baseline Start', res, 'Mon 16/11/26'
CALL want 'EXPORT "'dir'lab-plan.xml"', 0
CALL want 'EXPORT "'dir'lab-plan.csv" FORMAT CSV', 0
CALL want 'OPEN "'dir'lab-plan.xml"', 0
CALL want 'FINISH', 0
CALL same 'FINISH from MSPDI', res, 'Fri 20/11/26 17:00'
CALL want 'OPEN "'dir'no such plan.gfproj"', 10

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
