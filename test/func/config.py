#!/usr/bin/env python
# -*- encoding: utf-8 -*-
"""
This file gathers the constants and static functions
"""

from tests import *

# Available unit test
UNIT_TESTS = {
    'icmp': {
        'name': 'ICMP',
        'class': ICMPTest,
        'bpfilter': 'icmp',
        'count': 10,
        'local_sniff': True,
        'payload': 'ICMP packet to NAT',
        'description': 'Generate ICMP traffic and check the replys'
    },
    'tcp': {
        'name': 'TCP',
        'class': TCPTest,
        'bpfilter': 'tcp',
        'count': 10,
        'local_sniff': False,
        'payload': 'TCP packet to NAT',
        'description': 'Generate TCP traffic and check if it\'s correctly NATted'
    },
    'udp': {
        'name': 'UDP',
        'class': UDPTest,
        'bpfilter': 'udp or icmp',
        # Send a small amout of packets to prevent kernel from stop sending
        # ICMP port unreachable after beeing flooded.
        'count': 5,
        'local_sniff': False,
        'payload': 'UDP packet to NAT',
        'description': 'Generate UDP traffic and check if it\'s correctly NATted'
    },
    'udp-df': {
        'name': 'UDP DF',
        'class': UDPDFTest,
        'bpfilter': 'udp or icmp',
        # Send a small amout of packets to prevent kernel from stop sending
        # ICMP port unreachable after beeing flooded.
        'count': 5,
        'local_sniff': False,
        'payload': 'UDP DF packet to NAT',
        'description': 'Generate UDP with IP DF flag traffic and check if it\'s correctly NATted'
    },
    'udp-frag': {
        'name': 'UDP fragmented',
        'class': UDPFragTest,
        'bpfilter': 'udp or icmp',
        # Send a small amout of packets to prevent kernel from stop sending
        # ICMP port unreachable after beeing flooded.
        'count': 1,
        'local_sniff': False,
        'payload': 'A' * 1472 + 'B' * 1472 + 'C' * 1472,
        'description': 'Generate fragmented UDP traffic and check if it\'s correctly NATted'
    },
    'udp-zero-csum': {
        'name': 'UDP Zero checksum',
        'class': UDPZeroCsumTest,
        'bpfilter': 'udp or icmp',
        'count': 3,
        'local_sniff': False,
        'payload': 'UDP Zero checksum to NAT',
        'description': 'Generate UDP traffic with checksum to set to 0 and check if it remains 0 after being natted'
    },
    'tcp-frag': {
        'name': 'TCP fragmented',
        'class': TCPFragTest,
        'bpfilter': 'tcp',
        'count': 1,
        'local_sniff': False,
        'payload': 'A' * 1450 + 'B' * 1450 + 'C' * 1450,
        'description': 'Generate fragmented TCP traffic and check if it\'s correctly NATted'
    },
    'icmp-frag': {
        'name': 'ICMP fragmented',
        'class': ICMPFragTest,
        'bpfilter': 'icmp',
        'count': 1,
        'local_sniff': True,
        'payload': 'A' * 1450 + 'B' * 1450 + 'C' * 1450,
        'description': 'Generate fragmented ICMP traffic and check if it\'s correctly NATted'
    },
    'arp': {
        'name': 'ARP request',
        'class': ARPTest,
        'bpfilter': 'arp',
        'count': 1,
        'local_sniff': True,
        'payload': '',
        'description': 'Generate an ARP request and check if it\'s correctly replying'
    },
    'traceroute': {
        'name': 'TraceRoute simulation',
        'class': TraceRouteTest,
        'bpfilter': 'icmp',
        'count': 1,
        'local_sniff': False,
        'payload': 'UDP over ICMP packet to NAT',
        'description': 'Generate an UDP over ICMP traffic and check if the'
                       'inner and outer packets are correctly NATted'
    },
}

# Main configuration file
CONFIG_FILE = "config.json"

SETUP_FILES = {
    './deliveries/tn/tn_setup.sh':      0744,
    './deliveries/tn/tn_tear_down.sh':  0744,
    './deliveries/dut/nat.conf':        0644,
}


def print_topology():
    """
    print the topology to setup with this script
    """

    return '''
                DUT
+--------------------+------------------+
|                    |                  |
|              -- NAT                     |
|                    |                  |
|  NS35              | NS76             |
|                    |                  |   DUT:
|     +--------+     |    +--------+    |       Vlan35: 10.35.0.1/24
|     |        |     |    |        |    |       Vlan76: 10.76.0.1/24
|     | VLan35 |     |    | VLan76 |    |
|     |        |     |    |        |    |
|     +---+----+     |    +---+----+    |
|         |          |        |         |
|         |          |        |         |
|         |      +---+--+     |         |
|         |      |      |     |         |
|         +------+ Eth0 +-----+         |
|                |      |               |
+----------------+---+--+---------------+
                     |
                     |
                     |
                     |
+----------------+---+--+---------------+
|                |      |               |
|         +------+ Eth0 +-----+         |
|         |      |      |     |         |   TestNode:
|         |      +--+---+     |         |       Vlan35: 10.35.0.2/24
|         |         |         |         |       Vlan76: 10.76.0.2/24
|         |         |         |         |
|     +---+----+    |    +----+---+     |
|     |        |    |    |        |     |
|     | VLan35 |    |    | VLan76 |     |
|     |        |    |    |        |     |
|     +--------+    |    +--------+     |
|                   |                   |
|  NS35             | NS76              |
|                   |                   |
+-------------------+-------------------+
                TestNode

Public @20.35.0.1 is translated to Private @10.35.0.2
Public @20.76.0.1 is translated to Private @10.76.0.2
    '''
