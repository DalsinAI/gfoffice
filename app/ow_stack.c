/* Run OpenWrite's UI on a useful stack. MIT, Copyright (c) 2026 Dalsin Limited. */
#include "ow_stack.h"
#include <exec/memory.h>
#include <exec/tasks.h>
#include <proto/exec.h>

static struct StackSwapStruct swap;
static int (*run_fn)(int, char **);
static int run_argc, run_rc;
static char **run_argv;

static void __attribute__((noinline)) call_fn(void)
{
    run_rc = run_fn(run_argc, run_argv);
}

static void __attribute__((noinline)) run_on_new_stack(void)
{
#ifdef __AROS__
    struct StackSwapArgs none = { { 0 } };
    NewStackSwap(&swap, (APTR)call_fn, &none);
#else
    StackSwap(&swap);
    call_fn();
    StackSwap(&swap);
#endif
}

int ow_run_with_stack(unsigned long bytes, int (*fn)(int argc, char **argv), int argc, char **argv)
{
    struct Task *me = FindTask(NULL);
    APTR mem;
    if ((ULONG)me->tc_SPUpper - (ULONG)me->tc_SPLower >= bytes) return fn(argc, argv);
    mem = AllocVec(bytes, MEMF_ANY);
    if (!mem) return fn(argc, argv);
    swap.stk_Lower = mem;
#ifdef __AROS__
    swap.stk_Upper = (APTR)((UBYTE *)mem + bytes);
#else
    swap.stk_Upper = (ULONG)mem + bytes;
#endif
    swap.stk_Pointer = (APTR)swap.stk_Upper;
    run_fn = fn;
    run_argc = argc;
    run_argv = argv;
    run_on_new_stack();
    FreeVec(mem);
    return run_rc;
}
