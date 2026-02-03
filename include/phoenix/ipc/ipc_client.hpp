#ifndef PHOENIX_IPC_IPC_CLIENT_HPP_
#define PHOENIX_IPC_IPC_CLIENT_HPP_

#include <atomic>
#include <cstdint>
#include <cstring>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>

#include "../layout/phoenix_shm_layout.hpp"
#include "../platform/linux_eventfd.hpp"
#include "ipc_types.hpp"
#include "../protocol/msg_header.hpp"

namespace phoenix {
namespace ipc {

/**
 * IpcClient - Client for connecting to PhoenixIPC server
 *
 * Responsibilities:
 * - Attach to existing shared memory segment
 * - Send requests to server
 * - Receive responses
 * - Handle connection state
 */
class IpcClient {
 public:
  /**
   * Construct IPC client
   * @param shm_name Shared memory name (without leading /)
   */
  explicit IpcClient(const std::string& shm_name);

  /**
   * Destructor - cleanup resources
   */
  ~IpcClient();

  // Non-copyable, non-movable
  IpcClient(const IpcClient&) = delete;
  IpcClient& operator=(const IpcClient&) = delete;
  IpcClient(IpcClient&&) = delete;
  IpcClient& operator=(IpcClient&&) = delete;

  /**
   * Connect to the IPC server
   * @param timeout_ms Timeout for connection
   * @return true if connected successfully
   */
  bool Connect(uint32_t timeout_ms = 5000);

  /**
   * Disconnect from the server
   */
  void Disconnect();

  /**
   * Check if connected
   */
  bool IsConnected() const;

  /**
   * Send a request and wait for response (Normal priority)
   * @param func_id Function ID
   * @param payload Payload data
   * @param payload_size Payload size
   * @param timeout_ms Timeout in milliseconds
   * @return Response header (seq_id=0 on error/timeout)
   */
  protocol::MsgHeader Call(
      uint32_t func_id,
      const uint8_t* payload,
      std::size_t payload_size,
      uint32_t timeout_ms = 1000);

  /**
   * Send a request with priority and wait for response
   * @param func_id Function ID
   * @param payload Payload data
   * @param payload_size Payload size
   * @param priority Message priority
   * @param timeout_ms Timeout in milliseconds
   * @return Response header (seq_id=0 on error/timeout)
   */
  protocol::MsgHeader Call(
      uint32_t func_id,
      const uint8_t* payload,
      std::size_t payload_size,
      protocol::Priority priority,
      uint32_t timeout_ms = 1000);

  /**
   * Send a request asynchronously (Normal priority)
   * @param func_id Function ID
   * @param payload Payload data
   * @param payload_size Payload size
   * @return Future for the response
   */
  std::future<protocol::MsgHeader> CallAsync(
      uint32_t func_id,
      const uint8_t* payload,
      std::size_t payload_size);

  /**
   * Send a request asynchronously with priority
   * @param func_id Function ID
   * @param payload Payload data
   * @param payload_size Payload size
   * @param priority Message priority
   * @return Future for the response
   */
  std::future<protocol::MsgHeader> CallAsync(
      uint32_t func_id,
      const uint8_t* payload,
      std::size_t payload_size,
      protocol::Priority priority);

  /**
   * Get client statistics
   */
  struct ClientStats {
    std::size_t requests_sent;
    std::size_t responses_received;
    std::size_t timeouts;
    std::size_t errors;
  };
  ClientStats GetStats() const;

 private:
  /**
   * Response poller thread
   */
  void ResponsePollerMain();

  /**
   * Send request to server
   */
  bool SendRequest(const protocol::MsgHeader& header,
                   const uint8_t* payload,
                   protocol::Priority priority);

  /**
   * Wait for response with given sequence ID
   */
  protocol::MsgHeader WaitForResponse(uint64_t seq_id, uint32_t timeout_ms);

  // Shared memory
  std::string shm_name_;
  void* shm_addr_ = nullptr;
  std::size_t shm_size_ = 0;
  int shm_fd_ = -1;

  // Layout pointer
  layout::PhoenixShmLayout* layout_ = nullptr;

  // EventFD for response notification
  platform::LinuxEventFdSignal response_signal_;

  // State
  std::atomic<bool> connected_{false};
  std::atomic<bool> running_{false};

  // Sequence ID tracking
  std::atomic<uint64_t> next_seq_id_{1};

  // Pending requests
  struct PendingRequest {
    std::promise<protocol::MsgHeader> promise;
    protocol::MsgHeader response;
    std::mutex mutex;
    bool ready = false;
  };
  std::unordered_map<uint64_t, std::shared_ptr<PendingRequest>> pending_requests_;
  std::mutex pending_mutex_;

  // Response poller thread
  std::thread poller_thread_;

  // Statistics
  std::atomic<std::size_t> requests_sent_{0};
  std::atomic<std::size_t> responses_received_{0};
  std::atomic<std::size_t> timeouts_{0};
  std::atomic<std::size_t> errors_{0};
};

/**
 * Create IPC client
 */
std::unique_ptr<IpcClient> CreateIpcClient(const std::string& shm_name);

}  // namespace ipc
}  // namespace phoenix

#endif  // PHOENIX_IPC_IPC_CLIENT_HPP_
