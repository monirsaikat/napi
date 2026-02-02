#pragma once

#include <napi.h>
#include <optional>
#include <string>

namespace inputhook {

struct InputModifiers {
  bool shift = false;
  bool ctrl = false;
  bool alt = false;
  bool meta = false;
};

struct InputEvent {
  std::string type;
  double time = 0.0;
  std::optional<uint32_t> keycode;
  std::optional<uint32_t> scancode;
  std::optional<uint32_t> button;
  std::optional<int32_t> x;
  std::optional<int32_t> y;
  std::optional<int32_t> deltaX;
  std::optional<int32_t> deltaY;
  InputModifiers modifiers;
};

inline Napi::Object ToJsObject(Napi::Env env, const InputEvent& event) {
  Napi::Object output = Napi::Object::New(env);
  output.Set("type", event.type);
  output.Set("time", event.time);

  if (event.keycode) {
    output.Set("keycode", *event.keycode);
  }
  if (event.scancode) {
    output.Set("scancode", *event.scancode);
  }
  if (event.button) {
    output.Set("button", *event.button);
  }
  if (event.x) {
    output.Set("x", *event.x);
  }
  if (event.y) {
    output.Set("y", *event.y);
  }
  if (event.deltaX) {
    output.Set("deltaX", *event.deltaX);
  }
  if (event.deltaY) {
    output.Set("deltaY", *event.deltaY);
  }

  Napi::Object modifierObj = Napi::Object::New(env);
  modifierObj.Set("shift", event.modifiers.shift);
  modifierObj.Set("ctrl", event.modifiers.ctrl);
  modifierObj.Set("alt", event.modifiers.alt);
  modifierObj.Set("meta", event.modifiers.meta);
  output.Set("modifiers", modifierObj);

  return output;
}

} // namespace inputhook
