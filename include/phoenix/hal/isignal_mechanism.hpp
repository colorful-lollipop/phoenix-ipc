#ifndef PHOENIX_HAL_ISIGNAL_MECHANISM_HPP_
#define PHOENIX_HAL_ISIGNAL_MECHANISM_HPP_

#include <cstddef>
#include <cstdint>

namespace phoenix {
namespace hal {

/**
 * ISignalMechanism - Platform-agnostic signaling interface
 *
 * This interface abstracts signaling mechanisms used for inter-thread
 * or inter-process notification. Implementations include:
 * - Linux: eventfd, signalfd
 * - Windows: Event objects, condition variables
 *
 * Design principles:
 * - Edge-triggered or level-triggered semantics based on implementation
 * - Support for timeout-based waiting
 * - Thread-safe operations
 */
class ISignalMechanism {
 public:
  virtual ~ISignalMechanism() = default;

  /**
   * Notify one waiting thread (edge-triggered)
   * Increments the counter/wakes one thread
   * @return true if notification succeeded
   */
  virtual bool Notify() = 0;

  /**
   * Wait for signal to be raised
   * Blocks until notification or timeout
   * @param timeout_ms Maximum time to wait in milliseconds (0 = infinite)
   * @return true if signaled, false if timeout
   */
  virtual bool Wait(uint32_t timeout_ms = 0) = 0;

  /**
   * Try to wait without blocking
   * @return true if signaled, false if not
   */
  virtual bool TryWait() = 0;

  /**
   * Reset the signal (clear pending notifications)
   * @return true if reset succeeded
   */
  virtual bool Reset() = 0;

  /**
   * Get file descriptor or handle (platform-specific)
   * @return Native handle for polling/selecting
   */
  virtual int GetNativeHandle() const = 0;

  /**
   * Check if the mechanism is valid/open
   * @return true if valid
   */
  virtual bool IsValid() const = 0;

  /**
   * Get the last error message
   * @return Error description string
   */
  virtual const char* GetLastError() const = 0;
};

/**
 * Signal mechanism type enumeration
 */
enum class SignalMechanismType {
  kEventFd,       // Linux eventfd
  kSignalfd,      // Linux signalfd
  kEventObject,   // Windows Event
  kConditionVar,  // Cross-platform condition variable
};

}  // namespace hal
}  // namespace phoenix

#endif  // PHOENIX_HAL_ISIGNAL_MECHANISM_HPP_
