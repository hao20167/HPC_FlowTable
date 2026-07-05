#include "mempool.h"
#include "spi.h"
#include "pcap.h"
#include "stats.h"

int main(int argc, char **argv) {
  int ret = rte_eal_init(argc, argv);
  if (ret < 0) {
    printf("EAL init failed\n");
    return 1;
  }

  struct spi_engine spi;
  spi_engine_init(&spi);

  // FIX: what if each lcore has their own mpool?
  struct rte_mempool *mbuf_pool;
  mempool_init(mbuf_pool);

  uint16_t port_id = 0;
  pcap_init(port_id, mbuf_pool);

  struct worker_arg workers[MAX_WORKERS];
  volatile int stop = 0;
  unsigned int num_workers = workers_init(workers, &spi, &stop);

  struct flow_table ft = {0};
  flow_table_init(&ft, FLOW_TABLE_CAP);

  // ============================== start main ==============================
  uint64_t dropped = 0;
  pcap_replay(port_id, &ft, workers, num_workers, &stop);
  // =============================== end main ===============================

  stats_print(&ft, workers, num_workers);

  flow_table_free(&ft);
  rte_eal_cleanup();

  return 0;
}

