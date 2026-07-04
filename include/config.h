#ifndef CONFIG_H
#define CONFIG_H

// WARN: i have literally no idea how to choose those counstant numbers, will have to ask gpt later
#define FLOW_TABLE_CAP 65536
#define FLOW_TIME_LIMIT 5

#define NUM_PACKETS 10000
#define BURST_SIZE 32

#define MAX_WORKERS 4
// according to dpdk docs, num of objects in a mempool should be 2^n-1
#define NUM_MBUFS 8191
#define MBUF_CACHE_SIZE 256

#define SPI_RULE_NAME_LEN 16
#define SPI_RULE_LEN 128
#define SPI_RULE_PATH "rules.cfg"

#define RING_SIZE 4096

#endif
