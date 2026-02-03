/**
 * PhoenixIPC Producer Library
 *
 * High-performance SPSC message producer for shared memory IPC.
 *
 * Usage:
 *   auto producer = Producer::Create("/tmp/phoenix_coord");
 *   producer->Send(func_id, payload, payload_size);
 */

#ifndef PHOENIX_IPC_PRODUCER_HPP_
#define PHOENIX_IPC_PRODUCER_HPP_

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
 * Result of a send operation
 */
enum class SendResult {
  kSuccess = 0,
  kQueueFull = 1,
  kShmClosed = 2,
  kError = 3
};

/**
 * Producer for SPSC shared memory IPC
 *
 * The producer creates shared memory and writes coordination file.
 * Consumers can then attach to the shared memory using the coordination file.
 */
class Producer {
 public:
  /**
   * Create a new producer
   * @param coord_file_path Path to coordination file
   * @param queue_capacity Number of messages in queue (default: 1024)
   * @param payload_size Size of payload buffer (default: 32KB)
   * @return Unique pointer to producer, or nullptr on failure
   */
  static std::unique_ptr<Producer> Create(
      const std::string& coord_file_path,
      std::size_t queue_capacity = coordination::CoordinationFile::GetDefaultQueueCapacity(),
      std::size_t payload_size = coordination::CoordinationFile::GetDefaultPayloadSize());

  /**
   * Destructor - cleans up shared memory and coordination file
   */
  ~Producer();

  // Non-copyable, non-movable
  Producer(const Producer&) = delete;
  Producer& operator=(const Producer&) = delete;
  Producer(Producer&&) = delete;
  Producer& operator=(Producer&&) = delete;

  /**
   * Send a message (non-blocking)
   * @param func_id Function ID for routing
   * @param payload Payload data (optional, nullptr if no payload)
   * @param payload_len Payload length
   * @return Send result
   */
  SendResult Send(uint32_t func_id,
                  const uint8_t* payload = nullptr,
                  std::size_t payload_len = 0);

  /**
   * Send a message with blocking
   * @param func_id Function ID for routing
   * @param payload Payload data (optional)
   * @param payload_len Payload length
   * @param timeout_ms Maximum time to wait (0 = no timeout)
   * @return Send result
   */
  SendResult SendBlocking(uint32_t func_id,
                          const uint8_t* payload = nullptr,
                          std::size_t payload_len = 0,
                          uint32_t timeout_ms = 5000);

  /**
   * Send a message with vector payload (convenience)
   * @param func_id Function ID
   * @param payload Payload data
   * @return Send result
   */
  SendResult Send(uint32_t func_id, const std::vector<uint8_t>& payload);

  /**
   * Get available space in queue
   * @return Number of messages that can be sent
   */
  std::size_t AvailableSpace() const;

  /**
   * Get queue capacity
   * @return Total queue capacity
   */
  std::size_t QueueCapacity() const;

  /**
   * Check if producer is valid and running
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
   * Get last error message
   * @return Error message or empty if no error
   */
   const std::string& GetLastError() const;

  private:
   Producer();
   bool Initialize(const std::string& coord_file_path,
                   std::size_t queue_cap,
                   std::size_t payload_size);
  void Cleanup();
  void SetError(const std::string& error);

  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace ipc
}  // namespace phoenix

#endif  // PHOENIX_IPC_PRODUCER_HPP_
