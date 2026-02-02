#include "hook_x11.h"

#include <X11/Xlib.h>
#include <X11/extensions/XInput2.h>

#include <chrono>
#include <cstring>
#include <thread>
#include <sys/time.h>

namespace inputhook {
namespace platform {
namespace linux {

namespace {
int QueryXiOpcode(Display* display) {
  int opcode = 0;
  int event = 0;
  int error = 0;
  if (!XQueryExtension(display, "XInputExtension", &opcode, &event, &error)) {
    return -1;
  }
  if (XIQueryVersion(display, &event, &error) != Success) {
    return -1;
  }
  return opcode;
}

double CurrentTimeMs() {
  struct timeval now;
  gettimeofday(&now, nullptr);
  return now.tv_sec * 1000.0 + now.tv_usec / 1000.0;
}

} // namespace

LinuxPlatformHook::LinuxPlatformHook(EventCallback callback)
    : PlatformHook(std::move(callback)) {}

LinuxPlatformHook::~LinuxPlatformHook() {
  Stop();
}

void LinuxPlatformHook::ThreadLoop() {
  display_ = XOpenDisplay(nullptr);
  if (!display_) {
    running_ = false;
    return;
  }

  xiOpcode_ = QueryXiOpcode(display_);
  if (xiOpcode_ < 0) {
    running_ = false;
    XCloseDisplay(display_);
    display_ = nullptr;
    return;
  }

  Window root = DefaultRootWindow(display_);
  XIEventMask mask;
  unsigned char maskBytes[XIMaskLen(XI_LASTEVENT)];
  memset(maskBytes, 0, sizeof(maskBytes));
  XISetMask(maskBytes, XI_KeyPress);
  XISetMask(maskBytes, XI_KeyRelease);
  XISetMask(maskBytes, XI_ButtonPress);
  XISetMask(maskBytes, XI_ButtonRelease);
  XISetMask(maskBytes, XI_Motion);
  XISetMask(maskBytes, XI_RawMotion);
  mask.deviceid = XIAllMasterDevices;
  mask.mask_len = sizeof(maskBytes);
  mask.mask = maskBytes;

  XISelectEvents(display_, root, &mask, 1);

  XEvent event;
  while (running_) {
    if (XPending(display_) == 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }

    XNextEvent(display_, &event);
    if (!running_) {
      break;
    }

    if (event.type != GenericEvent ||
        event.xgeneric.extension != xiOpcode_) {
      continue;
    }

    if (!XGetEventData(display_, &event.xcookie)) {
      continue;
    }

    auto* devEvent = reinterpret_cast<XIDeviceEvent*>(event.xcookie.data);
    InputEvent inputEvent;
    inputEvent.time = CurrentTimeMs();
    inputEvent.modifiers.shift =
        (devEvent->mods.effective & ShiftMask) != 0;
    inputEvent.modifiers.ctrl =
        (devEvent->mods.effective & ControlMask) != 0;
    inputEvent.modifiers.alt =
        (devEvent->mods.effective & Mod1Mask) != 0;
    inputEvent.modifiers.meta =
        (devEvent->mods.effective & Mod4Mask) != 0;

    ProcessEvent(devEvent, inputEvent);
    if (!inputEvent.type.empty()) {
      Dispatch(std::move(inputEvent));
    }

    XFreeEventData(display_, &event.xcookie);
  }

  if (display_) {
    XCloseDisplay(display_);
    display_ = nullptr;
    xiOpcode_ = 0;
  }
}

void LinuxPlatformHook::ProcessEvent(XIDeviceEvent* event, InputEvent& inputEvent) {
  switch (event->evtype) {
    case XI_KeyPress:
      inputEvent.type = "keydown";
      inputEvent.keycode = event->detail;
      break;
    case XI_KeyRelease:
      inputEvent.type = "keyup";
      inputEvent.keycode = event->detail;
      break;
    case XI_ButtonPress:
      inputEvent.type = "mousedown";
      inputEvent.button = event->detail;
      break;
    case XI_ButtonRelease:
      inputEvent.type = "mouseup";
      inputEvent.button = event->detail;
      break;
    case XI_Motion:
    case XI_RawMotion:
      inputEvent.type = "mousemove";
      inputEvent.x = static_cast<int32_t>(event->event_x);
      inputEvent.y = static_cast<int32_t>(event->event_y);
      break;
    default:
      inputEvent.type.clear();
      return;
  }

}

bool LinuxPlatformHook::Start() {
  if (running_) {
    return false;
  }
  running_ = true;
  workerThread_ = std::thread(&LinuxPlatformHook::ThreadLoop, this);
  return true;
}

void LinuxPlatformHook::Stop() {
  if (!running_) {
    return;
  }
  running_ = false;
  if (workerThread_.joinable()) {
    workerThread_.join();
  }
}

} // namespace linux
} // namespace platform
} // namespace inputhook
