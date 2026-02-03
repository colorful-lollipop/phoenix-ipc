#include "phoenix/ipc/ipc_server.hpp"

#include <fcntl.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#include <iostream>

namespace phoenix {
namespace ipc {

// Default configuration
static ServerConfig GetDefaultConfig(const std::string& shm_name) {
  ServerConfig config;
  config.shm_name = shm_name;
  config.shm_size = layout::PhoenixShmLayout::RequiredSize();
  config.worker_timeout_us = 5000000;  // 5 seconds
  config.max_pending_requests = 10000;
  config.enable_cpu_affinity = false;
  return config;
}

IpcServer::IpcServer(const ServerConfig& config)
    : config_(config),
      shm_name_("/" + config.shm_name),
      response_signal_() {}

IpcServer::~IpcServer() {
  Stop();
}

bool IpcServer::Start(const std::string& worker_path, int argc, char** argv) {
  if (running_.load()) {
    return true;
  }

  // Create shared memory
  shm_size_ = layout::PhoenixShmLayout::RequiredSize();
  shm_fd_ = shm_open(shm_name_.c_str(), O_CREAT | O_RDWR, 0666);
  if (shm_fd_ < 0) {
    std::cerr << "Failed to create shared memory: " << strerror(errno) << std::endl;
    return false;
  }

  if (ftruncate(shm_fd_, shm_size_) < 0) {
    std::cerr << "Failed to set shared memory size: " << strerror(errno) << std::endl;
    close(shm_fd_);
    shm_unlink(shm_name_.c_str());
    return false;
  }

  shm_addr_ = mmap(nullptr, shm_size_, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_, 0);
  if (shm_addr_ == MAP_FAILED) {
    std::cerr << "Failed to map shared memory: " << strerror(errno) << std::endl;
    close(shm_fd_);
    shm_unlink(shm_name_.c_str());
    return false;
  }

  // Initialize layout
  layout_ = new (shm_addr_) layout::PhoenixShmLayout();
  if (!layout_->Initialize(shm_size_)) {
    std::cerr << "Failed to initialize layout" << std::endl;
    munmap(shm_addr_, shm_size_);
    close(shm_fd_);
    shm_unlink(shm_name_.c_str());
    return false;
  }

  // Create response signal
  if (!response_signal_.Create(0, 0)) {
    std::cerr << "Failed to create response signal: " << response_signal_.GetLastError() << std::endl;
    return false;
  }

  // Initialize handlers
  handlers_.resize(kMaxHandlers);

  // Start supervisor thread
  running_.store(true);
  state_.store(ConnectionState::kConnecting);

  supervisor_thread_ = std::thread(&IpcServer::SupervisorMain, this);
  collector_thread_ = std::thread(&IpcServer::ResponseCollectorMain, this);

  // Fork worker process
  worker_pid_ = fork();
  if (worker_pid_ < 0) {
    std::cerr << "Failed to fork worker: " << strerror(errno) << std::endl;
    Stop();
    return false;
  }

  if (worker_pid_ == 0) {
    // Child process - exec worker
    char* child_argv[4];
    child_argv[0] = const_cast<char*>(worker_path.c_str());
    child_argv[1] = const_cast<char*>(shm_name_.c_str());
    child_argv[2] = const_cast<char*>(std::to_string(shm_size_).c_str());
    child_argv[3] = nullptr;

    execv(worker_path.c_str(), child_argv);
    // If exec fails, exit
    _exit(127);
  }

  // Parent process
  state_.store(ConnectionState::kConnected);
  std::cout << "IPC Server started with worker PID: " << worker_pid_ << std::endl;

  return true;
}

void IpcServer::Stop() {
  if (!running_.load()) {
    return;
  }

  running_.store(false);

  // Kill worker if running
  if (worker_pid_ > 0) {
    kill(worker_pid_, SIGTERM);
    waitpid(worker_pid_, nullptr, 0);
    worker_pid_ = -1;
  }

  // Stop threads
  if (supervisor_thread_.joinable()) {
    supervisor_thread_.join();
  }
  if (collector_thread_.joinable()) {
    collector_thread_.join();
  }

  // Cleanup
  response_signal_.Reset();

  if (shm_addr_ != nullptr && shm_addr_ != MAP_FAILED) {
    munmap(shm_addr_, shm_size_);
    shm_addr_ = nullptr;
  }

  if (shm_fd_ >= 0) {
    close(shm_fd_);
    shm_fd_ = -1;
  }

  shm_unlink(shm_name_.c_str());

  state_.store(ConnectionState::kDisconnected);
  std::cout << "IPC Server stopped" << std::endl;
}

bool IpcServer::IsRunning() const {
  return running_.load() && state_.load() == ConnectionState::kConnected;
}

std::future<protocol::MsgHeader> IpcServer::SendAsync(
    uint32_t func_id,
    const uint8_t* payload,
    std::size_t payload_size) {
  return SendAsync(func_id, payload, payload_size, protocol::Priority::kLow);
}

std::future<protocol::MsgHeader> IpcServer::SendAsync(
    uint32_t func_id,
    const uint8_t* payload,
    std::size_t payload_size,
    protocol::Priority priority) {
  protocol::MsgHeader header;
  header.seq_id = next_seq_id_++;
  header.func_id = func_id;
  header.payload_len = static_cast<uint32_t>(payload_size);

  if (!DispatchMessage(header, payload, priority)) {
    // Return an already completed future with error
    protocol::MsgHeader error;
    error.seq_id = 0;
    auto promise = std::make_shared<std::promise<protocol::MsgHeader>>();
    promise->set_value(error);
    return promise->get_future();
  }

  requests_sent_++;

  // For simplicity, return an empty future for now
  // Full implementation would track pending requests and fulfill promises
  protocol::MsgHeader placeholder;
  placeholder.seq_id = header.seq_id;
  auto promise = std::make_shared<std::promise<protocol::MsgHeader>>();
  promise->set_value(placeholder);
  return promise->get_future();
}

protocol::MsgHeader IpcServer::Send(
    uint32_t func_id,
    const uint8_t* payload,
    std::size_t payload_size,
    uint32_t timeout_ms) {
  auto future = SendAsync(func_id, payload, payload_size);
  if (future.wait_for(std::chrono::milliseconds(timeout_ms)) == std::future_status::ready) {
    return future.get();
  }
  protocol::MsgHeader timeout;
  timeout.seq_id = 0;
  return timeout;
}

void IpcServer::RegisterHandler(uint32_t func_id, RequestCallback callback) {
  if (func_id < kMaxHandlers) {
    handlers_[func_id] = std::move(callback);
  }
}

IpcServer::ServerStats IpcServer::GetStats() const {
  ServerStats stats;
  stats.requests_sent = requests_sent_.load();
  stats.responses_received = responses_received_.load();
  stats.crashes_detected = crashes_detected_.load();
  stats.poison_pills_skipped = poison_pills_skipped_.load();
  stats.normal_queue_load = layout_->FindBestNormalWorker();
  stats.vip_queue_load = layout_->FindBestVipWorker();
  return stats;
}

void IpcServer::SupervisorMain() {
  while (running_.load()) {
    int status = 0;
    pid_t result = waitpid(worker_pid_, &status, WNOHANG);

    if (result == worker_pid_) {
      // Worker crashed or exited
      if (WIFEXITED(status)) {
        std::cout << "Worker exited with code: " << WEXITSTATUS(status) << std::endl;
      } else if (WIFSIGNALED(status)) {
        std::cout << "Worker killed by signal: " << WTERMSIG(status) << std::endl;
      }

      crashes_detected_++;
      state_.store(ConnectionState::kCrashed);

      // Recover from crash
      RecoverFromCrash();

      // Restart worker
      worker_pid_ = fork();
      if (worker_pid_ < 0) {
        std::cerr << "Failed to restart worker" << std::endl;
        running_.store(false);
        break;
      }

      if (worker_pid_ == 0) {
        // Child would need to re-exec, but for simplicity, just exit
        _exit(0);
      }

      state_.store(ConnectionState::kConnected);
      std::cout << "Worker restarted with PID: " << worker_pid_ << std::endl;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}

void IpcServer::ResponseCollectorMain() {
  while (running_.load()) {
    // Wait for response signal
    if (response_signal_.Wait(100) && IsRunning()) {
      responses_received_++;
      // Response processing would happen here
      // For now, just consume the signal
    }
  }
}

bool IpcServer::DispatchMessage(const protocol::MsgHeader& header,
                                 const uint8_t* payload,
                                 protocol::Priority priority) {
  // Select best worker based on load
  std::size_t worker_index = SelectBestWorker(priority);

  if (priority == protocol::Priority::kHigh) {
    auto& channel = layout_->GetVipChannel(worker_index);

    if (channel.request_queue.Full()) {
      return false;
    }

    if (!channel.request_queue.Push(header)) {
      return false;
    }

    // Note: In real implementation, would trigger VIP worker eventfd here
    return true;
  } else {
    auto& channel = layout_->GetNormalChannel(worker_index);

    if (channel.request_queue.Full()) {
      return false;
    }

    if (!channel.request_queue.Push(header)) {
      return false;
    }

    // Note: In real implementation, would trigger normal worker eventfd here
    return true;
  }
}

std::size_t IpcServer::SelectBestWorker(protocol::Priority priority) {
  if (priority == protocol::Priority::kHigh) {
    return layout_->FindBestVipWorker();
  }
  return layout_->FindBestNormalWorker();
}

void IpcServer::HandleCrash() {
  std::cout << "Handling worker crash..." << std::endl;
}

void IpcServer::RecoverFromCrash() {
  // Scan and recover crashed workers
  std::size_t skipped = layout_->RecoverCrashedWorkers();
  poison_pills_skipped_ += skipped;

  std::cout << "Recovered " << skipped << " poison pill messages" << std::endl;
}

std::unique_ptr<IpcServer> CreateIpcServer(const std::string& shm_name) {
  auto config = GetDefaultConfig(shm_name);
  return std::make_unique<IpcServer>(config);
}

}  // namespace ipc
}  // namespace phoenix
