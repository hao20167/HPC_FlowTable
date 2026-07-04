#ifndef PCAP_H
#define PCAP_H

#include "flow_table.h"
#include "worker.h"

#include <rte_mempool.h>

void pcap_init(uint16_t port_id, struct rte_mempool* mbuf_pool);
void pcap_replay(struct rte_mempool* mbuf_pool, struct flow_table* ft, struct worker_arg workers[], unsigned int num_workers, volatile int* stop);

#endif
