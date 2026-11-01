#include "ow_autosave.h"

#include <exec/io.h>
#include <exec/ports.h>
#include <devices/timer.h>
#include <dos/dosextens.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct ow_autosave {
    struct MsgPort *port;
    struct timerequest *request;
    unsigned seconds;
    int opened, pending;
};

static void arm(ow_autosave *t)
{
    if (!t || !t->opened || t->pending) return;
    t->request->tr_node.io_Command = TR_ADDREQUEST;
    t->request->tr_time.tv_secs = t->seconds ? t->seconds : 60;
    t->request->tr_time.tv_micro = 0;
    SendIO((struct IORequest *)t->request);
    t->pending = 1;
}

ow_autosave *ow_autosave_open(unsigned seconds)
{
    ow_autosave *t = (ow_autosave *)calloc(1, sizeof(*t));
    if (!t) return NULL;
    t->seconds = seconds ? seconds : 60;
    t->port = CreateMsgPort();
    if (t->port)
        t->request = (struct timerequest *)CreateIORequest(t->port, sizeof(*t->request));
    if (!t->port || !t->request ||
        OpenDevice(TIMERNAME, UNIT_VBLANK, (struct IORequest *)t->request, 0)) {
        ow_autosave_close(t);
        return NULL;
    }
    t->opened = 1;
    arm(t);
    return t;
}

void ow_autosave_close(ow_autosave *t)
{
    if (!t) return;
    if (t->pending && t->request) {
        if (!CheckIO((struct IORequest *)t->request))
            AbortIO((struct IORequest *)t->request);
        WaitIO((struct IORequest *)t->request);
        t->pending = 0;
    }
    if (t->opened && t->request)
        CloseDevice((struct IORequest *)t->request);
    if (t->request) DeleteIORequest((struct IORequest *)t->request);
    if (t->port) DeleteMsgPort(t->port);
    free(t);
}

ULONG ow_autosave_signal(const ow_autosave *t)
{
    return t && t->port ? (1UL << t->port->mp_SigBit) : 0;
}

int ow_autosave_tick(ow_autosave *t)
{
    if (!t || !t->pending || !CheckIO((struct IORequest *)t->request))
        return 0;
    WaitIO((struct IORequest *)t->request);
    t->pending = 0;
    arm(t);
    return 1;
}

/* ---- The recovery copy (ow_autosave.h) ---- */

#define RECOVERY_DRAWER "PROGDIR:Recovery"

/* A lock on path, or 0 with *err saying why; no "Please insert volume" requester (PROGDIR: of a program made
 * resident has none). */
static BPTR quiet_lock(const char *path, LONG mode, LONG *err)
{
    struct Process *me = (struct Process *)FindTask(NULL);
    APTR was = me->pr_WindowPtr;
    BPTR l;
    me->pr_WindowPtr = (APTR)-1;
    l = Lock((STRPTR)path, mode);
    *err = l ? 0 : IoErr();
    me->pr_WindowPtr = was;
    return l;
}

static int name_free(const char *path)
{
    LONG err;
    BPTR l = quiet_lock(path, SHARED_LOCK, &err);
    if (l) UnLock(l);
    return !l && err == ERROR_OBJECT_NOT_FOUND;
}

void ow_recovery_init(ow_recovery *r, const char *program, const char *ext)
{
    const char *dir = RECOVERY_DRAWER "/";
    ULONG id = (ULONG)FindTask(NULL);
    LONG err;
    BPTR l;
    int i;
    memset(r, 0, sizeof *r);
    snprintf(r->program, sizeof r->program, "%s", program);
    snprintf(r->ext, sizeof r->ext, "%s", ext);
    if (!(l = quiet_lock(RECOVERY_DRAWER, SHARED_LOCK, &err))) {
        struct Process *me = (struct Process *)FindTask(NULL);
        APTR was = me->pr_WindowPtr;
        me->pr_WindowPtr = (APTR)-1;
        l = CreateDir((STRPTR)RECOVERY_DRAWER);
        me->pr_WindowPtr = was;
    }
    if (l) UnLock(l);
    else dir = "T:";                                   /* PROGDIR: not writable (a CD, a locked disk) */
    for (i = 0; i < 64; i++, id++) {                   /* never a name another copy, running or not, has */
        snprintf(r->name, sizeof r->name, "%s%s-%08lx.%s", dir, program, (unsigned long)id, ext);
        snprintf(r->meta, sizeof r->meta, "%s%s-%08lx.meta", dir, program, (unsigned long)id);
        if (name_free(r->name) && name_free(r->meta)) return;
    }
    r->name[0] = r->meta[0] = 0;                       /* none: no recovery copies this run */
}

int ow_recovery_due(const ow_recovery *r, int dirty, unsigned long changes)
{
    return r->name[0] && dirty && (!r->made || changes != r->changes);
}

int ow_recovery_begin(ow_recovery *r)
{
    if (r->name[0] && !r->meta_fh)
        r->meta_fh = Open((STRPTR)r->meta, MODE_NEWFILE);   /* exclusive: another copy sees it in use */
    return r->meta_fh != 0;
}

void ow_recovery_done(ow_recovery *r, const char *docname, unsigned long changes)
{
    if (r->meta_fh && Seek(r->meta_fh, 0, OFFSET_BEGINNING) >= 0) {
        Write(r->meta_fh, (APTR)docname, (LONG)strlen(docname));
        Write(r->meta_fh, (APTR)"\n", 1);
        SetFileSize(r->meta_fh, 0, OFFSET_CURRENT);
    }
    r->changes = changes;
    r->made = 1;
}

void ow_recovery_clear(ow_recovery *r)
{
    if (r->meta_fh) Close(r->meta_fh);
    r->meta_fh = 0;
    r->made = 0;
    if (!r->name[0]) return;                           /* (DeleteFile("") is the current drawer) */
    DeleteFile((STRPTR)r->name);
    DeleteFile((STRPTR)r->meta);
}

void ow_recovery_end(ow_recovery *r, int keep)
{
    if (keep && r->made) {
        if (r->meta_fh) Close(r->meta_fh);
        r->meta_fh = 0;
        return;
    }
    {
        struct Process *me = (struct Process *)FindTask(NULL);
        APTR was = me->pr_WindowPtr;
        ow_recovery_clear(r);
        me->pr_WindowPtr = (APTR)-1;
        DeleteFile((STRPTR)RECOVERY_DRAWER);           /* only when empty: no drawer left behind the program */
        me->pr_WindowPtr = was;
    }
}

int ow_recovery_find(const ow_recovery *r, char *out, size_t size)
{
    static const char *const dirs[2] = { RECOVERY_DRAWER "/", "T:" };
    struct FileInfoBlock *fib;
    size_t plen = strlen(r->program), elen = strlen(r->ext);
    int d, found = 0;
    if (!r->name[0] || !(fib = AllocDosObject(DOS_FIB, NULL))) return 0;
    for (d = 0; d < 2 && !found; d++) {
        char dir[24];
        LONG err;
        BPTR l;
        snprintf(dir, sizeof dir, "%s", dirs[d]);
        if (d == 0) dir[strlen(dir) - 1] = 0;          /* the drawer itself, without its "/" */
        if (!(l = quiet_lock(dir, SHARED_LOCK, &err))) continue;
        if (Examine(l, fib) && fib->fib_DirEntryType > 0)
            while (!found && ExNext(l, fib)) {
                const char *n = (const char *)fib->fib_FileName;
                size_t len = strlen(n);
                char meta[80];
                BPTR m;
                /* <program>-<eight characters>.<ext> */
                if (fib->fib_DirEntryType >= 0 || len != plen + 10 + elen || strncmp(n, r->program, plen) ||
                    n[plen] != '-' || n[plen + 9] != '.' || strcmp(n + plen + 10, r->ext))
                    continue;
                snprintf(meta, sizeof meta, "%s%.*s.meta", dirs[d], (int)(plen + 9), n);
                m = quiet_lock(meta, SHARED_LOCK, &err);
                if (m) UnLock(m);
                else if (err == ERROR_OBJECT_IN_USE) continue;      /* a running copy's */
                if ((size_t)snprintf(out, size, "%s%s", dirs[d], n) < size && strcmp(out, r->name))
                    found = 1;
            }
        UnLock(l);
    }
    FreeDosObject(DOS_FIB, fib);
    return found;
}

int ow_recovery_discard(const char *found)
{
    char meta[80];
    const char *dot = strrchr(found, '.');
    int gone = DeleteFile((STRPTR)found) != 0;
    if (dot && (size_t)(dot - found) + 6 <= sizeof meta) {
        snprintf(meta, sizeof meta, "%.*s.meta", (int)(dot - found), found);
        DeleteFile((STRPTR)meta);
    }
    return gone;
}
