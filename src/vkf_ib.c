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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <infiniband/verbs.h>
#include "vkf/vkf.h"


int vkf_ib_open(vkf_ctx_t *ctx, const char *ib_devname)
{
    struct ibv_device **devlist;
    struct ibv_device  *dev = NULL;
    int nb_devs, i, rc;

    devlist = ibv_get_device_list(&nb_devs);
    if (!devlist || nb_devs == 0) {
        fprintf(stderr, "vkf_ib_open: no IB devices found\n");
        return -ENODEV;
    }

    if (ib_devname) {
        for (i = 0; i < nb_devs; i++) {
            if (!strcmp(ibv_get_device_name(devlist[i]), ib_devname)) {
                dev = devlist[i];
                break;
            }
        }
        if (!dev) {
            fprintf(stderr, "vkf_ib_open: device '%s' not found\n", ib_devname);
            ibv_free_device_list(devlist);
            return -ENOENT;
        }
    } else {
        dev = devlist[0];
    }

    printf("vkf_ib_open: using IB device '%s'\n", ibv_get_device_name(dev));

    ctx->ibv_ctx = ibv_open_device(dev);
    ibv_free_device_list(devlist);

    if (!ctx->ibv_ctx) {
        fprintf(stderr, "vkf_ib_open: ibv_open_device() failed: %s\n",
                strerror(errno));
        return -errno;
    }

   
    rc = ibv_query_gid(ctx->ibv_ctx, VKF_IB_PORT, 0, &ctx->local_gid);
    if (rc) {
        fprintf(stderr, "vkf_ib_open: ibv_query_gid() rc=%d\n", rc);
        ibv_close_device(ctx->ibv_ctx);
        ctx->ibv_ctx = NULL;
        return -rc;
    }

    ctx->pd = ibv_alloc_pd(ctx->ibv_ctx);
    if (!ctx->pd) {
        fprintf(stderr, "vkf_ib_open: ibv_alloc_pd() failed: %s\n",
                strerror(errno));
        ibv_close_device(ctx->ibv_ctx);
        ctx->ibv_ctx = NULL;
        return -errno;
    }

    printf("vkf_ib_open: PD allocated, GID %02x:%02x:%02x:%02x:...\n",
           ctx->local_gid.raw[0], ctx->local_gid.raw[1],
           ctx->local_gid.raw[2], ctx->local_gid.raw[3]);

    return 0;
}


int vkf_ib_create_cq(vkf_ctx_t *ctx)
{
    ctx->cq = ibv_create_cq(ctx->ibv_ctx, VKF_CQ_DEPTH, NULL, NULL, 0);
    if (!ctx->cq) {
        fprintf(stderr, "vkf_ib_create_cq: ibv_create_cq() failed: %s\n",
                strerror(errno));
        return -errno;
    }
    printf("vkf_ib_create_cq: CQ depth=%d\n", VKF_CQ_DEPTH);
    return 0;
}


int vkf_ib_create_qp(vkf_ctx_t *ctx)
{
    struct ibv_qp_init_attr qp_attr = {
        .send_cq        = ctx->cq,
        .recv_cq        = ctx->cq,
        .qp_type        = IBV_QPT_RC,
        .cap = {
            .max_send_wr     = VKF_QP_MAX_SEND_WR,
            .max_recv_wr     = VKF_QP_MAX_RECV_WR,
            .max_send_sge    = VKF_QP_MAX_SGE,
            .max_recv_sge    = VKF_QP_MAX_SGE,
            .max_inline_data = 0,
        },
        .sq_sig_all = 0,   
    };

    ctx->qp = ibv_create_qp(ctx->pd, &qp_attr);
    if (!ctx->qp) {
        fprintf(stderr, "vkf_ib_create_qp: ibv_create_qp() failed: %s\n",
                strerror(errno));
        return -errno;
    }

    printf("vkf_ib_create_qp: QPN=0x%06x\n", ctx->qp->qp_num);
    return 0;
}


int vkf_ib_qp_to_init(vkf_ctx_t *ctx)
{
    struct ibv_qp_attr attr = {
        .qp_state        = IBV_QPS_INIT,
        .pkey_index      = VKF_PKEY_IDX,
        .port_num        = VKF_IB_PORT,
        .qp_access_flags = VKF_ACCESS_FLAGS,
    };
    int flags = IBV_QP_STATE | IBV_QP_PKEY_INDEX |
                IBV_QP_PORT  | IBV_QP_ACCESS_FLAGS;

    int rc = ibv_modify_qp(ctx->qp, &attr, flags);
    if (rc) {
        fprintf(stderr, "vkf_ib_qp_to_init: RESET→INIT failed rc=%d\n", rc);
        return -rc;
    }
    printf("vkf_ib_qp_to_init: QP is INIT\n");
    return 0;
}


int vkf_ib_qp_to_rtr(vkf_ctx_t *ctx, const vkf_buffer_descriptor_t *remote)
{
    struct ibv_qp_attr attr = {
        .qp_state               = IBV_QPS_RTR,
        .path_mtu               = IBV_MTU_1024,
        .dest_qp_num            = remote->qpn,
        .rq_psn                 = VKF_PSN_INIT,
        .max_dest_rd_atomic     = 1,
        .min_rnr_timer          = 12,
        .ah_attr = {
            .is_global          = 1,
            .port_num           = VKF_IB_PORT,
            .sl                 = 0,
            .src_path_bits      = 0,
            .grh = {
                .hop_limit      = 1,
                .sgid_index     = 0,   
               
            },
        },
    };

    static_assert(sizeof(attr.ah_attr.grh.dgid) == 16,
                  "dgid size mismatch");
    memcpy(&attr.ah_attr.grh.dgid, remote->gid, 16);

    int flags = IBV_QP_STATE            |
                IBV_QP_AV               |
                IBV_QP_PATH_MTU         |
                IBV_QP_DEST_QPN         |
                IBV_QP_RQ_PSN           |
                IBV_QP_MAX_DEST_RD_ATOMIC |
                IBV_QP_MIN_RNR_TIMER;

    int rc = ibv_modify_qp(ctx->qp, &attr, flags);
    if (rc) {
        fprintf(stderr, "vkf_ib_qp_to_rtr: INIT→RTR failed rc=%d\n", rc);
        return -rc;
    }
    printf("vkf_ib_qp_to_rtr: QP is RTR, remote QPN=0x%06x\n", remote->qpn);
    return 0;
}


int vkf_ib_qp_to_rts(vkf_ctx_t *ctx)
{
    struct ibv_qp_attr attr = {
        .qp_state       = IBV_QPS_RTS,
        .timeout        = 14,  
        .retry_cnt      = 7,
        .rnr_retry      = 7,   
        .sq_psn         = VKF_PSN_INIT,
        .max_rd_atomic  = 1,
    };
    int flags = IBV_QP_STATE     |
                IBV_QP_TIMEOUT   |
                IBV_QP_RETRY_CNT |
                IBV_QP_RNR_RETRY |
                IBV_QP_SQ_PSN    |
                IBV_QP_MAX_QP_RD_ATOMIC;

    int rc = ibv_modify_qp(ctx->qp, &attr, flags);
    if (rc) {
        fprintf(stderr, "vkf_ib_qp_to_rts: RTR→RTS failed rc=%d\n", rc);
        return -rc;
    }
    printf("vkf_ib_qp_to_rts: QP is RTS — RDMA operations enabled\n");
    return 0;
}


void vkf_ib_close(vkf_ctx_t *ctx)
{
    if (ctx->gpu_mr) {
        ibv_dereg_mr(ctx->gpu_mr);
        ctx->gpu_mr = NULL;
    }
    if (ctx->qp) {
        ibv_destroy_qp(ctx->qp);
        ctx->qp = NULL;
    }
    if (ctx->cq) {
        ibv_destroy_cq(ctx->cq);
        ctx->cq = NULL;
    }
    if (ctx->pd) {
        ibv_dealloc_pd(ctx->pd);
        ctx->pd = NULL;
    }
    if (ctx->ibv_ctx) {
        ibv_close_device(ctx->ibv_ctx);
        ctx->ibv_ctx = NULL;
    }
}
