#ifndef WORKER_H
#define WORKER_H

#include "traffic_types.h"

#include <stdint.h>

struct worker_stats {
  uint64_t packets;
  uint64_t bytes;
  
  uint64_t counters[TRAFFIC_MAX];

  uint64_t forwarded;
  uint64_t dropped;
};

struct worker_arg {
  unsigned int worker_id;
  struct rte_ring* ring;
  volatile int* stop;
  struct worker_stats stats;
};

int worker_main(void* arg);
void worker_stats_count(struct worker_arg* worker, traffic_type type, uint32_t bytes);
void worker_stats_print(struct worker_arg* worker);

#endif
