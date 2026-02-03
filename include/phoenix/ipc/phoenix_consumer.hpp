/**
 * PhoenixIPC Consumer Library
 *
 * High-performance SPSC message consumer for shared memory IPC.
 *
 * Usage:
 *   auto consumer = Consumer::Attach("/tmp/phoenix_coord");
 *   auto header = consumer->Receive(payload_buffer);
 */

#ifndef PHOENIX_IPC_CONSUMER_HPP_
#define PHOENIX_IPC_CONSUMER_HPP_

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "phoenix/coordination/coordination_file.hpp"
#include "phoenix/protocol/msg_header.hpp"

namespace phoenix {
namespace ipc {

/**
 * Result of a receive operation
 */
enum class ReceiveResult {
  kSuccess = 0,
  kQueueEmpty = 1,
  kShmClosed = 2,
  kError = 3
};

/**
 * Consumer for SPSC shared memory IPC
 *
 * The consumer reads the coordination file and attaches to the shared memory
 * created by a producer. Messages are received in FIFO order.
 */
class Consumer {
 public:
  /**
   * Attach to an existing producer's shared memory
   * @param coord_file_path Path to coordination file created by producer
   * @param timeout_ms Timeout for reading coordination file (default: 5000ms)
   * @return Unique pointer to consumer, or nullptr on failure
   */
  static std::unique_ptr<Consumer> Attach(
      const std::string& coord_file_path,
      uint32_t timeout_ms = 5000);

  /**
   * Destructor - cleans up resources
   */
  ~Consumer();

  // Non-copyable, non-movable
  Consumer(const Consumer&) = delete;
  Consumer& operator=(const Consumer&) = delete;
  Consumer(Consumer&&) = delete;
  Consumer& operator=(Consumer&&) = delete;

  /**
   * Receive a message (non-blocking)
   * @param payload_buffer Buffer to receive payload data (must be at least GetPayloadSize())
   * @return Optional containing message header if received, nullopt if queue empty
   */
  std::optional<protocol::MsgHeader> Receive(uint8_t* payload_buffer);

  /**
   * Receive a message with blocking
   * @param payload_buffer Buffer to receive payload data
   * @param timeout_ms Maximum time to wait (0 = no timeout)
   * @return Optional containing message header if received, nullopt if timeout
   */
  std::optional<protocol::MsgHeader> ReceiveBlocking(uint8_t* payload_buffer,
                                                      uint32_t timeout_ms = 5000);

  /**
   * Get number of messages available
   * @return Number of messages in queue
   */
  std::size_t AvailableMessages() const;

  /**
   * Get queue capacity
   * @return Total queue capacity
   */
  std::size_t QueueCapacity() const;

  /**
   * Check if consumer is valid and attached
   * @return true if valid
   */
  bool IsValid() const;

  /**
   * Get coordination file path
   * @return Path to coordination file
   */
  const std::string& GetCoordFilePath() const;

  /**
   * Get shared memory name
   * @return POSIX shared memory name
   */
  const std::string& GetShmName() const;

  /**
   * Get payload size
   * @return Maximum payload size per message
   */
  std::size_t GetPayloadSize() const;

  /**
   * Get last error message
   * @return Error message or empty if no error
   */
   const std::string& GetLastError() const;

  /**
   * Clean up shared memory and coordination file
   * Should be called when done receiving messages
   */
   void Cleanup();

  private:
   Consumer();
   bool Initialize(const std::string& coord_file_path, uint32_t timeout_ms);
   void SetError(const std::string& error);

  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace ipc
}  // namespace phoenix

#endif  // PHOENIX_IPC_CONSUMER_HPP_
