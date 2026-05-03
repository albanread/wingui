# Spec + Bind User Guide

## What it is

Spec + Bind is Wingui's plain C ABI for declarative native UI.

It separates the problem into two parts:

- `Spec`: a JSON window description
- `Bind`: a table of callback bindings keyed by event name

The host language owns application state. Wingui owns rendering and native widget integration.

The runtime now covers two related use cases:

- declarative native UI driven by JSON specs plus named event bindings
- hosted frame-time drawing into declarative text-grid, indexed, and RGBA panes through the same C ABI

The naming split is:

- build with `spec_builder`
- run with `spec_bind`

## When to use it

Use Spec + Bind when:

- you want to drive Wingui from C or another FFI-friendly language
- you want the UI description to be serializable as JSON
- you want callbacks to stay in the host language rather than being embedded in the UI description

Keep using the C++ builder when you are writing a native C++ app and want the most ergonomic authoring layer.

For non-C++ hosts, `spec_builder` is the intended authoring-side namespace. Today that mostly means your language builds JSON directly; reusable cross-language builder helpers can grow there without overloading `spec_bind`.

The first concrete `spec_builder` helpers are now validation, canonical-copy, normalization, and diff-to-patch helpers. They are intentionally narrow and mirror the same reconciliation rules used internally by `spec_bind`.

If you want to exercise those authoring helpers directly, use the small C sample in `src/demo_c_spec_builder.c` rather than the runtime demos.

## Files

- Public header: `include/wingui/spec_bind.h`
- Authoring boundary: `include/wingui/spec_builder.h`
- Zig binding: `src/zig/wingui.zig`
- Runtime implementation: `src/spec_bind.cpp`
- Working demo: `src/demo_spec_bind.cpp`
- Plain C demo: `src/demo_c_spec_bind.c`
- Plain C authoring demo: `src/demo_c_spec_builder.c`
- Zig demo: `src/demo_zig_spec_bind.zig`
- Design note: `spec_bind.md`
- Authoring note: `spec_builder.md`

## Runtime model

The public runtime type is opaque:

```c
typedef struct WinguiSpecBindRuntime WinguiSpecBindRuntime;
```

The host flow is:

1. Create a runtime.
2. Build or load a JSON window spec.
3. Bind one or more event names to callbacks.
4. Optionally bind a frame callback if the spec contains hosted panes you want to draw into.
4. Load the spec into the runtime.
5. Run the hosted app.
6. In event callbacks, update host state and call `wingui_spec_bind_runtime_load_spec_json(...)` again with the new full spec.
7. In frame callbacks, resolve pane ids from declarative node ids and call the frame-scoped text-grid, indexed, RGBA, sprite, or convenience draw helpers.

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

### Frame callback binding

```c
typedef struct WinguiSpecBindFrameView WinguiSpecBindFrameView;

typedef void (WINGUI_CALL *WinguiSpecBindFrameHandlerFn)(
  void* user_data,
  WinguiSpecBindRuntime* runtime,
  const WinguiSpecBindFrameView* frame_view);

void wingui_spec_bind_runtime_set_frame_handler(
  WinguiSpecBindRuntime* runtime,
  WinguiSpecBindFrameHandlerFn handler,
  void* user_data);
```

- The frame handler is optional.
- If present, it runs on the hosted UI/render thread during the normal frame loop.
- The frame callback is where a foreign host performs frame-time drawing into declarative panes.
- The `frame_view` object is callback-local and should not be cached after the callback returns.

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

## Frame and pane access

Once a frame callback is registered, the host can use `WinguiSpecBindFrameView` and `WinguiSpecBindPaneRef` to work with pane-local graphics APIs.

### Frame metadata

```c
uint64_t wingui_spec_bind_frame_index(
  const WinguiSpecBindFrameView* frame_view);

uint64_t wingui_spec_bind_frame_elapsed_ms(
  const WinguiSpecBindFrameView* frame_view);

uint64_t wingui_spec_bind_frame_delta_ms(
  const WinguiSpecBindFrameView* frame_view);

uint32_t wingui_spec_bind_frame_target_frame_ms(
  const WinguiSpecBindFrameView* frame_view);

uint32_t wingui_spec_bind_frame_buffer_index(
  const WinguiSpecBindFrameView* frame_view);

uint32_t wingui_spec_bind_frame_active_buffer_index(
  const WinguiSpecBindFrameView* frame_view);

uint32_t wingui_spec_bind_frame_buffer_count(
  const WinguiSpecBindFrameView* frame_view);
```

These let a foreign host inspect timing and current buffer state without depending on the raw `SuperTerminalFrameTick` layout.

### Resolving panes from the JSON spec

```c
typedef struct WinguiSpecBindPaneRef {
  SuperTerminalPaneId pane_id;
  uint32_t buffer_index;
  uint32_t active_buffer_index;
} WinguiSpecBindPaneRef;

int32_t wingui_spec_bind_frame_resolve_pane_utf8(
  const WinguiSpecBindFrameView* frame_view,
  const char* node_id_utf8,
  WinguiSpecBindPaneRef* out_pane);

int32_t wingui_spec_bind_frame_bind_pane(
  const WinguiSpecBindFrameView* frame_view,
  SuperTerminalPaneId pane_id,
  WinguiSpecBindPaneRef* out_pane);

int32_t wingui_spec_bind_frame_get_pane_layout(
  const WinguiSpecBindFrameView* frame_view,
  WinguiSpecBindPaneRef pane,
  SuperTerminalPaneLayout* out_layout);
```

Recommended usage:

1. Give your text-grid, indexed, or RGBA node a stable `id` in the JSON window spec.
2. In the frame callback, call `wingui_spec_bind_frame_resolve_pane_utf8(...)` with that node id.
3. Use the returned `WinguiSpecBindPaneRef` with the frame-scoped graphics helpers.

`WinguiSpecBindPaneRef` is a value type, not a heap object. It carries the current frame's buffer indices so the helper layer can route operations correctly.

### Shared frame helpers

```c
int32_t wingui_spec_bind_frame_request_present(
  const WinguiSpecBindFrameView* frame_view);

int32_t wingui_spec_bind_frame_get_glyph_atlas_info(
  const WinguiSpecBindFrameView* frame_view,
  WinguiGlyphAtlasInfo* out_info);
```

- `request_present` explicitly asks the host to present.
- `get_glyph_atlas_info` is useful when doing glyph or text layout in RGBA vector content.

## Event contract

For native UI events, the runtime extracts three pieces of information:

- `event_name_utf8`: the JSON `event` field
- `payload_json_utf8`: the full JSON event payload
- `source_utf8`: the JSON `source` field when present

The runtime also exposes two reserved pseudo-events:

- `__close_requested`
- `__host_stopping`

If no handler is bound for close requests, the runtime falls back to `request_stop(0)`.

## Graphics APIs through Spec + Bind

The frame helper layer now exposes the underlying pane graphics capabilities directly through the Spec + Bind C ABI.

### Text-grid helpers

```c
int32_t wingui_spec_bind_frame_text_grid_write_cells(
  const WinguiSpecBindFrameView* frame_view,
  WinguiSpecBindPaneRef pane,
  const SuperTerminalTextGridCell* cells,
  uint32_t cell_count);

int32_t wingui_spec_bind_frame_text_grid_clear_region(
  const WinguiSpecBindFrameView* frame_view,
  WinguiSpecBindPaneRef pane,
  uint32_t row,
  uint32_t column,
  uint32_t width,
  uint32_t height,
  uint32_t fill_codepoint,
  WinguiGraphicsColour foreground,
  WinguiGraphicsColour background);
```

Use these when the declarative spec includes a `text-grid` pane.

Current implementation note:

- `wingui_spec_bind_frame_text_grid_write_cells(...)` and `wingui_spec_bind_frame_text_grid_clear_region(...)` target the current frame buffer.
- Use these in frame callbacks when text is part of a frame-driven surface and should follow the same buffer-index semantics as other frame-local pane drawing.
- `wingui_spec_bind_runtime_text_grid_write_cells(...)` and `wingui_spec_bind_runtime_text_grid_clear_region(...)` are the retained-style alternatives for updates outside the frame callback; those follow the mirrored text-buffer path and should be called again only when the retained text changes.

### Indexed pane helpers

```c
int32_t wingui_spec_bind_frame_indexed_graphics_upload(
  const WinguiSpecBindFrameView* frame_view,
  WinguiSpecBindPaneRef pane,
  const SuperTerminalIndexedGraphicsFrame* frame);

int32_t wingui_spec_bind_frame_indexed_fill_rect(
  const WinguiSpecBindFrameView* frame_view,
  WinguiSpecBindPaneRef pane,
  uint32_t x,
  uint32_t y,
  uint32_t width,
  uint32_t height,
  uint32_t palette_index);

int32_t wingui_spec_bind_frame_indexed_draw_line(
  const WinguiSpecBindFrameView* frame_view,
  WinguiSpecBindPaneRef pane,
  int32_t x0,
  int32_t y0,
  int32_t x1,
  int32_t y1,
  uint32_t palette_index);
```

These cover the common indexed use cases directly.

### RGBA and vector helpers

```c
int32_t wingui_spec_bind_frame_rgba_upload(
  const WinguiSpecBindFrameView* frame_view,
  WinguiSpecBindPaneRef pane,
  const SuperTerminalRgbaFrame* frame);

int32_t wingui_spec_bind_frame_rgba_gpu_copy(
  const WinguiSpecBindFrameView* frame_view,
  WinguiSpecBindPaneRef dst_pane,
  uint32_t dst_x,
  uint32_t dst_y,
  WinguiSpecBindPaneRef src_pane,
  uint32_t src_x,
  uint32_t src_y,
  uint32_t region_width,
  uint32_t region_height);

int32_t wingui_spec_bind_frame_vector_draw(
  const WinguiSpecBindFrameView* frame_view,
  WinguiSpecBindPaneRef pane,
  uint32_t content_buffer_mode,
  uint32_t blend_mode,
  int32_t clear_before,
  const float clear_color_rgba[4],
  const WinguiVectorPrimitive* primitives,
  uint32_t primitive_count);
```

These are the lower-level RGBA drawing entry points.

### RGBA convenience draw calls

To make foreign-language drawing simpler, Spec + Bind now also includes one-call helpers for common vector shapes and text:

```c
int32_t wingui_spec_bind_frame_draw_line(...);
int32_t wingui_spec_bind_frame_fill_rect(...);
int32_t wingui_spec_bind_frame_stroke_rect(...);
int32_t wingui_spec_bind_frame_fill_circle(...);
int32_t wingui_spec_bind_frame_stroke_circle(...);
int32_t wingui_spec_bind_frame_draw_arc(...);
int32_t wingui_spec_bind_frame_draw_text_utf8(...);
```

These helpers:

- work on `WinguiSpecBindPaneRef`
- use the current frame buffer automatically
- let a plain C caller draw lines, rectangles, circles, arcs, and text without building `WinguiVectorPrimitive` arrays by hand

If you need more control, you can still build your own `WinguiVectorPrimitive` array and call `wingui_spec_bind_frame_vector_draw(...)` directly.

### Assets and sprites

```c
int32_t wingui_spec_bind_frame_register_rgba_asset_owned(...);
int32_t wingui_spec_bind_frame_asset_blit_to_pane(...);
int32_t wingui_spec_bind_frame_define_sprite(...);
int32_t wingui_spec_bind_frame_render_sprites(...);
```

These expose the existing asset/sprite path through the same frame-local C ABI.

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

## Minimal frame drawing pattern

This is the intended shape for hosted graphics:

```c
static void WINGUI_CALL on_frame(
  void* user_data,
  WinguiSpecBindRuntime* runtime,
  const WinguiSpecBindFrameView* frame_view) {
  DemoState* state = (DemoState*)user_data;
  WinguiSpecBindPaneRef canvas;
  const float clear[4] = { 0.05f, 0.06f, 0.08f, 1.0f };

  (void)runtime;

  if (!wingui_spec_bind_frame_resolve_pane_utf8(frame_view, "canvas", &canvas)) {
    return;
  }

  state->x += state->vx;
  state->y += state->vy;

  wingui_spec_bind_frame_fill_circle(
    frame_view,
    canvas,
    SUPERTERMINAL_RGBA_CONTENT_BUFFER_PERSISTENT,
    WINGUI_RGBA_BLIT_ALPHA_OVER,
    wingui_spec_bind_frame_index(frame_view) == 0,
    clear,
    state->x,
    state->y,
    24.0f,
    0.95f,
    0.35f,
    0.20f,
    1.0f);

  wingui_spec_bind_frame_draw_text_utf8(
    frame_view,
    canvas,
    SUPERTERMINAL_RGBA_CONTENT_BUFFER_PERSISTENT,
    WINGUI_RGBA_BLIT_ALPHA_OVER,
    0,
    clear,
    "Spec + Bind frame drawing",
    8.0f,
    8.0f,
    0.85f,
    0.85f,
    0.90f,
    1.0f);
}

int run_demo(void) {
  WinguiSpecBindRuntime* runtime = NULL;
  DemoState state = {0};

  if (!wingui_spec_bind_runtime_create(&runtime)) {
    return 1;
  }

  wingui_spec_bind_runtime_set_frame_handler(runtime, on_frame, &state);
  wingui_spec_bind_runtime_load_spec_json(runtime, build_spec_json(&state));
  /* ...then call wingui_spec_bind_runtime_run(...) ... */
}
```

The important part is the frame flow:

- the JSON spec declares the pane with a stable `id`
- the frame callback resolves that pane from its id
- the callback uses frame-scoped draw helpers to render into it

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

If you want frame-time graphics, the body can also contain declarative pane nodes such as:

- `text-grid`
- `indexed-graphics`
- `rgba-pane`

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
- `src/demo_zig_spec_bind.zig`: Zig host code using the `src/zig/wingui.zig` wrapper instead of calling the raw C ABI directly

The Zig binding is intentionally kept outside `src` because it is a reusable host-side library surface, not demo code. It wraps the string-copy probe pattern, converts last-error access into plain Zig slices, and exposes typed runtime/event/frame helpers so ordinary Zig host code does not need to call `wingui_spec_bind_*` and `wingui_spec_builder_*` functions directly.

Together they show:

- building a full JSON spec in host code
- binding handlers for `name`, `notes`, `enabled`, `counter_up`, `counter_reset`, `menu_reset_demo`, and `menu_exit`
- republishing a full new spec whenever state changes
- driving menu bar, command bar, status bar, and form controls from the same host state

The new frame and graphics helper APIs are available now, but the repository does not yet have a dedicated Spec + Bind sample whose primary purpose is to demonstrate hosted frame drawing in C or Zig.

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
- calling frame-scoped helpers outside a valid frame callback
- resolving a pane id that is not present in the current JSON spec
- missing packaged runtime assets when launching the demo directly

## Current limitations

The current implementation is intentionally small.

- There is no separate reusable binding-set object yet.
- There is no JSON patch helper yet.
- There are no built-in UTF-8 file load/save helpers yet.
- JSON validation is currently shallow: the runtime checks for a valid JSON object, but it does not fully validate the schema.
- There is not yet a dedicated reference sample focused on the new frame drawing helper layer.

## Recommended practice

- Keep the full application state in the host language.
- Treat the JSON spec as a rendered view of that state.
- Use stable event-name strings.
- Use stable pane `id` values for any text-grid, indexed, or RGBA pane you want to address from the frame callback.
- Rebuild and republish the full spec after each meaningful state change.
- Use the repository build script for demos so `wingui.dll` and shader assets stay in sync.
- Use the convenience frame draw helpers first, and drop to raw `WinguiVectorPrimitive` arrays only when you need a custom primitive batch.

## Next likely additions

- a reusable binding-set object
- JSON patch support
- file-based spec load/save helpers
- a foreign-language sample
- a dedicated frame-drawing sample for text-grid, indexed, and RGBA panes