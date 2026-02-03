/**
 * Hello World Example for PhoenixIPC
 *
 * This example demonstrates basic message passing through shared memory.
 */

#include <iostream>
#include <thread>
#include <chrono>

#include "phoenix/protocol/msg_header.hpp"
#include "phoenix/layout/phoenix_shm_layout.hpp"

int main() {
  std::cout << "PhoenixIPC Hello World Example" << std::endl;
  std::cout << "This example demonstrates the shared memory layout." << std::endl;

  // Calculate and display layout size
  const std::size_t layout_size = phoenix::layout::PhoenixShmLayout::RequiredSize();
  std::cout << "PhoenixShmLayout size: " << layout_size << " bytes" << std::endl;

  // Display configuration
  std::cout << "\nConfiguration:" << std::endl;
  std::cout << "  Normal workers: " << phoenix::layout::PhoenixShmLayout::kNormalWorkerCount << std::endl;
  std::cout << "  Normal queue capacity: " << phoenix::layout::PhoenixShmLayout::kNormalQueueCapacity << std::endl;
  std::cout << "  VIP workers: " << phoenix::layout::PhoenixShmLayout::kVipWorkerCount << std::endl;
  std::cout << "  VIP queue capacity: " << phoenix::layout::PhoenixShmLayout::kVipQueueCapacity << std::endl;
  std::cout << "  Cache line size: " << phoenix::layout::PhoenixShmLayout::kCacheLineSize << " bytes" << std::endl;

  // Display header structure
  std::cout << "\nMsgHeader structure:" << std::endl;
  std::cout << "  Size: " << sizeof(phoenix::protocol::MsgHeader) << " bytes" << std::endl;
  std::cout << "  seq_id: " << offsetof(phoenix::protocol::MsgHeader, seq_id) << std::endl;
  std::cout << "  func_id: " << offsetof(phoenix::protocol::MsgHeader, func_id) << std::endl;
  std::cout << "  payload_len: " << offsetof(phoenix::protocol::MsgHeader, payload_len) << std::endl;

  std::cout << "\nHello World example completed successfully!" << std::endl;

  return 0;
}
