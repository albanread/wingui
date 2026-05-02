# Spec + Bind

## Goal

Spec + Bind is the C ABI path for Wingui's declarative native UI.

It separates two concerns:

- `Spec`: a pure JSON window spec that describes structure, labels, ids, layout, menus, command bars, status bars, and widget state.
- `Bind`: a runtime event table that maps event names from the spec to native callbacks supplied by the embedding language.

The existing C++ builder stays as the most ergonomic authoring layer. Spec + Bind is the portable execution layer for C, Zig, Rust, Odin, C#, Python FFI, and other languages that can call a plain C ABI.

## Why

The current JSON UI model already captures almost all view state. Event handlers are the missing piece for foreign-language use.

The design target is:

1. Build or load a JSON spec.
2. Bind named events to host callbacks.
3. Run the hosted Wingui app.
4. Let callbacks replace the current spec when application state changes.

That keeps behavior in the host language while keeping the UI description serializable and portable.

## First implementation

The first shipped slice is intentionally narrow.

- `WinguiSpecBindRuntime` stores the current JSON spec and a table of event bindings.
- `wingui_spec_bind_runtime_run(...)` hosts the app through `super_terminal_run_hosted_app(...)`.
- `wingui_spec_bind_runtime_load_spec_json(...)` replaces the current spec and, when the runtime is active, applies a UI patch when possible or falls back to a full publish when reconciliation requires it.
- Native UI events are parsed for their string `event` name and dispatched to the matching binding.
- A default handler can observe or handle unbound events.
- Close requests fall back to `request_stop(0)` when no handler is present.

This is enough to make a foreign host own application state and re-render the declarative UI by publishing full JSON specs.

## Event contract

For native UI events, the runtime extracts:

- `event_name_utf8`: the `event` field from the JSON payload
- `payload_json_utf8`: the full native UI event payload JSON
- `source_utf8`: the `source` field from the payload when present

The runtime also exposes two reserved pseudo-events:

- `__close_requested`
- `__host_stopping`

These let a foreign host participate in app shutdown without needing a second callback system.

## Runtime contract

`load_spec_json` is both a preload and a live-update operation.

- Before `run`, it only stores the current spec.
- During `run`, it stores the spec and pushes either a patch or a full publish into the active hosted app.

That keeps the public API small and makes the host-side state loop straightforward: compute JSON, call `load_spec_json`, return.

## Deliberate constraints in v1

- No separate binding-set object yet. Bindings live directly on the runtime.
- No diff/patch helper yet. Foreign hosts can still update incrementally by computing a new full spec and calling `load_spec_json` again.
- No file-loading helper yet. The host language can load JSON however it prefers.
- No automatic schema validation beyond JSON parse and event-name extraction.

These are follow-up layers, not prerequisites for a useful portable runtime.

## Integration shape

The implementation lives in its own public header and source file:

- `include/wingui/spec_bind.h`
- `src/spec_bind.cpp`

The rest of the codebase only needs thin build integration. The runtime reuses the existing hosted terminal API instead of reimplementing the window loop.

## Typical flow

1. Create a `WinguiSpecBindRuntime`.
2. Load a JSON spec.
3. Bind callbacks such as `btn-save`, `menu-file-exit`, or `format-selection:bold`.
4. Run the runtime.
5. Inside a callback, update application state, serialize a new spec, and call `load_spec_json` again.

## Follow-up directions

- Add a reusable `BindingSet` object for sharing bindings across runtimes.
- Add optional JSON patch support for hosts that want smaller updates.
- Add schema/version metadata for spec compatibility checks.
- Add convenience helpers for loading/saving specs from UTF-8 file paths.
- Add a small C example and one foreign-language sample.