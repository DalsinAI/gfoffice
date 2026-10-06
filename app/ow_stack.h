#ifndef OW_STACK_H
#define OW_STACK_H
int ow_run_with_stack(unsigned long bytes, int (*fn)(int argc, char **argv), int argc, char **argv);
#endif
