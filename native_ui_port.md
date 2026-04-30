# Wingui Native UI Port

The WinScheme native Win32 declarative UI host has been extracted into this repo as:

- `include/wingui/native_ui.h`
- `src/native_ui.cpp`

The extracted layer now exposes a generic callback-based host boundary instead of resolving WinScheme executable exports.

Applications should populate `WinguiNativeCallbacks` and call `wingui_native_set_callbacks(...)` before publishing a UI spec or entering the host loop.

For terminal/server integration, the native host now also exposes a queue-backed bridge:

- `wingui_native_enqueue_command(...)` accepts typed native-ui commands such as publish, patch, and show-host.
- `wingui_native_drain_command_queue(...)` lets a host-side runtime drain queued commands from its own control loop.
- `wingui_native_poll_event(...)` and `wingui_native_event_handle(...)` expose reactive control events as queued JSON payloads suitable for a client/event queue model.
- `wingui_native_release_event(...)` releases payload buffers returned by `wingui_native_poll_event(...)`.

The legacy `wingui_native_publish_json(...)`, `wingui_native_patch_json(...)`, and callback dispatch flow remain available as direct wrappers for standalone usage.

Current external dependency:

- `nlohmann/json.hpp` must be available on the include path when compiling `src/native_ui.cpp`.

The implementation remains the full Win32 native-controls renderer and patch engine ported from the WinScheme tree, with the public API renamed to the `wingui_native_*` surface for reuse by other Windows applications.