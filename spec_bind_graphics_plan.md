# Spec + Bind Graphics Access Plan

## Goal

Make the hosted DirectX pane paths easy to drive from other languages through the C ABI without exposing raw D3D objects or forcing every foreign host to rediscover the low-level `SuperTerminal` command set.

The immediate gaps are:

- Spec + Bind has no public `on_frame` callback even though `SuperTerminalHostedAppDesc` already supports one.
- Foreign-language hosts must currently know about pane ids, frame ticks, buffer indices, and the difference between frame-scoped and non-frame-scoped write APIs.
- The low-level terminal API is already capable, but it is not ergonomic enough to be the primary FFI surface for text-grid, indexed, and RGBA pane work.

This plan keeps the current runtime architecture intact:

- UI thread still owns Win32 and D3D.
- Foreign hosts still talk through a pure C ABI.
- The new layer should be a thin helper layer over `SuperTerminal`, not a second renderer or a second runtime.

## Existing Foundations

The current codebase already has the important primitives.

- Hosted frame hook exists at the `SuperTerminal` layer via `SuperTerminalHostedFrameFn` and `SuperTerminalHostedAppDesc.on_frame`.
- Pane identity and geometry are already queryable through `super_terminal_resolve_pane_id_utf8(...)` and `super_terminal_get_pane_layout(...)`.
- Text-grid, indexed, RGBA, sprite, vector, and asset operations already exist as C functions in `include/wingui/terminal.h`.
- The C++ wrapper in `include/wingui/app.hpp` already demonstrates the ergonomic target: a frame callback receives a frame object, resolves panes by id, and performs simple pane-local operations.

The main conclusion is that we do not need a new rendering substrate. We need a better public C-facing binding layer over the existing one.

## Design Direction

Add a small "frame session" C ABI on top of the current Spec + Bind runtime.

That layer should do three things:

1. Expose hosted `on_frame` to foreign languages.
2. Hide raw `SuperTerminalFrameTick` and current buffer bookkeeping behind stable helper types.
3. Provide pane-oriented helper functions for text-grid, indexed, and RGBA work so foreign hosts can operate by pane id or pane node id without manually stitching together low-level calls.

The design target is the C equivalent of the current C++ shape:

- register a frame callback
- resolve a pane once from its declarative node id
- on each frame, get a pane handle or pane view
- call straightforward helpers like write text cells, clear a region, upload RGBA pixels, upload indexed pixels, draw vector primitives, or blit assets

## Non-Goals

- Do not expose raw `HWND`, `ID3D11Device`, `ID3D11DeviceContext`, or swap-chain pointers through Spec + Bind.
- Do not require foreign hosts to run on the UI thread.
- Do not replace the existing low-level `SuperTerminal` functions; the new layer should wrap them.
- Do not collapse the retained declarative UI path into imperative pane APIs.

## Proposed Public API Shape

## 1. Add a frame callback to Spec + Bind

Extend `include/wingui/spec_bind.h` with a frame callback type and setter.

Suggested shape:

```c
typedef struct WinguiSpecBindFrameView WinguiSpecBindFrameView;

typedef void (WINGUI_CALL *WinguiSpecBindFrameHandlerFn)(
    void* user_data,
    WinguiSpecBindRuntime* runtime,
    const WinguiSpecBindFrameView* frame_view);

WINGUI_API void WINGUI_CALL wingui_spec_bind_runtime_set_frame_handler(
    WinguiSpecBindRuntime* runtime,
    WinguiSpecBindFrameHandlerFn handler,
    void* user_data);
```

Implementation detail:

- `src/spec_bind.cpp` currently sets `hosted.on_frame = nullptr`.
- The first implementation step is to store an optional frame handler on the runtime and wire it into `SuperTerminalHostedAppDesc.on_frame`.

## 2. Introduce an opaque frame view

Do not expose `SuperTerminalClientContext*` directly as the foreign-language frame API. Instead expose an opaque, callback-lifetime frame view.

Suggested responsibilities of `WinguiSpecBindFrameView`:

- hold the active `SuperTerminalClientContext*`
- hold the current `SuperTerminalFrameTick*`
- optionally cache resolved pane ids
- only be valid during the frame callback

Suggested helper queries:

```c
WINGUI_API uint64_t WINGUI_CALL wingui_spec_bind_frame_index(
    const WinguiSpecBindFrameView* frame_view);

WINGUI_API uint64_t WINGUI_CALL wingui_spec_bind_frame_elapsed_ms(
    const WinguiSpecBindFrameView* frame_view);

WINGUI_API uint64_t WINGUI_CALL wingui_spec_bind_frame_delta_ms(
    const WinguiSpecBindFrameView* frame_view);

WINGUI_API uint32_t WINGUI_CALL wingui_spec_bind_frame_buffer_index(
    const WinguiSpecBindFrameView* frame_view);

WINGUI_API uint32_t WINGUI_CALL wingui_spec_bind_frame_active_buffer_index(
    const WinguiSpecBindFrameView* frame_view);
```

These are simple, FFI-safe scalar accessors and they avoid forcing foreign hosts to duplicate the `SuperTerminalFrameTick` layout.

## 3. Add a pane reference type

Foreign hosts should not have to repeatedly do:

- resolve pane id from node id
- fetch layout
- remember current frame buffer index

Introduce a small C struct representing a pane reference within a frame callback.

Suggested shape:

```c
typedef struct WinguiSpecBindPaneRef {
    SuperTerminalPaneId pane_id;
} WinguiSpecBindPaneRef;
```

Suggested lookup helpers:

```c
WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_resolve_pane_utf8(
    const WinguiSpecBindFrameView* frame_view,
    const char* node_id_utf8,
    WinguiSpecBindPaneRef* out_pane);

WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_get_pane_layout(
    const WinguiSpecBindFrameView* frame_view,
    WinguiSpecBindPaneRef pane,
    SuperTerminalPaneLayout* out_layout);
```

Two important policy choices:

- Keep pane refs as value types, not heap objects.
- Resolve by declarative node id first, because that is what foreign hosts naturally know from the JSON spec.

## Ergonomic Helper Layers by Pane Type

The key API choice is whether to expose every existing `super_terminal_*` function directly through the frame view, or to group them by pane type. The better foreign-language shape is grouped helpers.

## A. Text Grid Helpers

Text grids should be the easiest path because they are the simplest pane model.

Minimum helper set:

```c
WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_text_grid_write_cells(
    const WinguiSpecBindFrameView* frame_view,
    WinguiSpecBindPaneRef pane,
    const SuperTerminalTextGridCell* cells,
    uint32_t cell_count);

WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_text_grid_clear_region(
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

Optional ergonomics for later:

- a helper that writes one cell
- a helper that writes a UTF-8 string across a row with uniform styling
- a small text-grid builder or row-writer utility in C

Recommendation: do not add string layout helpers in the first slice. The first slice should expose only the dense cell operations that already exist.

## B. Indexed Graphics Helpers

Indexed graphics needs two tiers.

Tier 1 should mirror the frame upload and simple draw ops already in `terminal.h`:

```c
WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_indexed_upload(
    const WinguiSpecBindFrameView* frame_view,
    WinguiSpecBindPaneRef pane,
    const SuperTerminalIndexedGraphicsFrame* frame);

WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_indexed_fill_rect(
    const WinguiSpecBindFrameView* frame_view,
    WinguiSpecBindPaneRef pane,
    uint32_t x,
    uint32_t y,
    uint32_t width,
    uint32_t height,
    uint32_t palette_index);

WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_indexed_draw_line(
    const WinguiSpecBindFrameView* frame_view,
    WinguiSpecBindPaneRef pane,
    int32_t x0,
    int32_t y0,
    int32_t x1,
    int32_t y1,
    uint32_t palette_index);
```

Tier 2 should target foreign-language ergonomics once Tier 1 is stable:

- indexed sprite helpers
- palette helpers
- optional owned-buffer upload helper variants to reduce copy/lifetime confusion

Important constraint for indexed panes:

- Preserve the existing indexed model, including line palettes and global palette usage.
- Keep index `0` transparent as part of the documented contract when broadening the foreign-language surface.

## C. RGBA Graphics Helpers

RGBA needs two categories: full-frame uploads and higher-level composition helpers.

Minimum helper set:

```c
WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_rgba_upload(
    const WinguiSpecBindFrameView* frame_view,
    WinguiSpecBindPaneRef pane,
    const SuperTerminalRgbaFrame* frame);

WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_vector_draw(
    const WinguiSpecBindFrameView* frame_view,
    WinguiSpecBindPaneRef pane,
    uint32_t content_buffer_mode,
    uint32_t blend_mode,
    int32_t clear_before,
    const float clear_color_rgba[4],
    const WinguiVectorPrimitive* primitives,
    uint32_t primitive_count);

WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_rgba_gpu_copy(
    const WinguiSpecBindFrameView* frame_view,
    WinguiSpecBindPaneRef dst_pane,
    uint32_t dst_x,
    uint32_t dst_y,
    WinguiSpecBindPaneRef src_pane,
    uint32_t src_x,
    uint32_t src_y,
    uint32_t region_width,
    uint32_t region_height,
    uint32_t content_buffer_mode);
```

Notable simplification:

- The helper should infer the correct frame buffer index from `frame_view` rather than forcing foreign hosts to supply it each call.

Optional second step:

- asset registration and blit wrappers
- a primitive-list builder ABI if we find that raw `WinguiVectorPrimitive` arrays are too painful in some languages

## Cross-Cutting Helper APIs

## 1. Glyph atlas access

Text in vector/RGBA panes already depends on glyph atlas info. That needs a frame-view-level accessor.

Suggested helper:

```c
WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_get_glyph_atlas_info(
    const WinguiSpecBindFrameView* frame_view,
    WinguiGlyphAtlasInfo* out_info);
```

That gives foreign hosts the same capability the C++ `Frame` wrapper already uses.

## 2. Request present

If the caller is driving frame-time graphics, it should have a direct helper for explicit present requests.

Suggested helper:

```c
WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_request_present(
    const WinguiSpecBindFrameView* frame_view);
```

Even if the runtime continues to auto-present on frame cadence, this is the right place to expose explicit control.

## 3. Pane layout and cache policy

The API should support a simple usage pattern:

- resolve pane once per callback or once on first use
- fetch layout each frame when geometry matters

Do not bake stale layout pointers into long-lived host objects. Layout should remain query-based and frame-local.

## Recommended Implementation Phases

## Phase 1: Frame callback plumbing

Goal:
Add `on_frame` support to Spec + Bind with no new pane helpers yet.

Deliverables:

- add frame handler storage to `WinguiSpecBindRuntime`
- wire `hosted.on_frame` in `src/spec_bind.cpp`
- define `WinguiSpecBindFrameView`
- expose scalar frame/tick accessors

Exit criteria:

- a C host can receive a frame callback and read frame timing/buffer metadata

## Phase 2: Pane lookup and layout helpers

Goal:
Make pane access discoverable and stable from the frame callback.

Deliverables:

- resolve pane by node id
- query pane layout
- document declarative ids as the contract between JSON spec and frame drawing

Exit criteria:

- a foreign host can find a pane declared in the JSON tree without touching lower-level `SuperTerminal` APIs directly

## Phase 3: Text-grid helper layer

Goal:
Provide the thinnest useful foreign-language graphics path first.

Deliverables:

- frame-scoped text-grid write and clear helpers
- one C sample using a hosted text-grid pane through Spec + Bind

Exit criteria:

- a non-C++ host can animate or update a text-grid pane entirely through the Spec + Bind surface

## Phase 4: RGBA helper layer

Goal:
Unlock the most generally useful pixel path for foreign hosts.

Deliverables:

- RGBA frame upload helper
- vector draw helper
- glyph atlas accessor
- request-present helper

Exit criteria:

- a foreign host can draw a simple animated RGBA pane with no direct use of `SuperTerminalClientContext*`

## Phase 5: Indexed helper layer

Goal:
Expose the retro/indexed pipeline with minimal friction while preserving palette semantics.

Deliverables:

- indexed upload helper
- indexed fill rect helper
- indexed draw line helper
- one indexed sample proving line-palette/global-palette use still works across the C ABI

Exit criteria:

- an FFI host can drive an indexed pane without manually managing command buffer indices

## Phase 6: Foreign-language ergonomics pass

Goal:
Reduce binding friction for languages beyond C and Zig.

Deliverables:

- verify header cleanliness in Zig and one non-C-family FFI target
- identify which value types are awkward for Rust/C#/Python FFI
- add narrowly-scoped helper functions where raw struct passing is too cumbersome

Exit criteria:

- the API can be wrapped naturally in at least one GC language or Rust without ad hoc per-language shims inside the runtime

## Sample and Validation Plan

Add one sample per pane family through Spec + Bind rather than validating only at the raw terminal layer.

Recommended samples:

- text-grid sample: frame-time clock or scrolling log
- RGBA sample: bouncing ball or primitive animation similar to the C++ `wg::Frame` sample
- indexed sample: palette animation, line drawing, or sprite movement

Each sample should prove three things:

- pane node ids resolve correctly from the declarative spec
- frame callback cadence is stable
- updates happen through the new helper layer rather than direct `SuperTerminal` calls in user code

## Why this plan is the right size

The repo already has the hard parts:

- UI-thread ownership model
- hosted frame callback support
- pane registry and layout lookup
- low-level pane command surface

What is missing is only the public ergonomic layer.

So the smallest defensible plan is:

- do not redesign rendering
- do not expose DirectX handles
- do not create a second runtime
- expose `on_frame`
- wrap pane operations in a frame-local C API with stable helper types

That gets foreign-language hosts close to the current C++ `wg::Frame` ergonomics while preserving the architecture that already works.
