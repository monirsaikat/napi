#pragma once

#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <atomic>
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

 private:
  static CGEventRef EventCallback(CGEventTapProxy proxy,
                                   CGEventType type,
                                   CGEventRef event,
                                   void* userInfo);
  void RunLoopThread();

  std::atomic<bool> running_{false};
  std::thread runLoopThread_;
  CFMachPortRef eventTap_{nullptr};
  CFRunLoopSourceRef runLoopSource_{nullptr};
  CFRunLoopRef runLoop_{nullptr};
  std::mutex startPromiseMutex_;
  std::shared_ptr<std::promise<bool>> startPromise_;
  std::string failureReason_;
  mutable std::mutex failureMutex_;

  void NotifyStartResult(bool success);
  bool EnsurePermissions();
  bool CreateEventTap(CGEventTapLocation location, CGEventMask mask);
  void SetFailureReason(std::string reason);
};

} // namespace mac
} // namespace platform
} // namespace inputhook
