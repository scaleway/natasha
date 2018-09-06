
#!/usr/bin/env python
# -*- encoding: utf-8 -*-
"""
utils functions and classes
"""

from threading import Thread
from scapy.sendrecv import sniff

class Sniffer(Thread):
    """ Sniffer thread """

    def  __init__(self, log=None, iface=None, timeout=3, bpfilter=None):
        self._log = log
        self._pcap = []
        self._iface = iface
        self._timeout = timeout
        self._bpfilter = bpfilter
        Thread.__init__(self)

    def run(self):
        """ sniff wrapper """
        self._log.debug('Start sniffing on iface "%s" with bpfilter: "%s"' %
                        (self._iface, self._bpfilter))
        self._pcap = sniff(iface=self._iface,
                           filter=self._bpfilter,
                           timeout=self._timeout)
        self._log.debug('Sniffing stoped, %d packets have been captured' %
                        len(self._pcap))

    def stop(self, timeout=None):
        """ pthread join wrapper """
        self.join(timeout)

    def get_pcap(self):
        """ wrapper to return the pcap """
        return self._pcap[:]
