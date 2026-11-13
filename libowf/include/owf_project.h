/*
 * owf_project.h: GFOpenProject's plan model (tasks, links, constraints, working calendars) and its
 * critical path scheduler. Plain C99 with integer arithmetic only (no FPU), built on any host.
 * Design: amicradle-teams designs/GFOPENPROJECT-20261113.md (Dale's decisions, 13 Nov 2026).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 *
 * Times are minutes since 1 January 2000, 00:00 (local, no time zone). Durations, lags and slack are in
 * working minutes of the task's calendar unless the task is elapsed (every minute counts).
 */
#ifndef OWF_PROJECT_H
#define OWF_PROJECT_H

#include "owf.h"

typedef long owf_ptime;

/* 1 Jan 2000 to 31 Dec 4000; month 1..12, day 1..31 (not checked against the month). */
owf_ptime owf_ptime_make(int year, int month, int day, int hour, int minute);
void owf_ptime_split(owf_ptime t, int *year, int *month, int *day, int *hour, int *minute);
/* 0 Sunday .. 6 Saturday, as MSPDI's DayType 1..7 less one. */
int owf_ptime_weekday(owf_ptime t);

/* ---- Working calendars ---- */

#define OWF_PCAL_SPANS 5                 /* working spans in one day */

typedef struct {
    unsigned char nspans;
    unsigned short from[OWF_PCAL_SPANS], to[OWF_PCAL_SPANS];  /* minutes after midnight; from < to <= 1440 */
} owf_pday;

typedef struct {
    long day;                            /* days since 1 Jan 2000 */
    owf_pday hours;                      /* nspans 0: not a working day */
} owf_pexception;

typedef struct {
    char *name;
    owf_pday week[7];                    /* Sunday first */
    owf_pexception *exceptions;          /* sorted by day */
    int nexceptions, capexceptions;
} owf_pcal;

/* Monday to Friday, 08:00-12:00 and 13:00-17:00 (MS Project's Standard). */
void owf_pcal_standard(owf_pcal *cal);
/* Sets one weekday (0 Sunday) to nspans spans given as from,to pairs in minutes; 0 or an OWF_ERR_. */
int owf_pcal_set_week(owf_pcal *cal, int weekday, int nspans, const unsigned short *pairs);
/* A day off (nspans 0) or a day with its own hours; replaces an exception on that day. */
int owf_pcal_set_exception(owf_pcal *cal, long day, int nspans, const unsigned short *pairs);
void owf_pcal_free(owf_pcal *cal);
/* The working hours of the day holding t. */
const owf_pday *owf_pcal_day(const owf_pcal *cal, long day);
/* t moved forward by minutes of working time (backward when negative). Moving forward from the end of
 * a working span stays there when minutes is 0; owf_pcal_next_start gives the next span's start.
 * A calendar with no working time leaves t as it is. */
owf_ptime owf_pcal_add(const owf_pcal *cal, owf_ptime t, long minutes);
/* The working minutes from a to b (negative when b is before a). */
long owf_pcal_work(const owf_pcal *cal, owf_ptime a, owf_ptime b);
/* t, or the start of the next working span when t is not inside one. */
owf_ptime owf_pcal_next_start(const owf_pcal *cal, owf_ptime t);
/* t, or the end of the previous working span when t is not inside one or at its end. */
owf_ptime owf_pcal_prev_end(const owf_pcal *cal, owf_ptime t);

/* ---- Tasks and links ---- */

enum { OWF_LINK_FS, OWF_LINK_SS, OWF_LINK_FF, OWF_LINK_SF };
enum {
    OWF_CON_ASAP, OWF_CON_ALAP, OWF_CON_MSO, OWF_CON_MFO,   /* as soon / late as possible; must start / finish on */
    OWF_CON_SNET, OWF_CON_SNLT, OWF_CON_FNET, OWF_CON_FNLT  /* start / finish no earlier / later than */
};

typedef struct {
    int pred;                            /* index of the predecessor task */
    int type;                            /* OWF_LINK_ */
    long lag;                            /* working minutes (negative: a lead), or percent of pred's duration */
    int lag_percent;                     /* 1: lag is a percentage */
} owf_plink;

typedef struct {
    char *name;
    char *notes;
    int uid;                             /* kept through files; 0: given one on adding */
    int level;                           /* outline level, 1 = top; a task followed by a deeper one is a summary */
    long duration;                       /* working minutes; 0 with milestone set is a milestone */
    int elapsed;                         /* 1: duration counts every minute (MS Project's "ed") */
    int estimated;
    int milestone;
    int constraint;                      /* OWF_CON_ */
    owf_ptime constraint_date;
    int percent;                         /* complete, 0..100 */
    int calendar;                        /* index into the project's calendars, -1: the project's */
    owf_plink *links;                    /* predecessors */
    int nlinks, caplinks;
    /* worked out by owf_project_schedule */
    int summary;
    int parent;                          /* -1 at the top level */
    owf_ptime early_start, early_finish, late_start, late_finish;
    long total_slack, free_slack;
    int critical;
} owf_ptask;

typedef struct {
    char *title;
    owf_ptime start;                     /* the project's start */
    owf_ptime finish;                    /* worked out: the latest early finish */
    int calendar;                        /* the project's calendar */
    long minutes_per_day, minutes_per_week;  /* for showing durations in days and weeks: 480, 2400 */
    owf_ptask *tasks;
    int ntasks, captasks;
    owf_pcal *calendars;
    int ncalendars, capcalendars;
    int next_uid;
    int loop_task;                       /* after OWF_ERR_FORMAT from scheduling: a task in the loop */
} owf_project;

/* A new project starting at start, with the Standard calendar as its calendar. NULL: memory. */
owf_project *owf_project_new(owf_ptime start);
void owf_project_free(owf_project *p);
/* Adds a task at the end (level 1, one working day, ASAP); its index, or -1 for memory. */
int owf_project_add_task(owf_project *p, const char *name, long duration);
/* Adds a calendar (a copy of Standard named name); its index, or -1 for memory. */
int owf_project_add_calendar(owf_project *p, const char *name);
/* Makes pred a predecessor of succ; OWF_OK, OWF_ERR_FORMAT (out of range, itself) or OWF_ERR_MEMORY.
 * A second link between the same two tasks replaces the first. */
int owf_project_link(owf_project *p, int pred, int succ, int type, long lag, int lag_percent);
/* Removes the link from pred to succ, if there is one. */
void owf_project_unlink(owf_project *p, int pred, int succ);

/* Works out every task's dates, slack and criticality, and the project's finish (the critical path
 * method: a forward pass from the project's start, a backward pass from its finish).
 * - Links: FS, SS, FF and SF, with lags and leads in working time or as a percent of the predecessor.
 * - Constraints: MSO and MFO fix a date; SNET and FNET push later; SNLT and FNLT pull earlier (the
 *   constraint wins over links, as MS Project's default); ALAP tasks go to their late dates.
 * - Summary tasks span their subtasks; a summary's predecessors hold back each of its subtasks, and its
 *   successors wait for its finish.
 * - Total slack is late start less early start; a task is critical when its total slack is 0 or less.
 * OWF_OK, OWF_ERR_FORMAT when the links make a loop (loop_task names one task in it), or OWF_ERR_MEMORY. */
int owf_project_schedule(owf_project *p);

/* A deep copy (for Undo); NULL for memory. */
owf_project *owf_project_copy(const owf_project *p);
/* About how many bytes p holds, for an Undo budget. */
size_t owf_project_bytes(const owf_project *p);

/* ---- Files (owf_pfile.c) ----
 * .gfproj: GFOpenProject's own, a zip holding "mimetype" (stored first), content.xml and meta.xml;
 *   content.xml uses MSPDI's element names in GFOpenProject's namespace.
 * MSPDI: MS Project's XML (Microsoft's published Project XML Data Interchange schema).
 * CSV: the task table, a header row then one row per task, columns found by their header. */
enum { OWF_PFILE_GFPROJ, OWF_PFILE_MSPDI, OWF_PFILE_CSV };
#define OWF_PFILE_MIME "application/x-gfopenproject"
#define OWF_PFILE_NS "urn:amicradle:gfopenproject:1"

/* What data looks like: an OWF_PFILE_, or -1 when it is none of them (anything that is not a zip or
 * XML is taken for CSV). */
int owf_project_detect(const unsigned char *data, size_t length);
/* Reads a plan of any of the three into a new project (not scheduled). OWF_OK, OWF_ERR_FORMAT (not a
 * plan), OWF_ERR_CORRUPT (damaged), OWF_ERR_UNSUPPORTED or OWF_ERR_MEMORY. A CSV's project starts at
 * its earliest Start, or 1 Jan 2000 when it has none. */
int owf_project_read(const unsigned char *data, size_t length, owf_project **out);
/* Writes p as an OWF_PFILE_ into memory from owf_malloc (owf_free it), NUL after; OWF_OK or an
 * OWF_ERR_. The dates worked out by owf_project_schedule are written as they stand. */
int owf_project_write(const owf_project *p, int format, unsigned char **data, size_t *length);

/* Durations as text, as MS Project shows them: "5d", "2.5h", "3ed", "1w?" ("?": estimated; "e":
 * elapsed); a number alone is days. In minutes, with *elapsed and *estimated set; OWF_OK or
 * OWF_ERR_FORMAT. */
int owf_pduration_parse(const owf_project *p, const char *s, long *minutes, int *elapsed, int *estimated);
/* The other way: the largest unit that gives a whole number. out holds at least 24 bytes. */
void owf_pduration_text(const owf_project *p, long minutes, int elapsed, int estimated, char *out);
/* A row's predecessors as MS Project types them, "2,3SS+1d,4FF-2h,5SF+50%" (IDs are row numbers, from 1),
 * replacing its links. OWF_OK; OWF_ERR_FORMAT, the links left as they were, when any part isn't a
 * predecessor or names no row or the row itself; OWF_ERR_MEMORY. */
int owf_ppredecessors_parse(owf_project *p, int succ, const char *text);
/* The other way, cut to size. */
void owf_ppredecessors_text(const owf_project *p, int row, char *out, size_t size);

#endif
