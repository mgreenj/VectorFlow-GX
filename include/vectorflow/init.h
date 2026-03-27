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

#ifndef INCLUDE_VECTORFLOW_INIT_H
#define INCLUDE_VECTORFLOW_INIT_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#ifndef ALLOW_EXPERIMENTAL_API
#define ALLOW_EXPERIMENTAL_API
#endif

#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <getopt.h>

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <rte_log.h>
#include <rte_eal.h>
#include <rte_errno.h>
#include <rte_sched.h>
#include <rte_ether.h>
#include <rte_memory.h>
#include <rte_ethdev.h>
#include <rte_gpudev.h>
#include <rte_common.h>
#include <rte_mbuf.h>
#include <rte_dev.h>

#include <cuda.h>
#include <gdrapi.h>

// DPDK Version
#define __DPDK_VERSION__            26
#define __DPDK_EXPERIMENTAL__       1

/* default log type */
#define VECTORFLOW_GX_LOGTYPE       RTE_LOGTYPE_USER3

#define VFGX_MAX_GPUS               1

/* Configure Ring descriptors */
/* Defaults consistent with driver for Broadcom (BCM57412 NetXtreme-E 10 GB) */
/*      Driver bnxt_en, version 6.14.8-2 */
/*
 * I referenced DPDK documentation for TX/RX queue configuration
 *      - https://doc.dpdk.org/guides-18.05/prog_guide/poll_mode_drv.html)
 */

// descriptor size
#define VFGX_TX_DESC_SIZE           2048
#define VFGX_RX_DESC_SIZE           2048

#define MAX_PKT_RX_BURST            64
#define PKT_ENQUEUE                 64
#define PKT_DEQUEUE                 63
#define MAX_PKT_TX_BURST            64

// Thresholds
#define RX_PTHRESH                  8
#define RX_HTHRESH                  8
#define RX_WTHRESH                  0
#define RX_FREE_THRESH              32

#define TX_PTHRESH                  32
#define TX_HTHRESH                  0
#define TX_WTHRESH                  0
#define TX_FREE_THRESH              32

// Link Defaults
#define VFGX_LINK_SPEED             RTE_ETH_LINK_SPEED_AUTONEG
#define VFGX_LINK_NEGOTIATION       RTE_ETH_LINK_AUTONEG
#define VFGX_LINK_MTU               1500

// GPU Defaults
#define VFGX_GPU_ID                 0

#define GPU_PAGE_SHIFT              16
#define GPU_PAGE_SIZE               (1UL << GPU_PAGE_SHIFT)
//#define GPU_PAGE_OFFSET             (GPU_PAGE_SIZE - 1)
#define GPU_PAGE_MASK               (~GPU_PAGE_OFFSET)

#define VFGX_CPU_ALIGNMENT          4096

/*
 * Following the same memory model used by OVS:
 * https://docs.openvswitch.org/en/latest/topics/dpdk/memory/
 */
#define VFGX_MIN_NB_MBUF            8192
#define VFGX_MAX_LCORE              128

// Ring size and Mbufs
#define VFGX_RING_SIZE              8192        // software ring: 8 * 1024
#define VFGX_NUM_MBUF               22560       // (1*2048) + (2*2048) + (1*32) + 16384

#define VFGX_DT_SIZE                RTE_MBUF_DEFAULT_DATAROOM


extern struct rte_pktmbuf_extmem gpu_mem;
extern struct rte_eth_dev_info nic_dev_info;
extern struct rte_mempool *vfgx_mempool_payload;
extern struct rte_mempool *vfgx_mempool_hd;

/*
 * GPU functions (vfgx_gpu.c)
 *
 * Call order:
 *   vfgx_gpu_init()          - probe and enumerate GPU devices (call from main)
 *   vfgx_gpu_alloc()         - allocate GPU mem + register with EAL (call from vfgx_init_ethport)
 *   vfgx_gpu_mempool_create()- create extbuf mempool (call from vfgx_init_ethport after DMA map)
 */
int  vfgx_gpu_init(void);
void vfgx_gpu_alloc(void);
void vfgx_gpu_mempool_create(void);

/*
 * Ethernet functions (vfgx_eth.c)
 *
 * vfgx_init_ethport() handles steps 2-5 internally:
 *   - rte_eth_dev_info_get    (populates nic_dev_info.device)
 *   - vfgx_gpu_alloc          (GPU mem alloc + EAL register)
 *   - rte_dev_dma_map         (DMA map GPU mem to NIC)
 *   - vfgx_gpu_mempool_create (create extbuf mempool)
 *   - rte_eth_rx_queue_setup  (RX queue using the mempool)
 */
int vfgx_init_ethport(uint16_t id);

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

#endif /* INCLUDE_VECTORFLOW_INIT_H */