# Performance test

# Description
Test is used to track the performance of the NAT when Performance testing
allows tracking and determining the speed with packet per seconds that are
processed by the NAT software.

The `DUT` is loaded with 65K NAT rules and the `TGen` sends a huge range of
different packets (more details are in the `pktgen-rang.lua` file) at the wire
speed (here 40Gbps with 64bytes packets).

## Topology
For now, this test uses this basic topology described in [RFC2544][1]. We
use VLAN interfaces to be as close as possible to a production like
environment, our actual production environment uses VLANs to filter Ingress
and Egress traffic to our Data centers. The topology (with Vlans) us described
below:
```
+--------------------+------------------+
|                    |                  |
|                   NAT                 |
|                    |                  |
|  NS35              | NS76             |
|                    |                  |   DUT:
|     +--------+     |    +--------+    |       Vlan35: 10.35.0.1/24
|     |        |     |    |        |    |       Vlan76: 10.76.0.1/24
|     | VLan35 |     |    | VLan76 |    |
|     |        |     |    |        |    |
|     +---+----+     |    +---+----+    |
|         |          |        |         |
|         |          |        |         |
|         |      +---+--+     |         |
|         |      |      |     |         |
|         +------+ Eth0 +-----+         |
|                |      |               |
+----------------+---+--+---------------+
                     |
                     |
                     |
                     |
+----------------+---+--+---------------+
|                |      |               |
|         +------+ Eth0 +-----+         |
|         |      |      |     |         |   TGen:
|         |      +--+---+     |         |       Vlan35: 10.35.0.2/24
|         |         |         |         |       Vlan76: 10.76.0.2/24
|         |         |         |         |
|     +---+----+    |    +----+---+     |
|     |        |    |    |        |     |
|     | VLan35 |    |    | VLan76 |     |
|     |        |    |    |        |     |
|     +--------+    |    +--------+     |
|                   |                   |
|  NS35             | NS76              |
|                   |                   |
+-------------------+-------------------+
                  TGen

64K rules are translated
```

Where we can see:
* TGen: a machine used to generate and receive traffic going through NAT using
        Pktgen, a DPDK based traffic generator.
* DUT: the device under test aka the NAT.

## Install dependencies
Clone and build `DPDK` and `Pktgen-DPDK`

## Configuration
Open `pktgen-rang.lua` file and modify the source and destination MAC address.
Open `nat.conf` file and modify the next hop MAC address accordingly.

Prob the `uio` driver from the kernel, then insert the compiled `igb_uio` module from `DPDK` build:
```
insmod $(DPDK_PATH)/x86_64-native-linuxapp-gcc/kmod/igb_uio.ko
```

Bind the target NIC **PCI address** or **name** to `igb_uio` driver using:
```
$(DPDK_PATH)/usertools/dpdk-devbind.py -b igb_uio 0000:04:00.0
```

Now you can now start the traffic generator.

## Running a test

### DUT
Start the NAT using the config file `nat.conf`. Use *one slave only* for
datapath. The goal of this perf test is to determine the perfs per CPU core.
```
$(NATASHA_PATH)/build/nat -l 0,2 --master-lcore=0 -n 4 -w 0000:04:00.0 -- \
                            -f $(NATASHA_PATH)/test/perf/nat.conf
```

### TGEN
Start the traffic generator with a max of cores (pow of 2) using the config
file `pktgen-range.lua` for the packets to send.

```
cd $(PKTGEN_PATH)
root@tgen:$(PKTGEN_PATH)# ./app/x86_64-native-linuxapp-gcc/pktgen -l 0,2,4,6,8 \
                --master-lcore=0 -w 0000:04:00.0 -- -P -T \
                -f $(NATASHA_PATH)/test/perf/pktgen-range.lua -m "[2-8].0"
```

Then you get to the Pktgen dashboard:
```
\ Ports 0-0 of 1   <Main Page>  Copyright (c) <2010-2017>, Intel Corporation
  Flags:Port      :   P-----R--------:0
Link State        :       <UP-40000-FD>     ----TotalRate----
Pkts/s Max/Rx     :                 3/0                   3/0
       Max/Tx     :                 2/0                   2/0
MBits/s Rx/Tx     :                 0/0                   0/0
Broadcast         :                   0
Multicast         :                   0
  64 Bytes        :                   0
  65-127          :                   0
  128-255         :                   0
  256-511         :                   0
  512-1023        :                   0
  1024-1518       :                   0
Runts/Jumbos      :                 0/0
Errors Rx/Tx      :                 0/0
Total Rx Pkts     :                   8
      Tx Pkts     :                   7
      Rx MBs      :                   0
      Tx MBs      :                   0
ARP/ICMP Pkts     :                 0/0
                  :
Pattern Type      :             abcd...
Tx Count/% Rate   :       Forever /100%
PktSize/Tx Burst  :           64 /   64
Src/Dest Port     :         1234 / 5678
Pkt Type:VLAN ID  :     IPv4 / TCP:0001
802.1p CoS        :                   0
ToS Value:        :                   0
  - DSCP value    :                   0
  - IPP  value    :                   0
Dst  IP Address   :         192.168.1.1
Src  IP Address   :      192.168.0.1/24
Dst MAC Address   :   00:00:00:00:00:00
Src MAC Address   :   3c:fd:fe:a5:7c:48
VendID/PCI Addr   :   8086:1583/04:00.0

-- Pktgen Ver: 3.5.0 (DPDK 17.11.0)  Powered by DPDK --------------------------

** Version: DPDK 17.11.0, Command Line Interface with timers
Pktgen:/>
```

When you are in the pktgen CLI, enable vlan support on the ports using:
```
Pktgen:/> enable 0 vlan
```
Then start the traffic:
```
Pktgen:/> start 0
```

## PERFORMANCE RESULTS
The array below resumes the test results depending on the NAT release version.
The bench marks are used on CPU reference and **should** remain on the same
hardware stats for reference a matter:
```
root@tgen:~# lscpu | grep "Model name"
Model name:            Intel(R) Xeon(R) CPU E5-2640 v4 @ 2.40GHz
```
and NIC:
```
root@tgen:~# lspci | grep Eth
04:00.0 Ethernet controller: Intel Corporation Ethernet Controller XL710 for 40GbE QSFP+ (rev 02)
```

The results should be updated for each NATASHA **release tag**.

Release tag | PPS 
--- | ---
v2.1 | 7383070
v2.2 | 8029181
v2.3 | 7414833

## TODO
* Make a script that generate the configuration file for both `pktgen-DPDK` and
  nat configuration using the right MAC addresses according to the machines.

[1]: https://tools.ietf.org/html/rfc2544
