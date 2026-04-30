# Wingui Terminal Framework Design

## Purpose

This document describes a practical framework layered on top of `wingui` for applications that want a graphical terminal with optional audio, while keeping the application's own logic off the foreground UI thread.

The missing top-level coordination component in that framework is the product-facing runtime we should call `SuperTerminal`: a single API surface that composes declarative native UI, fast-path Direct3D panes, input, audio, and lifecycle into one application model.

The intended model is:

- The framework starts the `wingui` window and message pump on the foreground thread.
- The framework starts the client application on a background thread by invoking a startup callback.
- The client thread never touches Win32, Direct3D, or `wingui` rendering objects directly.
- Cross-thread interaction happens through explicit command and event queues.

That split matches the current `wingui` surface well: window creation, message pumping, and D3D context ownership are naturally UI-thread bound, while audio already uses an internal worker-thread model and can fit under a higher-level runtime.

## Goals

- Keep Win32 and D3D ownership on one thread.
- Let the client application behave like a terminal program, not a GUI toolkit user.
- Provide predictable, typed cross-thread communication instead of ad hoc locking.
- Support retained text/graphics state so rendering is stable and efficient.
- Make shutdown, cancellation, and error propagation explicit.
- Stay compatible with a C-style public API.

## Non-goals

- Exposing raw `HWND`, swap chain, or renderer internals to the client thread.
- Allowing arbitrary client drawing callbacks on the UI thread.
- Treating every terminal write as an immediate render call.
- Supporting multiple client threads in the first version.

## The Missing Coordination Layer: SuperTerminal

The current design already describes the right primitives, but it still reads like two neighboring systems:

- a declarative native UI framework for Win32 controls
- a fast-path Direct3D terminal and graphics surface host

What is missing is the explicit coordination component that turns those primitives into one coherent application API.

That component should be named `SuperTerminal`.

`SuperTerminal` is not another renderer and not a second UI toolkit. It is the composition root and public runtime contract for applications that want all of the following at once:

- declarative menus, forms, lists, dialogs, and chrome
- one or more hosted text-grid panes
- one or more hosted indexed-graphics panes
- one or more hosted RGBA panes
- unified input, lifecycle, and audio services
- a single client-thread programming model

The key idea is that an application should not think in terms of "the native UI system over here" and "the Direct3D panes over there". It should think in terms of one `SuperTerminal` app model where:

- the declarative tree defines the window structure and pane placement
- the fast-path commands stream content into pane ids declared by that tree
- the host coordinates both under one queue, one lifecycle, and one event model

Without this coordination layer, the design remains technically correct but product-incomplete.

## Core Architecture

The framework should be split into four layers.

### 1. Wingui platform layer

This is the existing `wingui` library.

- Window creation and dispatch
- Message pump
- D3D11 context and renderers
- Audio and MIDI services

### 2. Hosted presentation engines

These are the retained presentation subsystems coordinated by the runtime.

- native declarative UI host and reconciler
- text-grid surface host
- indexed-graphics surface host
- RGBA pane surface host
- menu and pane layout state

These are UI-thread owned implementation pieces, not the public app-facing API.

### 3. SuperTerminal coordination layer

This is the missing product-facing runtime built on top of `wingui` and the hosted presentation engines.

It is the layer applications should target.

- Owns the window, renderer instances, native UI engine, and pane registry
- Starts and joins the client thread
- Drains client-to-UI commands
- Collects UI-to-client input events
- Coordinates shutdown and error reporting
- Presents one coherent app model instead of exposing adjacent subsystems
- Defines the contract between declarative pane layout and fast-path surface updates

### 4. Client application layer

This is the user's application.

- Receives a startup callback on a worker thread
- Pushes terminal/UI commands through a SuperTerminal client context
- Reads keyboard, mouse, resize, and control events from an event queue
- Does not call `wingui_create_window_utf8`, `wingui_create_context`, or any renderer functions directly

In other words: `wingui` is the platform, the hosted native and Direct3D pieces are retained engines, and `SuperTerminal` is the actual application runtime.

## Threading Model

Use a strict two-thread design in the first implementation.

### UI thread

The foreground thread owns:

- `WinguiWindow`
- `WinguiContext`
- `WinguiTextGridRenderer`
- `WinguiIndexedGraphicsRenderer` and `WinguiRgbaPaneRenderer` if enabled
- the reconciled declarative layout that positions both standard Win32 controls and hosted Direct3D panes
- terminal presentation state
- menu state
- framework lifecycle state visible to Win32

The UI thread is responsible for:

- creating the window
- pumping Win32 messages
- draining the command queue
- reconciling the declarative tree into control layout, pane layout, and retained host state
- updating retained terminal state
- rendering frames
- forwarding user input into the event queue
- beginning shutdown when the window closes or the client fails

### Client thread

The background thread owns:

- client application logic
- terminal protocol parsing
- session state
- application timers, networking, and file I/O

The client thread is responsible for:

- running the startup callback
- pushing UI commands into the command queue
- reading UI/input events from the event queue
- honoring cancellation and exiting cleanly

### Optional internal workers

The framework may still rely on internal worker threads already present inside `wingui`, such as audio mixing. Those workers remain implementation details. The framework API should still present a simple two-party model: host thread and client thread.

## Ownership Rules

These rules are the most important part of the design.

1. Only the UI thread may access `WinguiWindow`, `WinguiContext`, or any renderer instance.
2. The client thread may only interact with the UI through framework queue APIs.
3. UI events are copied into framework-owned event records before being visible to the client thread.
4. Shared state between threads is limited to queue metadata, shutdown flags, and small counters/sequence numbers.
5. Retained terminal buffers are mutated on the UI thread after commands are drained.
6. The native Win32 control host, if enabled, also lives entirely on the UI thread and is mutated only by queued framework commands.

This avoids the usual failure modes of Win32 cross-thread UI access: invalid re-entrancy, unsafe `HWND` use, D3D context misuse, and shutdown races.

## Why Retained State Beats Immediate Drawing

The client should not send low-level draw calls like "render glyph now". It should send state mutation commands such as:

- write text at row and column
- set style attributes
- scroll a region
- clear a range
- move cursor
- swap to a new screen buffer
- request a present

The framework should hold a retained terminal model and render from that model each frame. That gives:

- deterministic redraw after resize or expose
- straightforward dirty-region tracking
- the ability to coalesce many client writes into one frame
- simpler recovery if the device is resized or recreated

The same principle applies to the entire window layout, interleaving native Win32 controls with Wingui's custom D3D surfaces (text grid, indexed graphics, RGBA panes). The boundary between the two threads is the passing of a single, unified declarative state:

1. **Client thread**: Owns the *authoritative logical UI model*. The client translates its application state into a declarative UI description (JSON) containing both native controls and layout definitions for the Direct3D custom surfaces, or computes JSON patches representing deltas to that description.
2. **UI thread (Host)**: Owns the *retained presentation model*. The host holds the last-published JSON spec, applies queued patches to it, and reconciles the physical Win32 `HWND` tree alongside the layout bounds of the D3D renderers to match the updated spec.

The client should not issue imperative control calls like "create a button now" or "mutate this HWND directly". Instead, it should enqueue higher-level UI state changes such as:

- publish a full native UI tree
- patch part of the native UI tree (e.g. JSON Patch)
- request a window/menu/native-ui refresh

When commands are drained, the host updates its retained model and reconciles the Win32 control tree from it. That keeps native controls aligned with the same design constraints as the text grid: deterministic rebuild, coalesced updates, and no client-thread access to Win32 objects.

### The Dual-Latency Model (Fast vs. Slow Path)

This design intentionally establishes a dual-latency model across the strict queue boundary, treating it as a core architectural feature:

- **Fast Path (Binary/Dense):** Commands mutating the terminal grid or sprite surfaces pass dense, binary C-structs (e.g., `SUPERTERMINAL_CMD_TEXT_GRID_WRITE_CELLS`). Because these surfaces are flat 2D arrays that never change structural topology, the client can safely blast thousands of high-speed, low-latency updates per frame with near-zero allocation overhead.
- **Slow Path (Structural/Sparse):** The native UI "chrome" (menus, lists, dialogs) manages complex hierarchical layouts, nested scrolling, and focus trees. Manipulating this via the `ui_model` Virtual DOM and JSON patches incurs a small serialization overhead, yielding a perfectly acceptable 1-2 frame asynchronous UI latency.

By preserving this duality, the client application achieves maximum throughput for its core terminal/graphical canvas, while enjoying the safety and ergonomics of a declarative, web-like UI model for its HUD and tooling—without either path ever violating the strict thread isolation rules.

## SuperTerminal Responsibilities

`SuperTerminal` should be the only high-level API most applications need to understand.

Its responsibilities are:

- own the UI-thread host runtime and all retained presentation subsystems
- expose a single startup entry point for launching an application
- define how declarative layout names and places fast-path panes
- route client commands either to the slow structural path or the fast binary path
- translate UI-thread events into one ordered client-visible event stream
- provide a single shutdown, diagnostics, and error-reporting model

That means the lower-level pieces should be treated as implementation detail boundaries:

- `wingui.h` remains the platform and rendering primitive layer
- `native_ui.h` remains the Win32 declarative host and reconciler layer
- `ui_model.h` remains the client-side authoring and diff layer
- `SuperTerminal` becomes the composition and usage layer tying them together

## Runtime Components

The framework layer should expose a small set of internal components.

### SuperTerminal

Public coordination object.

Suggested responsibilities:

- present one unified application API to the client
- own the internal `TerminalHost`
- bind declarative pane definitions to fast-path surface instances
- manage lifecycle, diagnostics, and policy choices visible at the API boundary

The important distinction is that `TerminalHost` is the internal host/runtime object, while `SuperTerminal` is the public conceptual product and API surface built on top of it.

### TerminalHost

Top-level runtime object.

Suggested responsibilities:

- owns the window and renderers
- owns the native UI engine and retained control tree when native controls are enabled
- owns the queues
- owns the client thread handle
- owns the global shutdown state
- stores configuration and diagnostics

Suggested fields:

```c
typedef struct TerminalHost {
    WinguiWindow* window;
    WinguiContext* context;
    WinguiTextGridRenderer* text_renderer;
    WinguiIndexedGraphicsRenderer* graphics_renderer;
    WinguiRgbaPaneRenderer* rgba_renderer;
    WinguiNativeUiHost* native_ui;
    SuperTerminalCommandQueue ui_queue;
    SuperTerminalEventQueue client_queue;
    TerminalSurface surface;
    SuperTerminalLifecycle lifecycle;
    void* client_thread_handle;
    uint32_t wake_message;
    int32_t exit_code;
} TerminalHost;
```

### TerminalSurface

Retained visual model for the terminal.

Recommended contents:

- text grid cells
- foreground/background colors
- cursor position and shape
- viewport size in rows and columns
- optional status bar or overlay layer
- optional palette and theme state
- optional dirty rectangle tracking

The client does not hold a direct pointer to this object.

### NativeUiHost

Optional retained presentation model and reconciler for native Win32 controls.

Recommended contents:

- current published native UI tree (last known JSON state for diffing/patching)
- pending native UI patch state
- control bindings and ID mapping (HWND translation)
- queued native UI events awaiting delivery to the client thread
- waitable event handle for reactive native control events

This object belongs to `TerminalHost`, is owned by the UI thread, and should be treated as another retained presentation surface rather than a separate application runtime.

### Pane registry and layout bridge

This is the internal bridge that makes `SuperTerminal` feel unified instead of split.

Recommended contents:

- authoritative pane ids exported from the declarative tree
- pane type metadata (`text-grid`, `indexed-graphics`, `rgba-pane`)
- resolved client rectangles for each pane
- bindings from pane id to renderer-owned retained surface state
- dirty and visibility state for each hosted pane

This bridge is what lets the declarative tree own structure while the fast path owns content.

## Unified Surface Model

The central design rule for `SuperTerminal` is that declarative layout and Direct3D pane streaming are two views of the same UI, not two unrelated APIs.

The declarative tree should be authoritative for structure:

- window title and high-level window props
- split views, stacks, rows, tabs, cards, forms, and menus
- standard Win32 controls and their placement inside that layout
- hosted pane placeholder nodes with stable ids and types

The hosted application should remain authoritative for its own logical model.

That means:

- the backend computes and owns the application state that produces the declarative tree
- the reactive model publishes full trees or patches for structural UI and standard control updates
- the same backend separately emits high-speed binary commands for text-grid and graphics pane content
- the UI thread never becomes the source of truth for application state; it only retains and reconciles the latest published view of it

The fast-path commands should be authoritative for content:

- text cell data for a text-grid pane id
- indexed framebuffer, sprite atlas, and palette updates for an indexed pane id
- BGRA buffer allocation, upload, and present requests for an RGBA pane id

Standard Win32 controls participate through the reactive path:

- their position and lifetime come from the declarative layout
- their property updates come from declarative publishes or patches
- their user interactions are translated into semantic events and sent back to the backend model

This yields a clean mental model:

1. The backend owns the application model and publishes a declarative tree that says which controls and panes exist and where they live.
2. The UI thread reconciles that tree into Win32 control placement and authoritative pane rectangles.
3. Native controls raise semantic events back to the backend, which updates its model and publishes patches when needed.
4. The backend streams fast content updates into the declared pane ids.
5. The host renders the combined result as one window.

That is the actual value of `SuperTerminal`: it unifies structural layout and real-time graphics into one runtime contract.

## Public API Shape

The public API should be framed around `SuperTerminal`, not around the individual hosted subsystems.

Conceptually, the surface should look like this:

```c
typedef struct SuperTerminalApp SuperTerminalApp;
typedef struct SuperTerminalClientContext SuperTerminalClientContext;
typedef struct SuperTerminalPaneId {
    uint64_t value;
} SuperTerminalPaneId;

typedef struct SuperTerminalAppDesc {
    const char* title_utf8;
    uint32_t flags;
    const char* initial_ui_json_utf8;
    int32_t (WINGUI_CALL *startup)(SuperTerminalClientContext* ctx, void* user_data);
    void (WINGUI_CALL *shutdown)(void* user_data);
    void* user_data;
} SuperTerminalAppDesc;
```

The exact names can change. The important part is that the app launches one coordinated runtime, not a bag of neighboring services.

From that client context, the app should be able to:

- publish or patch the declarative UI model
- stream text-grid, indexed, and RGBA content into named panes
- receive input and native control events through one event queue
- sample high-speed keyboard and mouse sensor state once per frame without reconstructing it from events
- trigger audio and lifecycle requests through the same runtime

More concretely, the API boundary should expose four categories of operation:

- lifecycle: create, run, stop, and join one `SuperTerminal` app instance
- structural UI: publish or patch the declarative tree that defines controls, panes, menus, and layout
- pane streaming: send dense binary updates into a previously declared pane id
- event/input: consume one ordered event stream plus optional polled key and mouse state

That is the crucial split: structure is declarative and comparatively slow; pane content is binary and comparatively fast.

## Relationship to Existing Pieces

`SuperTerminal` should be implemented mostly as composition, not reinvention.

Existing pieces already line up well:

- `native_ui.cpp` already knows how to reconcile declarative Win32 UI and expose pane placeholders
- `ui_model.cpp` already gives the client a strongly-typed builder and diff layer for structural UI
- `wingui.cpp` already provides the text-grid, indexed-graphics, RGBA, window, input, and audio primitives

The missing work is to define and document the coordinating boundary that owns them together.

Put differently:

- `native_ui` should not be presented as a separate app runtime
- fast-path panes should not be presented as a separate rendering product
- both should be subsumed under `SuperTerminal`

### Command queue

Carries client-to-UI operations.

This queue should be bounded and typed. For the first implementation, a mutex-protected ring buffer is acceptable. If throughput later matters, it can become an SPSC lock-free ring without changing the public API.

The queue should be treated as two logical lanes carried by one physical transport:

- structural commands: `SUPERTERMINAL_CMD_NATIVE_UI_*` and related window/menu commands
- fast-path pane commands: `SUPERTERMINAL_CMD_TEXT_GRID_*`, `SUPERTERMINAL_CMD_INDEXED_*`, and `SUPERTERMINAL_CMD_RGBA_*`

They can share one bounded queue implementation, but the command taxonomy must keep them visibly distinct.

### Event queue

Carries UI-to-client events.

This queue should deliver:

- key down/up
- translated character input
- mouse button and move events
- wheel events
- resize events
- focus gained/lost
- close requested
- host error or cancellation notices

## Queue Strategy

The simplest suitable design is two queues.

### Queue A: client to UI

Direction:

- producer: client thread
- consumer: UI thread

Use it for:

- structural `NATIVE_UI_*` commands
- pane-targeted text-grid commands
- pane-targeted indexed-graphics commands
- pane-targeted RGBA commands
- title and window state changes
- audio requests
- clipboard requests
- menu state changes
- controlled shutdown requests

### Queue B: UI to client

Direction:

- producer: UI thread
- consumer: client thread

Use it for:

- input events
- native control dispatch events
- pane input and focus events
- window resize notifications
- focus changes
- close requests
- host lifecycle notifications

### Wake-up mechanism

Do not make the UI thread poll aggressively.

When the client enqueues a command, it should also wake the UI thread. On Windows, the cleanest option is a custom posted message:

- reserve `WM_APP + N` for `SUPERTERMINAL_WAKE`
- after pushing one or more commands, call `PostMessage(hwnd, SUPERTERMINAL_WAKE, 0, 0)`

The UI thread then drains the queue during normal message processing or before rendering the next frame.

This is better than a busy loop and avoids tying rendering to constant polling.

## Command Model

Commands should be explicit tagged unions, not loosely structured callbacks.

Example:

```c
typedef enum SuperTerminalCommandType {
    SUPERTERMINAL_CMD_NOP = 0,

    /* Structural and reactive UI lane. */
    SUPERTERMINAL_CMD_NATIVE_UI_PUBLISH,
    SUPERTERMINAL_CMD_NATIVE_UI_PATCH,
    SUPERTERMINAL_CMD_NATIVE_UI_SET_MENU,
    SUPERTERMINAL_CMD_NATIVE_UI_CLEAR_MENU,
    SUPERTERMINAL_CMD_WINDOW_SET_TITLE,
    SUPERTERMINAL_CMD_WINDOW_SET_STATE,
    SUPERTERMINAL_CMD_WINDOW_FLASH,
    SUPERTERMINAL_CMD_CLIPBOARD_SET_TEXT,
    SUPERTERMINAL_CMD_CLIPBOARD_REQUEST_TEXT,

    /* Fast text-grid pane lane. */
    SUPERTERMINAL_CMD_TEXT_GRID_WRITE_CELLS,
    SUPERTERMINAL_CMD_TEXT_GRID_CLEAR_REGION,
    SUPERTERMINAL_CMD_TEXT_GRID_SCROLL_REGION,
    SUPERTERMINAL_CMD_TEXT_GRID_SET_CURSOR,
    SUPERTERMINAL_CMD_TEXT_GRID_SET_PALETTE,

    /* Fast indexed-graphics pane lane. */
    SUPERTERMINAL_CMD_INDEXED_DEFINE_PALETTE,
    SUPERTERMINAL_CMD_INDEXED_UPLOAD_TILES,
    SUPERTERMINAL_CMD_INDEXED_UPLOAD_SPRITES,
    SUPERTERMINAL_CMD_INDEXED_PRESENT,

    /* Fast RGBA pane lane. */
    SUPERTERMINAL_CMD_RGBA_ALLOCATE_BUFFER,
    SUPERTERMINAL_CMD_RGBA_UPLOAD_RECT,
    SUPERTERMINAL_CMD_RGBA_PRESENT,

    /* Shared control lane. */
    SUPERTERMINAL_CMD_AUDIO_PLAY,
    SUPERTERMINAL_CMD_REQUEST_PRESENT,
    SUPERTERMINAL_CMD_REQUEST_CLOSE
} SuperTerminalCommandType;

typedef struct SuperTerminalNativeUiPublish {
    const char* json_utf8;
} SuperTerminalNativeUiPublish;

typedef struct SuperTerminalNativeUiPatch {
    const char* patch_json_utf8;
} SuperTerminalNativeUiPatch;

typedef struct SuperTerminalTextGridWriteCells {
    SuperTerminalPaneId pane_id;
    const SuperTerminalGlyphSpan* spans;
    uint32_t span_count;
} SuperTerminalTextGridWriteCells;

typedef struct SuperTerminalIndexedPresent {
    SuperTerminalPaneId pane_id;
    const SuperTerminalIndexedFrame* frame;
} SuperTerminalIndexedPresent;

typedef struct SuperTerminalRgbaPresent {
    SuperTerminalPaneId pane_id;
    uint32_t buffer_index;
} SuperTerminalRgbaPresent;

typedef struct SuperTerminalCommand {
    SuperTerminalCommandType type;
    uint32_t sequence;
    union {
        SuperTerminalNativeUiPublish native_ui_publish;
        SuperTerminalNativeUiPatch native_ui_patch;
        SuperTerminalSetMenu set_menu;
        SuperTerminalSetTitle set_title;
        SuperTerminalSetWindowState set_window_state;
        SuperTerminalClipboardSetText clipboard_set_text;
        SuperTerminalTextGridWriteCells text_grid_write_cells;
        SuperTerminalTextGridClearRegion text_grid_clear_region;
        SuperTerminalTextGridScrollRegion text_grid_scroll_region;
        SuperTerminalTextGridSetCursor text_grid_set_cursor;
        SuperTerminalTextGridSetPalette text_grid_set_palette;
        SuperTerminalIndexedDefinePalette indexed_define_palette;
        SuperTerminalIndexedUploadTiles indexed_upload_tiles;
        SuperTerminalIndexedUploadSprites indexed_upload_sprites;
        SuperTerminalIndexedPresent indexed_present;
        SuperTerminalRgbaAllocateBuffer rgba_allocate_buffer;
        SuperTerminalRgbaUploadRect rgba_upload_rect;
        SuperTerminalRgbaPresent rgba_present;
        SuperTerminalPlaySound play_sound;
    } data;
} SuperTerminalCommand;
```

For native UI specifically, the command payload should still describe state, not imperative widget operations. A publish command should carry a full declarative tree. A patch command should carry a patch document or another copied state-delta representation that the UI thread can validate and reconcile.

### Structural command lane

The structural lane exists to mutate the retained declarative model and everything derived from it.

Recommended commands:

- `SUPERTERMINAL_CMD_NATIVE_UI_PUBLISH`: replace the authoritative declarative tree
- `SUPERTERMINAL_CMD_NATIVE_UI_PATCH`: apply a structural patch to that tree
- `SUPERTERMINAL_CMD_NATIVE_UI_SET_MENU`: replace menu model or menu subtree
- `SUPERTERMINAL_CMD_NATIVE_UI_CLEAR_MENU`: clear menu state
- `SUPERTERMINAL_CMD_WINDOW_SET_TITLE`: update top-level title
- `SUPERTERMINAL_CMD_WINDOW_SET_STATE`: minimize, maximize, restore, fullscreen if supported

Rules for this lane:

- payloads are semantic and structural, not renderer-facing
- pane ids appear here as declarations inside the tree, not as frame data
- the UI thread validates, stores, and reconciles these updates before any renderer state is touched

### Fast pane command lanes

The fast lanes exist to mutate retained content inside pane ids already declared by the structural lane.

Text-grid commands:

- `SUPERTERMINAL_CMD_TEXT_GRID_WRITE_CELLS`
- `SUPERTERMINAL_CMD_TEXT_GRID_CLEAR_REGION`
- `SUPERTERMINAL_CMD_TEXT_GRID_SCROLL_REGION`
- `SUPERTERMINAL_CMD_TEXT_GRID_SET_CURSOR`
- `SUPERTERMINAL_CMD_TEXT_GRID_SET_PALETTE`

Indexed-graphics commands:

- `SUPERTERMINAL_CMD_INDEXED_DEFINE_PALETTE`
- `SUPERTERMINAL_CMD_INDEXED_UPLOAD_TILES`
- `SUPERTERMINAL_CMD_INDEXED_UPLOAD_SPRITES`
- `SUPERTERMINAL_CMD_INDEXED_PRESENT`

RGBA pane commands:

- `SUPERTERMINAL_CMD_RGBA_ALLOCATE_BUFFER`
- `SUPERTERMINAL_CMD_RGBA_UPLOAD_RECT`
- `SUPERTERMINAL_CMD_RGBA_PRESENT`

Rules for these lanes:

- every payload targets a `pane_id` resolved by the declarative layout bridge
- commands mutate retained pane content, not pane placement
- if a pane id is missing, mismatched, or hidden, the host rejects or ignores the command according to policy
- these commands should avoid JSON and favor copied binary structs or stable buffer handles

### Command design rules

1. Commands should describe intent, not expose renderer internals.
2. Commands should contain copied data or stable references with explicit lifetime rules.
3. Structural commands own layout and standard control state; fast commands own pane content only.
4. Most commands should be fire-and-forget.
5. A small number of commands may support request/response semantics through a completion token.

## Event Model

Events should also use a tagged union.

```c
typedef enum SuperTerminalEventType {
    SUPERTERMINAL_EVENT_NONE = 0,
    SUPERTERMINAL_EVENT_KEY,
    SUPERTERMINAL_EVENT_CHAR,
    SUPERTERMINAL_EVENT_MOUSE,
    SUPERTERMINAL_EVENT_PANE_INPUT,
    SUPERTERMINAL_EVENT_RESIZE,
    SUPERTERMINAL_EVENT_FOCUS,
    SUPERTERMINAL_EVENT_NATIVE_UI,
    SUPERTERMINAL_EVENT_CLIPBOARD_TEXT,
    SUPERTERMINAL_EVENT_FILE_DROP,
    SUPERTERMINAL_EVENT_CLOSE_REQUESTED,
    SUPERTERMINAL_EVENT_HOST_STOPPING,
    SUPERTERMINAL_EVENT_DIAGNOSTIC
} SuperTerminalEventType;

typedef enum SuperTerminalNativeUiEventKind {
    SUPERTERMINAL_NATIVE_UI_BUTTON_CLICKED = 0,
    SUPERTERMINAL_NATIVE_UI_VALUE_CHANGED,
    SUPERTERMINAL_NATIVE_UI_SELECTION_CHANGED,
    SUPERTERMINAL_NATIVE_UI_SUBMIT,
    SUPERTERMINAL_NATIVE_UI_CUSTOM_JSON
} SuperTerminalNativeUiEventKind;

typedef struct SuperTerminalNativeUiEvent {
    SuperTerminalNativeUiEventKind kind;
    char node_id_utf8[128];
    const char* value_utf8;
    const char* payload_json_utf8;
} SuperTerminalNativeUiEvent;

typedef struct SuperTerminalPaneInputEvent {
    SuperTerminalPaneId pane_id;
    uint32_t device_kind;
    int32_t x;
    int32_t y;
    uint32_t modifiers;
} SuperTerminalPaneInputEvent;

typedef struct SuperTerminalEvent {
    SuperTerminalEventType type;
    uint32_t sequence;
    union {
        SuperTerminalKeyEvent key;
        SuperTerminalCharEvent character;
        SuperTerminalMouseEvent mouse;
        SuperTerminalPaneInputEvent pane_input;
        SuperTerminalResizeEvent resize;
        SuperTerminalFocusEvent focus;
        SuperTerminalNativeUiEvent native_ui;
        SuperTerminalClipboardTextEvent clipboard_text;
        SuperTerminalFileDropEvent file_drop;
        SuperTerminalHostStoppingEvent host_stopping;
        SuperTerminalDiagnosticEvent diagnostic;
    } data;
} SuperTerminalEvent;
```

Important point: the client should consume semantic terminal events, not raw Win32 messages. The framework is the translation boundary.

That same translation rule applies to hosted native controls: the client should receive semantic native-ui events such as "button clicked", "selection changed", or a JSON event payload emitted by the native-ui host, rather than direct `WM_COMMAND` or `WM_NOTIFY` traffic.

The event stream should also preserve the same structural-vs-pane distinction as the command stream:

- `SUPERTERMINAL_EVENT_NATIVE_UI` carries semantic control events from the reconciled declarative control tree
- `SUPERTERMINAL_EVENT_PANE_INPUT` carries pointer, focus, or capture events targeted at a specific hosted pane id

That way the client can react differently to, for example, a button click in the declarative chrome versus a drag gesture inside an RGBA pane.

### Dual Input Model (Events vs. Polled Sensors)

Just as output is split into fast (binary struct) and slow (JSON patch) paths, input requires a split between **event-driven** and **polled** processing to support both UI applications and low-latency game loops.

1. **Event-Driven (The Event Queue):**
    The UI thread translates Win32 messages into structured `SuperTerminalEvent` entries (`EVENT_KEY`, `EVENT_MOUSE`, `EVENT_CHAR`, `EVENT_NATIVE_UI`, `EVENT_PANE_INPUT`). This is mandatory for accurate typing, scroll-wheel deltas, tracking mouse trajectories, and interacting with hierarchical native UI dialogs.

2. **Polled Sensors (High-Speed Game Input):**
   Games frequently prefer to sample the state of the keyboard or mouse at the beginning of a simulation tick (e.g., "is WASD or the Spacebar down right now?") without writing boilerplate to drain and track up/down events manually.
    **Requirement:** The host should maintain a lock-free, thread-safe sensor snapshot for the 256 virtual keys and mouse buttons, updated directly by the UI thread's message pump instead of the slower native-controls event path. The framework should expose both single-key probes and dense snapshot reads so games can sample once per frame with stable cost.

   Concretely, the public surface should include both of these styles:

   - `super_terminal_get_key_state(ctx, WINGUI_KEY_SPACE)` for targeted checks
   - `super_terminal_get_keyboard_state(ctx, &keyboard_state)` for one bulk frame snapshot
   - `super_terminal_get_mouse_state(ctx, &mouse_state)` for pointer/buttons

   The bulk keyboard snapshot is the important part for games: it avoids 256 individual API calls per frame and gives the client a fast "sensor lane" that is separate from ordered UI/control events.

This duality guarantees that a client game loop can achieve zero-queue-latency read access to player movement controls, while concurrently receiving perfectly ordered events for text boxes, chat windows, and declarative configuration menus.

## Lifecycle

The framework should have a small explicit lifecycle state machine.

Suggested states:

- `created`
- `initializing`
- `running`
- `stop_requested`
- `client_exited`
- `shutting_down`
- `stopped`

### Startup sequence

1. Main thread creates `TerminalHost`.
2. Main thread creates the window through `wingui_create_window_utf8`.
3. Main thread creates the D3D context and required renderer objects.
4. Main thread initializes audio services if configured.
5. Main thread starts the client thread.
6. Client thread enters the user startup callback with a `SuperTerminalClientContext*`.
7. Main thread shows the window and enters the message loop.

### Steady-state loop

On the UI thread, each iteration does the following:

1. pump or wait for Win32 messages
2. translate UI messages into terminal events
3. drain client commands
4. mutate retained surface state
5. render if dirty or explicitly requested
6. present

### Shutdown sequence

Shutdown should be symmetric and idempotent.

1. UI receives `WM_CLOSE` or client requests close.
2. Host marks `stop_requested`.
3. Host pushes a stop event to the client queue.
4. Client thread exits its callback.
5. UI thread joins the client thread.
6. UI thread destroys renderers, context, audio, and window.
7. Host returns final exit code.

The host should treat repeated shutdown requests as harmless.

## Rendering Policy

The framework should separate logical updates from presentation.

Recommended behavior:

- apply all pending commands before rendering
- mark regions dirty when state changes
- render only when dirty, when resizing, or when explicitly requested
- coalesce many writes into one present when possible

The default UI loop can use `wingui_pump_message(0, ...)` to drain pending messages and then render when work exists, while falling back to `wingui_pump_message(1, ...)` when idle.

That fits the current `wingui` API without requiring a new lower-level event primitive.

## Client-Side UI Model Library (Optional)

Because boundary interactions rely on declarative JSON trees and JSON-Patch documents, raw string manipulation or manual JSON object construction is tedious for the user's client thread. 

It is highly recommended to build a `ui-model` support library for the client. This library would act as a virtual DOM:
- Provide strongly-typed C++ builders for nodes (e.g., `SplitView`, `Button`, `Table`).
- Maintain the local, complete structural state before serializing to JSON.
- Provide diffing capabilities to automatically compute JSON-Patch payloads between two logical states, reducing queue bandwidth and UI-thread parsing overhead.

## Audio Policy

There are two sensible options.

### Option A: audio as another UI-owned service

The host initializes audio during startup and processes audio-related client commands on the UI thread.

Benefits:

- one clear owner for all framework services
- simpler shutdown ordering
- easier logging and diagnostics

Cost:

- more commands pass through the UI queue

### Option B: audio as a host service callable from any thread

The framework exposes audio helpers that the client thread can call directly, relying on `wingui`'s internal synchronization.

Benefits:

- lower latency for sound triggers
- simpler client-side usage for one-shot sounds

Cost:

- weaker ownership model
- inconsistent programming model versus rendering

Recommended first version: Option A. Consistency is more valuable than shaving a small amount of latency from terminal bells and UI sounds.

## Error Handling

The framework should distinguish between three classes of errors.

### Host initialization errors

Examples:

- window creation failed
- D3D context creation failed
- glyph atlas creation failed
- audio initialization failed

These should abort startup and return immediately.

### Client runtime errors

Examples:

- startup callback returns failure
- client throws an exception in a C++ wrapper
- queue overflow treated as fatal

These should trigger orderly shutdown and surface a structured error to the UI thread.

### Recoverable operational errors

Examples:

- sound failed to play
- clipboard request failed
- unsupported key translation edge case

These should be logged and optionally forwarded to the client as notifications, but should not necessarily terminate the host.

## Backpressure and Queue Capacity

This matters for terminal workloads, especially large text bursts.

Recommended policy for the first version:

- use bounded queues
- return explicit failure when the queue is full
- provide a blocking enqueue variant for large producers
- allow the host to coalesce adjacent text writes where possible

Avoid an unbounded heap-backed queue in the first version. It hides load problems until memory usage becomes the problem.

## API Shape

The public framework API should stay small and procedural.

Example sketch:

```c
typedef struct SuperTerminalRunResult {
    int32_t exit_code;
    int32_t host_error_code;
    const char* message_utf8;
} SuperTerminalRunResult;

typedef struct SuperTerminalAppDesc {
    const char* title_utf8;
    uint32_t flags;
    const char* initial_ui_json_utf8;
    void* user_data;
    int32_t (WINGUI_CALL *startup)(SuperTerminalClientContext* ctx, void* user_data);
    void (WINGUI_CALL *shutdown)(void* user_data);
} SuperTerminalAppDesc;

WINGUI_API int32_t WINGUI_CALL super_terminal_run(
    const SuperTerminalAppDesc* desc,
    SuperTerminalRunResult* out_result);

WINGUI_API int32_t WINGUI_CALL super_terminal_enqueue(
    SuperTerminalClientContext* ctx,
    const SuperTerminalCommand* command);

WINGUI_API int32_t WINGUI_CALL super_terminal_wait_event(
    SuperTerminalClientContext* ctx,
    uint32_t timeout_ms,
    SuperTerminalEvent* out_event);

WINGUI_API int32_t WINGUI_CALL super_terminal_request_stop(
    SuperTerminalClientContext* ctx,
    int32_t exit_code);

WINGUI_API int32_t WINGUI_CALL super_terminal_publish_ui_json(
    SuperTerminalClientContext* ctx,
    const char* json_utf8);

WINGUI_API int32_t WINGUI_CALL super_terminal_patch_ui_json(
    SuperTerminalClientContext* ctx,
    const char* patch_json_utf8);

WINGUI_API int32_t WINGUI_CALL super_terminal_get_key_state(
    SuperTerminalClientContext* ctx,
    uint32_t virtual_key);

WINGUI_API int32_t WINGUI_CALL super_terminal_get_mouse_state(
    SuperTerminalClientContext* ctx,
    SuperTerminalMouseState* out_state);
```

If convenience helpers are added, they should follow the same split explicitly.

Structural helpers:

- `super_terminal_publish_ui_json(...)`
- `super_terminal_patch_ui_json(...)`
- `super_terminal_set_menu_json(...)`

Fast-pane helpers:

- `super_terminal_text_grid_write_cells(...)`
- `super_terminal_indexed_present(...)`
- `super_terminal_rgba_present(...)`

Those helpers should compile down to ordinary `SuperTerminalCommand` enqueue operations rather than opening a second hidden control path.

### Why a single `super_terminal_run` entry point is useful

It gives the framework one place to own:

- startup ordering
- thread creation
- message loop lifetime
- renderer creation and destruction
- audio initialization and shutdown
- final exit code resolution

That is cleaner than exposing many independent setup calls that the application must sequence correctly.

## Client Programming Model

The client callback should feel like writing a terminal application, not a GUI callback tree.

Example shape:

```c
int32_t WINGUI_CALL my_app_startup(SuperTerminalClientContext* ctx, void* user_data) {
    super_terminal_publish_ui_json(ctx, "{\"type\":\"window\",\"title\":\"System ready\"}");
    super_terminal_request_present(ctx);

    for (;;) {
        SuperTerminalEvent event;
        if (!super_terminal_wait_event(ctx, 100, &event)) {
            continue;
        }

        if (event.type == SUPERTERMINAL_EVENT_CLOSE_REQUESTED) {
            return 0;
        }

        if (event.type == SUPERTERMINAL_EVENT_CHAR) {
            handle_char_input(ctx, &event);
        }
    }
}
```

This style is easy to reason about and makes it feasible to host shells, emulators, editors, interpreters, or game-like terminal clients.

## Extended Application Requirements

Beyond the basic text grid and input processing, a hosted terminal application needs several capabilities to function as a fully-featured tool (like an editor, shell, or interactive utility). The framework must route these securely without violating the threading rules.

### 1. Waitable Event Handles (for Async I/O)
A pure `super_terminal_wait_event` call is sufficient for simple apps, but real-world terminal applications often multiplex UI events with network sockets, file I/O, sub-processes, or system timers.
**Requirement**: The framework's event queue should expose access to a native Win32 `HANDLE` (an Event) that becomes signaled when UI events are available in the queue. This allows the client thread to use `MsgWaitForMultipleObjects` or `WaitForMultipleObjects` to sleep efficiently until *either* a UI event or an external I/O event occurs.

### 2. Clipboard Access (Async)
Reading and writing the clipboard safely requires UI-thread ownership in Win32 to avoid locking issues and hangs.
**Requirement**: 
- **Write**: The client enqueues a `SUPERTERMINAL_CMD_CLIPBOARD_SET_TEXT` command containing the text string.
- **Read**: The client enqueues a `SUPERTERMINAL_CMD_CLIPBOARD_REQUEST_TEXT` command. The UI thread retrieves the text and pushes it back into the client queue as a `SUPERTERMINAL_EVENT_CLIPBOARD_TEXT` event.

### 3. Drag and Drop Integration
Many terminal and editor applications allow dragging files into the window to paste their paths.
**Requirement**: The UI thread should listen for `WM_DROPFILES`, extract the paths, and push a `SUPERTERMINAL_EVENT_FILE_DROP` event into the client queue, containing the list of UTF-8 file paths.

### 4. Advanced Window Control
The client application may need to control the physical window state or alert the user.
**Requirement**: Provide commands for `SUPERTERMINAL_CMD_WINDOW_FLASH` (to get user attention in the taskbar) and commands to Minimize, Maximize, and Restore the window. 

### 5. Configurable Input Modes
Different terminal applications require varying levels of input verbosity.
**Requirement**: The framework should support a `SUPERTERMINAL_CMD_SET_INPUT_MODE`-style command or configuration update. This should toggle features like:
- **Mouse tracking**: Clicks only vs. report all mouse motion (dragging, hover). This prevents flooding the event queue with mouse-move events when the app only cares about clicks.
- **Raw keyboard mode**: Deciding whether to process Win32 `WM_CHAR` translated characters, or report pure `WM_KEYDOWN` virtual key codes without layout translation.

### 6. System Metrics & Layout Data
If the user moves the window to a different monitor with a different scaling factor, or resizes it, the text scale and grid dimensions might change.
**Requirement**: The `SUPERTERMINAL_EVENT_RESIZE` event must include the current DPI scale, cellular dimensions, and the total column/row count so the client application can accurately layout its UI.

## UI Message Handling

The framework window procedure should stay thin.

It should mostly do four things:

1. translate Win32 input into `SuperTerminalEvent`
2. enqueue resize and control notifications
3. react to `SUPERTERMINAL_WAKE`
4. begin shutdown on `WM_CLOSE`

It should not contain application logic.

## Recommended Internal Sequence

This is the simplest end-to-end flow.

### Host thread

```text
create host
create window
create context and renderers
start client thread
show window
while running:
  drain messages
  drain ui queue
  update surface
  render if dirty
join client
destroy host resources
```

### Client thread

```text
enter startup callback
enqueue UI mutations
wait for terminal events
react to input and resize
exit when stop requested
```

## Recommended Implementation Phases

### Phase 1

- host runtime object
- client thread creation
- command queue and wake message
- retained text grid surface
- key/char/resize events
- clean shutdown

### Phase 2

- palette and cursor support
- clipboard helpers
- simple audio commands
- better dirty-region tracking

### Phase 3

- optional graphics pane integration
- menus and command bindings
- request/response commands
- profiling and queue metrics

## Suitability for the Wingui Project

This design is a good fit for the current project because:

- `wingui` already centers on explicit window/context creation rather than hidden global application state
- the message pump is already exposed in a way the framework can drive
- rendering is already retained enough to support a state-model layer above it
- audio already uses internal threaded services, which matches a host-managed runtime model

Most importantly, this design preserves a clean boundary: `wingui` remains the low-level platform library, and the terminal framework becomes a higher-level runtime built on top of it rather than mixed into every client application.

## Recommendation

Implement the first framework version as a single-host, single-client-thread runtime with:

- UI-thread ownership of all `wingui` window and rendering objects
- a bounded client-to-UI command queue
- a bounded UI-to-client event queue
- a retained text-grid surface model
- a single `super_terminal_run` entry point
- orderly, explicit shutdown with wake messages and join semantics

That design is simple enough to implement cleanly, strong enough to avoid the usual Win32 threading mistakes, and flexible enough to support a real terminal-style application framework rather than a one-off demo shell.