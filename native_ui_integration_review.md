# Integration Review: Native UI in the Wingui Terminal Framework

## Overview

The `wingui` platform currently presents `native_ui` as an independent blocking module utilizing a C-style callback mechanism (`wingui_native_set_callbacks`) and managing its own internal Win32 host state, message pumping, and threading. 

However, the intention for the Wingui Terminal Framework (as described in `terminal_framework_design.md`) is a strict two-thread architecture where the UI thread owns the window, DX contexts, and renderers, while a background Client thread offloads logic and uses discrete message queues (Commands and Events) to communicate with the UI.

In this context, the legacy "Native UI reactive framework" needs to act as a **drop-in extension** to the Terminal Framework, integrating through the same Command/Event queue architecture rather than running its own isolated loop.

## Evaluating Native UI as a Drop-In

Currently, `native_ui.cpp` uses `nlohmann::json` to declaratively patch and dispatch events. This model heavily relies on:
1. `wingui_native_publish_json` / `wingui_native_patch_json`
2. Direct thread-safe mutations wrapping state in a global `g_native` mutex.
3. Callbacks synchronously dispatching events.

To become a true drop-in for the C++ command queue model, `native_ui` must be restructured so that it sits behind the `TerminalHost` queue:
- **UI Thread (Host)**: Consumes native UI rendering commands from the queue (e.g., `WINGUI_TERMINAL_CMD_NATIVE_PATCH_JSON`) and executes the Win32 `native_ui` logic.
- **Client Thread**: Emits declarative UI state JSONs into the queue, and listens for Reactive UI Events (like button clicks) translated into `WinguiTerminalEvent` structures.

## Proposed C++ Centric Command Queue Integration

### 1. Extending the Terminal Command Types

We should extend `WinguiTerminalCommandType` with Native UI operations:

```cpp
enum class WinguiCommandType : uint32_t {
    NOP = 0,
    // ... existing terminal cmds
    NATIVE_UI_PUBLISH = 100, // Publish a full layout
    NATIVE_UI_PATCH   = 101, // Diff-patch existing layout
};

struct NativeUiCommand {
    std::string json_payload; // Sent by client thread
};
```

### 2. Extending the Event Queue

The reactive events driven by control interactions (slider changes, button clicks) will be pushed back from the UI thread to the Client via the event queue.

```cpp
enum class WinguiEventType : uint32_t {
    NONE = 0,
    // ... existing terminal events
    NATIVE_UI_DISPATCH = 100,
};

struct NativeUiEvent {
    std::string json_event_payload;
};
```

### 3. Modifying `native_ui.cpp` Subsystem

Instead of `wingui_native_host_run()`, which takes over the main runloop, `native_ui` should expose a class or struct (e.g., `WinguiNativeUiEngine`) initialized and owned by the `TerminalHost` UI thread.

```cpp
class WinguiNativeUiEngine {
public:
    void Initialize(HWND container);
    void Destroy();
    
    // Called by the UI thread when draining the Terminal command queue
    void ApplyPatch(const std::string& json_patch);
    void PublishLayout(const std::string& json_layout);
    
    // Internal callback from the Subclassed Win32 controls, places into the Terminal event queue
    void OnControlEvent(const std::string& reactive_ui_event_args);
};
```

## Integration Strategy

1. **Refactor State:** Migrate the `g_native` singleton in `native_ui.cpp` into a cohesive `WinguiNativeUiEngine` object.
2. **Terminal Host Extension:** Add `WinguiNativeUiEngine* native_ui;` to the `WinguiTerminalHost` structure.
3. **Hook up Queues:** In the UI thread command loop drainer, if a `NATIVE_UI_*` command comes in, forward it to `native_ui->ApplyPatch(...)`. 
4. **Proxy Events:** Rather than using the global `g_native_callbacks.dispatch_event_json()`, the Win32 subclasses in `native_ui.cpp` will enqueue a `WinguiTerminalEvent` (with Native UI payload) back to the TerminalHost's event queue.
5. **C++ Queue Abstraction:** Use a thread-safe MPSC or SPMC `std::queue` or `moodycamel::ConcurrentQueue` for fast JSON message passing.

This fulfills the reactive structural requirement while adhering to the threading guarantees required by `terminal_framework_design.md`.
