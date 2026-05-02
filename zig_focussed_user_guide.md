# Zig-Focused User Guide

## What this is

This guide is for Zig users who want to drive Wingui through the Zig wrapper rather than the raw C ABI.

The relevant files are:

- `src/zig/wingui.zig`: the Zig wrapper
- `src/demo_zig_spec_bind.zig`: the main Zig runtime sample
- `src/demo_zig_starfield.zig`: a Zig graphics and keyboard-input sample
- `build_zig_spec_bind.ps1`: the current Windows build script for the Zig sample
- `build_zig_starfield.ps1`: the Windows build script for the Zig starfield sample
- `include/wingui/spec_bind.h`: the underlying runtime C ABI
- `include/wingui/spec_builder.h`: the underlying authoring-side C ABI

The short version is:

- build specs with normal Zig data and JSON serialization
- run them through `wingui.Runtime`
- bind named events with Zig callbacks
- republish a full spec whenever your host state changes

## Mental model

Wingui's Zig story is intentionally thin.

The wrapper does not try to invent a separate Zig UI framework. It gives Zig users a more natural layer over the existing C ABI:

- Zig slices instead of raw `char*` where possible
- Zig error returns instead of manual success-code checks
- typed callback trampolines for event and frame handlers
- small helpers for JSON serialization and copied strings

The core split is still:

- build with `SpecBuilder`
- run with `Runtime`

That means:

1. your Zig app owns its own state
2. your Zig app builds a full JSON window spec
3. `Runtime.loadSpec(...)` publishes or patches that spec internally
4. event callbacks mutate your state and republish

## First example

The current Zig demo follows this shape:

```zig
const std = @import("std");
const wingui = @import("zig/wingui.zig");

const App = struct {
    runtime: ?*wingui.Runtime = null,
    count: u32 = 0,
    spec_buffer: [4096]u8 = [_]u8{0} ** 4096,

    fn buildSpec(self: *App) ![:0]u8 {
        return wingui.stringifyJsonToBufferZ(&self.spec_buffer, .{
            .type = "window",
            .id = "example_window",
            .title = "Wingui Zig Example",
            .body = .{
                .type = "stack",
                .id = "root",
                .children = .{
                    .{ .type = "text", .id = "label", .text = "Hello from Zig" },
                    .{ .type = "button", .id = "inc", .text = "Increment", .event = "inc" },
                },
            },
        });
    }

    fn republish(self: *App) !void {
        const runtime = self.runtime orelse return error.WinguiCallFailed;
        try runtime.loadSpec(try self.buildSpec());
    }
};

fn onIncrement(app: *App, _: *wingui.Runtime, _: wingui.EventView) void {
    app.count += 1;
    app.republish() catch {};
}
```

That is the intended style. Keep app state in Zig. Rebuild the full spec from that state. Let Wingui decide whether the update becomes a patch or a full publish.

## The wrapper surface

The main entry points in `src/zig/wingui.zig` are:

- `wingui.Runtime`: runtime lifetime, binding, publishing, running, patch metrics
- `wingui.SpecBuilder`: validation, canonicalization, normalization, patch generation
- `wingui.EventView`: typed access to callback event name, payload JSON, and source
- `wingui.FrameView`: typed access to frame timing and pane/graphics helpers
- `wingui.stringifyJsonToBufferZ(...)`: write Zig data as JSON into a caller-owned zero-terminated buffer
- `wingui.defaultRunDesc(...)`: create a reasonable default run descriptor
- `wingui.lastError()` and `wingui.lastErrorOr(...)`: retrieve the last Wingui error text

### Runtime lifecycle

The normal runtime flow is:

1. `var runtime = try wingui.Runtime.create();`
2. build a `RunDesc` with `wingui.defaultRunDesc(...)`
3. bind one or more events with `runtime.bindEvent(...)`
4. publish the initial spec with `runtime.loadSpec(...)`
5. call `try runtime.run(&desc)`

Typical setup looks like this:

```zig
var runtime = try wingui.Runtime.create();
defer runtime.destroy();

var state = App{};
state.runtime = &runtime;

var desc = wingui.defaultRunDesc("My Zig App");
desc.columns = 100;
desc.rows = 30;

try runtime.bindEvent("inc", &state, onIncrement);
try state.republish();

const result = try runtime.run(&desc);
return @intCast(result.exit_code);
```

### Events

Event handlers are keyed by the JSON `event` string in your spec.

The Zig callback shape is:

```zig
fn handler(context: *MyState, runtime: *wingui.Runtime, event: wingui.EventView) void
```

Use `EventView` when you care about the details of the callback:

```zig
fn onEvent(state: *MyState, _: *wingui.Runtime, event: wingui.EventView) void {
    const name = event.name();
    const payload = event.payloadJson();
    const source = event.source();
    _ = state;
    _ = name;
    _ = payload;
    _ = source;
}
```

If you do not need `runtime` or `event`, declare them as `_` directly in the parameter list. That keeps the sample code feeling like Zig rather than translated C.

### Republishing

Republishing should usually stay as an ordinary Zig helper on your state type:

```zig
fn republish(self: *App) !void {
    const runtime = self.runtime orelse return error.WinguiCallFailed;
    try runtime.loadSpec(try self.buildSpec());
}
```

That is preferable to manually threading `loadSpec(...)` calls through every callback body.

## Building specs in Zig

The most natural pattern today is:

1. keep typed app state in Zig structs
2. derive small formatted strings into stack buffers where needed
3. pass anonymous Zig structs into `stringifyJsonToBufferZ(...)`

The current demo does exactly that. It computes status strings with `std.fmt.bufPrint(...)` and then serializes a nested anonymous struct tree into one stable spec buffer.

This is a better fit for Zig than hand-writing JSON strings.

Example:

```zig
var label_buffer: [64]u8 = undefined;
const label = try std.fmt.bufPrint(&label_buffer, "Count {}", .{self.count});

return wingui.stringifyJsonToBufferZ(&self.spec_buffer, .{
    .type = "window",
    .id = "counter_window",
    .title = "Counter",
    .body = .{
        .type = "stack",
        .id = "body",
        .children = .{
            .{ .type = "text", .id = "count_label", .text = label },
            .{ .type = "button", .id = "inc", .text = "Increment", .event = "inc" },
        },
    },
});
```

## Authoring helpers with `SpecBuilder`

`Runtime` is the execution side. `SpecBuilder` is the authoring side.

The Zig wrapper exposes:

- `SpecBuilder.validate(...)`
- `SpecBuilder.canonical(...)`
- `SpecBuilder.normalized(...)`
- `SpecBuilder.patch(...)`

Use these when you want validation or authoring-time transforms before runtime execution.

Example:

```zig
try wingui.SpecBuilder.validate(spec_json);

const normalized = try wingui.SpecBuilder.normalized(allocator, spec_json);
defer allocator.free(normalized);
```

For patch generation:

```zig
const result = try wingui.SpecBuilder.patch(allocator, old_json, new_json);
switch (result) {
    .patch => |patch| {
        defer allocator.free(patch.json);
    },
    .full_publish_required => |_| {},
}
```

That is useful for tools, tests, authoring workflows, or foreign-host integration experiments. It is not required for ordinary runtime use.

## Ownership rules

There are two ownership patterns to remember.

### Caller-owned buffers

`stringifyJsonToBufferZ(...)` writes into a caller-owned buffer and returns a sentinel-terminated slice pointing into that same buffer.

That means:

- keep the backing buffer alive for as long as you need the returned slice
- reuse one stable buffer in your state when rebuilding specs repeatedly

### Allocated returned strings

Some wrapper helpers allocate and return owned sentinel slices, for example:

- `Runtime.copySpec(...)`
- `SpecBuilder.canonical(...)`
- `SpecBuilder.normalized(...)`
- `SpecBuilder.patch(...)` when it returns a patch JSON string

Those should be freed by the allocator you passed in.

## Error handling

The Zig wrapper converts failing C calls into Zig errors.

Typical style:

```zig
var runtime = wingui.Runtime.create() catch {
    return failWithMessage(title_z);
};
```

If you want human-readable Wingui error text, use:

- `wingui.lastError()` for a Zig slice
- `wingui.lastErrorOr(...)` when you need a zero-terminated fallback string

The current demo uses `lastErrorOr(...)` because it forwards the text directly to `MessageBoxA`.

## Frame-time drawing

If your spec includes hosted text-grid, indexed, or RGBA panes, bind a frame handler with:

```zig
runtime.setFrameHandler(&state, onFrame);
```

The frame callback shape is:

```zig
fn onFrame(state: *MyState, runtime: *wingui.Runtime, frame: wingui.FrameView) void
```

From `FrameView` you can:

- inspect timing information such as `index()`, `elapsedMs()`, and `deltaMs()`
- poll low-latency keyboard state with `keyDown(...)` and `keyboardState()`
- resolve pane ids from declarative node ids with `resolvePane(...)`
- bind directly to known pane ids with `bindPane(...)`
- query pane layout with `paneLayout(...)`
- issue text-grid, indexed, RGBA, sprite, asset, and vector draw calls

This part of the wrapper is still intentionally close to the underlying runtime API because it maps directly onto the pane graphics surface.

## Real-time keyboard input

The Zig wrapper now exposes frame-scoped keyboard polling for frame-driven interactions.

The small convenience surface is:

- `wingui.Key.left`, `wingui.Key.up`, `wingui.Key.right`, `wingui.Key.down`
- `wingui.Key.space`, `wingui.Key.escape`
- `frame.keyDown(...)`
- `frame.keyboardState()`

That is the right tool for held-key movement, steering, or camera control. It avoids treating high-frequency directional intent as a stream of ordinary declarative UI events.

Example:

```zig
fn onFrame(state: *MyState, runtime: *wingui.Runtime, frame: wingui.FrameView) void {
    _ = runtime;

    if (frame.keyDown(wingui.Key.left) catch false) {
        state.heading_x = -1;
    }
    if (frame.keyDown(wingui.Key.right) catch false) {
        state.heading_x = 1;
    }
    if (frame.keyDown(wingui.Key.up) catch false) {
        state.heading_y = -1;
    }
    if (frame.keyDown(wingui.Key.down) catch false) {
        state.heading_y = 1;
    }
}
```

Use declarative events for commands and state toggles. Use frame key polling for continuous motion.

## Starfield sample

The repository now includes a Zig sample focused on exactly that split:

- declarative window chrome through JSON
- a command bar and status bar through `Runtime.loadSpec(...)`
- an RGBA pane driven by `setFrameHandler(...)`
- arrow-key steering via `frame.keyDown(...)`

See `src/demo_zig_starfield.zig`.

It is a good reference when you want:

- a command bar for discrete actions such as reset, slower, faster, and pause
- a status bar for lightweight telemetry
- a real-time graphics pane that reacts to held keys every frame

## Building and running the Zig demo

The repository already has a Windows build script for the Zig demo:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\build_zig_spec_bind.ps1 Release
```

That script:

- expects `manual_build\out\wingui.lib` to exist already
- links against the packaged Wingui output in `manual_build\out`
- builds `src/demo_zig_spec_bind.zig`
- includes `app_manifest.rc`
- uses `--subsystem windows`
- writes the final executable to `manual_build\out\wingui_demo_zig_spec_bind.exe`

To build and run in one step:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\build_zig_spec_bind.ps1 Release -Run
```

If the packaged Wingui outputs do not exist yet, first build any packaged Wingui demo so that `wingui.lib`, `wingui.dll`, and the shader assets are present in `manual_build\out`.

To build the starfield sample:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\build_zig_starfield.ps1 Release
```

To build and run it in one step:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\build_zig_starfield.ps1 Release -Run
```

That writes the final executable to `manual_build\out\wingui_demo_zig_starfield.exe`.

## Practical guidance for Zig users

- Keep application state in ordinary Zig structs.
- Prefer slices and typed values over manually managed C-style buffers in your own app code.
- Use `stringifyJsonToBufferZ(...)` instead of hand-writing JSON.
- Treat `Runtime.loadSpec(...)` as the single place where host state becomes UI.
- Use frame key polling for held-key movement and declarative events for commands.
- Use `SpecBuilder` for validation and authoring tools, not as a required part of the runtime path.
- Let the wrapper hide the C ABI details, but do not expect it to replace Zig's own data modeling.

## Current limitations

The Zig wrapper is intentionally small. It aims to make the existing ABI pleasant to use, not to introduce a second declarative DSL or a deep Zig-native widget layer.

So today:

- you still describe UI as JSON-shaped data
- you still republish a full spec after state changes
- frame-time graphics helpers remain relatively close to the C runtime surface

That is deliberate. It keeps the wrapper predictable and keeps the real portability boundary in one place.