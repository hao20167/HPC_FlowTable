#include "traffics.h"

#include <netinet/in.h>

const char* traffic_type_str[TRAFFIC_MAX] = {
  "http",
  "https",
  "dns",
  "tcp",
  "udp",
  "other"
};

traffic_type get_traffic_type_from_flow_key(struct flow_key *key) {
  if (key->protocol == IPPROTO_TCP) {
    if (key->dst_port == 80) return TRAFFIC_HTTP;
    if (key->dst_port == 443) return TRAFFIC_HTTPS;
    return TRAFFIC_TCP;
  }
  if (key->protocol == IPPROTO_UDP) {
    if (key->dst_port == 53) return TRAFFIC_DNS;
    return TRAFFIC_UDP;
  }
  return TRAFFIC_OTHER;
}
