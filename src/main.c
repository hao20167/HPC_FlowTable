#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#include <rte_eal.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>
#include <rte_mempool.h>
#include <rte_pause.h>
#include <rte_ring.h>
#include <generic/rte_cycles.h>
#include <rte_hash.h>

#include "flow_table.h"
#include "packet_parser.h"
#include "packet_ctx.h"
#include "demo.h"
#include "config.h"
#include "worker.h"
#include "spi_engine.h"
#include "stats.h"

void mempool_init(struct rte_mempool *mbuf_pool);
void run_pcap_replay(struct rte_mempool* mbuf_pool, struct flow_table* ft, struct worker_arg workers[], unsigned int num_workers, volatile int* stop);

int main(int argc, char **argv) {
  int ret = rte_eal_init(argc, argv);
  if (ret < 0) {
    printf("EAL init failed\n");
    return 1;
  }

  struct spi_engine spi;
  spi_engine_init(&spi);

  struct rte_mempool *mbuf_pool;
  mempool_init(mbuf_pool);

  struct worker_arg workers[MAX_WORKERS];
  volatile int stop = 0;
  unsigned int num_workers = workers_init(workers, &spi, &stop);

  struct flow_table ft = {0};
  flow_table_init(&ft, FLOW_TABLE_CAP);


  // start main
  uint64_t dropped = 0;
  run_pcap_replay(mbuf_pool, &ft, workers, num_workers, &stop);
  // end main

  stats_print(&ft, workers, num_workers);

  flow_table_free(&ft);
  rte_eal_cleanup();

  return 0;
}


void mempool_init(struct rte_mempool *mbuf_pool) {
  mbuf_pool = rte_pktmbuf_pool_create(
    "MBUF_POOL",
    NUM_MBUFS,
    MBUF_CACHE_SIZE, 
    PACKET_CTX_SIZE,
    RTE_MBUF_DEFAULT_BUF_SIZE,
    rte_socket_id()
  );

  if (mbuf_pool == NULL) {
    rte_exit(EXIT_FAILURE, "Failed to create mbuf pool\n");
  }
}

void run_pcap_replay(struct rte_mempool* mbuf_pool, struct flow_table* ft, struct worker_arg workers[], unsigned int num_workers, volatile int* stop) {
  uint64_t last_age = 0, timeout_cycles = rte_get_tsc_hz() * FLOW_TIME_LIMIT;
  for (uint64_t i = 0; i < NUM_PACKETS; i++) {
    struct rte_mbuf* mbuf = rte_pktmbuf_alloc(mbuf_pool);
    if (mbuf == NULL) { // couldnt alloc = failed to receive that mbuf packet
      dropped++;
      continue;
    }
    
    if (demo_tcp_mbuf(mbuf, i) < 0) {
      rte_pktmbuf_free(mbuf);
      dropped++;
      continue;
    }

    struct flow_key key = {0};
    parse_result ret = packet_to_flow_key(mbuf, &key);
    if (ret != PARSE_OK) {
      rte_pktmbuf_free(mbuf);
      dropped++;
      continue;
    }
    struct packet_ctx* ctx = packet_to_ctx(mbuf);
    ctx->type = get_traffic_type_from_flow_key(&key);
    ctx->key = key;

    struct flow_entry* entry = flow_table_lookup_or_create(
      ft,
      &key,
      num_workers,
      rte_get_tsc_cycles()
    );
    if (entry == NULL) {
      rte_pktmbuf_free(mbuf);
      dropped++;
      continue;
    }

    unsigned int worker_id = entry->worker_id;
    if (rte_ring_enqueue(&workers[worker_id].ring, mbuf) < 0) {
      rte_pktmbuf_free(mbuf);
      dropped++;
      continue;
    }

    // after a number of packets delivered, clear the timeout_flows
    if (i % 10000 == 0) {
      uint64_t now = rte_get_tsc_cycles();
      if (last_age - now < timeout_cycles) continue;
      last_age = now;

      uint64_t aged_flows = flow_table_age(ft, now, timeout_cycles);
      if (aged_flows == 0) continue;
      printf("Aged out %" PRIu64 " flows!\n");
    }
  }

  *stop = 1;
  rte_eal_mp_wait_lcore();

  {
    uint64_t aged_flows = flow_table_age(ft, rte_get_tsc_cycles(), timeout_cycles);
    if (aged_flows != 0) printf("Aged out %" PRIu64 " flows!\n");
  }
}

