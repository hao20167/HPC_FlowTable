#include "flow_table.h"

#include <rte_hash.h>
#include <rte_jhash.h>
#include <rte_malloc.h>

#include <stdlib.h>

void flow_table_init(struct flow_table* ft, uint32_t capacity) {
  struct rte_hash_parameters params = {
    .name = "flow_hash",
    .entries = capacity,
    .key_len = sizeof(struct flow_key),
    .hash_func_init_val = 0,
    .hash_func = rte_jhash, // TODO: why
    .socket_id = rte_socket_id()
  };

  ft->hash = rte_hash_create(&params);
  if (ft->hash == NULL) {
    fprintf(stderr, "Failed to initialize rte_hash\n");
    rte_exit(EXIT_FAILURE, "Failed to initialize flow_table\n");
  }

  // this, initializes in the `hugepages` area
  ft->entries = rte_zmalloc("flow_entries", sizeof(struct flow_entry) * capacity, 64);
  if (ft->entries == NULL) {
    fprintf(stderr, "Failed to initialize flow_entry\n");
    rte_exit(EXIT_FAILURE, "Failed to initialize flow_table\n");
  }

  ft->capacity = capacity;
  ft->used = 0;
}

void flow_table_free(struct flow_table* ft) {
  if (ft->hash != NULL) rte_free(ft->hash);
  if (ft->entries != NULL) rte_free(ft->entries);
  memset(ft, 0, sizeof(*ft));
}

struct flow_entry* flow_table_lookup_or_create(
  struct flow_table* ft,
  struct flow_key* key,
  uint8_t num_workers,
  uint64_t now
) {
  void* found = NULL;
  int ret = rte_hash_lookup_data(ft->hash, key, &found);
  if (ret >= 0) {
    struct flow_entry* entry = found;
    entry->packets++;
    entry->last_seen = now;
    ft->lookup_hits++;
    return found;
  }

  ft->lookup_misses++;

  if (ft->used >= ft->capacity) return NULL;
  struct flow_entry* entry = &ft->entries[ft->used++];

  entry->key = *key;
  entry->worker_id = key->src_ip % num_workers; // WARN: modify this pls
  entry->in_use = 1;
  entry->create_time = now;
  entry->last_seen = now;
  entry->packets = 1;

  ret = rte_hash_add_key_data(ft->hash, key, entry);
  if (ret < 0) return NULL;

  ft->active_flows++;
  ft->created_flows++;
  
  return entry;
}

uint64_t flow_table_age(struct flow_table* ft, uint64_t now, uint64_t timeout_cycles) {
  uint64_t aged_flows = 0;
  for (uint32_t i = 0; i < ft->used; i++) {
    struct flow_entry* entry = &ft->entries[i];
    if (entry->in_use == 0) continue;
    if (now - entry->last_seen < timeout_cycles) continue;

    aged_flows++;
    int ret = rte_hash_del_key(ft->hash, &entry->key);
    if (ret < 0) continue;

    entry->in_use = 0;
  }

  if (aged_flows) {
    ft->deleted_flows += aged_flows;
    ft->timeout_flows += aged_flows;
  }

  return aged_flows;
}
