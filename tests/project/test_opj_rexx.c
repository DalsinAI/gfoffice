/*
 * GFOpenProject's ARexx commands on the PC (src/project/opj_rexx.c): each command run as a script would
 * send it, its return code, its result and LASTERROR, and the plan it leaves. The plan starts on Monday 16
 * November 2026 at 08:00 on the Standard calendar (Monday to Friday, 08:00-12:00 and 13:00-17:00). Files
 * go to build/host/rexx-<pid>/build/host. Run under ASan and UBSan by make check.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
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
    CHECK(strstr(ok("HELP"), "VERSION ACTIVATE DEACTIVATE FAULT PRINT VIEW QUIT HELP LASTERROR") == out);
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
    CHECK_S(ok("GETRESOURCE 1 Calendar"), "Standard");                      /* Base Calendar: the plan's */
    CHECK_N(rx_do("SETRESOURCE 1 Calendar Nights"), OPJ_RX_ERROR);          /* no such calendar */
    ok("SETRESOURCE 1 \"Base Calendar\" 1");
    CHECK_S(ok("GETRESOURCE 1 basecalendar"), "Standard");
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
    CHECK_S(rx.lasterror, "EXPORT: FORMAT is MSPDI, CSV, GFPROJ or PDF, not Word");
    /* The printed Gantt view as a PDF: by a .pdf name, or FORMAT PDF whatever the name. */
    ok("EXPORT build/host/rexx-test.pdf");
    ok("EXPORT build/host/rexx-test.out FORMAT=pdf");
    f = fopen("build/host/rexx-test.out", "rb");
    CHECK(f != NULL);
    if (f) {
        CHECK(fgets(line, sizeof line, f) && strncmp(line, "%PDF-", 5) == 0);
        fclose(f);
    }
    f = fopen("build/host/rexx-test.pdf", "rb");
    CHECK(f != NULL);
    if (f) {
        CHECK(fgets(line, sizeof line, f) && strncmp(line, "%PDF-", 5) == 0);
        fclose(f);
    }
    CHECK_N(rx_do("SAVEAS build/host/rexx-test-saved.pdf"), OPJ_RX_OK);  /* SAVEAS never writes a PDF */
    f = fopen("build/host/rexx-test-saved.pdf", "rb");
    CHECK(f != NULL);
    if (f) {
        CHECK(fgets(line, sizeof line, f) && strncmp(line, "%PDF-", 5) != 0);
        fclose(f);
    }
    ok("SAVEAS build/host/rexx-test.gfproj");
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
    {
        unsigned long docs = rx.docs;                                       /* the app's view follows this */
        CHECK_N(rx_do("OPEN build/host/rexx-test.junk"), OPJ_RX_ERROR);
        CHECK_S(ok("GETCELL 1 Name"), "Saved");                             /* still the plan it had */
        CHECK_N(rx.docs, docs);                                             /* a failed OPEN keeps the doc */
        ok("OPEN build/host/rexx-test.gfproj");
        CHECK_N(rx.docs, docs + 1);                                         /* even if the new doc reuses the address */
    }

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
    {
        unsigned long docs = rx.docs;
        ok("NEW FORCE");
        CHECK_N(rx.docs, docs + 1);
    }
    CHECK_N(rx.doc->plan->start, AT(23, 8, 0));                            /* the plan's start kept */
    CHECK_N(rx_do("DELETETASK 1"), OPJ_RX_ERROR);                          /* nothing there */
    CHECK_N(rx_do("EXPORT build/host/rexx-empty.pdf"), OPJ_RX_NOW);         /* no tasks: nothing to print */
    CHECK_S(rx.lasterror, "The plan has no tasks to print");
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

    /* A word that fills the argument text leaves no room for a /F rest: refused, quoted or not. */
    {
        static char line[1100];
        size_t k;
        for (k = 0; k < 2; k++) {
            memcpy(line, "SETCELL 1 ", 10);
            memset(line + 10, 'c', 1021);
            strcpy(line + 10 + 1021, k ? " \"hello\"" : " hello");
            CHECK_N(rx_do(line), OPJ_RX_ERROR);
            CHECK_S(rx.lasterror, "SETCELL: The arguments are too long");
        }
    }
}

/* The plain and the ASan builds run side by side under make -j: each writes its files in a folder of its
 * own (build/host/rexx-<pid>/build/host), so neither reads a plan the other is halfway through writing. */
static int own_folder(void)
{
    char dir[64];
    snprintf(dir, sizeof dir, "build/host/rexx-%ld", (long)getpid());
    if (mkdir(dir, 0777) != 0 || chdir(dir) != 0) return 0;
    return mkdir("build", 0777) == 0 && mkdir("build/host", 0777) == 0;
}

/* COLUMN, the column chooser: a column by title or alias, shown, hidden, and the table keeping one. */
static void test_columns(void)
{
    opj_view v;
    char line[96];
    int c;
    rx.view = NULL;
    CHECK_N(rx_do("COLUMN Resources"), OPJ_RX_NOW);                      /* no window */
    CHECK(strstr(rx.lasterror, "no window") != NULL);
    opj_view_init(&v, rx.doc, 800, 400, 16, OPJ_ZOOM_DAYS);
    rx.view = &v;
    CHECK_S(ok("COLUMN \"Resource Names\""), "0");
    CHECK_S(ok("COLUMN Resources SHOW"), "1");
    CHECK_N(v.columns[OPJ_COL_RESOURCES], 140);
    CHECK_S(ok("COLUMN resourcenames"), "1");
    CHECK_S(ok("COLUMN Duration HIDE"), "0");
    CHECK_N(v.columns[OPJ_COL_DURATION], 0);
    CHECK_S(ok("COLUMN percent SHOW"), "1");
    CHECK_N(rx_do("COLUMN Colour SHOW"), OPJ_RX_ERROR);
    CHECK(strstr(rx.lasterror, "no column Colour") != NULL);
    CHECK_N(rx_do("COLUMN WBS SHOW HIDE"), OPJ_RX_ERROR);
    CHECK_N(rx_do("COLUMN"), OPJ_RX_ERROR);                              /* COLUMN/A */
    for (c = 0; c < OPJ_NCOLS; c++)
        if (c != OPJ_COL_NAME) opj_view_show_column(&v, c, 0);
    CHECK_N(rx_do("COLUMN Name HIDE"), OPJ_RX_NOW);                      /* the last one shown */
    CHECK_S(ok("COLUMN Name"), "1");
    CHECK(strstr(ok("HELP COLUMN"), "COLUMN/A,SHOW/S,HIDE/S") != NULL);
    ok("ADDTASK Review");                                                 /* the new columns through SETCELL */
    sprintf(line, "SETCELL %s Constraint FNET", ok("TASKS"));
    ok(line);
    sprintf(line, "GETCELL %s \"Constraint Type\"", ok("TASKS"));
    CHECK_S(ok(line), "Finish No Earlier Than");
    sprintf(line, "SETCELL %s Notes Ask Holly first", ok("TASKS"));
    ok(line);
    sprintf(line, "GETCELL %s Notes", ok("TASKS"));
    CHECK_S(ok(line), "Ask Holly first");
    CHECK_S(ok("COLUMN Notes SHOW"), "1");
    rx.view = NULL;
}

/* Calendars by ARexx: CALENDARS, ADDCALENDAR, WORKTIME (a weekday or days, set or read) and SETCALENDAR,
 * on a fresh plan from Mon 16 Nov 2026 with one 2-day task. */
static void test_calendars(void)
{
    opj_doc_free(rx.doc);
    rx.doc = opj_doc_new(NULL, AT(16, 8, 0));
    CHECK(rx.doc != NULL);
    if (!rx.doc) return;
    ok("ADDTASK Lay DURATION 2d");
    CHECK_S(ok("CALENDARS"), "1");
    CHECK_S(ok("WORKTIME Standard Mon"), "08:00-12:00, 13:00-17:00");
    CHECK_S(ok("WORKTIME 1 sunday"), "off");
    CHECK_S(ok("WORKTIME Standard 18/11/26"), "08:00-12:00, 13:00-17:00");
    ok("WORKTIME Standard Monday 08:00-12:00");
    CHECK_S(ok("GETCELL 1 Finish"), "Wed 18/11/26");
    CHECK_N(rx.doc->plan->tasks[0].early_finish, AT(18, 12, 0));
    ok("UNDO");
    ok("WORKTIME Standard 16/11/26 TO 17/11/26 off");                     /* two holidays */
    CHECK_S(ok("GETCELL 1 Start"), "Wed 18/11/26");
    CHECK_S(ok("WORKTIME Standard 17/11/26"), "off");
    ok("WORKTIME Standard 17/11/26 DEFAULT");
    CHECK_S(ok("WORKTIME Standard 17/11/26"), "08:00-12:00, 13:00-17:00");
    CHECK_S(ok("GETCELL 1 Start"), "Tue 17/11/26");     /* Mon 16 still a holiday */
    CHECK_N(rx_do("WORKTIME Standard Mon DEFAULT"), OPJ_RX_ERROR);
    CHECK(strstr(rx.lasterror, "for dates") != NULL);
    CHECK_N(rx_do("WORKTIME Standard 17/11/26 TO 18/11/26"), OPJ_RX_ERROR);
    CHECK_N(rx_do("WORKTIME Standard Someday 8-12"), OPJ_RX_ERROR);
    CHECK(strstr(rx.lasterror, "not a weekday or a date") != NULL);
    CHECK_N(rx_do("WORKTIME Nowhere Mon"), OPJ_RX_ERROR);
    CHECK_S(rx.lasterror, "WORKTIME: no calendar Nowhere");
    CHECK_N(rx_do("WORKTIME Standard Mon 12-8"), OPJ_RX_ERROR);
    /* A copy for nights; a name taken or a number is refused. */
    CHECK_S(ok("ADDCALENDAR Night FROM Standard"), "2");
    CHECK_N(rx_do("ADDCALENDAR night"), OPJ_RX_ERROR);
    CHECK_N(rx_do("ADDCALENDAR 7"), OPJ_RX_ERROR);
    CHECK_N(rx_do("ADDCALENDAR Odd FROM Nowhere"), OPJ_RX_ERROR);
    CHECK_S(ok("CALENDARS"), "2");
    ok("WORKTIME Night Mon 22:00-24:00");
    ok("SETCALENDAR Night ROW 1");
    CHECK_N(rx.doc->plan->tasks[0].early_start, AT(17, 8, 0));            /* the copy kept Mon 16 off */
    ok("WORKTIME Night 16/11/26 DEFAULT");
    CHECK_N(rx.doc->plan->tasks[0].early_start, AT(16, 22, 0));
    ok("SETCALENDAR \"\" ROW 1");                                      /* the plan's again */
    CHECK_N(rx.doc->plan->tasks[0].early_start, AT(17, 8, 0));
    ok("SETCALENDAR Night");                                              /* the plan's calendar */
    CHECK_N(rx.doc->plan->calendar, 1);
    CHECK_N(rx_do("SETCALENDAR Night ROW 5"), OPJ_RX_ERROR);
    CHECK_N(rx_do("SETCALENDAR -"), OPJ_RX_ERROR);                       /* the plan needs one */
    CHECK(strstr(ok("HELP WORKTIME"), "CALENDAR/A,DAY/A,TO/K,HOURS/F") != NULL);
}

/* UPDATE: Update Project through a status date, the tasks changed; the tracking columns by short names. */
static void test_update(void)
{
    opj_doc_free(rx.doc);
    rx.doc = opj_doc_new(NULL, AT(16, 8, 0));
    CHECK(rx.doc != NULL);
    if (!rx.doc) return;
    ok("ADDTASK Dig DURATION 2d");
    ok("ADDTASK Fill DURATION 2d");
    ok("LINK 1");
    CHECK_S(ok("UPDATE 17/11/26"), "1");                                  /* 17:00 when no time is given */
    CHECK_S(ok("GETCELL 1 ActualFinish"), "Tue 17/11/26");
    CHECK_S(ok("GETCELL 1 Percent"), "100%");
    CHECK_S(ok("UPDATE \"18/11/26 12:00\""), "1");
    CHECK_S(ok("GETCELL 2 Percent"), "25%");
    CHECK_S(ok("GETCELL 2 ActualDuration"), "4h");
    CHECK_S(ok("GETCELL 2 Remaining"), "12h");
    ok("UNDO");
    CHECK_S(ok("UPDATE \"18/11/26 12:00\" WHOLE"), "1");
    CHECK_S(ok("GETCELL 2 Percent"), "0%");
    CHECK_S(ok("GETCELL 2 ActualStart"), "Wed 18/11/26");
    ok("SETCELL 2 ActualStart NA");
    CHECK_S(ok("GETCELL 2 \"Actual Start\""), "NA");
    CHECK_N(rx_do("UPDATE soon"), OPJ_RX_ERROR);
    CHECK_S(rx.lasterror, "UPDATE: soon is not a date");
    CHECK_N(rx_do("SETCELL 2 ActualDuration 1d"), OPJ_RX_ERROR);
}

/* Baselines 1 to 10: BASELINE NUMBER=n and BASELINES. */
static void test_baselines(void)
{
    opj_doc_free(rx.doc);
    rx.doc = opj_doc_new(NULL, AT(16, 8, 0));
    CHECK(rx.doc != NULL);
    if (!rx.doc) return;
    ok("ADDTASK Dig DURATION 2d");
    ok("ADDTASK Fill DURATION 2d");
    CHECK_S(ok("BASELINES"), "none");
    ok("BASELINE NUMBER=3");
    CHECK_S(ok("BASELINES"), "3");
    CHECK_S(ok("GETCELL 1 \"Baseline Start\""), "NA");                    /* Baseline 0 untouched */
    ok("BASELINE");
    ok("BASELINE 2 NUMBER 10");
    CHECK_S(ok("BASELINES"), "0 3 10");
    ok("BASELINE NUMBER=3 CLEAR");
    CHECK_S(ok("BASELINES"), "0 10");
    ok("UNDO");
    CHECK_S(ok("BASELINES"), "0 3 10");
    CHECK_N(rx_do("BASELINE NUMBER=11"), OPJ_RX_ERROR);
    CHECK_S(rx.lasterror, "BASELINE: NUMBER is 0 to 10");
    CHECK_N(rx_do("BASELINE NUMBER=-1"), OPJ_RX_ERROR);
    CHECK_S(ok("BASELINES"), "0 3 10");
}

/* REPLACE: Assign Resources' Replace. */
static void test_replace(void)
{
    opj_doc_free(rx.doc);
    rx.doc = opj_doc_new(NULL, AT(16, 8, 0));
    CHECK(rx.doc != NULL);
    if (!rx.doc) return;
    ok("ADDTASK Dig DURATION 2d");
    ok("ADDRESOURCE Dale");
    ok("ADDRESOURCE Bob");
    ok("ADDRESOURCE Ann");
    ok("ASSIGN 1 Dale UNITS=50");
    ok("REPLACE 1 Dale Bob");
    CHECK_S(ok("GETCELL 1 Resources"), "Bob[50%]");
    ok("REPLACE 1 Bob Ann UNITS=75%");
    CHECK_S(ok("GETCELL 1 Resources"), "Ann[75%]");
    CHECK_N(rx_do("REPLACE 1 Bob Dale"), OPJ_RX_ERROR);                    /* Bob isn't on it */
    CHECK_N(rx_do("REPLACE 1 Ann Nobody"), OPJ_RX_ERROR);
    CHECK_S(rx.lasterror, "REPLACE: no resource Nobody");
    CHECK_N(rx_do("REPLACE 1 Nobody Ann"), OPJ_RX_ERROR);
    CHECK_S(rx.lasterror, "REPLACE: no resource Nobody");
    CHECK_N(rx_do("REPLACE 1 Ann Dale UNITS=lots"), OPJ_RX_ERROR);
    ok("UNDO");
    CHECK_S(ok("GETCELL 1 Resources"), "Bob[50%]");
}

/* LEVEL and CLEARLEVELING. */
static void test_level(void)
{
    opj_doc_free(rx.doc);
    rx.doc = opj_doc_new(NULL, AT(16, 8, 0));
    CHECK(rx.doc != NULL);
    if (!rx.doc) return;
    ok("ADDTASK Dig DURATION 2d");
    ok("ADDTASK Fill DURATION 1d");
    ok("ADDRESOURCE Dale");
    ok("ASSIGN 1 Dale");
    ok("ASSIGN 2 Dale");
    CHECK_S(ok("LEVEL WITHINSLACK"), "0");                                 /* Fill's day of slack isn't two */
    CHECK_S(ok("GETCELL 2 Delay"), "0d");
    CHECK_S(ok("LEVEL"), "1");
    CHECK_S(ok("GETCELL 2 \"Leveling Delay\""), "2d");
    CHECK_S(ok("GETCELL 2 Start"), "Wed 18/11/26");
    ok("CLEARLEVELING");
    CHECK_S(ok("GETCELL 2 leveldelay"), "0d");
    ok("SETCELL 2 Delay 1d");
    CHECK_S(ok("GETCELL 2 Start"), "Tue 17/11/26");
    ok("UNDO");
    CHECK_S(ok("GETCELL 2 Start"), "Mon 16/11/26");
}

/* SORT, FIND and REPLACETEXT. */
static void test_sort_find(void)
{
    opj_doc_free(rx.doc);
    rx.doc = opj_doc_new(NULL, AT(16, 8, 0));
    CHECK(rx.doc != NULL);
    if (!rx.doc) return;
    ok("ADDTASK Pour DURATION 2d");
    ok("ADDTASK Dig DURATION 1d");
    ok("ADDTASK Fill DURATION 3d");
    ok("ADDRESOURCE Dale");
    ok("SORT Name");
    CHECK_S(ok("GETCELL 1 Name"), "Dig");
    CHECK_S(ok("GETCELL 3 Name"), "Pour");
    ok("SORT Duration DESC");
    CHECK_S(ok("GETCELL 1 Name"), "Fill");
    ok("SORT Start THEN Name THENDESC");
    CHECK_S(ok("GETCELL 1 Name"), "Pour");
    CHECK_N(rx_do("SORT Colour"), OPJ_RX_ERROR);
    CHECK_S(rx.lasterror, "SORT: no column Colour");
    CHECK_S(ok("FIND i"), "2");                                            /* Fill */
    CHECK_S(ok("FIND i FROM 2"), "3");                                     /* Dig */
    CHECK_S(ok("FIND i FROM 2 BACK"), "3");                                /* round the top */
    CHECK_S(ok("FIND dig EXACT"), "3");
    CHECK_S(ok("FIND 3d COLUMN Duration"), "2");
    CHECK_S(ok("FIND da RESOURCES BEGINS"), "1");
    CHECK_N(rx_do("FIND nothing"), OPJ_RX_NOW);
    CHECK_S(rx.lasterror, "FIND: no nothing");
    CHECK_N(rx_do("FIND i COLUMN Colour"), OPJ_RX_ERROR);
    CHECK_S(ok("REPLACETEXT Name i o"), "2");
    CHECK_S(ok("GETCELL 2 Name"), "Foll");
    CHECK_S(ok("REPLACETEXT Name o i ROW 2"), "1");
    CHECK_S(ok("GETCELL 2 Name"), "Fill");
    CHECK_N(rx_do("REPLACETEXT Duration d x"), OPJ_RX_ERROR);
    CHECK_S(rx.lasterror, "REPLACETEXT: 3 cells wouldn't take it; nothing changed");
    ok("UNDO");
    CHECK_S(ok("GETCELL 2 Name"), "Foll");
}

/* FILTER, GROUP, LINES and LINE: the window's lines, worked out again after every command. */
static void test_filter_group(void)
{
    opj_lines lines;
    char want[80];
    opj_doc_free(rx.doc);
    rx.doc = opj_doc_new(NULL, AT(16, 8, 0));
    CHECK(rx.doc != NULL);
    if (!rx.doc) return;
    ok("ADDTASK Pour DURATION 2d");
    ok("ADDTASK Dig DURATION 1d");
    ok("ADDTASK Ship DURATION 0d");
    ok("ADDTASK Fill DURATION 2d");
    CHECK_N(rx_do("FILTER Milestones"), OPJ_RX_NOW);                       /* no window */
    CHECK_S(rx.lasterror, "FILTER: there is no window to filter or group");
    opj_lines_init(&lines);
    rx.lines = &lines;
    CHECK_S(ok("LINES"), "4");
    CHECK_S(ok("FILTER Milestones"), "1");
    CHECK_S(ok("LINES"), "1");
    CHECK_S(ok("LINE 1"), "3");
    CHECK_S(ok("FILTER AutoFilter COLUMN Name TEXT i"), "3");              /* Dig, Ship, Fill */
    CHECK_S(ok("FILTER AutoFilter COLUMN Name TEXT fill EXACT"), "1");
    CHECK_S(ok("FILTER AutoFilter COLUMN Name TEXT d BEGINS"), "1");
    CHECK_N(rx_do("FILTER AutoFilter"), OPJ_RX_ERROR);
    CHECK_S(rx.lasterror, "FILTER: AutoFilter needs a COLUMN");
    CHECK_N(rx_do("FILTER nope"), OPJ_RX_ERROR);
    CHECK_S(rx.lasterror, "FILTER: no filter nope");
    CHECK_S(ok("FILTER All"), "4");
    CHECK_S(ok("GROUP BY Duration"), "3");                                 /* 0, 1 and 2 days */
    CHECK_S(ok("LINES"), "7");
    snprintf(want, sizeof want, "Duration: %s", ok("GETCELL 3 Duration"));
    CHECK_S(ok("LINE 1"), want);
    CHECK_S(ok("LINE 2"), "3");
    CHECK_S(ok("GROUP BY Duration DESC"), "3");
    CHECK_S(ok("LINE 2"), "1");                                            /* 2 days: Pour, Fill */
    ok("SETCELL 2 Duration 2d");                                           /* the lines follow the edit */
    CHECK_S(ok("LINES"), "6");
    CHECK_N(rx_do("GROUP BY Colour"), OPJ_RX_ERROR);
    CHECK_N(rx_do("LINE 9"), OPJ_RX_ERROR);
    CHECK_S(rx.lasterror, "LINE: no line 9");
    CHECK_S(ok("GROUP"), "0");
    CHECK_S(ok("LINES"), "4");
    ok("FILTER Milestones SUMMARIES");                                     /* NEW starts on All Tasks, */
    ok("GROUP BY Duration DESC");                                          /* ungrouped */
    ok("SAVEAS build/host/rexx-lines.gfproj");
    ok("NEW FORCE");
    CHECK_N(lines.filter.kind, OPJ_FILTER_ALL);
    CHECK_N(lines.filter.summaries, 0);
    CHECK_N(lines.group, -1);
    CHECK_N(lines.group_descending, 0);
    CHECK_S(ok("LINES"), "0");
    ok("FILTER Milestones");
    ok("OPEN build/host/rexx-lines.gfproj FORCE");              /* and so does OPEN */
    CHECK_N(lines.filter.kind, OPJ_FILTER_ALL);
    CHECK_S(ok("LINES"), "4");
    remove("build/host/rexx-lines.gfproj");
    rx.lines = NULL;
    opj_lines_free(&lines);
}

/* UPDATETASK and PROGRESSLINES. */
static void test_update_task(void)
{
    opj_view v;
    opj_doc_free(rx.doc);
    rx.doc = opj_doc_new(NULL, AT(16, 8, 0));
    CHECK(rx.doc != NULL);
    if (!rx.doc) return;
    ok("ADDTASK Dig DURATION 4d");
    ok("UPDATETASK 1 ACTUAL 1d");
    CHECK_S(ok("GETCELL 1 Percent"), "25%");
    ok("UPDATETASK 1 REMAINING 1d");
    CHECK_S(ok("GETCELL 1 Duration"), "2d");
    ok("UPDATETASK 1 PERCENT 100 FINISH 18/11/26");
    CHECK_S(ok("GETCELL 1 ActualFinish"), "Wed 18/11/26");
    ok("UNDO");
    CHECK_S(ok("GETCELL 1 Percent"), "50%");
    CHECK_N(rx_do("UPDATETASK 1 ACTUAL soon"), OPJ_RX_ERROR);
    CHECK_N(rx_do("UPDATETASK 2 PERCENT 5"), OPJ_RX_ERROR);
    CHECK_S(ok("GETCELL 1 Work"), "0h");
    CHECK_S(ok("GETCELL 1 Type"), "Fixed Units");                             /* task types by column */
    ok("SETCELL 1 Type \"Fixed Work\"");
    CHECK_S(ok("GETCELL 1 EffortDriven"), "Yes");
    CHECK_N(rx_do("SETCELL 1 EffortDriven No"), OPJ_RX_ERROR);
    CHECK_N(rx_do("PROGRESSLINES ON"), OPJ_RX_NOW);                         /* no window */
    opj_view_init(&v, rx.doc, 800, 200, 16, OPJ_ZOOM_DAYS);
    rx.view = &v;
    CHECK_S(ok("PROGRESSLINES"), "0");
    CHECK_S(ok("PROGRESSLINES ON"), "1");
    CHECK(v.progress_lines);
    CHECK_S(ok("PROGRESSLINES OFF"), "0");
    CHECK_N(rx_do("PROGRESSLINES ON OFF"), OPJ_RX_ERROR);
    rx.view = NULL;
}

int main(void)
{
    if (!own_folder()) {
        printf("project ARexx tests: no folder of their own under build/host\n");
        return 1;
    }
    rx.doc = opj_doc_new(NULL, AT(16, 8, 0));
    CHECK(rx.doc != NULL);
    if (!rx.doc) return 1;
    test_words();
    test_tasks();
    test_resources_and_baseline();
    test_files();
    test_huge_numbers();
    test_columns();
    test_calendars();
    test_update();
    test_baselines();
    test_replace();
    test_level();
    test_sort_find();
    test_filter_group();
    test_update_task();
    if (!rx.doc) return 1;
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
