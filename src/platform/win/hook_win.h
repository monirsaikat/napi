#pragma once

#include <atomic>
#include <thread>
#include <windows.h>

#include "../../common/emitter.h"

namespace inputhook {
namespace platform {
namespace win {

class WinPlatformHook : public PlatformHook {
 public:
  explicit WinPlatformHook(EventCallback callback);
  ~WinPlatformHook() override;

  bool Start() override;
  void Stop() override;

 private:
  static LRESULT CALLBACK KeyboardProc(int code, WPARAM wParam, LPARAM lParam);
  static LRESULT CALLBACK MouseProc(int code, WPARAM wParam, LPARAM lParam);
  void ThreadLoop();

  static WinPlatformHook* instance_;

  std::thread workerThread_;
  std::atomic<bool> running_{false};
  HHOOK keyboardHook_{nullptr};
  HHOOK mouseHook_{nullptr};
  DWORD threadId_{0};
};

} // namespace win
} // namespace platform
} // namespace inputhook
