#include "ow_autosave.h"

#include <exec/io.h>
#include <exec/ports.h>
#include <devices/timer.h>
#include <proto/exec.h>

#include <stdlib.h>

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
