#include "mempool.h"
#include "pcap.h"
#include "spi.h"
#include "stats.h"

#include <pthread.h>
#include <signal.h>
#include <stdlib.h>

#include <rte_ethdev.h>

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

  struct rte_mempool *mbuf_pool;
  mempool_init(&mbuf_pool);

  uint16_t port_id = 0;
  unsigned int num_dispatchers = NUM_DISPATCHERS;
  unsigned int num_workers = MAX_WORKERS;

  struct rte_eth_dev_info dev_info;
  int dev_ret = rte_eth_dev_info_get(port_id, &dev_info);
  if (dev_ret == 0 && dev_info.max_rx_queues > 0) {
    if (num_dispatchers > dev_info.max_rx_queues) {
      printf("Notice: Requested %u dispatchers, but port %u only has %u RX queues configured via devargs. Clamping to %u.\n",
             num_dispatchers, port_id, dev_info.max_rx_queues, dev_info.max_rx_queues);
      num_dispatchers = dev_info.max_rx_queues;
    }
  }

  unsigned int total_lcores = rte_lcore_count();
  if (total_lcores < num_dispatchers + num_workers) {
    printf("Notice: Total available lcores = %u (expected %u for %u dispatchers and %u workers)\n",
           total_lcores, num_dispatchers + num_workers, num_dispatchers, num_workers);
    if (total_lcores <= num_dispatchers) {
      rte_exit(EXIT_FAILURE, "Need at least %u lcores (got %u)\n", num_dispatchers + 1, total_lcores);
    }
    num_workers = total_lcores - num_dispatchers;
  }

  pcap_init(port_id, mbuf_pool, num_dispatchers);

  // Initialize SPSC crossbar ring matrix (D x W rings)
  struct rte_ring* disp_rings[NUM_DISPATCHERS][MAX_WORKERS];
  crossbar_rings_init(disp_rings, num_dispatchers, num_workers);

  // Initialize workers
  struct worker_arg workers[MAX_WORKERS];
  workers_init(workers, num_workers, disp_rings, num_dispatchers, &spi, &stop);

  // Initialize dispatchers
  struct dispatcher_arg dispatchers[NUM_DISPATCHERS];
  for (unsigned int d = 0; d < num_dispatchers; d++) {
    dispatchers[d].dispatcher_id = d;
    dispatchers[d].port_id = port_id;
    dispatchers[d].queue_id = d;
    dispatchers[d].num_workers = num_workers;
    for (unsigned int w = 0; w < num_workers; w++) {
      dispatchers[d].rings[w] = disp_rings[d][w];
    }
    dispatchers[d].stop = &stop;
    dispatchers[d].stats.processed = 0;
    dispatchers[d].stats.dropped = 0;
  }

  // Launch remote threads on worker lcores
  unsigned int lcore_id;
  unsigned int d_remote_idx = 1;
  unsigned int w_idx = 0;

  RTE_LCORE_FOREACH_WORKER(lcore_id) {
    if (d_remote_idx < num_dispatchers) {
      if (rte_eal_remote_launch(dispatcher_main, &dispatchers[d_remote_idx], lcore_id) < 0) {
        rte_exit(EXIT_FAILURE, "Failed to launch dispatcher %u on lcore %u\n", d_remote_idx, lcore_id);
      }
      d_remote_idx++;
    } else if (w_idx < num_workers) {
      if (rte_eal_remote_launch(worker_main, &workers[w_idx], lcore_id) < 0) {
        rte_exit(EXIT_FAILURE, "Failed to launch worker %u on lcore %u\n", w_idx, lcore_id);
      }
      w_idx++;
    }
  }

  printf("Pipeline started: %u Dispatchers, %u Workers (%u total SPSC rings)\n",
         num_dispatchers, num_workers, num_dispatchers * num_workers);

  // ========================================================================
  // ============================== start main ==============================
  pthread_t stats_thread;
  struct stats_arg arg = {mbuf_pool, dispatchers, num_dispatchers, workers, num_workers, &stop};
  if (pthread_create(&stats_thread, NULL, stats_thread_main, &arg) != 0) {
    rte_exit(EXIT_FAILURE, "Realtime stats thread creation failed\n");
  }

  // Run Dispatcher 0 on the main lcore
  dispatcher_main(&dispatchers[0]);

  pthread_join(stats_thread, NULL);
  // =============================== end main ===============================
  // ========================================================================

  rte_eal_mp_wait_lcore();

  stats_print(dispatchers, num_dispatchers, workers, num_workers);

  // pcap cleanup
  rte_eth_dev_stop(port_id);
  rte_eth_dev_close(port_id);

  for (unsigned int w = 0; w < num_workers; w++) {
    flow_table_free(&workers[w].ft);
  }

  for (unsigned int d = 0; d < num_dispatchers; d++) {
    for (unsigned int w = 0; w < num_workers; w++) {
      rte_ring_free(disp_rings[d][w]);
    }
  }

  rte_mempool_free(mbuf_pool);

  rte_eal_cleanup();
  printf("All clear!\n");

  return 0;
}
