# Wingui Multi-Window Design

## Purpose

This document outlines a practical path from the current single-window `SuperTerminal` model to a multi-window design where:

- each top-level window owns its own native layout session
- each top-level window owns its own renderers and D3D context
- the same declarative layout engine and custom pane controls are reused across windows
- hosted applications can create, update, focus, and close windows explicitly

The design is intended to preserve the current strengths of the framework:

- Win32 and Direct3D ownership stay on the UI side
- hosted applications do not touch `HWND` or renderer objects directly
- the layout system remains declarative
- text-grid, indexed, and RGBA panes remain fast-path presentation surfaces inside the layout tree

## Current Model

The current runtime is effectively single-windowed.

- `SuperTerminalRuntimeHost` owns one `WinguiWindow*`
- the native UI layer is centered around one global host/session state
- pane lookup and event routing assume one active host
- hosted applications target one implicit top-level window

This is sufficient for the current demos, but it prevents the framework from treating windows as first-class objects.

## Target Model

The multi-window design should treat a top-level window as a runtime-owned object with its own retained UI and rendering state.

The target hierarchy is:

1. `SuperTerminalAppHost`
2. `SuperTerminalWindowHost`
3. `NativeUiSession`
4. pane presenters owned by that window

In that model:

- the app host owns process-wide lifecycle, global routing, and shared services
- each window host owns one Win32 top-level window and one layout session
- each window host owns its own renderers and pane registry
- events and commands are routed by `window_id`
- hosted apps can manage multiple windows without being given raw platform objects

## Core Design Principle

The major change is not the custom controls. The controls and layout engine are already reusable.

The major change is ownership.

Today, layout state and runtime state are singleton/global. The multi-window design makes them session-scoped and window-scoped instead.

That means the same layout and control code can continue to exist, but it must resolve state through a specific window/session instead of through global variables.

## Runtime Structure

### 1. `SuperTerminalAppHost`

This should become the process-level coordination root.

Responsibilities:

- own the list/map of open windows
- allocate stable `window_id` values
- own app-level command and event routing
- own shutdown policy across all windows
- own client-thread lifecycle
- own shared services that are not tied to one `HWND`

Likely contents:

- `std::unordered_map<uint64_t, SuperTerminalWindowHost>`
- next window id counter
- app command queue and event queue metadata
- global stop/error state
- client thread handle
- optional shared immutable assets and caches

### 2. `SuperTerminalWindowHost`

This should become the unit of UI ownership.

Each window host owns:

- one `WinguiWindow*`
- one native UI session
- one pane registry for panes declared in that window's layout
- one focus model
- one render-dirty bit
- one layout root and window-local retained spec

Each window host should also own its own renderers and contexts for anything tied to that window's `HWND` tree.

That includes:

- text-grid presenters attached to pane child windows
- RGBA presenters attached to pane child windows
- indexed presenters attached to pane child windows
- any top-level window renderers that remain necessary

Recommended rule:

- if an object depends on a window handle, swap chain, child host, or viewport bounds, it is window-local

### 3. `NativeUiSession`

The current native UI singleton should be turned into a heap object representing one embedded declarative layout session.

Each session should own:

- the published JSON spec for one top-level window
- the `node_id -> HWND` map for that window
- bindings, menu state, scroll state, command bar state, focus state, and event queue for that window
- the root host bounds and content host for that window

The same layout code can be reused if it is parameterized on `NativeUiSession&` rather than reading global state.

### 4. Pane presenters

Pane presenters should remain per-pane, but the containing registry should be per-window.

That means:

- a text-grid pane presenter belongs to one `SuperTerminalWindowHost`
- an RGBA pane presenter belongs to one `SuperTerminalWindowHost`
- an indexed pane presenter belongs to one `SuperTerminalWindowHost`

This keeps the current renderer ownership model simple and avoids cross-window D3D state sharing in the first implementation.

## Required API Direction

The public runtime will need explicit window identities.

The main shift is from implicit single-host APIs to explicit window-scoped APIs.

Examples:

- create window
- close window
- publish layout for window
- patch layout for window
- resolve pane id in window
- write retained text to pane in window
- submit frame updates to pane in window
- query layout/focus/mouse state for window

Representative C-style API shape:

```c
typedef struct SuperTerminalWindowId {
    uint64_t value;
} SuperTerminalWindowId;

int32_t super_terminal_create_window(
    SuperTerminalClientContext* ctx,
    const SuperTerminalWindowDesc* desc,
    SuperTerminalWindowId* out_window_id);

int32_t super_terminal_close_window(
    SuperTerminalClientContext* ctx,
    SuperTerminalWindowId window_id);

int32_t super_terminal_publish_ui_json_for_window(
    SuperTerminalClientContext* ctx,
    SuperTerminalWindowId window_id,
    const char* json_utf8);

int32_t super_terminal_patch_ui_json_for_window(
    SuperTerminalClientContext* ctx,
    SuperTerminalWindowId window_id,
    const char* patch_json_utf8);

int32_t super_terminal_resolve_pane_id_utf8_for_window(
    SuperTerminalClientContext* ctx,
    SuperTerminalWindowId window_id,
    const char* node_id_utf8,
    SuperTerminalPaneId* out_pane_id);
```

The same pattern should apply to the spec-bind and Zig wrappers.

## Layout Semantics

Each top-level window should carry its own declarative layout tree.

That does not mean every window must have a different spec. It means the runtime treats each window's spec as independent retained state.

This enables:

- one app opening two identical editor windows with different documents
- one app opening a tool window with the same controls but different state
- one app reusing the same pane/control definitions across multiple windows
- one window being patched or relaid out without affecting other windows

Recommended semantic rule:

- `node_id` uniqueness only needs to hold within a window session, not globally across the entire app

The runtime should therefore treat pane resolution as:

- `window_id + node_id -> pane_id`

rather than:

- `node_id -> pane_id`

## Renderer Ownership

The first multi-window implementation should use per-window renderer ownership.

That means:

- one window host creates and destroys its own renderer objects
- presenters are recreated only for panes in that window
- window close tears down only that window's D3D resources

This is the safest first design because:

- swap chains and render targets are naturally window-bound
- child pane hosts are window-bound
- the failure domain stays small
- shutdown order is easier to reason about

Possible later optimization:

- share immutable assets such as decoded atlases or CPU-side sprite/image blobs across windows

That optimization should wait until correctness is established.

## Event Routing

All app-visible events should include `window_id`.

That includes:

- native control events
- pane focus events
- keyboard events
- mouse events
- resize/layout events
- close requests

Representative event shape:

```c
typedef struct SuperTerminalEvent {
    SuperTerminalWindowId window_id;
    SuperTerminalEventType type;
    union {
        ...
    } data;
} SuperTerminalEvent;
```

This keeps the hosted app model explicit: when an app receives an event, it knows which window it belongs to.

## Hosted Application Implications

Hosted applications will need to manage windows as explicit state objects instead of assuming one implicit shell.

The main implications are:

### 1. Applications need their own window model

The app should maintain a table keyed by `window_id`.

For example:

- current document per window
- current tool mode per window
- pane ids resolved for that window
- last known layout bounds per window
- per-window REPL/editor/graphics state

The framework should not try to hide this. Multi-window applications already need this distinction.

### 2. Pane ids should be treated as window-local handles

Hosted apps should not assume a pane id resolved in one window is meaningful in another window, even if the layout uses the same `node_id` strings.

### 3. Window lifecycle becomes part of app logic

Hosted apps need to handle:

- creating secondary windows
- deciding whether close means close-one-window or exit-app
- tracking the last main window
- deciding whether to reuse an existing window or open a new one

### 4. Frame handlers may need a per-window view

If the runtime continues to support frame-driven panes, the callback contract should tell the app which window is being serviced.

For example:

- one global frame callback that receives `window_id`
- or one registered frame handler per window

The simpler first approach is one global callback with `window_id`.

## Threading Recommendation

### Short answer

Do not introduce a per-app thread for each window in the first multi-window design.

Recommended first model:

- one UI thread for all windows
- one client/application thread for app logic
- optional worker threads created by the application for expensive tasks

### Why not one app thread per window?

Per-window app threads create more problems than they solve in the initial design.

Problems introduced by per-window app threads:

- much harder shutdown semantics
- cross-window coordination becomes lock-heavy
- documents/shared state need synchronization across client threads
- event ordering becomes harder to reason about
- background tasks may race when updating shared app models
- the hosted app API becomes significantly more complex

Most multi-window desktop apps do not require one application logic thread per window. They require one app model that can target multiple windows.

### Why one UI thread for all windows?

Win32 already supports multiple top-level windows on one UI/message-pump thread.

Benefits:

- message pumping stays simple
- D3D ownership stays concentrated on one thread
- focus and activation transitions remain straightforward
- host teardown is easier to centralize

This also preserves the current framework philosophy: the hosted application is not a GUI-thread programmer.

### When would per-window UI threads make sense?

Only consider per-window UI threads later if there is a proven need such as:

- one window can stall rendering badly enough to hurt others
- one window embeds unusually heavy platform components that require isolation
- the app needs independent responsiveness or crash isolation per window

That should be treated as an advanced mode, not the baseline architecture.

### Recommended concurrency model for hosted apps

The baseline hosted-app model should be:

- one client thread owns the authoritative app state
- that thread processes events for all windows
- it emits commands tagged with `window_id`
- app-created worker threads may do I/O, compilation, or asset processing, but they should still marshal UI changes back through the main client thread or directly through the runtime queue with explicit window ids

This keeps windowing scalable without multiplying ownership domains.

## Migration Plan

The safest path is incremental.

### Phase 1: isolate native UI session state

- replace the native UI singleton with a `NativeUiSession` object
- thread session references through layout, patch, and lookup code
- keep the external runtime effectively single-window while removing global assumptions internally

Outcome:

- same behavior as today, but native layout state is no longer global

### Phase 2: split app host from window host

- introduce `SuperTerminalAppHost`
- rename the current `SuperTerminalRuntimeHost` responsibilities into `SuperTerminalWindowHost`
- move window-local fields into the window host
- keep one window open initially, but use the new types internally

Outcome:

- the architecture now matches the intended ownership boundaries

### Phase 3: add explicit `window_id`

- introduce stable window ids
- add window id to events and relevant commands
- make pane resolution window-scoped
- update wrappers and helpers to carry window context

Outcome:

- hosted apps can distinguish windows explicitly even before opening several of them

### Phase 4: add create/close window commands

- add runtime commands to create and close windows
- let hosted apps open a second top-level window with its own layout/session
- keep one client thread and one UI thread

Outcome:

- first usable multi-window runtime

### Phase 5: update demos and spec-bind wrappers

- add a simple multi-window demo
- add wrapper helpers for creating a window and publishing a layout to it
- demonstrate same layout code reused across multiple windows

Outcome:

- the public model is validated end-to-end

### Phase 6: consider shared immutable assets

- measure whether per-window renderer ownership is sufficient
- if needed, share immutable CPU-side asset caches
- keep D3D presentation objects window-local unless profiling proves otherwise

Outcome:

- optimize only after the design is correct and stable

## Invariants To Preserve

The multi-window design should preserve these rules:

1. Hosted apps do not directly manipulate `HWND` or renderer objects.
2. Win32 and D3D presentation ownership stays on the UI side.
3. Layout and patching remain declarative.
4. Pane content updates remain fast-path operations.
5. Window close tears down only that window's retained UI and renderers.
6. A failure in one window should not require tearing down unrelated windows unless the app requests global shutdown.

## Recommended First Implementation Decision

The recommended first implementation is:

- one app host
- many window hosts
- one UI thread for all windows
- one client thread for all hosted-app logic
- one native UI session per window
- one pane/render registry per window
- explicit `window_id` on commands and events

This gives the framework a real multi-window model without forcing hosted applications into a much more complex thread topology.

## Open Questions

These should be resolved during implementation, not before starting the refactor:

- should window creation accept a full initial JSON spec or create an empty shell first?
- should the first window be implicit for backward compatibility, or should all windows become explicit at the API boundary?
- should frame callbacks be global-with-window-id or registered per window?
- should close-last-window imply app shutdown by default?
- which immutable assets are worth sharing across windows, if any?

## Conclusion

The correct path to multi-window support is to make the current singleton ownership model window-scoped and session-scoped.

The framework should not start with a per-app thread for each window. The first version should keep a simple model:

- one UI thread
- one app/client thread
- many windows, each with its own renderer and layout session

That model is strong enough for typical editor/tooling apps, preserves the existing SuperTerminal philosophy, and leaves room for more advanced isolation later if profiling or product requirements justify it.