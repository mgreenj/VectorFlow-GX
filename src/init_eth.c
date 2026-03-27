/*
 * Copyright (C) 2026 Maurice Green
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include "vectorflow/init.h"

struct ring_conf vfgx_ring_conf = {
    .rx_size    = VFGX_RX_DESC_SIZE,
    .ring_size  = VFGX_RING_SIZE,
    .tx_size    = VFGX_TX_DESC_SIZE
};

struct rte_eth_thresh vfgx_rx_thresh = {
    .pthresh    = RX_PTHRESH,
    .hthresh    = RX_HTHRESH,
    .wthresh    = RX_WTHRESH
};

struct rte_eth_thresh vfgx_tx_thresh = {
    .pthresh    = TX_PTHRESH,
    .hthresh    = TX_HTHRESH,
    .wthresh    = TX_WTHRESH
};

struct burst_conf vfgx_burst_conf = {
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

static const struct rte_eth_conf vfgx_eth_conf = {
    .link_speeds = VFGX_LINK_SPEED,
    .rxmode = {
        .mq_mode            = RTE_ETH_MQ_RX_NONE,
        .max_lro_pkt_size   = RTE_ETHER_MAX_LEN

    },
};

int vfgx_init_ethport(uint16_t id)
{
    int res;
    struct rte_eth_link link;
    struct rte_eth_rxconf vfgx_rx_conf = {0};
    struct rte_eth_txconf vfgx_tx_conf = {0};
    uint16_t nb_ports;
    struct rte_eth_conf port_conf = vfgx_eth_conf;
    char link_status_str[RTE_ETH_LINK_MAX_STR_LEN];

    memset(&vfgx_rx_conf, 0, sizeof(struct rte_eth_rxconf));
    vfgx_rx_conf.rx_thresh      = vfgx_rx_thresh;
    vfgx_rx_conf.rx_free_thresh = RX_FREE_THRESH;

    memset(&vfgx_tx_conf, 0, sizeof(struct rte_eth_txconf));
    vfgx_tx_conf.tx_thresh      = vfgx_tx_thresh;
    vfgx_tx_conf.tx_free_thresh = TX_FREE_THRESH;

    nb_ports = rte_eth_dev_count_avail();
    if (nb_ports == 0)
        rte_exit(EXIT_FAILURE, "No ethernet ports available!\n");

    printf("\nDetected %d ports available\n", nb_ports );
    
    RTE_LOG(INFO, USER3, "Initializing port %"PRIu16"...\n", id);

    res = rte_eth_dev_info_get(id, &nic_dev_info);
    if (res != 0)
        rte_exit(EXIT_FAILURE,
                 "[Port %u] Unable to retrieve device info. Reason: %s\n",
                 id, strerror(-res));
    printf("Device driver in use: %s\n", nic_dev_info.driver_name);

#if __DPDK_VERSION__ < 26
    if (nic_dev_info.rx_offload_capa & RTE_ETH_RX_OFFLOAD_SCATTER)
        port_conf.rxmode.offloads |= RTE_ETH_RX_OFFLOAD_SCATTER;
#endif
#if __DPDK_EXPERIMENTAL__
    if (nic_dev_info.rx_offload_capa & RTE_ETH_RX_OFFLOAD_BUFFER_SPLIT)
        port_conf.rxmode.offloads |= RTE_ETH_RX_OFFLOAD_BUFFER_SPLIT;
#endif
    if (nic_dev_info.tx_offload_capa & RTE_ETH_TX_OFFLOAD_MULTI_SEGS)
        port_conf.txmode.offloads |= RTE_ETH_TX_OFFLOAD_MULTI_SEGS;

    vfgx_gpu_alloc();

    res = rte_dev_dma_map(nic_dev_info.device, gpu_mem.buf_ptr,
                          gpu_mem.buf_iova, gpu_mem.buf_len);
    if (res)
        rte_exit(EXIT_FAILURE,
                 "[Port %u] Failed to DMA map GPU memory. Reason: %d\n",
                 id, res);
    
    printf("DMA map success | driver: %s\n", nic_dev_info.driver_name);

    vfgx_gpu_mempool_create();

    res = rte_eth_dev_configure(id, 1, 1, &port_conf);
    if (res < 0)
        rte_exit(EXIT_FAILURE,
                 "[Port %u] Unable to configure device. Reason: %d\n",
                 id, res);

    res = rte_eth_dev_adjust_nb_rx_tx_desc(id,
                                           (uint16_t *)&vfgx_ring_conf.rx_size,
                                           (uint16_t *)&vfgx_ring_conf.tx_size);
    if (res != 0)
        rte_exit(EXIT_FAILURE,
                 "[Port %u] Unable to adjust RX/TX ring size. Reason: %d\n",
                 id, res);

    /* Step 5: setup RX queue with the GPU mempool */
    RTE_LOG(INFO, USER3, "Initializing Rx Queue on port %"PRIu16"...\n", id);
    vfgx_rx_conf.offloads = port_conf.rxmode.offloads;

    res = rte_eth_rx_queue_setup(id, 0, (uint16_t)vfgx_ring_conf.rx_size,
                                 rte_eth_dev_socket_id(id),
                                 &vfgx_rx_conf, vfgx_mempool_payload);
    if (res != 0)
        rte_exit(EXIT_FAILURE,
                 "[Port %u] Unable to setup Rx Queue. Reason: %d\n",
                 id, res);

    RTE_LOG(INFO, USER3, "Initializing Tx Queue on port %"PRIu16"...\n", id);
    vfgx_tx_conf.offloads = port_conf.txmode.offloads;

    res = rte_eth_tx_queue_setup(id, 0, (uint16_t)vfgx_ring_conf.tx_size,
                                 rte_eth_dev_socket_id(id), &vfgx_tx_conf);
    if (res != 0)
        rte_exit(EXIT_FAILURE,
                 "[Port %u] Unable to setup Tx Queue. Reason: %d\n",
                 id, res);

    RTE_LOG(INFO, USER3, "Starting device on port %"PRIu16"...\n", id);
    res = rte_eth_dev_start(id);
    if (res != 0)
        rte_exit(EXIT_FAILURE,
                 "[Port %u] Unable to start device. Reason: %d\n",
                 id, res);

    res = rte_eth_link_get(id, &link);
    if (res != 0)
        rte_exit(EXIT_FAILURE,
                 "[Port %u] Failed to get link status. Reason: %d\n",
                 id, res);

    rte_eth_link_to_str(link_status_str, sizeof(link_status_str), &link);
    printf("%s\n", link_status_str);

    rte_eth_promiscuous_enable(id);

    return 0;
}
