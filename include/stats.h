#ifndef STATS_H
#define STATS_H

#include <stdint.h>

#include "worker.h"

extern uint64_t dropped;

void stats_print(struct flow_table* ft, struct worker_arg workers[], unsigned int num_workers);

#endif
