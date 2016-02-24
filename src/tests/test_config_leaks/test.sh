#!/bin/sh

cd $(dirname $0)

./test_bin -c 0x3 -n 1 --no-huge -- -f natasha.conf > /dev/null || exit 1
