#ifndef PHOENIX_PLATFORM_LINUX_EVENTFD_HPP_
#define PHOENIX_PLATFORM_LINUX_EVENTFD_HPP_

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <poll.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <memory>
#include <string>

#include "../hal/isignal_mechanism.hpp"

namespace phoenix {
namespace platform {

/**
 * LinuxEventFdSignal - Linux eventfd-based signaling mechanism
 *
 * Uses Linux eventfd for efficient inter-thread/event notification.
 * Features:
 * - Edge-triggered semantics (counter-based)
 * - Supports non-blocking operations
 * - Compatible with epoll/select
 */
class LinuxEventFdSignal : public hal::ISignalMechanism {
 public:
  LinuxEventFdSignal() : fd_(-1), valid_(false) {}

  ~LinuxEventFdSignal() override {
    if (fd_ >= 0) {
      ::close(fd_);
      fd_ = -1;
    }
  }

  bool Notify() override {
    if (!valid_) {
      return false;
    }

    uint64_t value = 1;
    ssize_t written = ::write(fd_, &value, sizeof(value));
    return written == sizeof(value);
  }

  bool Wait(uint32_t timeout_ms) override {
    if (!valid_) {
      return false;
    }

    struct pollfd pfd;
    pfd.fd = fd_;
    pfd.events = POLLIN;

    int ret = ::poll(&pfd, 1, static_cast<int>(timeout_ms));
    if (ret > 0) {
      uint64_t value;
      ::read(fd_, &value, sizeof(value));
      return true;
    }
    return false;
  }

  bool TryWait() override {
    if (!valid_) {
      return false;
    }

    uint64_t value;
    ssize_t n = ::read(fd_, &value, sizeof(value));
    return n == sizeof(value);
  }

  bool Reset() override {
    if (!valid_) {
      return false;
    }

    uint64_t value;
    while (::read(fd_, &value, sizeof(value)) == sizeof(value)) {
      // Drain all pending values
    }
    return true;
  }

  int GetNativeHandle() const override {
    return fd_;
  }

  bool IsValid() const override {
    return valid_ && fd_ >= 0;
  }

  bool Create(uint64_t initial_value = 0, int flags = 0) {
    fd_ = ::eventfd(initial_value, flags);
    if (fd_ < 0) {
      last_error_ = strerror(errno);
      return false;
    }

    valid_ = true;
    return true;
  }

  const char* GetLastError() const override {
    return last_error_.c_str();
  }

 private:
  int fd_;
  bool valid_;
  std::string last_error_;
};

/**
 * Create a new eventfd signal mechanism
 */
inline std::unique_ptr<hal::ISignalMechanism> CreateEventFdSignal() {
  auto signal = std::make_unique<LinuxEventFdSignal>();
  if (!signal->Create(0, 0)) {
    return nullptr;
  }
  return signal;
}

}  // namespace platform
}  // namespace phoenix

#endif  // PHOENIX_PLATFORM_LINUX_EVENTFD_HPP_
