# Project: GPU-Accelerated SDN for HPC-Kubernetes

## Overview

This project implements a high-performance Software Defined Network (SDN) data plane for Kubernetes, offloading packet processing from the Linux kernel and CPU directly to NVIDIA GPUs. By leveraging **DPDK (Data Plane Development Kit)**, the **gpudev** library, and **GPUDirect RDMA**, this solution bypasses the traditional kernel network stack to provide deterministic latency and massive throughput suitable for High-Performance Computing (HPC) workloads.

In a standard Kubernetes environment, the Container Network Interface (CNI) and kernel-space switching introduce significant overhead and jitter. This project replaces that model with a persistent CUDA kernel acting as a programmable forwarding plane, allowing Kubernetes to meet the performance requirements of tightly coupled MPI applications and large-scale data processing.

---

## Technical Architecture

The system operates by mapping NIC hardware queues directly to GPU memory addresses, allowing the NIC to DMA packets into a GPU-resident mempool without CPU intervention in the data path.



### Key Components

* **CPU Control Plane:** Orchestrates the environment using the DPDK EAL. It manages the lifecycle of the `rte_gpu_comm_list` and provides the NIC with the physical addresses of the GPU mempool.
* **GPU Data Plane:** A persistent CUDA kernel that polls a communication list in a SIMT (Single Instruction, Multiple Threads) loop. It performs SDN functions such as VXLAN encapsulation, Network Policy enforcement, and NAT.
* **GPUDirect RDMA:** Enables the Network Interface Card (NIC) to read/write headers and payloads directly to/from Video RAM (VRAM).
* **Kubernetes Integration:** Utilizes Multus CNI and the SR-IOV Network Operator to provide pods with direct access to the accelerated network interface.

---

## Performance Objectives

Current Kubernetes networking often forces a trade-off between bandwidth and latency. This implementation aims to optimize both simultaneously:

| Metric | Traditional K8s CNI (Kernel) | GPU-Accelerated SDN |
| :--- | :--- | :--- |
| **Latency** | Variable (CPU Jitter/Interrupts) | Deterministic (Persistent Polling) |
| **Throughput** | Limited by CPU Core Clock/IPC | Scalable via GPU Parallelism |
| **Packet Rate** | Bottlenecked at ~10-20 Mpps per core | Capable of 100Gbps+ Line Rate |
| **Isolation** | Shared Kernel Stack | Hardware-level VF Isolation |

---

## Implementation Details

### CPU-GPU Synchronization
The project utilizes `struct rte_gpu_comm_list` to facilitate low-latency signaling. The CPU populates the list with pointers to received `mbufs` and toggles a status flag. The GPU, monitoring this flag via a volatile pointer in a persistent kernel, immediately begins processing the burst upon detection.

### Compiler Optimization
The CUDA kernels are optimized for the NVIDIA Ampere architecture and later, specifically focusing on:
* **PTX Analysis:** Monitoring for `ld.global.nc` to ensure cache-efficient polling.
* **Memory Barriers:** Implementing `__threadfence_system()` to ensure data consistency between the NIC, CPU, and GPU memory domains.

---

## Prerequisites

### Hardware
* NVIDIA GPU (Pascal architecture or newer; Ampere+ recommended for `gpudev` features).
* NVIDIA ConnectX-5 or newer SmartNIC with GPUDirect RDMA support.
* PCIe Topology: NIC and GPU should ideally reside on the same PCIe Switch or Root Complex.

### Software
* **DPDK 22.11+** with `gpudev` enabled.
* **CUDA Toolkit 11.x/12.x**.
* **NVIDIA Network Operator** installed on the Kubernetes cluster.
* **Multus CNI** and **SR-IOV Network Operator**.

---

## Usage

1. **Allocate GPU Mempool:** The CPU initializes external memory using `rte_extmem_register` on the GPU VRAM.
2. **Launch Persistent Kernel:** The GPU starts the SDN processing loop before the first packet arrives.
3. **Start RX/TX:** The CPU enters the `rte_eth_rx_burst` loop, passing descriptors to the GPU via the communication list.