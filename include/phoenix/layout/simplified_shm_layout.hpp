/**
 * Simplified Shared Memory Layout for SPSC IPC
 *
 * This layout is designed for single-producer single-consumer IPC
 * between two independent processes.
 *
 * Layout:
 * - Header: magic number, version, size info
 * - ThreadMonitor: crash detection
 * - SPSC queue metadata (write_idx, read_idx)
 * - Payload Buffer: variable-length message payloads
 */

#ifndef PHOENIX_LAYOUT_SIMPLIFIED_SHM_LAYOUT_HPP_
#define PHOENIX_LAYOUT_SIMPLIFIED_SHM_LAYOUT_HPP_

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <signal.h>
#include <sys/types.h>

#include "../core/spsc_ring_buffer.hpp"
#include "../protocol/msg_header.hpp"
#include "thread_monitor.hpp"

namespace phoenix {
namespace layout {

/**
 * Simplified shared memory layout for SPSC IPC
 */
struct SimplifiedShmLayout {
  // === Header Section ===
  uint32_t magic_number;       // Must be 0x50484E58 ("PHNX")
  uint32_t version;            // Protocol version
  uint64_t shm_size;           // Total shared memory size
  uint64_t header_size;        // Offset to data section
  uint64_t queue_offset;       // Offset to SPSC ring buffer
  uint64_t payload_offset;     // Offset to payload buffer
  uint64_t queue_capacity;     // Number of messages in queue
  uint64_t payload_size;       // Total payload buffer size
  uint64_t producer_pid;       // PID of producer process
  uint64_t creation_time;      // Creation timestamp

  // === SPSC Queue State (64-byte aligned) ===
  alignas(64) std::atomic<uint64_t> write_idx{0};
  alignas(64) std::atomic<uint64_t> read_idx{0};

  // === Thread Monitor (aligned) ===
  alignas(64) ThreadMonitor monitor;

  // === Constants ===
  static constexpr uint32_t kMagicNumber = 0x50484E58;  // "PHNX"
  static constexpr uint32_t kVersion = 1;
  static constexpr std::size_t kCacheLineSize = 64;

  // === Methods ===

  /**
   * Initialize the shared memory layout
   * @param total_shm_size Total size of shared memory
   * @param queue_cap Number of messages in queue
   * @param payload_buf_size Size of payload buffer
   * @param prod_pid PID of producer
   * @return true if initialized successfully
   */
  bool Initialize(std::size_t total_shm_size,
                  std::size_t queue_cap,
                  std::size_t payload_buf_size,
                  pid_t prod_pid) {
    // Validate size
    std::size_t min_size = sizeof(SimplifiedShmLayout) +
                           queue_cap * sizeof(protocol::MsgHeader) +
                           payload_buf_size;
    if (total_shm_size < min_size) {
      return false;
    }

    magic_number = kMagicNumber;
    version = kVersion;
    shm_size = total_shm_size;
    header_size = sizeof(SimplifiedShmLayout);
    queue_offset = header_size;
    payload_offset = queue_offset + queue_cap * sizeof(protocol::MsgHeader);
    payload_size = payload_buf_size;
    queue_capacity = queue_cap;
    producer_pid = static_cast<uint64_t>(prod_pid);
    creation_time = static_cast<uint64_t>(time(nullptr));

    // Initialize atomics
    write_idx.store(0, std::memory_order_relaxed);
    read_idx.store(0, std::memory_order_relaxed);

    // Initialize monitor
    monitor.Reset();

    return true;
  }

  /**
   * Validate the shared memory layout
   * @return true if valid
   */
  bool IsValid() const {
    return magic_number == kMagicNumber &&
           version == kVersion &&
           shm_size > 0 &&
           queue_capacity > 0;
  }

  /**
   * Get queue capacity
   * @return Number of messages queue can hold
   */
  std::size_t GetQueueCapacity() const {
    return queue_capacity;
  }

  /**
   * Get payload buffer size
   * @return Size in bytes
   */
  std::size_t GetPayloadSize() const {
    return payload_size;
  }

  /**
   * Get producer PID
   * @return PID of producer, 0 if unknown
   */
  pid_t GetProducerPid() const {
    return static_cast<pid_t>(producer_pid);
  }

  /**
   * Check if producer is still alive
   * @return true if producer process exists
   */
  bool IsProducerAlive() const {
    if (producer_pid == 0) {
      return false;
    }
    return kill(producer_pid, 0) == 0;
  }

  /**
   * Check if queue is empty
   */
  bool IsEmpty() const {
    return write_idx.load(std::memory_order_acquire) ==
           read_idx.load(std::memory_order_acquire);
  }

  /**
   * Check if queue is full
   */
  bool IsFull() const {
    uint64_t write = write_idx.load(std::memory_order_relaxed);
    uint64_t read = read_idx.load(std::memory_order_acquire);
    return (write - read) >= queue_capacity;
  }

  /**
   * Get number of available messages
   */
  std::size_t Available() const {
    uint64_t write = write_idx.load(std::memory_order_acquire);
    uint64_t read = read_idx.load(std::memory_order_acquire);
    return static_cast<std::size_t>(write - read);
  }

  /**
   * Get free space in queue
   */
  std::size_t FreeSpace() const {
    uint64_t write = write_idx.load(std::memory_order_relaxed);
    uint64_t read = read_idx.load(std::memory_order_acquire);
    return queue_capacity - static_cast<std::size_t>(write - read);
  }

  /**
   * Get pointer to message at queue index
   * @param index Queue index
   * @return Pointer to message header
   */
  protocol::MsgHeader* GetMessage(std::size_t index) {
    std::size_t offset = queue_offset + (index % queue_capacity) * sizeof(protocol::MsgHeader);
    return reinterpret_cast<protocol::MsgHeader*>(
        reinterpret_cast<uint8_t*>(this) + offset);
  }

  /**
   * Get pointer to payload buffer at offset
   * @param offset Payload offset
   * @return Pointer to payload data
   */
  void* GetPayload(std::size_t offset) {
    return reinterpret_cast<void*>(
        reinterpret_cast<uint8_t*>(this) + payload_offset + offset);
  }

  /**
   * Push a message to the queue
   * @param header Message header
   * @return true if pushed successfully, false if queue full
   */
  bool Push(const protocol::MsgHeader& header) {
    uint64_t current_write = write_idx.load(std::memory_order_relaxed);
    uint64_t current_read = read_idx.load(std::memory_order_acquire);

    if (current_write - current_read >= queue_capacity) {
      return false;  // Queue full
    }

    protocol::MsgHeader* slot = GetMessage(current_write);
    *slot = header;

    write_idx.store(current_write + 1, std::memory_order_release);
    return true;
  }

  /**
   * Peek at the front message without removing
   * @return Pointer to message, nullptr if empty
   */
  protocol::MsgHeader* Peek() {
    uint64_t current_read = read_idx.load(std::memory_order_relaxed);
    uint64_t current_write = write_idx.load(std::memory_order_acquire);

    if (current_write == current_read) {
      return nullptr;  // Empty
    }

    return GetMessage(current_read);
  }

  /**
   * Pop the front message
   * @return true if popped, false if empty
   */
  bool Pop() {
    uint64_t current_read = read_idx.load(std::memory_order_relaxed);
    uint64_t current_write = write_idx.load(std::memory_order_acquire);

    if (current_write == current_read) {
      return false;  // Empty
    }

    read_idx.store(current_read + 1, std::memory_order_release);
    return true;
  }

  /**
   * Force advance read index (for crash recovery)
   * @param count Number of messages to skip
   */
  void ForceAdvanceReadIndex(std::size_t count) {
    uint64_t current_read = read_idx.load(std::memory_order_relaxed);
    read_idx.store(current_read + count, std::memory_order_release);
  }

  /**
   * Get write index
   */
  uint64_t GetWriteIndex() const {
    return write_idx.load(std::memory_order_acquire);
  }

  /**
   * Get read index
   */
  uint64_t GetReadIndex() const {
    return read_idx.load(std::memory_order_acquire);
  }

  /**
   * Calculate required shared memory size
   * @param queue_capacity Number of messages
   * @param payload_size Size of payload buffer
   * @return Total required size in bytes
   */
  static std::size_t CalculateSize(std::size_t queue_capacity,
                                    std::size_t payload_size) {
    return sizeof(SimplifiedShmLayout) +
           queue_capacity * sizeof(protocol::MsgHeader) +
           payload_size;
  }

  /**
   * Get minimum queue capacity
   */
  static constexpr std::size_t GetMinQueueCapacity() {
    return 8;
  }

  /**
   * Get minimum payload size
   */
  static constexpr std::size_t GetMinPayloadSize() {
    return 1024;  // 1KB minimum
  }
};

/**
 * Layout factory for creating/getting layouts
 */
class SimplifiedShmLayoutFactory {
 public:
  /**
   * Create a new layout at the given address
   * @param address Memory address
   * @param size Available size
   * @param queue_cap Queue capacity
   * @param payload_size Payload buffer size
   * @param prod_pid Producer PID
   * @return Pointer to created layout, nullptr on failure
   */
  static SimplifiedShmLayout* Create(void* address,
                                      std::size_t size,
                                      std::size_t queue_cap,
                                      std::size_t payload_size,
                                      pid_t prod_pid) {
    if (size < sizeof(SimplifiedShmLayout)) {
      return nullptr;
    }

    SimplifiedShmLayout* layout = new (address) SimplifiedShmLayout();
    if (!layout->Initialize(size, queue_cap, payload_size, prod_pid)) {
      return nullptr;
    }
    return layout;
  }

  /**
   * Get layout from existing address
   * @param address Memory address
   * @return Pointer to layout, nullptr if invalid
   */
  static SimplifiedShmLayout* Get(void* address) {
    SimplifiedShmLayout* layout = static_cast<SimplifiedShmLayout*>(address);
    if (!layout->IsValid()) {
      return nullptr;
    }
    return layout;
  }
};

}  // namespace layout
}  // namespace phoenix

#endif  // PHOENIX_LAYOUT_SIMPLIFIED_SHM_LAYOUT_HPP_
