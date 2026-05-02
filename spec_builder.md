# Spec Builder

## Goal

Spec Builder is the authoring-side half of Wingui's portable declarative UI story.

Build with `spec_builder`. Run with `spec_bind`.

This split keeps two concerns separate:

- `spec_builder`: construct, validate, and eventually transform UI specs
- `spec_bind`: load a spec, bind callbacks, run the hosted app, and reconcile live updates

## Why this exists

The current codebase already has two different layers:

- an internal C++ authoring and diff layer in `ui_model.h` / `ui_model.cpp`
- a public plain-C runtime layer in `spec_bind.h` / `spec_bind.cpp`

What is missing for other languages is not a second runtime. The missing piece is a portable authoring helper layer that gives C, Zig, Rust, C#, Python FFI, and similar hosts a cleaner way to produce valid Wingui specs.

That is what `spec_builder` names.

## Current status

`spec_builder` now exports a first narrow helper slice instead of trying to expose the whole C++ builder API.

Today the supported flow is still:

1. Keep application state in the host language.
2. Serialize a full JSON window spec in that language.
3. Pass the JSON to `wingui_spec_bind_runtime_load_spec_json(...)`.
4. Let `spec_bind` handle runtime reconciliation.

The first reusable helpers now available are:

- `wingui_spec_builder_validate_json(...)`
- `wingui_spec_builder_copy_canonical_json(...)`
- `wingui_spec_builder_copy_normalized_json(...)`
- `wingui_spec_builder_copy_patch_json(...)`

These are intentionally small. They let foreign hosts validate a full JSON spec against the real native-ui contract, normalize it to a canonical and patch-friendly JSON string, and ask Wingui's existing reconciler whether two specs can be patched incrementally, using the same diff engine already used internally by `spec_bind`.

## Scope

`spec_builder` should own authoring-oriented helpers such as:

- JSON spec validation
- schema/version helpers
- reusable builder utilities for common node shapes
- optional diff or patch helper utilities that are useful before runtime execution

`spec_builder` should not own:

- hosted execution
- callback binding
- UI-thread runtime control
- frame callback dispatch

Those stay in `spec_bind`.

## Relationship to ui_model

The existing `ui_model` layer remains an internal C++ implementation surface and an ergonomic native-C++ authoring API.

The intent is not to export the whole existing `ui_model` API directly as a C ABI. Instead, `spec_builder` should expose the smaller cross-language subset that is actually useful and stable for foreign hosts.

## Immediate rule

When documenting or expanding the portable API surface, use this mental model:

- build with `spec_builder`
- run with `spec_bind`

## First exports

```c
int32_t wingui_spec_builder_validate_json(
	const char* json_utf8);

int32_t wingui_spec_builder_copy_canonical_json(
	const char* json_utf8,
	char* buffer_utf8,
	uint32_t buffer_size,
	uint32_t* out_required_size);

int32_t wingui_spec_builder_copy_normalized_json(
	const char* json_utf8,
	char* buffer_utf8,
	uint32_t buffer_size,
	uint32_t* out_required_size);

int32_t wingui_spec_builder_copy_patch_json(
	const char* old_json_utf8,
	const char* new_json_utf8,
	char* buffer_utf8,
	uint32_t buffer_size,
	uint32_t* out_required_size,
	int32_t* out_requires_full_publish,
	uint32_t* out_patch_op_count);
```

`validate_json` checks the native-ui structural contract, not just JSON syntax. It now enforces the same minimum shape the native host validates at publish time: every node must have a non-empty string `type`, `window` must have an object `body`, `children` must be arrays of objects, `split-view` must have exactly two `split-pane` children, and the structured collection fields must use the right array/object shapes.

`copy_canonical_json` parses the spec and writes it back using Wingui's ordered JSON representation. That gives foreign hosts a stable canonical string form before storing, hashing, diffing, or comparing specs.

`copy_normalized_json` applies the same structural validation and also assigns auto ids in the same `__auto__:path` style used by the C++ builder. That makes builder-side diffing and patchability more predictable for foreign hosts that do not want to assign every id manually.

`copy_patch_json` now normalizes both specs first and then computes the patch document using `ui_model_diff(...)` and `ui_patch_ops_to_json(...)`.

If the reconciler cannot express the change as an incremental patch, `out_requires_full_publish` is set to nonzero and no patch string is returned.

That keeps the API aligned with the real runtime behavior: incremental patch when possible, full republish when structure diverges.

## Normalized spec contract

`normalize` is the right term for the operation that turns a raw JSON object into the form Wingui expects for reliable reconciliation.

In this context, normalization means:

- validate against the same structural rules the native UI host enforces
- preserve ordered JSON output
- assign auto ids in the same `__auto__:path` style used by the C++ builder when ids are missing or empty

This is more than canonical serialization. Canonical serialization preserves data in a stable text form. Normalization also applies Wingui's builder-side contract so later diffing and patching behave predictably.

## Small C demo

The repository now includes a minimal authoring-side demo:

- `src/demo_c_spec_builder.c`

It does not run a hosted app. Instead it demonstrates the authoring helpers directly:

1. validate a JSON window spec
2. compare canonical and normalized output
3. compute a patch JSON document between two full specs
4. show a structural divergence case that requires full publish instead of a patch

Build it with:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\build_demo_impl.ps1" demo_c_spec_builder.c Release
```

That produces:

```text
manual_build\out\wingui_demo_c_spec_builder.exe
```

The sample shows the resulting canonical JSON, normalized JSON, incremental patch JSON, and a full-publish-required case in a message box so the authoring-side API can be exercised without opening the full Spec + Bind runtime.

## Recommended flow for foreign hosts

For non-C++ hosts that are constructing specs directly, the safest sequence is:

1. build the full JSON spec in the host language
2. call `wingui_spec_builder_validate_json(...)` during development or tests
3. call `wingui_spec_builder_copy_normalized_json(...)` before storing or comparing specs when stable ids matter
4. call `wingui_spec_builder_copy_patch_json(...)` when you want to know whether two specs can reconcile incrementally
5. pass the resulting full spec to `spec_bind` at runtime