#ifndef PCAP_H
#define PCAP_H

#include "flow_table.h"
#include "worker.h"

#include <rte_mempool.h>
#include <signal.h>

void pcap_init(uint16_t port_id, struct rte_mempool* mbuf_pool);
// void pcap_replay(uint16_t port_id, struct flow_table* ft, struct worker_arg workers[], unsigned int num_workers, volatile sig_atomic_t* stop);
void pcap_replay(struct rte_mempool* mbuf_pool, uint16_t port_id, struct flow_table* ft, struct worker_arg workers[], unsigned int num_workers, volatile sig_atomic_t* stop);

#endif
