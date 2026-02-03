/**
 * Simple Producer Example
 *
 * Demonstrates how to create a producer that sends messages to shared memory.
 * Run this first, then run simple_consumer to receive messages.
 */

#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>

#include "phoenix/ipc/phoenix_producer.hpp"

int main() {
  constexpr const char* kCoordFilePath = "/tmp/phoenix_ipc_example.coord";

  std::cout << "=== PhoenixIPC Simple Producer ===" << std::endl;

  // Create producer with default settings
  auto producer = phoenix::ipc::Producer::Create(
      kCoordFilePath,
      1024,                              // queue capacity
      32 * 1024);                        // payload size (32KB)

  if (!producer) {
    std::cerr << "Failed to create producer" << std::endl;
    std::cerr << "Error: " << producer->GetLastError() << std::endl;
    return 1;
  }

  std::cout << "Producer created successfully!" << std::endl;
  std::cout << "  Shared memory: " << producer->GetShmName() << std::endl;
  std::cout << "  Queue capacity: " << producer->QueueCapacity() << std::endl;
  std::cout << "  Payload size: " << producer->GetShmName() << " bytes" << std::endl;

  // Send some test messages
  std::cout << "\nSending messages..." << std::endl;

  for (int i = 1; i <= 10; ++i) {
    // Create message payload
    char payload[256];
    snprintf(payload, sizeof(payload), "Hello from producer - message %d", i);

    // Send message
    auto result = producer->Send(i, reinterpret_cast<const uint8_t*>(payload), strlen(payload));

    if (result == phoenix::ipc::SendResult::kSuccess) {
      std::cout << "  Sent message " << i << std::endl;
    } else {
      std::cerr << "  Failed to send message " << i << std::endl;
    }

    // Small delay between messages
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  std::cout << "\nProducer finished sending messages." << std::endl;
  std::cout << "Available space: " << producer->AvailableSpace() << std::endl;
  std::cout << "Run simple_consumer to receive messages." << std::endl;

  return 0;
}
