#include "pcap.h"
#include "config.h"
#include "flow_table.h"
#include "stats.h"
#include "packet_parser.h"
#include "packet_ctx.h"
#include "traffics.h"

#include <signal.h>
#include <stdint.h>
#include <stdlib.h>

#include <rte_ethdev.h>
#include <rte_mbuf.h>
#include <rte_mbuf_core.h>
#include <rte_mempool.h>

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

#include <rte_thash.h>

/* Standard 40-byte Microsoft RSS Key used by most NICs by default */
static const uint8_t default_rss_key[40] = {
    0x6d, 0x5a, 0x56, 0xda, 0x25, 0x5b, 0x0e, 0xc2,
    0x41, 0x67, 0x25, 0x3d, 0x43, 0xa3, 0x8f, 0xb0,
    0xd0, 0xca, 0x2b, 0xcb, 0xae, 0x7b, 0x30, 0xb4,
    0x77, 0xcb, 0x2d, 0xa3, 0x80, 0x30, 0xf2, 0x0c,
    0x6a, 0x42, 0xb7, 0x3b, 0xbe, 0xac, 0x01, 0xfa
};

static unsigned int teoplitz_dispatch(struct flow_key* key, unsigned int num_worker) {
  union rte_thash_tuple tuple;
  tuple.v4.src_addr = key->src_ip;
  tuple.v4.dst_addr = key->dst_ip;
  tuple.v4.sport = key->src_port;
  tuple.v4.dport = key->dst_port;
  uint32_t tuple_len_32bit_words = RTE_THASH_V4_L4_LEN; 
  uint32_t hash_result = rte_softrss_be((uint32_t *)&tuple, tuple_len_32bit_words, default_rss_key);
  return hash_result % num_worker;
}

void pcap_replay(struct rte_mempool* mbuf_pool, uint16_t port_id, struct worker_arg workers[], unsigned int num_workers, volatile sig_atomic_t* stop) {
  (void)mbuf_pool;

  struct rte_mbuf* pkts[RX_BURST_SIZE];
  uint32_t empty_polls = 0;

  while (!*stop && empty_polls < RX_MAX_EMPTY_POLLS) {
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

      unsigned int worker_id = teoplitz_dispatch(&key, num_workers);

      // save traffic types and key to priv space in mbuf (metadata field)
      struct packet_ctx* ctx = packet_to_ctx(mbuf);
      ctx->type = get_traffic_type_from_flow_key(&key);
      ctx->key = key;

      if (rte_ring_enqueue(workers[worker_id].ring, mbuf) < 0) {
        rte_pktmbuf_free(mbuf);
        dropped++;
        continue;
      }
    }

    processed += n;
  }

  *stop = 1;
  rte_eal_mp_wait_lcore();


  rte_eth_dev_stop(port_id);
  rte_eth_dev_close(port_id);

  // exit(0); // WARN: lines after rte_eal_mp_wait_lcore only print out after i uncomment this? why?
}

