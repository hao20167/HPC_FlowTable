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

int main(int argc, char **argv) {
  int ret = rte_eal_init(argc, argv);
  if (ret < 0) {
    printf("EAL init failed\n");
    return 1;
  }

  struct rte_mempool *mbuf_pool = rte_pktmbuf_pool_create(
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

  struct rte_ring* rings[MAX_WORKERS];
  struct worker_arg workers[MAX_WORKERS];

  volatile int stop = 0;
  unsigned int lcore_id, num_workers = 0;

  RTE_LCORE_FOREACH_WORKER(lcore_id) {
    if (num_workers >= MAX_WORKERS) break;

    char ring_name[32];
    snprintf(ring_name, 32, "worker_ring_%u", num_workers);

    rings[num_workers] = rte_ring_create(
      ring_name,
      RING_SIZE,
      rte_socket_id(),
      RING_F_SP_ENQ | RING_F_SC_DEQ
    );
    if (rings[num_workers] == NULL) {
      rte_exit(EXIT_FAILURE, "Failed to create %s", ring_name);
    }

    workers[num_workers].worker_id = num_workers;
    workers[num_workers].ring = rings[num_workers];
    workers[num_workers].stop = &stop;
    memset(&workers[num_workers].stats, 0, sizeof(struct worker_stats));

    // send message to worker lcore to wake it up (WAIT (from init) -> RUNNING)
    rte_eal_remote_launch(worker_main, &workers[num_workers], lcore_id);

    num_workers++;
  }

  if (num_workers == 0) {
    rte_exit(EXIT_FAILURE, "Failed to run, need at least 1 worker lcore\n");
  }

  printf("Dispatcher running on lcore: %u\n", rte_lcore_id());
  printf("Workers: %u\n", num_workers);

  struct flow_table ft = {0};
  ret = flow_table_init(&ft, FLOW_TABLE_CAP);
  if (ret < 0) {
    rte_exit(EXIT_FAILURE, "Failed to initialize flow_table\n");
  }

  uint64_t last_age = 0, timeout_cycles = rte_get_tsc_hz() * FLOW_TIME_LIMIT;
  uint64_t dropped = 0;

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
      if (dropped % 5000 == 0) {
        fprintf(stderr, "packet_to_flow_key\n");
        fprintf(stderr, "%s\n", parse_result_str[ret]);
      }
      dropped++;
      continue;
    }
    struct packet_ctx* ctx = packet_to_ctx(mbuf);
    ctx->type = get_traffic_type_from_flow_key(&key);

    struct flow_entry* entry = flow_table_lookup_or_create(
      &ft,
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
    if (rte_ring_enqueue(workers[worker_id].ring, mbuf) < 0) {
      rte_pktmbuf_free(mbuf);
      dropped++;
      continue;
    }

    // after a number of packets delivered, clear the timeout_flows
    if (i % 10000 == 0) {
      uint64_t now = rte_get_tsc_cycles();
      if (last_age - now < timeout_cycles) continue;
      last_age = now;

      uint64_t aged_flows = flow_table_age(&ft, now, timeout_cycles);
      if (aged_flows == 0) continue;
      printf("Aged out %" PRIu64 " flows!\n");
    }
  }

  stop = 1;
  rte_eal_mp_wait_lcore();

  {
    uint64_t aged_flows = flow_table_age(&ft, rte_get_tsc_cycles(), timeout_cycles);
    if (aged_flows != 0) printf("Aged out %" PRIu64 " flows!\n");
  }

  uint64_t total = 0;
  for (unsigned int i = 0; i < num_workers; i++) {
    total += workers[i].stats.packets;
  }

  printf("=====================\n");
  printf("packets dispatched: %u\n", NUM_PACKETS);
  printf("packets processed: %" PRIu64 "\n", total);
  printf("packets dropped: %" PRIu64 "\n", dropped);
  printf("=====================\n");
  printf("num_workers: %u\n", num_workers);
  printf("FLOW TABLE ==========\n");
  printf("used = %u/%u\n", ft.used, ft.capacity);
  printf("flows created: %" PRIu64 "\n", ft.created_flows);
  printf("flows deleted: %" PRIu64 "\n", ft.deleted_flows);
  printf("flows timeout: %" PRIu64 "\n", ft.timeout_flows);
  printf("lookup_hits: %" PRIu64 "\n", ft.lookup_hits);
  printf("lookup_misses: %" PRIu64 "\n", ft.lookup_misses);
  printf("=====================\n");

  for (unsigned int i = 0; i < num_workers; i++) {
    worker_stats_print(&workers[i]);
  }

  flow_table_free(&ft);
  rte_eal_cleanup();
}
