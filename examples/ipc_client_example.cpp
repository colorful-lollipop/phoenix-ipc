/**
 * IPC Client Example - Demonstrates PhoenixIPC Client usage
 */

#include <iostream>
#include <thread>
#include <chrono>

#include "phoenix/ipc/ipc_client.hpp"

int main() {
  std::cout << "PhoenixIPC Client Example" << std::endl;
  std::cout << "=========================" << std::endl;

  // Create client
  auto client = phoenix::ipc::CreateIpcClient("/test_ipc");

  // Display initial stats
  auto stats = client->GetStats();
  std::cout << "\nInitial client stats:" << std::endl;
  std::cout << "  Requests sent: " << stats.requests_sent << std::endl;
  std::cout << "  Responses received: " << stats.responses_received << std::endl;
  std::cout << "  Timeouts: " << stats.timeouts << std::endl;
  std::cout << "  Errors: " << stats.errors << std::endl;

  std::cout << "\nNote: To connect, the server must be running." << std::endl;
  std::cout << "This example demonstrates the API surface only." << std::endl;

  // Demonstrate API surface
  std::cout << "\nAvailable API:" << std::endl;
  std::cout << "  IpcClient::Connect(timeout_ms) - Connect to server" << std::endl;
  std::cout << "  IpcClient::Disconnect() - Disconnect from server" << std::endl;
  std::cout << "  IpcClient::IsConnected() - Check connection status" << std::endl;
  std::cout << "  IpcClient::Call(func_id, payload, size, priority, timeout) - Synchronous call" << std::endl;
  std::cout << "  IpcClient::CallAsync(...) - Asynchronous call returning std::future" << std::endl;
  std::cout << "  IpcClient::GetStats() - Get client statistics" << std::endl;

  return 0;
}
