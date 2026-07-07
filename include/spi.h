#ifndef SPI_H
#define SPI_H

#include "flow_table.h"
#include "config.h"

#include <stdint.h>

typedef enum {
  SPI_FORWARD = 0,
  SPI_DROP,
  SPI_LOG,
  SPI_COUNT,
  SPI_MAX
} spi_action;

extern const char* spi_action_str[SPI_MAX];

struct spi_rule {
  char name[SPI_RULE_NAME_LEN];
  
  uint8_t any_proto;
  uint8_t protocol;

  uint8_t any_src_ip;
  uint32_t src_ip;

  uint8_t any_dst_ip;
  uint32_t dst_ip;

  uint8_t any_src_port;
  uint16_t src_port;

  uint8_t any_dst_port;
  uint16_t dst_port;

  spi_action action;
};

struct spi_engine {
  struct spi_rule rules[SPI_RULE_LEN];
  uint64_t hits[SPI_RULE_LEN];
  uint8_t num_rule; // WARN: modify this
};

int spi_engine_load(struct spi_engine* engine, const char* path);
struct spi_rule* spi_engine_match(struct spi_engine* engine, const struct flow_key* key);
void spi_engine_init(struct spi_engine *spi);
void spi_engine_stats_print(FILE* fp, const struct spi_engine* engine);

#endif
