#include "vectorflow/init.h"
#include <signal.h>


struct rte_pktmbuf_extmem gpu_mem;
struct rte_eth_dev_info nic_dev_info;
struct rte_mempool *vfgx_mempool_payload;
struct rte_mempool *vfgx_mempool_hd;

CUdevice cu_dev;
CUcontext cu_ctx;

static volatile bool vfgx_quit;

enum app_args { ARG_HELP };

static void usage(const char *prog_name)
{
    printf("%s [EAL options] -- \n", prog_name);
}

void signal_handler(int signum)
{
    if (signum == SIGINT || signum == SIGTERM || signum == SIGUSR1) {
        printf("\nSignal %d received!\n", signum);
        RTE_GPU_VOLATILE(vfgx_quit) = 1;
    }
}

static void args_parse(int argc, char **argv)
{
	char **argvopt;
	int opt, opt_idx;

	static struct option lgopts[] = {
		{ "help", 0, 0, ARG_HELP},
		{ 0, 0, 0, 0 }
	};

	argvopt = argv;
	while ((opt = getopt_long(argc, argvopt, "", lgopts, &opt_idx)) != EOF)
    {
		switch (opt) {
		case ARG_HELP:
			usage(argv[0]);
			break;
		default:
			usage(argv[0]);
			rte_exit(EXIT_FAILURE, "Invalid option: %s\n", argv[optind]);
			break;
		}
	}
}

int main(int argc, char **argv)
{
    int res;
    uint16_t port_id = 0;

    RTE_GPU_VOLATILE(vfgx_quit) = 0;
    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGUSR1, signal_handler);

    res = rte_eal_init(argc, argv);
    if (res < 0)
        rte_exit(EXIT_FAILURE, "EAL Init Failed!\n");

    argc -= res;
    argv += res;

    if (argc > 1)
        args_parse(argc, argv);

    cuInit(0);
    cuDeviceGet(&cu_dev, 0);
    cuDevicePrimaryCtxRetain(&cu_ctx, cu_dev);
    cuCtxSetCurrent(cu_ctx);

    vfgx_gpu_init();

    res = vfgx_init_ethport(port_id);
    if (res != 0)
        rte_exit(EXIT_FAILURE, "Failed to initialize ethernet device!\n");


    rte_eth_dev_stop(port_id);
    rte_eth_dev_close(port_id);
    rte_eal_cleanup();

    rte_eal_cleanup();

    printf("Exiting VectorFlow-GX\n");
    return 0;
}