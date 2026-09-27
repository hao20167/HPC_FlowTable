#include "stats.h"
#include "config.h"
#include "pcap.h"
#include "worker.h"

#include <inttypes.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <rte_mempool.h>

static time_t stats_start_time = 0;

static void collect_dispatcher_stats(struct dispatcher_arg dispatchers[], unsigned int num_dispatchers, uint64_t* dispatched, uint64_t* main_dropped) {
  *dispatched = *main_dropped = 0;
  for (unsigned int d = 0; d < num_dispatchers; d++) {
    *dispatched += dispatchers[d].stats.processed;
    *main_dropped += dispatchers[d].stats.dropped;
  }
}

static void collect_worker_stats(struct worker_arg workers[], unsigned int num_workers, uint64_t* packets, uint64_t* bytes, uint64_t traffic[TRAFFIC_MAX], uint64_t* forwarded, uint64_t* dropped_worker) {
  *packets = *bytes = *forwarded = *dropped_worker = 0;
  for (uint8_t i = 0; i < TRAFFIC_MAX; i++) traffic[i] = 0;

  for (unsigned int i = 0; i < num_workers; i++) {
    struct worker_stats* s = &workers[i].stats;

    *packets += s->packets;
    *bytes += s->bytes;
    *forwarded += s->forwarded;
    *dropped_worker += s->dropped;
    for (uint8_t t = 0; t < TRAFFIC_MAX; t++) {
      traffic[t] += s->counters[t];
    }
  }
}

static void print_realtime(struct rte_mempool* mbuf_pool, struct dispatcher_arg dispatchers[], unsigned int num_dispatchers, struct worker_arg workers[], unsigned int num_workers, volatile sig_atomic_t* stop) {
  stats_start_time = time(NULL);

  uint64_t prev_packets = 0;
  uint64_t prev_bytes = 0;

  while (!*stop) {
    sleep(1);

    uint64_t dispatched = 0, main_dropped = 0;
    collect_dispatcher_stats(dispatchers, num_dispatchers, &dispatched, &main_dropped);

    uint64_t packets, bytes, forwarded, dropped_worker;
    uint64_t traffic[TRAFFIC_MAX];

    collect_worker_stats(workers, num_workers, &packets, &bytes, traffic, &forwarded, &dropped_worker);

    uint64_t delta_packets = packets - prev_packets, delta_bytes = bytes - prev_bytes;
    prev_packets = packets;
    prev_bytes = bytes;

    double pps = (double)delta_packets;
    double mbps = ((double)delta_bytes * 8.0) / 1000000.0;
    double gbps = ((double)delta_bytes * 8.0) / 1000000000.0;

    printf("\033[2J\033[H");
    printf("Mempool avail: %u\n", rte_mempool_avail_count(mbuf_pool));
    printf("=== Realtime Benchmark (Multi-Dispatcher) ===\n");
    printf("PPS:       %.2f\n", pps);
    printf("Mbps:      %.2f\n", mbps);
    printf("Gbps:      %.4f\n", gbps);
    printf("\n");
    printf("=== Packets ===\n");
    printf("Dispatched: %" PRIu64 "\n", dispatched);
    printf("Processed:  %" PRIu64 "\n", packets);
    printf("Forward:    %" PRIu64 "\n", forwarded);
    printf("Drop:       %" PRIu64 " (Main: %" PRIu64 ", Worker: %" PRIu64 ")\n",
           main_dropped + dropped_worker, main_dropped, dropped_worker);
    printf("\n");
    printf("=== Traffic ===\n");
    printf("HTTP:      %" PRIu64 "\n", traffic[TRAFFIC_HTTP]);
    printf("HTTPS:     %" PRIu64 "\n", traffic[TRAFFIC_HTTPS]);
    printf("DNS:       %" PRIu64 "\n", traffic[TRAFFIC_DNS]);
    printf("TCP:       %" PRIu64 "\n", traffic[TRAFFIC_TCP]);
    printf("UDP:       %" PRIu64 "\n", traffic[TRAFFIC_UDP]);
    printf("OTHER:     %" PRIu64 "\n", traffic[TRAFFIC_OTHER]);

    fflush(stdout);
  }
}

void* stats_thread_main(void* args) {
  struct stats_arg* arg = args;
  print_realtime(arg->mbuf_pool, arg->dispatchers, arg->num_dispatchers, arg->workers, arg->num_workers, arg->stop);
  return NULL;
}

void stats_print(struct dispatcher_arg dispatchers[], unsigned int num_dispatchers, struct worker_arg workers[], unsigned int num_workers) {
  FILE* fp = fopen(RESULT_PATH, "w");
  if (fp == NULL) {
    perror("Failed to open " RESULT_PATH "\n");
    fp = stdout;
  }

  uint64_t dispatched = 0, main_dropped = 0;
  collect_dispatcher_stats(dispatchers, num_dispatchers, &dispatched, &main_dropped);

  uint64_t packets = 0, bytes = 0, forwarded = 0, dropped_worker = 0;
  uint64_t traffic[TRAFFIC_MAX];

  collect_worker_stats(
      workers,
      num_workers,
      &packets,
      &bytes,
      traffic,
      &forwarded,
      &dropped_worker
  );

  double seconds = stats_start_time > 0 ? difftime(time(NULL), stats_start_time) : 0.0;

  fprintf(fp, "\n");
  fprintf(fp, "========== Final Statistics ==========\n");
  fprintf(fp, "Packets dispatched: %" PRIu64 "\n", dispatched);
  fprintf(fp, "Packets processed:  %" PRIu64 "\n", packets);
  fprintf(fp, "Packets forwarded:  %" PRIu64 "\n", forwarded);
  fprintf(fp, "Packets dropped:    %" PRIu64 "\n", main_dropped + dropped_worker);
  fprintf(fp, "  - Main dropped:   %" PRIu64 "\n", main_dropped);
  fprintf(fp, "  - Worker dropped: %" PRIu64 "\n", dropped_worker);
  fprintf(fp, "Bytes processed:    %" PRIu64 "\n", bytes);

  if (seconds > 0.0) {
    double pps = (double)packets / seconds;
    double mbps = ((double)bytes * 8.0) / seconds / 1000000.0;
    double gbps = ((double)bytes * 8.0) / seconds / 1000000000.0;

    fprintf(fp, "Duration:           %.2f sec\n", seconds);
    fprintf(fp, "PPS:                %.2f\n", pps);
    fprintf(fp, "Mbps:               %.2f\n", mbps);
    fprintf(fp, "Gbps:               %.4f\n", gbps);
  }

  if (packets > 0) {
    fprintf(fp, "Avg packet size:    %.2f bytes\n", (double)bytes / (double)packets);
  }

  fprintf(fp, "========== Traffic ==========\n");
  fprintf(fp, "HTTP:   %" PRIu64 "\n", traffic[TRAFFIC_HTTP]);
  fprintf(fp, "HTTPS:  %" PRIu64 "\n", traffic[TRAFFIC_HTTPS]);
  fprintf(fp, "DNS:    %" PRIu64 "\n", traffic[TRAFFIC_DNS]);
  fprintf(fp, "TCP:    %" PRIu64 "\n", traffic[TRAFFIC_TCP]);
  fprintf(fp, "UDP:    %" PRIu64 "\n", traffic[TRAFFIC_UDP]);
  fprintf(fp, "OTHER:  %" PRIu64 "\n", traffic[TRAFFIC_OTHER]);

  fprintf(fp, "========== Dispatchers ==========\n");
  fprintf(fp, "Num dispatchers: %u\n", num_dispatchers);
  for (unsigned int d = 0; d < num_dispatchers; d++) {
    fprintf(fp, "  - [dispatcher %u] dispatched: %" PRIu64 ", dropped: %" PRIu64 "\n",
            dispatchers[d].dispatcher_id, dispatchers[d].stats.processed, dispatchers[d].stats.dropped);
  }

  fprintf(fp, "========== Workers ==========\n");
  fprintf(fp, "Num workers: %u\n", num_workers);

  if (PER_WORKER_STATS_PRINT == 0) {
    if (fp != stdout) fclose(fp);
    return;
  }

  fprintf(fp, "\n\n\n");
  fprintf(fp, "----------------------------------------\n");
  fprintf(fp, "|           Per-worker Stats           |\n");
  fprintf(fp, "----------------------------------------\n");
  for (unsigned int i = 0; i < num_workers; i++) {
    worker_stats_print(fp, &workers[i]);
  }
  fprintf(fp, "======================================\n");

  if (fp != stdout) fclose(fp);

  printf("Result is available at \n" RESULT_PATH "\n");
}
