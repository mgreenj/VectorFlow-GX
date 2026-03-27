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

#ifndef INCLUDE_VKF_VKF_H
#define INCLUDE_VKF_VKF_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include <infiniband/verbs.h>
#include <cuda.h>
#include <gdrapi.h>

#define VKF_ETHERTYPE           0x88B5

#define VKF_MAX_NODES           64
#define VKF_DISC_FRAME_LEN      256


#define VKF_IB_PORT             1
#define VKF_CQ_DEPTH            256
#define VKF_QP_MAX_SEND_WR      128
#define VKF_QP_MAX_RECV_WR      128
#define VKF_QP_MAX_SGE          1
#define VKF_PSN_INIT            0x000000
#define VKF_PKEY_IDX            0
#define VKF_ACCESS_FLAGS        (IBV_ACCESS_LOCAL_WRITE  | \
                                 IBV_ACCESS_REMOTE_WRITE | \
                                 IBV_ACCESS_REMOTE_READ  | \
                                 IBV_ACCESS_REMOTE_ATOMIC)

#define VKF_GPU_PAGE_SIZE       (1UL << 16)

typedef enum {
    VKF_BUF_TYPE_RX = 0,
    VKF_BUF_TYPE_TX,
    VKF_BUF_TYPE_CTRL,
} vkf_buf_type_t;

typedef struct __attribute__((packed)) vkf_buffer_descriptor {
    uint64_t        remote_addr;
    uint32_t        rkey;
    uint32_t        size;
    uint32_t        node_id;
    uint8_t         buffer_type;

    uint32_t        qpn;
    uint8_t         gid[16];
    uint8_t         mac[6];
} vkf_buffer_descriptor_t;

typedef struct __attribute__((packed)) vkf_disc_frame {
    uint8_t                 magic[4];
    uint8_t                 version;
    uint8_t                 reserved[3];
    vkf_buffer_descriptor_t desc;
} vkf_disc_frame_t;

#define VKF_DISC_MAGIC  "VKF"

typedef struct vkf_ctx {
    void               *gpu_ptr;
    size_t              gpu_len;
    CUdeviceptr         cu_dev_ptr;
    gdr_t               gdr;
    gdr_mh_t            gdr_mh;
    void               *gdr_map_ptr;
    struct ibv_mr      *gpu_mr;
    struct ibv_context *ibv_ctx;
    struct ibv_pd      *pd;
    struct ibv_cq      *cq;
    struct ibv_qp      *qp;
    union  ibv_gid      local_gid;
    uint8_t             local_mac[6];
    int                 raw_sock;
    char                ifname[32];
    int                 ifindex;
    uint32_t            node_id;
    vkf_buffer_descriptor_t remotes[VKF_MAX_NODES];
    bool                    remote_valid[VKF_MAX_NODES];
    int                     nb_remotes;
} vkf_ctx_t;


int vkf_init(vkf_ctx_t *ctx,
             const char *ib_devname,
             const char *ifname,
             void *gpu_ptr, size_t gpu_len,
             uint32_t node_id);

int vkf_discover(vkf_ctx_t *ctx, int timeout_ms);

int vkf_write(vkf_ctx_t *ctx,
              uint32_t node_id,
              uint64_t local_off,
              uint64_t remote_off,
              uint32_t len,
              uint64_t wr_id);

int vkf_read(vkf_ctx_t *ctx,
             uint32_t node_id,
             uint64_t local_off,
             uint64_t remote_off,
             uint32_t len,
             uint64_t wr_id);

int vkf_poll(vkf_ctx_t *ctx, struct ibv_wc *wcs, int max_wcs);


void vkf_free(vkf_ctx_t *ctx);

int  vkf_gpu_gdr_pin(vkf_ctx_t *ctx);
void vkf_gpu_gdr_unpin(vkf_ctx_t *ctx);
int  vkf_gpu_reg_mr(vkf_ctx_t *ctx);

int  vkf_ib_open(vkf_ctx_t *ctx, const char *ib_devname);
int  vkf_ib_create_cq(vkf_ctx_t *ctx);
int  vkf_ib_create_qp(vkf_ctx_t *ctx);
int  vkf_ib_qp_to_init(vkf_ctx_t *ctx);
int  vkf_ib_qp_to_rtr(vkf_ctx_t *ctx, const vkf_buffer_descriptor_t *remote);
int  vkf_ib_qp_to_rts(vkf_ctx_t *ctx);
void vkf_ib_close(vkf_ctx_t *ctx);

int  vkf_disc_open_sock(vkf_ctx_t *ctx);
int  vkf_disc_send(vkf_ctx_t *ctx);
int  vkf_disc_recv(vkf_ctx_t *ctx, int timeout_ms);
void vkf_disc_close_sock(vkf_ctx_t *ctx);

int  vkf_rdma_post_write(vkf_ctx_t *ctx,
                         uint64_t local_addr, uint32_t lkey,
                         uint64_t remote_addr, uint32_t rkey,
                         uint32_t len, uint64_t wr_id);
int  vkf_rdma_initiate_read(vkf_ctx_t *ctx,
                        uint64_t local_addr, uint32_t lkey,
                        uint64_t remote_addr, uint32_t rkey,
                        uint32_t len, uint64_t wr_id);
int  vkf_rdma_post_fence(vkf_ctx_t *ctx, uint64_t wr_id);

#endif /* INCLUDE_VKF_VKF_H */
