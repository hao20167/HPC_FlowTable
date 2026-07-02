#ifndef DEMO_H
#define DEMO_H

#include <string.h>
#include <stdint.h>

#include <rte_byteorder.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_tcp.h>
#include <rte_udp.h>

#include "flow_table.h"

// TODO: restrict!!!
// WARN: when to use this
void demo_key(struct flow_key* restrict key, uint64_t i) {
  uint32_t id = i % 1000;
  key->src_ip = 0x0a000001 + id;
  key->dst_ip = 0x08080808;
  key->src_port = 10000 + id;
  key->dst_port = (i % 3 == 0) ? 80 : ((i % 3 == 1) ? 443 : 53);
  key->protocol = (key->dst_port == 53) ? 17 : 6;
  // 17: udp, 6: tcp
}

// WARN: i aint gonna write all of this
int demo_tcp_mbuf(struct rte_mbuf *m, uint64_t i) {
    uint16_t pkt_len =
        sizeof(struct rte_ether_hdr) +
        sizeof(struct rte_ipv4_hdr) +
        sizeof(struct rte_tcp_hdr);

    uint8_t* data = (uint8_t*)rte_pktmbuf_append(m, pkt_len);
    if (data == NULL)
        return -1;

    memset(data, 0, pkt_len);

    struct rte_ether_hdr *eth = (struct rte_ether_hdr *)data;
    eth->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);

    struct rte_ipv4_hdr *ip =
        (struct rte_ipv4_hdr *)(data + sizeof(struct rte_ether_hdr));

    uint32_t flow_id = i % 10000;

    ip->version_ihl = (4 << 4) | 5;
    ip->time_to_live = 64;
    ip->next_proto_id = IPPROTO_TCP;
    ip->total_length = rte_cpu_to_be_16(
        sizeof(struct rte_ipv4_hdr) + sizeof(struct rte_tcp_hdr)
    );

    ip->src_addr = rte_cpu_to_be_32(0x0a000001 + flow_id);
    ip->dst_addr = rte_cpu_to_be_32(0x08080808);

    struct rte_tcp_hdr *tcp =
        (struct rte_tcp_hdr *)((uint8_t *)ip + sizeof(struct rte_ipv4_hdr));

    tcp->src_port = rte_cpu_to_be_16(10000 + (flow_id % 50000));

    if (flow_id % 2 == 0)
        tcp->dst_port = rte_cpu_to_be_16(80);
    else
        tcp->dst_port = rte_cpu_to_be_16(443);

    return 0;
}

#endif
