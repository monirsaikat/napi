# inputhook usage notes

## What changed from `uihook-napi`

Historically the renderer process wired up `ioHook` events like `keydown`, `keyup`, `mousedown`, `mouseup`, `wheel`, and `mousemove` and fed them into the tracking bucket logic shown in your snippet (dedescribed events, dedup maps, health watchdog, etc.).  This now becomes the responsibility of this native addon (`inputhook`) because it exposes the same event names and payloads through a single `onEvent` callback plus explicit `start`/`stop` controls.  The rest of your tracking stack (health monitoring, bucket accounting, renderer IPC, etc.) can remain the same: just call `inputhook.onEvent(...)` once, register the handler before `inputhook.start()`, and keep using the same `markActivity`/idle-tracking logic you already wrote.

## Event schema

Each callback receives an object shaped like:

| field     | description |
|-----------|-------------|
| `type`    | string: `"keydown"`, `"keyup"`, `"mousedown"`, `"mouseup"`, `"mousemove"`, or `"wheel"` |
| `time`    | epoch milliseconds (double) |
| `keycode` | optional numeric virtual key identifier (keyboard only) |
| `scancode` | optional hardware scan code (keyboard only) |
| `button`  | optional zero-based mouse button (0=left, 1=right, 2=middle) |
| `x`, `y`  | optional cursor coordinates (mousemove, mousedown, mouseup) |
| `deltaX`, `deltaY` | optional deltas for wheel or raw motion events |
| `modifiers` | `{shift, ctrl, alt, meta}` booleans derived from the current keyboard state |

This matches the fields you normalized via `normalizeCode`; `keycode`/`button` are the canonical identifiers you already read from the event objects.

## Platform behavior notes

- **Linux (X11)** – the addon listens to XInput2 raw events (`XI_RawKeyPress`, `XI_RawButtonPress`, etc.) before falling back to device events if necessary.  Mouse wheels are translated from button 4/5/6/7 plus `XI_RawMotion` valuators so scroll deltas come through as `"wheel"` events with `deltaX`/`deltaY`.  Raw pointer events are flagged so you only get each action once.
- **macOS** – the `CGEventTap` hook already provided `keydown`/`keyup`, mouse buttons, movement, and scroll wheel events plus modifier flags; make sure your process has accessibility permission and that you build after the constructor change in `MacPlatformHook`.
- **Windows** – the `WH_KEYBOARD_LL`/`WH_MOUSE_LL` hooks keep working the same way as before, emitting the same six event types and the standard `InputModifiers`.

Because the addon matches the prior `uihook-napi` surface (event names + payload shape), you can keep the `activityEventCounts`, dedupe logic and watchdog exactly as-is.

## Integration checklist

1. `const inputhook = require('.')` (or your path alias) and register the renderer listener once via `inputhook.onEvent((event) => { ... })`.
2. Call `inputhook.start()` _after_ the handler is set.  If it returns `false`, check that the native addon built successfully (`npm run build` / xcode toolchain for macOS).
3. Use the same `normalizeCode` function you already wrote to look at `keycode` / `rawcode` / `button`.
4. Record `keyboard` vs `mouse` counts by consulting `event.type`.
5. Keep the existing dedupe maps (`downKeysAt`, `downButtonsAt`, `lastKeyEventAt`, `lastButtonEventAt`) to guard against rapid repeats; the event names and payloads are identical so those maps remain valid.
6. When pausing/tracking stops, call `inputhook.stop()` to tear down the native hooks cleanly; the addon already calls `inputhook.stop()` internally from the C++ `Cleanup` hook when the module unloads, but it is safe to stop and start multiple times as you were doing with `ioHook`.

## Debugging & restart guidance

- If you ever see no events for a long time, trigger `inputhook.stop()` / `inputhook.start()` just like the `restartHook` in your snippet.
- The addon surfaces mouse wheel via `"wheel"` with `deltaY` or `deltaX` set to ±1 steps; treat those exactly like the old `wheel`/`mousewheel` listeners.
- Keep the same cooldown constants (`HOOK_RESTART_COOLDOWN_MS`, `HOOK_INACTIVITY_MS`, etc.) because they still protect the native hook thread.

With this doc you now have a reference for the event payloads and best practices; copy the relevant sections back into your renderer/tracker module when you wire the new addon. Let me know if you need examples for the renderer-to-main IPC bridge (e.g., `tracking` events) as well.
