#ifndef PACKET_CTX_H
#define PACKET_CTX_H

#include "traffic_types.h"

#include <rte_mbuf.h>

struct packet_ctx {
  traffic_type type;
};

#define PACKET_CTX_SIZE RTE_ALIGN_CEIL(sizeof(struct packet_ctx), 8)

static inline struct packet_ctx* packet_to_ctx(struct rte_mbuf* mbuf) {
  return (struct packet_ctx*)rte_mbuf_to_priv(mbuf);
}

#endif
