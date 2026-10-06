// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// Glossary of the RDMA terms and abbreviations used in this file and in rdma_loopback.cpp.
//
//   RDMA    Remote Direct Memory Access: the NIC moves data to/from registered memory without the
//           remote CPU taking part in the transfer.
//   HCA     Host Channel Adapter, i.e. the RDMA-capable NIC.
//   RoCEv2  RDMA over Converged Ethernet v2: RDMA carried inside UDP/IPv4 frames. The IPv4 address
//           of the port shows up as an IPv4-mapped GID.
//   GID     Global IDentifier: the 128-bit address of a port, the RDMA equivalent of an IP address.
//           A port has several, one per configured address/protocol; we must pick the RoCEv2 one.
//   PD      Protection Domain: the container that ties together queue pairs and memory regions;
//           an operation may only touch memory registered in the same PD as its queue pair.
//   MR      Memory Region: a range of memory pinned and mapped for NIC DMA. Registration yields an
//           lkey (used by the local side) and an rkey (handed to the remote side).
//   QP      Queue Pair: a send queue plus a receive queue, the endpoint of an RDMA connection.
//   QPN     Queue Pair Number: the identifier a QP is addressed by, like a port number.
//   RC      Reliable Connection: the QP type that supports RDMA READ, with in-order, acknowledged,
//           retried delivery between exactly two QPs.
//   PSN     Packet Sequence Number: the starting sequence number each side expects from the other.
//   CQ      Completion Queue: where the HCA posts a CQE for each finished work request.
//   CQE     Completion Queue Entry: one completion record, carrying success or an error status.
//   WR      Work Request: one posted operation (here: one chunked RDMA READ).
//   SGE     Scatter/Gather Element: one {address, length, lkey} tuple describing local memory of a WR.
//   MTU     Maximum Transmission Unit: largest payload the port carries per packet; both QPs of a
//           connection must agree on it.
//   AH      Address Handle: the destination description (GID, port, hop limit) attached to a QP.
//   GRH     Global Routing Header: the routable (IP-like) header, configured through the AH.
//   IOVA    I/O Virtual Address: the address space the NIC uses for an MR. We register the dma-buf
//           at IOVA 0, so a remote address is simply a byte offset into the exported window.
//   RTR/RTS Ready To Receive / Ready To Send: the QP states a connection is walked through
//           (RESET -> INIT -> RTR -> RTS) before it can carry traffic.
//   rd_atomic
//           How many RDMA READ (and atomic) operations a QP may have in flight - separately capped
//           for the requester side and the responder side.

// libibverbs types, forward-declared so this header does not drag <infiniband/verbs.h> into every
// translation unit that uses the harness. Declared at global scope on purpose: an elaborated
// `struct ibv_*` first seen inside the namespace below would name a different type than libibverbs'.
struct ibv_context;
struct ibv_pd;
struct ibv_cq;
struct ibv_qp;
struct ibv_mr;

namespace tt::umd::test {

// Same-host RDMA loopback harness over an exported dma-buf.
//
// Sets up two RC queue pairs on a single ACTIVE port of the first usable RDMA NIC and cross-connects
// them using that port's own RoCEv2 GID. Because source and destination GID are identical, the NIC
// recognises the target as local and turns the traffic around internally instead of putting it on the
// wire - so an RDMA READ posted on the requester QP is served by the responder QP on the same host,
// with no peer and no fabric needed.
//
// The point of the exercise is that the read is served out of a dma-buf exported by
// Cluster::export_dmabuf(): a successful, correct read is proof that the exported fd is genuine
// device memory a third-party device can DMA from, not merely a valid file descriptor.
//
// Hardware state driven: an ibverbs context, protection domain, completion queue and two RC queue
// pairs on the NIC, plus the pinned buffers, all released in the destructor. Chip state is never
// changed - the device side is only ever read.
//
// All methods throw std::runtime_error on failure. Not thread-safe.
class RdmaLoopback {
public:
    // max_outstanding_reads: how many RDMA READs read() may keep in flight. Also sizes the queues,
    // and is clamped to what the HCA supports. On return both queue pairs are in RTS.
    explicit RdmaLoopback(int max_outstanding_reads);

    // Releases every ibverbs resource, registered memory regions included. The dma-buf fd stays the
    // caller's to close.
    ~RdmaLoopback();

    RdmaLoopback(const RdmaLoopback&) = delete;
    RdmaLoopback& operator=(const RdmaLoopback&) = delete;

    // Pins host memory as the sink RDMA READs write into. Must outlive this object.
    void register_local_sink(void* addr, size_t size);

    // Maps a dma-buf exported by Cluster::export_dmabuf() as the source reads are served from, at
    // IOVA 0 - so a remote address is a plain byte offset into it. The fd stays caller-owned.
    void register_dmabuf_source(int dmabuf_fd, size_t size);

    // Copies [0, size) of the dma-buf source into the start of the local sink, chunk_size bytes per
    // work request, up to max_outstanding_reads in flight. On throw the transfer is incomplete and
    // the sink holds undefined bytes.
    void read(size_t size, size_t chunk_size);

private:
    // Opens the first RDMA device that has an ACTIVE port and records that device's name and port.
    void open_first_active_device();

    // Finds the index of the port's RoCEv2 (IPv4-mapped) GID and reads its raw bytes.
    void select_rocev2_gid();

    // Reads the HCA's RDMA READ in-flight limits and clamps rd_atomic_init_/rd_atomic_dest_.
    void query_rd_atomic_limits(int max_outstanding_reads);

    // Creates one RC queue pair in the INIT state, with a send queue of sq_depth entries.
    ibv_qp* create_qp(int sq_depth);

    // Walks `qp` INIT -> RTR (pointed at dest_qpn, via our own GID: this is the loopback) -> RTS.
    void connect_qp(ibv_qp* qp, uint32_t dest_qpn, uint32_t dest_psn, uint32_t local_psn);

    ibv_context* ctx_ = nullptr;
    ibv_pd* pd_ = nullptr;
    ibv_cq* cq_ = nullptr;
    ibv_qp* qp_requester_ = nullptr;  // issues the RDMA READs
    ibv_qp* qp_responder_ = nullptr;  // serves them out of the dma-buf memory region
    ibv_mr* local_mr_ = nullptr;      // host sink
    ibv_mr* dmabuf_mr_ = nullptr;     // device-memory source
    void* local_addr_ = nullptr;
    size_t local_size_ = 0;
    size_t dmabuf_size_ = 0;

    std::string device_name_;
    uint8_t port_ = 1;
    int gid_index_ = -1;
    uint8_t gid_raw_[16] = {};  // raw bytes of the selected GID; also used as the destination GID
    int max_outstanding_reads_ = 1;
    int rd_atomic_init_ = 1;  // reads this queue pair may have outstanding as requester
    int rd_atomic_dest_ = 1;  // reads it may have inbound as responder
    int path_mtu_ = 0;        // enum ibv_mtu, negotiated down to the port's active MTU
};

}  // namespace tt::umd::test
