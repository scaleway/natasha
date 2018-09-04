#!/usr/bin/env bash
# -*- encoding: utf-8 -*-

set -x

echo "Starting TestNode configuration..."

ip l s {{ conf['tn']['ns35']['lower_dev'] }} up

# Add Vlan interfaces
ip l add link {{ conf['tn']['ns35']['lower_dev'] }} \
  name {{ conf['tn']['ns35']['dev'] }} type vlan \
    id {{ conf['tn']['ns35']['vlanid'] }}

ip l add link {{ conf['tn']['ns76']['lower_dev'] }} \
  name {{ conf['tn']['ns76']['dev'] }} type vlan \
  id {{ conf['tn']['ns76']['vlanid'] }}

# configure Vlan 35
ip addr add {{ conf['tn']['ns35']['ip_priv'] }}/24 \
  dev {{ conf['tn']['ns35']['dev'] }}

ip l s {{ conf['tn']['ns35']['dev'] }} up

ip route add {{ conf['tn']['ns35']['ip_rmt'] }}/32 \
  via {{ conf['tn']['ns35']['ip_nh'] }} dev {{ conf['tn']['ns35']['dev'] }}

# configure Vlan 76
ip addr add {{ conf['tn']['ns76']['ip_priv'] }}/24 \
  dev {{ conf['tn']['ns76']['dev'] }}

ip l s {{ conf['tn']['ns76']['dev'] }} up

ip route add {{ conf['tn']['ns76']['ip_rmt'] }}/32 \
  via {{ conf['tn']['ns76']['ip_nh'] }} dev {{ conf['tn']['ns76']['dev'] }}

sleep 1

ip addr sh {{ conf['tn']['ns35']['dev'] }}
ip r
ip addr sh {{ conf['tn']['ns76']['dev'] }}
ip r

echo "Configuration Done."
