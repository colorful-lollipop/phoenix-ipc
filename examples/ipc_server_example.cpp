/**
 * IPC Server Example - Demonstrates PhoenixIPC Server usage
 */

#include <iostream>
#include <thread>
#include <chrono>

#include "phoenix/ipc/ipc_server.hpp"

void HandleEcho(const phoenix::protocol::MsgHeader& request,
                const uint8_t* payload,
                phoenix::protocol::MsgHeader& response,
                uint8_t* response_payload) {
  std::cout << "Worker received request: func_id=" << request.func_id
            << ", seq_id=" << request.seq_id << std::endl;

  // Echo back the payload
  response.seq_id = request.seq_id;
  response.func_id = request.func_id;
  response.payload_len = request.payload_len;

  if (payload && request.payload_len > 0) {
    std::memcpy(response_payload, payload, request.payload_len);
  }
}

void HandleAdd(const phoenix::protocol::MsgHeader& request,
               const uint8_t* payload,
               phoenix::protocol::MsgHeader& response,
               uint8_t* response_payload) {
  std::cout << "Worker received ADD request: seq_id=" << request.seq_id << std::endl;

  // Parse two integers from payload and add them
  if (request.payload_len >= 8) {
    int64_t a = *reinterpret_cast<const int64_t*>(payload);
    int64_t b = *reinterpret_cast<const int64_t*>(payload + 8);

    int64_t result = a + b;

    response.seq_id = request.seq_id;
    response.func_id = request.func_id;
    response.payload_len = sizeof(int64_t);
    *reinterpret_cast<int64_t*>(response_payload) = result;

    std::cout << "  " << a << " + " << b << " = " << result << std::endl;
  }
}

int main() {
  std::cout << "PhoenixIPC Server Example" << std::endl;
  std::cout << "=========================" << std::endl;

  // Create server configuration
  phoenix::ipc::ServerConfig config;
  config.shm_name = "/test_ipc";
  config.shm_size = 0;  // Will be calculated
  config.worker_timeout_us = 5000000;
  config.max_pending_requests = 10000;
  config.enable_cpu_affinity = false;

  // Note: We can't actually start the server without a worker executable
  // This example demonstrates the API surface
  std::cout << "\nServer configuration:" << std::endl;
  std::cout << "  Shared memory: " << config.shm_name << std::endl;
  std::cout << "  Worker timeout: " << config.worker_timeout_us << " us" << std::endl;
  std::cout << "  Max pending: " << config.max_pending_requests << std::endl;
  std::cout << "  CPU affinity: " << (config.enable_cpu_affinity ? "enabled" : "disabled") << std::endl;

  std::cout << "\nNote: To fully test, compile with a worker executable." << std::endl;
  std::cout << "This example demonstrates the API surface only." << std::endl;

  return 0;
}
