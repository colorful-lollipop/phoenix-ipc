#ifndef PHOENIX_CORE_SPSC_RING_BUFFER_HPP_
#define PHOENIX_CORE_SPSC_RING_BUFFER_HPP_

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <stdexcept>

namespace phoenix {
namespace core {

/**
 * SPSC Ring Buffer - Single Producer Single Consumer Lock-Free Queue
 *
 * A high-performance ring buffer designed for lock-free communication
 * between exactly one producer thread and one consumer thread.
 *
 * Key characteristics:
 * - Lock-free: Uses atomic operations only
 * - SPSC: Single Producer, Single Consumer
 * - Zero-copy: Works with pointers to pre-allocated memory
 * - Bounded: Fixed capacity
 *
 * Memory ordering:
 * - Producer writes data, then updates write_idx (release)
 * - Consumer reads write_idx, then reads data (acquire)
 * - This ensures data is visible only after the index update
 *
 * @tparam T The element type (typically a header or pointer)
 * @tparam Capacity The fixed capacity of the queue (must be power of 2 for efficient modulo)
 */
template <typename T, size_t Capacity>
class SpscRingBuffer {
 public:
  static_assert(Capacity > 0, "Capacity must be positive");
  static_assert((Capacity & (Capacity - 1)) == 0,
                "Capacity must be a power of 2");

  /**
   * Construct an empty SPSC Ring Buffer
   * @param buffer_start Pointer to the start of the buffer region (must be aligned)
   * @param buffer_size Size of the buffer region (must be >= sizeof(T) * Capacity)
   */
  explicit SpscRingBuffer(void* buffer_start = nullptr, size_t buffer_size = 0);

  // Non-copyable, non-movable
  SpscRingBuffer(const SpscRingBuffer&) = delete;
  SpscRingBuffer& operator=(const SpscRingBuffer&) = delete;
  SpscRingBuffer(SpscRingBuffer&&) = delete;
  SpscRingBuffer& operator=(SpscRingBuffer&&) = delete;

  ~SpscRingBuffer() = default;

  /**
   * Push an element to the back of the queue (Producer only)
   * @param item The item to push
   * @return true if pushed successfully, false if queue is full
   */
  bool Push(const T& item);

  /**
   * Push an element using placement new (Producer only)
   * @param args Arguments to construct the element
   * @return Pointer to the constructed element, nullptr if queue is full
   */
  template <typename... Args>
  T* PushEmplace(Args&&... args);

  /**
   * Peek at the front element without removing it (Consumer only)
   * @return Pointer to the front element, nullptr if queue is empty
   */
  T* Peek() const;

  /**
   * Pop the front element (Consumer only)
   * @return true if popped successfully, false if queue is empty
   */
  bool Pop();

  /**
   * Force advance the read index (for crash recovery)
   * @param count Number of elements to skip
   */
  void ForceAdvanceReadIndex(size_t count);

  /**
   * Check if the queue is empty
   * @return true if empty, false otherwise
   */
  bool Empty() const;

  /**
   * Check if the queue is full
   * @return true if full, false otherwise
   */
  bool Full() const;

  /**
   * Get the number of elements in the queue
   * @return Current count of elements
   */
  size_t Size() const;

  /**
   * Get the capacity of the queue
   * @return Fixed capacity
   */
  static constexpr size_t kCapacity = Capacity;

  /**
   * Get the current write index
   * @return Write index value
   */
  uint64_t WriteIndex() const;

  /**
   * Get the current read index
   * @return Read index value
   */
  uint64_t ReadIndex() const;

  /**
   * Get the distance from write to read (elements available to read)
   * @return Number of elements available
   */
  size_t Available() const;

  /**
   * Get the free space in the queue
   * @return Number of elements that can be pushed
   */
  size_t FreeSpace() const;

  /**
   * Initialize with external buffer (for shared memory usage)
   * @param buffer_start Pointer to the start of the buffer region
   * @param buffer_size Size of the buffer region
   * @return true if initialized successfully
   */
  bool Initialize(void* buffer_start, size_t buffer_size);

  /**
   * Check if the buffer has been initialized
   * @return true if initialized
   */
  bool IsInitialized() const;

 private:
  /**
   * Calculate the actual buffer offset for an index
   * @param index The logical index
   * @return The byte offset in the buffer
   */
  constexpr size_t BufferOffset(uint64_t index) const;

  /**
   * Calculate the element pointer for an index
   * @param index The logical index
   * @return Pointer to the element at that index
   */
  T* GetElementPtr(uint64_t index) const;

  // Buffer management
  void* buffer_start_ = nullptr;
  size_t buffer_size_ = 0;

  // Write index (Producer only) - using uint64_t to avoid wrap-around issues
  std::atomic<uint64_t> write_idx_{0};

  // Read index (Consumer only)
  std::atomic<uint64_t> read_idx_{0};

  // Padding to prevent false sharing with adjacent fields
  char padding_[56];
};

// Template implementation

template <typename T, size_t Capacity>
SpscRingBuffer<T, Capacity>::SpscRingBuffer(void* buffer_start,
                                             size_t buffer_size)
    : buffer_start_(buffer_start), buffer_size_(buffer_size) {
  // Initialize indices to 0
  write_idx_.store(0, std::memory_order_relaxed);
  read_idx_.store(0, std::memory_order_relaxed);
}

template <typename T, size_t Capacity>
bool SpscRingBuffer<T, Capacity>::Push(const T& item) {
  const uint64_t current_write = write_idx_.load(std::memory_order_relaxed);
  const uint64_t current_read = read_idx_.load(std::memory_order_acquire);

  // Check if full: write index has caught up to read index (with capacity offset)
  if (current_write - current_read >= kCapacity) {
    return false;  // Queue is full
  }

  // Get the slot and construct the element
  T* slot = GetElementPtr(current_write);
  new (slot) T(item);

  // Signal the new element to consumers (release semantics)
  write_idx_.store(current_write + 1, std::memory_order_release);

  return true;
}

template <typename T, size_t Capacity>
template <typename... Args>
T* SpscRingBuffer<T, Capacity>::PushEmplace(Args&&... args) {
  const uint64_t current_write = write_idx_.load(std::memory_order_relaxed);
  const uint64_t current_read = read_idx_.load(std::memory_order_acquire);

  // Check if full
  if (current_write - current_read >= kCapacity) {
    return nullptr;  // Queue is full
  }

  // Get the slot and construct the element in-place
  T* slot = GetElementPtr(current_write);
  new (slot) T(std::forward<Args>(args)...);

  // Signal the new element (release)
  write_idx_.store(current_write + 1, std::memory_order_release);

  return slot;
}

template <typename T, size_t Capacity>
T* SpscRingBuffer<T, Capacity>::Peek() const {
  const uint64_t current_read = read_idx_.load(std::memory_order_relaxed);
  const uint64_t current_write = write_idx_.load(std::memory_order_acquire);

  // Check if empty
  if (current_write == current_read) {
    return nullptr;
  }

  return GetElementPtr(current_read);
}

template <typename T, size_t Capacity>
bool SpscRingBuffer<T, Capacity>::Pop() {
  const uint64_t current_read = read_idx_.load(std::memory_order_relaxed);
  const uint64_t current_write = write_idx_.load(std::memory_order_acquire);

  // Check if empty
  if (current_write == current_read) {
    return false;
  }

  // Get the element and destroy it
  T* slot = GetElementPtr(current_read);
  slot->~T();

  // Advance read index (release)
  read_idx_.store(current_read + 1, std::memory_order_release);

  return true;
}

template <typename T, size_t Capacity>
void SpscRingBuffer<T, Capacity>::ForceAdvanceReadIndex(size_t count) {
  const uint64_t current_read = read_idx_.load(std::memory_order_relaxed);
  // For crash recovery: manually advance read index without destroying elements
  read_idx_.store(current_read + count, std::memory_order_release);
}

template <typename T, size_t Capacity>
bool SpscRingBuffer<T, Capacity>::Empty() const {
  return write_idx_.load(std::memory_order_acquire) ==
         read_idx_.load(std::memory_order_acquire);
}

template <typename T, size_t Capacity>
bool SpscRingBuffer<T, Capacity>::Full() const {
  const uint64_t write = write_idx_.load(std::memory_order_relaxed);
  const uint64_t read = read_idx_.load(std::memory_order_acquire);
  return (write - read) >= kCapacity;
}

template <typename T, size_t Capacity>
size_t SpscRingBuffer<T, Capacity>::Size() const {
  const uint64_t write = write_idx_.load(std::memory_order_acquire);
  const uint64_t read = read_idx_.load(std::memory_order_acquire);
  return static_cast<size_t>(write - read);
}



template <typename T, size_t Capacity>
uint64_t SpscRingBuffer<T, Capacity>::WriteIndex() const {
  return write_idx_.load(std::memory_order_acquire);
}

template <typename T, size_t Capacity>
uint64_t SpscRingBuffer<T, Capacity>::ReadIndex() const {
  return read_idx_.load(std::memory_order_acquire);
}

template <typename T, size_t Capacity>
size_t SpscRingBuffer<T, Capacity>::Available() const {
  return Size();
}

template <typename T, size_t Capacity>
size_t SpscRingBuffer<T, Capacity>::FreeSpace() const {
  const uint64_t write = write_idx_.load(std::memory_order_relaxed);
  const uint64_t read = read_idx_.load(std::memory_order_acquire);
  return kCapacity - static_cast<size_t>(write - read);
}

template <typename T, size_t Capacity>
bool SpscRingBuffer<T, Capacity>::Initialize(void* buffer_start,
                                              size_t buffer_size) {
  const size_t required_size = sizeof(T) * kCapacity;

  if (buffer_size < required_size) {
    return false;
  }

  buffer_start_ = buffer_start;
  buffer_size_ = buffer_size;

  // Initialize indices
  write_idx_.store(0, std::memory_order_relaxed);
  read_idx_.store(0, std::memory_order_relaxed);

  return true;
}

template <typename T, size_t Capacity>
bool SpscRingBuffer<T, Capacity>::IsInitialized() const {
  return buffer_start_ != nullptr;
}

template <typename T, size_t Capacity>
constexpr size_t SpscRingBuffer<T, Capacity>::BufferOffset(
    uint64_t index) const {
  // Power of 2 capacity allows efficient modulo with bitwise AND
  return static_cast<size_t>(index & (kCapacity - 1)) * sizeof(T);
}

template <typename T, size_t Capacity>
T* SpscRingBuffer<T, Capacity>::GetElementPtr(uint64_t index) const {
  return reinterpret_cast<T*>(
      static_cast<uint8_t*>(buffer_start_) + BufferOffset(index));
}

}  // namespace core
}  // namespace phoenix

#endif  // PHOENIX_CORE_SPSC_RING_BUFFER_HPP_
