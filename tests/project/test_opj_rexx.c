/*
 * GFOpenProject's ARexx commands on the PC (src/project/opj_rexx.c): each command run as a script would
 * send it, its return code, its result and LASTERROR, and the plan it leaves. The plan starts on Monday 16
 * November 2026 at 08:00 on the Standard calendar (Monday to Friday, 08:00-12:00 and 13:00-17:00). Files
 * go to build/host. Run under ASan and UBSan by make check.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <stdio.h>
#include <string.h>
#include "openproject_core.h"

static int fails, checks;

#define AT(d, h, m) owf_ptime_make(2026, 11, d, h, m)
#define CHECK(cond) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_N(got, want) do { long g_ = (long)(got), w_ = (long)(want); checks++; if (g_ != w_) { fails++; \
    printf("FAIL %s:%d: %s is %ld, want %ld\n", __FILE__, __LINE__, #got, g_, w_); } } while (0)
#define CHECK_S(got, want) do { const char *g_ = (got), *w_ = (want); checks++; if (strcmp(g_, w_) != 0) { \
    fails++; printf("FAIL %s:%d: %s is \"%s\", want \"%s\"\n", __FILE__, __LINE__, #got, g_, w_); } } while (0)

static opj_rexx rx;
static char out[1024];

/* A command's return code; its result in out. */
static int rx_do(const char *line)
{
    return opj_rexx_command(&rx, line, out, sizeof out);
}

/* A command that must work; its result. */
static const char *ok(const char *line)
{
    int rc = rx_do(line);
    checks++;
    if (rc != OPJ_RX_OK) {
        fails++;
        printf("FAIL %s gave %d: %s\n", line, rc, rx.lasterror);
    }
    return out;
}

static void test_words(void)
{
    CHECK(strstr(ok("HELP"), "VERSION ACTIVATE DEACTIVATE FAULT PRINT QUIT HELP LASTERROR") == out);
    CHECK(strstr(out, " SETCELL ") && strstr(out, " BASELINE ") && strstr(out, " CRITICAL"));
    CHECK_S(ok("help setcell"), "SETCELL ROW/A/N,COLUMN/A,TEXT/A/F");
    CHECK_N(rx_do("HELP NOTHING"), OPJ_RX_ERROR);
    CHECK_N(rx_do("FROB 1 2"), OPJ_RX_FAIL);
    CHECK_S(ok("LASTERROR"), "Unknown GFOpenProject ARexx command FROB; HELP lists them");
    CHECK_N(rx_do("TASKSX"), OPJ_RX_FAIL);                                   /* a whole word only */
    CHECK_N(rx_do("TASKS 3"), OPJ_RX_ERROR);
    CHECK(strstr(rx.lasterror, "Too many arguments"));
    CHECK_N(rx_do("GETCELL"), OPJ_RX_ERROR);
    CHECK_S(rx.lasterror, "GETCELL: ROW is needed");
    CHECK_N(rx_do("GETCELL ROW=one Name"), OPJ_RX_ERROR);
    CHECK_S(rx.lasterror, "GETCELL: ROW must be a number, not \"one\"");
    CHECK_N(rx_do("GETCELL ROW="), OPJ_RX_ERROR);
    CHECK_S(ok("TASKS"), "0");
}

static void test_tasks(void)
{
    /* Plan (2d), Build (3d), Ship (a milestone put in at row 3), linked one to the next. */
    CHECK_S(ok("ADDTASK Plan DURATION=2d"), "1");
    CHECK_S(ok("ADDTASK \"Build it\" DURATION 3d"), "2");
    CHECK_S(ok("ADDTASK Ship AT=3 DURATION=0d"), "3");
    CHECK_N(rx_do("ADDTASK Nope DURATION=soon"), OPJ_RX_ERROR);
    CHECK_N(rx_do("ADDTASK Nope AT=9"), OPJ_RX_ERROR);
    CHECK_S(ok("TASKS"), "3");                                               /* neither went in */
    ok("LINK 1 COUNT=3");
    CHECK_S(ok("GETCELL 2 Predecessors"), "1");
    CHECK_S(ok("GETCELL 3 predecessors"), "2");
    CHECK_S(ok("GETCELL 2 Start"), "Wed 18/11/26");
    CHECK_S(ok("GETCELL ROW 3 COLUMN Duration"), "0d");
    CHECK_S(ok("GETCELL COLUMN=Name ROW=2"), "Build it");
    CHECK_S(ok("FINISH"), "Fri 20/11/26 17:00");
    CHECK_S(ok("CRITICAL"), "1 2 3");

    /* Cells: the rest of the line is the text, quotes off when it is all one quoted word. */
    ok("SETCELL 1 Name Plan it, then dig");
    CHECK_S(ok("GETCELL 1 Name"), "Plan it, then dig");
    ok("SETCELL 1 Name \"Say *\"go*\"\"");
    CHECK_S(ok("GETCELL 1 \"Task Name\""), "Say \"go\"");
    ok("SETCELL 1 \"% Complete\" 50");
    CHECK_S(ok("GETCELL 1 percent"), "50%");
    CHECK_S(ok("GETCELL 1 WBS"), "1");
    CHECK_N(rx_do("SETCELL 9 Name X"), OPJ_RX_ERROR);
    CHECK_N(rx_do("SETCELL 1 Duration never"), OPJ_RX_ERROR);
    CHECK(strstr(rx.lasterror, "SETCELL: not possible"));
    CHECK_N(rx_do("GETCELL 1 Colour"), OPJ_RX_ERROR);
    CHECK_S(rx.lasterror, "GETCELL: no column Colour");
    CHECK_N(rx_do("SETCELL 2 Predecessors 3"), OPJ_RX_ERROR);                /* a loop */

    /* Outline: Build can't go under Plan, its predecessor (a loop); a new row under another can, and back. */
    CHECK_N(rx_do("INDENT 2"), OPJ_RX_ERROR);
    CHECK_S(ok("ADDTASK Extra"), "4");
    CHECK_S(ok("ADDTASK More"), "5");
    ok("INDENT 5");
    CHECK_S(ok("GETCELL 5 WBS"), "4.1");
    ok("OUTDENT 5");
    CHECK_S(ok("GETCELL 5 WBS"), "5");
    ok("DELETETASK 4 COUNT=2");
    CHECK_S(ok("TASKS"), "3");
    CHECK_S(ok("GETCELL 3 Finish"), "Fri 20/11/26");
}

static void test_resources_and_baseline(void)
{
    CHECK_S(ok("ADDRESOURCE Dale"), "1");
    CHECK_N(rx_do("ADDRESOURCE dale"), OPJ_RX_ERROR);
    ok("ASSIGN 1 Dale");
    ok("ASSIGN 2 1 UNITS=50");
    CHECK_N(rx_do("ASSIGN 1 Nobody"), OPJ_RX_ERROR);
    CHECK_S(rx.lasterror, "ASSIGN: no resource Nobody");
    CHECK_S(ok("GETCELL 2 Resources"), "Dale[50%]");
    ok("SETRESOURCE 1 \"Std. Rate\" 20/h");
    CHECK_S(ok("GETRESOURCE 1 rate"), "20.00/h");
    CHECK_S(ok("GETRESOURCE 1 Work"), "28h");                                /* 16h, and half of 24h */
    CHECK_S(ok("GETRESOURCE 1 Cost"), "560.00");
    CHECK_S(ok("GETRESOURCE 1 Name"), "Dale");
    CHECK_N(rx_do("GETRESOURCE 2 Name"), OPJ_RX_ERROR);
    CHECK_N(rx_do("SETRESOURCE 1 Work 3h"), OPJ_RX_ERROR);
    CHECK_S(ok("RESOURCES"), "1");
    CHECK_S(ok("USAGE Dale 16/11/26 17/11/26"), "8h");
    CHECK_S(ok("USAGE 1 FROM=18/11/2026 TO=21/11/2026"), "12h");
    CHECK_S(ok("USAGE Dale 23/11/26 24/11/26"), "0h");
    CHECK_N(rx_do("USAGE Dale 24/11/26 23/11/26"), OPJ_RX_ERROR);
    ok("ASSIGN 2 Dale UNITS=0");
    CHECK_S(ok("GETCELL 2 Resources"), "");

    /* Baseline, a slip, the variance; CLEAR; Undo and Redo. */
    ok("BASELINE");
    CHECK_S(ok("GETCELL 2 \"Baseline Start\""), "Wed 18/11/26");
    ok("SETCELL 1 Duration 3d");
    CHECK_S(ok("GETCELL 2 \"Start Var.\""), "1d");
    CHECK_S(ok("GETCELL 3 finishvariance"), "1d");
    ok("BASELINE 2 CLEAR");
    CHECK_S(ok("GETCELL 2 \"Baseline Start\""), "NA");
    CHECK_S(ok("GETCELL 3 \"Baseline Finish\""), "Fri 20/11/26");
    ok("UNDO");
    CHECK_S(ok("GETCELL 2 \"Baseline Start\""), "Wed 18/11/26");
    ok("REDO");
    CHECK_S(ok("GETCELL 2 \"Baseline Start\""), "NA");
    CHECK_N(rx_do("REDO"), OPJ_RX_NOW);
    CHECK_S(rx.lasterror, "REDO: nothing to redo");
    CHECK_N(rx_do("BASELINE 2 COUNT=7"), OPJ_RX_ERROR);
}

static void test_files(void)
{
    FILE *f;
    char line[64];
    opj_doc *was;
    CHECK_S(ok("ISDIRTY"), "1");
    CHECK_S(ok("GETFILE"), "");
    CHECK_N(rx_do("SAVE"), OPJ_RX_NOW);
    CHECK_S(rx.lasterror, "SAVE: the plan has no file yet (SAVEAS names one)");
    ok("SAVEAS build/host/rexx-test.gfproj");
    CHECK_S(ok("ISDIRTY"), "0");
    CHECK_S(ok("GETFILE"), "build/host/rexx-test.gfproj");
    ok("EXPORT build/host/rexx-test.xml");
    ok("EXPORT build/host/rexx-test.txt FORMAT=CSV");
    CHECK_N(rx_do("EXPORT build/host/rexx-test.doc FORMAT=Word"), OPJ_RX_ERROR);
    CHECK_N(rx_do("EXPORT /no/such/place/plan.xml"), OPJ_RX_ERROR);
    CHECK(strstr(rx.lasterror, "Could not write"));
    CHECK_S(ok("GETFILE"), "build/host/rexx-test.gfproj");                   /* EXPORT keeps the plan's file */
    f = fopen("build/host/rexx-test.txt", "rb");
    CHECK(f != NULL);
    if (f) {
        CHECK(fgets(line, sizeof line, f) && strncmp(line, "ID,Unique ID,Name", 17) == 0);
        fclose(f);
    }

    /* OPEN won't drop unsaved changes without FORCE. */
    ok("SETCELL 1 Name Changed");
    CHECK_N(rx_do("OPEN build/host/rexx-test.gfproj"), OPJ_RX_NOW);
    CHECK_S(ok("GETCELL 1 Name"), "Changed");
    was = rx.doc;
    ok("OPEN build/host/rexx-test.gfproj FORCE");
    CHECK(rx.doc != was);
    CHECK_S(ok("GETCELL 1 Name"), "Say \"go\"");
    CHECK_S(ok("TASKS"), "3");
    CHECK_S(ok("GETCELL 3 \"Baseline Finish\""), "Fri 20/11/26");          /* the baseline came too */
    CHECK_S(ok("GETRESOURCE 1 Cost"), "480.00");                           /* Plan's 3 days, Dale alone */
    CHECK_S(ok("ISDIRTY"), "0");
    ok("SETCELL 1 Name Saved");
    ok("SAVE");
    CHECK_S(ok("ISDIRTY"), "0");
    ok("OPEN build/host/rexx-test.xml");                                     /* MS Project XML */
    CHECK_S(ok("GETCELL 1 Name"), "Say \"go\"");
    CHECK_S(ok("GETFILE"), "build/host/rexx-test.xml");
    ok("OPEN build/host/rexx-test.gfproj");
    CHECK_S(ok("GETCELL 1 Name"), "Saved");
    CHECK_N(rx_do("OPEN build/host/no-such-plan.gfproj"), OPJ_RX_ERROR);
    CHECK_S(rx.lasterror, "OPEN: could not read build/host/no-such-plan.gfproj");
    f = fopen("build/host/rexx-test.junk", "wb");
    if (f) {
        fputs("\x01\x02 not a plan", f);
        fclose(f);
    }
    CHECK_N(rx_do("OPEN build/host/rexx-test.junk"), OPJ_RX_ERROR);
    CHECK_S(ok("GETCELL 1 Name"), "Saved");                                 /* still the plan it had */

    /* NEW: an empty plan from a date; the file is forgotten. */
    CHECK_N(rx_do("NEW START=someday"), OPJ_RX_ERROR);
    ok("NEW START=23/11/2026");
    CHECK_S(ok("TASKS"), "0");
    CHECK_S(ok("GETFILE"), "");
    CHECK_N(rx.doc->plan->start, AT(23, 8, 0));
    CHECK_S(ok("ADDTASK One DURATION=1d"), "1");
    CHECK_S(ok("ADDTASK Two"), "2");
    ok("DELETETASK 2");
    CHECK_S(ok("TASKS"), "1");
    CHECK_N(rx_do("NEW"), OPJ_RX_NOW);
    ok("NEW FORCE");
    CHECK_N(rx.doc->plan->start, AT(23, 8, 0));                            /* the plan's start kept */
    CHECK_N(rx_do("DELETETASK 1"), OPJ_RX_ERROR);                          /* nothing there */
}

/* A ROW or COUNT that overflows an int on the way in is refused, never wrapped into a negative size. */
static void test_huge_numbers(void)
{
    static const char *const lines[] = {
        "DELETETASK 2 COUNT 2147483647", "DELETETASK 2 COUNT 99999999999", "DELETETASK 2147483647",
        "DELETETASK -2147483648 COUNT 2", "INDENT 2 COUNT 2147483647", "OUTDENT 2 COUNT 2147483647",
        "LINK 2 COUNT 2147483647", "LINK 2147483647", "UNLINK 2 COUNT 2147483647",
        "BASELINE 2 COUNT 2147483647", "BASELINE CLEAR 2 COUNT 2147483647",
        "DELETERESOURCE 1 COUNT 2147483647", "DELETERESOURCE 2147483647 COUNT 2",
        "GETCELL 2147483647 Name", "SETCELL 99999999999 Name X", "ADDTASK X ROW 2147483647",
        "ASSIGN 2147483647 Ann", "GETRESOURCE 2147483647 Name",
    };
    char name[300];
    size_t i;
    ok("NEW FORCE");
    ok("ADDTASK A");
    ok("ADDTASK B");
    ok("ADDTASK C");
    ok("ADDRESOURCE Ann");
    ok("ADDRESOURCE Bob");
    for (i = 0; i < sizeof lines / sizeof *lines; i++) {
        int rc = rx_do(lines[i]);
        checks++;
        if (rc != OPJ_RX_ERROR) {
            fails++;
            printf("FAIL %s gave %d, want %d\n", lines[i], rc, OPJ_RX_ERROR);
        }
    }
    CHECK_S(ok("TASKS"), "3");
    CHECK_S(ok("RESOURCES"), "2");
    CHECK_N(rx.doc->plan->tasks[1].nlinks + rx.doc->plan->tasks[2].nlinks, 0);

    /* The same calls straight into the core. */
    CHECK_N(opj_delete_tasks(rx.doc, 1, 2147483647), OWF_ERR_FORMAT);
    CHECK_N(opj_indent(rx.doc, 1, 2147483647, 1), OWF_ERR_FORMAT);
    CHECK_N(opj_indent(rx.doc, 1, 2147483647, -1), OWF_ERR_FORMAT);
    CHECK_N(opj_link_rows(rx.doc, 1, 2147483647), OWF_ERR_FORMAT);
    CHECK_N(opj_unlink_rows(rx.doc, 1, 2147483647), OWF_ERR_FORMAT);
    CHECK_N(opj_delete_resources(rx.doc, 1, 2147483647), OWF_ERR_FORMAT);
    CHECK_N(opj_set_baseline(rx.doc, 1, 2147483647), OWF_ERR_FORMAT);
    CHECK_N(rx.doc->plan->ntasks, 3);
    ok("DELETETASK 2 COUNT 2");                                           /* the last rows exactly */
    CHECK_S(ok("TASKS"), "1");

    /* A file name that does not fit is refused, so SAVE never writes somewhere else. */
    memset(name, 'a', sizeof name - 1);
    name[sizeof name - 1] = 0;
    memcpy(name, "SAVEAS ", 7);
    CHECK_N(rx_do(name), OPJ_RX_ERROR);
    CHECK(strstr(rx.lasterror, "SAVEAS: the file name is longer than 255 bytes") != NULL);
    memcpy(name, "OPEN   ", 7);
    CHECK_N(rx_do(name), OPJ_RX_ERROR);
    CHECK(strstr(rx.lasterror, "OPEN: the file name is longer than 255 bytes") != NULL);
    CHECK_S(ok("TASKS"), "1");                                            /* nothing opened */
}

int main(void)
{
    rx.doc = opj_doc_new(NULL, AT(16, 8, 0));
    CHECK(rx.doc != NULL);
    if (!rx.doc) return 1;
    test_words();
    test_tasks();
    test_resources_and_baseline();
    test_files();
    test_huge_numbers();
    CHECK_N(opj_rexx_format("A.XML"), OWF_PFILE_MSPDI);
    CHECK_N(opj_rexx_format("plan.Csv"), OWF_PFILE_CSV);
    CHECK_N(opj_rexx_format("plan"), OWF_PFILE_GFPROJ);
    CHECK_N(opj_rexx_command(&rx, "TASKS", NULL, 0), OPJ_RX_OK);
    CHECK_N(opj_rexx_command(NULL, "TASKS", out, sizeof out), OPJ_RX_FAIL);
    opj_doc_free(rx.doc);
    if (fails) {
        printf("project ARexx tests: %d of %d checks failed\n", fails, checks);
        return 1;
    }
    printf("project ARexx tests passed (%d checks)\n", checks);
    return 0;
}
