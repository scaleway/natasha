#!/bin/sh

cd $(dirname $0)

./test_bin -c 0x3 -- -f natasha.conf > /dev/null || exit 1
