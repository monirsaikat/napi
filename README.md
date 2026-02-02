# inputhook

Cross-platform Node.js native addon for global keyboard and mouse events.

## Build

1. Install dependencies: `npm install`.
2. Build the addon (any time after install): `npm run build` (runs `node-gyp rebuild`).
3. The compiled binary lives at `build/Release/inputhook.node` (or `Debug` if you switch builds).

### Notes

- `node-addon-api` is required and already listed in `package.json`.
- macOS requires accessibility permission for the `CGEventTap`.
- Linux requires X11 and XInput2 headers/libraries (installed via your distro, e.g., `libxi-dev`).
- Windows links against `user32.lib`.
# napi
