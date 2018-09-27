# NTF
NTF (Natasha Test Framework) is a micro framework used to test Natasha
stack. It gathers the basic tests and can be extended to support many
complex scenarios of test. This documentation explains how to setup and start
a test then there is a section explaining how to write a new test.

## Available tests
```
(.venv) root@tgen:~/nat/test/func# ntfc -l
Available Tests to play
+-----------------------+------------------------------------------------------------------------------------------------+
|          Test         |                                          Description                                           |
+-----------------------+------------------------------------------------------------------------------------------------+
|   UDP Zero checksum   |  Generate UDP traffic with checksum to set to 0 and check if it remains 0 after being natted   |
|      ARP request      |                  Generate an ARP request and check if it's correctly replying                  |
|          UDP          |                    Generate UDP traffic and check if it's correctly NATted                     |
| TraceRoute simulation | Generate an UDP over ICMP traffic and check if theinner and outer packets are correctly NATted |
|     UDP fragmented    |               Generate fragmented UDP traffic and check if it's correctly NATted               |
|         UDP DF        |            Generate UDP with IP DF flag traffic and check if it's correctly NATted             |
|          ICMP         |                           Generate ICMP traffic and check the replys                           |
|     TCP fragmented    |               Generate fragmented TCP traffic and check if it's correctly NATted               |
|          TCP          |                    Generate TCP traffic and check if it's correctly NATted                     |
|    ICMP fragmented    |              Generate fragmented ICMP traffic and check if it's correctly NATted               |
+-----------------------+------------------------------------------------------------------------------------------------+
```

## Topology
For now, this test uses this basic topology described in RFC2544 LINKJ YAW.  We
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
|         |      |      |     |         |   TestNode:
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
                TestNode(TN)

Public @20.35.0.1 is translated to Private @10.35.0.2
Public @20.76.0.1 is translated to Private @10.76.0.2
```

Where we can see:
* TestNode: a machine used to generate and receive traffic going through NAT.
* DUT: the device under test aka the NAT.

## Install dependencies
Before going any further in this documentation, make sure you have `python` and
`pip` installed in you environment.
Since this project depends on many external `python` packages, use this command
to generate the proper environment using python `virtualenv`
```
make venv
. .venv/bin/activate
```
Make sure you are in the virtualenv, then you can use the instructions below.

## Configuration

There is a configuration file in `json` format `config.json` that gathers all
the information about the topology and its network configuration. This file is
use to setup the generated packets and to validate the responses.  You have to
generate this file using a script `gen_conf` because the network interface and
the mac addresses depends on the physical machine you are running under.
The content of `config.json` again is used to render setup script under
`templates` directory.
To generate this configuration use:
```
(.venv) root@tgen:~/natv2/test# gen_conf -h
usage: gen_conf [-h] -i TN_IFACE -t TN_MAC -d DUT_MAC [-v]

optional arguments:
  -h, --help            show this help message and exit
  -i TN_IFACE, --tn-iface TN_IFACE
                        TestNode physical interface used for Vlans
  -t TN_MAC, --tn-mac TN_MAC
                        TestNode physical interface MAC address
  -d DUT_MAC, --dut-mac DUT_MAC
                        DUT physical interface MAC address
  -v, --version         show program's version number and exit

(.venv) root@tgen:~/natv2/test# gen_conf -i enp4s0f0 -t 3c:fd:fe:a5:7c:48 \
                                                     -d 3c:fd:fe:a5:80:98
configuration files generated in "deliveries"
```

Now the configuration scripts are generated with the given informations
(network device and mac addresses)
Use the scripts generated from `tempates` under the directory `deliveries` to
setup your Test-bed like the topology above:
* `tn_setup.sh`: script used to setup the network configuration above.
* `tn_tear_down.sh`: script used to unconfigure.
* `nat.conf`: file use to start `Natasha` on `DUT` (param of `-f` option).

## Running a test
The command `ntfc` for NTF command is used to run the tests. You can use `ntfc
-h` to show the help.
```
(.venv) root@tgen:~/natv2/test# ntfc -h
usage: ntfc [-h] [-t [TEST [TEST ...]]] [-d] [-l] [-v]

optional arguments:
  -h, --help            show this help message and exit
  -t [TEST [TEST ...]], --test [TEST [TEST ...]]
                        The unit test list to play, default all
  -d, --debug           Use Debug log level
  -l, --list            Display available tests to run
  -v, --version         show program's version number and exit
```
You can run a list of tests using the `-t` argument like a `python` list.
In the example below, we'll run `ICMP` and `TCP` test two times:
```
(.venv) root@tgen:~/natv2/test# ntfc -t icmp tcp icmp tcp -d
2018-09-04 09:46:39 tgen func[24727] INFO Running ['icmp', 'tcp', 'icmp', 'tcp'] tests on the topology:
                DUT
+--------------------+------------------+
|                    |                  |
|              -- NAT                     |
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
|         |      |      |     |         |   TestNode:
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
                TestNode

Public @20.35.0.1 is translated to Private @10.35.0.2
Public @20.76.0.1 is translated to Private @10.76.0.2

2018-09-04 09:46:39 tgen func[24727] INFO Starting ICMP test
2018-09-04 09:46:39 tgen func[24727] DEBUG From namespace ns35 sending:
Ether / IP / ICMP 10.35.0.2 > 20.76.0.1 echo-request 0
2018-09-04 09:46:39 tgen func[24727] DEBUG Start sniffing on iface "vlan35" with bpfilter: "icmp"
2018-09-04 09:46:41 tgen func[24727] DEBUG Sending 10 packets...
2018-09-04 09:46:41 tgen func[24727] DEBUG Sending done.
2018-09-04 09:46:42 tgen func[24727] DEBUG Sniffing stoped, 20 packets have been captured
2018-09-04 09:46:42 tgen func[24727] DEBUG Received packets have a correct layer2 and layer3 informations
2018-09-04 09:46:42 tgen func[24727] DEBUG From namespace ns76 sending:
Ether / IP / ICMP 10.76.0.2 > 20.35.0.1 echo-request 0
2018-09-04 09:46:42 tgen func[24727] DEBUG Start sniffing on iface "vlan76" with bpfilter: "icmp"
2018-09-04 09:46:44 tgen func[24727] DEBUG Sending 10 packets...
2018-09-04 09:46:44 tgen func[24727] DEBUG Sending done.
2018-09-04 09:46:45 tgen func[24727] DEBUG Sniffing stoped, 20 packets have been captured
2018-09-04 09:46:45 tgen func[24727] DEBUG Received packets have a correct layer2 and layer3 informations
2018-09-04 09:46:45 tgen func[24727] INFO ICMP Test done with success --> OK
2018-09-04 09:46:45 tgen func[24727] INFO Starting TCP test
2018-09-04 09:46:45 tgen func[24727] DEBUG From namespace ns35 sending:
Ether / IP / TCP 10.35.0.2:19992 > 20.76.0.1:14615 S
2018-09-04 09:46:45 tgen func[24727] DEBUG Start sniffing on iface "vlan76" with bpfilter: "tcp"
2018-09-04 09:46:47 tgen func[24727] DEBUG Sending 10 packets...
2018-09-04 09:46:47 tgen func[24727] DEBUG Sending done.
2018-09-04 09:46:48 tgen func[24727] DEBUG Sniffing stoped, 20 packets have been captured
2018-09-04 09:46:48 tgen func[24727] DEBUG Received packets have a correct layer2 and layer3 informations
2018-09-04 09:46:48 tgen func[24727] DEBUG From namespace ns76 sending:
Ether / IP / TCP 10.76.0.2:25055 > 20.35.0.1:7463 S
2018-09-04 09:46:48 tgen func[24727] DEBUG Start sniffing on iface "vlan35" with bpfilter: "tcp"
2018-09-04 09:46:50 tgen func[24727] DEBUG Sending 10 packets...
2018-09-04 09:46:50 tgen func[24727] DEBUG Sending done.
2018-09-04 09:46:51 tgen func[24727] DEBUG Sniffing stoped, 20 packets have been captured
2018-09-04 09:46:51 tgen func[24727] DEBUG Received packets have a correct layer2 and layer3 informations
2018-09-04 09:46:51 tgen func[24727] INFO TCP Test done with success --> OK
2018-09-04 09:46:51 tgen func[24727] INFO Starting ICMP test
2018-09-04 09:46:51 tgen func[24727] DEBUG From namespace ns35 sending:
Ether / IP / ICMP 10.35.0.2 > 20.76.0.1 echo-request 0
2018-09-04 09:46:51 tgen func[24727] DEBUG Start sniffing on iface "vlan35" with bpfilter: "icmp"
2018-09-04 09:46:53 tgen func[24727] DEBUG Sending 10 packets...
2018-09-04 09:46:54 tgen func[24727] DEBUG Sending done.
2018-09-04 09:46:55 tgen func[24727] DEBUG Sniffing stoped, 20 packets have been captured
2018-09-04 09:46:55 tgen func[24727] DEBUG Received packets have a correct layer2 and layer3 informations
2018-09-04 09:46:55 tgen func[24727] DEBUG From namespace ns76 sending:
Ether / IP / ICMP 10.76.0.2 > 20.35.0.1 echo-request 0
2018-09-04 09:46:55 tgen func[24727] DEBUG Start sniffing on iface "vlan76" with bpfilter: "icmp"
2018-09-04 09:46:57 tgen func[24727] DEBUG Sending 10 packets...
2018-09-04 09:46:57 tgen func[24727] DEBUG Sending done.
2018-09-04 09:46:58 tgen func[24727] DEBUG Sniffing stoped, 20 packets have been captured
2018-09-04 09:46:58 tgen func[24727] DEBUG Received packets have a correct layer2 and layer3 informations
2018-09-04 09:46:58 tgen func[24727] INFO ICMP Test done with success --> OK
2018-09-04 09:46:58 tgen func[24727] INFO Starting TCP test
2018-09-04 09:46:58 tgen func[24727] DEBUG From namespace ns35 sending:
Ether / IP / TCP 10.35.0.2:26369 > 20.76.0.1:47193 S
2018-09-04 09:46:58 tgen func[24727] DEBUG Start sniffing on iface "vlan76" with bpfilter: "tcp"
2018-09-04 09:47:00 tgen func[24727] DEBUG Sending 10 packets...
2018-09-04 09:47:00 tgen func[24727] DEBUG Sending done.
2018-09-04 09:47:01 tgen func[24727] DEBUG Sniffing stoped, 20 packets have been captured
2018-09-04 09:47:01 tgen func[24727] DEBUG Received packets have a correct layer2 and layer3 informations
2018-09-04 09:47:01 tgen func[24727] DEBUG From namespace ns76 sending:
Ether / IP / TCP 10.76.0.2:36787 > 20.35.0.1:46432 S
2018-09-04 09:47:01 tgen func[24727] DEBUG Start sniffing on iface "vlan35" with bpfilter: "tcp"
2018-09-04 09:47:03 tgen func[24727] DEBUG Sending 10 packets...
2018-09-04 09:47:03 tgen func[24727] DEBUG Sending done.
2018-09-04 09:47:04 tgen func[24727] DEBUG Sniffing stoped, 20 packets have been captured
2018-09-04 09:47:04 tgen func[24727] DEBUG Received packets have a correct layer2 and layer3 informations
2018-09-04 09:47:04 tgen func[24727] INFO TCP Test done with success --> OK
2018-09-04 09:47:04 tgen func[24727] INFO TestSuite results:
+------+-----------+
| Test |   Result  |
+------+-----------+
| ICMP | Succeeded |
| TCP  | Succeeded |
| ICMP | Succeeded |
| TCP  | Succeeded |
+------+-----------+
```

## How to write a new Test
All tests are written in `tests.py` file, checkout for more details.  There is
a generic test class called `TestSuite` which all test classes inherit from.

To simply add a new test `mytest`, first fill the configuration dict with:
```
    'mytest': {
        'name': 'mytest', # the test name to display
        'class': MyTest, # the test class to call
        'bpfilter': 'myfilter', # the bpfilter to use when sniffing
        'count': 10, # the number of packets to send in a test
        'local_sniff': False, # if you want to sniff on the sending interface
        'payload': 'mytest custom payload', # if you need a custom payload
        'description': 'mytest description' # the test description
    },
```

Then add the new class which inherits from `TestSuite` and override the
following methods:

* `buid_auery`: the method that allows building the packet to send.
* `validate_answer`: which validate the sniffed packets depending on the test,
                     you can use `validate_l2l3_answer()` to validate
                     automatically `Ethernet` and `IP` headers according to the
                     network configuration, thus It remains adding only the
                     packet validation functions specific to the test.

For more informations see the `ICMPTest`, `TCPTest` or `UDPTest` classes for
reference.

## TODO
* Write more tests (jumbo frames...).
* Make test validation more robust.

