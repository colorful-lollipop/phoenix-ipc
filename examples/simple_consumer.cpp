/**
 * Simple Consumer Example
 *
 * Demonstrates how to receive messages from shared memory created by a producer.
 * Run simple_producer first, then this consumer to receive messages.
 */

#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>

#include "phoenix/ipc/phoenix_consumer.hpp"

int main() {
  constexpr const char* kCoordFilePath = "/tmp/phoenix_ipc_example.coord";

  std::cout << "=== PhoenixIPC Simple Consumer ===" << std::endl;

  // Attach to existing producer's shared memory
  auto consumer = phoenix::ipc::Consumer::Attach(kCoordFilePath);

  if (!consumer) {
    std::cerr << "Failed to attach to consumer" << std::endl;
    return 1;
  }

  std::cout << "Consumer attached successfully!" << std::endl;
  std::cout << "  Shared memory: " << consumer->GetShmName() << std::endl;
  std::cout << "  Queue capacity: " << consumer->QueueCapacity() << std::endl;
  std::cout << "  Payload size: " << consumer->GetPayloadSize() << " bytes" << std::endl;

  // Receive messages with timeout
  std::cout << "\nWaiting for messages (timeout 5 seconds)..." << std::endl;

  uint8_t payload_buffer[32 * 1024];  // Must be at least GetPayloadSize()

  auto start_time = std::chrono::steady_clock::now();
  constexpr auto kTimeout = std::chrono::seconds(5);

  int message_count = 0;
  while (std::chrono::steady_clock::now() - start_time < kTimeout) {
    auto header = consumer->ReceiveBlocking(payload_buffer, 1000);  // 1 second timeout

    if (header) {
      message_count++;
      std::cout << "Received message " << message_count << ":" << std::endl;
      std::cout << "  Function ID: " << header->func_id << std::endl;
      std::cout << "  Payload length: " << header->payload_len << " bytes" << std::endl;

      if (header->payload_len > 0) {
        std::cout << "  Payload: " << std::string_view(
            reinterpret_cast<const char*>(payload_buffer), header->payload_len) << std::endl;
      }
    } else {
      // Timeout - check if producer is still alive
      if (consumer->AvailableMessages() == 0) {
        std::cout << "No messages available and producer may have exited." << std::endl;
        break;
      }
    }
  }

  std::cout << "\nConsumer received " << message_count << " messages total." << std::endl;
  std::cout << "Remaining messages in queue: " << consumer->AvailableMessages() << std::endl;

  return 0;
}
