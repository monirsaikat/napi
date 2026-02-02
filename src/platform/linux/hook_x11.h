#pragma once

#include <X11/Xlib.h>
#include <X11/extensions/XInput2.h>

#include <atomic>
#include <thread>

#include "../../common/emitter.h"

namespace inputhook {
namespace platform {
namespace linux {

class LinuxPlatformHook : public PlatformHook {
 public:
  explicit LinuxPlatformHook(EventCallback callback);
  ~LinuxPlatformHook() override;

  bool Start() override;
  void Stop() override;

 private:
  void ThreadLoop();
  void ProcessEvent(XIDeviceEvent* event, InputEvent& inputEvent);

  std::atomic<bool> running_{false};
  std::thread workerThread_;
  Display* display_{nullptr};
  int xiOpcode_{0};
};

} // namespace linux
} // namespace platform
} // namespace inputhook
