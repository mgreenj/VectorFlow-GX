
# System Setup Instructions

## Table of Contents

- [Justification for Required Packages](#justification-for-required-packages)
- [Resources](#resources)
- [Systems Requirements](#systems-requirements)
- [Required Packages](#required-packages)
  - [Installing Required Packages](#installing-required-packages)
- [Automated Installation](#automated-installation)
- [Manual (step-by-step) Installation](#manual-step-by-step-installation)
  - [Install Melanox Drivers](#install-melanox-drivers)
  - [Install NVIDIA CUDA Toolkit & Drivers](#install-nvidia-cuda-toolkit--drivers)
    - [Blacklist Nouveau Driver](#blacklist-nouveau-driver)
    - [CUDA Toolkit Installer](#cuda-toolkit-installer)
    - [Probe Driver](#probe-driver)
    - [Post CUDA Installation](#post-cuda-installation)
  - [IOMMU Configuration](#iommu-configuration)
    - [Determine Device ID for Target Devices](#determine-device-id-for-target-devices)
    - [Enable IOMMU PCI-Passthrough on GPU Device (Preferred)](#enable-iommu-pci-passthrough-on-gpu-device-preferred)
    - [Disable Device IOMMU on GPU Device](#disable-device-iommu-on-gpu-device)
    - [Verification](#verification)
  - [Install GDRCopy](#install-gdrcopy)
  - [Install DPDK](#install-dpdk)
    - [Install DPDK Dependencies](#install-dpdk-dependencies)
    - [Fork DPDK and Clone](#fork-dpdk-and-clone)
    - [Commit Changes to Forked DPDK Repo](#commit-changes-to-forked-dpdk-repo)
    - [Build with Meson & Ninja](#build-with-meson--ninja)
    - [Run DPDK GPUdev Test](#run-dpdk-gpudev-test)

## Justification for Required Packages

This document includes system setup instructions for the VectorFlow-GX application, which uses GPU Direct RDMA and DPDK to optimize the network packet data path, by transferring network packets directly from the NIC to GPU memory, using DMA. The CPU conducts control plane operations, storing mbuf metadata in user virtual address space (VAS), where DPDK operates.  The CPU is also responsible for communicating with the GPU to signal the storing of mbufs containing network packets in the GPUs memory.

Traditional DMA involves allocating coherent buffers in Kernel VAS and writing the address to a register used by the DMA controller, allowing direct data transfer from a hardware I/O device to memory, without using the CPU. DMA operations differ in the context of GPU Direct RDMA. The GPUs (physical) BAR address will not only be mapped into the kernels VAS (MMIO) for use by the CPU, it will also be mapped to the Network Interface Card (NIC). The NICs DMA controller will transfer Rx packets from a DPDK Rx queue to a DPDK membpool in GPU memory, but writing to the BAR address.

If you're familiar with DPDK, you may be familiar with the acronym `IOVA`, which stands for I/O (Input / Output) Virtual Address. Crucially, DPDK IOVA operates in PA (physical address) or VA (virtual address) mode. When operating in PA mode, the acronym is a misnomer; however, the distinction is necessary to understand. Once memory is allocated on the GPU using rte_extmem_alloc(), a virtual address pointer is returned; the IOVA (physical address) is what's copied to the NIC, not the virtual address. GPU Direct RDMA relies on a library/API/Driver called GDRCopy to copy the address to the NIC.  DPDK `gpudev` plugin is required, along with the CUDA toolset, and several important NVIDIA drivers. IOMMU must be disabled or configured for pass-through translation to GPUDirect RDMA to work.

> [!NOTE]
> It's vitally important for your system to have a compatible NIC. Not all NICs support the mechanism required for VectorFlow-GX (and GPU Direct RDMA in generl) to work. As shown below, the Mellanox NICs used in my exmaples not only support RDMA, but two of them have DPUs. Furthermore, There is a BlueField-2 SoC Management Interface that enhances that integrates the NIC and PCIe switch into a single unit and offloads/accelerates workloads from the CPU. This is not required to use GPU Direct RDMA; however, it is nice to have, especially for SDN.

```
$  lspci -nn | grep -i mellanox
63:00.0 Ethernet controller [0200]: Mellanox Technologies MT27800 Family [ConnectX-5] [15b3:1017]
63:00.1 Ethernet controller [0200]: Mellanox Technologies MT27800 Family [ConnectX-5] [15b3:1017]
81:00.0 Ethernet controller [0200]: Mellanox Technologies MT42822 BlueField-2 integrated ConnectX-6 Dx network controller [15b3:a2d6]
81:00.1 Ethernet controller [0200]: Mellanox Technologies MT42822 BlueField-2 integrated ConnectX-6 Dx network controller [15b3:a2d6]
81:00.2 DMA controller [0801]: Mellanox Technologies MT42822 BlueField-2 SoC Management Interface [15b3:c2d3]
```

## Resources

* [NVIDIA Documentation Hub](https://docs.nvidia.com/) includes links to relevant software in subsection pages (e.g., [NVIDIA Networking](https://docs.nvidia.com/networking/software/index.html))

* Link to [NVIDIA networking HUB page with relevant documentation](https://network.nvidia.com/products/GPUDirect-RDMA/) and download links bundled together. Some of the versions linked may be outdated; however, I'll cover how to install the latest version of each. Nevertheless, the documentation PDFs linked are useful.

* [Developing a Linux Kernel Module using GPUDirect RDMA](https://docs.nvidia.com/cuda/gpudirect-rdma/) includes nearly everything you need to know to get started.


## Systems Requirements

> [!IMPORTANT]
> For more information on GPUDirect RDMA, including a detailed section on `Supported Systems`, please read NVIDIA's [GPUDirect RDMA documentation](https://docs.nvidia.com/cuda/gpudirect-rdma/).
>

It is imperative that you make sure you're using supported hardware. Regarding supported GPU versions, please refer to my [supported-devices](/docs/supported-devices.md) guide that instructs you on how to enable support for an NVIDIA GPU with CUDA capability, that is `not` officially supported by the DPDK gpudev library. Please note, this is only an option if your GPU has CUDA capability. You `must` have a NIC that supports RDMA and/or has `iWARP` capabilities. In most cases, these will be `Mellanox Connect-X` NICs; however, there are a few smart NICs from Intel and other vendors that will work.

I recommend using the same OS and kernel version used when writing this guide.

* OS: Ubuntu 24.04 LTS (24.04.4 LTS)
* Kernel: 6.8.0-101-generic

I'm including information about the GPU and NIC used as well. You don't have to use the same model; however, again, you must ensure your GPU is supported by the DPDK gpudev plugin, and that your NIC is RDMA-capable.

* GPU: 2 x NVIDIA Corporation GV100GL [Tesla V100S PCIe 32GB]
* NIC: 2 x Mellanox Technologies MT42822 ; 2 x Mellanox Technologies MT27800

## Required Packages

### Installing Required Packages

The purpose of this guide is to guide anyone interestd through the installation process for the bulleted packages (below). There are two methods: manual (i.e., step-by-step) process and the automated process using Ansible. Follow the instructions outlined below for the installation method of your choice.

    * Standard: EAL, mbuf, NUMA, mempool, PMD
    * [Cuda GPU Driver](https://doc.dpdk.org/guides/gpus/cuda.html)
    * [GPUdev Library](https://doc.dpdk.org/guides/prog_guide/gpudev.html)
    * [NVIDIA MLX5 Ethernet Driver](https://doc.dpdk.org/guides/nics/mlx5.html) [[Setup Steps](https://doc.dpdk.org/guides/platform/bluefield.html)]
    * [GDRCopy](https://github.com/NVIDIA/gdrcopy)
    * [CUDA Toolkit](https://docs.nvidia.com/cuda/cuda-quick-start-guide/index.html)
    * [Melanox OFED (Now DOCA-Host/DOCA-OFED)](https://developer.nvidia.com/doca-downloads?deployment_platform=BlueField&deployment_package=BF-FW-Bundle). A complete guide is [here](https://docs.nvidia.com/doca/sdk/DOCA-Installation-Guide-for-Linux/index.html).
    * [NVIDIA Peermem (and other) Driver(s)]()

## Automated Installation

Admittedly, the process for installing the software prerequisites is moderelty complex. The instructions that I've provided for doing this manually will simplify tthe setup process A LOT. Still, you may prefer using automation, especially if you need to setup GPU Direct RDMA on multiple GPU nodes, or expect to re-provision previously configured nodes.

I've created an Ansible playbook to automate the setup process. The one caveat is that I will not make any attempt to maintain this playbook, nor will I enable multi-architecture, multi-distro, or multi-version support. At the time of development, it works for Ubuntu 24.04.4 (LTS) (Kernel version 6.8.0-101-generic) on x86_64 and installs CUDA toolkit 13.1.1 and NVIDIA driver version 580.

Anyone using this guide is welcome to modify the playbook to extend support for other architectures, distributions, kernel versions, or distro versions.

To run the playbook, do the following:

```
# Single Node
$  ansible-playbook -i node0, scripts/build/ansible-gpurdma.yml -u <username> --become

# Override Variables for CUDA toolkit and Nvidia Driver version
$  ansible-playbook -i node0, setup_gpurdma.yml --become \
    -e cuda_version=12.4 \
    -e cuda_driver_pkg=nvidia-driver-550
```
## Manual (step-by-step) Installation

The following section outlines the manual process for installing prerequisite software packages.

### Install Melanox Drivers

> [!Note]
> MLNX_OFED has transitioned into DOCA-Host, and now is available as DOCA-OFED profile (learn about DOCA-Host profiles here).
> MLNX_OFED last standalone release is October 2024 Long Term Support (3 years). Starting January 2025 all new features will only be
> included in DOCA-OFED. Download DOCA-Host [here](https://developer.nvidia.com/doca-downloads). A transition guide (MLNX_OEFD to DOCA)
> can be found [here](https://docs.nvidia.com/doca/sdk/nvidia-mlnx-ofed-to-doca-ofed-transition-guide.pdf).

DOCA-Host contains multiple melanox packages, certainly more than what is needed for this project. `DOCA-OFED` is a 1-to-1 substitute
for MLNX_OEFD, so, in the DOCA download center linked above, the following selection path will provide instructions for downloading 
DOCA_OFED: `Host-Server>DOCA-Host>Linux>x86_64>doca-ofed>Ubuntu>24.04>deb (online)`.

> [!INFO]
> All instructions assume Ubuntu 24.04 LTS and x86_64 architecture.
>

The instructions are copied below for convenience; however, you should follow the instruction above to ensrue you're using the latest
version, which appears to be pinned in the URL.

```
$  export DOCA_URL="https://linux.mellanox.com/public/repo/doca/3.3.0/ubuntu24.04/x86_64/"
$  curl https://linux.mellanox.com/public/repo/doca/GPG-KEY-Mellanox.pub | gpg --dearmor > /etc/apt/trusted.gpg.d/GPG-KEY-Mellanox.pub
$  echo "deb [signed-by=/etc/apt/trusted.gpg.d/GPG-KEY-Mellanox.pub] $DOCA_URL ./" > /etc/apt/sources.list.d/doca.list
$  sudo apt-get update
$  sudo apt-get -y install doca-ofed
$  (optional but recommended)
$  sudo apt-get -y install doca-networking
$  sudo apt-get -y install doca-all

```

Install the Bluefield2 Firmware. The [directions for installing BlueField using standard Linux Tools](https://docs.nvidia.com/doca/sdk/BF-Bundle-Installation-and-Upgrade/index.html) are reliable. Note that you need to use the URL for Ubuntu24, instead of version 22 provided by the URLs in the documentation. Visiting the site and changing the URI paths and file names is simple enough.

> [!NOTE]
> installing Bluefield Firmware (i.e., BF-Bundle or BF-FW-Bundle) is only an optoin for systems
> with a Bluefield DPU. If you do not have a BlueField DPU, skip to [Install NVIDIA CUDA toolkit  & Drivers](#install-nvidia-cuda-toolkit--drivers).
```
# This link allows you to bypass EULA
$  systemctl start rshim
$
$  # Ubuntu 24.04
$  wget https://content.mellanox.com/BlueField/BFBs/Ubuntu24.04/bf-bundle-3.3.0-202_26.01_ubuntu-24.04_64k_prod.bfb
$  bfb-install --bfb bf-bundle-3.3.0-202_26.01_ubuntu-24.04_64k_prod.bfb --rshim rshim0
$
$  # Ubuntu 22.04
$  wget https://content.mellanox.com/BlueField/FW-Bundle/bf-fwbundle-3.3.0-202_26.01-prod.bfb]
$  bfb-install --bfb bf-fwbundle-3.3.0-202_26.01-prod.bfb --rshim rshim0
$  # Either version
$  modprobe mlx5_core

```

Additional DOCA resource and repo downloads can be found [here](https://linux.mellanox.com/public/repo/doca/3.3.0/).


### Install NVIDIA CUDA Toolkit & Drivers

> [!IMPORTANT]
> Please follow the CUDA [Linux installation guide](https://docs.nvidia.com/cuda/cuda-installation-guide-linux/#ubuntu-installation) for installation instructions for other Linux distributes,
> and pre-requisite software requirements.
> 

The NVIDIA toolkit includes libraries and the nvcc compiler, needed to compile. The NVIDIA drivers are GPU drivers required for basic GPU programming and basic functionality. The distribution-specific installer
is recommended, and that is what I've used.

#### Blacklist Nouveau Driver

The Nouveau driver, if present, will try to assign itself to the GPU. You must `blacklist the Nouveau driver` to prevent assignment; otherwise, the nvidia driver cannot claim the GPU.

```
$  echo "blacklist nouveau" | sudo tee /etc/modprobe.d/blacklist-nouveau.conf
$  echo "options nouveau modeset=0" | sudo tee -a /etc/modprobe.d/blacklist-nouveau.conf
$  sudo update-initramfs -u
$  sudo reboot
```

#### CUDA Toolkit Installer


##### (Not Recommended) Option #1
The [official download instructions ](https://developer.nvidia.com/cuda-downloads?target_os=Linux&target_arch=x86_64&Distribution=Ubuntu&target_version=24.04&target_type=deb_local) can be followed to ensure the installation process is consistent with the NVIDIA verified installation process.

> [!NOTE]
> I recommend following the instructions that I've provided directly below this. However, if you prefer to follow the instructions linked above, note that you will likely need to modify the copy command above to match the file on your system, following the dpkg (not apt) install. 
> the alphanumeric string between `cuda` and `keyring` (i.e., C8699457 in the example above) may be differnt.
>

###### CUDA Toolkit Installer

```
$  wget https://developer.download.nvidia.com/compute/cuda/repos/ubuntu2404/x86_64/cuda-ubuntu2404.pin
$  sudo mv cuda-ubuntu2404.pin /etc/apt/preferences.d/cuda-repository-pin-600
$  wget https://developer.download.nvidia.com/compute/cuda/13.1.1/local_installers/cuda-repo-ubuntu2404-13-1-local_13.1.1-590.48.01-1_amd64.deb
$  sudo dpkg -i cuda-repo-ubuntu2404-13-1-local_13.1.1-590.48.01-1_amd64.deb
$  sudo cp /var/cuda-repo-ubuntu2404-13-1-local/cuda-*-keyring.gpg /usr/share/keyrings/
$  sudo apt-get update
$  sudo apt-get -y install cuda-toolkit-13-1
```

###### CUDA Driver Installer

The final step to install CUDA is to install the CUDA drivers. This includes the `nvidia-peermem` driver which, amond others, is crucial.  The peermem driver, especially, does not load automatically, so you will likely have to run `modprobe` to load make sure that it's loaded.

I am installing the `open kernel module flavor`.

```
$  sudo apt-get install -y nvidia-open
```

##### (Recommended) Option #2

I am installing CUDA toolkit version 13.1 with driver version 580. You must check to see the highest supported driver version for your GPU (I used `nvidia-detector`) and check the release notes for that driver to see the highest supported CUDA toolkit version. Once you've done that, I recommend using the debs in the [CUDA repo.](https://developer.download.nvidia.com/compute/cuda/repos/ubuntu2404/x86_64/).  All that's needed is to install the keyring, then install the desired CUDA toolkit and NVIDIA Driver versions.

```
$  wget https://developer.download.nvidia.com/compute/cuda/repos/ubuntu2404/x86_64/cuda-keyring_1.1-1_all.deb
$  sudo dpkg -i cuda-keyring_1.1-1_all.deb
$  
```

Make sure the repo has the latest supported version for the software. Compare the versions shown running the commands below with the latest supported version the [CUDA repo.](https://developer.download.nvidia.com/compute/cuda/repos/ubuntu2404/x86_64/), also liked above.
```
$  apt-cache show cuda-toolkit-13-1 | grep -E "Version|Filename"
$  apt-cache show nvidia-driver-580 | grep -E "Version|Filename"
```

If everything looks good, install the toolkit and driver. Apt will install the latest minor version.

```
$  apt-get install -y cuda-toolkit-13-1
$  apt-get install -y nvidia-driver-580
```
#### Probe Driver

After installing the toolkit and drivers, you must run modprobe. If you fail to probe/load any of the modules, check `dmesg` for the error.

```
$  sudo modprobe nvidia
$  sudo modprobe nvidia-peermem
$  sudo modprobe mlx5_core
```

After probing, you should see the modules when running `lsmod`

```
$  lsmod | grep -i "nvidia\|mlx5"
nvidia_uvm           2093056  0
nvidia_peermem         16384  0
nvidia              104153088  2 nvidia_uvm,nvidia_peermem
mlx5_fwctl             16384  0
fwctl                  12288  1 mlx5_fwctl
mlx5_ib               561152  0
ib_uverbs             212992  3 nvidia_peermem,rdma_ucm,mlx5_ib
macsec                 77824  1 mlx5_ib
ib_core               528384  8 rdma_cm,ib_ipoib,iw_cm,ib_umad,rdma_ucm,ib_uverbs,mlx5_ib,ib_cm
mlx5_core            3153920  2 mlx5_fwctl,mlx5_ib
mlxfw                  36864  1 mlx5_core
psample                16384  1 mlx5_core
mlxdevm               540672  1 mlx5_core
tls                   155648  1 mlx5_core
mlx_compat             12288  13 rdma_cm,ib_ipoib,mlxdevm,mlx5_fwctl,iw_cm,ib_umad,fwctl,ib_core,rdma_ucm,ib_uverbs,mlx5_ib,ib_cm,mlx5_core
pci_hyperv_intf        12288  1 mlx5_core
```

#### Post CUDA Installation

Ensure the path and library environment variables include the installation directories for the CUDA binaries and libraries, respectively. It's best if you add these to your `bashrc` file for persistence.

```
$  export PATH=/usr/local/cuda-13.1/bin${PATH:+:${PATH}}
$  export LD_LIBRARY_PATH=/usr/local/cuda-13.1/lib64${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}
```


### IOMMU Configuration

IOMMU must be disabled or confured for PCI passthrough for GPU Direct RDMA to work. The following instructions show how to do both.

#### Determine Device ID for Target Devices

Run the following to get the Vendor ID of your target device-- NVIDIA GPU in this example-- and detailed device information.
```
$  lspci -nn | grep -i nvidia
$  lspci -D -PP -d 10de: -vvv

```

Vendor and Device ID for my GPUs

```
21:00.0 3D controller [0302]: NVIDIA Corporation GV100GL [Tesla V100S PCIe 32GB] [10de:1df6] (rev a1)
e2:00.0 3D controller [0302]: NVIDIA Corporation GV100GL [Tesla V100S PCIe 32GB] [10de:1df6] (rev a1)
```

The following lines show the IOMMU group of my 2 GPU devices
```
	IOMMU group: 39
	IOMMU group: 75
```

#### Enable IOMMU PCI-Passthrough on GPU Device (Preferred)

To put the IOMMU units in PCI Passthrough mode:

```
$  vi /etc/default/grub
```

Add the following to GRUB_CMDLINE_LINUX_DEFAULT: `amd_iommu=on` `iommu=pt`. (If you're using an Intel CPU, use `intel_iommu=on`).

```
GRUB_CMDLINE_LINUX_DEFAULT="splash=quiet console=tty0 amd_iommu=on iommu=pt"

# save file then run
$  sudo update-grub
$  sudo reboot
```

#### Disable Device IOMMU on GPU Device

```
GRUB_CMDLINE_LINUX_DEFAULT="splash=quiet console=tty0 amd_iommu=off iommu=pt "
# save file then run
$  sudo update-grub
$  sudo reboot

```

#### Verification

After rebooting the system, verify the changes.

```
$  cat /proc/cmdline | grep iommu
BOOT_IMAGE=/boot/vmlinuz-6.8.0-101-generic root=UUID=ccb0832d-c80e-430f-b248-d117889f1992 ro console=ttyS0,115200 emulabcnet=4c:d9:8f:21:fd:ba amd_iommu=on iommu=pt
$  dmesg | grep -i "Passthrough
[    5.261710] iommu: Default domain type: Passthrough (set via kernel command line)
```


### Install GDRCopy

As described in the [software requirement justification](#justification-for-required-packages), `GDRCopy` is required by DPDK; gdrcopy provides an API allowing copy of the physical BAR to the NIC for DMA.

```
$  git clone https://github.com/NVIDIA/gdrcopy.git
$  cd gdrcopy/
$
$  apt -y install devscripts debhelper fakeroot
$  make prefix=/usr/local/ CUDA=/usr/local/cuda-13.1/ all install
$  sudo ./insmod.sh
$  lsmod | grep gdr
```

The default location for GDRCopy libraries is `/usr/local/lib/`. Update your LD_LIBRARY_PATH environment variable to reflect this location.


```
export LD_LIBRARY_PATH=/usr/local/lib:/usr/local/cuda-13.1/lib64${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}
```

Run the sanity check utility, `gdrcopy_sanity`, to verify GDRCopy is working properly.

```
$  gdrcopy_sanity
Total: 36, Passed: 31, Failed: 0, Waived: 5

List of waived tests:
    basic_v2_forcepci_cumemalloc
    basic_v2_forcepci_vmmalloc
    basic_with_tokens
    data_validation_mix_mappings_cumemalloc
    data_validation_v2_forcepci_cumemalloc
root@node0:/home/gpurdma/gdrcopy#
```

### Install DPDK

> [!WARNING]
> After implementing the original PoC using DPDK, I decided to use DOCA library
> instead. I'm leaving these setup instructions here until I have finished 
> the switch to DOCA. Once that happens, everything related to DPDK, including
> documentation, will be removed.

Installing DPDK is the final step. `Please` do not proceed with the DPDK installation if you haven't followed the installation steps outlined in previous sections. I will install the latest version of DPDK, currently `1.26.03`.

#### Install DPDK Dependencies

```
$  sudo apt -y install \
    build-essential \
    ninja-build \
    meson \
    python3-pyelftools \
    libpcap-dev \
    libnuma-dev \
    libssl-dev \
    libibverbs-dev \
    libibverbs1 \
    rdma-core

```

#### Fork DPDK and Clone

```
$  git clone https://github.com/mgreenj/dpdk

```

#### Commit Changes to Forked DPDK Repo

Follow the steps outlined in the [supported devices documentation](/docs/supported-devices.md) that I created; in particular, add support for your GPU by adding the Device ID as [shown in my commits](/docs/supported-devices.md#necessary-changes). You should know how to find the Device ID of your GPU(s) at this point.

#### Build with Meson & Ninja

Finally, build DPDK using `meson` and `ninja`. Make sure CUDA and GPUdev libraries are marked by meson for installation (i.e., requirements for each library are satisfied).

```
$  echo 1024 > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
$
$  meson setup build \
    -Denable_drivers=bus/auxiliary,common/mlx5,gpu/cuda,net/mlx5 \
    -Dplatform=native \
    --prefix=/usr/local \
    -Dc_args="-I/usr/local/cuda-13.1/include" \
    -Dc_link_args="-L/usr/local/cuda-13.1/lib64"


$  ninja -C build -j$(nproc)
$  cd build/
$  meson install
$  ldconfig
```

#### Run DPDK GPUdev Test

The final step for this setup is to verify dpdk installation and that GPUdev is working as expected. DPDK provides an app, `dpdk-test-gpudev`, that will do this for you.

```
$  ./build/app/dpdk-test-gpudev
EAL: Detected CPU lcores: 128
EAL: Detected NUMA nodes: 2
EAL: Detected static linkage of DPDK
EAL: Multi-process socket /var/run/dpdk/rte/mp_socket
EAL: Selected IOVA mode 'VA'


DPDK found 2 GPUs:
	GPU ID 0
		parent ID -1 GPU Bus ID 0000:21:00.0 NUMA node 0 Tot memory 32494.12 MB, Tot processors 80
	GPU ID 1
		parent ID -1 GPU Bus ID 0000:e2:00.0 NUMA node 1 Tot memory 32494.12 MB, Tot processors 80



=======> TEST: Allocate GPU memory

GPU memory allocated at 0x0x717b5ba00000 size is 1024 bytes
GPU memory allocated at 0x0x717b5ba01000 size is 1024 bytes
CUDA: Memory address 0x0x717b5ba00700 not found in driver memory
GPU memory 0x0x717b5ba00700 NOT freed: GPU driver didn't find this memory address internally.
GPU memory 0x0x717b5ba01000 freed
GPU memory 0x0x717b5ba00000 freed

=======> TEST: PASSED

=======> TEST: Register CPU memory

CPU memory registered at 0x0x1051c7280 1024B
CUDA: Memory address 0x0x1051c7980 not found in driver memory
CPU memory 0x0x1051c7980 NOT unregistered: GPU driver didn't find this memory address internally
CPU memory 0x0x1051c7280 unregistered

=======> TEST: PASSED

=======> TEST: Map GPU memory for CPU visibility

GPU memory allocated at 0x0x717b5ba00000 size is 1024 bytes
GPU memory CPU mapped at 0x0x717de804a000
GPU memory first 3 bytes set from CPU: 4 5 6
GPU memory CPU unmapped, 0x0x717de804a000 not valid anymore
GPU memory 0x0x717b5ba00000 freed

=======> TEST: PASSED

=======> TEST: Communication flag

Communication flag value at 0x0x1051cc240 was set to 25 and current value is 25
Communication flag value at 0x0x1051cc240 was set to 38 and current value is 38

=======> TEST: PASSED

=======> TEST: Communication list

GPUDEV: packet list is still in progress
Communication list not cleaned because packets have not been consumed yet.
Consuming packets...
Communication list cleaned because packets have been consumed now.

=======> TEST: PASSED

```