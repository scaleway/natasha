#include "natasha.h"
#include "network_headers.h"


int
action_print(struct rte_mbuf *pkt, uint8_t port, struct core *core, void *data)
{
    const struct ipv4_hdr *ipv4_hdr = ipv4_header(pkt);
    const int src_addr = rte_be_to_cpu_32(ipv4_hdr->src_addr);
    const int dst_addr = rte_be_to_cpu_32(ipv4_hdr->dst_addr);

    RTE_LOG(DEBUG, APP,
            "Port %i: packet on core %i from " IPv4_FMT " to " IPv4_FMT "\n",
            port, core->id, IPv4_FMTARGS(src_addr), IPv4_FMTARGS(dst_addr));

    switch (ipv4_hdr->next_proto_id) {
        case IPPROTO_TCP: {
            struct tcp_hdr *tcp_hdr;

            tcp_hdr = tcp_header(pkt);
            RTE_LOG(DEBUG, APP,
                    "\t>>> TCP packet, src port: %i, dst port: %i, tcp flags: %#x\n",
                    rte_be_to_cpu_16(tcp_hdr->src_port),
                    rte_be_to_cpu_16(tcp_hdr->dst_port),
                    tcp_hdr->tcp_flags);
            break ;
        }
        case IPPROTO_UDP: {
            struct udp_hdr *udp_hdr;

            udp_hdr = udp_header(pkt);
            RTE_LOG(DEBUG, APP,
                    "\t>>> UDP packet, src port: %i, dst port: %i\n",
                    rte_be_to_cpu_16(udp_hdr->src_port),
                    rte_be_to_cpu_16(udp_hdr->dst_port));
            break ;
        }
        case IPPROTO_ICMP: {
            struct icmp_hdr *icmp_hdr;

            icmp_hdr = icmp_header(pkt);
            RTE_LOG(DEBUG, APP, "\t>>> ICMP packet, type: %#x, code: %#x\n",
                    icmp_hdr->icmp_type, icmp_hdr->icmp_code);
            break ;
        }
        default:
            RTE_LOG(DEBUG, APP,
                    "\t>>> Not TCP/UDP/ICMP - ipv4.next_proto_id=%#x\n",
                    ipv4_hdr->next_proto_id);
            break ;
    }

    return 0;
}
