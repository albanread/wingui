# Spec + Bind User Guide

## What it is

Spec + Bind is Wingui's plain C ABI for declarative native UI.

It separates the problem into two parts:

- `Spec`: a JSON window description
- `Bind`: a table of callback bindings keyed by event name

The host language owns application state. Wingui owns rendering and native widget integration.

## When to use it

Use Spec + Bind when:

- you want to drive Wingui from C or another FFI-friendly language
- you want the UI description to be serializable as JSON
- you want callbacks to stay in the host language rather than being embedded in the UI description

Keep using the C++ builder when you are writing a native C++ app and want the most ergonomic authoring layer.

## Files

- Public header: `include/wingui/spec_bind.h`
- Runtime implementation: `src/spec_bind.cpp`
- Working demo: `src/demo_spec_bind.cpp`
- Plain C demo: `src/demo_c_spec_bind.c`
- Zig demo: `src/demo_zig_spec_bind.zig`
- Design note: `spec_bind.md`

## Runtime model

The public runtime type is opaque:

```c
typedef struct WinguiSpecBindRuntime WinguiSpecBindRuntime;
```

The host flow is:

1. Create a runtime.
2. Build or load a JSON window spec.
3. Bind one or more event names to callbacks.
4. Load the spec into the runtime.
5. Run the hosted app.
6. In callbacks, update host state and call `wingui_spec_bind_runtime_load_spec_json(...)` again with the new full spec.

That last step is the key design point: the host repaints by publishing a new full spec, not by attaching behavior to nodes.

## Public API

The current public surface is declared in `include/wingui/spec_bind.h`.

### Lifetime

```c
int32_t wingui_spec_bind_runtime_create(WinguiSpecBindRuntime** out_runtime);
void wingui_spec_bind_runtime_destroy(WinguiSpecBindRuntime* runtime);
```

- `create` returns nonzero on success.
- `destroy` frees the runtime and any bound handler table.

### Spec management

```c
int32_t wingui_spec_bind_runtime_load_spec_json(
    WinguiSpecBindRuntime* runtime,
    const char* json_utf8);

int32_t wingui_spec_bind_runtime_copy_spec_json(
    WinguiSpecBindRuntime* runtime,
    char* buffer_utf8,
    uint32_t buffer_size,
    uint32_t* out_required_size);
```

- `load_spec_json` parses and stores a full JSON object.
- Before `run`, it only updates the stored spec.
- During `run`, it applies a UI patch when the diff engine can reconcile the change set, and falls back to a full publish when it cannot.
- `copy_spec_json` lets the host retrieve the currently stored full spec.

### Binding

```c
typedef struct WinguiSpecBindEventView {
    const char* event_name_utf8;
    const char* payload_json_utf8;
    const char* source_utf8;
} WinguiSpecBindEventView;

typedef void (WINGUI_CALL *WinguiSpecBindEventHandlerFn)(
    void* user_data,
    WinguiSpecBindRuntime* runtime,
    const WinguiSpecBindEventView* event_view);

int32_t wingui_spec_bind_runtime_bind_event(
    WinguiSpecBindRuntime* runtime,
    const char* event_name_utf8,
    WinguiSpecBindEventHandlerFn handler,
    void* user_data);

int32_t wingui_spec_bind_runtime_unbind_event(
    WinguiSpecBindRuntime* runtime,
    const char* event_name_utf8);

void wingui_spec_bind_runtime_clear_bindings(
    WinguiSpecBindRuntime* runtime);

void wingui_spec_bind_runtime_set_default_handler(
    WinguiSpecBindRuntime* runtime,
    WinguiSpecBindEventHandlerFn handler,
    void* user_data);
```

- Bindings are keyed by the JSON `event` string from the UI payload.
- Rebinding the same event name replaces the old handler.
- The default handler is only used when no exact event-name binding is found.

### Run and shutdown

```c
typedef struct WinguiSpecBindRunDesc {
    const char* title_utf8;
    uint32_t columns;
    uint32_t rows;
    uint32_t flags;
    uint32_t command_queue_capacity;
    uint32_t event_queue_capacity;
    const char* font_family_utf8;
    int32_t font_pixel_height;
    float dpi_scale;
    const char* text_shader_path_utf8;
    uint32_t target_frame_ms;
    int32_t auto_request_present;
} WinguiSpecBindRunDesc;

int32_t wingui_spec_bind_runtime_request_stop(
    WinguiSpecBindRuntime* runtime,
    int32_t exit_code);

int32_t wingui_spec_bind_runtime_get_patch_metrics(
  WinguiSpecBindRuntime* runtime,
  SuperTerminalNativeUiPatchMetrics* out_metrics);

int32_t wingui_spec_bind_runtime_run(
    WinguiSpecBindRuntime* runtime,
    const WinguiSpecBindRunDesc* desc,
    SuperTerminalRunResult* out_result);
```

- `run` enters the hosted Wingui event loop.
- `request_stop` stops the active runtime from inside a callback.
- `get_patch_metrics` returns the host-side native UI patch counters for the active runtime.
- `out_result` receives the host exit code and host error status.

## Event contract

For native UI events, the runtime extracts three pieces of information:

- `event_name_utf8`: the JSON `event` field
- `payload_json_utf8`: the full JSON event payload
- `source_utf8`: the JSON `source` field when present

The runtime also exposes two reserved pseudo-events:

- `__close_requested`
- `__host_stopping`

If no handler is bound for close requests, the runtime falls back to `request_stop(0)`.

## Minimal host pattern

This is the intended loop in any host language:

```c
static void WINGUI_CALL on_counter_up(
    void* user_data,
    WinguiSpecBindRuntime* runtime,
    const WinguiSpecBindEventView* event_view) {
    DemoState* state = (DemoState*)user_data;
    state->clicks += 1;
    state->spec_json = build_spec_json(state);
    wingui_spec_bind_runtime_load_spec_json(runtime, state->spec_json);
}

int run_demo(void) {
    WinguiSpecBindRuntime* runtime = NULL;
    DemoState state = {0};
    SuperTerminalRunResult result = {0};

    if (!wingui_spec_bind_runtime_create(&runtime)) {
        return 1;
    }

    state.spec_json = build_spec_json(&state);

    wingui_spec_bind_runtime_bind_event(runtime, "counter_up", on_counter_up, &state);
    wingui_spec_bind_runtime_load_spec_json(runtime, state.spec_json);

    if (!wingui_spec_bind_runtime_run(runtime, NULL, &result)) {
        wingui_spec_bind_runtime_destroy(runtime);
        return 1;
    }

    wingui_spec_bind_runtime_destroy(runtime);
    return result.exit_code;
}
```

The important part is not the exact code. The important part is the state flow:

- callback updates host state
- host rebuilds full JSON spec
- host republishes the full spec

## JSON shape

The JSON schema is the same declarative UI model used by the C++ builder.

At the top level, the runtime expects a JSON object describing a `window`.

Typical top-level fields include:

- `type`
- `title`
- `menuBar`
- `commandBar`
- `statusBar`
- `body`

Example:

```json
{
  "type": "window",
  "title": "Spec + Bind Demo",
  "menuBar": {
    "menus": [
      {
        "text": "File",
        "items": [
          { "id": "menu_exit", "text": "Exit" }
        ]
      }
    ]
  },
  "body": {
    "type": "stack",
    "gap": 12,
    "children": [
      {
        "type": "input",
        "label": "Name",
        "value": "Ada Lovelace",
        "event": "name"
      },
      {
        "type": "button",
        "text": "Count click",
        "event": "counter_up"
      }
    ]
  }
}
```

## Working examples in this repo

There are now two reference implementations:

- `src/demo_spec_bind.cpp`: C++ host code using the C ABI runtime
- `src/demo_c_spec_bind.c`: plain C host code using the same API
- `src/demo_zig_spec_bind.zig`: Zig host code using the same API through `@cImport`

Together they show:

- building a full JSON spec in host code
- binding handlers for `name`, `notes`, `enabled`, `counter_up`, `counter_reset`, `menu_reset_demo`, and `menu_exit`
- republishing a full new spec whenever state changes
- driving menu bar, command bar, status bar, and form controls from the same host state

## Build and run

Use the repo build script so the packaged output is complete:

```powershell
Set-Location "c:\projects\wingui"
powershell -NoProfile -ExecutionPolicy Bypass -File ".\build_demo_impl.ps1" demo_spec_bind.cpp Release
```

For the plain C sample:

```powershell
Set-Location "c:\projects\wingui"
powershell -NoProfile -ExecutionPolicy Bypass -File ".\build_demo_impl.ps1" demo_c_spec_bind.c Release
```

For the Zig sample, first make sure `manual_build\out\wingui.lib`, `wingui.dll`, and `shaders\*` already exist, then build the Zig executable:

```powershell
Set-Location "c:\projects\wingui"
powershell -NoProfile -ExecutionPolicy Bypass -File ".\build_zig_spec_bind.ps1" Release
```

That produces:

- `manual_build\out\wingui.dll`
- `manual_build\out\wingui.lib`
- `manual_build\out\wingui_demo_spec_bind.exe`
- `manual_build\out\wingui_demo_c_spec_bind.exe`
- `manual_build\out\wingui_demo_zig_spec_bind.exe`
- `manual_build\out\shaders\*`

Run it from the repo root:

```powershell
Set-Location "c:\projects\wingui"
& ".\manual_build\out\wingui_demo_spec_bind.exe"
```

Or double-click `manual_build\out\wingui_demo_spec_bind.exe` in Explorer.

## Important packaging note

Explorer launch depends on the packaged output folder being complete.

The demo needs all of these beside the executable:

- `wingui_demo_spec_bind.exe`
- `wingui.dll`
- `shaders\*.hlsl`

If `manual_build\out\shaders` is missing, the demo can fail immediately on startup and appear to just beep and exit.

The reliable fix is to rebuild with `build_demo_impl.ps1`, because the script repopulates the full packaged output folder.

## Error handling

Most Spec + Bind APIs return nonzero on success and `0` on failure.

On failure, inspect:

```c
const char* message = wingui_last_error_utf8();
```

Common failure cases:

- `runtime was null`
- invalid or empty JSON passed to `load_spec_json`
- trying to stop a runtime that is not active
- missing packaged runtime assets when launching the demo directly

## Current limitations

The current implementation is intentionally small.

- There is no separate reusable binding-set object yet.
- There is no JSON patch helper yet.
- There are no built-in UTF-8 file load/save helpers yet.
- JSON validation is currently shallow: the runtime checks for a valid JSON object, but it does not fully validate the schema.

## Recommended practice

- Keep the full application state in the host language.
- Treat the JSON spec as a rendered view of that state.
- Use stable event-name strings.
- Rebuild and republish the full spec after each meaningful state change.
- Use the repository build script for demos so `wingui.dll` and shader assets stay in sync.

## Next likely additions

- a reusable binding-set object
- JSON patch support
- file-based spec load/save helpers
- a plain C sample
- a foreign-language sample