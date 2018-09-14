#NATASHA

## Introduction
Natasha is a fast, **DPDK** powered, stateless **NAT44** packet processor. It
supports DPDK `v18.02` stable.

## Features

* NAT `IPv4`/`TCP`/`UDP`/`UDPLite` traffic.
* Handle fragmented packets.
* Handle inner `IPv4` packet inside various `ICMP` messages.
* Reply to `ICMP Echo` requests in both the public and private interface.
* `VLAN` offload (`Tx`/`Rx` and filtering).
* `L3`/`L4` Rx checksum offload for stats purpose.
* `L3`/`L3` Checksum computation using incremental update ([RFC1624](https://tools.ietf.org/html/rfc1624)).
* Various `stats`, Software and Hardware stats and per core.
* Dumping release number with the commit id.
* Test directory `test` with a bench of functional and performance tests.

## TODO list

* Replace `AST` lookup with `LPM`.
* Rewrite configuration data structures.
* Write a proper `L2` stack with `ARP` handling.
* Return `ICMP` response if `TTL` is exceeded.
* Raise error if out on non-configured port.

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

## License

Natasha is Free Software (learn more:
http://www.gnu.org/philosophy/free-sw.html).

Natasha is released under the GPLv3 License. Please read the [COPYING](COPYING)
file for details.
