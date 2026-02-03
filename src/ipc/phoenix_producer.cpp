/**
 * PhoenixIPC Producer Implementation
 */

#include "phoenix/ipc/phoenix_producer.hpp"

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

struct Producer::Impl {
  std::string coord_file_path;
  std::string shm_name;
  int shm_fd = -1;
  void* shm_addr = nullptr;
  std::size_t shm_size = 0;
  layout::SimplifiedShmLayout* layout = nullptr;
  std::string last_error;
  bool valid = false;

  ~Impl() {
    Cleanup();
  }

  bool Initialize(const std::string& coord_file, std::size_t queue_cap, std::size_t payload_size) {
    coord_file_path = coord_file;

    // Generate unique shm name
    shm_name = coordination::CoordinationFile::GenerateShmName("phoenix_ipc");

    // Calculate total size
    shm_size = layout::SimplifiedShmLayout::CalculateSize(queue_cap, payload_size);
    if (shm_size == 0) {
      last_error = "Invalid size calculation";
      return false;
    }

    // Create shared memory
    shm_fd = shm_open(shm_name.c_str(), O_CREAT | O_RDWR, 0666);
    if (shm_fd < 0) {
      last_error = "shm_open failed: " + std::string(strerror(errno));
      return false;
    }

    // Set size
    if (ftruncate(shm_fd, shm_size) < 0) {
      last_error = "ftruncate failed: " + std::string(strerror(errno));
      close(shm_fd);
      shm_fd = -1;
      return false;
    }

    // Map
    shm_addr = mmap(nullptr, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shm_addr == MAP_FAILED) {
      last_error = "mmap failed: " + std::string(strerror(errno));
      close(shm_fd);
      shm_fd = -1;
      return false;
    }

    // Initialize layout
    layout = layout::SimplifiedShmLayoutFactory::Create(
        shm_addr, shm_size, queue_cap, payload_size, getpid());
    if (!layout) {
      last_error = "Failed to initialize layout";
      Cleanup();
      return false;
    }

    // Write coordination file
    coordination::CoordinationMetadata metadata;
    metadata.shm_name = shm_name;
    metadata.shm_size = shm_size;
    metadata.queue_capacity = queue_cap;
    metadata.payload_size = payload_size;
    metadata.producer_pid = getpid();
    metadata.creation_time = std::chrono::seconds(time(nullptr));
    metadata.modify_time = metadata.creation_time;

    auto result = coordination::CoordinationFile::Create(coord_file_path, metadata);
    if (result != coordination::CoordinationResult::kSuccess) {
      last_error = "Failed to create coordination file: " +
                   std::string(coordination::CoordinationFile::ResultToString(result));
      Cleanup();
      return false;
    }

    valid = true;
    return true;
  }

  void Cleanup() {
    valid = false;

    // Delete coordination file
    if (!coord_file_path.empty()) {
      coordination::CoordinationFile::Delete(coord_file_path);
      coord_file_path.clear();
    }

    // Unmap and close shm (but don't unlink - let consumer do cleanup)
    if (shm_addr && shm_addr != MAP_FAILED) {
      munmap(shm_addr, shm_size);
      shm_addr = nullptr;
    }

    if (shm_fd >= 0) {
      close(shm_fd);
      shm_fd = -1;
    }

    // Don't unlink shm here - shared memory persists until consumer cleans up
    // The shm_unlink will be called by the consumer when it's done

    layout = nullptr;
  }

  SendResult DoSend(uint32_t func_id, const uint8_t* payload, std::size_t payload_len, uint32_t timeout_ms) {
    if (!valid || !layout) {
      return SendResult::kShmClosed;
    }

    // Check payload size
    if (payload_len > layout->GetPayloadSize()) {
      last_error = "Payload too large";
      return SendResult::kError;
    }

    // Wait for space if blocking
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (layout->IsFull()) {
      if (timeout_ms == 0) {
        return SendResult::kQueueFull;
      }

      auto now = std::chrono::steady_clock::now();
      if (now >= deadline) {
        return SendResult::kQueueFull;
      }

      // Brief sleep to avoid busy-waiting
      std::this_thread::sleep_for(std::chrono::microseconds(100));
    }

    // Write payload first (if any)
    if (payload && payload_len > 0) {
      void* payload_dest = layout->GetPayload(0);
      std::memcpy(payload_dest, payload, payload_len);
    }

    // Create message header
    protocol::MsgHeader header;
    header.seq_id = 0;  // Not used for simple SPSC
    header.func_id = func_id;
    header.payload_len = static_cast<uint32_t>(payload_len);

    // Push to queue
    if (!layout->Push(header)) {
      return SendResult::kQueueFull;
    }

    return SendResult::kSuccess;
  }
};

Producer::Producer() : impl_(std::make_unique<Impl>()) {}

Producer::~Producer() {
  Cleanup();
}

std::unique_ptr<Producer> Producer::Create(
    const std::string& coord_file_path,
    std::size_t queue_capacity,
    std::size_t payload_size) {

  auto producer = std::unique_ptr<Producer>(new Producer);
  if (!producer->Initialize(coord_file_path, queue_capacity, payload_size)) {
    return nullptr;
  }
  return producer;
}

SendResult Producer::Send(uint32_t func_id,
                          const uint8_t* payload,
                          std::size_t payload_len) {
  return impl_->DoSend(func_id, payload, payload_len, 0);
}

SendResult Producer::SendBlocking(uint32_t func_id,
                                  const uint8_t* payload,
                                  std::size_t payload_len,
                                  uint32_t timeout_ms) {
  return impl_->DoSend(func_id, payload, payload_len, timeout_ms);
}

SendResult Producer::Send(uint32_t func_id, const std::vector<uint8_t>& payload) {
  return impl_->DoSend(func_id, payload.data(), payload.size(), 0);
}

std::size_t Producer::AvailableSpace() const {
  if (!impl_ || !impl_->valid) return 0;
  return impl_->layout->FreeSpace();
}

std::size_t Producer::QueueCapacity() const {
  if (!impl_ || !impl_->valid) return 0;
  return impl_->layout->GetQueueCapacity();
}

bool Producer::IsValid() const {
  return impl_ && impl_->valid;
}

const std::string& Producer::GetCoordFilePath() const {
  static const std::string empty;
  return impl_ ? impl_->coord_file_path : empty;
}

const std::string& Producer::GetShmName() const {
  static const std::string empty;
  return impl_ ? impl_->shm_name : empty;
}

const std::string& Producer::GetLastError() const {
  static const std::string empty;
  return impl_ ? impl_->last_error : empty;
}

void Producer::Cleanup() {
  if (impl_) {
    impl_->Cleanup();
  }
}

void Producer::SetError(const std::string& error) {
  if (impl_) {
    impl_->last_error = error;
  }
}

bool Producer::Initialize(const std::string& coord_file_path,
                          std::size_t queue_cap,
                          std::size_t payload_size) {
  return impl_->Initialize(coord_file_path, queue_cap, payload_size);
}

}  // namespace ipc
}  // namespace phoenix
