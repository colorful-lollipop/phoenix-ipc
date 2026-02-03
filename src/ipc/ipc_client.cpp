#include "phoenix/ipc/ipc_client.hpp"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <iostream>

namespace phoenix {
namespace ipc {

IpcClient::IpcClient(const std::string& shm_name)
    : shm_name_("/" + shm_name),
      response_signal_() {}

IpcClient::~IpcClient() {
  Disconnect();
}

bool IpcClient::Connect(uint32_t timeout_ms) {
  if (connected_.load()) {
    return true;
  }

  // Open existing shared memory
  shm_fd_ = shm_open(shm_name_.c_str(), O_RDWR, 0666);
  if (shm_fd_ < 0) {
    std::cerr << "Failed to open shared memory: " << strerror(errno) << std::endl;
    return false;
  }

  // Get size
  struct stat st;
  if (fstat(shm_fd_, &st) < 0) {
    std::cerr << "Failed to stat shared memory: " << strerror(errno) << std::endl;
    close(shm_fd_);
    return false;
  }
  shm_size_ = st.st_size;

  // Map
  shm_addr_ = mmap(nullptr, shm_size_, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_, 0);
  if (shm_addr_ == MAP_FAILED) {
    std::cerr << "Failed to map shared memory: " << strerror(errno) << std::endl;
    close(shm_fd_);
    return false;
  }

  // Get layout
  layout_ = layout::ShmLayoutFactory::Get(shm_addr_);
  if (!layout_) {
    std::cerr << "Invalid shared memory layout" << std::endl;
    munmap(shm_addr_, shm_size_);
    close(shm_fd_);
    return false;
  }

  // Create response signal
  if (!response_signal_.Create(0, 0)) {
    std::cerr << "Failed to create response signal" << std::endl;
    return false;
  }

  // Start response poller
  running_.store(true);
  poller_thread_ = std::thread(&IpcClient::ResponsePollerMain, this);

  connected_.store(true);
  std::cout << "IPC Client connected to " << shm_name_ << std::endl;

  return true;
}

void IpcClient::Disconnect() {
  if (!connected_.load()) {
    return;
  }

  running_.store(false);

  if (poller_thread_.joinable()) {
    poller_thread_.join();
  }

  response_signal_.Reset();

  if (shm_addr_ != nullptr && shm_addr_ != MAP_FAILED) {
    munmap(shm_addr_, shm_size_);
    shm_addr_ = nullptr;
  }

  if (shm_fd_ >= 0) {
    close(shm_fd_);
    shm_fd_ = -1;
  }

  layout_ = nullptr;
  connected_.store(false);

  // Clear pending requests
  {
    std::lock_guard<std::mutex> lock(pending_mutex_);
    pending_requests_.clear();
  }

  std::cout << "IPC Client disconnected" << std::endl;
}

bool IpcClient::IsConnected() const {
  return connected_.load();
}

protocol::MsgHeader IpcClient::Call(
    uint32_t func_id,
    const uint8_t* payload,
    std::size_t payload_size,
    uint32_t timeout_ms) {
  return Call(func_id, payload, payload_size, protocol::Priority::kLow, timeout_ms);
}

protocol::MsgHeader IpcClient::Call(
    uint32_t func_id,
    const uint8_t* payload,
    std::size_t payload_size,
    protocol::Priority priority,
    uint32_t timeout_ms) {
  if (!connected_.load()) {
    errors_++;
    protocol::MsgHeader error;
    error.seq_id = 0;
    return error;
  }

  uint64_t seq_id = next_seq_id_++;

  protocol::MsgHeader header;
  header.seq_id = seq_id;
  header.func_id = func_id;
  header.payload_len = static_cast<uint32_t>(payload_size);

  if (!SendRequest(header, payload, priority)) {
    errors_++;
    protocol::MsgHeader error;
    error.seq_id = 0;
    return error;
  }

  requests_sent_++;

  // Wait for response
  protocol::MsgHeader response = WaitForResponse(seq_id, timeout_ms);
  if (response.seq_id == 0) {
    timeouts_++;
  }
  return response;
}

std::future<protocol::MsgHeader> IpcClient::CallAsync(
    uint32_t func_id,
    const uint8_t* payload,
    std::size_t payload_size) {
  return CallAsync(func_id, payload, payload_size, protocol::Priority::kLow);
}

std::future<protocol::MsgHeader> IpcClient::CallAsync(
    uint32_t func_id,
    const uint8_t* payload,
    std::size_t payload_size,
    protocol::Priority priority) {
  if (!connected_.load()) {
    protocol::MsgHeader error;
    error.seq_id = 0;
    auto promise = std::make_shared<std::promise<protocol::MsgHeader>>();
    promise->set_value(error);
    return promise->get_future();
  }

  uint64_t seq_id = next_seq_id_++;

  protocol::MsgHeader header;
  header.seq_id = seq_id;
  header.func_id = func_id;
  header.payload_len = static_cast<uint32_t>(payload_size);

  // Create pending request entry
  auto pending = std::make_shared<PendingRequest>();
  {
    std::lock_guard<std::mutex> lock(pending_mutex_);
    pending_requests_[seq_id] = pending;
  }

  // Send request
  if (!SendRequest(header, payload, priority)) {
    std::lock_guard<std::mutex> lock(pending_mutex_);
    pending_requests_.erase(seq_id);
    errors_++;
    protocol::MsgHeader error;
    error.seq_id = 0;
    auto promise = std::make_shared<std::promise<protocol::MsgHeader>>();
    promise->set_value(error);
    return promise->get_future();
  }

  requests_sent_++;

  return pending->promise.get_future();
}

IpcClient::ClientStats IpcClient::GetStats() const {
  ClientStats stats;
  stats.requests_sent = requests_sent_.load();
  stats.responses_received = responses_received_.load();
  stats.timeouts = timeouts_.load();
  stats.errors = errors_.load();
  return stats;
}

void IpcClient::ResponsePollerMain() {
  while (running_.load()) {
    // Wait for response signal with timeout
    if (response_signal_.Wait(100) && connected_.load()) {
      responses_received_++;

      // Check all normal and VIP channels for responses
      // For simplicity, we check all workers' response queues
      for (std::size_t i = 0; i < layout::PhoenixShmLayout::kNormalWorkerCount; ++i) {
        auto& channel = layout_->GetNormalChannel(i);
        while (!channel.response_queue.Empty()) {
          auto* header = channel.response_queue.Peek();
          if (header) {
            uint64_t seq_id = header->seq_id;

            // Find and notify pending request
            std::shared_ptr<PendingRequest> pending;
            {
              std::lock_guard<std::mutex> lock(pending_mutex_);
              auto it = pending_requests_.find(seq_id);
              if (it != pending_requests_.end()) {
                pending = it->second;
                pending_requests_.erase(it);
              }
            }

            if (pending) {
              std::lock_guard<std::mutex> lock(pending->mutex);
              pending->response = *header;
              pending->ready = true;
              pending->promise.set_value(*header);
            }
          }
          channel.response_queue.Pop();
        }
      }

      for (std::size_t i = 0; i < layout::PhoenixShmLayout::kVipWorkerCount; ++i) {
        auto& channel = layout_->GetVipChannel(i);
        while (!channel.response_queue.Empty()) {
          auto* header = channel.response_queue.Peek();
          if (header) {
            uint64_t seq_id = header->seq_id;

            std::shared_ptr<PendingRequest> pending;
            {
              std::lock_guard<std::mutex> lock(pending_mutex_);
              auto it = pending_requests_.find(seq_id);
              if (it != pending_requests_.end()) {
                pending = it->second;
                pending_requests_.erase(it);
              }
            }

            if (pending) {
              std::lock_guard<std::mutex> lock(pending->mutex);
              pending->response = *header;
              pending->ready = true;
              pending->promise.set_value(*header);
            }
          }
          channel.response_queue.Pop();
        }
      }
    }
  }
}

bool IpcClient::SendRequest(const protocol::MsgHeader& header,
                            const uint8_t* payload,
                            protocol::Priority priority) {
  std::size_t worker_index = 0;

  if (priority == protocol::Priority::kHigh) {
    worker_index = layout_->FindBestVipWorker();
    auto& channel = layout_->GetVipChannel(worker_index);

    if (channel.request_queue.Full()) {
      return false;
    }

    if (!channel.request_queue.Push(header)) {
      return false;
    }

    // Trigger worker notification (would trigger eventfd here)
    return true;
  } else {
    worker_index = layout_->FindBestNormalWorker();
    auto& channel = layout_->GetNormalChannel(worker_index);

    if (channel.request_queue.Full()) {
      return false;
    }

    if (!channel.request_queue.Push(header)) {
      return false;
    }

    // Trigger worker notification (would trigger eventfd here)
    return true;
  }
}

protocol::MsgHeader IpcClient::WaitForResponse(uint64_t seq_id, uint32_t timeout_ms) {
  auto start_time = std::chrono::steady_clock::now();
  auto timeout_duration = std::chrono::milliseconds(timeout_ms);

  // Create a temporary promise/future for waiting
  std::promise<protocol::MsgHeader> promise;
  auto future = promise.get_future();

  // Create pending entry
  auto pending = std::make_shared<PendingRequest>();
  pending->promise = std::move(promise);

  {
    std::lock_guard<std::mutex> lock(pending_mutex_);
    pending_requests_[seq_id] = pending;
  }

  // Wait for response or timeout
  while (true) {
    auto remaining = timeout_duration - (std::chrono::steady_clock::now() - start_time);
    if (remaining.count() <= 0) {
      // Timeout - remove pending request
      std::lock_guard<std::mutex> lock(pending_mutex_);
      pending_requests_.erase(seq_id);
      protocol::MsgHeader timeout;
      timeout.seq_id = 0;
      return timeout;
    }

    auto status = future.wait_for(remaining);
    if (status == std::future_status::ready) {
      return future.get();
    }
  }
}

std::unique_ptr<IpcClient> CreateIpcClient(const std::string& shm_name) {
  return std::make_unique<IpcClient>(shm_name);
}

}  // namespace ipc
}  // namespace phoenix
