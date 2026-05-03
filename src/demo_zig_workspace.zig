const std = @import("std");
const wingui = @import("zig/wingui.zig");
const c = wingui.raw;

const demo_title = "Wingui Zig Workspace";
const demo_title_z: [*:0]const u8 = demo_title;
const primitive_capacity = 96;
const right_column_width: u32 = 560;
const editor_columns: u32 = 84;
const editor_rows: u32 = 38;
const repl_columns: u32 = 56;
const repl_rows: u32 = 14;
const editor_cell_capacity = 4096;
const repl_cell_capacity = 2048;

const DemoState = struct {
    runtime: ?*wingui.Runtime = null,
    status_text: []const u8 = "Workspace ready",
    last_command: []const u8 = "startup",
    build_count: u32 = 0,
    run_count: u32 = 0,
    show_repl_history: bool = true,
    text_grids_dirty: bool = true,
    spec_buffer: [8192]u8 = [_]u8{0} ** 8192,
    primitives: [primitive_capacity]wingui.VectorPrimitive = undefined,

    fn buildSpec(self: *DemoState) ![:0]u8 {
        var builds_buffer: [32]u8 = undefined;
        var runs_buffer: [32]u8 = undefined;
        var last_buffer: [80]u8 = undefined;
        const builds_text = try std.fmt.bufPrint(&builds_buffer, "Builds {}", .{self.build_count});
        const runs_text = try std.fmt.bufPrint(&runs_buffer, "Runs {}", .{self.run_count});
        const last_text = try std.fmt.bufPrint(&last_buffer, "Last {s}", .{self.last_command});

        return wingui.stringifyJsonToBufferZ(&self.spec_buffer, .{
            .type = "window",
            .id = "zig_workspace_window",
            .title = demo_title,
            .menuBar = .{
                .menus = .{
                    .{
                        .text = "File",
                        .items = .{
                            .{ .id = "workspace_new_file", .text = "New file" },
                            .{ .id = "workspace_reset", .text = "Reset workspace" },
                            .{ .separator = true },
                            .{ .id = "workspace_exit", .text = "Exit" },
                        },
                    },
                    .{
                        .text = "Build",
                        .items = .{
                            .{ .id = "workspace_build", .text = "Build" },
                            .{ .id = "workspace_run", .text = "Run" },
                            .{ .id = "workspace_clear_repl", .text = "Clear REPL" },
                        },
                    },
                },
            },
            .commandBar = .{
                .items = .{
                    .{ .id = "workspace_new_file", .text = "New file" },
                    .{ .separator = true },
                    .{ .id = "workspace_build", .text = "Build" },
                    .{ .id = "workspace_run", .text = "Run" },
                    .{ .id = "workspace_clear_repl", .text = "Clear REPL" },
                },
            },
            .statusBar = .{
                .parts = .{
                    .{ .text = self.status_text, .width = 220 },
                    .{ .text = builds_text, .width = 90 },
                    .{ .text = runs_text, .width = 90 },
                    .{ .text = last_text, .width = 180 },
                },
            },
            .body = .{
                .type = "row",
                .id = "workspace_client_row",
                .gap = 10,
                .children = .{
                    .{
                        .type = "card",
                        .id = "workspace_editor_card",
                        .title = "Editor",
                        .children = .{
                            .{ .type = "text-grid", .id = "workspace_editor", .columns = editor_columns, .rows = editor_rows, .focused = true },
                        },
                    },
                    .{
                        .type = "stack",
                        .id = "workspace_right_column",
                        .gap = 10,
                        .children = .{
                            .{
                                .type = "card",
                                .id = "workspace_graphics_card",
                                .title = "Graphics",
                                .children = .{
                                    .{ .type = "rgba-pane", .id = "workspace_graphics", .width = right_column_width, .height = 320 },
                                },
                            },
                            .{
                                .type = "card",
                                .id = "workspace_repl_card",
                                .title = "REPL",
                                .children = .{
                                    .{ .type = "text-grid", .id = "workspace_repl", .width = right_column_width, .columns = repl_columns, .rows = repl_rows },
                                },
                            },
                        },
                    },
                },
            },
        });
    }

    fn republish(self: *DemoState) !void {
        const runtime = self.runtime orelse return error.WinguiCallFailed;
        try runtime.loadSpec(try self.buildSpec());
    }

    fn markCommand(self: *DemoState, command: []const u8, status: []const u8) void {
        self.last_command = command;
        self.status_text = status;
        self.text_grids_dirty = true;
    }

    fn reset(self: *DemoState) void {
        self.status_text = "Workspace ready";
        self.last_command = "reset";
        self.build_count = 0;
        self.run_count = 0;
        self.show_repl_history = true;
        self.text_grids_dirty = true;
    }

    fn draw(self: *DemoState, frame: wingui.FrameView) void {
        self.drawGraphics(frame) catch {};
    }

    fn refreshRetainedText(self: *DemoState) !void {
        const runtime = self.runtime orelse return error.WinguiCallFailed;
        const editor_pane = try runtime.resolvePaneId("workspace_editor");
        const repl_pane = try runtime.resolvePaneId("workspace_repl");

        try self.writeEditorRetained(runtime, editor_pane);
        try self.writeReplRetained(runtime, repl_pane);
        self.text_grids_dirty = false;
    }

    fn writeEditorRetained(self: *DemoState, runtime: *wingui.Runtime, pane: wingui.PaneId) !void {
        _ = self;
        const fg = rgba(0xd8, 0xde, 0xe9, 0xff);
        const bg = rgba(0x18, 0x1d, 0x28, 0xff);
        const gutter_fg = rgba(0x6b, 0x72, 0x84, 0xff);
        const keyword_fg = rgba(0x88, 0xc0, 0xd0, 0xff);
        const accent_fg = rgba(0xeb, 0xcb, 0x8b, 0xff);
        var cells: [editor_cell_capacity]wingui.TextGridCell = undefined;
        var count: usize = 0;

        try runtime.clearRetainedTextGridRegion(pane, 0, 0, editor_columns, editor_rows, ' ', fg, bg);

        const lines = [_][]const u8{
            "const std = @import(\"std\");",
            "const workspace = @import(\"host/workspace.zig\");",
            "",
            "pub fn main() !void {",
            "    var app = try workspace.App.init(std.heap.c_allocator);",
            "    defer app.deinit();",
            "",
            "    try app.openProject(\"sample_project\");",
            "    try app.attachCompilerHost();",
            "    try app.run();",
            "}",
            "",
            "// Future host: compiler worker, diagnostics, symbol index,",
            "// build queue, and interactive REPL all living in one shell.",
        };

        for (lines, 0..) |line, index| {
            if (index >= editor_rows) break;
            var gutter_buffer: [8]u8 = undefined;
            const gutter = std.fmt.bufPrint(&gutter_buffer, "{d: >3} ", .{index + 1}) catch continue;
            appendTextCells(cells[0..], &count, @intCast(index), 0, gutter, gutter_fg, bg, editor_columns);
            appendTextCells(cells[0..], &count, @intCast(index), 4, line, fg, bg, editor_columns);
        }

        appendTextCells(cells[0..], &count, 0, 4, "const", keyword_fg, bg, editor_columns);
        appendTextCells(cells[0..], &count, 3, 0, "  >", accent_fg, bg, editor_columns);
        appendTextCells(cells[0..], &count, 3, 7, "pub", keyword_fg, bg, editor_columns);

        try runtime.writeRetainedTextGridCells(pane, cells[0..count]);
    }

    fn writeReplRetained(self: *DemoState, runtime: *wingui.Runtime, pane: wingui.PaneId) !void {
        const fg = rgba(0xe5, 0xe9, 0xf0, 0xff);
        const bg = rgba(0x11, 0x15, 0x1d, 0xff);
        const prompt_fg = rgba(0xa3, 0xbe, 0x8c, 0xff);
        const note_fg = rgba(0x81, 0xa1, 0xc1, 0xff);
        var cells: [repl_cell_capacity]wingui.TextGridCell = undefined;
        var count: usize = 0;

        try runtime.clearRetainedTextGridRegion(pane, 0, 0, repl_columns, repl_rows, ' ', fg, bg);

        const transcript: []const []const u8 = if (self.show_repl_history)
            &[_][]const u8{
                "> workspace.init()",
                "ok: shell online",
                "> compiler.attach()",
                "pending: compiler host not attached yet",
                "> status()",
                "editor=text-grid graphics=rgba repl=text-grid",
                "",
                "// compiler output and eval transcript will land here",
            }
        else
            &[_][]const u8{
                "> clear",
                "repl cleared",
                "",
                "// compiler output and eval transcript will land here",
            };

        for (transcript, 0..) |line, index| {
            if (index >= repl_rows) break;
            const colour = if (std.mem.startsWith(u8, line, ">")) prompt_fg else if (std.mem.startsWith(u8, line, "//")) note_fg else fg;
            appendTextCells(cells[0..], &count, @intCast(index), 0, line, colour, bg, repl_columns);
        }

        try runtime.writeRetainedTextGridCells(pane, cells[0..count]);
    }

    fn drawGraphics(self: *DemoState, frame: wingui.FrameView) !void {
        const pane = try frame.resolvePane("workspace_graphics");
        const layout = try frame.paneLayout(pane);
        const clear = [4]f32{ 0.05, 0.07, 0.10, 1.0 };
        const width = @as(f32, @floatFromInt(layout.width));
        const height = @as(f32, @floatFromInt(layout.height));
        const t = @as(f32, @floatFromInt(frame.index()));
        const center_x = width * 0.5;
        const center_y = height * 0.5;
        const orbit_x = center_x + @cos(t * 0.035) * width * 0.22;
        const orbit_y = center_y + @sin(t * 0.042) * height * 0.18;
        const build_glow = @min(@as(f32, @floatFromInt(self.build_count)) * 0.05, 0.35);
        var primitive_count: usize = 0;

        self.primitives[primitive_count] = makeFilledRect(0, 0, width, height, clear[0], clear[1], clear[2], clear[3]);
        primitive_count += 1;
        self.primitives[primitive_count] = makeFilledRect(18, 18, width - 18, height - 18, 0.08, 0.11, 0.16, 1.0);
        primitive_count += 1;
        self.primitives[primitive_count] = makeLine(28, center_y, width - 28, center_y, 0.55, 0.18, 0.24, 0.34, 0.55);
        primitive_count += 1;
        self.primitives[primitive_count] = makeLine(center_x, 28, center_x, height - 28, 0.55, 0.18, 0.24, 0.34, 0.55);
        primitive_count += 1;

        var x: f32 = 34;
        while (x < width - 34 and primitive_count < self.primitives.len - 8) : (x += 22) {
            const y = center_y + @sin(t * 0.04 + x * 0.03) * height * 0.16;
            self.primitives[primitive_count] = makeFilledCircle(x, y, 2.4, 0.45, 0.82, 1.0, 0.82);
            primitive_count += 1;
        }

        self.primitives[primitive_count] = makeLine(center_x, center_y, orbit_x, orbit_y, 0.9, 0.64, 0.78, 0.98, 0.86);
        primitive_count += 1;
        self.primitives[primitive_count] = makeFilledCircle(center_x, center_y, 8.0 + build_glow * 12.0, 0.92, 0.62, 0.28, 0.88);
        primitive_count += 1;
        self.primitives[primitive_count] = makeFilledCircle(orbit_x, orbit_y, 6.5, 0.48, 0.88, 0.76, 0.94);
        primitive_count += 1;

        try frame.vectorDraw(
            pane,
            wingui.RgbaContentBufferMode.frame,
            wingui.RgbaBlendMode.replace,
            1,
            &clear,
            self.primitives[0..primitive_count],
        );

        var overlay_buffer: [96]u8 = undefined;
        const overlay = try std.fmt.bufPrint(&overlay_buffer, "workspace preview  builds {}  runs {}", .{ self.build_count, self.run_count });
        try frame.drawText(
            pane,
            wingui.RgbaContentBufferMode.frame,
            wingui.RgbaBlendMode.alpha_over,
            0,
            &clear,
            overlay,
            14,
            14,
            0.94,
            0.96,
            1.0,
            0.95,
        );
    }
};

fn onNewFile(state: *DemoState, _: *wingui.Runtime, _: wingui.EventView) void {
    state.show_repl_history = true;
    state.markCommand("new_file", "New workspace buffer created");
    state.republish() catch {};
}

fn onReset(state: *DemoState, _: *wingui.Runtime, _: wingui.EventView) void {
    state.reset();
    state.republish() catch {};
}

fn onBuild(state: *DemoState, _: *wingui.Runtime, _: wingui.EventView) void {
    state.build_count += 1;
    state.show_repl_history = true;
    state.markCommand("build", "Build queued for future compiler host");
    state.republish() catch {};
}

fn onRun(state: *DemoState, _: *wingui.Runtime, _: wingui.EventView) void {
    state.run_count += 1;
    state.show_repl_history = true;
    state.markCommand("run", "Run requested from workspace shell");
    state.republish() catch {};
}

fn onClearRepl(state: *DemoState, _: *wingui.Runtime, _: wingui.EventView) void {
    state.show_repl_history = false;
    state.markCommand("clear_repl", "REPL transcript cleared");
    state.republish() catch {};
}

fn onExit(_: *DemoState, runtime: *wingui.Runtime, _: wingui.EventView) void {
    runtime.requestStop(0) catch {};
}

fn onFrame(state: *DemoState, runtime: *wingui.Runtime, frame: wingui.FrameView) void {
    const escape_down = frame.keyDown(wingui.Key.escape) catch false;
    if (escape_down) {
        runtime.requestStop(0) catch {};
        return;
    }
    if (state.text_grids_dirty) {
        state.refreshRetainedText() catch {};
    }
    state.draw(frame);
}

fn failWithMessage(title: [*:0]const u8) u8 {
    _ = c.MessageBoxA(null, wingui.lastErrorOr("Unknown error"), title, c.MB_OK | c.MB_ICONERROR);
    return 1;
}

pub fn main() u8 {
    var runtime = wingui.Runtime.create() catch {
        return failWithMessage(demo_title_z);
    };
    defer runtime.destroy();

    var state = DemoState{};
    state.runtime = &runtime;

    var desc = wingui.defaultRunDesc(demo_title_z);
    desc.columns = 150;
    desc.rows = 48;
    desc.auto_request_present = 1;

    runtime.bindEvent("workspace_new_file", &state, onNewFile) catch return failWithMessage(demo_title_z);
    runtime.bindEvent("workspace_reset", &state, onReset) catch return failWithMessage(demo_title_z);
    runtime.bindEvent("workspace_build", &state, onBuild) catch return failWithMessage(demo_title_z);
    runtime.bindEvent("workspace_run", &state, onRun) catch return failWithMessage(demo_title_z);
    runtime.bindEvent("workspace_clear_repl", &state, onClearRepl) catch return failWithMessage(demo_title_z);
    runtime.bindEvent("workspace_exit", &state, onExit) catch return failWithMessage(demo_title_z);
    runtime.setFrameHandler(&state, onFrame);

    state.republish() catch return failWithMessage(demo_title_z);

    const result = runtime.run(&desc) catch {
        return failWithMessage(demo_title_z);
    };
    return @intCast(result.exit_code);
}

fn appendTextCells(
    cells: []wingui.TextGridCell,
    count: *usize,
    row: u32,
    column: u32,
    text: []const u8,
    foreground: wingui.GraphicsColour,
    background: wingui.GraphicsColour,
    max_columns: u32,
) void {
    var col = column;
    for (text) |byte| {
        if (col >= max_columns or count.* >= cells.len) break;
        cells[count.*] = .{
            .row = row,
            .column = col,
            .codepoint = byte,
            .foreground = foreground,
            .background = background,
        };
        count.* += 1;
        col += 1;
    }
}

fn rgba(r: u8, g: u8, b: u8, a: u8) wingui.GraphicsColour {
    return .{ .r = r, .g = g, .b = b, .a = a };
}

fn makeFilledRect(x0: f32, y0: f32, x1: f32, y1: f32, r: f32, g: f32, b: f32, a: f32) wingui.VectorPrimitive {
    var primitive: wingui.VectorPrimitive = std.mem.zeroes(wingui.VectorPrimitive);
    primitive.bounds_min_x = x0;
    primitive.bounds_min_y = y0;
    primitive.bounds_max_x = x1;
    primitive.bounds_max_y = y1;
    primitive.param0[0] = 0;
    primitive.color[0] = r;
    primitive.color[1] = g;
    primitive.color[2] = b;
    primitive.color[3] = a;
    primitive.shape = c.WINGUI_VECTOR_RECT_FILLED;
    return primitive;
}

fn makeLine(x0: f32, y0: f32, x1: f32, y1: f32, half_thickness: f32, r: f32, g: f32, b: f32, a: f32) wingui.VectorPrimitive {
    const pad = half_thickness + 1.0;
    var primitive: wingui.VectorPrimitive = std.mem.zeroes(wingui.VectorPrimitive);
    primitive.bounds_min_x = @min(x0, x1) - pad;
    primitive.bounds_min_y = @min(y0, y1) - pad;
    primitive.bounds_max_x = @max(x0, x1) + pad;
    primitive.bounds_max_y = @max(y0, y1) + pad;
    primitive.param0[0] = x0;
    primitive.param0[1] = y0;
    primitive.param0[2] = x1;
    primitive.param0[3] = y1;
    primitive.param1[0] = half_thickness;
    primitive.color[0] = r;
    primitive.color[1] = g;
    primitive.color[2] = b;
    primitive.color[3] = a;
    primitive.shape = c.WINGUI_VECTOR_LINE;
    return primitive;
}

fn makeFilledCircle(cx: f32, cy: f32, radius: f32, r: f32, g: f32, b: f32, a: f32) wingui.VectorPrimitive {
    var primitive: wingui.VectorPrimitive = std.mem.zeroes(wingui.VectorPrimitive);
    primitive.bounds_min_x = cx - radius - 1.0;
    primitive.bounds_min_y = cy - radius - 1.0;
    primitive.bounds_max_x = cx + radius + 1.0;
    primitive.bounds_max_y = cy + radius + 1.0;
    primitive.param0[0] = cx;
    primitive.param0[1] = cy;
    primitive.param0[2] = radius;
    primitive.color[0] = r;
    primitive.color[1] = g;
    primitive.color[2] = b;
    primitive.color[3] = a;
    primitive.shape = c.WINGUI_VECTOR_CIRCLE_FILLED;
    return primitive;
}
