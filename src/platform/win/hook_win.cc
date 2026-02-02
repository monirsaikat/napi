#include "hook_win.h"

#include <chrono>

namespace inputhook {
namespace platform {
namespace win {

WinPlatformHook* WinPlatformHook::instance_ = nullptr;

namespace {

InputModifiers CurrentModifiers() {
  InputModifiers mods;
  mods.shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
  mods.ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
  mods.alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
  mods.meta = (GetAsyncKeyState(VK_LWIN) & 0x8000) != 0 || (GetAsyncKeyState(VK_RWIN) & 0x8000) != 0;
  return mods;
}

double CurrentTimeMs() {
  using namespace std::chrono;
  return static_cast<double>(duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
}

} // namespace

WinPlatformHook::WinPlatformHook(EventCallback callback)
    : PlatformHook(std::move(callback)) {}

WinPlatformHook::~WinPlatformHook() {
  Stop();
}

void WinPlatformHook::ThreadLoop() {
  threadId_ = GetCurrentThreadId();
  HINSTANCE module = GetModuleHandle(nullptr);
  keyboardHook_ = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardProc, module, 0);
  mouseHook_ = SetWindowsHookEx(WH_MOUSE_LL, MouseProc, module, 0);

  MSG message;
  while (running_ && GetMessage(&message, nullptr, 0, 0) > 0) {
    TranslateMessage(&message);
    DispatchMessage(&message);
  }

  if (keyboardHook_) {
    UnhookWindowsHookEx(keyboardHook_);
    keyboardHook_ = nullptr;
  }
  if (mouseHook_) {
    UnhookWindowsHookEx(mouseHook_);
    mouseHook_ = nullptr;
  }
}

bool WinPlatformHook::Start() {
  if (running_) {
    return false;
  }
  running_ = true;
  instance_ = this;
  workerThread_ = std::thread(&WinPlatformHook::ThreadLoop, this);
  return true;
}

void WinPlatformHook::Stop() {
  if (!running_) {
    return;
  }
  running_ = false;
  if (threadId_) {
    PostThreadMessage(threadId_, WM_QUIT, 0, 0);
  }
  if (workerThread_.joinable()) {
    workerThread_.join();
  }
  instance_ = nullptr;
  threadId_ = 0;
}

LRESULT CALLBACK WinPlatformHook::KeyboardProc(int code, WPARAM wParam, LPARAM lParam) {
  if (code == HC_ACTION && instance_) {
    auto data = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
    InputEvent event;
    event.time = CurrentTimeMs();
    event.modifiers = CurrentModifiers();
    switch (wParam) {
      case WM_KEYDOWN:
      case WM_SYSKEYDOWN:
        event.type = "keydown";
        break;
      case WM_KEYUP:
      case WM_SYSKEYUP:
        event.type = "keyup";
        break;
      default:
        break;
    }
    if (event.type.empty()) {
      return CallNextHookEx(nullptr, code, wParam, lParam);
    }
    event.keycode = data->vkCode;
    event.scancode = data->scanCode;
    instance_->Dispatch(std::move(event));
  }
  return CallNextHookEx(nullptr, code, wParam, lParam);
}

LRESULT CALLBACK WinPlatformHook::MouseProc(int code, WPARAM wParam, LPARAM lParam) {
  if (code == HC_ACTION && instance_) {
    auto data = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
    InputEvent event;
    event.time = CurrentTimeMs();
    event.modifiers = CurrentModifiers();
    event.x = static_cast<int32_t>(data->pt.x);
    event.y = static_cast<int32_t>(data->pt.y);

    switch (wParam) {
      case WM_MOUSEMOVE:
        event.type = "mousemove";
        break;
      case WM_LBUTTONDOWN:
        event.type = "mousedown";
        event.button = 0;
        break;
      case WM_LBUTTONUP:
        event.type = "mouseup";
        event.button = 0;
        break;
      case WM_RBUTTONDOWN:
        event.type = "mousedown";
        event.button = 1;
        break;
      case WM_RBUTTONUP:
        event.type = "mouseup";
        event.button = 1;
        break;
      case WM_MBUTTONDOWN:
        event.type = "mousedown";
        event.button = 2;
        break;
      case WM_MBUTTONUP:
        event.type = "mouseup";
        event.button = 2;
        break;
      case WM_MOUSEWHEEL:
        event.type = "wheel";
        event.deltaY = static_cast<int32_t>(GET_WHEEL_DELTA_WPARAM(data->mouseData));
        break;
      case WM_MOUSEHWHEEL:
        event.type = "wheel";
        event.deltaX = static_cast<int32_t>(GET_WHEEL_DELTA_WPARAM(data->mouseData));
        break;
      default:
        break;
    }
    if (event.type.empty()) {
      return CallNextHookEx(nullptr, code, wParam, lParam);
    }

    instance_->Dispatch(std::move(event));
  }
  return CallNextHookEx(nullptr, code, wParam, lParam);
}

} // namespace win
} // namespace platform
} // namespace inputhook
