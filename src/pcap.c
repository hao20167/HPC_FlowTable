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

static int active_dispatchers = 0;

void pcap_init(uint16_t port_id, struct rte_mempool* mbuf_pool, uint16_t num_rx_queues) {
  active_dispatchers = (int)num_rx_queues;
  struct rte_eth_conf port_conf = {0};

  int ret = rte_eth_dev_configure(port_id, num_rx_queues, 1, &port_conf);
  if (ret < 0) {
    fprintf(stderr, "rte_eth_dev_configure failed: %d\n", ret);
    rte_exit(EXIT_FAILURE, "Pcap init failed\n");
  }

  for (uint16_t q = 0; q < num_rx_queues; q++) {
    ret = rte_eth_rx_queue_setup(
      port_id, 
      q, 
      RX_RING_SIZE, 
      rte_eth_dev_socket_id(port_id),
      NULL,
      mbuf_pool
    );
    if (ret < 0) {
      fprintf(stderr, "rte_eth_rx_queue_setup failed for queue %u: %d\n", q, ret);
      rte_exit(EXIT_FAILURE, "Pcap init failed\n");
    }
  }

  ret = rte_eth_dev_start(port_id);
  if (ret < 0) {
    fprintf(stderr, "rte_eth_dev_start failed: %d\n", ret);
    rte_exit(EXIT_FAILURE, "Pcap init failed\n");
  }

  printf("Port %u started with %u RX queues\n", port_id, num_rx_queues);
}

int dispatcher_main(void* arg) {
  struct dispatcher_arg* disp = arg;
  struct rte_mbuf* pkts[RX_BURST_SIZE];
  uint32_t empty_polls = 0;

  printf("Dispatcher %u running on lcore %u, polling RX queue %u\n",
         disp->dispatcher_id, rte_lcore_id(), disp->queue_id);

  while (!*disp->stop && empty_polls < RX_MAX_EMPTY_POLLS) {
    // [1] burst get from assigned RX queue
    uint16_t n = rte_eth_rx_burst(
      disp->port_id,
      disp->queue_id,
      pkts,
      RX_BURST_SIZE
    );
    if (n == 0) {
      empty_polls++;
      rte_pause();
      continue;
    }
    empty_polls = 0;

    struct rte_mbuf* worker_pkts[MAX_WORKERS][RX_BURST_SIZE];
    unsigned int worker_pkt_cnt[MAX_WORKERS] = {0};

    for (uint16_t i = 0; i < n; i++) {
      struct rte_mbuf* mbuf = pkts[i];
      struct flow_key key = {0};
      // [2] parse packet to flow_key
      parse_result ret = packet_to_flow_key(mbuf, &key);
      if (ret != PARSE_OK) {
        rte_pktmbuf_free(mbuf);
        disp->stats.dropped++;
        continue;
      }

      // [3] assign current flow to a specific worker using 5-tuple hash
      unsigned int worker_id = (key.src_ip ^ key.dst_ip ^ ((uint32_t)key.src_port << 16 | key.dst_port) ^ key.protocol) % disp->num_workers;

      // save traffic types and key to priv space in mbuf (metadata field)
      struct packet_ctx* ctx = packet_to_ctx(mbuf);
      ctx->type = get_traffic_type_from_flow_key(&key);
      ctx->key = key;

      worker_pkts[worker_id][worker_pkt_cnt[worker_id]++] = mbuf;
    }

    // [4] pass the packets to each worker's dedicated SPSC ring for this dispatcher
    for (unsigned int w = 0; w < disp->num_workers; w++) {
      if (worker_pkt_cnt[w] > 0) {
        unsigned int sent = rte_ring_enqueue_burst(
          disp->rings[w],
          (void**)&worker_pkts[w][0],
          worker_pkt_cnt[w],
          NULL
        );
        if (sent < worker_pkt_cnt[w]) {
          for (unsigned int j = sent; j < worker_pkt_cnt[w]; j++) {
            rte_pktmbuf_free(worker_pkts[w][j]);
          }
          disp->stats.dropped += (worker_pkt_cnt[w] - sent);
        }
      }
    }

    disp->stats.processed += n;
  }

  if (__atomic_sub_fetch(&active_dispatchers, 1, __ATOMIC_SEQ_CST) == 0) {
    *disp->stop = 1;
  }
  printf("Dispatcher %u (lcore %u) stopped, packets processed = %" PRIu64 ", dropped = %" PRIu64 "\n",
         disp->dispatcher_id, rte_lcore_id(), disp->stats.processed, disp->stats.dropped);

  return 0;
}

