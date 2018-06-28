NATASHA configuration and implementation
========================================

Configuration format
--------------------

The natasha configuration file is divided in two parts. The `config` sections
defines the software configuration: ports IPs, VLANs, NAT rules, ...

```
config {
    port 0 ip 10.2.31.11;
    port 1 ip 212.47.255.91;

    # or:
    # port 0 mtu 8192 vlan 10 ip 10.0.0.0
    #                 vlan 11 ip 11.0.0.0;

    # NAT RULES
    nat rule 10.2.0.2 212.47.255.128;
    nat rule 10.2.0.3 212.47.255.129;
}
```

The `rules` section defines what to do for an incoming packet:

```
rules {

    # Rewrite both source and destination using NAT rules defined in the config
    # section
    if (ipv4.src_addr in 10.0.0.0/8 and ipv4.dst_addr in 212.47.0.0/16) {
        nat rewrite ipv4.src_addr;
        nat rewrite ipv4.dst_addr;
        out port 1 mac 7c:0e:ce:25:f3:97;
    }

    # Only rewrite source
    if (ipv4.src_addr in 10.0.0.0/8) {
        nat rewrite ipv4.src_addr;
        out port 1 mac 7c:0e:ce:25:f3:97;
    }

    # Only rewrite destination
    if (ipv4.dst_addr in 212.47.0.0/16) {
        nat rewrite ipv4.dst_addr;
        out port 0 mac 7c:0e:ce:25:f3:97;
    }

    # Also possible:
    # if (vlan 10) { ... }

    print;
    drop; # always drop packets at the end to prevent memory leak
}
```

Each line of the `rules` section is either a *condition* (`if`, `else`) or an
*action*.

Behind the scene, a *condition* or an *action* is a function that returns an
integer. If it returns -1, the following rules are not processed. Such
functions are qualified as **breaking** in this document.

For instance, the **out** action is breaking. This is why in the previous
example an incoming packet is only printed if it is not caught by one of the
three `if` statements.


Configuration parsing
---------------------

A configuration file is parsed with [flex](http://flex.sourceforge.net/) and
[bison](https://www.gnu.org/software/bison/). The [lex
file](src/parseconfig.lex) is straightforward: it reads the file and generates
tokens parsed by bison. The [bison file](src/parseconfig.y) is divided in two
parts:

* the `config` section parsing, which is easy to understand, so we don't
  explain it in this document.
* the `rules` section parsing, which generates an AST.

The AST is stored in the `app_config_node` structure defined in
[natasha.h](src/natasha.h):

```
struct app_config_node {
    struct app_config_node *left;
    struct app_config_node *right;

    enum {
        NOOP, // Unused
        ACTION, // Execute the action
        SEQ, // Execute both left and right
        IF, // Execute right if left is false
        COND, // Execute right if left is true
        AND, // True if left and right are true
        OR, // True if left or right is true
    } type;

    int (*action)(struct rte_mbuf *pkt, uint8_t port, struct core *core,
                  void *data);
    void *data;
};
```

The following configuration:

```
rules {
    if (ipv4.src_addr in 10.0.0.0/8 and ipv4.dst_addr in 212.47.0.0/16) {
       nat rewrite ipv4.src_addr;
       out port 0 mac 7c:0e:ce:25:f3:97;
    } else {
       out port 1 mac 7c:0e:ce:25:f3:97;
    }
    print;
}
```

Is represented as the following AST:

```
                             SEQ
                            /   \
                          IF     ACTION[action=action_print, data=NULL]
                        /    \
                    COND      ACTION[action=action_out, data=struct out_packet(port 1)]
                   /    \_________________________________________________________________
                  AND                                                                     |
                /    \______________________________________________________________      |
ACTION[action=cond_ipv4_src_in_network, data=struct ipv4_network(10.0.0.0/8)]       |     |
                                                                                    |     |
ACTION[action=cond_ipv4_src_in_network, data=struct ipv4_network(212.47.0.0/16)] ___|     |
                                                                                          |
                   _______________________________________________________________________|
                   |
                  SEQ
                 /   \______________________________________________
  ACTION[action=action_nat_rewrite, data=int *: IPV4_SRC_ADDR]      |
                                                                    |
 ___________________________________________________________________|
 |
 |__ ACTION[action=action_out, data=struct out_packet(port 0)]
```


Configuration execution
-----------------------

Rules are processed recursively by `process_rules()` in [ipv4.c](src/ipv4.c).
At any time, if `node->action` (set for ACTION nodes to execute actions and
conditions) returns -1, we stop processing the tree.

NATASHA application statistics
------------------------------

There are some application statistics used for debug perpose, they are defined:
```
struct nat_stats {
    uint64_t drop_no_rule;
    uint32_t drop_nat_condition;
    uint32_t drop_bad_l3_cksum;
    uint32_t rx_bad_l4_cksum;
    uint32_t drop_unknown_icmp;
    uint32_t drop_unhandled_ethertype;
    uint32_t drop_tx_notsent;
};
```

The definition of each stat is:

* **drop_nat_condition**: means that the input packet doesn't match the
configuration expected and used by the admin (the subnets). So the rule `drop;`
in the configuration increments this stat.
* **drop_no_rule**: means that there is no nat rule for the input packet.
* **drop_tx_notsent**: the NIC could no send the packet so its dropped to prevent mem leaks
* **drop_ba_l3_cksum**: the RX packet has a bad ip checksum so it's dropped.
* **rx_bad_l4_cksum**: the RX packet has a bad udp or tcp checksum.
* **drop_unknown_ethertype**: drop packet diffrent from ipv4 or arp.
* **drop_unknown_icmp**: the nat received un icmp message different from `echo`
