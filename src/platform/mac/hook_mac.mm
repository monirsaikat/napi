#import "hook_mac.h"

#import <ApplicationServices/ApplicationServices.h>

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

void MacPlatformHook::RunLoopThread() {
  CGEventMask mask = CGEventMaskBit(kCGEventKeyDown) |
                     CGEventMaskBit(kCGEventKeyUp) |
                     CGEventMaskBit(kCGEventMouseMoved) |
                     CGEventMaskBit(kCGEventLeftMouseDown) |
                     CGEventMaskBit(kCGEventLeftMouseUp) |
                     CGEventMaskBit(kCGEventRightMouseDown) |
                     CGEventMaskBit(kCGEventRightMouseUp) |
                     CGEventMaskBit(kCGEventOtherMouseDown) |
                     CGEventMaskBit(kCGEventOtherMouseUp) |
                     CGEventMaskBit(kCGEventScrollWheel);

  CFMachPortContext context = {};
  context.info = this;
  eventTap_ = CGEventTapCreate(kCGHIDEventTap,
                               kCGHeadInsertEventTap,
                               kCGEventTapOptionListenOnly,
                               mask,
                               EventCallback,
                               &context);
  if (!eventTap_) {
    NotifyStartResult(false);
    running_ = false;
    return;
  }

  NotifyStartResult(true);

  runLoop_ = CFRunLoopGetCurrent();
  CFRunLoopSourceRef source = CFMachPortCreateRunLoopSource(nullptr, eventTap_, 0);
  CFRunLoopAddSource(runLoop_, source, kCFRunLoopCommonModes);
  CGEventTapEnable(eventTap_, true);

  while (running_) {
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.1, true);
  }

  if (source) {
    CFRunLoopRemoveSource(runLoop_, source, kCFRunLoopCommonModes);
    CFRelease(source);
  }
  if (eventTap_) {
    CFMachPortInvalidate(eventTap_);
    CFRelease(eventTap_);
    eventTap_ = nullptr;
  }
}

bool MacPlatformHook::Start() {
  if (running_) {
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

}  // namespace mac
}  // namespace platform
}  // namespace inputhook
