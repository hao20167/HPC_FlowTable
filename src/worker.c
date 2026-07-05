#include "worker.h"
#include "config.h"
#include "packet_ctx.h"
#include "spi.h"
#include "traffics.h"

#include <signal.h>
#include <stdlib.h>

#include <rte_ring.h>
#include <rte_mbuf.h>

int worker_main(void* arg) {
  struct worker_arg* worker = arg;
  struct rte_mbuf* pkts[WORKER_RING_BURST_SIZE];

  while (!*(worker->stop) || !rte_ring_empty(worker->ring)) {
    unsigned int n = rte_ring_dequeue_burst(worker->ring, (void**)pkts, WORKER_RING_BURST_SIZE, NULL);
    if (n == 0) {
      // assuming that pausing for a short while wouldnt affect 
      // performance, will have to try both (to keep or to remove this) 
      // while doing benchmarks
      rte_pause(); 
      continue;
    }
    for (unsigned int i = 0; i < n; i++) {
      struct rte_mbuf* mbuf = pkts[i];
      struct packet_ctx* ctx = packet_to_ctx(mbuf);
      struct spi_rule* rule = spi_engine_match(&worker->spi, &ctx->key);

      spi_action action = SPI_FORWARD;
      if (rule != NULL) action = rule->action;

      worker_stats_count(worker, ctx->type, rte_pktmbuf_data_len(mbuf));
      if (action == SPI_DROP) {
        worker->stats.dropped++;
        rte_pktmbuf_free(mbuf);
        continue;
      }

      worker->stats.forwarded++;
      // TODO: action == SPI_COUNT / SPI_LOG

      rte_pktmbuf_free(mbuf);
    }
  }

  printf("Worker %u stopped, packets processed = %" PRIu64 "\n", worker->worker_id, worker->stats.packets);

  return 0;
}

void worker_ring_init(struct rte_ring** ring, unsigned int num_workers) {
  char ring_name[32];
  snprintf(ring_name, 32, "worker_ring_%u", num_workers);
  *ring = rte_ring_create(
    ring_name,
    RING_SIZE,
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

void worker_stats_print(struct worker_arg* worker) {
  struct worker_stats* stats = &worker->stats;
  printf("===========\n");
  printf("[worker_id %u]\n", worker->worker_id);
  printf("processed %" PRIu64 " bytes in %" PRIu64 " packets\n", stats->bytes, stats->packets);
  for (uint8_t i = 0; i < TRAFFIC_MAX; i++) {
    printf("> %s: %" PRIu64 "\n", traffic_type_str[i], stats->counters[i]);
  }
  printf("packets forwarded: %" PRIu64 "\n", stats->forwarded);
  printf("packets dropped: %" PRIu64 "\n", stats->dropped);
  spi_engine_stats_print(&worker->spi);
  printf("===========\n");
}
