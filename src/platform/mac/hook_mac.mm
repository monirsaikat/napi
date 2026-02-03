#import "hook_mac.h"

#import <ApplicationServices/ApplicationServices.h>
#import <Foundation/Foundation.h>

#include <chrono>
#include <optional>

namespace inputhook {
namespace platform {
namespace mac {

namespace {
double CurrentTimeMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

InputModifiers CurrentModifiers() {
  InputModifiers mods;
  CGEventFlags flags = CGEventSourceFlagsState(kCGEventSourceStateCombinedSessionState);
  mods.shift = (flags & kCGEventFlagMaskShift) != 0;
  mods.ctrl = (flags & kCGEventFlagMaskControl) != 0;
  mods.alt = (flags & kCGEventFlagMaskAlternate) != 0;
  mods.meta = (flags & kCGEventFlagMaskCommand) != 0;
  return mods;
}

std::optional<InputEvent> BuildEvent(CGEventType type, CGEventRef eventRef) {
  InputEvent event;
  event.time = CurrentTimeMs();
  event.modifiers = CurrentModifiers();
  CGPoint location = CGEventGetLocation(eventRef);
  event.x = static_cast<int32_t>(location.x);
  event.y = static_cast<int32_t>(location.y);

  switch (type) {
    case kCGEventKeyDown:
    case kCGEventFlagsChanged:
      event.type = "keydown";
      event.keycode = static_cast<uint32_t>(
          CGEventGetIntegerValueField(eventRef, kCGKeyboardEventKeycode));
      break;
    case kCGEventKeyUp:
      event.type = "keyup";
      event.keycode = static_cast<uint32_t>(
          CGEventGetIntegerValueField(eventRef, kCGKeyboardEventKeycode));
      break;
    case kCGEventMouseMoved:
    case kCGEventLeftMouseDragged:
    case kCGEventRightMouseDragged:
    case kCGEventOtherMouseDragged:
      event.type = "mousemove";
      break;
    case kCGEventLeftMouseDown:
    case kCGEventRightMouseDown:
    case kCGEventOtherMouseDown:
      event.type = "mousedown";
      event.button = static_cast<uint32_t>(
          CGEventGetIntegerValueField(eventRef, kCGMouseEventButtonNumber));
      break;
    case kCGEventLeftMouseUp:
    case kCGEventRightMouseUp:
    case kCGEventOtherMouseUp:
      event.type = "mouseup";
      event.button = static_cast<uint32_t>(
          CGEventGetIntegerValueField(eventRef, kCGMouseEventButtonNumber));
      break;
    case kCGEventScrollWheel:
      event.type = "wheel";
      event.deltaX = static_cast<int32_t>(
          CGEventGetIntegerValueField(eventRef, kCGScrollWheelEventDeltaAxis2));
      event.deltaY = static_cast<int32_t>(
          CGEventGetIntegerValueField(eventRef, kCGScrollWheelEventDeltaAxis1));
      break;
    default:
      return std::nullopt;
  }

  return event;
}

std::string BuildProcessPath() {
  NSURL* executableURL = [[NSProcessInfo processInfo] executableURL];
  if (executableURL) {
    const char* path = executableURL.fileSystemRepresentation;
    if (path) {
      return std::string(path);
    }
  }
  return "this process";
}

CGEventMask BuildEventMask() {
  return CGEventMaskBit(kCGEventKeyDown) |
         CGEventMaskBit(kCGEventKeyUp) |
         CGEventMaskBit(kCGEventMouseMoved) |
         CGEventMaskBit(kCGEventLeftMouseDown) |
         CGEventMaskBit(kCGEventLeftMouseUp) |
         CGEventMaskBit(kCGEventRightMouseDown) |
         CGEventMaskBit(kCGEventRightMouseUp) |
         CGEventMaskBit(kCGEventOtherMouseDown) |
         CGEventMaskBit(kCGEventOtherMouseUp) |
         CGEventMaskBit(kCGEventScrollWheel);
}

}  // namespace

MacPlatformHook::MacPlatformHook(PlatformHook::EventCallback callback)
    : PlatformHook(std::move(callback)) {}

MacPlatformHook::~MacPlatformHook() {
  Stop();
}

void MacPlatformHook::NotifyStartResult(bool success) {
  std::shared_ptr<std::promise<bool>> promise;
  {
    std::lock_guard<std::mutex> lock(startPromiseMutex_);
    promise = std::move(startPromise_);
  }
  if (promise) {
    promise->set_value(success);
  }
}

CGEventRef MacPlatformHook::EventCallback(CGEventTapProxy proxy,
                                           CGEventType type,
                                           CGEventRef event,
                                           void* userInfo) {
  auto* self = reinterpret_cast<MacPlatformHook*>(userInfo);
  if (!self || !event) {
    return event;
  }

  if (type == kCGEventTapDisabledByTimeout ||
      type == kCGEventTapDisabledByUserInput) {
    if (self->eventTap_) {
      CGEventTapEnable(self->eventTap_, true);
    }
    return event;
  }

  auto builtEvent = BuildEvent(type, event);
  if (builtEvent) {
    self->Dispatch(std::move(*builtEvent));
  }
  return event;
}

bool MacPlatformHook::CreateEventTap(CGEventTapLocation location,
                                     CGEventMask mask) {
  if (eventTap_) {
    return true;
  }

  CFMachPortContext context = {};
  context.info = this;
  CFMachPortRef tap = CGEventTapCreate(location,
                                       kCGHeadInsertEventTap,
                                       kCGEventTapOptionListenOnly,
                                       mask,
                                       EventCallback,
                                       &context);
  if (!tap) {
    return false;
  }

  eventTap_ = tap;
  runLoopSource_ = CFMachPortCreateRunLoopSource(nullptr, eventTap_, 0);
  if (!runLoopSource_) {
    CFMachPortInvalidate(eventTap_);
    CFRelease(eventTap_);
    eventTap_ = nullptr;
    return false;
  }

  return true;
}

bool MacPlatformHook::EnsurePermissions() {
  if (@available(macOS 10.15, *)) {
    if (!CGPreflightListenEventAccess()) {
      CGRequestListenEventAccess();
      SetFailureReason("Input Monitoring permission required for " +
                       BuildProcessPath() +
                       ". Enable it in System Settings > Privacy & Security > "
                       "Input Monitoring and restart the binary.");
      return false;
    }
    return true;
  }

  const void* keys[] = { kAXTrustedCheckOptionPrompt };
  const void* values[] = { kCFBooleanTrue };
  CFDictionaryRef options = CFDictionaryCreate(kCFAllocatorDefault,
                                               keys,
                                               values,
                                               1,
                                               &kCFTypeDictionaryKeyCallBacks,
                                               &kCFTypeDictionaryValueCallBacks);
  bool trusted = AXIsProcessTrustedWithOptions(options);
  if (options) {
    CFRelease(options);
  }
  if (!trusted) {
    SetFailureReason("Accessibility permission required for " +
                     BuildProcessPath() +
                     ". Grant it under Privacy & Security > Accessibility.");
    return false;
  }

  return true;
}

void MacPlatformHook::RunLoopThread() {
  CGEventMask mask = BuildEventMask();

  if (!CreateEventTap(kCGSessionEventTap, mask) &&
      !CreateEventTap(kCGHIDEventTap, mask)) {
    SetFailureReason("Unable to create a CGEventTap; ensure Input Monitoring "
                     "or Accessibility permissions are granted for " +
                     BuildProcessPath() + ".");
    running_ = false;
    NotifyStartResult(false);
    return;
  }

  SetFailureReason("");
  NotifyStartResult(true);

  runLoop_ = CFRunLoopGetCurrent();
  if (runLoopSource_) {
    CFRunLoopAddSource(runLoop_, runLoopSource_, kCFRunLoopCommonModes);
  }
  if (eventTap_) {
    CGEventTapEnable(eventTap_, true);
  }

  while (running_) {
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.1, true);
  }

  runLoop_ = nullptr;
  if (runLoopSource_) {
    CFRunLoopRemoveSource(runLoop_, runLoopSource_, kCFRunLoopCommonModes);
    CFRelease(runLoopSource_);
    runLoopSource_ = nullptr;
  }
  if (eventTap_) {
    CGEventTapEnable(eventTap_, false);
    CFMachPortInvalidate(eventTap_);
    CFRelease(eventTap_);
    eventTap_ = nullptr;
  }
}

bool MacPlatformHook::Start() {
  if (running_) {
    return false;
  }

  SetFailureReason("");
  if (!EnsurePermissions()) {
    return false;
  }

  running_ = true;

  auto promise = std::make_shared<std::promise<bool>>();
  auto future = promise->get_future();
  {
    std::lock_guard<std::mutex> lock(startPromiseMutex_);
    startPromise_ = promise;
  }

  runLoopThread_ = std::thread(&MacPlatformHook::RunLoopThread, this);

  bool started = future.get();

  if (!started) {
    running_ = false;
    if (runLoopThread_.joinable()) {
      runLoopThread_.join();
    }
  }

  return started;
}

void MacPlatformHook::Stop() {
  if (!running_) {
    return;
  }
  running_ = false;
  if (eventTap_) {
    CGEventTapEnable(eventTap_, false);
  }
  if (runLoop_) {
    CFRunLoopStop(runLoop_);
  }
  if (runLoopThread_.joinable()) {
    runLoopThread_.join();
  }
}

std::string MacPlatformHook::GetFailureReason() const {
  std::lock_guard<std::mutex> lock(failureMutex_);
  return failureReason_;
}

void MacPlatformHook::SetFailureReason(std::string reason) {
  std::lock_guard<std::mutex> lock(failureMutex_);
  failureReason_ = std::move(reason);
}

}  // namespace mac
}  // namespace platform
}  // namespace inputhook
