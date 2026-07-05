#include "pcap.h"
#include "config.h"
#include "flow_table.h"
#include "generic/rte_cycles.h"
#include "rte_mbuf.h"
#include "rte_mbuf_core.h"
#include "stats.h"
#include "packet_parser.h"
#include "packet_ctx.h"
#include "traffics.h"

#include <stdint.h>
#include <stdlib.h>

#include <rte_ethdev.h>

void pcap_init(uint16_t port_id, struct rte_mempool* mbuf_pool) {
  struct rte_eth_conf port_conf = {0};

  int ret = rte_eth_dev_configure(port_id, 1, 1, &port_conf);
  if (ret < 0) {
    fprintf(stderr, "rte_eth_dev_configure failed: %d\n", ret);
    rte_exit(EXIT_FAILURE, "Pcap init failed\n");
  }

  ret = rte_eth_rx_queue_setup(
    port_id, 
    0, 
    RX_RING_SIZE, 
    rte_eth_dev_socket_id(port_id),
    NULL,
    mbuf_pool
  );
  if (ret < 0) {
    fprintf(stderr, "rte_eth_rx_queue_setup failed: %d\n", ret);
    rte_exit(EXIT_FAILURE, "Pcap init failed\n");
  }

  // TODO:
  // rte_eth_tx_queue_setup
  // ...
  
  ret = rte_eth_dev_start(port_id);
  if (ret < 0) {
    fprintf(stderr, "rte_eth_dev_start failed: %d\n", ret);
    rte_exit(EXIT_FAILURE, "Pcap init failed\n");
  }

  printf("Port %u started\n", port_id);
}

void pcap_replay(uint16_t port_id, struct flow_table* ft, struct worker_arg workers[], unsigned int num_workers, volatile int* stop) {
  uint64_t last_age = 0, timeout_cycles = rte_get_tsc_hz() * FLOW_TIME_LIMIT;
  uint64_t processed = 0;

  struct rte_mbuf* pkts[RX_BURST_SIZE];
  uint32_t empty_polls = 0;

  while (empty_polls < RX_MAX_EMPTY_POLLS) {
    // [1] burst get from RX
    uint16_t n = rte_eth_rx_burst(
      port_id,
      0,
      pkts,
      RX_BURST_SIZE
    );
    if (n == 0) {
      empty_polls++;
      rte_pause();
      continue;
    }
    empty_polls = 0;

    for (uint16_t i = 0; i < n; i++) {
      struct rte_mbuf* mbuf = pkts[i];
      struct flow_key key = {0};
      // [2] parse packet to flow_key
      parse_result ret = packet_to_flow_key(mbuf, &key);
      if (ret != PARSE_OK) {
        rte_pktmbuf_free(mbuf);
        dropped++;
        continue;
      }

      // [3] lookup or create entry (from flow_key) in the flow_table
      // (also decide flow's worker)
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

      // save traffic types and key to priv space in mbuf (metadata field)
      struct packet_ctx* ctx = packet_to_ctx(mbuf);
      ctx->type = get_traffic_type_from_flow_key(&key);
      ctx->key = key;

      unsigned int worker_id = entry->worker_id;
      if (rte_ring_enqueue(workers[worker_id].ring, mbuf) < 0) {
        rte_pktmbuf_free(mbuf);
        dropped++;
        continue;
      }
    }

    if ((processed + n) / 10000 != processed / 10000) {
      uint64_t now = rte_get_tsc_cycles();
      if (now - last_age < timeout_cycles) goto skip;
      last_age = now;
      uint64_t aged_flows = flow_table_age(ft, now, timeout_cycles);
      if (aged_flows == 0) goto skip;
      printf("Aged out %" PRIu64 " flows!\n", aged_flows);
    }

  skip:;
    processed += n;
  }

  *stop = 1;
  rte_eal_mp_wait_lcore();

  {
    uint64_t aged_flows = flow_table_age(ft, rte_get_tsc_cycles(), timeout_cycles);
    if (aged_flows != 0) printf("Aged out %" PRIu64 " flows!\n", aged_flows);
  }
}

