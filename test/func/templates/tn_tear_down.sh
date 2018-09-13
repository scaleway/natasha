#!/usr/bin/env bash
# -*- encoding: utf-8 -*-

set -x
set -e

echo "Starting TestNode tear down..."

ip l s {{ conf['tn']['ns35']['dev'] }} down
ip l s {{ conf['tn']['ns76']['dev'] }} down
ip l del link name {{ conf['tn']['ns35']['dev'] }}
ip l del link name {{ conf['tn']['ns76']['dev'] }}

echo "Tear down Done."
