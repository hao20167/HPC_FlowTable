#include "worker.h"
#include "flow_table.h"
#include "config.h"
#include "packet_ctx.h"
#include "rte_hash.h"
#include "spi.h"
#include "traffics.h"
#include "config.h"

#include <signal.h>
#include <stdlib.h>

#include <rte_ring.h>
#include <rte_mbuf.h>

int worker_main(void* arg) {
  struct worker_arg* worker = arg;
  struct rte_mbuf* pkts[WORKER_RING_BURST_SIZE];
  uint64_t last_age = 0, timeout_cycles = rte_get_tsc_hz() * WORKER_FLOW_TIME_LIMIT_SECONDS;
  uint64_t processed = 0;

  while (!*(worker->stop) || !rte_ring_empty(worker->ring)) {
    unsigned int n = rte_ring_dequeue_burst(worker->ring, (void**)pkts, WORKER_RING_BURST_SIZE, NULL);
    if (n == 0) {
      rte_pause(); 
      continue;
    }

    const void* keys[WORKER_RING_BURST_SIZE];
    void* entries[WORKER_RING_BURST_SIZE];
    uint64_t hit_mask = 0; 
    for (unsigned int i = 0; i < n; i++) {
      keys[i] = &packet_to_ctx(pkts[i])->key;
    }

    rte_hash_lookup_bulk_data(
      worker->ft.hash,
      keys,
      n,
      &hit_mask,
      entries
    );
    
    uint64_t now = rte_get_tsc_cycles();

    for (unsigned int i = 0; i < n; i++) {
      struct rte_mbuf* mbuf = pkts[i];
      struct packet_ctx* ctx = packet_to_ctx(mbuf);
      struct spi_rule* rule = spi_engine_match(&worker->spi, &ctx->key);

      struct flow_entry* entry;
      if (hit_mask >> i & 1) {
        entry = entries[i];
        entry->last_seen = now;
        worker->ft.lookup_hits++;
      } else entry = flow_table_lookup_or_create( &worker->ft, &ctx->key, now);
      if (entry == NULL) {
        worker->stats.dropped++;
        continue;
      }

      spi_action action = SPI_FORWARD;
      if (rule != NULL) action = rule->action;

      worker_stats_count(worker, ctx->type, rte_pktmbuf_data_len(mbuf));
      if (action == SPI_DROP) {
        worker->stats.dropped++;
        continue;
      }

      worker->stats.forwarded++;
      // TODO: action == SPI_COUNT / SPI_LOG
    }

    rte_pktmbuf_free_bulk(pkts, n);

    if ((processed + n) / 10000 != processed / 10000) {
      uint64_t now = rte_get_tsc_cycles();
      if (now - last_age < timeout_cycles) goto skip;
      last_age = now;
      uint64_t aged_flows = flow_table_age(&worker->ft, now, timeout_cycles);
      if (aged_flows == 0) goto skip;
      // printf("Aged out %" PRIu64 " flows!\n", aged_flows);
    }

  skip:;
    processed += n;
  }

  {
    uint64_t aged_flows = flow_table_age(&worker->ft, rte_get_tsc_cycles(), timeout_cycles);
    (void)aged_flows;
    // if (aged_flows != 0) printf("Aged out %" PRIu64 " flows!\n", aged_flows);
  }

  printf("Worker %u stopped, packets processed = %" PRIu64 "\n", worker->worker_id, worker->stats.packets);

  return 0;
}

void worker_ring_init(struct rte_ring** ring, unsigned int num_workers) {
  char ring_name[32];
  snprintf(ring_name, 32, "worker_ring_%u", num_workers);
  *ring = rte_ring_create(
    ring_name,
    WORKER_RING_SIZE,
    rte_socket_id(),
    RING_F_SP_ENQ | RING_F_SC_DEQ
  );
  if (*ring == NULL) {
    rte_exit(EXIT_FAILURE, "Failed to create %s", ring_name);
  }
}

unsigned int workers_init(struct worker_arg workers[], struct spi_engine* spi, volatile sig_atomic_t *stop) {
  unsigned int lcore_id, num_workers = 0;
  RTE_LCORE_FOREACH_WORKER(lcore_id) {
    if (num_workers >= MAX_WORKERS) break;

    // initialize worker_arg
    workers[num_workers].worker_id = num_workers;
    memset(&workers[num_workers].ft, 0, sizeof(struct flow_table));
    flow_table_init(&workers[num_workers].ft, WORKER_FLOW_TABLE_CAP, num_workers);
    worker_ring_init(&workers[num_workers].ring, num_workers);
    memcpy(&workers[num_workers].spi, spi, sizeof(struct spi_engine));
    workers[num_workers].stop = stop;
    memset(&workers[num_workers].stats, 0, sizeof(struct worker_stats));

    // send message to worker lcore to wake it up (WAIT (from init) -> RUNNING)
    if (rte_eal_remote_launch(worker_main, &workers[num_workers], lcore_id) < 0) {
      rte_exit(EXIT_FAILURE, "Failed to rte_eal_remote_launch [lcore_id=%u]\n", lcore_id);
    }

    num_workers++;
  }

  if (num_workers == 0) {
    rte_exit(EXIT_FAILURE, "Failed to run, need at least 1 worker lcore\n");
  }

  printf("Dispatcher running on lcore: %u\n", rte_lcore_id());
  printf("Workers: %u\n", num_workers);

  return num_workers;
}

void worker_stats_count(struct worker_arg* worker, traffic_type type, uint32_t bytes) {
  struct worker_stats* stats = &worker->stats;
  if (type >= TRAFFIC_MAX) type = TRAFFIC_OTHER;
  stats->packets++;
  stats->bytes += bytes;
  stats->counters[type]++;
}

void worker_stats_print(FILE* fp, struct worker_arg* worker) {
  struct worker_stats* stats = &worker->stats;
  fprintf(fp, "=================================\n");
  fprintf(fp, "----------[worker_id %u]----------\n", worker->worker_id);
  fprintf(fp, "processed %" PRIu64 " bytes in %" PRIu64 " packets\n", stats->bytes, stats->packets);
  for (uint8_t i = 0; i < TRAFFIC_MAX; i++) {
    fprintf(fp, "> %s: %" PRIu64 "\n", traffic_type_str[i], stats->counters[i]);
  }
  fprintf(fp, "packets forwarded: %" PRIu64 "\n", stats->forwarded);
  fprintf(fp, "packets dropped: %" PRIu64 "\n", stats->dropped);
  fprintf(fp, "Active flows:   %" PRIu64 "\n", worker->ft.active_flows);
  fprintf(fp, "Created flows:  %" PRIu64 "\n", worker->ft.created_flows);
  fprintf(fp, "Deleted flows:  %" PRIu64 "\n", worker->ft.deleted_flows);
  fprintf(fp, "Timeout flows:  %" PRIu64 "\n", worker->ft.timeout_flows);
  fprintf(fp, "Lookup hits:    %" PRIu64 "\n", worker->ft.lookup_hits);
  fprintf(fp, "Lookup misses:  %" PRIu64 "\n", worker->ft.lookup_misses);
  spi_engine_stats_print(fp, &worker->spi);
  fprintf(fp, "=================================\n");
  fprintf(fp, "\n\n");
}
