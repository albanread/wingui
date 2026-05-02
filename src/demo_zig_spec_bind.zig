const std = @import("std");
const wingui = @import("zig/wingui.zig");
const c = wingui.raw;

const demo_title = "Spec + Bind Zig Demo";
const demo_title_z: [*:0]const u8 = demo_title;

const DemoState = struct {
    runtime: ?*wingui.Runtime = null,
    enabled: bool = true,
    clicks: u32 = 0,
    event_count: u32 = 0,
    last_event: []const u8 = "startup",
    spec_buffer: [8192]u8 = [_]u8{0} ** 8192,
    patch_metrics: wingui.PatchMetrics = std.mem.zeroes(wingui.PatchMetrics),

    fn reset(self: *DemoState) void {
        self.enabled = true;
        self.clicks = 0;
        self.event_count = 0;
        self.patch_metrics = std.mem.zeroes(wingui.PatchMetrics);
        self.last_event = "startup";
    }

    fn markEvent(self: *DemoState, value: []const u8) void {
        self.event_count += 1;
        self.last_event = value;
    }

    fn updatePatchMetrics(self: *DemoState) void {
        if (self.runtime) |runtime| {
            self.patch_metrics = runtime.getPatchMetrics() catch return;
        }
    }

    fn buildSpec(self: *DemoState) ![:0]u8 {
        self.updatePatchMetrics();

        var metrics_buffer: [256]u8 = undefined;
        var clicks_buffer: [32]u8 = undefined;
        var events_buffer: [32]u8 = undefined;
        var patches_buffer: [32]u8 = undefined;
        var rebuilds_buffer: [32]u8 = undefined;
        var last_event_buffer: [96]u8 = undefined;
        var counter_label_buffer: [96]u8 = undefined;
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

        const status_text = if (self.enabled) "Mode enabled" else "Mode paused";
        const toggle_text = if (self.enabled) "Pause" else "Resume";
        const enabled_text = if (self.enabled)
            "Interactions are active."
        else
            "Interactions are paused in the state model, but still patching.";
        const rebuild_count = self.patch_metrics.subtree_rebuild_count + self.patch_metrics.window_rebuild_count;
        const clicks_text = try std.fmt.bufPrint(&clicks_buffer, "Clicks {}", .{self.clicks});
        const events_text = try std.fmt.bufPrint(&events_buffer, "Events {}", .{self.event_count});
        const patches_text = try std.fmt.bufPrint(&patches_buffer, "Patches {}", .{self.patch_metrics.direct_apply_count});
        const rebuilds_text = try std.fmt.bufPrint(&rebuilds_buffer, "Rebuilds {}", .{rebuild_count});
        const last_event_text = try std.fmt.bufPrint(&last_event_buffer, "Last {s}", .{self.last_event});
        const counter_label = try std.fmt.bufPrint(&counter_label_buffer, "Hello from Zig. The counter is {}.", .{self.clicks});

        return wingui.stringifyJsonToBufferZ(&self.spec_buffer, .{
            .type = "window",
            .id = "demo_zig_window",
            .title = demo_title,
            .menuBar = .{
                .menus = .{
                    .{
                        .text = "File",
                        .items = .{
                            .{ .id = "zig_counter_up", .text = "Count click" },
                            .{ .separator = true },
                            .{ .id = "zig_exit", .text = "Exit" },
                        },
                    },
                    .{
                        .text = "State",
                        .items = .{
                            .{ .id = "zig_toggle_enabled", .text = "Enabled", .checked = self.enabled },
                            .{ .id = "zig_reset", .text = "Reset" },
                        },
                    },
                },
            },
            .commandBar = .{
                .items = .{
                    .{ .id = "zig_counter_up", .text = "Count click" },
                    .{ .id = "zig_toggle_enabled", .text = "Enabled", .checked = self.enabled },
                    .{ .separator = true },
                    .{ .id = "zig_reset", .text = "Reset" },
                },
            },
            .statusBar = .{
                .parts = .{
                    .{ .text = status_text },
                    .{ .text = clicks_text, .width = 90 },
                    .{ .text = events_text, .width = 90 },
                    .{ .text = patches_text, .width = 100 },
                    .{ .text = rebuilds_text, .width = 110 },
                    .{ .text = last_event_text, .width = 180 },
                },
            },
            .body = .{
                .type = "stack",
                .id = "demo_zig_body",
                .gap = 12,
                .children = .{
                    .{ .type = "heading", .id = "demo_zig_heading", .text = "Spec + Bind (Zig)" },
                    .{ .type = "text", .id = "demo_zig_intro", .text = "This sample is written in Zig and uses a small Zig wrapper over the public Spec + Bind ABI." },
                    .{ .type = "text", .id = "demo_zig_metrics", .text = metrics_summary },
                    .{
                        .type = "card",
                        .id = "demo_zig_actions_card",
                        .title = "Actions",
                        .children = .{
                            .{
                                .type = "row",
                                .id = "demo_zig_actions_row",
                                .gap = 10,
                                .children = .{
                                    .{ .type = "button", .id = "demo_zig_counter_up", .text = "Count click", .event = "zig_counter_up" },
                                    .{ .type = "button", .id = "demo_zig_reset", .text = "Reset", .event = "zig_reset" },
                                    .{ .type = "button", .id = "demo_zig_toggle_enabled", .text = toggle_text, .event = "zig_toggle_enabled" },
                                },
                            },
                            .{ .type = "text", .id = "demo_zig_counter_label", .text = counter_label },
                            .{ .type = "text", .id = "demo_zig_enabled_label", .text = enabled_text },
                        },
                    },
                },
            },
        });
    }

    fn republish(self: *DemoState) !void {
        const runtime = self.runtime orelse return error.WinguiCallFailed;
        const spec = try self.buildSpec();
        try runtime.loadSpec(spec);
    }
};

fn onCounterUp(state: *DemoState, _: *wingui.Runtime, _: wingui.EventView) void {
    state.clicks += 1;
    state.markEvent("zig_counter_up");
    state.republish() catch {};
}

fn onReset(state: *DemoState, _: *wingui.Runtime, _: wingui.EventView) void {
    state.clicks = 0;
    state.markEvent("zig_reset");
    state.republish() catch {};
}

fn onToggleEnabled(state: *DemoState, _: *wingui.Runtime, _: wingui.EventView) void {
    state.enabled = !state.enabled;
    state.markEvent("zig_toggle_enabled");
    state.republish() catch {};
}

fn onExit(state: *DemoState, runtime: *wingui.Runtime, _: wingui.EventView) void {
    state.markEvent("zig_exit");
    runtime.requestStop(0) catch {};
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
    var desc = wingui.defaultRunDesc(demo_title_z);

    state.reset();
    state.runtime = &runtime;

    runtime.bindEvent("zig_counter_up", &state, onCounterUp) catch {
        return failWithMessage(demo_title_z);
    };
    runtime.bindEvent("zig_reset", &state, onReset) catch {
        return failWithMessage(demo_title_z);
    };
    runtime.bindEvent("zig_toggle_enabled", &state, onToggleEnabled) catch {
        return failWithMessage(demo_title_z);
    };
    runtime.bindEvent("zig_exit", &state, onExit) catch {
        return failWithMessage(demo_title_z);
    };

    state.republish() catch {
        return failWithMessage(demo_title_z);
    };

    desc.columns = 110;
    desc.rows = 30;

    const result = runtime.run(&desc) catch {
        return failWithMessage(demo_title_z);
    };

    return @intCast(result.exit_code);
}
