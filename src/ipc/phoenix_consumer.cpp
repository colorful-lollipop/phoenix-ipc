/**
 * PhoenixIPC Consumer Implementation
 */

#include "phoenix/ipc/phoenix_consumer.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

#include "phoenix/coordination/coordination_file.hpp"
#include "phoenix/layout/simplified_shm_layout.hpp"

namespace phoenix {
namespace ipc {

struct Consumer::Impl {
  std::string coord_file_path;
  std::string shm_name;
  int shm_fd = -1;
  void* shm_addr = nullptr;
  std::size_t shm_size = 0;
  layout::SimplifiedShmLayout* layout = nullptr;
  std::string last_error;
  bool valid = false;
  pid_t producer_pid = 0;

  ~Impl() {
    Cleanup();
  }

  bool Initialize(const std::string& coord_file, uint32_t /*timeout_ms*/) {
    coord_file_path = coord_file;

    // Read coordination file
    coordination::CoordinationMetadata metadata;
    auto result = coordination::CoordinationFile::Read(coord_file_path, metadata);
    if (result != coordination::CoordinationResult::kSuccess) {
      last_error = "Failed to read coordination file: " +
                   std::string(coordination::CoordinationFile::ResultToString(result));
      return false;
    }

    shm_name = metadata.shm_name;
    shm_size = metadata.shm_size;

    // Open shared memory (need RDWR to keep it alive)
    shm_fd = shm_open(shm_name.c_str(), O_RDWR, 0666);
    if (shm_fd < 0) {
      last_error = "shm_open failed: " + std::string(strerror(errno));
      return false;
    }

    // Map shared memory (read-write to keep reference)
    shm_addr = mmap(nullptr, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shm_addr == MAP_FAILED) {
      last_error = "mmap failed: " + std::string(strerror(errno));
      close(shm_fd);
      shm_fd = -1;
      return false;
    }

    // Get layout (consumer mode)
    layout = layout::SimplifiedShmLayoutFactory::Get(shm_addr);
    if (!layout) {
      last_error = "Failed to get layout";
      Cleanup();
      return false;
    }

    producer_pid = layout->GetProducerPid();
    valid = true;
    return true;
  }

  bool IsProducerAlive() const {
    if (producer_pid == 0) {
      return false;
    }
    return kill(producer_pid, 0) == 0;
  }

  void Cleanup() {
    valid = false;

    // Unmap and close shm
    if (shm_addr && shm_addr != MAP_FAILED) {
      munmap(shm_addr, shm_size);
      shm_addr = nullptr;
    }

    if (shm_fd >= 0) {
      close(shm_fd);
      shm_fd = -1;
    }

    // Unlink shm (cleanup after we're done)
    if (!shm_name.empty()) {
      shm_unlink(shm_name.c_str());
      shm_name.clear();
    }

    layout = nullptr;
  }

  std::optional<protocol::MsgHeader> DoReceive(uint8_t* payload_buffer, uint32_t timeout_ms) {
    if (!valid || !layout) {
      return std::nullopt;
    }

    // Wait for message if blocking
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (true) {
      // Check producer liveness first
      if (!IsProducerAlive()) {
        last_error = "Producer has exited";
        return std::nullopt;
      }

      // Check if queue has messages
      if (!layout->IsEmpty()) {
        break;
      }

      if (timeout_ms == 0) {
        return std::nullopt;
      }

      auto now = std::chrono::steady_clock::now();
      if (now >= deadline) {
        return std::nullopt;
      }

      // Brief sleep to avoid busy-waiting
      std::this_thread::sleep_for(std::chrono::microseconds(100));
    }

    // Re-check producer liveness before accessing shared memory
    if (!IsProducerAlive()) {
      last_error = "Producer has exited";
      return std::nullopt;
    }

    // Peek message header
    auto* header = layout->Peek();
    if (!header) {
      return std::nullopt;
    }

    // Read payload if available
    if (header->payload_len > 0 && payload_buffer) {
      const void* payload_src = layout->GetPayload(0);
      std::memcpy(payload_buffer, payload_src, header->payload_len);
    }

    // Pop message from queue
    if (!layout->Pop()) {
      return std::nullopt;
    }

    return *header;
  }
};

Consumer::Consumer() : impl_(std::make_unique<Impl>()) {}

Consumer::~Consumer() {
  Cleanup();
}

std::unique_ptr<Consumer> Consumer::Attach(
    const std::string& coord_file_path,
    uint32_t timeout_ms) {

  auto consumer = std::unique_ptr<Consumer>(new Consumer);
  if (!consumer->Initialize(coord_file_path, timeout_ms)) {
    return nullptr;
  }
  return consumer;
}

std::optional<protocol::MsgHeader> Consumer::Receive(uint8_t* payload_buffer) {
  return impl_->DoReceive(payload_buffer, 0);
}

std::optional<protocol::MsgHeader> Consumer::ReceiveBlocking(uint8_t* payload_buffer,
                                                               uint32_t timeout_ms) {
  return impl_->DoReceive(payload_buffer, timeout_ms);
}

std::size_t Consumer::AvailableMessages() const {
  if (!impl_ || !impl_->valid) return 0;
  return impl_->layout->Available();
}

std::size_t Consumer::QueueCapacity() const {
  if (!impl_ || !impl_->valid) return 0;
  return impl_->layout->GetQueueCapacity();
}

bool Consumer::IsValid() const {
  return impl_ && impl_->valid;
}

const std::string& Consumer::GetCoordFilePath() const {
  static const std::string empty;
  return impl_ ? impl_->coord_file_path : empty;
}

const std::string& Consumer::GetShmName() const {
  static const std::string empty;
  return impl_ ? impl_->shm_name : empty;
}

std::size_t Consumer::GetPayloadSize() const {
  if (!impl_ || !impl_->valid) return 0;
  return impl_->layout->GetPayloadSize();
}

const std::string& Consumer::GetLastError() const {
  static const std::string empty;
  return impl_ ? impl_->last_error : empty;
}

void Consumer::Cleanup() {
  if (impl_) {
    impl_->Cleanup();
  }
}

void Consumer::SetError(const std::string& error) {
  if (impl_) {
    impl_->last_error = error;
  }
}

bool Consumer::Initialize(const std::string& coord_file_path, uint32_t timeout_ms) {
  return impl_->Initialize(coord_file_path, timeout_ms);
}

}  // namespace ipc
}  // namespace phoenix
