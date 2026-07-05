#ifndef MEMPOOL_H
#define MEMPOOL_H

#include <rte_mempool.h>

void mempool_init(struct rte_mempool** mbuf_pool);

#endif
