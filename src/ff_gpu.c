
#include "fastflow.h"


void _ff_gpu_mempool_init()
{
    int res;

    /* 2KB Dataroom + RTE_PKTMBUF_HEADROOM avoids splitting
    Eth frame into multiple segments */
    gpu_mem.elt_size = FF_DT_SIZE + RTE_PKTMBUF_HEADROOM;
    gpu_mem.buf_len = RTE_ALIGN_CEIL(FF_NUM_MBUF + gpu_mem.elt_size,
                                    GPU_PAGE_SIZE);

    gpu_mem.buf_iova = RTE_BAD_IOVA;
    gpu_mem.buf_ptr = rte_gpu_mem_alloc(FF_GPU_ID, gpu_mem.buf_len,
                                        FF_CPU_ALIGNMENT);
    if (gpu_mem.buf_ptr == NULL)
        rte_exit(EXIT_FAILURE, "Could not allocate GPU device memory\n");
    
    res rte_extmem_register(gpu_mem.buf_ptr, gpu_mem.buf_len, NULL,
                            gpu_mem.buf_iova, GPU_PAGE_SIZE);
    if (res)
        rte_exit(EXIT_FAILURE, "Could not register addr 0x%p. Reason: %d\n",
                gpu_mem.buf_ptr, res);
    
    res = rte_dev_dma_map(nic_dev_info.device, gpu_mem.buf_ptr, gpu_mem.buf_iova,
                         gpu_mem.buf_len);
    
    ff_mempool_payload = rte_pktmbuf_pool_create_extbuf(
        "FF_GPU_MEMPOOL",
        FF_NUM_MBUF, 0, 0,
        gpu_mem.elt_size,   // Data room + RTE_PKTMBUF_HEADROOM
        rte_socket_id(), &gpu_mem, 1);

    if (ff_mempool_payload == NULL)
        rte_exit(EXIT_FAILURE, "Could not create External membpool on GPU Device\n");
    
    /**
     * Protocol buffer splitting splits an received packet into two separate regions 
     * based on its content. This is ideal for GPU acceleration to enable true zero
     * copy.
     */

#if __DPDK_EXPERIMENTAL__
    // TODO: Add functionality for buffer split

#endif

}

int ff_gpu_init(void)
{
    int gpuidx;
    uint16_t nb_gpus;
    rte_gpu_info gpuinfo;

    RTE_LOG(INFO, FF, "Gethering GPU Information %"PRIu16"... ", FF_GPU_ID);
    fflush(stdout);

    nb_gpus = rte_gpu_count_avail();
    printf("\nFastFlow found %d GPUs\n", nb_gpus);

    RTE_GPU_FOREACH(gpuidx)
    {
        if (rte_gpu_info_get(gpuidx, &gpuinfo))
            rte_exit(EXIT_WARNING, "Failed to Retrieve GPU Information\n");
        
        RTE_LOG(INFO, FF, "GPU information gathered %"PRIu16"... ", FF_GPU_ID);
        printf(
            "GPU ID: %d\nParent ID: %d\nProcessor Count: %d\nNUMA Node ID: %d\nTotal Memory: %.02f MB\n",
            gpuinfo.dev_id,
            gpuinfo.parent,
            gpuinfo.processor_count,
            gpuinfo.numa_node,
            (((float)gpuinfo.total_memory/(float(1024)/(float)1024))
        );
    }

    if (nb_gpus == 0)
        rte_exit(EXIT_WARNING, "Found %d GPUs\n", nb_gpus);
    
    _ff_gpu_mempool_init();
}