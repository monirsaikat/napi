#include "hook_x11.h"

#include <X11/Xlib.h>
#include <X11/extensions/XInput2.h>
#include <X11/XKBlib.h>

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

InputModifiers BuildModifiersFromState(const XIModifierState& state) {
  InputModifiers modifiers;
  modifiers.shift = (state.effective & ShiftMask) != 0;
  modifiers.ctrl = (state.effective & ControlMask) != 0;
  modifiers.alt = (state.effective & Mod1Mask) != 0;
  modifiers.meta = (state.effective & Mod4Mask) != 0;
  return modifiers;
}

InputModifiers QueryKeyboardModifiers(Display* display) {
  InputModifiers modifiers;
  if (!display) {
    return modifiers;
  }

  XkbStateRec state{};
  if (XkbGetState(display, XkbUseCoreKbd, &state) == Success) {
    modifiers.shift = (state.mods & ShiftMask) != 0;
    modifiers.ctrl = (state.mods & ControlMask) != 0;
    modifiers.alt = (state.mods & Mod1Mask) != 0;
    modifiers.meta = (state.mods & Mod4Mask) != 0;
  }
  return modifiers;
}

bool IsValuatorMaskSet(const XIValuatorState& state, int axis) {
  if (!state.mask || axis < 0) {
    return false;
  }
  int byteIndex = axis / 8;
  if (byteIndex >= state.mask_len) {
    return false;
  }
  unsigned char maskByte = state.mask[byteIndex];
  return (maskByte & (1 << (axis % 8))) != 0;
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
  XISetMask(maskBytes, XI_RawKeyPress);
  XISetMask(maskBytes, XI_RawKeyRelease);
  XISetMask(maskBytes, XI_ButtonPress);
  XISetMask(maskBytes, XI_ButtonRelease);
  XISetMask(maskBytes, XI_Motion);
  XISetMask(maskBytes, XI_RawMotion);
  mask.deviceid = XIAllMasterDevices;
  mask.mask_len = sizeof(maskBytes);
  mask.mask = maskBytes;

  XISelectEvents(display_, root, &mask, 1);

  rawKeyboardSeen_.store(false, std::memory_order_release);

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

    InputEvent inputEvent;
    inputEvent.time = CurrentTimeMs();

    InputModifiers modifiers;
    bool shouldDispatch = false;
    int evtype = event.xcookie.evtype;

    switch (evtype) {
      case XI_RawKeyPress:
      case XI_RawKeyRelease: {
        modifiers = QueryKeyboardModifiers(display_);
        ProcessRawKeyEvent(reinterpret_cast<XIRawEvent*>(event.xcookie.data),
                           inputEvent,
                           evtype);
        shouldDispatch = !inputEvent.type.empty();
        if (shouldDispatch) {
          rawKeyboardSeen_.store(true, std::memory_order_release);
        }
        break;
      }
      case XI_RawMotion: {
        modifiers = QueryKeyboardModifiers(display_);
        ProcessRawMotionEvent(reinterpret_cast<XIRawEvent*>(event.xcookie.data),
                              inputEvent);
        shouldDispatch = !inputEvent.type.empty();
        break;
      }
      default: {
        auto* devEvent = reinterpret_cast<XIDeviceEvent*>(event.xcookie.data);
        modifiers = BuildModifiersFromState(devEvent->mods);
        ProcessDeviceEvent(devEvent, inputEvent);
        shouldDispatch = !inputEvent.type.empty();
        break;
      }
    }

    inputEvent.modifiers = modifiers;
    if (shouldDispatch) {
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

void LinuxPlatformHook::ProcessDeviceEvent(XIDeviceEvent* event, InputEvent& inputEvent) {
  if (!event) {
    inputEvent.type.clear();
    return;
  }

  switch (event->evtype) {
    case XI_KeyPress:
      if (rawKeyboardSeen_.load(std::memory_order_acquire)) {
        inputEvent.type.clear();
        return;
      }
      inputEvent.type = "keydown";
      inputEvent.keycode = event->detail;
      inputEvent.scancode = event->detail;
      break;
    case XI_KeyRelease:
      if (rawKeyboardSeen_.load(std::memory_order_acquire)) {
        inputEvent.type.clear();
        return;
      }
      inputEvent.type = "keyup";
      inputEvent.keycode = event->detail;
      inputEvent.scancode = event->detail;
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
      inputEvent.type = "mousemove";
      inputEvent.x = static_cast<int32_t>(event->event_x);
      inputEvent.y = static_cast<int32_t>(event->event_y);
      break;
    default:
      inputEvent.type.clear();
      return;
  }
}

void LinuxPlatformHook::ProcessRawKeyEvent(XIRawEvent* event,
                                           InputEvent& inputEvent,
                                           int evtype) {
  if (!event) {
    return;
  }
  inputEvent.keycode = event->detail;
  inputEvent.scancode = event->detail;
  inputEvent.type = (evtype == XI_RawKeyPress) ? "keydown" : "keyup";
}

void LinuxPlatformHook::ProcessRawMotionEvent(XIRawEvent* event,
                                              InputEvent& inputEvent) {
  if (!event || event->valuators.mask_len == 0 || !event->raw_values) {
    inputEvent.type.clear();
    return;
  }

  int axisCount = event->valuators.mask_len * 8;
  if (axisCount <= 0) {
    inputEvent.type.clear();
    return;
  }

  double deltaX = 0.0;
  double deltaY = 0.0;
  bool hasDeltaX = false;
  bool hasDeltaY = false;
  int valueIndex = 0;

  for (int axis = 0; axis < axisCount; ++axis) {
    if (!IsValuatorMaskSet(event->valuators, axis)) {
      continue;
    }
    double value = event->raw_values[valueIndex++];
    if (axis == 0) {
      deltaX = value;
      hasDeltaX = true;
    } else if (axis == 1) {
      deltaY = value;
      hasDeltaY = true;
    }
    if (hasDeltaX && hasDeltaY) {
      break;
    }
  }

  if (!hasDeltaX && !hasDeltaY) {
    inputEvent.type.clear();
    return;
  }

  inputEvent.type = "mousemove";
  if (hasDeltaX) {
    inputEvent.deltaX = static_cast<int32_t>(deltaX);
  }
  if (hasDeltaY) {
    inputEvent.deltaY = static_cast<int32_t>(deltaY);
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
