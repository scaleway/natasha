#!/bin/sh

#
# Run ./bin for each *.conf, and compare it's output against the expect file.
#

cd $(dirname $0)

error() {
    echo ">>> Fail on test ${f}: $(dirname $0)/${out} != $(dirname $0)/${expect}" >&2
    exit 1
}

for f in *.conf; do
    expect=$(echo "$f" | sed 's/\.conf/\.expect/')
    out=$(echo "$f" | sed 's/\.conf/\.out/')

    ./bin -c 0x3 -n 1 --no-huge -- -f ${f} > ${out}

    cat ${out} | grep ^EXPECT: | sed 's/^EXPECT:\s*//g' | diff -B - ${expect} \
        || error
done
