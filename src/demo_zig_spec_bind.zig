const std = @import("std");

const c = @cImport({
    @cInclude("windows.h");
    @cInclude("wingui/spec_bind.h");
    @cInclude("wingui/wingui.h");
});

const DemoState = struct {
    runtime: ?*c.WinguiSpecBindRuntime = null,
    enabled: bool = true,
    clicks: u32 = 0,
    event_count: u32 = 0,
    last_event: [64]u8 = [_]u8{0} ** 64,
    spec_buffer: [8192]u8 = [_]u8{0} ** 8192,
    patch_metrics: c.SuperTerminalNativeUiPatchMetrics = std.mem.zeroes(c.SuperTerminalNativeUiPatchMetrics),

    fn reset(self: *DemoState) void {
        self.enabled = true;
        self.clicks = 0;
        self.event_count = 0;
        self.patch_metrics = std.mem.zeroes(c.SuperTerminalNativeUiPatchMetrics);
        self.setLastEvent("startup");
    }

    fn setLastEvent(self: *DemoState, value: []const u8) void {
        @memset(&self.last_event, 0);
        const count = @min(value.len, self.last_event.len - 1);
        @memcpy(self.last_event[0..count], value[0..count]);
    }

    fn lastEventSlice(self: *const DemoState) []const u8 {
        return std.mem.sliceTo(&self.last_event, 0);
    }

    fn markEvent(self: *DemoState, value: []const u8) void {
        self.event_count += 1;
        self.setLastEvent(value);
    }

    fn updatePatchMetrics(self: *DemoState) void {
        if (self.runtime) |runtime| {
            var metrics = std.mem.zeroes(c.SuperTerminalNativeUiPatchMetrics);
            if (c.wingui_spec_bind_runtime_get_patch_metrics(runtime, &metrics) != 0) {
                self.patch_metrics = metrics;
            }
        }
    }

    fn buildSpec(self: *DemoState) ![:0]u8 {
        self.updatePatchMetrics();

        var metrics_buffer: [256]u8 = undefined;
        const metrics_summary = try std.fmt.bufPrint(
            &metrics_buffer,
            "Patch metrics: publish={} patch={} direct={} subtree={} window={} failed={}",
            .{
                self.patch_metrics.publish_count,
                self.patch_metrics.patch_request_count,
                self.patch_metrics.direct_apply_count,
                self.patch_metrics.subtree_rebuild_count,
                self.patch_metrics.window_rebuild_count,
                self.patch_metrics.failed_patch_count,
            },
        );

        var stream = std.io.fixedBufferStream(&self.spec_buffer);
        const writer = stream.writer();

        try writer.writeAll("{\n");
        try writer.writeAll("  \"type\": \"window\",\n");
        try writer.writeAll("  \"id\": \"demo_zig_window\",\n");
        try writer.writeAll("  \"title\": \"Spec + Bind Zig Demo\",\n");
        try writer.writeAll("  \"menuBar\": {\n");
        try writer.writeAll("    \"menus\": [\n");
        try writer.writeAll("      {\n");
        try writer.writeAll("        \"text\": \"File\",\n");
        try writer.writeAll("        \"items\": [\n");
        try writer.writeAll("          { \"id\": \"zig_counter_up\", \"text\": \"Count click\" },\n");
        try writer.writeAll("          { \"separator\": true },\n");
        try writer.writeAll("          { \"id\": \"zig_exit\", \"text\": \"Exit\" }\n");
        try writer.writeAll("        ]\n");
        try writer.writeAll("      },\n");
        try writer.writeAll("      {\n");
        try writer.writeAll("        \"text\": \"State\",\n");
        try writer.writeAll("        \"items\": [\n");
        try writer.print("          {{ \"id\": \"zig_toggle_enabled\", \"text\": \"Enabled\", \"checked\": {} }},\n", .{self.enabled});
        try writer.writeAll("          { \"id\": \"zig_reset\", \"text\": \"Reset\" }\n");
        try writer.writeAll("        ]\n");
        try writer.writeAll("      }\n");
        try writer.writeAll("    ]\n");
        try writer.writeAll("  },\n");

        try writer.writeAll("  \"commandBar\": {\n");
        try writer.writeAll("    \"items\": [\n");
        try writer.writeAll("      { \"id\": \"zig_counter_up\", \"text\": \"Count click\" },\n");
        try writer.print("      {{ \"id\": \"zig_toggle_enabled\", \"text\": \"Enabled\", \"checked\": {} }},\n", .{self.enabled});
        try writer.writeAll("      { \"separator\": true },\n");
        try writer.writeAll("      { \"id\": \"zig_reset\", \"text\": \"Reset\" }\n");
        try writer.writeAll("    ]\n");
        try writer.writeAll("  },\n");

        try writer.writeAll("  \"statusBar\": {\n");
        try writer.writeAll("    \"parts\": [\n");
        try writer.print("      {{ \"text\": \"{s}\" }},\n", .{if (self.enabled) "Mode enabled" else "Mode paused"});
        try writer.print("      {{ \"text\": \"Clicks {}\", \"width\": 90 }},\n", .{self.clicks});
        try writer.print("      {{ \"text\": \"Events {}\", \"width\": 90 }},\n", .{self.event_count});
        try writer.print("      {{ \"text\": \"Patches {}\", \"width\": 100 }},\n", .{self.patch_metrics.direct_apply_count});
        try writer.print("      {{ \"text\": \"Rebuilds {}\", \"width\": 110 }},\n", .{self.patch_metrics.subtree_rebuild_count + self.patch_metrics.window_rebuild_count});
        try writer.print("      {{ \"text\": \"Last {s}\", \"width\": 180 }}\n", .{self.lastEventSlice()});
        try writer.writeAll("    ]\n");
        try writer.writeAll("  },\n");

        try writer.writeAll("  \"body\": {\n");
        try writer.writeAll("    \"type\": \"stack\",\n");
        try writer.writeAll("    \"id\": \"demo_zig_body\",\n");
        try writer.writeAll("    \"gap\": 12,\n");
        try writer.writeAll("    \"children\": [\n");
        try writer.writeAll("      { \"type\": \"heading\", \"id\": \"demo_zig_heading\", \"text\": \"Spec + Bind (Zig)\" },\n");
        try writer.writeAll("      { \"type\": \"text\", \"id\": \"demo_zig_intro\", \"text\": \"This sample is written in Zig and uses the public Spec + Bind C ABI through @cImport.\" },\n");
        try writer.print("      {{ \"type\": \"text\", \"id\": \"demo_zig_metrics\", \"text\": \"{s}\" }},\n", .{metrics_summary});
        try writer.writeAll("      {\n");
        try writer.writeAll("        \"type\": \"card\",\n");
        try writer.writeAll("        \"id\": \"demo_zig_actions_card\",\n");
        try writer.writeAll("        \"title\": \"Actions\",\n");
        try writer.writeAll("        \"children\": [\n");
        try writer.writeAll("          {\n");
        try writer.writeAll("            \"type\": \"row\",\n");
        try writer.writeAll("            \"id\": \"demo_zig_actions_row\",\n");
        try writer.writeAll("            \"gap\": 10,\n");
        try writer.writeAll("            \"children\": [\n");
        try writer.writeAll("              { \"type\": \"button\", \"id\": \"demo_zig_counter_up\", \"text\": \"Count click\", \"event\": \"zig_counter_up\" },\n");
        try writer.writeAll("              { \"type\": \"button\", \"id\": \"demo_zig_reset\", \"text\": \"Reset\", \"event\": \"zig_reset\" },\n");
        try writer.print("              {{ \"type\": \"button\", \"id\": \"demo_zig_toggle_enabled\", \"text\": \"{s}\", \"event\": \"zig_toggle_enabled\" }}\n", .{if (self.enabled) "Pause" else "Resume"});
        try writer.writeAll("            ]\n");
        try writer.writeAll("          },\n");
        try writer.print("          {{ \"type\": \"text\", \"id\": \"demo_zig_counter_label\", \"text\": \"Hello from Zig. The counter is {}.\" }},\n", .{self.clicks});
        try writer.print("          {{ \"type\": \"text\", \"id\": \"demo_zig_enabled_label\", \"text\": \"{s}\" }}\n", .{if (self.enabled) "Interactions are active." else "Interactions are paused in the state model, but still patching."});
        try writer.writeAll("        ]\n");
        try writer.writeAll("      }\n");
        try writer.writeAll("    ]\n");
        try writer.writeAll("  }\n");
        try writer.writeAll("}\n");
        try writer.writeByte(0);

        return self.spec_buffer[0 .. stream.pos - 1 :0];
    }

    fn republish(self: *DemoState) bool {
        const runtime = self.runtime orelse return false;
        const spec = self.buildSpec() catch return false;
        return c.wingui_spec_bind_runtime_load_spec_json(runtime, spec.ptr) != 0;
    }
};

fn stateFromUserData(user_data: ?*anyopaque) *DemoState {
    return @ptrCast(@alignCast(user_data.?));
}

fn onCounterUp(user_data: ?*anyopaque, runtime: ?*c.WinguiSpecBindRuntime, event_view: ?*const c.WinguiSpecBindEventView) callconv(.c) void {
    _ = runtime;
    _ = event_view;
    const state = stateFromUserData(user_data);
    state.clicks += 1;
    state.markEvent("zig_counter_up");
    _ = state.republish();
}

fn onReset(user_data: ?*anyopaque, runtime: ?*c.WinguiSpecBindRuntime, event_view: ?*const c.WinguiSpecBindEventView) callconv(.c) void {
    _ = runtime;
    _ = event_view;
    const state = stateFromUserData(user_data);
    state.clicks = 0;
    state.markEvent("zig_reset");
    _ = state.republish();
}

fn onToggleEnabled(user_data: ?*anyopaque, runtime: ?*c.WinguiSpecBindRuntime, event_view: ?*const c.WinguiSpecBindEventView) callconv(.c) void {
    _ = runtime;
    _ = event_view;
    const state = stateFromUserData(user_data);
    state.enabled = !state.enabled;
    state.markEvent("zig_toggle_enabled");
    _ = state.republish();
}

fn onExit(user_data: ?*anyopaque, runtime: ?*c.WinguiSpecBindRuntime, event_view: ?*const c.WinguiSpecBindEventView) callconv(.c) void {
    _ = event_view;
    const state = stateFromUserData(user_data);
    state.markEvent("zig_exit");
    if (runtime) |value| {
        _ = c.wingui_spec_bind_runtime_request_stop(value, 0);
    }
}

fn failWithMessage(title: [*:0]const u8) u8 {
    const message = c.wingui_last_error_utf8();
    const fallback: [*:0]const u8 = "Unknown error";
    const text = if (message != null and message[0] != 0) message else fallback;
    _ = c.MessageBoxA(null, text, title, c.MB_OK | c.MB_ICONERROR);
    return 1;
}

pub fn main() u8 {
    var runtime: ?*c.WinguiSpecBindRuntime = null;
    var state = DemoState{};
    var desc = std.mem.zeroes(c.WinguiSpecBindRunDesc);
    var result = std.mem.zeroes(c.SuperTerminalRunResult);

    state.reset();

    if (c.wingui_spec_bind_runtime_create(&runtime) == 0) {
        return failWithMessage("Spec + Bind Zig Demo");
    }
    state.runtime = runtime;

    const ok = c.wingui_spec_bind_runtime_bind_event(runtime, "zig_counter_up", onCounterUp, &state) != 0 and
        c.wingui_spec_bind_runtime_bind_event(runtime, "zig_reset", onReset, &state) != 0 and
        c.wingui_spec_bind_runtime_bind_event(runtime, "zig_toggle_enabled", onToggleEnabled, &state) != 0 and
        c.wingui_spec_bind_runtime_bind_event(runtime, "zig_exit", onExit, &state) != 0 and
        state.republish();

    if (!ok) {
        c.wingui_spec_bind_runtime_destroy(runtime);
        return failWithMessage("Spec + Bind Zig Demo");
    }

    desc.title_utf8 = "Spec + Bind Zig Demo";
    desc.columns = 110;
    desc.rows = 30;
    desc.command_queue_capacity = 1024;
    desc.event_queue_capacity = 1024;
    desc.font_pixel_height = 18;
    desc.dpi_scale = 1.0;
    desc.target_frame_ms = 16;
    desc.auto_request_present = 0;

    if (c.wingui_spec_bind_runtime_run(runtime, &desc, &result) == 0) {
        c.wingui_spec_bind_runtime_destroy(runtime);
        return failWithMessage("Spec + Bind Zig Demo");
    }

    c.wingui_spec_bind_runtime_destroy(runtime);
    return @intCast(result.exit_code);
}
