#include "stats.h"
#include "config.h"

#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>

#include "worker.h"

uint64_t dropped = 0;

void stats_print(struct flow_table* ft, struct worker_arg workers[], unsigned int num_workers) {
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
  printf("used = %u/%u\n", ft->used, ft->capacity);
  printf("flows created: %" PRIu64 "\n", ft->created_flows);
  printf("flows deleted: %" PRIu64 "\n", ft->deleted_flows);
  printf("flows timeout: %" PRIu64 "\n", ft->timeout_flows);
  printf("lookup_hits: %" PRIu64 "\n", ft->lookup_hits);
  printf("lookup_misses: %" PRIu64 "\n", ft->lookup_misses);
  printf("=====================\n");

  for (unsigned int i = 0; i < num_workers; i++) {
    worker_stats_print(&workers[i]);
  }

}
