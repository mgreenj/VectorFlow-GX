#include <stdio.h>
#include <errno.h>
#include <string.h>

#include "vkf/vkf.h"

int vkf_gpu_gdr_pin(vkf_ctx_t *ctx)
{
    gdr_info_t info;
    int rc;

    ctx->gdr = gdr_open();
    if (!ctx->gdr) {
        fprintf(stderr, "vkf_gpu_gdr_pin: gdr_open() failed\n");
        return -ENODEV;
    }

    CHECK_RC(gdr_pin_buffer_v2(ctx->gdr, ctx->cu_dev_ptr, ctx->gpu_len,
                                GDR_PIN_FLAG_DEFAULT, &ctx->gdr_mh),
              err_close, "vkf_gpu_gdr_pin: gdr_pin_buffer() rc=%d\n", rc);

    CHECK_RC(gdr_map(ctx->gdr, ctx->gdr_mh, &ctx->gdr_map_ptr, ctx->gpu_len),
             err_unpin, "vkf_gpu_gdr_pin: gdr_map() rc=%d\n", rc);

    CHECK_RC(gdr_get_info(ctx->gdr, ctx->gdr_mh, &info,
             err_unmap, "vkf_gpu_gdr_pin: gdr_get_info() rc=%d\n", rc);

    printf("vkf_gpu_gdr_pin: gpu_va=0x%llx bar_pa=0x%llx len=%zu\n",
           (unsigned long long)ctx->cu_dev_ptr,
           (unsigned long long)info.mapped_size,
           ctx->gpu_len);

    return 0;

err_unmap:
    rte_gpu_mem_cpu_unmap(ctx->gdr, ctx->gdr_mh, ctx->gdr_map_ptr, ctx->gpu_len);
err_unpin:
    gdr_unpin_buffer(ctx->gdr, ctx->gdr_mh);
err_close:
    gdr_close(ctx->gdr);
    ctx->gdr = NULL;
    return rc;
}

void vkf_gpu_gdr_unpin(vkf_ctx_t *ctx)
{
    if (ctx->gdr_map_ptr) {
        gdr_unmap(ctx->gdr, ctx->gdr_mh, ctx->gdr_map_ptr, ctx->gpu_len);
        ctx->gdr_map_ptr = NULL;
    }
    if (ctx->gdr) {
        gdr_unpin_buffer(ctx->gdr, ctx->gdr_mh);
        gdr_close(ctx->gdr);
        ctx->gdr = NULL;
    }
}

int vkf_gpu_reg_mr(vkf_ctx_t *ctx)
{

    ctx->gpu_mr = ibv_reg_mr(ctx->pd, ctx->gpu_ptr, ctx->gpu_len,
                             VKF_ACCESS_FLAGS);
    if (!ctx->gpu_mr) {
        fprintf(stderr, "vkf_gpu_reg_mr: ibv_reg_mr() failed: %s\n",
                strerror(errno));
        return -errno;
    }

    printf("vkf_gpu_reg_mr: lkey=0x%08x rkey=0x%08x addr=%p len=%zu\n",
           ctx->gpu_mr->lkey,
           ctx->gpu_mr->rkey,
           ctx->gpu_ptr,
           ctx->gpu_len);
           
    return 0;
}
