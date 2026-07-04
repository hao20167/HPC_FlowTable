#include "mempool.h"
#include "config.h"
#include "packet_ctx.h"

#include <rte_mempool.h>
#include <stdlib.h>

void mempool_init(struct rte_mempool *mbuf_pool) {
  mbuf_pool = rte_pktmbuf_pool_create(
    "MBUF_POOL",
    NUM_MBUFS,
    MBUF_CACHE_SIZE, 
    PACKET_CTX_SIZE,
    RTE_MBUF_DEFAULT_BUF_SIZE,
    rte_socket_id()
  );

  if (mbuf_pool == NULL) {
    rte_exit(EXIT_FAILURE, "Failed to create mbuf pool\n");
  }
}

