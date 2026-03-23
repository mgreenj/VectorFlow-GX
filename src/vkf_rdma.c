

#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <infiniband/verbs.h>
#include "vkf/vkf.h"


int vkf_rdma_post_write(vkf_ctx_t *ctx,
                        uint64_t local_addr, uint32_t lkey,
                        uint64_t remote_addr, uint32_t rkey,
                        uint32_t len, uint64_t wr_id)
{
    struct ibv_sge sge = {
        .addr   = local_addr,
        .length = len,
        .lkey   = lkey,
    };

    struct ibv_send_wr wr = {
        .wr_id              = wr_id,
        .sg_list            = &sge,
        .num_sge            = 1,
        .opcode             = IBV_WR_RDMA_WRITE,
        .send_flags         = IBV_SEND_SIGNALED,
        .wr.rdma = {
            .remote_addr    = remote_addr,
            .rkey           = rkey,
        },
    };
    struct ibv_send_wr *bad_wr = NULL;

    int rc = ibv_post_send(ctx->qp, &wr, &bad_wr);
    if (rc) {
        fprintf(stderr, "vkf_rdma_post_write: ibv_post_send() rc=%d\n", rc);
        return -rc;
    }
    return 0;
}


int vkf_rdma_post_read(vkf_ctx_t *ctx,
                       uint64_t local_addr, uint32_t lkey,
                       uint64_t remote_addr, uint32_t rkey,
                       uint32_t len, uint64_t wr_id)
{
    struct ibv_sge sge = {
        .addr   = local_addr,
        .length = len,
        .lkey   = lkey,
    };

    struct ibv_send_wr wr = {
        .wr_id              = wr_id,
        .sg_list            = &sge,
        .num_sge            = 1,
        .opcode             = IBV_WR_RDMA_READ,
        .send_flags         = IBV_SEND_SIGNALED,
        .wr.rdma = {
            .remote_addr    = remote_addr,
            .rkey           = rkey,
        },
    };
    struct ibv_send_wr *bad_wr = NULL;

    int rc = ibv_post_send(ctx->qp, &wr, &bad_wr);
    if (rc) {
        fprintf(stderr, "vkf_rdma_post_read: ibv_post_send() rc=%d\n", rc);
        return -rc;
    }
    return 0;
}


int vkf_rdma_post_fence(vkf_ctx_t *ctx, uint64_t wr_id)
{
   
    uint64_t local_scratch_addr = (uint64_t)(uintptr_t)ctx->gpu_ptr;
    uint32_t local_scratch_lkey = ctx->gpu_mr->lkey;

   
    uint64_t remote_fence_addr = 0;
    uint32_t remote_fence_rkey = 0;

    (void)remote_fence_addr;
    (void)remote_fence_rkey;

    struct ibv_sge sge = {
        .addr   = local_scratch_addr,
        .length = 8,
        .lkey   = local_scratch_lkey,
    };

    struct ibv_send_wr wr = {
        .wr_id      = wr_id,
        .sg_list    = &sge,
        .num_sge    = 1,
        .opcode     = IBV_WR_ATOMIC_CMP_AND_SWP,
        .send_flags = IBV_SEND_SIGNALED | IBV_SEND_FENCE,
        .wr.atomic  = {
            .remote_addr  = remote_fence_addr,
            .rkey         = remote_fence_rkey,
            .compare_add  = 0, 
            .swap         = 1, 
        },
    };
    struct ibv_send_wr *bad_wr = NULL;

   
    fprintf(stderr, "vkf_rdma_post_fence: TODO — fence addr/rkey not set\n");

    int rc = ibv_post_send(ctx->qp, &wr, &bad_wr);
    if (rc) {
        fprintf(stderr, "vkf_rdma_post_fence: ibv_post_send() rc=%d\n", rc);
        return -rc;
    }
    return 0;
}
