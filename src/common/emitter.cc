#include <string>
#include <utility>

#include "emitter.h"

#if defined(_WIN32)
#include "../platform/win/hook_win.h"
#elif defined(__APPLE__)
#include "../platform/mac/hook_mac.h"
#elif defined(__linux__)
#include "../platform/linux/hook_x11.h"
#endif

namespace inputhook {

PlatformHook::PlatformHook(EventCallback callback)
    : callback_(std::move(callback)) {}

PlatformHook::~PlatformHook() = default;

std::string PlatformHook::GetFailureReason() const {
  return {};
}

void PlatformHook::Dispatch(InputEvent event) {
  if (callback_) {
    callback_(std::move(event));
  }
}

InputEmitter::InputEmitter(EventCallback callback) {
#if defined(_WIN32)
  platformHook_ = std::make_unique<platform::win::WinPlatformHook>(std::move(callback));
#elif defined(__APPLE__)
  platformHook_ = std::make_unique<platform::mac::MacPlatformHook>(std::move(callback));
#elif defined(__linux__)
  platformHook_ = std::make_unique<platform::linux::LinuxPlatformHook>(std::move(callback));
#else
  (void)callback;
#endif
}

InputEmitter::~InputEmitter() {
  Stop();
}

bool InputEmitter::Start() {
  return platformHook_ ? platformHook_->Start() : false;
}

void InputEmitter::Stop() {
  if (platformHook_) {
    platformHook_->Stop();
  }
}

std::string InputEmitter::GetFailureReason() const {
  return platformHook_ ? platformHook_->GetFailureReason() : std::string();
}

} // namespace inputhook
