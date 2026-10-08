#ifndef STATS_H
#define STATS_H

#include "config.h"
#include "worker.h"

#include <signal.h>
#include <stdint.h>

#include "pcap.h"

struct stats_arg {
  struct rte_mempool* mbuf_pool;
  struct dispatcher_arg* dispatchers;
  unsigned int num_dispatchers;
  struct worker_arg* workers;
  unsigned int num_workers;
  volatile sig_atomic_t* stop;
};

void* stats_thread_main(void* arg);
void stats_print(struct dispatcher_arg* dispatchers, unsigned int num_dispatchers, struct worker_arg* workers, unsigned int num_workers);

#endif
