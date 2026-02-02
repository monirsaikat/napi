#pragma once

#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <atomic>
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

 private:
  static CGEventRef EventCallback(CGEventTapProxy proxy,
                                   CGEventType type,
                                   CGEventRef event,
                                   void* userInfo);
  void RunLoopThread();

  std::atomic<bool> running_{false};
  std::thread runLoopThread_;
  CFMachPortRef eventTap_{nullptr};
  CFRunLoopRef runLoop_{nullptr};
};

} // namespace mac
} // namespace platform
} // namespace inputhook
