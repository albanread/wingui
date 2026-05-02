const std = @import("std");

pub const raw = @cImport({
    @cInclude("windows.h");
    @cInclude("wingui/spec_builder.h");
    @cInclude("wingui/spec_bind.h");
    @cInclude("wingui/wingui.h");
});

pub const Error = std.mem.Allocator.Error || error{
    WinguiCallFailed,
    Utf8ContainsNul,
    NoSpaceLeft,
};

pub const PatchResult = union(enum) {
    patch: struct {
        json: [:0]u8,
        op_count: u32,
    },
    full_publish_required: struct {
        op_count: u32,
    },
};

pub const RunDesc = raw.WinguiSpecBindRunDesc;
pub const RunResult = raw.SuperTerminalRunResult;
pub const PatchMetrics = raw.SuperTerminalNativeUiPatchMetrics;
pub const PaneId = raw.SuperTerminalPaneId;
pub const PaneLayout = raw.SuperTerminalPaneLayout;
pub const PaneRef = raw.WinguiSpecBindPaneRef;
pub const GlyphAtlasInfo = raw.WinguiGlyphAtlasInfo;
pub const GraphicsColour = raw.WinguiGraphicsColour;
pub const TextGridCell = raw.SuperTerminalTextGridCell;
pub const IndexedGraphicsFrame = raw.SuperTerminalIndexedGraphicsFrame;
pub const RgbaFrame = raw.SuperTerminalRgbaFrame;
pub const SpriteId = raw.SuperTerminalSpriteId;
pub const SpriteInstance = raw.SuperTerminalSpriteInstance;
pub const VectorPrimitive = raw.WinguiVectorPrimitive;
pub const AssetId = raw.SuperTerminalAssetId;
pub const FreeFn = raw.SuperTerminalFreeFn;

fn fail() Error {
    return error.WinguiCallFailed;
}

fn requirePointerContext(comptime Context: type) void {
    if (@typeInfo(Context) != .pointer) {
        @compileError("Wingui Zig callbacks require a pointer user-data context.");
    }
}

fn cStringSlice(ptr: [*c]const u8) []const u8 {
    if (ptr == null) {
        return "";
    }
    return std.mem.span(@as([*:0]const u8, @ptrCast(ptr)));
}

fn allocTempZ(text: []const u8) Error![:0]u8 {
    if (std.mem.indexOfScalar(u8, text, 0) != null) {
        return error.Utf8ContainsNul;
    }
    return try std.heap.c_allocator.dupeZ(u8, text);
}

fn captureOwnedString(
    allocator: std.mem.Allocator,
    comptime CopyFn: anytype,
    args: anytype,
) Error![:0]u8 {
    var required_size: u32 = 0;
    if (@call(.auto, CopyFn, args ++ .{ null, 0, &required_size }) == 0 and required_size == 0) {
        return fail();
    }

    const buffer = try allocator.allocSentinel(u8, required_size - 1, 0);
    errdefer allocator.free(buffer);

    if (@call(.auto, CopyFn, args ++ .{ buffer.ptr, required_size, &required_size }) == 0) {
        return fail();
    }
    return buffer;
}

pub fn lastError() []const u8 {
    return cStringSlice(raw.wingui_last_error_utf8());
}

pub fn lastErrorOr(fallback: [*:0]const u8) [*:0]const u8 {
    const text = raw.wingui_last_error_utf8();
    if (text == null or text[0] == 0) {
        return fallback;
    }
    return text;
}

pub fn stringifyJsonToBufferZ(buffer: []u8, value: anytype) Error![:0]u8 {
    if (buffer.len == 0) {
        return error.NoSpaceLeft;
    }

    var stream = std.io.fixedBufferStream(buffer);
    try stream.writer().print("{f}", .{std.json.fmt(value, .{})});
    try stream.writer().writeByte(0);
    return buffer[0 .. stream.pos - 1 :0];
}

pub fn defaultRunDesc(title_utf8: [*:0]const u8) RunDesc {
    var desc = std.mem.zeroes(RunDesc);
    desc.title_utf8 = title_utf8;
    desc.columns = 120;
    desc.rows = 40;
    desc.command_queue_capacity = 1024;
    desc.event_queue_capacity = 1024;
    desc.font_pixel_height = 18;
    desc.dpi_scale = 1.0;
    desc.target_frame_ms = 16;
    desc.auto_request_present = 0;
    return desc;
}

pub const SpecBuilder = struct {
    pub fn validate(json: []const u8) Error!void {
        const json_z = try allocTempZ(json);
        defer std.heap.c_allocator.free(json_z);

        if (raw.wingui_spec_builder_validate_json(json_z.ptr) == 0) {
            return fail();
        }
    }

    pub fn canonical(allocator: std.mem.Allocator, json: []const u8) Error![:0]u8 {
        const json_z = try allocTempZ(json);
        defer std.heap.c_allocator.free(json_z);

        return captureOwnedString(allocator, raw.wingui_spec_builder_copy_canonical_json, .{json_z.ptr});
    }

    pub fn normalized(allocator: std.mem.Allocator, json: []const u8) Error![:0]u8 {
        const json_z = try allocTempZ(json);
        defer std.heap.c_allocator.free(json_z);

        return captureOwnedString(allocator, raw.wingui_spec_builder_copy_normalized_json, .{json_z.ptr});
    }

    pub fn patch(allocator: std.mem.Allocator, old_json: []const u8, new_json: []const u8) Error!PatchResult {
        const old_json_z = try allocTempZ(old_json);
        defer std.heap.c_allocator.free(old_json_z);
        const new_json_z = try allocTempZ(new_json);
        defer std.heap.c_allocator.free(new_json_z);

        var required_size: u32 = 0;
        var requires_full_publish: i32 = 0;
        var patch_op_count: u32 = 0;
        if (raw.wingui_spec_builder_copy_patch_json(
            old_json_z.ptr,
            new_json_z.ptr,
            null,
            0,
            &required_size,
            &requires_full_publish,
            &patch_op_count,
        ) == 0 and required_size == 0 and requires_full_publish == 0) {
            return fail();
        }

        if (requires_full_publish != 0) {
            return .{ .full_publish_required = .{ .op_count = patch_op_count } };
        }

        const buffer = try allocator.allocSentinel(u8, required_size - 1, 0);
        errdefer allocator.free(buffer);

        if (raw.wingui_spec_builder_copy_patch_json(
            old_json_z.ptr,
            new_json_z.ptr,
            buffer.ptr,
            required_size,
            &required_size,
            &requires_full_publish,
            &patch_op_count,
        ) == 0) {
            return fail();
        }

        return .{ .patch = .{ .json = buffer, .op_count = patch_op_count } };
    }
};

pub const EventView = struct {
    raw_view: *const raw.WinguiSpecBindEventView,

    pub fn name(self: EventView) []const u8 {
        return cStringSlice(self.raw_view.event_name_utf8);
    }

    pub fn payloadJson(self: EventView) []const u8 {
        return cStringSlice(self.raw_view.payload_json_utf8);
    }

    pub fn source(self: EventView) []const u8 {
        return cStringSlice(self.raw_view.source_utf8);
    }
};

pub const FrameView = struct {
    raw_view: *const raw.WinguiSpecBindFrameView,

    pub fn index(self: FrameView) u64 {
        return raw.wingui_spec_bind_frame_index(self.raw_view);
    }

    pub fn elapsedMs(self: FrameView) u64 {
        return raw.wingui_spec_bind_frame_elapsed_ms(self.raw_view);
    }

    pub fn deltaMs(self: FrameView) u64 {
        return raw.wingui_spec_bind_frame_delta_ms(self.raw_view);
    }

    pub fn targetFrameMs(self: FrameView) u32 {
        return raw.wingui_spec_bind_frame_target_frame_ms(self.raw_view);
    }

    pub fn bufferIndex(self: FrameView) u32 {
        return raw.wingui_spec_bind_frame_buffer_index(self.raw_view);
    }

    pub fn activeBufferIndex(self: FrameView) u32 {
        return raw.wingui_spec_bind_frame_active_buffer_index(self.raw_view);
    }

    pub fn bufferCount(self: FrameView) u32 {
        return raw.wingui_spec_bind_frame_buffer_count(self.raw_view);
    }

    pub fn resolvePane(self: FrameView, node_id: []const u8) Error!PaneRef {
        const node_id_z = try allocTempZ(node_id);
        defer std.heap.c_allocator.free(node_id_z);

        var pane: PaneRef = std.mem.zeroes(PaneRef);
        if (raw.wingui_spec_bind_frame_resolve_pane_utf8(self.raw_view, node_id_z.ptr, &pane) == 0) {
            return fail();
        }
        return pane;
    }

    pub fn bindPane(self: FrameView, pane_id: PaneId) Error!PaneRef {
        var pane: PaneRef = std.mem.zeroes(PaneRef);
        if (raw.wingui_spec_bind_frame_bind_pane(self.raw_view, pane_id, &pane) == 0) {
            return fail();
        }
        return pane;
    }

    pub fn paneLayout(self: FrameView, pane: PaneRef) Error!PaneLayout {
        var layout: PaneLayout = std.mem.zeroes(PaneLayout);
        if (raw.wingui_spec_bind_frame_get_pane_layout(self.raw_view, pane, &layout) == 0) {
            return fail();
        }
        return layout;
    }

    pub fn requestPresent(self: FrameView) Error!void {
        if (raw.wingui_spec_bind_frame_request_present(self.raw_view) == 0) {
            return fail();
        }
    }

    pub fn glyphAtlasInfo(self: FrameView) Error!GlyphAtlasInfo {
        var info: GlyphAtlasInfo = std.mem.zeroes(GlyphAtlasInfo);
        if (raw.wingui_spec_bind_frame_get_glyph_atlas_info(self.raw_view, &info) == 0) {
            return fail();
        }
        return info;
    }

    pub fn textGridWriteCells(self: FrameView, pane: PaneRef, cells: []const TextGridCell) Error!void {
        const ptr = if (cells.len == 0) null else cells.ptr;
        if (raw.wingui_spec_bind_frame_text_grid_write_cells(self.raw_view, pane, ptr, @intCast(cells.len)) == 0) {
            return fail();
        }
    }

    pub fn textGridClearRegion(
        self: FrameView,
        pane: PaneRef,
        row: u32,
        column: u32,
        width: u32,
        height: u32,
        fill_codepoint: u32,
        foreground: GraphicsColour,
        background: GraphicsColour,
    ) Error!void {
        if (raw.wingui_spec_bind_frame_text_grid_clear_region(
            self.raw_view,
            pane,
            row,
            column,
            width,
            height,
            fill_codepoint,
            foreground,
            background,
        ) == 0) {
            return fail();
        }
    }

    pub fn indexedGraphicsUpload(self: FrameView, pane: PaneRef, frame: *const IndexedGraphicsFrame) Error!void {
        if (raw.wingui_spec_bind_frame_indexed_graphics_upload(self.raw_view, pane, frame) == 0) {
            return fail();
        }
    }

    pub fn rgbaUpload(self: FrameView, pane: PaneRef, frame: *const RgbaFrame) Error!void {
        if (raw.wingui_spec_bind_frame_rgba_upload(self.raw_view, pane, frame) == 0) {
            return fail();
        }
    }

    pub fn rgbaGpuCopy(
        self: FrameView,
        dst_pane: PaneRef,
        dst_x: u32,
        dst_y: u32,
        src_pane: PaneRef,
        src_x: u32,
        src_y: u32,
        region_width: u32,
        region_height: u32,
    ) Error!void {
        if (raw.wingui_spec_bind_frame_rgba_gpu_copy(
            self.raw_view,
            dst_pane,
            dst_x,
            dst_y,
            src_pane,
            src_x,
            src_y,
            region_width,
            region_height,
        ) == 0) {
            return fail();
        }
    }

    pub fn registerRgbaAssetOwned(
        self: FrameView,
        width: u32,
        height: u32,
        bgra8_pixels: ?*anyopaque,
        source_pitch: u32,
        free_fn: ?FreeFn,
        free_user_data: ?*anyopaque,
    ) Error!AssetId {
        var asset_id: AssetId = std.mem.zeroes(AssetId);
        if (raw.wingui_spec_bind_frame_register_rgba_asset_owned(
            self.raw_view,
            width,
            height,
            bgra8_pixels,
            source_pitch,
            free_fn,
            free_user_data,
            &asset_id,
        ) == 0) {
            return fail();
        }
        return asset_id;
    }

    pub fn assetBlitToPane(
        self: FrameView,
        asset_id: AssetId,
        src_x: u32,
        src_y: u32,
        region_width: u32,
        region_height: u32,
        dst_pane: PaneRef,
        dst_x: u32,
        dst_y: u32,
    ) Error!void {
        if (raw.wingui_spec_bind_frame_asset_blit_to_pane(
            self.raw_view,
            asset_id,
            src_x,
            src_y,
            region_width,
            region_height,
            dst_pane,
            dst_x,
            dst_y,
        ) == 0) {
            return fail();
        }
    }

    pub fn defineSprite(
        self: FrameView,
        pane: PaneRef,
        sprite_id: SpriteId,
        frame_w: u32,
        frame_h: u32,
        frame_count: u32,
        frames_per_tick: u32,
        pixels: ?*anyopaque,
        palette: ?*anyopaque,
        free_fn: ?FreeFn,
        free_user_data: ?*anyopaque,
    ) Error!void {
        if (raw.wingui_spec_bind_frame_define_sprite(
            self.raw_view,
            pane,
            sprite_id,
            frame_w,
            frame_h,
            frame_count,
            frames_per_tick,
            pixels,
            palette,
            free_fn,
            free_user_data,
        ) == 0) {
            return fail();
        }
    }

    pub fn renderSprites(
        self: FrameView,
        pane: PaneRef,
        sprite_tick: u64,
        target_width: u32,
        target_height: u32,
        instances: []const SpriteInstance,
    ) Error!void {
        const ptr = if (instances.len == 0) null else instances.ptr;
        if (raw.wingui_spec_bind_frame_render_sprites(
            self.raw_view,
            pane,
            sprite_tick,
            target_width,
            target_height,
            ptr,
            @intCast(instances.len),
        ) == 0) {
            return fail();
        }
    }

    pub fn vectorDraw(
        self: FrameView,
        pane: PaneRef,
        content_buffer_mode: u32,
        blend_mode: u32,
        clear_before: i32,
        clear_color_rgba: *const [4]f32,
        primitives: []const VectorPrimitive,
    ) Error!void {
        const ptr = if (primitives.len == 0) null else primitives.ptr;
        if (raw.wingui_spec_bind_frame_vector_draw(
            self.raw_view,
            pane,
            content_buffer_mode,
            blend_mode,
            clear_before,
            clear_color_rgba,
            ptr,
            @intCast(primitives.len),
        ) == 0) {
            return fail();
        }
    }

    pub fn drawLine(
        self: FrameView,
        pane: PaneRef,
        content_buffer_mode: u32,
        blend_mode: u32,
        clear_before: i32,
        clear_color_rgba: *const [4]f32,
        x0: f32,
        y0: f32,
        x1: f32,
        y1: f32,
        half_thickness: f32,
        color_r: f32,
        color_g: f32,
        color_b: f32,
        color_a: f32,
    ) Error!void {
        if (raw.wingui_spec_bind_frame_draw_line(
            self.raw_view,
            pane,
            content_buffer_mode,
            blend_mode,
            clear_before,
            clear_color_rgba,
            x0,
            y0,
            x1,
            y1,
            half_thickness,
            color_r,
            color_g,
            color_b,
            color_a,
        ) == 0) {
            return fail();
        }
    }

    pub fn fillRect(
        self: FrameView,
        pane: PaneRef,
        content_buffer_mode: u32,
        blend_mode: u32,
        clear_before: i32,
        clear_color_rgba: *const [4]f32,
        x0: f32,
        y0: f32,
        x1: f32,
        y1: f32,
        corner_radius: f32,
        color_r: f32,
        color_g: f32,
        color_b: f32,
        color_a: f32,
    ) Error!void {
        if (raw.wingui_spec_bind_frame_fill_rect(
            self.raw_view,
            pane,
            content_buffer_mode,
            blend_mode,
            clear_before,
            clear_color_rgba,
            x0,
            y0,
            x1,
            y1,
            corner_radius,
            color_r,
            color_g,
            color_b,
            color_a,
        ) == 0) {
            return fail();
        }
    }

    pub fn strokeRect(
        self: FrameView,
        pane: PaneRef,
        content_buffer_mode: u32,
        blend_mode: u32,
        clear_before: i32,
        clear_color_rgba: *const [4]f32,
        x0: f32,
        y0: f32,
        x1: f32,
        y1: f32,
        half_thickness: f32,
        corner_radius: f32,
        color_r: f32,
        color_g: f32,
        color_b: f32,
        color_a: f32,
    ) Error!void {
        if (raw.wingui_spec_bind_frame_stroke_rect(
            self.raw_view,
            pane,
            content_buffer_mode,
            blend_mode,
            clear_before,
            clear_color_rgba,
            x0,
            y0,
            x1,
            y1,
            half_thickness,
            corner_radius,
            color_r,
            color_g,
            color_b,
            color_a,
        ) == 0) {
            return fail();
        }
    }

    pub fn fillCircle(
        self: FrameView,
        pane: PaneRef,
        content_buffer_mode: u32,
        blend_mode: u32,
        clear_before: i32,
        clear_color_rgba: *const [4]f32,
        cx: f32,
        cy: f32,
        radius: f32,
        color_r: f32,
        color_g: f32,
        color_b: f32,
        color_a: f32,
    ) Error!void {
        if (raw.wingui_spec_bind_frame_fill_circle(
            self.raw_view,
            pane,
            content_buffer_mode,
            blend_mode,
            clear_before,
            clear_color_rgba,
            cx,
            cy,
            radius,
            color_r,
            color_g,
            color_b,
            color_a,
        ) == 0) {
            return fail();
        }
    }

    pub fn strokeCircle(
        self: FrameView,
        pane: PaneRef,
        content_buffer_mode: u32,
        blend_mode: u32,
        clear_before: i32,
        clear_color_rgba: *const [4]f32,
        cx: f32,
        cy: f32,
        radius: f32,
        half_thickness: f32,
        color_r: f32,
        color_g: f32,
        color_b: f32,
        color_a: f32,
    ) Error!void {
        if (raw.wingui_spec_bind_frame_stroke_circle(
            self.raw_view,
            pane,
            content_buffer_mode,
            blend_mode,
            clear_before,
            clear_color_rgba,
            cx,
            cy,
            radius,
            half_thickness,
            color_r,
            color_g,
            color_b,
            color_a,
        ) == 0) {
            return fail();
        }
    }

    pub fn drawArc(
        self: FrameView,
        pane: PaneRef,
        content_buffer_mode: u32,
        blend_mode: u32,
        clear_before: i32,
        clear_color_rgba: *const [4]f32,
        cx: f32,
        cy: f32,
        radius: f32,
        half_thickness: f32,
        rotation_rad: f32,
        half_aperture_rad: f32,
        color_r: f32,
        color_g: f32,
        color_b: f32,
        color_a: f32,
    ) Error!void {
        if (raw.wingui_spec_bind_frame_draw_arc(
            self.raw_view,
            pane,
            content_buffer_mode,
            blend_mode,
            clear_before,
            clear_color_rgba,
            cx,
            cy,
            radius,
            half_thickness,
            rotation_rad,
            half_aperture_rad,
            color_r,
            color_g,
            color_b,
            color_a,
        ) == 0) {
            return fail();
        }
    }

    pub fn drawText(
        self: FrameView,
        pane: PaneRef,
        content_buffer_mode: u32,
        blend_mode: u32,
        clear_before: i32,
        clear_color_rgba: *const [4]f32,
        text: []const u8,
        origin_x: f32,
        origin_y: f32,
        color_r: f32,
        color_g: f32,
        color_b: f32,
        color_a: f32,
    ) Error!void {
        const text_z = try allocTempZ(text);
        defer std.heap.c_allocator.free(text_z);

        if (raw.wingui_spec_bind_frame_draw_text_utf8(
            self.raw_view,
            pane,
            content_buffer_mode,
            blend_mode,
            clear_before,
            clear_color_rgba,
            text_z.ptr,
            origin_x,
            origin_y,
            color_r,
            color_g,
            color_b,
            color_a,
        ) == 0) {
            return fail();
        }
    }

    pub fn indexedFillRect(self: FrameView, pane: PaneRef, x: u32, y: u32, width: u32, height: u32, palette_index: u32) Error!void {
        if (raw.wingui_spec_bind_frame_indexed_fill_rect(self.raw_view, pane, x, y, width, height, palette_index) == 0) {
            return fail();
        }
    }

    pub fn indexedDrawLine(self: FrameView, pane: PaneRef, x0: i32, y0: i32, x1: i32, y1: i32, palette_index: u32) Error!void {
        if (raw.wingui_spec_bind_frame_indexed_draw_line(self.raw_view, pane, x0, y0, x1, y1, palette_index) == 0) {
            return fail();
        }
    }
};

pub const Runtime = struct {
    handle: *raw.WinguiSpecBindRuntime,

    pub fn create() Error!Runtime {
        var runtime: ?*raw.WinguiSpecBindRuntime = null;
        if (raw.wingui_spec_bind_runtime_create(&runtime) == 0 or runtime == null) {
            return fail();
        }
        return .{ .handle = runtime.? };
    }

    pub fn destroy(self: *Runtime) void {
        raw.wingui_spec_bind_runtime_destroy(self.handle);
    }

    pub fn loadSpec(self: *Runtime, json: []const u8) Error!void {
        const json_z = try allocTempZ(json);
        defer std.heap.c_allocator.free(json_z);

        if (raw.wingui_spec_bind_runtime_load_spec_json(self.handle, json_z.ptr) == 0) {
            return fail();
        }
    }

    pub fn copySpec(self: *Runtime, allocator: std.mem.Allocator) Error![:0]u8 {
        return captureOwnedString(allocator, raw.wingui_spec_bind_runtime_copy_spec_json, .{self.handle});
    }

    pub fn bindEvent(self: *Runtime, event_name: []const u8, context: anytype, comptime handler: fn (@TypeOf(context), *Runtime, EventView) void) Error!void {
        const Context = @TypeOf(context);
        requirePointerContext(Context);

        const event_name_z = try allocTempZ(event_name);
        defer std.heap.c_allocator.free(event_name_z);

        const Trampoline = struct {
            fn callback(user_data: ?*anyopaque, runtime: ?*raw.WinguiSpecBindRuntime, event_view: ?*const raw.WinguiSpecBindEventView) callconv(.c) void {
                var wrapped_runtime = Runtime{ .handle = runtime.? };
                const typed_context: Context = @ptrCast(@alignCast(user_data.?));
                handler(typed_context, &wrapped_runtime, .{ .raw_view = event_view.? });
            }
        };

        if (raw.wingui_spec_bind_runtime_bind_event(self.handle, event_name_z.ptr, Trampoline.callback, context) == 0) {
            return fail();
        }
    }

    pub fn unbindEvent(self: *Runtime, event_name: []const u8) Error!void {
        const event_name_z = try allocTempZ(event_name);
        defer std.heap.c_allocator.free(event_name_z);

        if (raw.wingui_spec_bind_runtime_unbind_event(self.handle, event_name_z.ptr) == 0) {
            return fail();
        }
    }

    pub fn clearBindings(self: *Runtime) void {
        raw.wingui_spec_bind_runtime_clear_bindings(self.handle);
    }

    pub fn setDefaultHandler(self: *Runtime, context: anytype, comptime handler: fn (@TypeOf(context), *Runtime, EventView) void) void {
        const Context = @TypeOf(context);
        requirePointerContext(Context);

        const Trampoline = struct {
            fn callback(user_data: ?*anyopaque, runtime: ?*raw.WinguiSpecBindRuntime, event_view: ?*const raw.WinguiSpecBindEventView) callconv(.c) void {
                var wrapped_runtime = Runtime{ .handle = runtime.? };
                const typed_context: Context = @ptrCast(@alignCast(user_data.?));
                handler(typed_context, &wrapped_runtime, .{ .raw_view = event_view.? });
            }
        };

        raw.wingui_spec_bind_runtime_set_default_handler(self.handle, Trampoline.callback, context);
    }

    pub fn setFrameHandler(self: *Runtime, context: anytype, comptime handler: fn (@TypeOf(context), *Runtime, FrameView) void) void {
        const Context = @TypeOf(context);
        requirePointerContext(Context);

        const Trampoline = struct {
            fn callback(user_data: ?*anyopaque, runtime: ?*raw.WinguiSpecBindRuntime, frame_view: ?*const raw.WinguiSpecBindFrameView) callconv(.c) void {
                var wrapped_runtime = Runtime{ .handle = runtime.? };
                const typed_context: Context = @ptrCast(@alignCast(user_data.?));
                handler(typed_context, &wrapped_runtime, .{ .raw_view = frame_view.? });
            }
        };

        raw.wingui_spec_bind_runtime_set_frame_handler(self.handle, Trampoline.callback, context);
    }

    pub fn requestStop(self: *Runtime, exit_code: i32) Error!void {
        if (raw.wingui_spec_bind_runtime_request_stop(self.handle, exit_code) == 0) {
            return fail();
        }
    }

    pub fn getPatchMetrics(self: *Runtime) Error!PatchMetrics {
        var metrics: PatchMetrics = std.mem.zeroes(PatchMetrics);
        if (raw.wingui_spec_bind_runtime_get_patch_metrics(self.handle, &metrics) == 0) {
            return fail();
        }
        return metrics;
    }

    pub fn run(self: *Runtime, desc: *const RunDesc) Error!RunResult {
        var result: RunResult = std.mem.zeroes(RunResult);
        if (raw.wingui_spec_bind_runtime_run(self.handle, desc, &result) == 0) {
            return fail();
        }
        return result;
    }
};
