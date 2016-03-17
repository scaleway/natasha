NATASHA
=======

Natasha is a fast, DPDK powered, stateless IPv4 packet processor.

At startup, natasha reads a configuration file that defines rules. These rules
are processed for each packet received.

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


TODO
====

- Check DPDK initialization: constants have been randomly used everywhere. We
  need to check they are optimal and ideally configurable.
- Return ICMP response if TTL is exceeded.
- stats: show mempools utilization.
- check return value of yylex_init in app_config_load.
