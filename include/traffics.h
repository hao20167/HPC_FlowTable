#ifndef TRAFFIC_TYPES_H
#define TRAFFIC_TYPES_H

#include "flow_table.h"

#include <stdint.h>

typedef enum {
  TRAFFIC_HTTP = 0,
  TRAFFIC_HTTPS,
  TRAFFIC_DNS,
  TRAFFIC_TCP,
  TRAFFIC_UDP,
  TRAFFIC_OTHER,
  TRAFFIC_MAX
} traffic_type;

extern const char* traffic_type_str[TRAFFIC_MAX];

traffic_type get_traffic_type_from_flow_key(struct flow_key *key);

#endif
