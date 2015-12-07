#include <rte_ip.h>

#include "natasha.h"


/*
 * Send a burst of output packets on the transmit @queue of @port.
 *
 * Packets that can't be stored in the transmit ring are freed.
 *
 * XXX: we should keep the packets not sent for a later call instead of
 *      discarding them.
 *
 * @return
 *  - The return value of rte_eth_tx_burst(), ie. the number of packets
 *    actually stored in transmit descriptors of the transmit ring.
 */
uint16_t
tx_flush(uint8_t port, struct tx_queue *queue)
{
    uint16_t sent;
    uint16_t n;

    sent = rte_eth_tx_burst(port, queue->id, queue->pkts, queue->len);

    // rte_eth_tx_burst() is responsible to free the sent packets. We need to
    // free the packets not sent.
    n = sent;
    while (n < queue->len) {
        rte_pktmbuf_free(queue->pkts[n]);
        n++;
    }

    queue->len = 0;

    return sent;
}

/*
 * Enqueue a packet. If the queue is full, flush it.
 *
 * @return
 *  - The number of packets sent. Zero if the packet is only enqueued and the
 *    queue isn't full, otherwise the value returned by tx_flush().
 */
uint16_t
tx_send(struct rte_mbuf *pkt, uint8_t port, struct tx_queue *queue)
{
    pkt->l2_len = sizeof(struct ether_hdr);
    pkt->l3_len = sizeof(struct ipv4_hdr);

    queue->pkts[queue->len] = pkt;
    queue->len++;

    if (queue->len >= sizeof(queue->pkts) / sizeof(*queue->pkts)) {
        return tx_flush(port, queue);
    }
    return 0;
}
