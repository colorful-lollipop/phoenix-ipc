#ifndef PHOENIX_LAYOUT_WORKER_CHANNEL_HPP_
#define PHOENIX_LAYOUT_WORKER_CHANNEL_HPP_

#include <cstddef>

#include "../core/spsc_ring_buffer.hpp"
#include "../protocol/msg_header.hpp"

namespace phoenix {
namespace layout {

/**
 * WorkerChannel - Communication channel for a single worker
 *
 * Each worker (either in Normal or VIP group) has its own dedicated channel
 * containing:
 * - request_queue: Parent process writes, worker reads
 * - response_queue: Worker writes, parent process reads
 *
 * Template parameter QSize must be a power of 2 for efficient ring buffer.
 *
 * @tparam QSize Queue capacity (must be power of 2)
 */
template <std::size_t QSize>
struct WorkerChannel {
  /**
   * Request queue: Parent -> Worker
   * Parent writes requests here, worker reads them
   */
  core::SpscRingBuffer<protocol::MsgHeader, QSize> request_queue;

  /**
   * Response queue: Worker -> Parent
   * Worker writes responses here, parent reads them
   */
  core::SpscRingBuffer<protocol::MsgHeader, QSize> response_queue;

  /**
   * Check if request queue is empty
   */
  inline bool RequestQueueEmpty() const {
    return request_queue.Empty();
  }

  /**
   * Check if request queue is full
   */
  inline bool RequestQueueFull() const {
    return request_queue.Full();
  }

  /**
   * Check if response queue is empty
   */
  inline bool ResponseQueueEmpty() const {
    return response_queue.Empty();
  }

  /**
   * Check if response queue is full
   */
  inline bool ResponseQueueFull() const {
    return response_queue.Full();
  }

  /**
   * Get available requests count
   */
  inline std::size_t AvailableRequests() const {
    return request_queue.Size();
  }

  /**
   * Get available responses count
   */
  inline std::size_t AvailableResponses() const {
    return response_queue.Size();
  }

  /**
   * Get queue capacity
   */
  static constexpr std::size_t QueueCapacity() {
    return QSize;
  }
};

/**
 * ChannelStats - Statistics for a worker channel
 */
struct ChannelStats {
  std::size_t request_queue_size;
  std::size_t response_queue_size;
  std::size_t request_queue_capacity;
  std::size_t response_queue_capacity;
  bool request_queue_full;
  bool response_queue_full;
};

}  // namespace layout
}  // namespace phoenix

#endif  // PHOENIX_LAYOUT_WORKER_CHANNEL_HPP_
