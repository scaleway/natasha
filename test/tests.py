#!/usr/bin/env python
# -*- encoding: utf-8 -*-
"""
Test classes
"""

from time import sleep

from scapy.sendrecv import sendp
from scapy.packet import Raw
from scapy.volatile import RandShort
from scapy.layers.l2 import Ether
from scapy.layers.inet import IP, ICMP, TCP, UDP, fragment, defragment

from utils import Sniffer

class TestSuite(object):

    """ Test class for all kind of packet pattern. """

    def __init__(self, name, log, conf, bpfilter, payload=None,
                 local_sniff=False, count=10):
        """
        :name: Test name
        :log: log object for output
        :conf: the network configuration
        :bpfilter: the bpfilter string
        :payload: custom payload to add
        :local_sniff: whether to sniff packet on the sending interface or not
        :count: the number of packet to send

        """
        self._name = name
        self._log = log
        self._conf = conf
        self._bpfilter = bpfilter
        self._payload = payload
        self._local_sniff = local_sniff
        self._count = count
        self._test_result = True

    def build_query(self, conf):
        """Method used to build the packet to send
        :conf: the namespace configuration
        :returns: packet built

        """
        pkt = Ether(src=conf['mac_local'], dst=conf['mac_nh'])
        pkt /= self._payload

        # Force building the packet
        return pkt.__class__(str(pkt))

    def checksum_is_valid(self, pkt, proto):
        """ Method to validate the protocol checksum by computing the checksum
        with Scapy and compare it with the checksum reveived

        :pkt: the packet to validate
        :proto: The protocol, can be IP, TCP, or UDP.
        :returns: Bool depending on the result

        """
        received_csum = pkt[proto].chksum
        # force the checksum to None
        del pkt[proto].chksum
        # rebuild the packet to calculate the checksum
        pkt = pkt.__class__(str(pkt))
        if pkt[proto].chksum != received_csum:
            self._log.error('Bad %s checksum detected 0x%x, expecting 0x%x' %
                            (proto.name, received_csum, pkt[proto].chksum))
            return False
        return True

    def validate_l2l3_answer(self, pkts, conf, layer3=True, layer2=True,
                             count_exp=None):
        """ This method validates the l2 and l3 informations on the answred
        packet this informations remain the same on all kind of tested traffic
        except ARP test.

        :pkts: packets list to validate
        :conf: the namespace configuration
        :layer2: test layer2
        :layer3: test layer3
        :expected: the expected number of packets

        :returns: True if the pkts are valid else False

        """
        log = self._log
        count = self._count * 2 if not count_exp else count_exp

        # Invert the MAC and IP @ when checking on the same iface that we send on
        if self._local_sniff:
            mac_src = conf['mac_local']
            mac_dst = conf['mac_nh']
            ip_src = conf['ip_priv']
            ip_dst = conf['ip_rmt']
        else:
            mac_src = conf['mac_nh']
            mac_dst = conf['mac_local']
            ip_src = conf['ip_pub']
            ip_dst = conf['ip_rmt_priv']

        if len(pkts) != count:
            log.error('Expecting %d captured packets found %d' %
                      (count, len(pkts)))
            return False

        _iter = iter(pkts)
        for pkt in _iter:
            req = pkt
            rsp = next(_iter)

            if layer2:
                if(req[Ether].src != mac_src or
                   req[Ether].dst != mac_dst):
                    log.error('Expecting packet with MAC@(src="%s", dst="%s"),'
                              ' got MAC@(src="%s" dst="%s")' %
                              (mac_src, mac_dst,
                               req[Ether].src, req[Ether].dst))
                    return False
                if(rsp[Ether].src != mac_dst or
                   rsp[Ether].dst != mac_src):
                    log.error('Expecting packet with MAC@(src="%s", dst="%s"),'
                              ' got MAC@(src="%s" dst="%s")' %
                              (mac_dst, mac_src,
                               rsp[Ether].src, rsp[Ether].dst))
                    return False
            if layer3:
                if(req[IP].src != ip_src or
                   req[IP].dst != ip_dst):
                    log.error('Expecting packet with IP@(src="%s", dst="%s"),'
                              ' got IP@(src="%s" dst="%s")' %
                              (ip_src, ip_dst,
                               req[IP].src, req[IP].dst))
                    return False
                if(rsp[IP].src != ip_dst or
                   rsp[IP].dst != ip_src):
                    log.error('Expecting packet with IP@(src="%s", dst="%s"),'
                              ' got IP@(src="%s" dst="%s")' %
                              (ip_dst, ip_src,
                               rsp[IP].src, rsp[IP].dst))
                    return False

                # Validate the l3 checksum on natted packet
                if  not self.checksum_is_valid(rsp if self._local_sniff else
                                               req, IP):
                    return False

                # Validate the payload
                payload = rsp[Raw].load if self._local_sniff else req[Raw].load
                if payload != self._payload:
                    log.error('Expecting packet with payload \""%s"\", '
                              'got \""%s"\"' % (self._payload, payload))
                    return False

        log.info('Received packets have a correct layer2, layer3 and payload'
                 ' informations')
        return True

    def validate_answer(self, req, pkts, conf):
        """ Wrapper for all validation methods

        :req: the packet generated and sent
        :pkts: captured packts
        :conf: the namespace configuration
        :returns: Bool

        """
        return self.validate_l2l3_answer(pkts, conf)

    def run(self):
        """TODO: Docstring for run.

        :returns: Bool depending on test result

        """
        self._log.info("Starting %s test" % self._name)
        # send traffic flow in both sides
        for namespace, conf in self._conf.iteritems():
            iface = conf['dev'] if self._local_sniff else conf['peer_dev']
            pkt = self.build_query(conf)
            self._log.info("From namespace %s:" % namespace)
            self._log.debug("Packet built, ready to send.")
            if not isinstance(pkt, list):
                self._log.debug("%s" % pkt.summary())

            sniffer = Sniffer(log=self._log,
                              iface=iface,
                              bpfilter=self._bpfilter)
            sniffer.start()
            sleep(2) # let the thread start and start sniffing, very important
            self._log.debug('Sending %d packets...' % self._count)
            sendp(pkt, iface=conf['dev'],
                  count=self._count,
                  verbose=False)
            self._log.debug("Sending done.")
            sniffer.stop()

            self._log.debug("Start DPI...")

            # force IP reassembly if it exists
            pkts = defragment(sniffer.get_pcap())

            result = self.validate_answer(pkt, pkts, conf)
            if result:
                self._log.debug("DPI done, traffic is correct as expected")
            else:
                self._log.error("DPI done, traffic is incorrect")
            # Don't exit the test if it fails in one way, let's test the other
            # way too
            self._test_result &= result
            break

        if self._test_result:
            self._log.info("%s Test done with success --> OK" % self._name)
        else:
            self._log.error("%s Test failed --> KO" % self._name)

        return self._test_result

class ICMPTest(TestSuite):

    """ICMP test class"""

    def build_query(self, conf):
        """Method used to build the packet to send
        :conf: the namespace configuration
        :returns: packet to send

        """
        pkt = Ether(src=conf['mac_local'], dst=conf['mac_nh'])
        pkt /= IP(src=conf['ip_priv'], dst=conf['ip_rmt'], id=RandShort())
        pkt /= ICMP(id=RandShort(), seq=1)
        pkt /= self._payload

        return pkt.__class__(str(pkt))

    def validate_icmp_answer(self, pkts, conf):
        """Validate the ICMP answers

        :pkts: received ICMP packets
        :conf: the namespace configuration
        :returns: bool

        """
        log = self._log
        count = self._count
        echo_replays = 0

        for pkt in pkts:
            if pkt.haslayer(ICMP) and pkt[ICMP].type == 0:
                if (pkt[IP].src == conf['ip_rmt'] and
                        pkt[IP].dst == conf['ip_priv'] and
                        pkt[Ether].src == conf['mac_nh'] and
                        pkt[Ether].dst == conf['mac_local']):
                    echo_replays += 1
        if echo_replays != count:
            log.error('Expecting %d ICMP echo reply, got %d' % (count,
                                                                echo_replays))
            return False
        log.info('Expected ICMP echo replys correct')
        return True

    def validate_answer(self, req, pkts, conf):
        """Validate ICMP answers, expecting ICMP echo and echo_replays.

        :req: the packet generated and sent
        :pkts: captured packts
        :conf: the namespace configuration
        :returns: Bool

        """
        return (self.validate_l2l3_answer(pkts, conf) and
                self.validate_icmp_answer(pkts, conf))

class TCPTest(TestSuite):

    """TCP test class"""

    def build_query(self, conf):
        """Method used to build the packet to send
        TCP test need to validate any kind of TCP packet so let's use TCP SYN

        :conf: the namespace configuration
        :returns: packet to send

        """
        pkt = Ether(src=conf['mac_local'], dst=conf['mac_nh'])
        pkt /= IP(src=conf['ip_priv'], dst=conf['ip_rmt'], id=RandShort())
        pkt /= TCP(sport=RandShort(), dport=RandShort(), flags='S')
        pkt /= self._payload

        return pkt.__class__(str(pkt))

    def validate_l4_answer(self, pkts):
        """Validate the TCP answers
        TCP protol sends RST (reset connexion) when the port is not open
        let's filter and check only SYN packets

        :pkts: received TCP packets
        :conf: the namespace configuration
        :returns: bool

        """
        log = self._log

        # test only received TCP packet with SYN flag
        for pkt in pkts:
            if pkt.haslayer(TCP) and pkt[TCP].flags == 'S':
                if  not self.checksum_is_valid(pkt, TCP):
                    return False
        log.info('Received TCP traffic is correct')
        return True

    def validate_answer(self, req, pkts, conf):
        """Validate answers, expecting TCP SYN and TCP RESET traffic

        :req: the packet generated and sent
        :pkts: captured packts
        :conf: the namespace configuration
        :returns: Bool

        """
        return (self.validate_l2l3_answer(pkts, conf) and
                self.validate_l4_answer(pkts))

class UDPTest(TestSuite):

    """UDP test class"""

    def build_query(self, conf):
        """Method used to build the UDP packet to send.
        TCP test need to validate any kind of TCP packet so let's use TCP SYN

        :conf: the namespace configuration
        :returns: packet to send

        """
        pkt = Ether(src=conf['mac_local'], dst=conf['mac_nh'])
        pkt /= IP(src=conf['ip_priv'], dst=conf['ip_rmt'], id=RandShort())
        pkt /= UDP(sport=RandShort(), dport=RandShort())
        pkt /= self._payload

        return pkt.__class__(str(pkt))

    def validate_l4_answer(self, pkts):
        """Validate the UDP answers
        Linux Kernel sends an ICMP port unreachable when a UDP port is not open
        let's filter and check only UDP packets

        :pkts: received UDP packets
        :conf: the namespace configuration
        :returns: bool

        """
        log = self._log

        for pkt in pkts:
            if pkt.haslayer(UDP):
                if not self.checksum_is_valid(pkt, UDP):
                    return False
        log.info('Received UDP traffic is correct')
        return True

    def validate_answer(self, req, pkts, conf):
        """Validate answers, we expect UDP packets and ICMP port unreachable
        packets as well

        :req: the packet generated and sent
        :pkts: captured packts
        :conf: the namespace configuration
        :returns: Bool

        """
        return (self.validate_l2l3_answer(pkts, conf) and
                self.validate_l4_answer(pkts))

class UDPDFTest(UDPTest):

    """UDPDF test class same as UDP test but with DF IP flag"""

    def build_query(self, conf):
        """Method used to build the UDP packet to send.

        :conf: the namespace configuration
        :returns: packet to send

        """
        pkt = Ether(src=conf['mac_local'], dst=conf['mac_nh'])
        pkt /= IP(src=conf['ip_priv'], dst=conf['ip_rmt'], id=RandShort(),
                  flags=2)
        pkt /= UDP(sport=RandShort(), dport=RandShort())
        pkt /= self._payload

        return pkt.__class__(str(pkt))

class UDPFragTest(TestSuite):

    """
    UDP Fragmented test class This test need to validate that the received
    reasemble packet has a good UDP checksum after being natted.

    """

    def build_query(self, conf):
        """Method used to build a serie of fragment of a huge UDP packet.

        :conf: the namespace configuration
        :returns: packet to send

        """
        payload = self._payload
        pkt = Ether(src=conf['mac_local'], dst=conf['mac_nh'])
        pkt /= IP(src=conf['ip_priv'], dst=conf['ip_rmt'], id=RandShort())
        pkt /= UDP(sport=RandShort(), dport=RandShort())
        pkt /= payload

        return fragment(pkt.__class__(str(pkt)))

    def validate_l4_answer(self, pkts):
        """Validate the UDP answers
        Linux Kernel sends an ICMP port unreachable when a UDP port is not open
        let's filter and check only UDP packets

        :pkts: received UDP packets
        :conf: the namespace configuration
        :returns: bool

        """
        log = self._log

        for pkt in pkts:
            if pkt.haslayer(UDP):
                if not self.checksum_is_valid(pkt, UDP):
                    return False
        log.info('Received UDP traffic is correct')
        return True

    def validate_answer(self, req, pkts, conf):
        """Validate answers, we expect UDP packets and ICMP port unreachable
        packets as well

        :req: the packet generated and sent
        :pkts: captured packts
        :conf: the namespace configuration
        :returns: Bool

        """
        # received packet are reassemble so we dont't expect the same number we
        # sent. let's recalculate it and taking in consideration reassembled
        # request

        return (self.validate_l2l3_answer(pkts, conf) and
                self.validate_l4_answer(pkts))

class ICMPFrag(ICMPTest):

    """ICMPFrag test class"""

    def build_query(self, conf):
        """Method used to build the packet to send
        :conf: the namespace configuration
        :returns: packet to send

        """
        pkt = Ether(src=conf['mac_local'], dst=conf['mac_nh'])
        pkt /= IP(src=conf['ip_priv'], dst=conf['ip_rmt'], id=RandShort())
        pkt /= ICMP(id=RandShort(), seq=1)
        pkt /= self._payload

        return fragment(pkt.__class__(str(pkt)))

    def validate_icmp_answer(self, pkts, conf):
        """Validate the ICMP answers

        :pkts: received ICMP packets
        :conf: the namespace configuration
        :returns: bool

        """
        log = self._log
        count = self._count
        echo_replays = 0

        for pkt in pkts:
            if pkt.haslayer(ICMP) and pkt[ICMP].type == 0:
                if (pkt[IP].src == conf['ip_rmt'] and
                        pkt[IP].dst == conf['ip_priv'] and
                        pkt[Ether].src == conf['mac_nh'] and
                        pkt[Ether].dst == conf['mac_local']):
                    echo_replays += 1
        if echo_replays != count:
            log.error('Expecting %d ICMP echo reply, got %d' % (count,
                                                                echo_replays))
            return False
        log.info('Expected ICMP echo replys correct')
        return True

    def validate_answer(self, req, pkts, conf):
        """Validate ICMP answers, expecting ICMP echo and echo_replays.

        :req: the packet generated and sent
        :pkts: captured packts
        :conf: the namespace configuration
        :returns: Bool

        """
        return (self.validate_l2l3_answer(pkts, conf) and
                self.validate_icmp_answer(pkts, conf))
