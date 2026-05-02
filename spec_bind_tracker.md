# Spec + Bind Tracker

## Current slice

- [x] Name the feature: `Spec + Bind`
- [x] Write the design note
- [x] Create a dedicated public header
- [x] Create a dedicated implementation source file
- [x] Keep the implementation out of the existing large native host files
- [x] Reuse `super_terminal_run_hosted_app(...)` instead of creating a second host loop
- [x] Support loading a full JSON spec before run
- [x] Support rebinding named events to C callbacks
- [x] Support republishing a new full JSON spec while running
- [x] Support a default event handler
- [x] Support close-request fallback when no handler is bound
- [x] Add a dedicated Spec+Bind demo
- [x] Add an example client in plain C
- [x] Add a small Zig sample
- [ ] Add a foreign-language sample
- [ ] Add JSON patch helpers
- [ ] Add a separate reusable binding-set object
- [ ] Add UTF-8 file load/save helpers

## Public API in v1

- `wingui_spec_bind_runtime_create`
- `wingui_spec_bind_runtime_destroy`
- `wingui_spec_bind_runtime_load_spec_json`
- `wingui_spec_bind_runtime_copy_spec_json`
- `wingui_spec_bind_runtime_bind_event`
- `wingui_spec_bind_runtime_unbind_event`
- `wingui_spec_bind_runtime_clear_bindings`
- `wingui_spec_bind_runtime_set_default_handler`
- `wingui_spec_bind_runtime_request_stop`
- `wingui_spec_bind_runtime_get_patch_metrics`
- `wingui_spec_bind_runtime_run`

## Samples

- [x] C++ reference demo: `src/demo_spec_bind.cpp`
- [x] Plain C sample: `src/demo_c_spec_bind.c`
- [x] Zig sample: `src/demo_zig_spec_bind.zig`

## Validation checklist

- [ ] Header compiles in C and C++ consumers
- [ ] DLL build includes `src/spec_bind.cpp`
- [ ] Runtime can publish an initial JSON spec
- [ ] Bound native UI event dispatch reaches the correct callback
- [ ] Unbound close request exits cleanly