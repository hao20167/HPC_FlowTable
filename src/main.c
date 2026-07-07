#include "mempool.h"
#include "pcap.h"
#include "spi.h"
#include "stats.h"

#include <pthread.h>
#include <signal.h>
#include <stdlib.h>

static volatile sig_atomic_t stop = 0;

static void handle_signal(int signum) {
  (void)signum;
  stop = 1;
}

static void install_signal_handler() {
  struct sigaction sa = {0};
  sa.sa_handler = handle_signal;
  sigemptyset(&sa.sa_mask);
  if (sigaction(SIGINT, &sa, NULL) != 0 || sigaction(SIGTERM, &sa, NULL) != 0) {
    fprintf(stderr, "Failed to install signal handler\n");
    exit(0);
  }
}

int main(int argc, char **argv) {
  install_signal_handler();

  int ret = rte_eal_init(argc, argv);
  if (ret < 0) {
    printf("EAL init failed\n");
    return 1;
  }

  struct spi_engine spi;
  spi_engine_init(&spi);

  // FIX: what if each lcore has their own mpool?
  // -> NO, mpool is only used by rx to push the packet into
  // -> if there is 1 mpool per lcore, it will have to move the packet
  // from rx pool to lcore pool, which downgrades performance
  struct rte_mempool *mbuf_pool;
  mempool_init(&mbuf_pool);

  uint16_t port_id = 0;
  pcap_init(port_id, mbuf_pool);

  struct worker_arg workers[MAX_WORKERS];
  unsigned int num_workers = workers_init(workers, &spi, &stop);

  struct flow_table ft = {0};
  flow_table_init(&ft, FLOW_TABLE_CAP);

  // ========================================================================
  // ============================== start main ==============================
  pthread_t stats_thread;
  struct stats_arg arg = {mbuf_pool, &ft, workers, num_workers, &stop};
  // tends to affect nothing at all
  if (pthread_create(&stats_thread, NULL, stats_thread_main, &arg) != 0) {
    rte_exit(EXIT_FAILURE, "Realtime stats thread creation failed\n");
  }

  pcap_replay(mbuf_pool, port_id, &ft, workers, num_workers, &stop);

  pthread_join(stats_thread, NULL);
  // =============================== end main ===============================
  // ========================================================================

  stats_print(&ft, workers, num_workers);

  flow_table_free(&ft);
  // TODO: free worker ring, mempool
  rte_eal_cleanup();
  printf("All clear!\n");

  return 0;
}
