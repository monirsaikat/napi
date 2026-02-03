#pragma once

#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <atomic>
#include <cstdint>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "../../common/emitter.h"

namespace inputhook {
namespace platform {
namespace mac {

class MacPlatformHook : public PlatformHook {
 public:
  explicit MacPlatformHook(PlatformHook::EventCallback callback);
  ~MacPlatformHook() override;

  bool Start() override;
  void Stop() override;
  std::string GetFailureReason() const override;
  std::string GetLastError() const override;

 private:
  static CGEventRef EventCallback(CGEventTapProxy proxy,
                                   CGEventType type,
                                   CGEventRef event,
                                   void* userInfo);
  void RunLoopThread();
  bool EnsurePermissions();
  bool CreateEventTap(CGEventTapLocation location, CGEventMask mask);
  bool CreateEventTapSequence(CGEventMask mask);
  void TeardownEventTap();
  bool RecreateEventTap(const char* reason);
  void SetFailureReason(std::string reason);
  void SetLastError(std::string reason);

  std::atomic<bool> running_{false};
  std::thread runLoopThread_;
  CFMachPortRef eventTap_{nullptr};
  CFRunLoopSourceRef runLoopSource_{nullptr};
  CFRunLoopRef runLoop_{nullptr};
  std::mutex startPromiseMutex_;
  std::shared_ptr<std::promise<bool>> startPromise_;
  std::string failureReason_;
  mutable std::mutex failureMutex_;
  std::string lastError_;
  mutable std::mutex lastErrorMutex_;
  std::atomic<int64_t> lastEventMs_{0};
  std::atomic<int64_t> lastRecreateMs_{0};
  std::atomic<bool> eventSeen_{false};
  std::string processPath_;

  void NotifyStartResult(bool success);
};

} // namespace mac
} // namespace platform
} // namespace inputhook
