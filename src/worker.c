#include "worker.h"
#include "config.h"
#include "packet_ctx.h"
#include "spi_engine.h"
#include "traffic_types.h"

#include <rte_ring.h>
#include <rte_mbuf.h>

int worker_main(void* arg) {
  struct worker_arg* worker = arg;
  struct rte_mbuf* pkts[BURST_SIZE];

  while (!*(worker->stop) || !rte_ring_empty(worker->ring)) {
    unsigned int n = rte_ring_dequeue_burst(worker->ring, (void**)pkts, BURST_SIZE, NULL);
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
        worker->stats.forwarded--;
        worker->stats.dropped++;
        rte_pktmbuf_free(mbuf);
        continue;
      }

      // TODO: action == SPI_COUNT / SPI_LOG

      rte_pktmbuf_free(mbuf);
    }
  }

  printf("Worker %u stopped, packets processed = %" PRIu64 "\n", worker->worker_id, worker->stats.packets);

  return 0;
}


void worker_stats_count(struct worker_arg* worker, traffic_type type, uint32_t bytes) {
  struct worker_stats* stats = &worker->stats;
  if (type >= TRAFFIC_MAX) type = TRAFFIC_OTHER;
  stats->packets++;
  stats->bytes += bytes;
  stats->counters[type]++;
  stats->forwarded++;
}

void worker_stats_print(struct worker_arg* worker) {
  struct worker_stats* stats = &worker->stats;
  printf("===========\n");
  printf("[worker_id %u\n]", worker->worker_id);
  printf("processed %" PRIu64 " bytes in %" PRIu64 " packets\n", stats->bytes, stats->packets);
  for (uint8_t i = 0; i < TRAFFIC_MAX; i++) {
    printf("> %s: %" PRIu64 "\n", traffic_type_str[i], stats->counters[i]);
  }
  printf("packets forwarded: %" PRIu64 "\n", stats->forwarded);
  printf("packets dropped: %" PRIu64 "\n", stats->dropped);
  spi_engine_stats_print(&worker->spi);
  printf("===========\n");
}
