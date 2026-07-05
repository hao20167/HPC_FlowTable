#include "mempool.h"
#include "pcap.h"
#include "spi.h"
#include "stats.h"

#include <pthread.h>

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
  mempool_init(&mbuf_pool);

  uint16_t port_id = 0;
  pcap_init(port_id, mbuf_pool);

  struct worker_arg workers[MAX_WORKERS];
  volatile int stop = 0;
  unsigned int num_workers = workers_init(workers, &spi, &stop);

  struct flow_table ft = {0};
  flow_table_init(&ft, FLOW_TABLE_CAP);

  // ========================================================================
  // ============================== start main ==============================
  pthread_t stats_thread;
  struct stats_arg arg = {&ft, workers, num_workers, &stop};
  // tends to affect nothing at all
  pthread_create(&stats_thread, NULL, stats_thread_main, &arg);

  pcap_replay(port_id, &ft, workers, num_workers, &stop);

  pthread_join(stats_thread, NULL);
  // =============================== end main ===============================
  // ========================================================================

  stats_print(&ft, workers, num_workers);

  flow_table_free(&ft);
  rte_eal_cleanup();

  return 0;
}
