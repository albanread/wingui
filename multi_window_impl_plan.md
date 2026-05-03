# Multi-Window Implementation Plan

## Status legend
- `[ ]` not started
- `[-]` in progress
- `[x]` done

---

## Scope summary

The goal is to move from a **single-window singleton runtime** to a **multi-window runtime** where:

- `SuperTerminalAppHost` owns process-wide lifecycle, shared services, the client thread, and the list of windows.
- `SuperTerminalWindowHost` (one per top-level window) owns its Win32 HWND, its D3D/renderer objects, its native UI session, and its pane registry.
- `NativeUiSession` replaces the global `NativeHostState g_native` in native_ui.cpp and represents one embedded declarative layout session attached to one HWND.
- All events carry a `window_id`.
- The public API gains explicit window-management calls (`create_window`, `close_window`, `publish_ui_for_window`, `resolve_pane_id_for_window`, etc.).
- One UI thread and one client thread are kept; no per-window threads.

---

## Phase 1 — Archive existing demos
**Files moved:** `src/archived/`

- [x] Move `demo.cpp`, `demo_c_spec_bind.c`, `demo_c_spec_builder.c`, `demo_spec_bind.cpp`
- [x] Move `demo_widget_galley.cpp`, `demo_widget_galley_no_scroll.cpp`, `demo_widget_patch_probe.cpp`
- [x] Move `demo_zig_graphics_demo.zig`, `demo_zig_spec_bind.zig`, `demo_zig_starfield.zig`, `demo_zig_workspace.zig`

---

## Phase 2 — Introduce `NativeUiSession` in native_ui.cpp

### What changes

The entire `NativeHostState g_native` singleton plus `g_native_callbacks` and `g_native_last_error` become members (or fields) of a heap-allocated `NativeUiSession`. All functions that currently read `g_native` directly must accept either a `NativeUiSession*` parameter (internal functions) or resolve the session via the active window context.

For the first pass we keep a **single** `NativeUiSession*` but store it as a pointer rather than a global struct, so the refactor path to multiple sessions is clear.

### New opaque handle in native_ui.h

```c
typedef struct WinguiNativeUiSession WinguiNativeUiSession;
```

Replace current session-create API with session-scoped create/destroy:

```c
WINGUI_API WinguiNativeUiSession* WINGUI_CALL wingui_native_session_create(void);
WINGUI_API void                   WINGUI_CALL wingui_native_session_destroy(WinguiNativeUiSession* session);
WINGUI_API void  WINGUI_CALL wingui_native_session_set_callbacks(WinguiNativeUiSession* session, const WinguiNativeCallbacks* callbacks);
WINGUI_API int64_t WINGUI_CALL wingui_native_session_attach_embedded_host(WinguiNativeUiSession* session, const WinguiNativeEmbeddedHostDesc* desc);
WINGUI_API void    WINGUI_CALL wingui_native_session_detach(WinguiNativeUiSession* session);
WINGUI_API int64_t WINGUI_CALL wingui_native_session_publish_json(WinguiNativeUiSession* session, const char* utf8);
WINGUI_API int64_t WINGUI_CALL wingui_native_session_patch_json(WinguiNativeUiSession* session, const char* utf8);
WINGUI_API int64_t WINGUI_CALL wingui_native_session_host_run(WinguiNativeUiSession* session);
WINGUI_API void*   WINGUI_CALL wingui_native_session_host_hwnd(WinguiNativeUiSession* session);
WINGUI_API int64_t WINGUI_CALL wingui_native_session_try_get_node_bounds(WinguiNativeUiSession* session, const char* node_id_utf8, WinguiNativeNodeBounds* out_bounds);
WINGUI_API int64_t WINGUI_CALL wingui_native_session_try_get_node_hwnd(WinguiNativeUiSession* session, const char* node_id_utf8, void** out_hwnd);
WINGUI_API int64_t WINGUI_CALL wingui_native_session_try_get_node_type_utf8(WinguiNativeUiSession* session, const char* node_id_utf8, char* buffer_utf8, uint32_t buffer_size);
WINGUI_API int64_t WINGUI_CALL wingui_native_session_copy_focused_pane_id_utf8(WinguiNativeUiSession* session, char* buffer_utf8, uint32_t buffer_size);
WINGUI_API int64_t WINGUI_CALL wingui_native_session_set_host_bounds(WinguiNativeUiSession* session, int32_t x, int32_t y, int32_t width, int32_t height);
WINGUI_API int64_t WINGUI_CALL wingui_native_session_get_patch_metrics(WinguiNativeUiSession* session, WinguiNativePatchMetrics* out_metrics);
WINGUI_API const char* WINGUI_CALL wingui_native_session_last_error_utf8(WinguiNativeUiSession* session);
WINGUI_API int64_t WINGUI_CALL wingui_native_session_poll_event(WinguiNativeUiSession* session, WinguiNativeEvent* out_event);
WINGUI_API void    WINGUI_CALL wingui_native_session_release_event(WinguiNativeUiSession* session, WinguiNativeEvent* event);
WINGUI_API void*   WINGUI_CALL wingui_native_session_event_handle(WinguiNativeUiSession* session);
```

Old single-session API (`wingui_native_publish_json`, `wingui_native_attach_embedded_host`, etc.) is **removed**. All callers in terminal.cpp switch to session-scoped calls.

### Tracker

- [ ] Define `WinguiNativeUiSession` opaque type in `native_ui.h`
- [ ] Add all `wingui_native_session_*` declarations to `native_ui.h`
- [ ] Remove old singleton declarations from `native_ui.h`
- [ ] In `native_ui.cpp`: convert `NativeHostState g_native` from a global struct into a heap-allocated member of `NativeUiSession`
- [ ] Move `g_native_callbacks` into `NativeUiSession`
- [ ] Move `g_native_last_error` into `NativeUiSession` (or keep thread-local for the UI thread since all sessions share the UI thread)
- [ ] Refactor all internal functions in `native_ui.cpp` to take `NativeUiSession*` where they currently implicitly use `g_native`
  - Key functions: `dispatchUiEventJson`, `rebuildNativeWindow`, `rebuildNativeContainerContents`, `layoutNode`, `measureNode`, `ensureNativeThread`, `attachEmbeddedNativeHost`, `executeNativePublishJson`, `executeNativePatchJson`, `executeNativeHostRun`, `nativeHostThreadMain`, all subclass procs that read `g_native`
  - Subclass procs (`containerForwardSubclassProc`, `transparentPaneSubclassProc`, `canvasSubclassProc`, etc.) currently read `g_native` via globals; they must store the session pointer in the subclass ref_data parameter
  - `nativeWndProc` currently reads `g_native`; it should look up the session from a per-HWND map or the HWND's user data
- [ ] Implement `wingui_native_session_create` / `_destroy`
- [ ] Implement all `wingui_native_session_*` C API wrappers
- [ ] Keep old `wingui_native_*` global-style stubs **temporarily** (they will be removed when terminal.cpp is updated)

---

## Phase 3 — Introduce `SuperTerminalWindowId` and window-scoped types in terminal.h

### New types

```c
typedef struct SuperTerminalWindowId {
    uint64_t value;
} SuperTerminalWindowId;

typedef struct SuperTerminalWindowDesc {
    const char* title_utf8;
    uint32_t columns;
    uint32_t rows;
    const char* font_family_utf8;
    int32_t font_pixel_height;
    float dpi_scale;
    const char* text_shader_path_utf8;
    const char* initial_ui_json_utf8;
    uint32_t command_queue_capacity;
    uint32_t event_queue_capacity;
    uint32_t flags;
} SuperTerminalWindowDesc;
```

### Add `window_id` to `SuperTerminalEvent`

```c
typedef struct SuperTerminalEvent {
    SuperTerminalWindowId window_id;   // NEW FIELD (first, before type)
    SuperTerminalEventType type;
    uint32_t sequence;
    union { ... } data;
} SuperTerminalEvent;
```

### New window-management API

```c
WINGUI_API int32_t WINGUI_CALL super_terminal_create_window(
    SuperTerminalClientContext* ctx,
    const SuperTerminalWindowDesc* desc,
    SuperTerminalWindowId* out_window_id);

WINGUI_API int32_t WINGUI_CALL super_terminal_close_window(
    SuperTerminalClientContext* ctx,
    SuperTerminalWindowId window_id);

WINGUI_API int32_t WINGUI_CALL super_terminal_publish_ui_json_for_window(
    SuperTerminalClientContext* ctx,
    SuperTerminalWindowId window_id,
    const char* json_utf8);

WINGUI_API int32_t WINGUI_CALL super_terminal_patch_ui_json_for_window(
    SuperTerminalClientContext* ctx,
    SuperTerminalWindowId window_id,
    const char* patch_json_utf8);

WINGUI_API int32_t WINGUI_CALL super_terminal_resolve_pane_id_for_window(
    SuperTerminalClientContext* ctx,
    SuperTerminalWindowId window_id,
    const char* node_id_utf8,
    SuperTerminalPaneId* out_pane_id);

WINGUI_API int32_t WINGUI_CALL super_terminal_get_pane_layout_for_window(
    SuperTerminalClientContext* ctx,
    SuperTerminalWindowId window_id,
    SuperTerminalPaneId pane_id,
    SuperTerminalPaneLayout* out_layout);

WINGUI_API int32_t WINGUI_CALL super_terminal_set_title_for_window(
    SuperTerminalClientContext* ctx,
    SuperTerminalWindowId window_id,
    const char* title_utf8);
```

All pane-content commands (`text_grid_write_cells`, `rgba_upload`, `indexed_upload`, `sprite_render`, `vector_draw`, `indexed_fill_rect`, `indexed_draw_line`) gain a `window_id` field in their payload structs. Commands that currently use `SuperTerminalPaneId` still use it, but the pane_id is now only meaningful relative to the window it was resolved in.

### New app-level descriptor

```c
typedef struct SuperTerminalAppDesc {
    uint32_t command_queue_capacity;
    uint32_t event_queue_capacity;
    void* user_data;
    int32_t (WINGUI_CALL *startup)(SuperTerminalClientContext* ctx, void* user_data);
    void    (WINGUI_CALL *shutdown)(void* user_data);
} SuperTerminalAppDesc;

typedef struct SuperTerminalHostedAppDesc {
    uint32_t command_queue_capacity;
    uint32_t event_queue_capacity;
    uint32_t target_frame_ms;
    int32_t auto_request_present;
    void* user_data;
    SuperTerminalHostedSetupFn setup;
    SuperTerminalHostedEventFn on_event;
    SuperTerminalHostedFrameFn on_frame;
    SuperTerminalHostedShutdownFn shutdown;
} SuperTerminalHostedAppDesc;
```

Window creation is now explicit via `super_terminal_create_window` called from the client startup callback. The initial window is NOT implicit.

### Tracker

- [ ] Add `SuperTerminalWindowId` to `terminal.h`
- [ ] Add `SuperTerminalWindowDesc` to `terminal.h`
- [ ] Add `window_id` field to `SuperTerminalEvent`
- [ ] Add new `super_terminal_*_for_window` / `super_terminal_create_window` / `super_terminal_close_window` declarations
- [ ] Slim down `SuperTerminalAppDesc` (remove per-window fields)
- [ ] Slim down `SuperTerminalHostedAppDesc` similarly
- [ ] Add new `SUPERTERMINAL_CMD_CREATE_WINDOW` and `SUPERTERMINAL_CMD_CLOSE_WINDOW` command types
- [ ] Add `SuperTerminalCreateWindow` and `SuperTerminalCloseWindow` command payload structs

---

## Phase 4 — Refactor terminal.cpp: AppHost + WindowHost

### Structural changes

```
SuperTerminalRuntimeHost (current, single-window)
  ↓ becomes
SuperTerminalAppHost (process-level)
  ├── list of SuperTerminalWindowHost
  ├── app-level command queue (cross-window commands)
  ├── app-level event queue
  ├── client thread
  ├── stop_requested, exit_code
  ├── next_window_id counter
  └── shared glyph atlas (kept app-level; all windows share same font for now)

SuperTerminalWindowHost (per-window)
  ├── window_id (uint64_t)
  ├── WinguiWindow*
  ├── WinguiContext*
  ├── WinguiTextGridRenderer*
  ├── WinguiIndexedGraphicsRenderer*
  ├── WinguiRgbaPaneRenderer*
  ├── WinguiVectorRenderer*
  ├── WinguiIndexedFillRenderer*
  ├── WinguiNativeUiSession*
  ├── vector<RegisteredPane> panes  (window-local)
  ├── pane_mutex
  ├── render_dirty
  ├── display_buffer_index
  ├── active_pane_id
  ├── window_focused
  ├── native_attached flag
  ├── sprite_atlas_packer
  ├── rgba_assets map (window-local assets)
  └── BufferedTerminalSurface surface (fallback/root text surface)

SuperTerminalClientContext
  ├── app*  → pointer to SuperTerminalAppHost
  └── (window lookups go through app->find_window(window_id))
```

### Message pump changes

`pumpMessages` currently loops over a single host window. It must loop over **all open windows** (or let the OS dispatch to all via a single `GetMessageW` since they share a thread). The existing single-threaded Win32 model handles this naturally — `GetMessageW(nullptr,...)` receives messages for all windows on the thread.

### Command routing

Commands that target a window carry a `window_id`. `drainCommands` in the new implementation resolves `window_id → SuperTerminalWindowHost*` and dispatches accordingly.

Commands that don't target a window (e.g. `REQUEST_CLOSE` at app level) are handled at the AppHost level.

### Window create/close flow

`super_terminal_create_window`:
1. Client enqueues `SUPERTERMINAL_CMD_CREATE_WINDOW` with a `SuperTerminalWindowDesc` copy.
2. UI thread drains the command, calls `initWindowHost(app, desc, &window_host)`.
3. `initWindowHost` creates the Win32 window, D3D context, renderers, and `WinguiNativeUiSession`.
4. On success, fires a `SUPERTERMINAL_EVENT_WINDOW_CREATED` event back to the client with the new `window_id`.

`super_terminal_close_window`:
1. Client enqueues `SUPERTERMINAL_CMD_CLOSE_WINDOW` with `window_id`.
2. UI thread finds the window host, calls `shutdownWindowHost`, removes it from the map.
3. Fires `SUPERTERMINAL_EVENT_WINDOW_CLOSED` event.
4. If no windows remain and no explicit global stop was requested, the app may choose to call `super_terminal_request_stop`.

### `nativeDispatchEventJson` change

Currently reads `g_active_host`. Must change to:
1. Carry a `window_id` captured at session-attach time (the session knows its owning window).
2. Route the event to the correct window's event queue by looking up the `SuperTerminalAppHost` global (still one app per process).

### Tracker

- [ ] Define `SuperTerminalWindowHost` struct
- [ ] Define `SuperTerminalAppHost` struct
- [ ] Move `RingQueue<SuperTerminalCommand>` and `RingQueue<SuperTerminalEvent>` to AppHost
- [ ] Move per-window fields into `SuperTerminalWindowHost`
- [ ] Implement `initWindowHost(SuperTerminalAppHost*, const SuperTerminalWindowDesc*, SuperTerminalWindowHost**)` 
- [ ] Implement `shutdownWindowHost(SuperTerminalAppHost*, SuperTerminalWindowHost*)`
- [ ] Implement `shutdownAppHost(SuperTerminalAppHost*)`
- [ ] Refactor `renderSurface` → `renderWindowHost(SuperTerminalWindowHost*)`
- [ ] Refactor `drainCommands` → `drainCommands(SuperTerminalAppHost*)` dispatching by window_id
- [ ] Refactor `applyCommand` → `applyCommandToWindow(SuperTerminalWindowHost*, cmd)`
- [ ] Update `pumpMessages` to pump for all windows (single `GetMessageW` loop, natural with Win32)
- [ ] Update `nativeDispatchEventJson` to route via `window_id`
- [ ] Update `hostWindowProc` to look up the correct `SuperTerminalWindowHost` from HWND
- [ ] Implement `SUPERTERMINAL_CMD_CREATE_WINDOW` handling in drain loop
- [ ] Implement `SUPERTERMINAL_CMD_CLOSE_WINDOW` handling in drain loop
- [ ] Add window-lifecycle events: `SUPERTERMINAL_EVENT_WINDOW_CREATED`, `SUPERTERMINAL_EVENT_WINDOW_CLOSED`
- [ ] Update all `super_terminal_*` C API functions to resolve `window_id → window_host` where needed
- [ ] Update `super_terminal_resolve_pane_id_utf8` → `super_terminal_resolve_pane_id_for_window`
- [ ] Update `super_terminal_run` to initialise `SuperTerminalAppHost` (no implicit window; client creates its own)
- [ ] Update `super_terminal_run_hosted_app` similarly
- [ ] Remove `g_active_host` global; replace with `g_active_app` (one per process)
- [ ] Update `super_terminal_enqueue` to post wake message to all open windows (or broadcast via `PostThreadMessageW` to the UI thread)

---

## Phase 5 — Refactor native_ui.cpp for NativeUiSession

See Phase 2 tracker. This phase is the implementation after the header changes.

Key implementation notes:

- The `NativeHostState` struct is **renamed** to `NativeUiSessionState` and lives on the heap inside `WinguiNativeUiSession`.
- Subclass proc `ref_data` parameters currently carry 0; they must carry a pointer to the owning session or to the owning HWND-state block so they can call back into session methods without touching a global.
- `nativeWndProc` receives messages for the session's own window HWND. The HWND's user data (`GWLP_USERDATA`) or a static map can be used to recover `WinguiNativeUiSession*`. The simplest safe approach: a process-wide `std::unordered_map<HWND, WinguiNativeUiSession*>` protected by a mutex, populated at creation time.
- `nativeHostThreadMain` becomes `nativeSessionThreadMain(WinguiNativeUiSession* session)` — runs in a dedicated native-UI thread that owns the message loop for one session's HWND.
- Embedded mode (child HWND, no separate thread) stays supported: `wingui_native_session_attach_embedded_host` is called from the UI thread directly (no separate thread needed) as it is today.

### Tracker

- [ ] Rename `NativeHostState` → `NativeUiSessionState`, move to heap in `WinguiNativeUiSession`
- [ ] Thread all internal `g_native` references through a `NativeUiSessionState&` or `NativeUiSession*` parameter
- [ ] Update `dispatchUiEventJson` to carry session reference
- [ ] Update all subclass procs to capture session pointer via `ref_data`
- [ ] Update `nativeWndProc` to look up session from HWND
- [ ] Rename `nativeHostThreadMain` → `nativeSessionThreadMain(WinguiNativeUiSession*)`
- [ ] Implement HWND → session map for wndproc lookup
- [ ] Implement `wingui_native_session_create`
- [ ] Implement `wingui_native_session_destroy`
- [ ] Implement remaining `wingui_native_session_*` wrappers
- [ ] Remove old global `wingui_native_*` entry points

---

## Phase 6 — Write new multi-window demo

File: `src/demo_multi_window.cpp`

The demo should:

1. Call `super_terminal_run_hosted_app` (app-only desc, no implicit window).
2. In `setup`, call `super_terminal_create_window` twice with different titles and JSON layouts.
3. In `on_event`:
   - Inspect `event.window_id` to determine which window generated the event.
   - Handle close-request for each window independently.
   - Close both windows → request app stop.
4. Optionally render something into each window's panes on `on_frame` to confirm independent rendering.

### Tracker

- [ ] Create `src/demo_multi_window.cpp`
- [ ] Create matching JSON UI specs for each window (inline strings)
- [ ] Wire up event handling per window
- [ ] Test: both windows open, independently closable, app exits when last window closes

---

## Phase 7 — Update spec_bind for window_id

`spec_bind.h` / `spec_bind.cpp` wrap the hosted-app callbacks. They must:

- Carry `window_id` on the `WinguiSpecBindEventView`
- Expose `wingui_spec_bind_create_window` / `_close_window` wrappers
- Expose `wingui_spec_bind_resolve_pane_for_window`

### Tracker

- [ ] Add `window_id` to `WinguiSpecBindEventView`
- [ ] Add `wingui_spec_bind_create_window` / `_close_window`
- [ ] Add `wingui_spec_bind_resolve_pane_for_window`
- [ ] Update `spec_bind.cpp` to route events and commands per window

---

## Phase 8 — Build project files

The vcxproj files reference source files. After archiving demos and adding the new demo, the project files need updating.

- [ ] Remove archived demo files from `wingui_demo.vcxproj` and `wingui_demo_widget_galley.vcxproj`
- [ ] Add `src/demo_multi_window.cpp` to `wingui_demo.vcxproj`
- [ ] Verify `wingui.vcxproj` compiles the refactored `terminal.cpp` and `native_ui.cpp`
- [ ] Do a test build; fix any compile errors

---

## Key invariants (from design doc, preserved throughout)

1. Hosted apps do not directly manipulate `HWND` or renderer objects.
2. Win32 and D3D presentation ownership stays on the UI side.
3. Layout and patching remain declarative.
4. Pane content updates remain fast-path operations.
5. Window close tears down only that window's retained UI and renderers.
6. A failure in one window should not tear down unrelated windows unless the app requests global shutdown.
7. One UI thread for all windows.
8. One client thread for all hosted-app logic.

---

## Open decisions (resolve during implementation)

| # | Question | Decision |
|---|----------|----------|
| 1 | Should `super_terminal_create_window` be synchronous (waits for window to appear) or async (returns immediately, fires WINDOW_CREATED event)? | **Async** — client enqueues command, receives WINDOW_CREATED event with `window_id` before using the window. |
| 2 | Should closing the last window automatically stop the app? | **No** — hosted app decides; it receives WINDOW_CLOSED and can call `request_stop` if it wants. |
| 3 | Frame callbacks: global-with-window-id or per-window registered? | **Global with window_id** in `SuperTerminalFrameTick` for the first implementation. |
| 4 | Should the initial window be created implicitly for compatibility? | **No** — break cleanly, all windows are explicit. Demos are already archived. |
| 5 | How are pane commands routed when a pane_id could collide across windows? | Each `RegisteredPane` is held inside `SuperTerminalWindowHost`; the client must pass `window_id` together with any pane command or the command struct contains the resolved pane_id (which encodes the node_id hash, unique-enough within a session). |
| 6 | Share glyph atlas across windows? | **Yes** — single atlas in `SuperTerminalAppHost`; all windows use the same font for now. |
| 7 | Share sprite atlas packer across windows? | **No** — per-window for now to keep failure domains separate. |

---

## Implementation order

1. Phase 2 header (`native_ui.h` new API) + stub implementations in `native_ui.cpp`
2. Phase 3 header (`terminal.h` new types)
3. Phase 5 (`native_ui.cpp` full session refactor)
4. Phase 4 (`terminal.cpp` AppHost + WindowHost)
5. Phase 7 (`spec_bind` updates)
6. Phase 6 (new demo)
7. Phase 8 (project files)
