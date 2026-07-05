#include "stats.h"
#include "config.h"
#include "flow_table.h"
#include "worker.h"

#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

uint64_t dropped = 0;

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

static void print_realtime(struct flow_table* ft, struct worker_arg workers[], unsigned int num_workers, volatile int* stop) {
  uint64_t prev_packets = 0;
  uint64_t prev_bytes = 0;

  while (!*stop) {
    sleep(1);

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
    printf("=== Realtime Benchmark ===\n");
    printf("PPS:       %.2f\n", pps);
    printf("Mbps:      %.2f\n", mbps);
    printf("Gbps:      %.4f\n", gbps);
    printf("\n");
    printf("=== Packets ===\n");
    printf("Processed: %" PRIu64 "\n", packets);
    printf("Forward:   %" PRIu64 "\n", forwarded);
    printf("Drop:      %" PRIu64 "\n", dropped + dropped_worker);
    printf("\n");
    printf("=== Flows ===\n");
    printf("Active:    %" PRIu64 "\n", ft->active_flows);
    printf("Created:   %" PRIu64 "\n", ft->created_flows);
    printf("Deleted:   %" PRIu64 "\n", ft->deleted_flows);
    printf("Timeout:   %" PRIu64 "\n", ft->timeout_flows);
    printf("Used:      %u/%u\n", ft->used, ft->capacity);
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
  print_realtime(arg->ft, arg->workers, arg->num_workers, arg->stop);
  return NULL;
}

void stats_print(struct flow_table* ft, struct worker_arg workers[], unsigned int num_workers) {
  uint64_t packets = 0;
  uint64_t bytes = 0;
  uint64_t forwarded = 0;
  uint64_t dropped_worker = 0;
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

  uint64_t total_dropped = dropped + dropped_worker;

  printf("\n");
  printf("========== Final Statistics ==========\n");
  printf("Packets dispatched: %" PRIu64 "\n", (uint64_t)NUM_PACKETS);
  printf("Packets processed:  %" PRIu64 "\n", packets);
  printf("Packets forwarded:  %" PRIu64 "\n", forwarded);
  printf("Packets dropped:    %" PRIu64 "\n", total_dropped);
  printf("  - Main dropped:   %" PRIu64 "\n", dropped);
  printf("  - Worker dropped: %" PRIu64 "\n", dropped_worker);
  printf("Bytes processed:    %" PRIu64 "\n", bytes);

  if (packets > 0) {
    printf("Avg packet size:    %.2f bytes\n", (double)bytes / (double)packets);
  }

  printf("\n");
  printf("========== Workers ==========\n");
  printf("Num workers: %u\n", num_workers);

  printf("\n");
  printf("========== Flow Table ==========\n");
  printf("Active flows:   %" PRIu64 "\n", ft->active_flows);
  printf("Used:           %u/%u\n", ft->used, ft->capacity);
  printf("Flows created:  %" PRIu64 "\n", ft->created_flows);
  printf("Flows deleted:  %" PRIu64 "\n", ft->deleted_flows);
  printf("Flows timeout:  %" PRIu64 "\n", ft->timeout_flows);
  printf("Lookup hits:    %" PRIu64 "\n", ft->lookup_hits);
  printf("Lookup misses:  %" PRIu64 "\n", ft->lookup_misses);

  printf("\n");
  printf("========== Traffic ==========\n");
  printf("HTTP:   %" PRIu64 "\n", traffic[TRAFFIC_HTTP]);
  printf("HTTPS:  %" PRIu64 "\n", traffic[TRAFFIC_HTTPS]);
  printf("DNS:    %" PRIu64 "\n", traffic[TRAFFIC_DNS]);
  printf("TCP:    %" PRIu64 "\n", traffic[TRAFFIC_TCP]);
  printf("UDP:    %" PRIu64 "\n", traffic[TRAFFIC_UDP]);
  printf("OTHER:  %" PRIu64 "\n", traffic[TRAFFIC_OTHER]);

  printf("\n");
  printf("========== Per-worker Stats ==========\n");

  for (unsigned int i = 0; i < num_workers; i++) {
    worker_stats_print(&workers[i]);
  }

  printf("======================================\n");
}
