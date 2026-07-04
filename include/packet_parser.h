#ifndef PACKET_PARSER_H
#define PACKET_PARSER_H

#include <stdint.h>

#include <rte_mbuf.h>

#include "flow_table.h"

typedef enum {
  PARSE_OK = 0, 
  PARSE_TOO_SHORT, 
  PARSE_NOT_IPV4, 
  PARSE_UNSUPPORTED, 
  PARSE_FRAGMENTED,
  PARSE_MAX
} parse_result;

extern const char* parse_result_str[PARSE_MAX];

parse_result packet_to_flow_key(struct rte_mbuf* m, struct flow_key* key);

#endif
