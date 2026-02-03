#pragma once

#include <string>
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
  std::string GetFailureReason() const;
  std::string GetLastError() const;

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
  virtual std::string GetFailureReason() const;
  virtual std::string GetLastError() const;

protected:
  void Dispatch(InputEvent event);

 private:
  EventCallback callback_;
};

} // namespace inputhook
