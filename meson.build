project('vectorflow-gx', 'c', 'cuda',
    version: '0.1',
    default_options: [
        'buildtype=release',
        'c_std=c17',
        'cuda_std=c++17',
        'optimization=3'
    ]
)

dpdk_deps = ['eal', 'gpudev', 'ethdev', 'mempool']
dpdk = dependency('libdpdk', modules: dpdk_deps + ['gpu_cuda'], required: true)

cuda_dep = dependency('cuda', version: '>=13.0', required: true)

ibverbs_dep = dependency('libibverbs', required: true)

gdrcopy_dep = dependency('gdrcopy', required: false)
if not gdrcopy_dep.found()

    gdrcopy_inc = include_directories('/usr/local/cuda/include',
                                      '/usr/local/include')
    gdrcopy_dep = declare_dependency(
        include_directories: gdrcopy_inc,
        link_args: ['-lgdrapi'],
    )
endif

add_project_arguments('-Wdeprecated-declarations', language: 'c')

inc = include_directories('include')

sources = files(
    'src/main.c',
    'src/init_eth.c',
    'src/init_gpu.c',
    'src/vkf.c',
    'src/vkf_gpu.c',
    'src/vkf_ib.c',
    'src/vkf_discover.c',
    'src/vkf_rdma.c',
)

c_args = ['-march=native', '-Wpedantic', '-Wdeprecated-declarations']

cuda_args = [
    '-gencode', 'arch=compute_90,code=sm_90',
    '-Xcompiler', '-fPIC',
    '--ptxas-options=-v'
]

dpdk_libdir = dpdk.get_variable(pkgconfig: 'libdir')
dpdk_pmddir = dpdk_libdir + '/dpdk/pmds-26.1'

executable('vectflow-gx',
    sources,
    dependencies: [dpdk, cuda_dep, ibverbs_dep, gdrcopy_dep],
    include_directories: inc,
    c_args: c_args,
    cuda_args: cuda_args,
    link_args: [
        '-L' + dpdk_pmddir,
        '-lrte_gpu_cuda',
        '-Xlinker=-rpath',
        '-Xlinker=' + dpdk_pmddir,
        '-lcuda',
        '-libverbs',
        '-lgdrapi',
    ],
    install: true
)