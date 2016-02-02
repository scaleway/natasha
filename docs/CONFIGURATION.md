NATASHA configuration and implementation
========================================

Configuration format
--------------------

The natasha configuration file is divided in two parts. The `config` sections
defines the software configuration:

```
config {
    port 0 ip 10.2.31.11;
    port 1 ip 212.47.255.91;

    # Also possible:
    # port 0 vlan 10 ip 10.0.0.0;
    # or
    # port 0 ip 10.0.0.0 vlan 10 ip 10.0.0.1 vlan 11 ip 10.0.0.2;
    #
    # MTU can be set with:
    # port 0 mtu 8192 ...

    # NAT RULES
    nat rule 10.2.0.2 212.47.255.128;
    nat rule 10.2.0.3 212.47.255.129;
}
```

And the `rules` section defines how to transform a packet:

```
rules {

    # Rewrite both source and destination
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
}
```

Each line of the `rules` section is either a *condition* (`if`, `else`) or an
*action*.

Behind the scene, a *condition* or an *action* is a function that returns an
integer. If it returns -1, the following rules are not processed. Such
functions are qualified as **breaking** in this document.

For instance, the **out** action is breaking. This is why an incoming packet is
only printed if it is not caught by one of the three `if` statements of the
configuration example above.


Configuration parsing
---------------------

A configuration file is parsed with [flex](http://flex.sourceforge.net/) and
[bison](https://www.gnu.org/software/bison/). The [lex
file](src/parseconfig.lex) is straightforward: it parses the file and generates
tokens parsed by bison. The [bison file](src/parseconfig.y) is divided in two
parts:

* the `config` section parsing, which is easy to understand and hack and won't
  be discussed here.
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

There's nothing better than a schema. The following configuration:

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

Rules in [parseconfig.y](src/parseconfig.y) generate this AST.


Configuration execution
-----------------------

Rules are processed recursively by `process_rules()` in [ipv4.c](src/ipv4.c).
At any time, if `node->action` (set for ACTION nodes to execute actions and
conditions) returns -1, we stop processing the tree.
