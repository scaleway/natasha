This document details the constants used to initialize DPDK in
[core.c](src/core.c).


# `rte_eth_dev_configure()`

DPDK documentation of [`rte_eth_dev_configure()`] [doc_eth_dev_configure].

```
Configure an Ethernet device. This function must be invoked first before any
other function in the Ethernet API.
```

[doc_eth_dev_configure]: http://dpdk.org/doc/api/rte__ethdev_8h.html

## Parameters:

- **port**: the port identifier of the Ethernet device to configure.
- **nb_rx_queue** and **nb_tx_queue**: the number of receive and transmit queues
  to set up for the Ethernet device. Natasha creates one receive queue and one
  transmit queue for each port, for each core.
- **eth_conf**: the configuration data to be used for the Ethernet device (see
[rte_eth_conf](#rte_eth_conf)).


### Receive and transmit queues

In this example, natasha is run on a server with two Ethernet devices, and with
`-c 0xf` (equivalent to  -l 0,1,2,3)), ie. with the logical cores 0 to 3
activated.

Transmit and receive queues are configured as follow:

```
            Port 0         Port 1
Core 0
Core 1    RX      TX     RX      TX
Core 2    RX      TX     RX      TX
Core 3    RX      TX     RX      TX
```

The master (core 0) is used to run the administration server and doesn't have
RX/TX queues setup.

### rte_eth_conf

This complex structure is
[described] [doc_eth_conf] in DPDK
documentation. For natasha, we use the following values:

[doc_eth_conf]: http://dpdk.org/doc/api/structrte__eth__conf.html

- **link_speed**: Ethernet device link speed. We use 0, for autonegociation.

- **link_duplex**: Half or full duplex. We use 0, for autonegociation.

- **rx_mode**, which is a structure with the following fields:

    - **mq_mode**: specify the method to use to route packets to multiple
      queues. This [document] [intel_improve_network_perf] from Intel explains
      what are the different methods. In short:

        - RSS maps queues to processor cores. Principle of [RSS] [ixgbe_rss]:

          ```
          * The source and destination IP addresses of the IP header and the source
          * and destination ports of TCP/UDP headers, if any, of received packets are
          * hashed against a configurable random key to compute a 32-bit RSS hash result.
          * The seven (7) LSBs of the 32-bit hash result are used as an index into a
          * 128-entry redirection table (RETA). Each entry of the RETA provides a 3-bit
          * RSS output index which is used as the RX queue index where to store the
          * received packets.
          ```

          In other words, when RSS is used, a hash is built from the IP
          addresses and source/destination ports of the IP/TCP/UDP headers.
          This way, packets with the same source/dest IP/port will always be
          handled by the same network queue.

        - VMDq filters data into queues based of MAC address of VLAN tags.

      natasha is completely stateless: we don't need a packet flow to be always
      handled by the same core.

      To dispatch packets to our multiple receive queues, we use ETH_MQ_RX_RSS.

      Note: give a look to this [blog post] [rss_blog_post] to configure the
      RSS indirection table.


[intel_improve_network_perf]: http://www.intel.com/content/dam/doc/white-paper/improving-network-performance-in-multi-core-systems-paper.pdf
[ixgbe_rss]: http://dpdk.org/browse/dpdk/tree/drivers/net/ixgbe/ixgbe_rxtx.c#n2582
[rss_blog_post]: http://galsagie.github.io/dpdk%20design%20tips/dpdk/nfv/2015/02/26/dpdk-tips-1/

    - **jumbo_frame**: whether we enable or not jumbo frames. We use 1 if the
       interface MTU configured in natasha.conf is bigger than ETHER_MAX_LEN,
       otherwise 0.

    - **max_rx_pkt_len**: only used if jumbo_frame enabled. We use the
      interface MTU, configured in natasha.conf (default 1500).

    - **header_split**: whether we should split headers or not, as explained
      in [this document] [accelerating_network_processing]. In short, if true:

      ```
      Using this capability, the controller can partition the packet between
      the headers and the data, and copy these into two separate buffers. This
      has several advantages. First, it allows both the header and the data to
      be optimally aligned. Second, it allows the network data buffer to
      consist of a small slab-allocated header buffer plus a larger,
      page-allocated data buffer. Surprisingly, making these two allocations is
      faster than one large slab allocation, due to how the buddy allocator
      works. Third, split header suppoer results in better cache utilization by
      not polluting the CPU’s cache with any application data during network
      processing.
      ```

      After experimentations, we handle 100k more pps if this value is set to 0
      instead of 1.

[accelerating_network_processing]: https://www.kernel.org/doc/ols/2005/ols2005v1-pages-289-296.pdf

    - **split_hdr_size**: only used if header_split is enabled. Since
      header_split = 0, we use 0.

    - **hw_ip_checksum**: whether IP checksum offload is enable or not.
      The ixgbe driver has several receive functions. It uses the most
      effective one depending on the configuration.
      If set to 1, the driver uses the non-optimized receive function
      *ixgbe_recv_pkts_lro_bulk_alloc()*. We use 0 to use the more optimized
      *receive function ixgbe_recv_scattered_pkts_vec()*.

    - **hw_vlan_filter**: if 1, only accept traffic from VLANs configured with
      [rte_eth_dev_vlan_filter()] [doc_eth_dev_configure]. If 0, accept any
      encapsulated packet. The natasha configuration file specifies the VLANs to
      accept for a given port. Consequently, we set this variable to 1.

    - **hw_vlan_strip**: if 1, let the hardware remove the VLAN header. We use
      1, so we don't have to programmatically remove the header. We can still
      get the VLAN details through `pkt->vlan_tci`.

    - **hw_vlan_extend**: whether extended VLANs are enabled or not. We don't
      use extended VLANs, so set this variable to 0.

    - **hw_strip_crc**: if 1, the hardware strips the Ethernet CRC. If 0, let
      the driver strip it.
      Performances are not really affected by this parameter, so we use 1.

    - **enable_scatter**: Whether scatter packets should be enabled or not.
      [This document] [tuning_10gb_network_cards] explains what scatter is:

      ```
      Scatter and Gather, also known as Vectored I/O, is a concept that was
      primarily used in hard disks[3]. It basically enhance large I/O request
      performance, if supported by the hardware. Scatter reading is the ability
      to deliver data blocks stored at consecutive hardware address to
      non-consecutive memory addresses. Gather writing is the ability to
      deliver blocks of data stored at non-consecutive memory addresses to
      consecutively addressed hardware blocks.

      One of the constraints that happens to DMA is that the physical memory
      buffer should be contiguous in order to receive the data. On the other
      hand, a device that supports scatter and gather capability allows the
      kernel to allocate smaller buffers at various memory locations for DMA.
      Allocating smaller buffers is much easier and faster than finding for a
      huge buffer to place the packet.

      When scatter and Gather is enable, it is also possible to do a concept
      called page flip. This basically allows the transport and other headers
      to be separated from the payload. Splitting header from payload is useful
      for copy avoidance because a virtual memory system may map the payload to
      an possible application buffer, only manipulating the virtual memory page
      to point to the payload, instead of copying the payload from one place to
      another.

      The advantage of Scatter and Gather is to reduce over head allocating
      memory and copying data, also as hav ing a better memory footprint.
      ```

      Setting this parameter to 1 helps to receive huge packets in
      non-contiguous memory.

      This is not our case: we have a lot of 2048kB hugepages, and our packets
      can fit in these hugepages.

[tuning_10gb_network_cards]: https://wiki.chipp.ch/twiki/pub/CmsTier3/NodeTypeFileServerHPDL380G7/ols2009-pages-169-1842.pdf

    - **enable_lro**: Whether large receive offload ([doc1] [wikipedia_lro],
      [doc2] [lwn_lro]) should be activated or not.

      ```
      In computer networking, large receive offload (LRO) is a technique for
      increasing inbound throughput of high-bandwidth network connections by
      reducing CPU overhead. It works by aggregating multiple incoming packets
      from a single stream into a larger buffer before they are passed higher
      up the networking stack, thus reducing the number of packets that have to
      be processed.

      [...]

      LRO should not be used on machines acting as routers, as it breaks the
      end-to-end principle and can significantly impact performance.
      ```

    Since our NAT is acting as a router, we use 0.

[wikipedia_lro]: https://en.wikipedia.org/wiki/Large_receive_offload
[lwn_lro]: https://lwn.net/Articles/358910/

- **tx_mode**, which is a structure with the following fields:

    - **mq_mode**: a value to identify what method to use to transmit packets
      using multiple traffic classes: DCB, VT, none or both. According to the
      [intel datasheet of our NIC] [intel_datasheet]:

      ```
      Virtualization (VT) - In a virtualized environment, DMA resources are
      shared between more than one software entity (operating system and/or
      device driver). This is done through allocation of transmit descriptor
      queues to virtual partitions (VMM, IOVM, VMs, or VFs).  Allocation of
      queues to virtual partitions is done in sets of queues of the same size,
      called queue pools, or pools. A pool is associated with a single virtual
      partition. Different queues in a pool can be associated with different
      packet buffers. For example, in a DCB system, each of the queues in a
      pool might belong to a different TC and therefore to a different packet
      buffer.
      ```

      ```
      DCB — DCB provides QoS through priority queues, priority flow control,
      and congestion management. Queues are classified into one of several (up
      to eight) Traffic Classes (TCs). Each TC is associated with a single
      unique packet buffer.
      ```

      We're not in a virtualized environment and we don't do QoS, so we set
      this value to ETH_MQ_TX_NONE.

[intel_datasheet]: http://www.intel.com/content/www/us/en/embedded/products/networking/82599-10-gbe-controller-datasheet.html

    - The following flags are only used for the i40e driver:

        - **pvid**: if hw_vlan_insert_pvid is true, the pvid to add. We use 0,
          because hw_vlan_insert_pvid is not set.

        - **hw_vlan_reject_tagged**: if set, reject sending out tagged packets.
          We use 0, since depending on the configuration, we might want to emit
          tagged packets.
        - **hw_vlan_reject_untagged**: if set, reject sending out untagged
          packets. We use 0, since depending on the configuration, we might
          want to emit untagged packets.

        - **hw_vlan_insert_pvid**: if set, insert port based VLAN insertion. We
          use 0, as we define manually the VLAN to offload in
          [src/core.c](src/core.c).

- **lpbk_mode**: according to [DPDK documentation] [doc_lpbk_mode]:

  ```
  Loopback operation mode. By default the value is 0, meaning the loopback mode
  is disabled. Read the datasheet of given ethernet controller for details. The
  possible values of this field are defined in implementation of each driver.
  ```

  pktgen sets this value to 1, probably to send the same packets over and over
  again without freeing them.

  Anyway, we don't need such a fewature so we set this value to 0.

[doc_lpbk_mode]: http://dpdk.org/doc/api/structrte__eth__conf.html#a90279894934ce85eb3acea67b4758950

- **rx_adv_conf**: an union to set RSS/VMDQ/DCB configuration. We use the
  following RSS configuration:

    - **rss_conf**: a structure to configure RSS configuration.

        - **rss_key**: the RSS key used to dispatch packets. We use NULL to use
          the default value set by the driver.

          Note: it is possible to set a symmetrical key so the packets (src IP,
          dst IP) or (dst IP, src IP) are always handled by the same core.
          However, natasha is completely stateless (so there's no point using
          such a key) and such a symmetrical key is pointless in the context of
          a NAT application, which is the main purpose of natasha.

        - **rss_key_len**: set to 0 to use the default value.

        - **rss_hf**: the different types of IPv6/IPv6 packets to which the RSS
          hashing must be applied.

          We use ETH_RSS_IP to dispatch packets based on the source and
          destination IP addresses.

          Instead, we could use ETH_RSS_PROTO_xxx to include the source and
          destination ports and to have a better dispatch, which implies the
          necessity of reading more data from memory.

          We didn't test if using ETH_RSS_PROTO_xxx affects performances, so we
          stick with ETH_RSS_IP.

    - **vmdq_dcb_conf**: port VMDQ+DCB RX configuration (not set).

    - **dcb_rx_conf**: port DCB RX configuration (not set).

    - **vmdq_rx_conf**: port VMDQ RX configuration (not set).

- **tx_adv_conf**: union of port TX configuration. Since txmode.mq_mode is
  ETH_MQ_TX_NONE, we leave these fields uninitialized.

    - **vmdq_dcb_tx_conf**: DCB + VT configuration

    - **dcb_tx_conf**: DCB configuration

    - **vmdq_tx_conf**: VMDQ configuration

- **dcb_capability_en**: Configure Priority Flow Control of DCB. Since
  rxmode.mq_mode is ETH_MQ_RX_NONE, we use 0 here.

-  **fdir_conf**: a structure to define flow director filters. A flow director
  is used to run an action (drop or forward the packet to a specific RX queue)
  depending on a filter (VLAN header, source and destination IP addresses,
  ...).  See [Flow Director Filters section of the intel datasheet]
  [intel_datasheet].  We don't need it, so we leave this structure
  uninitialized.

- **ntr_conf**: Interrupt mode configuration, which is a structure containing :

    - **lsc**: 1 to enable link status interrupt. If 1, we can register a
      callback with rte_eth_dev_callback_register as in the [DPDK example]
      [example_set_lsc] to get notified when the link status changes. We don't
      need such callback, so we leave this field to 0.

    - **rxq**: RXQ interrupt configuration. If set to 1, you can configure a
      callback when the RX queue is active. Is useful for power saving or to
      prevent a core from being 100% used.
      We don't really care, so we leave this flag to 0.

[example_set_lsc]: http://dpdk.readthedocs.org/en/v2.2.0/sample_app_ug/link_status_intr.html


# `rte_eth_rx_queue_setup()`

DPDK documentation of [`rte_eth_rx_queue_setup(
    uint8_t                      port_id,
    uint16_t                     rx_queue_id,
    uint16_t                     nb_rx_desc,
    unsigned int                 socket_id,
    const struct rte_eth_rxconf  *rx_conf,
    struct rte_mempool           *mb_pool
)`] [doc_eth_rx_queue_setup].

```
Allocate and set up a receive queue for an Ethernet device.

The function allocates a contiguous block of memory for nb_rx_desc receive
descriptors from a memory zone associated with socket_id and initializes each
receive descriptor with a network buffer allocated from the memory pool
mb_pool.
```

[doc_eth_rx_queue_setup]: http://dpdk.org/doc/api/rte__ethdev_8h.html


## Parameters:

- **rx_queue_id**: as explained in [Receive and transmit
  queues](#receive-and-transmit-queues), each slave core creates a RX queue for
  each port. Consequently, the first slave sets queue_id=0, the second one
  queue_id=1 and so on.

- **nb_rx_desc**: the number of receive descriptors to allocate for the receive
  ring. Almost everywhere in DPDK sample applications, 128 is used.

  After bechmarks, we decided to set this value to 256. When this value is too
  big, descriptors are spread among different memory pages, causing IO TLB
  misses. When it's too small, we lose packets under pressure. 

- **socket_id**: the NUMA socket ID of the core.

- **rx_conf**: a structure to configure the receive queue, with the following
  fields:

    - **rx_thresh**: a structure used to configure RX ring threshold registers,
    with the following fields:

        - **pthresh**: probably corresponds to the number of descriptors that
          can be stored in a cacheline, The default value for ixgbe is 8.

        - **hthresh**: ring host threshold. We use 8.
        - **wthresh**: ring writeback. We use 0.

    - **rx_free_thresh**: drives the freeing of RX descriptors. We use 32.

    - **rx_drop_en**: If 1, the NIC won't try to hold packets when the ring is
      full. We want to avoid losing packets, so this value is set to 0.

    - **rx_deferred_start**: if true, do not start queue with
      rte_eth_dev_start(). We use 0.

- **mb_pool**: a pointer to a memory pool created with rte_mempool_create() to
  allocate rte_mbuf network memory buffers to populate each descriptor of the
  receive ring.
  Note: instead of using rte_mempool_create to create the mempool, we use the
  wrapper rte_pktmbuf_pool_create which ensures mbufs are correctly cache
  aligned.

    - **n**: number of elements in the mempool. We use 8192 as it seems to be
      optimal according to our tests.

      Note: we have one mempool per core / per port. Is we decided to share one
      mempool betweens all the queues/ports, we would have to increase this
      value accordingly.

    - **elt_size**: the size of each element in the mempool. We use 9216 +
      RTE_PKTMBUF_HEADROOM.

      Note: 9216 % 64 (ie. the size of a cacheline on x86) = 0. In memory, a
      mbuf is stored as follow:

      byte   0: header
      byte  26: data offset padding
      byte  32: priv size padding, useful to store data before the mbuf. We set
                this value to 0, but it could be interesting to set this value
                to 32 so the mbuf data would be at an address % 64, which might
                improve performances.
      byte  32: pkt mbuf headroot (128 bytes), used by DPDK for multisegment,
                vlan stripping, ...
      byte 160: mbuf payload

    - **cache_size**: this value corresponds to the number of elements reserved
      for each core when a mempool is used by multiple producers and/or
      consumers. If this value is not 0, cores don't have to use the shared
      pool if their requests can be satisfied by their cache.
      This value should always be % 64 (as the documentation says), otherwise
      the memory use isn't optimal.
      We use 512 even though that's probably useless.

    - **private_data_size**: According to DPDK documentation, "the size of the
      private data appended after the mempool structure. This is useful for
      storing some private data after the mempool structure".

      We don't need to store such data after the mempool. If we had to, we need
      to be cautious and ensure the total mbuf size is % cacheline size.

    - **flags**: an OR of the following flags:

      ```
      MEMPOOL_F_NO_SPREAD: By default, objects addresses are spread between
      channels in RAM: the pool allocator will add padding between objects
      depending on the hardware configuration. See Memory alignment
      constraints for details. If this flag is set, the allocator will just
      align them to a cache line.

      MEMPOOL_F_NO_CACHE_ALIGN: By default, the returned objects are
      cache-aligned. This flag removes this constraint, and no padding will
      be present between objects. This flag implies MEMPOOL_F_NO_SPREAD.

      MEMPOOL_F_SP_PUT: If this flag is set, the default behavior when
      using rte_mempool_put() or rte_mempool_put_bulk() is
      "single-producer".  Otherwise, it is "multi-producers".

      MEMPOOL_F_SC_GET: If this flag is set, the default behavior when
      using rte_mempool_get() or rte_mempool_get_bulk() is
      "single-consumer".  Otherwise, it is "multi-consumers".
      ```

      We use the wrapper rte_pktmbuf_pool_create to create the mempool. This
      wrapper doesn't accept flags.


# `rte_eth_tx_queue_setup()`

DPDK documentation of [`rte_eth_tx_queue_setup(
    uint8_t                      port_id,
    uint16_t                     tx_queue_id,
    uint16_t                     nb_tx_desc,
    unsigned int                 socket_id,
    const struct rte_eth_txconf  *tx_conf
)`] [doc_eth_tx_queue_setup].

```
Allocate and set up a transmit queue for an Ethernet device.
```

[doc_eth_tx_queue_setup]: http://dpdk.org/doc/api/rte__ethdev_8h.html


## Parameters:

- **tx_queue_id**: same than rx_queue_id of rte_eth_rx_queue_setup().

- **nb_tx_desc**: the number of transmit descriptors to allocate for the
  transmit ring. After some tests, 512 seems ideal. This value should always be
  greater than rx_ring_size to ensure you can always send the packets
  previously receivedV

- **socket_id**: the NUMA socket ID of the core.

- **tx_conf**: a structure to configure the transmit queue. We use the PMD
  default value for the following fields:

    - **tx_thresh**: a structure used to configure TX ring threshold registers,
      with the following fields:

        - **pthresh**: ring prefetch threshold. We use 32.

        - **hthresh**: ring host threshold. We use 0.

        - **wthresh**: ring writeback. According to [DPDK documentation]
          [config_transmit_receive_queues], for optimal performance, this value
          should be set to 0 when tx_rs_thresh is greater than 1. Consequently,
          we use 0.

    - **tx_rs_thresh**:

      [from Poll Mode Driver documentation] [doc_pmd]

      ```
      The minimum RS bit threshold. The minimum number of transmit descriptors
      to use before setting the Report Status (RS) bit in the transmit
      descriptor. Note that this parameter may only be valid for Intel 10 GbE
      network adapters. The RS bit is set on the last descriptor used to
      transmit a packet if the number of descriptors used since the last RS bit
      setting, up to the first descriptor used to transmit the packet, exceeds
      the transmit RS bit threshold (tx_rs_thresh). In short, this parameter
      controls which transmit descriptors are written back to host memory by
      the network adapter.  A value of 0 can be passed during the TX queue
      configuration to indicate that the default value should be used. The
      default value for tx_rs_thresh is 32. This ensures that at least 32
      descriptors are used before the network adapter writes back the most
      recently used descriptor. This saves upstream PCIe* bandwidth resulting
      from TX descriptor write-backs. It is important to note that the TX
      Write-back threshold (TX wthresh) should be set to 0 when tx_rs_thresh is
      greater than 1. Refer to the Intel® 82599 10 Gigabit Ethernet Controller
      Datasheet for more details.
      ```

      We use the default value, 0.

    - **tx_free_thresh**:

      [from Poll Mode Driver documentation] [doc_pmd]

      ```
      The minimum transmit packets to free threshold (tx_free_thresh). When the
      number of descriptors used to transmit packets exceeds this threshold,
      the network adaptor should be checked to see if it has written back
      descriptors. A value of 0 can be passed during the TX queue configuration
      to indicate the default value should be used. The default value for
      tx_free_thresh is 32. This ensures that the PMD does not search for
      completed descriptors until at least 32 have been processed by the NIC
      for this queue.
      ```

      We use the default value, 0.

    - **txq_flags**: one of the following:

        - ETH_TXQ_FLAGS_NOMULTSEGS: to prevent packets from being segmented
          among several mbufs.

        - ETH_TXQ_FLAGS_NOREFCOUNT: (???)

        - ETH_TXQ_FLAGS_NOMULTMEMP: all buffers come from the same mempool
          (???)

        - ETH_TXQ_FLAGS_NOVLANOFFL: to disable VLAN offload

        - ETH_TXQ_FLAGS_NOXSUMSCTP: to disable SCTP checksum offload

        - ETH_TXQ_FLAGS_NOXSUMUDP: to disable UDP checksum offload

        - ETH_TXQ_FLAGS_NOXSUMTCP: to disable TCP checksum offload

      We use ETH_TXQ_FLAGS_NOMULTSEGS as a packet can't be segmented (because
      mbuf are big enough to contain the biggest packets we can receive).

    - **tx_deferred_start**: if true, do not start queue with
      rte_eth_dev_start(). There's no reason to do so, so we 0.

[config_transmit_receive_queues]: http://dpdk.org/doc/guides/prog_guide/poll_mode_drv.html
[doc_pmd]: http://dpdk.org/doc/guides/prog_guide/poll_mode_drv.html
