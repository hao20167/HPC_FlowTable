#ifndef STATS_H
#define STATS_H

#include "worker.h"

#include <stdint.h>

struct stats_arg {
  struct flow_table* ft;
  struct worker_arg* workers;
  unsigned int num_workers;
  volatile int* stop;
};

extern uint64_t dropped;

void* stats_thread_main(void* arg);
void stats_print(struct flow_table* ft, struct worker_arg workers[], unsigned int num_workers);

#endif
