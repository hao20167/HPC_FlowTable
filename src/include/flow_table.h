#ifndef FLOW_TABLE_H
#define FLOW_TABLE_H

#include <stdint.h>

#include <rte_hash.h>

struct flow_key {
  uint32_t src_ip;
  uint32_t dst_ip;
  uint16_t src_port;
  uint16_t dst_port;
  uint8_t protocol;
  // 13 bytes
  uint8_t _padding[3]; // TODO: can this be optimized?
  // ensure all the values are being set to 0
}; // WARN: consider using `__attribute__((__packed__));`

struct flow_entry {
  struct flow_key key;
  uint8_t worker_id;
  uint8_t in_use;
  uint64_t create_time;
  uint64_t last_seen;
  uint64_t packets;
};

struct flow_table {
  struct rte_hash* hash;
  struct flow_entry* entries;
  uint32_t capacity;
  uint32_t used;

  uint64_t active_flows;
  uint64_t created_flows;
  uint64_t deleted_flows;
  uint64_t timeout_flows;
  uint64_t lookup_hits;
  uint64_t lookup_misses;
};

int flow_table_init(struct flow_table* ft, uint32_t capacity);
void flow_table_free(struct flow_table* ft);
struct flow_entry* flow_table_lookup_or_create(
  struct flow_table* ft,
  struct flow_key* key,
  uint8_t num_workers,
  uint64_t now
);

uint64_t flow_table_age(struct flow_table* ft, uint64_t now, uint64_t timeout_cycles);

#endif
