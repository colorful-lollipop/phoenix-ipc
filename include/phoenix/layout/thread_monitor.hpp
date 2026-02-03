#ifndef PHOENIX_LAYOUT_THREAD_MONITOR_HPP_
#define PHOENIX_LAYOUT_THREAD_MONITOR_HPP_

#include <atomic>
#include <ctime>
#include <cstdint>

namespace phoenix {
namespace layout {

/**
 * ThreadMonitor - Crash-safe transaction monitor for workers
 *
 * This structure tracks the state of each worker thread for:
 * - Detecting which request is currently being processed
 * - Identifying crashed workers (left in PROCESSING state)
 * - Enabling crash recovery by locating "poison pill" messages
 *
 * IMPORTANT: Must be aligned to 64-byte cache line boundary
 * to prevent false sharing with adjacent fields.
 *
 * Memory ordering:
 * - status and seq_id are written together with release semantics
 * - status is read with acquire semantics to ensure seq_id visibility
 */
struct alignas(64) ThreadMonitor {
  // Transaction state
  std::atomic<uint8_t> status;        // 0=IDLE, 1=PROCESSING
  std::atomic<uint64_t> processing_seq;  // SEQ of request being processed
  std::atomic<uint64_t> start_timestamp; // When processing started (for timeout detection)

  // Worker identification
  uint8_t group_id;       // 0=Normal, 1=VIP
  uint16_t worker_index;  // Index within the group

  // Constants
  static constexpr uint8_t kStatusIdle = 0;
  static constexpr uint8_t kStatusProcessing = 1;

  // Constructor - ensures proper initialization
  ThreadMonitor() noexcept {
    status.store(kStatusIdle, std::memory_order_relaxed);
    processing_seq.store(0, std::memory_order_relaxed);
    start_timestamp.store(0, std::memory_order_relaxed);
    group_id = 0;
    worker_index = 0;
  }

  /**
   * Mark as processing a new request
   */
  inline void BeginTransaction(uint64_t seq_id, uint8_t group, uint16_t index) {
    status.store(kStatusProcessing, std::memory_order_release);
    processing_seq.store(seq_id, std::memory_order_release);
    start_timestamp.store(GetTimestamp(), std::memory_order_release);
    group_id = group;
    worker_index = index;
  }

  /**
   * Mark transaction as complete
   */
  inline void EndTransaction() {
    status.store(kStatusIdle, std::memory_order_release);
  }

  /**
   * Check if currently processing
   */
  inline bool IsProcessing() const {
    return status.load(std::memory_order_acquire) == kStatusProcessing;
  }

  /**
   * Get current processing sequence (only valid if IsProcessing())
   */
  inline uint64_t GetProcessingSeq() const {
    return processing_seq.load(std::memory_order_acquire);
  }

  /**
   * Get start timestamp (for deadlock detection)
   */
  inline uint64_t GetStartTimestamp() const {
    return start_timestamp.load(std::memory_order_acquire);
  }

  /**
   * Get group ID
   */
  inline uint8_t GetGroupId() const {
    return group_id;
  }

  /**
   * Get worker index
   */
  inline uint16_t GetWorkerIndex() const {
    return worker_index;
  }

  /**
   * Check if transaction has timed out
   */
  inline bool HasTimedOut(uint64_t timeout_us) const {
    if (!IsProcessing()) {
      return false;
    }
    uint64_t now = GetTimestamp();
    uint64_t start = GetStartTimestamp();
    return (now - start) > timeout_us;
  }

  /**
   * Reset monitor to initial state
   */
  inline void Reset() {
    status.store(kStatusIdle, std::memory_order_relaxed);
    processing_seq.store(0, std::memory_order_relaxed);
    start_timestamp.store(0, std::memory_order_relaxed);
    group_id = 0;
    worker_index = 0;
  }

 private:
  static uint64_t GetTimestamp() {
    // Simplified timestamp - use clock_gettime in production
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000ULL + ts.tv_nsec / 1000ULL;
  }
};

/**
 * WorkerGroupConfig - Configuration for a worker group
 */
struct WorkerGroupConfig {
  uint8_t group_id;
  uint16_t worker_count;
  uint32_t queue_capacity;
  uint32_t stack_size;
  uint32_t priority;
};

}  // namespace layout
}  // namespace phoenix

#endif  // PHOENIX_LAYOUT_THREAD_MONITOR_HPP_
