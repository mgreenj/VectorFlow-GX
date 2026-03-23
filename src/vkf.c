
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "vkf/vkf.h"

int vkf_init(vkf_ctx_t *ctx,
             const char *ib_devname,
             const char *ifname,
             void *gpu_ptr, size_t gpu_len,
             uint32_t node_id)
{
    int rc;

    if (!ctx || !ifname || !gpu_ptr || gpu_len == 0)
        return -EINVAL;

    memset(ctx, 0, sizeof(*ctx));
    ctx->raw_sock = -1;
    ctx->gpu_ptr  = gpu_ptr;
    ctx->gpu_len  = gpu_len;
    ctx->gpu_len  = gpu_len;
    ctx->cu_dev_ptr = (CUdeviceptr)(uintptr_t)gpu_ptr;
    ctx->node_id  = node_id;
    strncpy(ctx->ifname, ifname, sizeof(ctx->ifname) - 1);

   
    rc = vkf_ib_open(ctx, ib_devname);
    if (rc) {
        fprintf(stderr, "vkf_init: vkf_ib_open() failed: %d\n", rc);
        return rc;
    }

   
    rc = vkf_gpu_gdr_pin(ctx);
    if (rc) {
        fprintf(stderr, "vkf_init: vkf_gpu_gdr_pin() failed: %d\n", rc);
        vkf_ib_close(ctx);
        return rc;
    }

   
    rc = vkf_gpu_reg_mr(ctx);
    if (rc) {
        fprintf(stderr, "vkf_init: vkf_gpu_reg_mr() failed: %d\n", rc);
        vkf_gpu_gdr_unpin(ctx);
        vkf_ib_close(ctx);
        return rc;
    }

   
    rc = vkf_ib_create_cq(ctx);
    if (rc) {
        fprintf(stderr, "vkf_init: vkf_ib_create_cq() failed: %d\n", rc);
        goto err_ib;
    }

    rc = vkf_ib_create_qp(ctx);
    if (rc) {
        fprintf(stderr, "vkf_init: vkf_ib_create_qp() failed: %d\n", rc);
        goto err_ib;
    }

    rc = vkf_ib_qp_to_init(ctx);
    if (rc) {
        fprintf(stderr, "vkf_init: vkf_ib_qp_to_init() failed: %d\n", rc);
        goto err_ib;
    }

    printf("vkf_init: ready — node_id=%u gpu_ptr=%p gpu_len=%zu "
           "lkey=0x%08x rkey=0x%08x QPN=0x%06x\n",
           node_id, gpu_ptr, gpu_len,
           ctx->gpu_mr->lkey, ctx->gpu_mr->rkey,
           ctx->qp->qp_num);

    return 0;

err_ib:
    vkf_gpu_gdr_unpin(ctx);
    vkf_ib_close(ctx);
    return rc;
}




int vkf_discover(vkf_ctx_t *ctx, int timeout_ms)
{
    int rc;

    rc = vkf_disc_open_sock(ctx);
    if (rc) {
        fprintf(stderr, "vkf_discover: vkf_disc_open_sock() failed: %d\n", rc);
        return rc;
    }

    rc = vkf_disc_send(ctx);
    if (rc) {
        fprintf(stderr, "vkf_discover: vkf_disc_send() failed: %d\n", rc);
        vkf_disc_close_sock(ctx);
        return rc;
    }

    rc = vkf_disc_recv(ctx, timeout_ms);
    if (rc < 0) {
        fprintf(stderr, "vkf_discover: vkf_disc_recv() failed: %d\n", rc);
        vkf_disc_close_sock(ctx);
        return rc;
    }

    printf("vkf_discover: %d remote(s) discovered\n", rc);
    return rc;
}


int vkf_write(vkf_ctx_t *ctx,
              uint32_t node_id,
              uint64_t local_off,
              uint64_t remote_off,
              uint32_t len,
              uint64_t wr_id)
{
    uint32_t slot = node_id % VKF_MAX_NODES;

    if (!ctx->remote_valid[slot] || ctx->remotes[slot].node_id != node_id) {
        fprintf(stderr, "vkf_write: unknown remote node_id=%u\n", node_id);
        return -ENOENT;
    }

    const vkf_buffer_descriptor_t *rem = &ctx->remotes[slot];

    if (local_off + len > ctx->gpu_len ||
        remote_off + len > rem->size) {
        fprintf(stderr, "vkf_write: transfer out of bounds\n");
        return -ERANGE;
    }

    uint64_t local_addr  = (uint64_t)(uintptr_t)ctx->gpu_ptr + local_off;
    uint64_t remote_addr = rem->remote_addr + remote_off;

    return vkf_rdma_post_write(ctx,
                               local_addr,  ctx->gpu_mr->lkey,
                               remote_addr, rem->rkey,
                               len, wr_id);
}


int vkf_read(vkf_ctx_t *ctx,
             uint32_t node_id,
             uint64_t local_off,
             uint64_t remote_off,
             uint32_t len,
             uint64_t wr_id)
{
    uint32_t slot = node_id % VKF_MAX_NODES;

    if (!ctx->remote_valid[slot] || ctx->remotes[slot].node_id != node_id) {
        fprintf(stderr, "vkf_read: unknown remote node_id=%u\n", node_id);
        return -ENOENT;
    }

    const vkf_buffer_descriptor_t *rem = &ctx->remotes[slot];

    if (local_off + len > ctx->gpu_len ||
        remote_off + len > rem->size) {
        fprintf(stderr, "vkf_read: transfer out of bounds\n");
        return -ERANGE;
    }

    uint64_t local_addr  = (uint64_t)(uintptr_t)ctx->gpu_ptr + local_off;
    uint64_t remote_addr = rem->remote_addr + remote_off;

    return vkf_rdma_initiate_read(ctx,
                              local_addr,  ctx->gpu_mr->lkey,
                              remote_addr, rem->rkey,
                              len, wr_id);
}




int vkf_poll(vkf_ctx_t *ctx, struct ibv_wc *wcs, int max_wcs)
{
    int ne = ibv_poll_cq(ctx->cq, max_wcs, wcs);
    if (ne < 0) {
        fprintf(stderr, "vkf_poll: ibv_poll_cq() error: %d\n", ne);
        return ne;
    }

    for (int i = 0; i < ne; i++) {
        if (wcs[i].status != IBV_WC_SUCCESS) {
            fprintf(stderr, "vkf_poll: WC error wr_id=%llu status=%s vendor=%u\n",
                    (unsigned long long)wcs[i].wr_id,
                    ibv_wc_status_str(wcs[i].status),
                    wcs[i].vendor_err);
        }
    }
    return ne;
}




void vkf_free(vkf_ctx_t *ctx)
{
    if (!ctx) return;

    vkf_disc_close_sock(ctx);
    vkf_gpu_gdr_unpin(ctx);
    vkf_ib_close(ctx);    

    memset(ctx, 0, sizeof(*ctx));
    ctx->raw_sock = -1;
}
