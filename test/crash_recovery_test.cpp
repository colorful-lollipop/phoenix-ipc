/**
 * Crash Recovery Test for PhoenixIPC
 *
 * This test verifies the crash recovery mechanism by simulating
 * worker crashes and ensuring the supervisor can recover.
 */

#include <gtest/gtest.h>

#include <cstring>
#include <sys/wait.h>
#include <unistd.h>

#include "phoenix/layout/phoenix_shm_layout.hpp"
#include "phoenix/protocol/msg_header.hpp"

namespace phoenix {
namespace test {

class CrashRecoveryTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Allocate shared memory for testing
    shm_size_ = phoenix::layout::PhoenixShmLayout::RequiredSize() * 2;
    shm_addr_ = ::aligned_alloc(64, shm_size_);
    ASSERT_NE(shm_addr_, nullptr);

    // Initialize layout
    layout_ = new (shm_addr_) phoenix::layout::PhoenixShmLayout();
    ASSERT_TRUE(layout_->Initialize(shm_size_));
  }

  void TearDown() override {
    if (layout_) {
      layout_->~PhoenixShmLayout();
      layout_ = nullptr;
    }
    std::free(shm_addr_);
  }

  std::size_t shm_size_;
  void* shm_addr_ = nullptr;
  phoenix::layout::PhoenixShmLayout* layout_ = nullptr;
};

TEST_F(CrashRecoveryTest, LayoutInitialization) {
  EXPECT_TRUE(layout_->IsValid());
  EXPECT_EQ(layout_->magic_number, phoenix::layout::PhoenixShmLayout::kMagicNumber);
  EXPECT_EQ(layout_->version, phoenix::layout::PhoenixShmLayout::kVersion);
}

TEST_F(CrashRecoveryTest, NormalWorkerSelection) {
  // All workers should be idle initially
  EXPECT_EQ(layout_->ScanCrashedWorkers(), 0);

  // Find best worker should return index 0 initially
  EXPECT_EQ(layout_->FindBestNormalWorker(), 0);
  EXPECT_EQ(layout_->FindBestVipWorker(), 0);
}

TEST_F(CrashRecoveryTest, PoisonPillSkip) {
  // Simulate a crashed worker with a poison pill
  auto& normal_channel = layout_->GetNormalChannel(0);
  auto& normal_monitor = layout_->GetNormalMonitor(0);

  // Push a message to the queue
  protocol::MsgHeader msg{100, 1, 64};
  EXPECT_TRUE(normal_channel.request_queue.Push(msg));

  // Simulate worker crash (mark as processing but don't pop)
  normal_monitor.BeginTransaction(100, 0, 0);

  // Verify crash is detected
  EXPECT_EQ(layout_->ScanCrashedWorkers(), 1);

  // Recover from crash
  EXPECT_EQ(layout_->RecoverCrashedWorkers(), 1);

  // Verify worker is back to idle
  EXPECT_FALSE(normal_monitor.IsProcessing());
  EXPECT_EQ(layout_->ScanCrashedWorkers(), 0);

  // Verify message was skipped (queue should be empty)
  EXPECT_TRUE(normal_channel.request_queue.Empty());
}

TEST_F(CrashRecoveryTest, MultipleCrashRecovery) {
  // Simulate crashes in multiple workers
  auto& normal_channel_0 = layout_->GetNormalChannel(0);
  auto& normal_monitor_0 = layout_->GetNormalMonitor(0);
  auto& vip_channel_1 = layout_->GetVipChannel(1);
  auto& vip_monitor_1 = layout_->GetVipMonitor(1);

  // Push messages to both channels
  protocol::MsgHeader msg1{100, 1, 64};
  protocol::MsgHeader msg2{101, 2, 64};
  EXPECT_TRUE(normal_channel_0.request_queue.Push(msg1));
  EXPECT_TRUE(vip_channel_1.request_queue.Push(msg2));

  // Simulate crashes
  normal_monitor_0.BeginTransaction(100, 0, 0);
  vip_monitor_1.BeginTransaction(101, 1, 1);

  // Detect crashes
  EXPECT_EQ(layout_->ScanCrashedWorkers(), 2);

  // Recover
  EXPECT_EQ(layout_->RecoverCrashedWorkers(), 2);

  // Verify all recovered
  EXPECT_FALSE(normal_monitor_0.IsProcessing());
  EXPECT_FALSE(vip_monitor_1.IsProcessing());
  EXPECT_TRUE(normal_channel_0.request_queue.Empty());
  EXPECT_TRUE(vip_channel_1.request_queue.Empty());
}

// === Main ===

}  // namespace test
}  // namespace phoenix

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
