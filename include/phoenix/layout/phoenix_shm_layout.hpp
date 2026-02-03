#ifndef PHOENIX_LAYOUT_PHOENIX_SHM_LAYOUT_HPP_
#define PHOENIX_LAYOUT_PHOENIX_SHM_LAYOUT_HPP_

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "../core/spsc_ring_buffer.hpp"
#include "../protocol/msg_header.hpp"
#include "thread_monitor.hpp"
#include "worker_channel.hpp"

namespace phoenix {
namespace layout {

/**
 * PhoenixShmLayout - Main shared memory layout for PhoenixIPC
 *
 * This structure defines the complete memory layout of the shared memory
 * segment used for IPC communication between parent and worker processes.
 *
 * Layout:
 * - Header: magic number, version
 * - Normal group: 4 workers with 1024 capacity queues
 * - VIP group: 2 workers with 256 capacity queues
 * - Buffer regions for each queue (allocated after the structure)
 *
 * Cache line alignment:
 * - ThreadMonitor is already 64-byte aligned
 * - WorkerChannel padding ensures no false sharing
 */
struct PhoenixShmLayout {
  // === Header Section ===
  uint32_t magic_number;      // Must be 0xPHOENIX
  uint32_t version;           // Protocol version
  uint64_t total_size;        // Total shared memory size
  uint64_t buffer_offset;     // Offset to buffer region

  // === Normal Priority Group (Group A) ===
  // 4 workers, each with 1024 capacity queues
  static constexpr std::size_t kNormalWorkerCount = 4;
  static constexpr std::size_t kNormalQueueCapacity = 1024;

  alignas(64) WorkerChannel<kNormalQueueCapacity> normal_channels[kNormalWorkerCount];
  alignas(64) ThreadMonitor normal_monitors[kNormalWorkerCount];

  // === VIP Priority Group (Group B) ===
  // 2 workers, each with 256 capacity queues (higher priority)
  static constexpr std::size_t kVipWorkerCount = 2;
  static constexpr std::size_t kVipQueueCapacity = 256;

  alignas(64) WorkerChannel<kVipQueueCapacity> vip_channels[kVipWorkerCount];
  alignas(64) ThreadMonitor vip_monitors[kVipWorkerCount];

  // === Constants ===
  static constexpr uint32_t kMagicNumber = 0x50484E58;  // "PHNX" in hex
  static constexpr uint32_t kVersion = 1;
  static constexpr std::size_t kCacheLineSize = 64;

  /**
   * Calculate the buffer size needed for all queues
   */
  static std::size_t CalculateBufferSize() {
    // Normal group: 4 workers * 2 queues/worker * capacity * sizeof(MsgHeader)
    std::size_t normal_buffer = kNormalWorkerCount * 2 * kNormalQueueCapacity * sizeof(protocol::MsgHeader);
    // VIP group: 2 workers * 2 queues/worker * capacity * sizeof(MsgHeader)
    std::size_t vip_buffer = kVipWorkerCount * 2 * kVipQueueCapacity * sizeof(protocol::MsgHeader);
    return normal_buffer + vip_buffer;
  }

  /**
   * Initialize the shared memory layout
   * @param shm_size Total size of the shared memory segment
   * @return true if initialized successfully
   */
  bool Initialize(std::size_t shm_size) {
    // Validate header
    std::size_t min_size = sizeof(PhoenixShmLayout) + CalculateBufferSize();
    if (shm_size < min_size) {
      return false;
    }

    magic_number = kMagicNumber;
    version = kVersion;
    total_size = shm_size;
    buffer_offset = sizeof(PhoenixShmLayout);

    // Get buffer pointers
    uint8_t* buffer_base = reinterpret_cast<uint8_t*>(this) + buffer_offset;
    uint8_t* normal_buffer = buffer_base;
    uint8_t* vip_buffer = normal_buffer + (kNormalWorkerCount * 2 * kNormalQueueCapacity * sizeof(protocol::MsgHeader));

    // Initialize normal worker queues
    std::size_t req_buffer_size = kNormalQueueCapacity * sizeof(protocol::MsgHeader);
    std::size_t resp_buffer_size = req_buffer_size;

    for (std::size_t i = 0; i < kNormalWorkerCount; ++i) {
      normal_monitors[i].Reset();
      uint8_t* req_start = normal_buffer + (i * 2 * req_buffer_size);
      uint8_t* resp_start = req_start + req_buffer_size;
      normal_channels[i].request_queue.Initialize(req_start, req_buffer_size);
      normal_channels[i].response_queue.Initialize(resp_start, resp_buffer_size);
    }

    // Initialize VIP worker queues
    for (std::size_t i = 0; i < kVipWorkerCount; ++i) {
      vip_monitors[i].Reset();
      uint8_t* req_start = vip_buffer + (i * 2 * req_buffer_size);
      uint8_t* resp_start = req_start + req_buffer_size;
      vip_channels[i].request_queue.Initialize(req_start, req_buffer_size);
      vip_channels[i].response_queue.Initialize(resp_start, resp_buffer_size);
    }

    return true;
  }

  /**
   * Validate the shared memory layout
   * @return true if valid
   */
  bool IsValid() const {
    return magic_number == kMagicNumber && version == kVersion;
  }

  /**
   * Get the normal worker channel by index
   */
  inline WorkerChannel<kNormalQueueCapacity>& GetNormalChannel(std::size_t index) {
    return normal_channels[index];
  }

  /**
   * Get the normal worker monitor by index
   */
  inline ThreadMonitor& GetNormalMonitor(std::size_t index) {
    return normal_monitors[index];
  }

  /**
   * Get the VIP worker channel by index
   */
  inline WorkerChannel<kVipQueueCapacity>& GetVipChannel(std::size_t index) {
    return vip_channels[index];
  }

  /**
   * Get the VIP worker monitor by index
   */
  inline ThreadMonitor& GetVipMonitor(std::size_t index) {
    return vip_monitors[index];
  }

  /**
   * Find the normal worker with minimal queue backlog
   * @return Index of the least loaded worker
   */
  std::size_t FindBestNormalWorker() const {
    std::size_t best_index = 0;
    std::size_t min_size = normal_channels[0].AvailableRequests();

    for (std::size_t i = 1; i < kNormalWorkerCount; ++i) {
      std::size_t size = normal_channels[i].AvailableRequests();
      if (size < min_size) {
        min_size = size;
        best_index = i;
      }
    }
    return best_index;
  }

  /**
   * Find the VIP worker with minimal queue backlog
   * @return Index of the least loaded worker
   */
  std::size_t FindBestVipWorker() const {
    std::size_t best_index = 0;
    std::size_t min_size = vip_channels[0].AvailableRequests();

    for (std::size_t i = 1; i < kVipWorkerCount; ++i) {
      std::size_t size = vip_channels[i].AvailableRequests();
      if (size < min_size) {
        min_size = size;
        best_index = i;
      }
    }
    return best_index;
  }

  /**
   * Scan all monitors to find crashed workers
   * @return Number of crashed workers found
   */
  std::size_t ScanCrashedWorkers() const {
    std::size_t count = 0;

    // Check normal workers
    for (std::size_t i = 0; i < kNormalWorkerCount; ++i) {
      if (normal_monitors[i].IsProcessing()) {
        ++count;
      }
    }

    // Check VIP workers
    for (std::size_t i = 0; i < kVipWorkerCount; ++i) {
      if (vip_monitors[i].IsProcessing()) {
        ++count;
      }
    }

    return count;
  }

  /**
   * Recover from a crash by skipping poison pill messages
   * @return Number of messages skipped
   */
  std::size_t RecoverCrashedWorkers() {
    std::size_t skipped = 0;

    // Recover normal workers
    for (std::size_t i = 0; i < kNormalWorkerCount; ++i) {
      if (normal_monitors[i].IsProcessing()) {
        // Skip the poison pill
        normal_channels[i].request_queue.ForceAdvanceReadIndex(1);
        normal_monitors[i].EndTransaction();
        ++skipped;
      }
    }

    // Recover VIP workers
    for (std::size_t i = 0; i < kVipWorkerCount; ++i) {
      if (vip_monitors[i].IsProcessing()) {
        // Skip the poison pill
        vip_channels[i].request_queue.ForceAdvanceReadIndex(1);
        vip_monitors[i].EndTransaction();
        ++skipped;
      }
    }

    return skipped;
  }

  /**
   * Get total required size for this layout
   */
  static std::size_t RequiredSize() {
    return sizeof(PhoenixShmLayout) + CalculateBufferSize();
  }
};

/**
 * Shared memory layout factory
 */
class ShmLayoutFactory {
 public:
  /**
   * Create a new layout at the given address
   * @param address Memory address to place the layout
   * @param size Available size
   * @return Pointer to the created layout, nullptr on failure
   */
  static PhoenixShmLayout* Create(void* address, std::size_t size) {
    if (size < sizeof(PhoenixShmLayout)) {
      return nullptr;
    }

    PhoenixShmLayout* layout = new (address) PhoenixShmLayout();
    if (!layout->Initialize(size)) {
      return nullptr;
    }
    return layout;
  }

  /**
   * Get the layout from an existing address
   */
  static PhoenixShmLayout* Get(void* address) {
    PhoenixShmLayout* layout = static_cast<PhoenixShmLayout*>(address);
    if (!layout->IsValid()) {
      return nullptr;
    }
    return layout;
  }
};

}  // namespace layout
}  // namespace phoenix

#endif  // PHOENIX_LAYOUT_PHOENIX_SHM_LAYOUT_HPP_
