#ifndef PHOENIX_IPC_IPC_SERVER_HPP_
#define PHOENIX_IPC_IPC_SERVER_HPP_

#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "../layout/phoenix_shm_layout.hpp"
#include "../platform/linux_eventfd.hpp"
#include "ipc_types.hpp"
#include "../protocol/msg_header.hpp"

namespace phoenix {
namespace ipc {

/**
 * IpcServer - Parent process IPC server (Router/Supervisor)
 *
 * Responsibilities:
 * - Create and manage shared memory segment
 * - Route messages to appropriate workers (Normal vs VIP)
 * - Load balancing across workers within each group
 * - Monitor child process health
 * - Handle crash recovery
 * - Collect responses from workers
 */
class IpcServer {
 public:
  /**
   * Construct IPC server
   * @param config Server configuration
   */
  explicit IpcServer(const ServerConfig& config);

  /**
   * Destructor - cleanup resources
   */
  ~IpcServer();

  // Non-copyable, non-movable
  IpcServer(const IpcServer&) = delete;
  IpcServer& operator=(const IpcServer&) = delete;
  IpcServer(IpcServer&&) = delete;
  IpcServer& operator=(IpcServer&&) = delete;

  /**
   * Start the IPC server
   * @param worker_child_path Path to worker executable
   * @param argc Number of arguments for worker
   * @param argv Arguments for worker
   * @return true if started successfully
   */
  bool Start(const std::string& worker_child_path,
             int argc = 0,
             char** argv = nullptr);

  /**
   * Stop the IPC server
   */
  void Stop();

  /**
   * Check if server is running
   */
  bool IsRunning() const;

  /**
   * Send a request asynchronously (Normal priority)
   * @param func_id Function ID
   * @param payload Payload data
   * @param payload_size Payload size
   * @return Future for the response
   */
  std::future<protocol::MsgHeader> SendAsync(
      uint32_t func_id,
      const uint8_t* payload,
      std::size_t payload_size);

  /**
   * Send a request asynchronously (with priority)
   * @param func_id Function ID
   * @param payload Payload data
   * @param payload_size Payload size
   * @param priority Message priority
   * @return Future for the response
   */
  std::future<protocol::MsgHeader> SendAsync(
      uint32_t func_id,
      const uint8_t* payload,
      std::size_t payload_size,
      protocol::Priority priority);

  /**
   * Send a synchronous request (Normal priority)
   * @param func_id Function ID
   * @param payload Payload data
   * @param payload_size Payload size
   * @param timeout_ms Timeout in milliseconds
   * @return Response header (seq_id=0 on timeout)
   */
  protocol::MsgHeader Send(
      uint32_t func_id,
      const uint8_t* payload,
      std::size_t payload_size,
      uint32_t timeout_ms = 1000);

  /**
   * Register a request handler for a function ID
   * @param func_id Function ID to handle
   * @param callback Callback function
   */
  void RegisterHandler(uint32_t func_id, RequestCallback callback);

  /**
   * Get server statistics
   */
  struct ServerStats {
    std::size_t requests_sent;
    std::size_t responses_received;
    std::size_t crashes_detected;
    std::size_t poison_pills_skipped;
    std::size_t normal_queue_load;
    std::size_t vip_queue_load;
  };
  ServerStats GetStats() const;

 private:
  /**
   * Worker process entry point
   */
  void WorkerMain();

  /**
   * Supervisor monitoring loop
   */
  void SupervisorMain();

  /**
   * Response collector thread
   */
  void ResponseCollectorMain();

  /**
   * Dispatch a message to a worker
   * @param header Message header
   * @param payload Payload data
   * @param priority Message priority
   * @return true if dispatched successfully
   */
  bool DispatchMessage(const protocol::MsgHeader& header,
                       const uint8_t* payload,
                       protocol::Priority priority);

  /**
   * Select the best worker based on load
   */
  std::size_t SelectBestWorker(protocol::Priority priority);

  /**
   * Handle child process crash
   */
  void HandleCrash();

  /**
   * Recover from crash
   */
  void RecoverFromCrash();

  // Configuration
  ServerConfig config_;

  // Shared memory
  std::string shm_name_;
  void* shm_addr_ = nullptr;
  std::size_t shm_size_ = 0;
  int shm_fd_ = -1;

  // Layout pointer (valid when shm_addr_ is valid)
  layout::PhoenixShmLayout* layout_ = nullptr;

  // EventFD for response notification
  platform::LinuxEventFdSignal response_signal_;

  // Worker process
  pid_t worker_pid_ = -1;
  bool worker_running_ = false;

  // Threads
  std::thread supervisor_thread_;
  std::thread collector_thread_;

  // State
  std::atomic<bool> running_{false};
  std::atomic<ConnectionState> state_{ConnectionState::kDisconnected};

  // Statistics
  std::atomic<std::size_t> requests_sent_{0};
  std::atomic<std::size_t> responses_received_{0};
  std::atomic<std::size_t> crashes_detected_{0};
  std::atomic<std::size_t> poison_pills_skipped_{0};

  // Next sequence ID
  std::atomic<uint64_t> next_seq_id_{1};

  // Handlers
  std::vector<RequestCallback> handlers_;
  static constexpr std::size_t kMaxHandlers = 256;
};

/**
 * Create IPC server with default configuration
 */
std::unique_ptr<IpcServer> CreateIpcServer(const std::string& shm_name = "/phoenix_ipc");

}  // namespace ipc
}  // namespace phoenix

#endif  // PHOENIX_IPC_IPC_SERVER_HPP_
