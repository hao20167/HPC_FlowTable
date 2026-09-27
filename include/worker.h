#ifndef WORKER_H
#define WORKER_H

#include "config.h"
#include "flow_table.h"
#include "spi.h"
#include "traffics.h"

#include <signal.h>
#include <stdint.h>
#include <stdio.h>

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
  struct rte_ring* in_rings[NUM_DISPATCHERS];
  unsigned int num_dispatchers;
  struct spi_engine spi;
  volatile sig_atomic_t* stop;
  struct worker_stats stats;
};

int worker_main(void* arg);
void crossbar_rings_init(struct rte_ring* rings[NUM_DISPATCHERS][MAX_WORKERS], unsigned int num_dispatchers, unsigned int num_workers);
void workers_init(struct worker_arg workers[], unsigned int num_workers, struct rte_ring* rings[NUM_DISPATCHERS][MAX_WORKERS], unsigned int num_dispatchers, struct spi_engine* spi, volatile sig_atomic_t *stop);
void worker_stats_count(struct worker_arg* worker, traffic_type type, uint32_t bytes);
void worker_stats_print(FILE* fp, struct worker_arg* worker);

#endif
