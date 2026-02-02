#pragma once

#include <functional>
#include <memory>

#include "event.h"

namespace inputhook {

class PlatformHook;

class InputEmitter {
 public:
  using EventCallback = std::function<void(InputEvent&&)>;

  explicit InputEmitter(EventCallback callback);
  ~InputEmitter();

  InputEmitter(const InputEmitter&) = delete;
  InputEmitter& operator=(const InputEmitter&) = delete;

  bool Start();
  void Stop();

 private:
  std::unique_ptr<PlatformHook> platformHook_;
};

class PlatformHook {
 public:
  using EventCallback = InputEmitter::EventCallback;

  explicit PlatformHook(EventCallback callback);
  virtual ~PlatformHook();

  PlatformHook(const PlatformHook&) = delete;
  PlatformHook& operator=(const PlatformHook&) = delete;

  virtual bool Start() = 0;
  virtual void Stop() = 0;

 protected:
  void Dispatch(InputEvent event);

 private:
  EventCallback callback_;
};

} // namespace inputhook
