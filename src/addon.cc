#include <atomic>
#include <memory>

#include <napi.h>

#include "common/emitter.h"
#include "common/event.h"

namespace {

void CallJsEvent(Napi::Env env,
                 Napi::Function callback,
                 void* /*context*/,
                 inputhook::InputEvent* event);

using EventTsfn =
    Napi::TypedThreadSafeFunction<void, inputhook::InputEvent, CallJsEvent>;

std::atomic<EventTsfn*> g_tsfnPointer{nullptr};
std::atomic<bool> g_running{false};
std::unique_ptr<inputhook::InputEmitter> g_emitter;
std::unique_ptr<EventTsfn> g_tsfnHolder;

void EventDispatcher(inputhook::InputEvent&& event) {
  EventTsfn* tsfn = g_tsfnPointer.load(std::memory_order_acquire);
  if (!tsfn) {
    return;
  }

  auto* eventCopy = new inputhook::InputEvent(std::move(event));
  napi_status status = tsfn->NonBlockingCall(eventCopy);
  if (status != napi_ok) {
    delete eventCopy;
  }
}

void CallJsEvent(Napi::Env env,
                 Napi::Function callback,
                 void* /*context*/,
                 inputhook::InputEvent* event) {
  if (!event) {
    return;
  }

  Napi::HandleScope scope(env);
  callback.Call({inputhook::ToJsObject(env, *event)});
  delete event;
}

void ResetThreadSafeFunction() {
  EventTsfn* previous = g_tsfnPointer.exchange(nullptr, std::memory_order_acq_rel);
  if (previous) {
    previous->Abort();
  }
  g_tsfnHolder.reset();
}

void RegisterEventCallback(Napi::Env env, Napi::Function callback) {
  ResetThreadSafeFunction();

  EventTsfn tsfn = EventTsfn::New(env,
                                  callback,
                                  "inputhook",
                                  0,
                                  1,
                                  nullptr);
  g_tsfnHolder = std::make_unique<EventTsfn>(std::move(tsfn));
  g_tsfnPointer.store(g_tsfnHolder.get(), std::memory_order_release);
}

Napi::Value Start(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (g_running.load(std::memory_order_acquire)) {
    return Napi::Boolean::New(env, false);
  }

  if (!g_tsfnHolder) {
    Napi::TypeError::New(env, "onEvent callback must be registered before starting")
        .ThrowAsJavaScriptException();
    return env.Undefined();
  }

  g_emitter = std::make_unique<inputhook::InputEmitter>(EventDispatcher);
  bool started = g_emitter->Start();
  if (!started) {
    g_emitter.reset();
    return Napi::Boolean::New(env, false);
  }

  g_running.store(true, std::memory_order_release);
  return Napi::Boolean::New(env, true);
}

Napi::Value Stop(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (!g_running.load(std::memory_order_acquire)) {
    return env.Undefined();
  }

  if (g_emitter) {
    g_emitter->Stop();
    g_emitter.reset();
  }
  g_running.store(false, std::memory_order_release);
  return env.Undefined();
}

Napi::Value OnEvent(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 1 || !info[0].IsFunction()) {
    Napi::TypeError::New(env, "callback function required")
        .ThrowAsJavaScriptException();
    return env.Undefined();
  }

  RegisterEventCallback(env, info[0].As<Napi::Function>());
  return env.Undefined();
}

void Cleanup() {
  if (g_emitter) {
    g_emitter->Stop();
    g_emitter.reset();
  }
  ResetThreadSafeFunction();
  g_running.store(false, std::memory_order_release);
}

Napi::Object Init(Napi::Env env, Napi::Object exports) {
  exports.Set("start", Napi::Function::New(env, Start));
  exports.Set("stop", Napi::Function::New(env, Stop));
  exports.Set("onEvent", Napi::Function::New(env, OnEvent));
  env.AddCleanupHook(Cleanup);
  return exports;
}

}  // namespace

NODE_API_MODULE(inputhook, Init)
