
# How EBA, Combined with Infiniband and RDMA can Solve a Major Problem with TCP

## EBA and Inspiration

I was introduced to Exposed Buffer Architecture in grad school while doing systems research. Work involved low-level kernel driver and shared library development, as well as network simulation in OMNeT++.

VectorFlow-GX is not an EBA implementation, but it is directly inspired by its core principles. Credit for EBA belongs to the original authors; several publicly available papers cover it in depth and are listed below.

* [EBA](https://arxiv.org/abs/2209.03488)
* [EBA Convergence](https://arxiv.org/abs/2008.00989)

## Quick Note

A unique challenge when working with TCP is that it doesn't respect boundaries. TCP operates in streams. When sending a message like "Hello World", the receiver has no inherent way to know where one message ends and the next begins. Without a protocol to enforce boundaries, the receiver cannot determine whether the arriving bytes represent `"H" "ello" "W" "o" "rld"`, `"Hello" "Wo" "rl" "d"`, or `"Hello World"`. This makes a boundary-enforcement protocol necessary, guaranteeing that nodes implementing the same protocol agree on where messages begin and end. Enabling devices to communicate this way can be thought of as overlay convergence.

This enforcement is not free. TCP is implemented in software: shared libraries loaded into a process's Virtual Address Space handle unprivileged operations like socket calls and memory copies, while privileged operations cross into the kernel via a context switch, executing on the process's kernel-side stack within Kernel VAS. The kernel TCP stack then performs the full cost of boundary enforcement: segmentation against MTU and MSS, header construction, checksum computation, sequence number tracking, retransmit timer management, reorder buffering for out-of-order segments, ACK generation, and reassembly before the data reaches the application. On the receive side, the application must then parse its own framing on top of the reassembled stream to reconstruct the boundary that TCP erased on the send side. Every one of these steps runs on CPU, consumes memory, and occupies cache. None of it is workload computation. This is what it means for `protocol to be the abstraction`: a significant portion of available compute, memory, and storage is consumed enforcing boundaries that the transport never natively understood, directly reducing what remains for actual workload processing.

Why does this matter? TCP/IP's value proposition is reliable, global connectivity. Strict adherence to the standard guarantees interoperability with any node that also implements the protocol. But many networks are not globally accessible, nor should they be. Firewalls, private routes, and isolated cloud environments deliberately limit reachability. Nodes inside those boundaries still pay the full cost of TCP's software enforcement, even though the global-connectivity justification that motivated that design does not apply to them.

This is especially relevant in private cloud and HPC environments, where nodes are co-located, interconnects are controlled, and the overhead of TCP's boundary enforcement is pure waste. These environments use specialized hardware and software, including RDMA, InfiniBand fabric, RoCE (RDMA over Converged Ethernet), and DPUs, to address exactly this problem. Critically, this hardware only eliminates the overhead when applications use it directly through the RDMA programming model. If an application still opens a TCP socket, the kernel TCP stack runs in full regardless of what the underlying hardware is capable of. The cost is determined by the abstraction in use, not the hardware present. 

RDMA and InfiniBand substantially reduce this overhead by changing the abstraction itself. Rather than a byte stream that the application must frame and the kernel must segment, the primitive is a memory region. The application registers a buffer with the NIC directly, and transfers are expressed as operations against that buffer. The NIC executes the transfer autonomously, bypassing the kernel entirely: no context switch, no kernel TCP stack, no segmentation against MSS, no software checksum, no reorder queue, no copy from kernel socket buffer to user space. However, even with all of that eliminated, the application must still make boundary decisions in software before the NIC can act. Buffer registration, memory region pinning, and advance coordination of what regions exist on both sides are all software costs that survive kernel bypass. 

Still, if an application still uses TCP as its communication primitive, even over RDMA-capable hardware, the full software enforcement cost on the host CPU survives regardless, because the cost is determined by the abstraction in use, not the hardware present. These are the problems Exposed Buffer Architecture addresses: by making remote buffer state directly observable, the sender no longer needs to pre-negotiate boundary structure before knowing whether the remote side is ready to receive it in that form, eliminating the remaining software cost of boundary decisions that RDMA alone does not remove.

