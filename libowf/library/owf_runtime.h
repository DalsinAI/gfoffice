/*
 * owf.library's runtime (owf_runtime.c): its memory pool, made at the
 * library's start and gone at its expunge.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#ifndef OWF_RUNTIME_H
#define OWF_RUNTIME_H

int owf_rt_init(void);
void owf_rt_cleanup(void);

#endif
