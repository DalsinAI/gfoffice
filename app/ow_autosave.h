#ifndef OPENWRITE_AUTOSAVE_H
#define OPENWRITE_AUTOSAVE_H

#include <exec/types.h>

typedef struct ow_autosave ow_autosave;

ow_autosave *ow_autosave_open(unsigned seconds);
void ow_autosave_close(ow_autosave *timer);
ULONG ow_autosave_signal(const ow_autosave *timer);
/* Call when the signal is received. Returns 1 for a completed tick and
 * immediately schedules the next one. */
int ow_autosave_tick(ow_autosave *timer);

#endif
