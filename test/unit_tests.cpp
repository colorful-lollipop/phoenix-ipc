#include <gtest/gtest.h>

#include <memory>
#include <thread>

#include "phoenix/core/spsc_ring_buffer.hpp"
#include "phoenix/layout/thread_monitor.hpp"
#include "phoenix/layout/worker_channel.hpp"
#include "phoenix/protocol/msg_header.hpp"

namespace phoenix {
namespace test {

// === SPSC Ring Buffer Tests ===

class SpscRingBufferTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Allocate aligned buffer for testing
    constexpr std::size_t kBufferSize = sizeof(TestMessage) * kCapacity;
    buffer_ = std::make_unique<uint8_t[]>(kBufferSize);
  }

  void TearDown() override {
    buffer_.reset();
  }

  struct TestMessage {
    uint64_t seq_id;
    uint32_t func_id;
    uint32_t payload_len;
    char payload[64];
  };

  static constexpr std::size_t kCapacity = 16;
  std::unique_ptr<uint8_t[]> buffer_;
};

TEST_F(SpscRingBufferTest, BasicPushPop) {
  core::SpscRingBuffer<TestMessage, kCapacity> queue(buffer_.get(), sizeof(TestMessage) * kCapacity);

  TestMessage msg{1, 100, 0, {}};
  EXPECT_TRUE(queue.Push(msg));
  EXPECT_FALSE(queue.Empty());
  EXPECT_EQ(queue.Size(), 1);

  TestMessage* peeked = queue.Peek();
  ASSERT_NE(peeked, nullptr);
  EXPECT_EQ(peeked->seq_id, 1);

  EXPECT_TRUE(queue.Pop());
  EXPECT_TRUE(queue.Empty());
  EXPECT_EQ(queue.Size(), 0);
}

TEST_F(SpscRingBufferTest, PushPopMultiple) {
  core::SpscRingBuffer<TestMessage, kCapacity> queue(buffer_.get(), sizeof(TestMessage) * kCapacity);

  // Fill the queue
  for (uint64_t i = 0; i < kCapacity; ++i) {
    TestMessage msg{i, static_cast<uint32_t>(i + 100), 0, {}};
    EXPECT_TRUE(queue.Push(msg));
  }

  EXPECT_TRUE(queue.Full());
  EXPECT_EQ(queue.Size(), kCapacity);

  // Try to push one more (should fail)
  TestMessage extra{100, 200, 0, {}};
  EXPECT_FALSE(queue.Push(extra));

  // Empty the queue
  for (uint64_t i = 0; i < kCapacity; ++i) {
    TestMessage* msg = queue.Peek();
    ASSERT_NE(msg, nullptr);
    EXPECT_EQ(msg->seq_id, i);
    EXPECT_TRUE(queue.Pop());
  }

  EXPECT_TRUE(queue.Empty());
}

TEST_F(SpscRingBufferTest, WrapAround) {
  core::SpscRingBuffer<TestMessage, kCapacity> queue(buffer_.get(), sizeof(TestMessage) * kCapacity);

  // Fill and empty to test wrap-around
  for (uint32_t round = 0; round < 4; ++round) {
    for (uint64_t i = 0; i < kCapacity; ++i) {
      TestMessage msg{round * kCapacity + i, 100 + round, 0, {}};
      EXPECT_TRUE(queue.Push(msg));
    }

    for (uint64_t i = 0; i < kCapacity; ++i) {
      TestMessage* msg = queue.Peek();
      ASSERT_NE(msg, nullptr);
      EXPECT_EQ(msg->seq_id, round * kCapacity + i);
      EXPECT_TRUE(queue.Pop());
    }
  }
}

TEST_F(SpscRingBufferTest, ForceAdvanceReadIndex) {
  core::SpscRingBuffer<TestMessage, kCapacity> queue(buffer_.get(), sizeof(TestMessage) * kCapacity);

  // Push some messages
  for (uint64_t i = 0; i < 4; ++i) {
    TestMessage msg{i, 100, 0, {}};
    EXPECT_TRUE(queue.Push(msg));
  }

  // Force advance by 2 (simulating crash recovery)
  queue.ForceAdvanceReadIndex(2);

  // Should have 2 messages left
  EXPECT_EQ(queue.Size(), 2);

  TestMessage* msg = queue.Peek();
  ASSERT_NE(msg, nullptr);
  EXPECT_EQ(msg->seq_id, 2);
}

// === ThreadMonitor Tests ===

class ThreadMonitorTest : public ::testing::Test {
 protected:
  layout::ThreadMonitor monitor;
};

TEST_F(ThreadMonitorTest, InitialState) {
  EXPECT_FALSE(monitor.IsProcessing());
  EXPECT_EQ(monitor.GetProcessingSeq(), 0);
  EXPECT_EQ(monitor.GetGroupId(), 0);
  EXPECT_EQ(monitor.GetWorkerIndex(), 0);
}

TEST_F(ThreadMonitorTest, BeginEndTransaction) {
  monitor.BeginTransaction(12345, 1, 2);

  EXPECT_TRUE(monitor.IsProcessing());
  EXPECT_EQ(monitor.GetProcessingSeq(), 12345);
  EXPECT_EQ(monitor.GetGroupId(), 1);
  EXPECT_EQ(monitor.GetWorkerIndex(), 2);
  EXPECT_NE(monitor.GetStartTimestamp(), 0);

  monitor.EndTransaction();

  EXPECT_FALSE(monitor.IsProcessing());
}

TEST_F(ThreadMonitorTest, Reset) {
  monitor.BeginTransaction(999, 1, 1);
  monitor.Reset();

  EXPECT_FALSE(monitor.IsProcessing());
  EXPECT_EQ(monitor.GetProcessingSeq(), 0);
}

// === Main ===

}  // namespace test
}  // namespace phoenix

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
