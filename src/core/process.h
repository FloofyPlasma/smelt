#ifndef SMELT_PROCESS_H
#define SMELT_PROCESS_H

#include "core/stringvec.h"

typedef struct {
  StringVec argv;
  const char *cwd;
  int inherit_stdio;
} Process;

void process_free(Process *p);

int process_argv_push(Process *p, const char *arg);
int process_argv_extend(Process *p, const StringVec *v);

void process_print(const Process *p);

int process_run(Process *p);
int process_capture(Process *p, char *dst, size_t dstsz);

#endif
