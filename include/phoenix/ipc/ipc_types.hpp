#ifndef PHOENIX_IPC_IPC_TYPES_HPP_
#define PHOENIX_IPC_IPC_TYPES_HPP_

#include <atomic>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>

#include "phoenix/protocol/msg_header.hpp"

namespace phoenix {
namespace ipc {

/**
 * RequestCallback - User-defined request handler
 */
using RequestCallback = std::function<void(const protocol::MsgHeader& header,
                                           const uint8_t* payload,
                                           protocol::MsgHeader& response,
                                           uint8_t* response_payload)>;

/**
 * CompletionToken - Token for async request tracking
 */
struct CompletionToken {
  std::promise<protocol::MsgHeader> promise;
  std::future<protocol::MsgHeader> future;

  explicit CompletionToken() : promise(), future(promise.get_future()) {}
};

/**
 * ConnectionState - IPC connection state
 */
enum class ConnectionState : uint8_t {
  kDisconnected = 0,
  kConnecting = 1,
  kConnected = 2,
  kCrashed = 3,
  kRecovering = 4,
};

/**
 * WorkerInfo - Information about a worker thread
 */
struct WorkerInfo {
  uint8_t group_id;
  uint16_t index;
  bool is_alive;
  std::atomic<uint64_t> processed_count;
  std::atomic<uint64_t> failed_count;

  WorkerInfo() : group_id(0), index(0), is_alive(true),
                 processed_count(0), failed_count(0) {}
};

/**
 * ServerConfig - Configuration for IPC server
 */
struct ServerConfig {
  std::string shm_name;
  std::size_t shm_size;
  uint32_t worker_timeout_us;
  uint32_t max_pending_requests;
  bool enable_cpu_affinity;
  uint32_t vip_cpu_cores[8];
  uint32_t normal_cpu_cores[16];
};

}  // namespace ipc
}  // namespace phoenix

#endif  // PHOENIX_IPC_IPC_TYPES_HPP_
