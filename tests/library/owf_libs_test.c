/*
 * OWFLibsTest: GFOffice's four libraries from LIBS:, as programs use them
 * (docs/suite/owf-libraries.md): owfwrite.library, owfcalc.library,
 * owfpub.library and owfpresent.library, opened together.
 *
 *   OWFLibsTest [SAMPLES=dir] [FIXTURES=dir] [ONLY=write|calc|pub|present]
 *               (default PROGDIR:samples and PROGDIR:fixtures, as
 *               build-library.sh lays them out)
 *
 * For each library: the 17 calls all four share (memory, reports, what
 * files are, zip and inflate, the structures' sizes) through that library,
 * then its own (owf_libs_write.c, _calc.c, _pub.c, _present.c, each built
 * for its library). Then it all again from a second task at the same time.
 * Prints one line per check (the second task's as one line) and
 * "OWFLibsTest: N passed, M failed". With ONLY it opens and checks that
 * one library alone (what a program on it loads).
 * z.library (ours, openzlib) is optional: without it the libraries read
 * zip packages with their own inflate, and write them stored.
 *
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "owf_libs.h"

#include <exec/types.h>
#include <exec/tasks.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include <stdio.h>
#include <string.h>

static const owf_libs_unit *const units[] = { &owf_libs_write, &owf_libs_calc, &owf_libs_pub, &owf_libs_present };
#define NUNITS ((int)(sizeof units / sizeof units[0]))
static int use[NUNITS];

/* each task counts its own checks; only the first task prints, and it
 * prints the second task's results once that one has finished (libnix's
 * stdout is not for two tasks at once) */
struct results {
    int passed, failed, print;
    char first_failure[96];
};

static struct results main_results = { 0, 0, 1, "" };

static void check_in(void *ctx, int ok, const char *what)
{
    struct results *r = (struct results *)ctx;
    if (r->print) printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (ok) {
        r->passed++;
    } else {
        if (!r->failed) strncpy(r->first_failure, what, sizeof r->first_failure - 1);
        r->failed++;
    }
}

static const char *samples = "PROGDIR:samples";
static const char *fixtures = "PROGDIR:fixtures";

static void run_checks(struct results *res, const char *tag)
{
    char t[48];
    int i;
    for (i = 0; i < NUNITS; i++) {
        owf_libs_env env;
        if (!use[i])
            continue;
        snprintf(t, sizeof t, "%s %.*s", tag, (int)(strchr(units[i]->name, '.') - units[i]->name), units[i]->name);
        env.check = check_in;
        env.ctx = res;
        env.samples = samples;
        env.fixtures = fixtures;
        env.temp = "T:";
        env.tag = t;
        units[i]->run(&env);
    }
}

/* ---- a second task using the libraries at the same time ------------------ */

static struct Task *parent;
static volatile int child_done;
static struct results child_results = { 0, 0, 0, "" };

static void child_main(void)
{
    run_checks(&child_results, "task 2");
    Forbid();                       /* the parent may wake only once we are gone */
    child_done = 1;
    Signal(parent, SIGBREAKF_CTRL_F);
}

int main(void)
{
    static const char template[] = "SAMPLES,FIXTURES,ONLY/K";
    LONG args[3] = { 0, 0, 0 };
    struct RDArgs *rda = ReadArgs((CONST_STRPTR)template, args, NULL);
    struct Process *child;
    char id[128];
    int i, opened = 0, missing = 0;

    if (rda && args[0]) samples = (const char *)args[0];
    if (rda && args[1]) fixtures = (const char *)args[1];
    for (i = 0; i < NUNITS; i++) {
        const char *only = rda && args[2] ? (const char *)args[2] : NULL;
        size_t n = only ? strlen(only) : 0;
        use[i] = !only || (!strncmp(units[i]->name + 3, only, n) && units[i]->name[3 + n] == '.');
    }
    for (i = 0; i < NUNITS; i++) {
        if (!use[i])
            continue;
        if (!units[i]->open()) {
            printf("OWFLibsTest: %s %d: %s\n", units[i]->name, units[i]->version, units[i]->why());
            use[i] = 0;
            missing++;
            continue;
        }
        units[i]->opened(id, sizeof id);
        printf("OWFLibsTest: %s\n", id);
        opened++;
    }
    if (!opened || missing) {
        for (i = 0; i < NUNITS; i++)
            if (use[i]) units[i]->close();
        if (rda) FreeArgs(rda);
        return RETURN_FAIL;
    }

    run_checks(&main_results, "task 1");

    /* the same checks from a second process while this one runs them again */
    parent = FindTask(NULL);
    child_done = 0;
    SetSignal(0, SIGBREAKF_CTRL_F);
    child = CreateNewProcTags(NP_Entry, (ULONG)child_main, NP_Name, (ULONG)"OWFLibsTest task 2",
                              NP_StackSize, 32768, NP_Output, (ULONG)Output(),
                              NP_CloseOutput, FALSE, TAG_DONE);
    check_in(&main_results, child != NULL, "second task started");
    run_checks(&main_results, "task 1 again");
    if (child) {
        char what[160];
        while (!child_done) Wait(SIGBREAKF_CTRL_F);
        snprintf(what, sizeof what, "task 2, at the same time: %d passed, %d failed%s%s",
                 child_results.passed, child_results.failed,
                 child_results.failed ? ", first: " : "", child_results.first_failure);
        check_in(&main_results, child_results.failed == 0 && child_results.passed > 0, what);
    }

    printf("OWFLibsTest: %d passed, %d failed\n", main_results.passed, main_results.failed);
    for (i = 0; i < NUNITS; i++)
        if (use[i]) units[i]->close();
    if (rda) FreeArgs(rda);
    return main_results.failed ? RETURN_ERROR : RETURN_OK;
}
