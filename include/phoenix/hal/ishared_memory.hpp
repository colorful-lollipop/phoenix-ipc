#ifndef PHOENIX_HAL_ISHARED_MEMORY_HPP_
#define PHOENIX_HAL_ISHARED_MEMORY_HPP_

#include <cstddef>
#include <cstdint>
#include <memory>

namespace phoenix {
namespace hal {

/**
 * ISharedMemory - Platform-agnostic shared memory interface
 *
 * This interface abstracts shared memory operations for inter-process
 * communication. Implementations include:
 * - Linux: shm_open with mmap
 * - Windows: CreateFileMapping with MapViewOfFile
 *
 * Design principles:
 * - Named shared memory segments for persistence
 * - Memory mapping with proper alignment
 * - Ownership semantics for cleanup
 * - Size query and resize support
 */
class ISharedMemory {
 public:
  /**
   * Memory protection flags
   */
  enum class Protection : uint32_t {
    kRead = 1 << 0,
    kWrite = 1 << 1,
    kExec = 1 << 2,
  };

  /**
   * Ownership semantics for shared memory
   */
  enum class Ownership : uint8_t {
    kCreator,     // Create new, own cleanup
    kAttach,      // Attach to existing, no cleanup
    kServer,      // Server mode (create if not exists)
  };

  virtual ~ISharedMemory() = default;

  /**
   * Create or open a shared memory segment
   * @param name Unique name for the segment
   * @param size Requested size in bytes
   * @param ownership Ownership semantics
   * @return true if created/opened successfully
   */
  virtual bool Create(const char* name, size_t size, Ownership ownership) = 0;

  /**
   * Map the shared memory into process address space
   * @param protection Memory protection flags
   * @return Pointer to mapped memory, nullptr on failure
   */
  virtual void* Map(Protection protection) = 0;

  /**
   * Unmap the shared memory
   * @return true if unmapped successfully
   */
  virtual bool Unmap() = 0;

  /**
   * Get the mapped pointer
   * @return Pointer to mapped memory, nullptr if not mapped
   */
  virtual void* GetMappedAddress() const = 0;

  /**
   * Get the actual size of the shared memory
   * @return Size in bytes
   */
  virtual size_t GetSize() const = 0;

  /**
   * Get the name of the shared memory segment
   * @return Name string
   */
  virtual const char* GetName() const = 0;

  /**
   * Get native handle (file descriptor/handle)
   * @return Native handle for advanced operations
   */
  virtual int GetNativeHandle() const = 0;

  /**
   * Close and cleanup the shared memory
   * @param unlink If true, also remove from system (if supported)
   * @return true if closed successfully
   */
  virtual bool Close(bool unlink = false) = 0;

  /**
   * Check if the shared memory is valid/open
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
 * Shared memory factory for creating platform-specific implementations
 */
class SharedMemoryFactory {
 public:
  /**
   * Create a new shared memory object
   * @return Unique pointer to ISharedMemory
   */
  static std::unique_ptr<ISharedMemory> Create();
};

}  // namespace hal
}  // namespace phoenix

#endif  // PHOENIX_HAL_ISHARED_MEMORY_HPP_
