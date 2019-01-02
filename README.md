# NATASHA

## Introduction

Natasha is a fast and scalable, **DPDK** powered, stateless **NAT44** packet
processor. It can achieve 100Gbits/s translation with 64bytes packets.

Natasha supports DPDK `v18.02` stable.

## Features

* NAT `IPv4`/`TCP`/`UDP`/`UDPLite` traffic.
* Handle fragmented packets.
* Handle inner `IPv4` packet inside various `ICMP` messages.
* Reply to `ICMP Echo` requests in both the public and private interface.
* `VLAN` offload (`Tx`/`Rx` and filtering).
* `L3`/`L4` Rx checksum offload for stats purpose.
* `L3`/`L4` Tx checksum using hardware offload or software incremental update
  ([RFC1624](https://tools.ietf.org/html/rfc1624)) depending on the case.
* Various `stats`, Software and Hardware stats and per core.
* Dumping release version with the commit id.
* Test directory `test` with a bench of functional and performance tests.

## TODO list

* Replace `AST` lookup with `LPM`.
* Rewrite configuration data structures.
* Write a proper `L2` stack with `ARP` handling.
* Return `ICMP` response if `TTL` is exceeded.

## Configuration

At startup, `NATASHA` reads a configuration file that defines rules. These
rules are processed for each packet received.

A configuration file looks like:

```
if (ipv4.dst_addr in 10.0.0.0/8
    or vlan 64
    or ipv4.dst_addr in 192.168.0.0/16) {

    drop ;

} else {
    out port 0 mac de:ad:be:ef:ff:ff;
}
```
More concrete examples are given in the [documentation](docs/CONFIGURATION.md).

## Installation

* Get DPDK stable sources and checkout on `v18.02` version:
```
git clone git://dpdk.org/dpdk-stable && cd $PATH_TO_DPDK_SOURCES
git checkout v18.02
```
* Configure and build DPDK (see
[Documentation](http://doc.dpdk.org/guides/linux_gsg/)).
* Then make sure that your RTE_SDK path is defined.
* Finally build Natasha:
```
cd NATASHA_SOURCES_PATH
make
```

Run Natasha like any DPDK app and add application specific param `-f` with the
right configuration file:
```
$(NATASHA_SOURCES_PATH)/build/nat -l 0,2 --master-lcore=0 -n 4 -w 0000:04:00.0 -- \
                            -f $NATASHA_CONFIG_FILE
```
You can check the configuration files in the CI for more examples.

## Tests

Natasha has a small CI process with a functional and performance tests, checkout
[functional](test/func/README.md) and [performance](test/perf/README.md)
documentation for more informations.

## Contributing and Bugs report

Any contribution or bug report are more than welcome :heart:, please make sure
to details the bug/feature request.

## License

Natasha is Free Software (learn more:
http://www.gnu.org/philosophy/free-sw.html).

Natasha is released under the GPLv3 License. Please read the [COPYING](COPYING)
file for details.
