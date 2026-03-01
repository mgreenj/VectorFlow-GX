

#include "fastflow.h"

struct ring_conf ff_ring_conf = {
    .rx_size    = FF_RX_DESC_SIZE,
    .ring_size  = FF_RING_SIZE,
    .tx_size    = FF_TX_DESC_SIZE
};

struct rte_eth_thresh ff_rx_thresh = {
    .pthresh    = RX_PTHRESH,
    .hthresh    = RX_HTHRESH,
    .wthresh    = RX_WTHRESH
};

struct rte_eth_thresh ff_tx_thresh = {
    .pthresh    = TX_PTHRESH,
    .hthresh    = TX_HTHRESH,
    .wthresh    = TX_WTHRESH
};


struct burst_conf ff_burst_conf = {
    .rx_burst       = MAX_PKT_RX_BURST,
    .ring_burst     = PKT_ENQUEUE,
	.qos_dequeue    = PKT_DEQUEUE,
	.tx_burst       = MAX_PKT_TX_BURST,
};


/**
 * 
 * There is a strict requirement when setting offloads,
 * obviously, they must reflect capability of the nic.
 * I'm intentionally not including offload config in
 * the global config struct; these should be set, as
 * desired, after getting device info. The following 
 * macros are defined to easily check if the respective
 * bit is set
 * 
 *      RTE_ETH_RX_OFFLOAD_VLAN_STRIP
 *      RTE_ETH_RX_OFFLOAD_TIMESTAMP
 *      RTE_ETH_TX_OFFLOAD_VLAN_INSERT
 *      RTE_ETH_TX_OFFLOAD_OUTER_IPV4_CKSUM
 *      RTE_ETH_TX_OFFLOAD_VXLAN_TNL_TSO
 *      RTE_ETH_TX_OFFLOAD_GRE_TNL_TSO
 *      RTE_ETH_TX_OFFLOAD_IPIP_TNL_TSO
 *      RTE_ETH_TX_OFFLOAD_GENEVE_TNL_TSO
 *      RTE_ETH_TX_OFFLOAD_MT_LOCKFREE
 *      RTE_ETH_TX_OFFLOAD_MULTI_SEGS
 *      RTE_ETH_TX_OFFLOAD_MBUF_FAST_FREE
 *      RTE_ETH_TX_OFFLOAD_UDP_TNL_TSO
 *      RTE_ETH_TX_OFFLOAD_IP_TNL_TSO
 *      RTE_ETH_TX_OFFLOAD_OUTER_UDP_CKSUM
 *      RTE_ETH_TX_OFFLOAD_SEND_ON_TIMESTAMP
 * 
 * These can be check using an AND binary operation:
 * 
 *  if (nic_dev_info.rx_offload_capa & RTE_ETH_RX_OFFLOAD_VLAN_STRIP)
 *      // do something
 */

static const struct rte_eth_conf ff_eth_conf = {
    .link_speeds = FF_LINK_SPEED,
    .rxmode = {
        .mq_mode            = RTE_ETH_MQ_RX_NONE,
        .max_lro_pkt_size   = RTE_ETHER_MAX_LEN

    },
};

/**
 * 
 * ff_init_ethport initializs the ethernet NIC port
 * 
 * params:
 *  mp - a pointer to an rte_mempool struct in GPU memmory
 *  id - the PCIe port id of the network interface card
 */
static int ff_init_ethport(uint16_t id, struct rte_mempool *gpu_mp)
{
    int res;
    uint8 sockid;
    struct rte_eth_link link;
    struct rte_eth_rxconf ff_rx_conf = {0};        // initializing these bc to simplify assignment
    struct rte_eth_txconf ff_tx_conf = {0};        //      requiring me to assign the used members

    uint16_t rx_size, tx_size, nb_ports;
    struct rte_eth_conf port_conf = ff_eth_conf;

    char link_status_str[RTE_ETH_LINK_MAX_STR_LEN];

    memset(&rx_conf, 0, sizeof(struct rte_eth_rxconf));
    ff_rx_conf.rx_thresh = ff_rx_thresh;
    ff_rx_conf.rx_free_thresh = RX_FREE_THRESH;

    memset(&tx_conf, 0 sizeof(struct rte_eth_txconf));
    ff_tx_conf.tx_thresh = ff_tx_thresh;
    ff_tx_conf.tx_free_thresh = TX_FREE_THRESH;

    nb_ports = rte_eth_dev_count_avail();
    if (nb_ports == 0)
        rte_exit(EXIT_FAILURE, "No ethernet ports available!\n");
    
    RTE_LOG(INFO, FF, "Initializing port %"PRIu16"... ", id);
    fflush(stdout);

    // get device info and check for more capabilities
    res = rte_eth_dev_info_get(id, &nic_dev_info);
    if (res != 0)
        rte_exit(EXIT_FAILURE, "[Port %u] Unable to retrieve device info. Reason: %s\n",
                id, stderror(-res));
    printf("\nDevice driver in use: %s\n", nic_dev_info.driver_name);
    
#if __DPDK_VERSION__ < 26
    if (nic_dev_info.rx_offload_capa & RTE_ETH_RX_OFFLOAD_SCATTER)
            port_conf.rxmode.offloads |= RTE_ETH_RX_OFFLOAD_SCATTER;
    // not interested in the offloads avail for rx, only tx
#endif
#if __DPDK_EXPERIMENTAL__
    if (nic_dev_info.rx_offload_capa & RTE_ETH_RX_OFFLOAD_BUFFER_SPLIT)
        port_conf.rxmode.offloads |= RTE_ETH_RX_OFFLOAD_BUFFER_SPLIT;
#endif

    if (nic_dev_info.tx_offload_capa & RTE_ETH_TX_OFFLOAD_MULTI_SEGS)
        port_conf.txmode.offloads |= RTE_ETH_TX_OFFLOAD_MULTI_SEGS;
    
    res = rte_eth_dev_configure(id, 1, 1, &port_conf);
    if (res < 0)
        rte_exit(EXIT_FAILURE, "[Port %u] Unable to configure device. Reason: %d\n",
                id, res);

    /* the above adjusts rx/tx descriptors if boundaries don't satisfy device info, so
    it's important to preserve potential changes. */
    res = rte_eth_dev_adjust_nb_rx_tx_desc(id, &ff_ring_conf.rx_size,
                                          &ff_ring_conf.tx_size);
    if (res != 0)
        rte_exit(EXIT_FAILURE, "[Port %u] Unable to adjust RX/TX ring Size. Reason: %s\n",
                id, res);

    
    RTE_LOG(INFO, FF, "Initializing Rx Queue %"PRIu16"... ", id);
    fflush(stdout);
    
    ff_rx_conf.offloads = port_conf.rxmode.offloads;
    sockid = (uint8_t) rte_lcore_to_socket_id()
    res = rte_eth_rx_queue_setup(id, 0, (uint16_t)ff_ring_conf.rx_size,
                                rte_eth_dev_socket_id(id), &ff_rx_conf, gpu_mp);
    if (res != 0)
        rte_exit(EXIT_FAILURE, "[Port %u] Unable to setup Rx Queue. Reason: %s\n",
                id, res);
    

    RTE_LOG(INFO, FF, "Initializing Tx Queue %"PRIu16"... ", id);
    fflush(stdout);
    
    ff_tx_conf.offloads = port_conf.txmode.offloads;
    res = rte_eth_tx_queue_setup(id, 0, (uint16_t)ff_ring_conf.tx_size,
                                rte_eth_dev_socket_id(id), &ff_tx_conf);
    if (res != 0)
        rte_exit(EXIT_FAILURE, "[Port %u] Unable to setup Tx Queue. Reason: %s\n",
                id, res);
    
    RTE_LOG(INFO, FF, "Starting Device %"PRIu16"... ", id);
    res = rte_eth_dev_start(id);
    if (res != 0)
        rte_exit(EXIT_FAILURE, "[Port %u] Unable to start device: Reason: %s\n",
                id, res);
    
    res = rte_eth_link_get(id, &link);
    if (res != 0)
        rte_exit(EXIT_FAILURE, "[Port %u] Failed to get link Status: Reason: %s\n",
                id, res);
    
    rte_eth_link_to_str(link_status_str, sizeof(link_status_str), &link);
    printf("%s\n", link_status_str);

    rte_eth_promiscuous_enable(conf_port_id);
}