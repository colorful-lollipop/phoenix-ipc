/**
 * Coordination File Management for PhoenixIPC
 *
 * This module handles the JSON coordination file used for
 * producer-consumer discovery and attachment.
 *
 * Coordination File Format (JSON):
 * {
 *   "version": "1.0",
 *   "shm_name": "/phoenix_ipc_12345",
 *   "shm_size": 65536,
 *   "queue_capacity": 1024,
 *   "payload_size": 32768,
 *   "producer_pid": 12345,
 *   "creation_timestamp": 1704067200,
 *   "last_modify_timestamp": 1704067200
 * }
 */

#ifndef PHOENIX_COORDINATION_COORDINATION_FILE_HPP_
#define PHOENIX_COORDINATION_COORDINATION_FILE_HPP_

#include <cstdint>
#include <string>
#include <optional>
#include <chrono>
#include <sys/types.h>

namespace phoenix {
namespace coordination {

/**
 * Coordination metadata extracted from the coordination file
 */
struct CoordinationMetadata {
  std::string shm_name;          // POSIX shared memory name (e.g., "/phoenix_ipc")
  std::size_t shm_size;          // Total shared memory size in bytes
  std::size_t queue_capacity;    // Number of messages in queue
  std::size_t payload_size;      // Payload buffer size in bytes
  pid_t producer_pid;            // PID of producer process
  std::chrono::seconds creation_time;  // Creation timestamp
  std::chrono::seconds modify_time;    // Last modification timestamp

  static constexpr const char* kVersion = "1.0";
};

/**
 * Result of a coordination file operation
 */
enum class CoordinationResult {
  kSuccess = 0,
  kFileNotFound = 1,
  kParseError = 2,
  kInvalidFormat = 3,
  kVersionMismatch = 4,
  kProducerGone = 5,
  kIoError = 6
};

/**
 * Coordination file manager
 */
class CoordinationFile {
 public:
  /**
   * Create coordination file with metadata
   * @param file_path Path to coordination file
   * @param metadata Coordination metadata
   * @return Result of operation
   */
  static CoordinationResult Create(const std::string& file_path,
                                    const CoordinationMetadata& metadata);

  /**
   * Read and parse coordination file
   * @param file_path Path to coordination file
   * @param metadata Output metadata (valid only if result is kSuccess)
   * @return Result of operation
   */
  static CoordinationResult Read(const std::string& file_path,
                                  CoordinationMetadata& metadata);

  /**
   * Update modify timestamp in coordination file
   * @param file_path Path to coordination file
   * @return Result of operation
   */
  static CoordinationResult Touch(const std::string& file_path);

  /**
   * Delete coordination file
   * @param file_path Path to coordination file
   * @return Result of operation
   */
  static CoordinationResult Delete(const std::string& file_path);

  /**
   * Check if coordination file exists and is valid
   * @param file_path Path to coordination file
   * @param metadata Output metadata (if valid)
   * @return true if file exists and is valid
   */
  static bool Exists(const std::string& file_path,
                     CoordinationMetadata* metadata = nullptr);

  /**
   * Check if producer is still alive
   * @param metadata Coordination metadata
   * @return true if producer process exists
   */
  static bool IsProducerAlive(const CoordinationMetadata& metadata);

  /**
   * Get error message for result
   * @param result Coordination result
   * @return Human-readable error message
   */
  static const char* ResultToString(CoordinationResult result);

  /**
   * Generate a unique shared memory name
   * @param base_name Base name (e.g., "phoenix_ipc")
   * @return Unique shm name with PID and timestamp
   */
  static std::string GenerateShmName(const std::string& base_name = "phoenix_ipc");

  /**
   * Get default coordination file path
   * @param base_name Base name
   * @return Default coordination file path (~/.phoenix_ipc)
   */
  static std::string GetDefaultCoordFile(const std::string& base_name = "phoenix_ipc");

  /**
   * Get default shared memory size
   */
  static constexpr std::size_t GetDefaultShmSize() {
    return 64 * 1024;  // 64KB default
  }

  /**
   * Get default queue capacity
   */
  static constexpr std::size_t GetDefaultQueueCapacity() {
    return 1024;  // 1024 messages
  }

  /**
   * Get default payload size
   */
  static constexpr std::size_t GetDefaultPayloadSize() {
    return 32 * 1024;  // 32KB
  }
};

}  // namespace coordination
}  // namespace phoenix

#endif  // PHOENIX_COORDINATION_COORDINATION_FILE_HPP_
