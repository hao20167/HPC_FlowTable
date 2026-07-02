#include "packet_parser.h"
#include "flow_table.h"

#include <rte_byteorder.h>
#include <rte_mbuf_core.h>
#include <rte_mbuf.h>
#include <rte_ether.h>
#include <rte_ip4.h>
#include <rte_tcp.h>
#include <rte_udp.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

const char* parse_result_str[PARSE_MAX] = {
  "OK",
  "TOO_SHORT",
  "NOT_IPV4",
  "UNSUPPORTED",
  "FRAGMENTED"
};

// [ Ethernet header ][ IPv4 header ][ TCP/UDP header ][ payload ]
// l3_offset = ethernet header length
// l4_offset = l3_offset + ipv4 header length (ihl)
// version_ihl = [ version 4 bits ][ IHL 4 bits words ] // (1 words = 4 bytes) 
// IHL (bytes) = IHL * 4
parse_result packet_to_flow_key(struct rte_mbuf* mbuf, struct flow_key* key) {
  if (mbuf == NULL || key == NULL) return PARSE_TOO_SHORT;
  memset(key, 0, sizeof(*key));
  uint16_t len = rte_pktmbuf_data_len(mbuf);
  uint8_t* data = rte_pktmbuf_mtod(mbuf, uint8_t*);
  
  const uint16_t l3_offset = sizeof(struct rte_ether_hdr);
  if (len < l3_offset) return PARSE_TOO_SHORT;

  struct rte_ether_hdr* ether = (struct rte_ether_hdr*)data;
  if (rte_be_to_cpu_16(ether->ether_type) != RTE_ETHER_TYPE_IPV4)
    return PARSE_NOT_IPV4;

  if (len < l3_offset + sizeof(struct rte_ipv4_hdr))
    return PARSE_TOO_SHORT;

  struct rte_ipv4_hdr* ipv4 = (struct rte_ipv4_hdr*)(data + l3_offset);
  if ((ipv4->version_ihl >> 4) != 4) return PARSE_NOT_IPV4;
  uint8_t ihl = (ipv4->version_ihl & 0x0f) * 4;

  if (ihl < sizeof(struct rte_ipv4_hdr))
    return PARSE_TOO_SHORT;

  uint16_t l4_offset = l3_offset + ihl;
  if (len < l4_offset) return PARSE_TOO_SHORT;

  // only the first fragment (offset = 0), has the fully TCP/UDP header
  // then how to forward 
  if ((rte_be_to_cpu_16(ipv4->fragment_offset) & 0x1fff) != 0)
    return PARSE_FRAGMENTED;

  key->src_ip = rte_be_to_cpu_32(ipv4->src_addr);
  key->dst_ip = rte_be_to_cpu_32(ipv4->dst_addr);
  key->protocol = ipv4->next_proto_id;

  if (key->protocol == IPPROTO_TCP) {
    if (len < l4_offset + sizeof(struct rte_tcp_hdr))
      return PARSE_TOO_SHORT;

    struct rte_tcp_hdr* tcp = (struct rte_tcp_hdr*)(data + l4_offset);
    key->src_port = rte_be_to_cpu_16(tcp->src_port);
    key->dst_port = rte_be_to_cpu_16(tcp->dst_port);
    
    return PARSE_OK;
  } 
  if (key->protocol == IPPROTO_UDP) {
    if (len < l4_offset + sizeof(struct rte_udp_hdr))
      return PARSE_TOO_SHORT;

    struct rte_udp_hdr* udp = (struct rte_udp_hdr*)(data + l4_offset);
    key->src_port = rte_be_to_cpu_16(udp->src_port);
    key->dst_port = rte_be_to_cpu_16(udp->dst_port);

    return PARSE_OK;
  } 

  // ICMP...
  key->src_port = 0;
  key->dst_port = 0;

  return PARSE_OK;
}
