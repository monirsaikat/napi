#import "hook_mac.h"

#import <ApplicationServices/ApplicationServices.h>

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <limits.h>
#include <mach-o/dyld.h>
#include <optional>
#include <unistd.h>
#include <vector>

namespace inputhook {
namespace platform {
namespace mac {

namespace {
constexpr int64_t kWatchdogIntervalMs = 120000;
constexpr int64_t kInitialNoEventMs = 30000;
constexpr int64_t kMinRecreateIntervalMs = 60000;

double CurrentTimeMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

int64_t NowSteadyMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

bool DebugEnabled() {
  static std::atomic<int> cached{-1};
  int value = cached.load(std::memory_order_acquire);
  if (value != -1) {
    return value == 1;
  }
  const char* env = std::getenv("DEBUG_HOOK");
  bool enabled = env && *env && std::string(env) != "0";
  cached.store(enabled ? 1 : 0, std::memory_order_release);
  return enabled;
}

void DebugLog(const char* fmt, ...) {
  if (!DebugEnabled()) {
    return;
  }
  va_list args;
  va_start(args, fmt);
  std::vfprintf(stderr, fmt, args);
  std::fprintf(stderr, "\n");
  va_end(args);
}

InputModifiers ModifiersFromFlags(CGEventFlags flags) {
  InputModifiers mods;
  mods.shift = (flags & kCGEventFlagMaskShift) != 0;
  mods.ctrl = (flags & kCGEventFlagMaskControl) != 0;
  mods.alt = (flags & kCGEventFlagMaskAlternate) != 0;
  mods.meta = (flags & kCGEventFlagMaskCommand) != 0;
  return mods;
}

std::optional<InputEvent> BuildEvent(CGEventType type, CGEventRef eventRef) {
  InputEvent event;
  event.time = CurrentTimeMs();
  event.modifiers = ModifiersFromFlags(CGEventGetFlags(eventRef));

  switch (type) {
    case kCGEventKeyDown:
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
      {
        CGPoint location = CGEventGetLocation(eventRef);
        event.x = static_cast<int32_t>(location.x);
        event.y = static_cast<int32_t>(location.y);
      }
      event.type = "mousemove";
      break;
    case kCGEventLeftMouseDown:
    case kCGEventRightMouseDown:
    case kCGEventOtherMouseDown:
      {
        CGPoint location = CGEventGetLocation(eventRef);
        event.x = static_cast<int32_t>(location.x);
        event.y = static_cast<int32_t>(location.y);
      }
      event.type = "mousedown";
      event.button = static_cast<uint32_t>(
          CGEventGetIntegerValueField(eventRef, kCGMouseEventButtonNumber));
      break;
    case kCGEventLeftMouseUp:
    case kCGEventRightMouseUp:
    case kCGEventOtherMouseUp:
      {
        CGPoint location = CGEventGetLocation(eventRef);
        event.x = static_cast<int32_t>(location.x);
        event.y = static_cast<int32_t>(location.y);
      }
      event.type = "mouseup";
      event.button = static_cast<uint32_t>(
          CGEventGetIntegerValueField(eventRef, kCGMouseEventButtonNumber));
      break;
    case kCGEventScrollWheel:
      {
        CGPoint location = CGEventGetLocation(eventRef);
        event.x = static_cast<int32_t>(location.x);
        event.y = static_cast<int32_t>(location.y);
      }
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

CGEventFlags ModifierFlagMask() {
  return kCGEventFlagMaskShift |
         kCGEventFlagMaskControl |
         kCGEventFlagMaskAlternate |
         kCGEventFlagMaskCommand |
         kCGEventFlagMaskAlphaShift;
}

std::string BuildProcessPath() {
  uint32_t size = 0;
  if (_NSGetExecutablePath(nullptr, &size) != -1 || size == 0) {
    return "this process";
  }

  std::vector<char> buffer(size, '\0');
  if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
    return "this process";
  }

  char resolved[PATH_MAX];
  if (realpath(buffer.data(), resolved)) {
    return std::string(resolved);
  }

  return std::string(buffer.data());
}

CGEventMask BuildEventMask() {
  return CGEventMaskBit(kCGEventKeyDown) |
         CGEventMaskBit(kCGEventKeyUp) |
         CGEventMaskBit(kCGEventFlagsChanged) |
         CGEventMaskBit(kCGEventMouseMoved) |
         CGEventMaskBit(kCGEventLeftMouseDown) |
         CGEventMaskBit(kCGEventLeftMouseUp) |
         CGEventMaskBit(kCGEventRightMouseDown) |
         CGEventMaskBit(kCGEventRightMouseUp) |
         CGEventMaskBit(kCGEventOtherMouseDown) |
         CGEventMaskBit(kCGEventOtherMouseUp) |
         CGEventMaskBit(kCGEventScrollWheel);
}

bool CheckAccessibilityPermission(const std::string& processPath,
                                  std::string* failureMessage) {
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
    std::string message = "Accessibility permission required for " +
                          (processPath.empty() ? BuildProcessPath() : processPath) +
                          ". Grant it under Privacy & Security > Accessibility.";
    if (failureMessage) {
      *failureMessage = message;
    }
    DebugLog("inputhook: permission preflight failed (Accessibility)");
    return false;
  }
  return true;
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

  if (!self->running_.load(std::memory_order_acquire)) {
    return event;
  }

  if (type == kCGEventTapDisabledByTimeout ||
      type == kCGEventTapDisabledByUserInput) {
    DebugLog("inputhook: event tap disabled (%d); re-enabling", static_cast<int>(type));
    if (self->eventTap_) {
      CGEventTapEnable(self->eventTap_, true);
    }
    return event;
  }

  self->lastEventMs_.store(NowSteadyMs(), std::memory_order_release);
  self->eventSeen_.store(true, std::memory_order_release);

  if (type == kCGEventFlagsChanged) {
    CGEventFlags flags = CGEventGetFlags(event);
    CGEventFlags mask = ModifierFlagMask();
    CGEventFlags changed = (flags ^ self->lastFlags_) & mask;
    self->lastFlags_ = (self->lastFlags_ & ~mask) | (flags & mask);
    if (changed == 0) {
      return event;
    }

    InputEvent modifierEvent;
    modifierEvent.time = CurrentTimeMs();
    modifierEvent.modifiers = ModifiersFromFlags(flags);
    modifierEvent.type = (flags & changed) ? "keydown" : "keyup";
    self->Dispatch(std::move(modifierEvent));
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

  DebugLog("inputhook: attempting event tap at location %d", static_cast<int>(location));
  CFMachPortContext context = {};
  context.info = this;
  CFMachPortRef tap = CGEventTapCreate(location,
                                       kCGHeadInsertEventTap,
                                       kCGEventTapOptionListenOnly,
                                       mask,
                                       EventCallback,
                                       &context);
  if (!tap) {
    DebugLog("inputhook: CGEventTapCreate failed for location %d", static_cast<int>(location));
    return false;
  }

  eventTap_ = tap;
  runLoopSource_ = CFMachPortCreateRunLoopSource(nullptr, eventTap_, 0);
  if (!runLoopSource_) {
    DebugLog("inputhook: failed to create run loop source");
    CFMachPortInvalidate(eventTap_);
    CFRelease(eventTap_);
    eventTap_ = nullptr;
    return false;
  }

  DebugLog("inputhook: event tap created at location %d", static_cast<int>(location));
  return true;
}

bool MacPlatformHook::CreateEventTapSequence(CGEventMask mask) {
  if (CreateEventTap(kCGSessionEventTap, mask)) {
    return true;
  }
  if (CreateEventTap(kCGAnnotatedSessionEventTap, mask)) {
    return true;
  }
  return CreateEventTap(kCGHIDEventTap, mask);
}

void MacPlatformHook::TeardownEventTap() {
  if (runLoopSource_) {
    if (runLoop_) {
      CFRunLoopRemoveSource(runLoop_, runLoopSource_, kCFRunLoopCommonModes);
    }
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

bool MacPlatformHook::RecreateEventTap(const char* reason) {
  DebugLog("inputhook: recreating event tap (%s)", reason ? reason : "unknown");
  TeardownEventTap();

  CGEventMask mask = BuildEventMask();
  if (!CreateEventTapSequence(mask)) {
    SetLastError("Failed to recreate CGEventTap");
    return false;
  }

  if (runLoop_ && runLoopSource_) {
    CFRunLoopAddSource(runLoop_, runLoopSource_, kCFRunLoopCommonModes);
  }
  if (eventTap_) {
    CGEventTapEnable(eventTap_, true);
  }

  return true;
}

bool MacPlatformHook::EnsurePermissions() {
  if (@available(macOS 10.15, *)) {
    std::string accessibilityFailure;
    if (!CheckAccessibilityPermission(processPath_, &accessibilityFailure)) {
      SetFailureReason(accessibilityFailure);
      SetLastError(accessibilityFailure);
      return false;
    }

    if (!CGPreflightListenEventAccess()) {
      CGRequestListenEventAccess();
      std::string message = "Input Monitoring permission required for " +
                            (processPath_.empty() ? BuildProcessPath() : processPath_) +
                            ". Enable it in System Settings > Privacy & Security > "
                            "Input Monitoring and restart the binary.";
      SetFailureReason(message);
      SetLastError(message);
      DebugLog("inputhook: permission preflight failed (Input Monitoring)");
      return false;
    }
    return true;
  }

  std::string accessibilityFailure;
  if (!CheckAccessibilityPermission(processPath_, &accessibilityFailure)) {
    SetFailureReason(accessibilityFailure);
    SetLastError(accessibilityFailure);
    return false;
  }

  return true;
}

void MacPlatformHook::RunLoopThread() {
  @autoreleasepool {
    runLoop_ = CFRunLoopGetCurrent();
    CGEventMask mask = BuildEventMask();

    if (!CreateEventTapSequence(mask)) {
      std::string message = "Unable to create a CGEventTap; ensure Input Monitoring "
                            "or Accessibility permissions are granted for " +
                            (processPath_.empty() ? BuildProcessPath() : processPath_) + ".";
      SetFailureReason(message);
      SetLastError(message);
      running_ = false;
      NotifyStartResult(false);
      return;
    }

    SetFailureReason("");
    NotifyStartResult(true);

    if (runLoopSource_) {
      CFRunLoopAddSource(runLoop_, runLoopSource_, kCFRunLoopCommonModes);
    }
    if (eventTap_) {
      CGEventTapEnable(eventTap_, true);
    }

    lastRecreateMs_.store(NowSteadyMs(), std::memory_order_release);

    while (running_) {
      CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.1, true);

      int64_t now = NowSteadyMs();
      int64_t lastEvent = lastEventMs_.load(std::memory_order_acquire);
      int64_t lastRecreate = lastRecreateMs_.load(std::memory_order_acquire);
      int64_t sinceEvent = now - lastEvent;
      int64_t sinceRecreate = now - lastRecreate;
      bool seen = eventSeen_.load(std::memory_order_acquire);

      if (sinceRecreate >= kMinRecreateIntervalMs) {
        if (!seen && sinceEvent >= kInitialNoEventMs) {
          lastRecreateMs_.store(now, std::memory_order_release);
          RecreateEventTap("no events after start");
        } else if (seen && sinceEvent >= kWatchdogIntervalMs) {
          lastRecreateMs_.store(now, std::memory_order_release);
          RecreateEventTap("no events watchdog");
        }
      }
    }

    TeardownEventTap();
    runLoop_ = nullptr;
  }
}

bool MacPlatformHook::Start() {
  if (running_) {
    return false;
  }

  SetFailureReason("");
  SetLastError("");
  processPath_ = BuildProcessPath();
  if (!EnsurePermissions()) {
    return false;
  }

  lastFlags_ = CGEventSourceFlagsState(kCGEventSourceStateHIDSystemState);
  int64_t now = NowSteadyMs();
  lastEventMs_.store(now, std::memory_order_release);
  lastRecreateMs_.store(now, std::memory_order_release);
  eventSeen_.store(false, std::memory_order_release);

  running_ = true;
  DebugLog("inputhook: starting mac event tap thread");

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
    DebugLog("inputhook: start failed");
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
  DebugLog("inputhook: stopping mac event tap thread");
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

std::string MacPlatformHook::GetLastError() const {
  std::lock_guard<std::mutex> lock(lastErrorMutex_);
  return lastError_;
}

void MacPlatformHook::SetFailureReason(std::string reason) {
  std::lock_guard<std::mutex> lock(failureMutex_);
  failureReason_ = std::move(reason);
}

void MacPlatformHook::SetLastError(std::string reason) {
  std::lock_guard<std::mutex> lock(lastErrorMutex_);
  lastError_ = std::move(reason);
}

}  // namespace mac
}  // namespace platform
}  // namespace inputhook
