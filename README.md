# Project: GPU-Accelerated Packet Processing for HPC

## Table of Contents

- [Introduction](#introduction)
- [Technical Architecture](#technical-architecture)
  - [Linux Kernel Driver PCI P2P Memory Pressure and Telemetry](#kernel-driver-for-telemetry-and-pci-p2p-memory-pressure-monitoring)
  - [Key Components](#key-components)
  - [Trying with DPDK](#trying-with-dpdk)
  - [Switching to NVIDIA DOCA](#switching-to-nvidia-doca-library-and-sdk)
- [System Setup](#system-setup)
  - [VectorFlow-GX Installation](#vectorflow-gx-installation)

## Introduction

While researching programmatic methods for optimizing network traffic in High Performance Computing, I discovered an interesting blog written by an NVIDIA researcher titled: Boosting Inline Packet Processing Using DPDK and GPUdev with GPUs, which introduced me to the concept of GPUDirect RDMA. VectorFlow-GX is my attempt to implement this solution, using GPUDirect RDMA and Infiniband verbs and a buffer-abstraction communication model inspired by EBA.

Simply put, VectorFlow-GX is my own implementation of a high-performance solution for networking in HPC, inspired by the following research and sources:

* [Exposed Buffer Architecture](https://web.eecs.utk.edu/~mbeck/Exposed_Buffer_Architecture.pdf)
  * [EBA](https://arxiv.org/abs/2209.03488)
  * [EBA Convergence](https://arxiv.org/abs/2008.00989)

* [Boosting Inline Packet Processing](https://developer.nvidia.com/blog/optimizing-inline-packet-processing-using-dpdk-and-gpudev-with-gpus/)

Why combine RDMA and IB with a buffer-abstraction communication model? I've provided a [detailed explaination in the docs](/docs/tcp-problem-eba.md).

---

## Technical Architecture

The system operates by mapping NIC hardware queues directly to GPU memory addresses, allowing the NIC to DMA packets into a GPU-resident mempool without CPU intervention in the data path.

### Kernel Driver for Telemetry and PCI P2P Memory Pressure Monitoring

I also wrote a Linux character device driver named [gpurdma-mon](https://github.com/mgreenj/gpurdma-mon) that should be used with this program. I've added the driver as a repo submodule, so make sure it's initialized.

### Key Components

* **CPU Control Plane:**
* **GPU Data Plane:**
* **GPUDirect RDMA:**
* **DPDK or DOCA SDK/Library:** (see below)

### Trying with DPDK

This project implements a high-performance Software Defined Network (SDN) data plane for Kubernetes, offloading packet processing from the Linux kernel and CPU directly to NVIDIA GPUs. My attempt involved using **DPDK's**, which includes gpudev and cuda libraries, along with GDRCopy. While this solution seems to work, support for GPUDirect RDMA is still a work in progress and some desired functionality isn't available. You can read [my blog post on using DPDK for GPUDirect RDMA](https://blog.mauricegreen.me/blogs/nvidia-gpu-rdma-packet-perf-1/), where I explain in great detail.

Ultimately, I decided to use the DOCA library. It's well documented and provides a lot of capability; the GPUNetIO subsystem is the main component used. 

### Switching to NVIDIA DOCA Library and SDK

I discovered an alternative method for implementing this packet processing data flow, that used the DOCA library and GPUNetIO. The documentation and example programs were enough to persuade me to start over and build my program using DOCA instead of DPDK

---

## System Setup

To simplify, I created an [system setup guide](/docs/SETUP.md). Please follow setup instructions closely to ensure `VectorFlow-GX` will function as expected.

### VectorFlow-GX Installation

After completing the system setup guide, run the [VectorFlow-GX deployment script](/scripts/build/install-vectorflow-gx.sh) locally on nodes to install VectorFlow-GX. Example usage is shown below. 

> [!NOTE]
> Binding refers to NIC binding with a DPDK application
>

```
chmod +x scripts/build/install-vectorflow-gx.sh

# Full setup - clone, build, bind NIC
$  ./scripts/build/install-vectorflow-gx.sh -r https://github.com/you/VectorFlow-GX \
    -n 0000:c2:00.0 \
    -g 0000:21:00.0

# Build only, no clone, no bind
$  ./scripts/build/install-vectorflow-gx.sh --no-clone --no-bind -d /home/gpurdma/VectorFlow-GX

# Override CUDA path if different
$  CUDA_HOME=/usr/local/cuda-13.1 ./scripts/build/install-vectorflow-gx.sh --no-clone -n 0000:c2:00.0

```
