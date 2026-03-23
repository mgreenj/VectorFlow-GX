#include "vectorflow/init.h"


void vfgx_gpu_alloc(void)
{
    int res;
    CUresult cu_res;
    CUdeviceptr dev_ptr;
    CUmemorytype mem_type;

    /* 2KB Dataroom + RTE_PKTMBUF_HEADROOM avoids splitting
       Eth frame into multiple segments */
    gpu_mem.elt_size = VFGX_DT_SIZE + RTE_PKTMBUF_HEADROOM;
    gpu_mem.buf_len  = RTE_ALIGN_CEIL(VFGX_NUM_MBUF * gpu_mem.elt_size,
                                      GPU_PAGE_SIZE);
    gpu_mem.buf_iova = RTE_BAD_IOVA;

    gpu_mem.buf_ptr = rte_gpu_mem_alloc(VFGX_GPU_ID, gpu_mem.buf_len,
                                        VFGX_CPU_ALIGNMENT);
    if (gpu_mem.buf_ptr == NULL)
        rte_exit(EXIT_FAILURE, "Could not allocate GPU device memory\n");

    /**
     * 
     * using Cuda API to get the physical address. The pointer returned
     * to gpu_mem.buf_ptr is cast to CUdeviceptr so that I can use 
     * cuPointerGetAttribute to query for certain pointer attributes.
     * 
     * First need to check to make sure the memory in the CUcontext
     * is device memory and not host memory.
     */

    dev_ptr = (CUdeviceptr)gpu_mem.buf_ptr;
    cu_res = cuPointerGetAttribute(&mem_type,
                        CU_POINTER_ATTRIBUTE_MEMORY_TYPE, dev_ptr);
    if (cu_res != CUDA_SUCCESS || mem_type != CU_MEMORYTYPE_DEVICE)
        rte_exit(EXIT_FAILURE, "Something is very wrong, gpu_mem.buf_ptr "
                "is pointing to Host memory!\n");

    res = rte_gpu_mem_register(VFGX_GPU_ID, gpu_mem.buf_len, gpu_mem.buf_ptr);
    if (res)
        rte_exit(EXIT_FAILURE,
                 "Could not register GPU addr %p with EAL. Reason: %d\n",
                 gpu_mem.buf_ptr, res);

    /* only run rte_extmem_register when the `CU_CTX_MAP_HOST` flag is 
     * not set in the CUDA context. In this case, rte_gpu_mem_register
     * will fail when cuda_mem_register attempts to register host 
     * memory using pfn_cuMemHostRegister 
     */

    printf("GPU mem: virt=%p, iova=0x%"PRIx64", len=%zu\n",
            gpu_mem.buf_ptr, gpu_mem.buf_iova, gpu_mem.buf_len);
}

void vfgx_gpu_mempool_create(void)
{
    vfgx_mempool_payload = rte_pktmbuf_pool_create_extbuf(
        "VFGX_GPU_MEMPOOL",
        VFGX_NUM_MBUF, 0, 0,
        gpu_mem.elt_size,
        rte_socket_id(), &gpu_mem, 1);

    if (vfgx_mempool_payload == NULL)
        rte_exit(EXIT_FAILURE,
                 "Could not create external mempool on GPU device\n");

#if __DPDK_EXPERIMENTAL__
#endif
}

int vfgx_gpu_init(void)
{
    int nb_gpus = 0;
    int16_t gpuidx = 0;
    struct rte_gpu_info gpuinfo;

    RTE_LOG(INFO, USER3, "Probing GPU device...\n");

    nb_gpus = rte_gpu_count_avail();
    if (nb_gpus == 0) {
        RTE_LOG(INFO, USER3, "No GPUs found via EAL, probing cuda device...\n");

        int probed = 0;
        char dev_name[32];
        for (int i = 0; i < VFGX_MAX_GPUS; i++) {
            snprintf(dev_name, sizeof(dev_name), "gpu_cuda%d", i);
            if (rte_dev_probe(dev_name) < 0)
                break;
            probed++;
        }

        if (probed == 0)
            rte_exit(EXIT_FAILURE,
                "Failed to probe gpu_cuda0. Is rte_gpu_cuda.so loaded? "
                "Check --vdev or LD_LIBRARY_PATH.\n");

        nb_gpus = rte_gpu_count_avail();
    }

    printf("\nVectorFlow-GX found %d GPUs\n", nb_gpus);

    if (nb_gpus == 0)
        rte_exit(EXIT_FAILURE, "Found 0 GPUs after probe\n");

    RTE_GPU_FOREACH(gpuidx)
    {
        if (rte_gpu_info_get(gpuidx, &gpuinfo))
            rte_exit(EXIT_FAILURE, "Failed to Retrieve GPU Information\n");
        
        printf(
            "GPU ID: %d | Parent: %d | SMs: %d | NUMA: %d | Memory: %.2f MB\n",
            gpuinfo.dev_id,
            gpuinfo.parent,
            gpuinfo.processor_count,
            gpuinfo.numa_node,
            (float)gpuinfo.total_memory / (1024.0f * 1024.0f)
        );

    }

    return 0;
}