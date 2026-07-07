#ifndef CONFIG_H
#define CONFIG_H

#define RESULT_PATH "tests/result.txt"

#define WORKER_FLOW_TABLE_CAP 65536
#define WORKER_FLOW_TIME_LIMIT_SECONDS 5

#define NUM_PACKETS 10000
// increase this wont help, so the bottleneck must have been the workers' processing phase
// or the dispatcher is too slow (maybe this, cause the flamegraph's showing that worker has a huge busy loop)
// WARN: please keep this small (<64, more detailed in worker bulk hash lookup)
#define WORKER_RING_BURST_SIZE 32
#define WORKER_RING_SIZE 4096
#define PER_WORKER_STATS_PRINT 1

#define MAX_WORKERS 4

// according to dpdk docs, num of objects in a mempool should be 2^n-1
// each worker_ring holds WORKER_RING_SIZE of refs to mbufs => WORKER_RING_SIZE * num_worker of mbufs wont be freed until
// workers have done their work, plus rx_ring still waits for new packets to arrive
// => maximum number of mbufs in mempool is `RX_RING_SIZE + WORKER_RING_SIZE * num_worker`
// => POOL_NUM_MBUFS has to be greater than this
// also, when use rx_infinite=1, dpdk will allocate all the packets into the mempool
// => POOL_NUM_MBUFS > size(pcap) + RX_RING_SIZE + WORKER_RING_SIZE * num_worker
#define POOL_NUM_MBUFS 32767
#define POOL_CACHE_SIZE 256

#define SPI_ENGINE 1
#define SPI_RULE_NAME_LEN 16
#define SPI_RULE_LEN 128
#define SPI_RULE_PATH "rules.cfg"

// change this to 4096 wont help
#define RX_RING_SIZE 1024
// change this to 64 or 128 wont increase dropping (~2k6 pps)
// but slightly increase pps to ~2m07 pps
#define RX_BURST_SIZE 128
#define RX_MAX_EMPTY_POLLS 512

#endif
