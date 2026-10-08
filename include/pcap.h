#ifndef PCAP_H
#define PCAP_H

#include "config.h"
#include "worker.h"

#include <rte_mempool.h>
#include <signal.h>
#include <stdint.h>

struct dispatcher_stats {
  uint64_t processed;
  uint64_t dropped;
};

struct dispatcher_arg {
  unsigned int dispatcher_id;
  uint16_t port_id;
  uint16_t queue_id;
  unsigned int num_workers;
  struct rte_ring* rings[MAX_WORKERS];
  volatile sig_atomic_t* stop;
  struct dispatcher_stats stats;
};

void pcap_init(uint16_t port_id, struct rte_mempool* mbuf_pool, uint16_t num_rx_queues);
int dispatcher_main(void* arg);

#endif
