#ifndef PHOENIX_PROTOCOL_MSG_HEADER_HPP_
#define PHOENIX_PROTOCOL_MSG_HEADER_HPP_

#include <cstdint>

namespace phoenix {
namespace protocol {

/**
 * MsgHeader - Core message header for PhoenixIPC protocol
 *
 * This header precedes every message sent through the IPC channel.
 * It contains metadata needed for routing, sequencing, and payload handling.
 *
 * Layout:
 * - 8 bytes: seq_id (global sequence number)
 * - 4 bytes: func_id (RPC function identifier)
 * - 4 bytes: payload_len (payload length in bytes)
 * - Total: 16 bytes (aligned)
 */
struct MsgHeader {
  uint64_t seq_id;     // Global unique sequence ID (monotonically increasing)
  uint32_t func_id;    // RPC function ID for routing
  uint32_t payload_len; // Payload length in bytes (0 if no payload)

  // Constants
  static constexpr std::size_t kSize = 16;
  static constexpr uint64_t kInvalidSeqId = 0;

  // Validation
  bool IsValid() const {
    return seq_id != kInvalidSeqId;
  }

  // Get total message size including payload
  constexpr std::size_t TotalSize() const {
    return kSize + payload_len;
  }
};

/**
 * Message priority levels
 */
enum class Priority : uint8_t {
  kLow = 0,    // Normal priority (Group A)
  kHigh = 1,   // VIP priority (Group B)
};

/**
 * Message status for tracking
 */
enum class MsgStatus : uint8_t {
  kPending = 0,    // Waiting to be processed
  kProcessing = 1, // Currently being processed
  kCompleted = 2,  // Successfully processed
  kFailed = 3,     // Processing failed
};

/**
 * Worker group types
 */
enum class WorkerGroup : uint8_t {
  kNormal = 0,  // Normal workers (Group A)
  kVip = 1,     // VIP workers (Group B)
};

/**
 * Control commands for IPC management
 */
enum class ControlCommand : uint32_t {
  kNone = 0,
  kPing = 1,
  kPong = 2,
  kShutdown = 3,
  kRestart = 4,
  kStats = 5,
};

}  // namespace protocol
}  // namespace phoenix

#endif  // PHOENIX_PROTOCOL_MSG_HEADER_HPP_
