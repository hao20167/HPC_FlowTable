#include "pcap.h"
#include "config.h"
#include "demo.h"
#include "stats.h"
#include "packet_parser.h"
#include "packet_ctx.h"

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

void pcap_replay(struct rte_mempool* mbuf_pool, struct flow_table* ft, struct worker_arg workers[], unsigned int num_workers, volatile int* stop) {
  uint64_t last_age = 0, timeout_cycles = rte_get_tsc_hz() * FLOW_TIME_LIMIT;
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
      dropped++;
      continue;
    }
    struct packet_ctx* ctx = packet_to_ctx(mbuf);
    ctx->type = get_traffic_type_from_flow_key(&key);
    ctx->key = key;

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

    unsigned int worker_id = entry->worker_id;
    if (rte_ring_enqueue(&workers[worker_id].ring, mbuf) < 0) {
      rte_pktmbuf_free(mbuf);
      dropped++;
      continue;
    }

    // after a number of packets delivered, clear the timeout_flows
    if (i % 10000 == 0) {
      uint64_t now = rte_get_tsc_cycles();
      if (last_age - now < timeout_cycles) continue;
      last_age = now;

      uint64_t aged_flows = flow_table_age(ft, now, timeout_cycles);
      if (aged_flows == 0) continue;
      printf("Aged out %" PRIu64 " flows!\n");
    }
  }

  *stop = 1;
  rte_eal_mp_wait_lcore();

  {
    uint64_t aged_flows = flow_table_age(ft, rte_get_tsc_cycles(), timeout_cycles);
    if (aged_flows != 0) printf("Aged out %" PRIu64 " flows!\n");
  }
}

