#ifndef OPENWRITE_AUTOSAVE_H
#define OPENWRITE_AUTOSAVE_H

#include <exec/types.h>
#include <dos/dos.h>

#include <stddef.h>

typedef struct ow_autosave ow_autosave;

ow_autosave *ow_autosave_open(unsigned seconds);
void ow_autosave_close(ow_autosave *timer);
ULONG ow_autosave_signal(const ow_autosave *timer);
/* Call when the signal is received. Returns 1 for a completed tick and
 * immediately schedules the next one. */
int ow_autosave_tick(ow_autosave *timer);

/* ---- The recovery copy (1 November 2026) ----
 * One for each running copy of a program, on disk: in PROGDIR:Recovery (T: when that drawer can't be had), as
 * <program>-<eight hex digits>.<ext>, the task's address, with the document's own name in a .meta file beside
 * it. A running copy holds its .meta open from its first recovery copy on: that is how another copy, starting,
 * tells a running one's copy from one an interrupted session left. A copy is made only when the document has
 * changed since the last (its change count has moved). Before, every copy of OpenWrite shared one name in T:
 * (RAM, gone at a reboot) and wrote the whole document every minute, changed or not. */
typedef struct ow_recovery {
    char name[64];              /* the copy: PROGDIR:Recovery/OpenWrite-0012a4b8.odt; "" before ow_recovery_init */
    char meta[64];              /* beside it, the document's own name */
    char program[16], ext[8];
    BPTR meta_fh;               /* the .meta, held open while this copy has a recovery copy */
    unsigned long changes;      /* the change count the last copy was made at */
    int made;                   /* a copy made since the document was opened, saved or made new */
} ow_recovery;

/* This run's names (nothing written yet): program "OpenWrite", ext "odt". */
void ow_recovery_init(ow_recovery *r, const char *program, const char *ext);
/* 1 when a copy is wanted: the document has unsaved changes (dirty) and its change count has moved. */
int ow_recovery_due(const ow_recovery *r, int dirty, unsigned long changes);
/* Before r->name is written: the names claimed (the .meta held open). 0: no copy now. */
int ow_recovery_begin(ow_recovery *r);
/* After r->name was written: the document's own name in the .meta, and the count. */
void ow_recovery_done(ow_recovery *r, const char *docname, unsigned long changes);
/* This run's copy deleted (the document saved, opened or made new). */
void ow_recovery_clear(ow_recovery *r);
/* At the end: keep, the copy stays for the next start (unsaved changes); else it goes. */
void ow_recovery_end(ow_recovery *r, int keep);
/* A copy an interrupted session left (not a running copy's): its name in out, 1; else 0. OpenWrite and OpenCalc
 * up to 1 November 2026 named theirs <program>-Recovery in T:, which is found too. */
int ow_recovery_find(const ow_recovery *r, char *out, size_t size);
/* A found copy deleted, and its .meta: 1 when it went. */
int ow_recovery_discard(const char *found);

#endif
