#ifndef WORKER_H
#define WORKER_H

#include "spi.h"
#include "traffics.h"

#include <signal.h>
#include <stdint.h>

#include <rte_ring.h>

struct worker_stats {
  uint64_t packets;
  uint64_t bytes;
  
  uint64_t counters[TRAFFIC_MAX];

  uint64_t forwarded;
  uint64_t dropped;
};

struct worker_arg {
  unsigned int worker_id;
  struct flow_table ft;
  struct rte_ring* ring;
  struct spi_engine spi;
  volatile sig_atomic_t* stop;
  struct worker_stats stats;
};

int worker_main(void* arg);
void worker_ring_init(struct rte_ring** ring, unsigned int num_workers);
unsigned int workers_init(struct worker_arg workers[], struct spi_engine* spi, volatile sig_atomic_t *stop);
void worker_stats_count(struct worker_arg* worker, traffic_type type, uint32_t bytes);
void worker_stats_print(struct worker_arg* worker);

#endif
