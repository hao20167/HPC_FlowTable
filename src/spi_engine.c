#include "spi_engine.h"
#include "flow_table.h"

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <stdio.h>

const char* spi_action_str[SPI_MAX] = {
  "FORWARD",
  "DROP",
  "LOG",
  "COUNT"
};

static void trim_newline(char *s) {
  size_t n = strlen(s);
  while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) {
    s[n - 1] = '\0';
    n--;
  }
}

static int parse_protocol(const char* s, uint8_t* any, uint8_t* proto) {
  if (strcmp(s, "*") == 0) {
    *any = 1;
    *proto = 0;
    return 0;
  }

  *any = 0;
  if (strcasecmp(s, "TCP") == 0) {
    *proto = IPPROTO_TCP;
    return 0;
  }
  if (strcasecmp(s, "UDP") == 0) {
    *proto = IPPROTO_UDP;
    return 0;
  }
  if (strcasecmp(s, "ICMP") == 0) {
    *proto = IPPROTO_ICMP;
    return 0;
  }

  return -1;
}

static int parse_ip(const char* s, uint8_t* any, uint32_t* ip) {
  if (strcmp(s, "*") == 0) {
    *any = 1;
    *ip = 0;
    return 0;
  }

  struct in_addr addr;
  if (inet_pton(AF_INET, s, &addr) != 1) return -1;
  *any = 0;
  *ip = ntohl(addr.s_addr);

  return 0;
}

static int parse_port(const char* s, uint8_t* any, uint16_t* port) {
  if (strcmp(s, "*") == 0) {
    *any = 1;
    *port = 0;
    return 0;
  }

  long v = strtol(s, NULL, 10);
  if (v < 0 || v > 65535) return -1;
  *any = 0;
  *port = (uint16_t)v;

  return 0;
}

static int parse_action(const char* s, spi_action* action) {
  if (strcasecmp(s, "FORWARD") == 0) {
    *action = SPI_FORWARD;
    return 0;
  }
  if (strcasecmp(s, "DROP") == 0) {
    *action = SPI_DROP;
    return 0;
  }
  if (strcasecmp(s, "LOG") == 0) {
    *action = SPI_LOG;
    return 0;
  }
  if (strcasecmp(s, "COUNT") == 0) {
    *action = SPI_COUNT;
    return 0;
  }

  return -1;
}

int spi_engine_load(struct spi_engine* engine, const char* path) {
  memset(engine, 0, sizeof(*engine));

  FILE* fp = fopen(path, "r");
  if (fp == NULL) {
    perror("fopen rules.cfg");
    return -1;
  }

  char line[512];

  while (fgets(line, sizeof(line), fp) != NULL) {
    trim_newline(line);
    if (line[0] == '\0' || line[0] == '#') continue;

    if (engine->num_rule >= SPI_RULE_LEN) {
      fprintf(stderr, "Too many SPI rules\n");
      fclose(fp);
      return -1;
    }

    char* save = NULL;
    char* name = strtok_r(line, ",", &save);
    char* proto = strtok_r(NULL, ",", &save);
    char* src_ip = strtok_r(NULL, ",", &save);
    char* dst_ip = strtok_r(NULL, ",", &save);
    char* src_port = strtok_r(NULL, ",", &save);
    char* dst_port = strtok_r(NULL, ",", &save);
    char* action = strtok_r(NULL, ",", &save);

    if (!name || !proto || !src_ip || !dst_ip || !src_port || !dst_port || !action) {
      fprintf(stderr, "Invalid rule\n");
      continue;
    }

    struct spi_rule* r = &engine->rules[engine->num_rule];

    snprintf(r->name, sizeof(r->name), "%s", name);

    if (parse_protocol(proto, &r->any_proto, &r->protocol) < 0 ||
        parse_ip(src_ip, &r->any_src_ip, &r->src_ip) < 0 ||
        parse_ip(dst_ip, &r->any_dst_ip, &r->dst_ip) < 0 ||
        parse_port(src_port, &r->any_src_port, &r->src_port) < 0 ||
        parse_port(dst_port, &r->any_dst_port, &r->dst_port) < 0 ||
        parse_action(action, &r->action) < 0) {
      fprintf(stderr, "Failed to parse rule: %s\n", name);
      continue;
    }

    engine->num_rule++;
  }

  fclose(fp);
  printf("Loaded %u SPI rules\n", engine->num_rule);

  return 0;
}

struct spi_rule* spi_engine_match(struct spi_engine* engine, const struct flow_key* key) {
  for (size_t i = 0; i < engine->num_rule; i++) {
    struct spi_rule* r = &engine->rules[i];

    if (!r->any_proto && r->protocol != key->protocol) continue;
    if (!r->any_src_ip && r->src_ip != key->src_ip) continue;
    if (!r->any_dst_ip && r->dst_ip != key->dst_ip) continue;
    if (!r->any_src_port && r->src_port != key->src_port) continue;
    if (!r->any_dst_port && r->dst_port != key->dst_port) continue;

    engine->hits[i]++;
    return r;
  }

  return NULL;
}

void spi_engine_stats_print(const struct spi_engine* engine) {
  printf("\n=== SPI Rule Hits ===\n");
  for (size_t i = 0; i < engine->num_rule; i++) {
    const struct spi_rule* r = &engine->rules[i];
    printf("%-16s action=%-8s hits=%lu\n", r->name, spi_action_str[r->action], engine->hits[i]);
  }
  printf("\n=== SPI Rule Hits ===\n");
}


