
#ifndef FASTFLOW_H
#define FASTFLOW_H

#ifdef __cplusplu
extern "C" {
#endif

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <rte_log.h>
#include <rte_errno.h>
#include <rte_sched.h>
#include <rte_ether.h>
#include <rte_memory.h>
#include <rte_ethdev.h>
#include <rte_gpudev.h>

// DPDK Version 
#define __DPDK_VERSION__        26
#define __DPDK_EXPERIMENTAL__   1

/* default log type */
#define FASTFLOW_LOGTYPE        RTE_LOGTYPE_USER3

/* Configure Ring descriptors */
/* Defaults consistent with driver for Broadcom (BCM57412 NetXtreme-E 10 GB) */
/*      Driver bnxt_en, version 6.14.8-2 */
/* 
 * I referenced DPDK documentation for TX/RX queue configuration
 *      - https://doc.dpdk.org/guides-18.05/prog_guide/poll_mode_drv.html) 
 */

// descriptor size
#define FF_TX_DESC_SIZE         2048
#define FF_RX_DESC_SIZE         2048

#define MAX_PKT_RX_BURST        64
#define PKT_ENQUEUE             64
#define PKT_DEQUEUE             63
#define MAX_PKT_TX_BURST        64

// Thresholds
#define RX_PTHRESH              8
#define RX_HTHRESH              8
#define RX_WTHRESH              0
#define RX_FREE_THRESH          32

#define TX_PTHRESH              32
#define TX_HTHRESH              0
#define TX_WTHRESH              0
#define TX_FREE_THRESH          32

// Link Defaults
#define FF_LINK_SPEED           RTE_ETH_LINK_SPEED_AUTONEG
#define FF_LINK_NEGOTIATION     RTE_ETH_LINK_AUTONEG
#define FF_LINK_MTU             1500

// GPU Defaults
#define FF_GPU_ID               0

#define GPU_PAGE_SHIFT          16
#define GPU_PAGE_SIZE           (1UL << GPU_PAGE_SHIFT)
#define GPU_PAGE_OFFSET         (GPU_PAGE_SIZE - 1)
#define GPU_PAGE_MASK           (~GPU_PAGE_OFFSET)

#define FF_CPU_ALIGNMENT        4096

/**
 * 
 * Following the same membory model used by OVS since I
 * don't have a framework for determining these numbers
 * on my own.
 * 
 * https://docs.openvswitch.org/en/latest/topics/dpdk/memory/
 * 
 * Adjustments may be necessary.
 * 
 */
#define FF_MIN_NB_MBUF          8192
#define FF_MAX_LCORE            128

// Ring size and Mbufs
#define FF_RING_SIZE            8192                    // For the software ring (i.e., rte_ring) 8 * 1024
#define FF_NUM_MBUF             22560                   // (1 * 2048) + (2 * 2048) + (1 * 32) + (16384)

#define FF_DT_SIZE              DEFAULT_MBUF_DATAROOM   // see https://doc.dpdk.org/api/rte__mbuf__core_8h.html


struct rte_pktmbuf_extmem gpu_mem;
struct rte_eth_dev_info nic_dev_info;
struct rte_mempool *ff_mempool_payload, *ff_mempool_hd;

struct __rte_cache_aligned th_conf
{
    uint16_t rx_port;
    uint16_t tx_port;
    uint16_t rx_queue;
    uint16_t tx_queue;
    struct rte_ring *rx_ring;
    struct rte_ring *tx_ring;
    struct rte_sched_port *sched_port;
};

struct flow_conf
{
	uint32_t rx_core;
	uint32_t wt_core;
	uint32_t tx_core;
	uint16_t rx_port;
	uint16_t tx_port;
	uint16_t rx_queue;
	uint16_t tx_queue;
	struct rte_ring *rx_ring;
	struct rte_ring *tx_ring;
	struct rte_sched_port *sched_port;
	struct rte_mempool *gpu_mbuf_pool;

	struct th_conf rx_thread;
	struct th_conf wt_thread;
	struct th_conf tx_thread;
};

struct ring_conf
{
	uint32_t rx_size;
	uint32_t ring_size;
	uint32_t tx_size;
};

struct burst_conf
{
	uint16_t rx_burst;
	uint16_t ring_burst;
	uint16_t qos_dequeue;
	uint16_t tx_burst;
};

extern struct ring_conf ring_conf;
extern struct burst_conf burst_conf;


#ifdef __cplusplus
}
#endif

#endif // FASTFLOW_H