const std = @import("std");
const wingui = @import("zig/wingui.zig");
const c = wingui.raw;

const demo_title = "Wingui Zig Graphics Demo";
const demo_title_z: [*:0]const u8 = demo_title;
const canvas_width: f32 = 960;
const canvas_height: f32 = 560;
const star_count = 110;
const boid_count = 36;
const diagonal_scale: f32 = 0.70710677;
const star_primitives_per_star = 4;
const background_primitive_count = 20;
const plasma_columns = 28;
const plasma_rows = 16;
const plasma_cell_count = plasma_columns * plasma_rows;
const primitive_capacity = 768;

const DrawMode = enum {
    starfield_2d,
    starfield_3d,
    plasma,
    boids,
    lissajous,
};

const Star = struct {
    x: f32,
    y: f32,
    depth: f32,
    prev_x: f32 = 0,
    prev_y: f32 = 0,
    prev_depth: f32 = 0,
    kind: u8 = 0,
    phase: u32 = 0,
};

const Boid = struct {
    x: f32,
    y: f32,
    vx: f32,
    vy: f32,
    phase: f32 = 0,
};

const DemoState = struct {
    runtime: ?*wingui.Runtime = null,
    prng: std.Random.DefaultPrng,
    draw_mode: DrawMode = .starfield_2d,
    paused: bool = false,
    speed: f32 = 180.0,
    dir_x: i8 = 0,
    dir_y: i8 = -1,
    steer_x: f32 = 0,
    steer_y: f32 = 0,
    warp_active: bool = false,
    prev_space_down: bool = false,
    prev_escape_down: bool = false,
    plasma_phase: f32 = 0,
    plasma_shift_x: f32 = 0,
    plasma_shift_y: f32 = 0,
    boid_phase: f32 = 0,
    lissajous_phase: f32 = 0,
    canvas_w: f32 = canvas_width,
    canvas_h: f32 = canvas_height,
    spec_buffer: [8192]u8 = [_]u8{0} ** 8192,
    primitives: [primitive_capacity]wingui.VectorPrimitive = undefined,
    stars: [star_count]Star = undefined,
    boids: [boid_count]Boid = undefined,

    fn init() DemoState {
        var state = DemoState{
            .prng = std.Random.DefaultPrng.init(0x5a17f13d),
        };
        state.resetScene();
        return state;
    }

    fn randomUnit(self: *DemoState) f32 {
        var random = self.prng.random();
        return random.float(f32);
    }

    fn randomRange(self: *DemoState, max: f32) f32 {
        return self.randomUnit() * max;
    }

    fn randomSigned(self: *DemoState, magnitude: f32) f32 {
        return (self.randomUnit() * 2.0 - 1.0) * magnitude;
    }

    fn randomDepth(self: *DemoState) f32 {
        return 0.15 + self.randomUnit() * 0.85;
    }

    fn randomDepth3d(self: *DemoState) f32 {
        return 40.0 + self.randomUnit() * 220.0;
    }

    fn respawnStar3d(self: *DemoState, star: *Star) void {
        const x = self.randomSigned(260.0);
        const y = self.randomSigned(160.0);
        const depth = self.randomDepth3d();
        const kind: u8 = if (self.randomUnit() < 0.08) 1 else 0;
        const phase: u32 = @intFromFloat(self.randomUnit() * 64.0);
        star.* = .{
            .x = x,
            .y = y,
            .depth = depth,
            .prev_x = x,
            .prev_y = y,
            .prev_depth = depth,
            .kind = kind,
            .phase = phase,
        };
    }

    fn resetStar(self: *DemoState, star: *Star) void {
        switch (self.draw_mode) {
            .starfield_2d => {
                star.* = .{
                    .x = self.randomRange(self.canvas_w),
                    .y = self.randomRange(self.canvas_h),
                    .depth = self.randomDepth(),
                    .prev_x = 0,
                    .prev_y = 0,
                    .prev_depth = 0,
                    .kind = 0,
                    .phase = 0,
                };
            },
            .starfield_3d => {
                self.respawnStar3d(star);
            },
            .plasma, .boids, .lissajous => {
                star.* = .{
                    .x = 0,
                    .y = 0,
                    .depth = 0,
                    .prev_x = 0,
                    .prev_y = 0,
                    .prev_depth = 0,
                    .kind = 0,
                    .phase = 0,
                };
            },
        }
    }

    fn resetBoid(self: *DemoState, boid: *Boid) void {
        const angle = self.randomUnit() * std.math.tau;
        const speed = 28.0 + self.randomUnit() * 40.0;
        boid.* = .{
            .x = self.randomRange(self.canvas_w),
            .y = self.randomRange(self.canvas_h),
            .vx = @cos(angle) * speed,
            .vy = @sin(angle) * speed,
            .phase = self.randomUnit() * std.math.tau,
        };
    }

    fn resetBoids(self: *DemoState) void {
        for (&self.boids) |*boid| {
            self.resetBoid(boid);
        }
    }

    fn resetModeState(self: *DemoState) void {
        self.warp_active = false;
        switch (self.draw_mode) {
            .starfield_2d, .starfield_3d => self.resetStars(),
            .plasma => {
                self.plasma_phase = 0;
                self.plasma_shift_x = 0;
                self.plasma_shift_y = 0;
            },
            .boids => {
                self.boid_phase = 0;
                self.resetBoids();
            },
            .lissajous => {
                self.lissajous_phase = 0;
            },
        }
    }

    fn resetStars(self: *DemoState) void {
        for (&self.stars) |*star| {
            self.resetStar(star);
        }
    }

    fn resetScene(self: *DemoState) void {
        self.paused = false;
        self.speed = 180.0;
        self.dir_x = 0;
        self.dir_y = -1;
        self.steer_x = 0;
        self.steer_y = 0;
        self.warp_active = false;
        self.prev_space_down = false;
        self.prev_escape_down = false;
        self.plasma_phase = 0;
        self.plasma_shift_x = 0;
        self.plasma_shift_y = 0;
        self.boid_phase = 0;
        self.lissajous_phase = 0;
        self.resetStars();
        self.resetBoids();
    }

    fn headingLabel(self: *const DemoState) []const u8 {
        return headingLabelFor(self.dir_x, self.dir_y);
    }

    fn drawModeLabel(self: *const DemoState) []const u8 {
        return switch (self.draw_mode) {
            .starfield_2d => "2D starfield",
            .starfield_3d => "3D rush",
            .plasma => "Plasma",
            .boids => "Boids",
            .lissajous => "Lissajous",
        };
    }

    fn detailCount(self: *const DemoState) u32 {
        return switch (self.draw_mode) {
            .starfield_2d, .starfield_3d => star_count,
            .plasma => plasma_cell_count,
            .boids => boid_count,
            .lissajous => 220,
        };
    }

    fn detailLabel(self: *const DemoState) []const u8 {
        return switch (self.draw_mode) {
            .starfield_2d, .starfield_3d => "Stars",
            .plasma => "Cells",
            .boids => "Boids",
            .lissajous => "Curves",
        };
    }

    fn setDirection(self: *DemoState, dir_x: i8, dir_y: i8) bool {
        if (self.dir_x == dir_x and self.dir_y == dir_y) {
            return false;
        }
        self.dir_x = dir_x;
        self.dir_y = dir_y;
        return true;
    }

    fn setDrawMode(self: *DemoState, draw_mode: DrawMode) bool {
        if (self.draw_mode == draw_mode) {
            return false;
        }
        self.draw_mode = draw_mode;
        self.resetModeState();
        return true;
    }

    fn togglePause(self: *DemoState) void {
        self.paused = !self.paused;
    }

    fn increaseSpeed(self: *DemoState) void {
        self.speed = @min(self.speed + 25.0, 420.0);
    }

    fn decreaseSpeed(self: *DemoState) void {
        self.speed = @max(self.speed - 25.0, 40.0);
    }

    fn republish(self: *DemoState) !void {
        const runtime = self.runtime orelse return error.WinguiCallFailed;
        try runtime.loadSpec(try self.buildSpec());
    }

    fn buildSpec(self: *DemoState) ![:0]u8 {
        var heading_buffer: [64]u8 = undefined;
        var speed_buffer: [32]u8 = undefined;
        var detail_buffer: [32]u8 = undefined;
        var draw_mode_buffer: [48]u8 = undefined;
        const mode_text = if (self.paused) "Paused" else "Cruising";
        const controls_text = switch (self.draw_mode) {
            .starfield_3d => "Arrows steer  Space warps  Esc exits",
            .plasma => "Arrows drift  Space pauses  Esc exits",
            .boids => "Arrows attract  Space pauses  Esc exits",
            .lissajous => "Arrows skew  Space pauses  Esc exits",
            .starfield_2d => "Arrows steer  Space pauses  Esc exits",
        };
        const heading_text = try std.fmt.bufPrint(&heading_buffer, "Heading {s}", .{self.headingLabel()});
        const speed_units: u32 = @intFromFloat(self.speed);
        const speed_text = try std.fmt.bufPrint(&speed_buffer, "Speed {}", .{speed_units});
        const detail_text = try std.fmt.bufPrint(&detail_buffer, "{s} {}", .{ self.detailLabel(), self.detailCount() });
        const draw_mode_text = try std.fmt.bufPrint(&draw_mode_buffer, "Mode {s}", .{self.drawModeLabel()});
        const pause_text = if (self.paused) "Resume" else "Pause";

        return wingui.stringifyJsonToBufferZ(&self.spec_buffer, .{
            .type = "window",
            .id = "zig_graphics_demo_window",
            .title = demo_title,
            .menuBar = .{
                .menus = .{
                    .{
                        .text = "File",
                        .items = .{
                            .{ .id = "starfield_reset", .text = "Reset field" },
                            .{ .separator = true },
                            .{ .id = "starfield_exit", .text = "Exit" },
                        },
                    },
                    .{
                        .text = "Draw",
                        .items = .{
                            .{ .id = "starfield_draw_2d", .text = "2D starfield", .checked = self.draw_mode == .starfield_2d },
                            .{ .id = "starfield_draw_3d", .text = "3D starfield", .checked = self.draw_mode == .starfield_3d },
                            .{ .id = "starfield_draw_plasma", .text = "Plasma", .checked = self.draw_mode == .plasma },
                            .{ .id = "starfield_draw_boids", .text = "Boids", .checked = self.draw_mode == .boids },
                            .{ .id = "starfield_draw_lissajous", .text = "Lissajous", .checked = self.draw_mode == .lissajous },
                        },
                    },
                },
            },
            .commandBar = .{
                .items = .{
                    .{ .id = "starfield_slower", .text = "Slower" },
                    .{ .id = "starfield_faster", .text = "Faster" },
                    .{ .id = "starfield_reset", .text = "Reset field" },
                    .{ .id = "starfield_toggle_pause", .text = pause_text, .checked = self.paused },
                    .{ .separator = true },
                    .{ .id = "starfield_exit", .text = "Exit" },
                },
            },
            .statusBar = .{
                .parts = .{
                    .{ .text = mode_text, .width = 90 },
                    .{ .text = draw_mode_text, .width = 120 },
                    .{ .text = heading_text, .width = 120 },
                    .{ .text = speed_text, .width = 90 },
                    .{ .text = detail_text, .width = 90 },
                    .{ .text = controls_text, .width = 260 },
                },
            },
            .body = .{
                .type = "stack",
                .id = "zig_graphics_demo_body",
                .gap = 10,
                .children = .{
                    .{ .type = "heading", .id = "zig_graphics_demo_heading", .text = "Zig Graphics Demo" },
                    .{ .type = "text", .id = "zig_graphics_demo_intro", .text = "This sample combines declarative chrome with a frame-driven RGBA pane. It includes 2D and 3D starfields, plasma, boids, and a Lissajous vector trace." },
                    .{ .type = "rgba-pane", .id = "star_canvas", .width = 960, .height = 560, .focused = true },
                },
            },
        });
    }

    fn advance(self: *DemoState, delta_ms: u64) void {
        switch (self.draw_mode) {
            .starfield_2d => self.advance2d(delta_ms),
            .starfield_3d => self.advance3d(delta_ms),
            .plasma => self.advancePlasma(delta_ms),
            .boids => self.advanceBoids(delta_ms),
            .lissajous => self.advanceLissajous(delta_ms),
        }
    }

    fn advance2d(self: *DemoState, delta_ms: u64) void {
        var dir_x: f32 = @floatFromInt(self.dir_x);
        var dir_y: f32 = @floatFromInt(self.dir_y);
        if (dir_x != 0 and dir_y != 0) {
            dir_x *= diagonal_scale;
            dir_y *= diagonal_scale;
        }

        const dt_ms = if (delta_ms == 0) 16 else delta_ms;
        const travel = self.speed * @as(f32, @floatFromInt(dt_ms)) / 1000.0;
        const margin = 12.0;

        for (&self.stars) |*star| {
            const scale = 0.35 + star.depth * 1.9;
            star.x -= dir_x * travel * scale;
            star.y -= dir_y * travel * scale;

            if (star.x < -margin) {
                star.x = self.canvas_w + margin;
                star.y = self.randomRange(self.canvas_h);
                star.depth = self.randomDepth();
            } else if (star.x > self.canvas_w + margin) {
                star.x = -margin;
                star.y = self.randomRange(self.canvas_h);
                star.depth = self.randomDepth();
            }

            if (star.y < -margin) {
                star.y = self.canvas_h + margin;
                star.x = self.randomRange(self.canvas_w);
                star.depth = self.randomDepth();
            } else if (star.y > self.canvas_h + margin) {
                star.y = -margin;
                star.x = self.randomRange(self.canvas_w);
                star.depth = self.randomDepth();
            }
        }
    }

    fn advance3d(self: *DemoState, delta_ms: u64) void {
        var target_x: f32 = @floatFromInt(self.dir_x);
        var target_y: f32 = @floatFromInt(self.dir_y);
        if (target_x != 0 and target_y != 0) {
            target_x *= diagonal_scale;
            target_y *= diagonal_scale;
        }

        const dt_ms = if (delta_ms == 0) 16 else delta_ms;
        const dt = @as(f32, @floatFromInt(dt_ms)) / 1000.0;
        const smoothing = std.math.pow(f32, 0.84, dt * 60.0);
        self.steer_x = clampf(self.steer_x * smoothing + target_x * 0.22, -1.5, 1.5);
        self.steer_y = clampf(self.steer_y * smoothing + target_y * 0.22, -1.2, 1.2);
        const speed_scale = self.speed / 180.0;

        for (&self.stars) |*star| {
            star.prev_x = star.x;
            star.prev_y = star.y;
            star.prev_depth = star.depth;
            const drift = ((520.0 - star.depth) / 260.0) * 2.1;
            star.x -= self.steer_x * drift;
            star.y -= self.steer_y * drift;
            star.depth -= bodySpeed(star.kind, self.warp_active) * speed_scale;

            const projected = projectStar3d(self.canvas_w, self.canvas_h, star.x, star.y, @max(star.depth, 1.0));
            if (star.depth < 8.0 or
                projected.x < -40.0 or projected.x > self.canvas_w + 40.0 or
                projected.y < -40.0 or projected.y > self.canvas_h + 40.0)
            {
                self.respawnStar3d(star);
            }
        }
    }

    fn advancePlasma(self: *DemoState, delta_ms: u64) void {
        const dt_ms = if (delta_ms == 0) 16 else delta_ms;
        const dt = @as(f32, @floatFromInt(dt_ms)) / 1000.0;
        const speed_scale = self.speed / 180.0;
        self.plasma_phase += dt * (1.0 + speed_scale * 1.3);
        self.plasma_shift_x += @as(f32, @floatFromInt(self.dir_x)) * dt * 0.9;
        self.plasma_shift_y += @as(f32, @floatFromInt(self.dir_y)) * dt * 0.9;
    }

    fn advanceBoids(self: *DemoState, delta_ms: u64) void {
        const dt_ms = if (delta_ms == 0) 16 else delta_ms;
        const dt = @as(f32, @floatFromInt(dt_ms)) / 1000.0;
        const speed_scale = self.speed / 180.0;
        const target_x = self.canvas_w * 0.5 + @as(f32, @floatFromInt(self.dir_x)) * self.canvas_w * 0.18;
        const target_y = self.canvas_h * 0.5 + @as(f32, @floatFromInt(self.dir_y)) * self.canvas_h * 0.18;
        var next_velocity: [boid_count][2]f32 = undefined;

        self.boid_phase += dt * 0.9;

        for (self.boids, 0..) |boid, index| {
            var neighbor_count: f32 = 0;
            var average_x: f32 = 0;
            var average_y: f32 = 0;
            var average_vx: f32 = 0;
            var average_vy: f32 = 0;
            var separation_x: f32 = 0;
            var separation_y: f32 = 0;

            for (self.boids, 0..) |other, other_index| {
                if (index == other_index) continue;
                const dx = other.x - boid.x;
                const dy = other.y - boid.y;
                const distance_sq = dx * dx + dy * dy;
                if (distance_sq < 6400.0) {
                    neighbor_count += 1;
                    average_x += other.x;
                    average_y += other.y;
                    average_vx += other.vx;
                    average_vy += other.vy;
                    if (distance_sq < 900.0 and distance_sq > 0.001) {
                        separation_x -= dx / distance_sq;
                        separation_y -= dy / distance_sq;
                    }
                }
            }

            var vx = boid.vx;
            var vy = boid.vy;
            if (neighbor_count > 0) {
                const inv = 1.0 / neighbor_count;
                vx += (average_x * inv - boid.x) * 0.18;
                vy += (average_y * inv - boid.y) * 0.18;
                vx += (average_vx * inv - boid.vx) * 0.08;
                vy += (average_vy * inv - boid.vy) * 0.08;
                vx += separation_x * 2200.0;
                vy += separation_y * 2200.0;
            }

            vx += (target_x - boid.x) * 0.08;
            vy += (target_y - boid.y) * 0.08;

            const min_speed = 24.0 + speed_scale * 18.0;
            const max_speed = 70.0 + speed_scale * 54.0;
            const velocity_len = @sqrt(vx * vx + vy * vy);
            if (velocity_len > max_speed) {
                const scale = max_speed / velocity_len;
                vx *= scale;
                vy *= scale;
            } else if (velocity_len < min_speed and velocity_len > 0.0001) {
                const scale = min_speed / velocity_len;
                vx *= scale;
                vy *= scale;
            }

            next_velocity[index] = .{ vx, vy };
        }

        for (&self.boids, 0..) |*boid, index| {
            boid.vx = next_velocity[index][0];
            boid.vy = next_velocity[index][1];
            boid.x += boid.vx * dt;
            boid.y += boid.vy * dt;
            boid.phase += dt * 2.4;

            if (boid.x < -24.0) boid.x = self.canvas_w + 24.0;
            if (boid.x > self.canvas_w + 24.0) boid.x = -24.0;
            if (boid.y < -24.0) boid.y = self.canvas_h + 24.0;
            if (boid.y > self.canvas_h + 24.0) boid.y = -24.0;
        }
    }

    fn advanceLissajous(self: *DemoState, delta_ms: u64) void {
        const dt_ms = if (delta_ms == 0) 16 else delta_ms;
        const dt = @as(f32, @floatFromInt(dt_ms)) / 1000.0;
        const speed_scale = self.speed / 180.0;
        self.lissajous_phase += dt * (0.75 + speed_scale * 0.55);
    }

    fn drawStarfield(self: *DemoState, frame: wingui.FrameView, pane: wingui.PaneRef) !void {
        switch (self.draw_mode) {
            .starfield_2d => try self.drawStarfield2d(frame, pane),
            .starfield_3d => try self.drawStarfield3d(frame, pane),
            .plasma => try self.drawPlasma(frame, pane),
            .boids => try self.drawBoids(frame, pane),
            .lissajous => try self.drawLissajous(frame, pane),
        }
    }

    fn drawStarfield2d(self: *DemoState, frame: wingui.FrameView, pane: wingui.PaneRef) !void {
        const clear = [4]f32{ 0.015, 0.02, 0.045, 1.0 };
        const accent = [4]f32{ 0.15, 0.22, 0.45, 1.0 };
        const mode = wingui.RgbaContentBufferMode.frame;

        var primitive_count: usize = 0;
        self.primitives[primitive_count] = makeFilledRect(
            0,
            0,
            self.canvas_w,
            self.canvas_h,
            clear[0],
            clear[1],
            clear[2],
            clear[3],
        );
        primitive_count += 1;

        self.primitives[primitive_count] = makeFilledCircle(
            self.canvas_w * 0.5,
            self.canvas_h * 0.55,
            self.canvas_h * 0.34,
            accent[0],
            accent[1],
            accent[2],
            0.08,
        );
        primitive_count += 1;

        for (self.stars) |star| {
            const radius = 0.8 + star.depth * 2.2;
            const brightness = 0.35 + star.depth * 0.65;

            self.primitives[primitive_count] = makeFilledCircle(
                star.x,
                star.y,
                radius,
                brightness,
                brightness,
                1.0,
                0.95,
            );
            primitive_count += 1;
        }

        try frame.vectorDraw(
            pane,
            mode,
            wingui.RgbaBlendMode.replace,
            1,
            &clear,
            self.primitives[0..primitive_count],
        );

        var overlay_buffer: [96]u8 = undefined;
        const overlay_text = try std.fmt.bufPrint(&overlay_buffer, "2D  Heading {s}  Speed {}", .{ self.headingLabel(), @as(u32, @intFromFloat(self.speed)) });
        try frame.drawText(
            pane,
            mode,
            wingui.RgbaBlendMode.alpha_over,
            0,
            &clear,
            overlay_text,
            12,
            12,
            0.85,
            0.9,
            1.0,
            0.95,
        );
    }

    fn drawStarfield3d(self: *DemoState, frame: wingui.FrameView, pane: wingui.PaneRef) !void {
        const clear = [4]f32{ 0.004, 0.006, 0.02, 1.0 };
        const mode = wingui.RgbaContentBufferMode.frame;
        const frame_f: f32 = @floatFromInt(frame.index());
        const center_x = self.canvas_w * 0.5;
        const warp_glow: f32 = if (self.warp_active) 1.0 else 0.0;
        const nebula_x = self.canvas_w * 0.19 + @sin(frame_f * 0.013) * self.canvas_w * 0.14;
        const nebula_y = self.canvas_h * 0.25 + @cos(frame_f * 0.017) * self.canvas_h * 0.07;
        const glow_x = self.canvas_w * 0.81 + @cos(frame_f * 0.020) * self.canvas_w * 0.08;
        const glow_y = self.canvas_h * 0.33 + @sin(frame_f * 0.014) * self.canvas_h * 0.08;
        const cockpit_left = self.canvas_w * 0.07;
        const cockpit_right = self.canvas_w - cockpit_left;
        const cockpit_top = self.canvas_h * 0.10;
        const cockpit_bottom = self.canvas_h - self.canvas_h * 0.16;

        var primitive_count: usize = 0;
        self.primitives[primitive_count] = makeFilledRect(
            0,
            0,
            self.canvas_w,
            self.canvas_h,
            clear[0],
            clear[1],
            clear[2],
            clear[3],
        );
        primitive_count += 1;

        self.primitives[primitive_count] = makeFilledCircle(
            nebula_x,
            nebula_y,
            self.canvas_h * 0.08,
            0.18,
            0.10,
            0.28,
            0.16,
        );
        primitive_count += 1;

        self.primitives[primitive_count] = makeFilledCircle(
            nebula_x,
            nebula_y,
            self.canvas_h * 0.05,
            0.28,
            0.18,
            0.42,
            0.14,
        );
        primitive_count += 1;

        self.primitives[primitive_count] = makeFilledCircle(
            glow_x,
            glow_y,
            self.canvas_h * 0.06,
            0.14,
            0.19,
            0.36,
            0.16 + warp_glow * 0.06,
        );
        primitive_count += 1;

        self.primitives[primitive_count] = makeFilledCircle(
            glow_x,
            glow_y,
            self.canvas_h * 0.03,
            0.98,
            0.94,
            0.82,
            0.18 + warp_glow * 0.08,
        );
        primitive_count += 1;

        self.primitives[primitive_count] = makeLine(cockpit_left, cockpit_top, cockpit_left + self.canvas_w * 0.05, cockpit_top + self.canvas_h * 0.05, 1.2, 0.50, 0.66, 1.0, 0.60);
        primitive_count += 1;
        self.primitives[primitive_count] = makeLine(cockpit_right, cockpit_top, cockpit_right - self.canvas_w * 0.05, cockpit_top + self.canvas_h * 0.05, 1.2, 0.50, 0.66, 1.0, 0.60);
        primitive_count += 1;
        self.primitives[primitive_count] = makeLine(cockpit_left, cockpit_bottom, cockpit_left + self.canvas_w * 0.05, cockpit_bottom - self.canvas_h * 0.05, 1.2, 0.50, 0.66, 1.0, 0.60);
        primitive_count += 1;
        self.primitives[primitive_count] = makeLine(cockpit_right, cockpit_bottom, cockpit_right - self.canvas_w * 0.05, cockpit_bottom - self.canvas_h * 0.05, 1.2, 0.50, 0.66, 1.0, 0.60);
        primitive_count += 1;
        self.primitives[primitive_count] = makeLine(center_x, cockpit_bottom, center_x - self.canvas_w * 0.03, self.canvas_h - self.canvas_h * 0.035, 1.1, 0.32, 0.56, 0.95, 0.75);
        primitive_count += 1;
        self.primitives[primitive_count] = makeLine(center_x, cockpit_bottom, center_x + self.canvas_w * 0.03, self.canvas_h - self.canvas_h * 0.035, 1.1, 0.32, 0.56, 0.95, 0.75);
        primitive_count += 1;
        self.primitives[primitive_count] = makeLine(center_x - self.canvas_w * 0.04, self.canvas_h - self.canvas_h * 0.05, center_x + self.canvas_w * 0.04, self.canvas_h - self.canvas_h * 0.05, 1.1, 0.32, 0.56, 0.95, 0.75);
        primitive_count += 1;

        for (self.stars) |star| {
            const current = projectStar3d(self.canvas_w, self.canvas_h, star.x, star.y, star.depth);
            if (current.x >= 20.0 and current.x < self.canvas_w - 20.0 and current.y >= 24.0 and current.y < self.canvas_h - 24.0) {
                const colour = bodyColour(star.kind, star.depth, star.phase, frame.index());
                const body_rgb = paletteRgb(colour);

                if (star.kind == 1) {
                    self.primitives[primitive_count] = makeLine(current.x - 3.0, current.y, current.x + 3.0, current.y, 0.8, body_rgb[0], body_rgb[1], body_rgb[2], 0.98);
                    primitive_count += 1;
                    self.primitives[primitive_count] = makeLine(current.x, current.y - 3.0, current.x, current.y + 3.0, 0.8, body_rgb[0], body_rgb[1], body_rgb[2], 0.98);
                    primitive_count += 1;
                    self.primitives[primitive_count] = makeFilledCircle(current.x, current.y, 1.4 + warp_glow * 0.4, 1.0, 0.92, 0.68, 0.92);
                    primitive_count += 1;
                } else if (star.depth < 42.0) {
                    self.primitives[primitive_count] = makeLine(current.x - 2.0, current.y, current.x + 2.0, current.y, 0.75, body_rgb[0], body_rgb[1], body_rgb[2], 0.95);
                    primitive_count += 1;
                    self.primitives[primitive_count] = makeLine(current.x, current.y - 2.0, current.x, current.y + 2.0, 0.75, body_rgb[0], body_rgb[1], body_rgb[2], 0.95);
                    primitive_count += 1;
                    self.primitives[primitive_count] = makeFilledCircle(current.x, current.y, 1.1, 0.95, 0.95, 1.0, 0.55);
                    primitive_count += 1;
                } else if (star.depth < 90.0) {
                    self.primitives[primitive_count] = makeFilledCircle(current.x, current.y, 1.0, body_rgb[0], body_rgb[1], body_rgb[2], 0.92);
                    primitive_count += 1;
                    self.primitives[primitive_count] = makeFilledCircle(current.x + 1.5, current.y, 0.8, 0.70, 0.78, 1.0, 0.42);
                    primitive_count += 1;
                } else {
                    self.primitives[primitive_count] = makeFilledCircle(current.x, current.y, 0.85, body_rgb[0], body_rgb[1], body_rgb[2], 0.86);
                    primitive_count += 1;
                }
            }
        }

        try frame.vectorDraw(
            pane,
            mode,
            wingui.RgbaBlendMode.replace,
            1,
            &clear,
            self.primitives[0..primitive_count],
        );

        var overlay_buffer: [96]u8 = undefined;
        const overlay_text = try std.fmt.bufPrint(&overlay_buffer, "3D rush  yaw {d:.1}  pitch {d:.1}  warp {s}", .{ self.steer_x * 10.0, self.steer_y * 10.0, if (self.warp_active) "ON" else "off" });
        try frame.drawText(
            pane,
            mode,
            wingui.RgbaBlendMode.alpha_over,
            0,
            &clear,
            overlay_text,
            12,
            12,
            0.85,
            0.9,
            1.0,
            0.95,
        );
    }

    fn drawPlasma(self: *DemoState, frame: wingui.FrameView, pane: wingui.PaneRef) !void {
        const clear = [4]f32{ 0.018, 0.010, 0.034, 1.0 };
        const mode = wingui.RgbaContentBufferMode.frame;
        const cell_w = self.canvas_w / @as(f32, @floatFromInt(plasma_columns));
        const cell_h = self.canvas_h / @as(f32, @floatFromInt(plasma_rows));
        const shift_x = self.plasma_shift_x;
        const shift_y = self.plasma_shift_y;

        var primitive_count: usize = 0;
        self.primitives[primitive_count] = makeFilledRect(0, 0, self.canvas_w, self.canvas_h, clear[0], clear[1], clear[2], clear[3]);
        primitive_count += 1;

        for (0..plasma_rows) |row| {
            for (0..plasma_columns) |column| {
                const x = @as(f32, @floatFromInt(column)) / @as(f32, @floatFromInt(plasma_columns));
                const y = @as(f32, @floatFromInt(row)) / @as(f32, @floatFromInt(plasma_rows));
                const wave = @sin((x + shift_x) * 10.0 + self.plasma_phase) +
                    @sin((y + shift_y) * 11.0 - self.plasma_phase * 0.9) +
                    @sin((x + y) * 8.0 + self.plasma_phase * 0.6) +
                    @sin(@sqrt((x - 0.5) * (x - 0.5) + (y - 0.5) * (y - 0.5)) * 16.0 - self.plasma_phase * 1.3);
                const normal = wave * 0.125 + 0.5;
                const r = clampf(0.12 + normal * 1.05, 0, 1);
                const g = clampf(0.04 + @sin(normal * 3.1416) * 0.78, 0, 1);
                const b = clampf(0.22 + (1.0 - normal) * 0.92, 0, 1);
                const x0 = @as(f32, @floatFromInt(column)) * cell_w;
                const y0 = @as(f32, @floatFromInt(row)) * cell_h;
                self.primitives[primitive_count] = makeFilledRect(x0, y0, x0 + cell_w + 1.0, y0 + cell_h + 1.0, r, g, b, 0.98);
                primitive_count += 1;
            }
        }

        try frame.vectorDraw(
            pane,
            mode,
            wingui.RgbaBlendMode.replace,
            1,
            &clear,
            self.primitives[0..primitive_count],
        );

        var overlay_buffer: [112]u8 = undefined;
        const overlay_text = try std.fmt.bufPrint(&overlay_buffer, "Plasma  drift {s}  speed {}", .{ self.headingLabel(), @as(u32, @intFromFloat(self.speed)) });
        try frame.drawText(
            pane,
            mode,
            wingui.RgbaBlendMode.alpha_over,
            0,
            &clear,
            overlay_text,
            12,
            12,
            0.96,
            0.96,
            1.0,
            0.94,
        );
    }

    fn drawBoids(self: *DemoState, frame: wingui.FrameView, pane: wingui.PaneRef) !void {
        const clear = [4]f32{ 0.010, 0.018, 0.026, 1.0 };
        const mode = wingui.RgbaContentBufferMode.frame;
        const target_x = self.canvas_w * 0.5 + @as(f32, @floatFromInt(self.dir_x)) * self.canvas_w * 0.18;
        const target_y = self.canvas_h * 0.5 + @as(f32, @floatFromInt(self.dir_y)) * self.canvas_h * 0.18;

        var primitive_count: usize = 0;
        self.primitives[primitive_count] = makeFilledRect(0, 0, self.canvas_w, self.canvas_h, clear[0], clear[1], clear[2], clear[3]);
        primitive_count += 1;
        self.primitives[primitive_count] = makeFilledCircle(target_x, target_y, 20.0, 0.18, 0.66, 0.90, 0.16);
        primitive_count += 1;
        self.primitives[primitive_count] = makeFilledCircle(target_x, target_y, 8.0, 0.90, 0.98, 1.0, 0.18);
        primitive_count += 1;

        for (self.boids) |boid| {
            const velocity_len = @sqrt(boid.vx * boid.vx + boid.vy * boid.vy);
            const forward_x = if (velocity_len > 0.001) boid.vx / velocity_len else 1.0;
            const forward_y = if (velocity_len > 0.001) boid.vy / velocity_len else 0.0;
            const side_x = -forward_y;
            const side_y = forward_x;
            const tail_x = boid.x - forward_x * 12.0;
            const tail_y = boid.y - forward_y * 12.0;
            const wing_span = 3.2 + 0.8 * @sin(boid.phase);
            const r = 0.35 + 0.15 * @sin(boid.phase + 1.2);
            const g = 0.76 + 0.20 * @sin(boid.phase + 2.1);
            const b = 0.66 + 0.24 * @sin(boid.phase + 4.2);

            self.primitives[primitive_count] = makeLine(tail_x, tail_y, boid.x, boid.y, 0.8, r, g, b, 0.76);
            primitive_count += 1;
            self.primitives[primitive_count] = makeLine(boid.x - side_x * wing_span, boid.y - side_y * wing_span, boid.x + side_x * wing_span, boid.y + side_y * wing_span, 0.6, r * 0.8, g, b, 0.70);
            primitive_count += 1;
            self.primitives[primitive_count] = makeFilledCircle(boid.x, boid.y, 2.2, r + 0.24, g, b, 0.92);
            primitive_count += 1;
        }

        try frame.vectorDraw(
            pane,
            mode,
            wingui.RgbaBlendMode.replace,
            1,
            &clear,
            self.primitives[0..primitive_count],
        );

        var overlay_buffer: [112]u8 = undefined;
        const overlay_text = try std.fmt.bufPrint(&overlay_buffer, "Boids  flock {}  attract {s}", .{ boid_count, self.headingLabel() });
        try frame.drawText(
            pane,
            mode,
            wingui.RgbaBlendMode.alpha_over,
            0,
            &clear,
            overlay_text,
            12,
            12,
            0.88,
            0.96,
            0.98,
            0.95,
        );
    }

    fn drawLissajous(self: *DemoState, frame: wingui.FrameView, pane: wingui.PaneRef) !void {
        const clear = [4]f32{ 0.018, 0.012, 0.022, 1.0 };
        const mode = wingui.RgbaContentBufferMode.frame;
        const center_x = self.canvas_w * 0.5;
        const center_y = self.canvas_h * 0.5;
        const amp_x = self.canvas_w * 0.33;
        const amp_y = self.canvas_h * 0.28;
        const freq_x = 3.0 + @as(f32, @floatFromInt(self.dir_x + 1));
        const freq_y = 2.0 + @as(f32, @floatFromInt(self.dir_y + 2));
        const segment_count: usize = 220;
        const phase = self.lissajous_phase;

        var primitive_count: usize = 0;
        self.primitives[primitive_count] = makeFilledRect(0, 0, self.canvas_w, self.canvas_h, clear[0], clear[1], clear[2], clear[3]);
        primitive_count += 1;
        self.primitives[primitive_count] = makeLine(center_x - amp_x, center_y, center_x + amp_x, center_y, 0.4, 0.28, 0.20, 0.42, 0.24);
        primitive_count += 1;
        self.primitives[primitive_count] = makeLine(center_x, center_y - amp_y, center_x, center_y + amp_y, 0.4, 0.28, 0.20, 0.42, 0.24);
        primitive_count += 1;

        var prev_x = center_x + @sin(phase) * amp_x;
        var prev_y = center_y + @sin(phase * 0.5) * amp_y;
        for (1..(segment_count + 1)) |segment| {
            const t = @as(f32, @floatFromInt(segment)) / @as(f32, @floatFromInt(segment_count)) * std.math.tau;
            const x = center_x + @sin(t * freq_x + phase) * amp_x;
            const y = center_y + @sin(t * freq_y + phase * 1.37 + 0.6) * amp_y;
            const glow = @as(f32, @floatFromInt(segment)) / @as(f32, @floatFromInt(segment_count));
            self.primitives[primitive_count] = makeLine(prev_x, prev_y, x, y, 0.85, 0.46 + glow * 0.34, 0.24 + glow * 0.50, 0.90 + glow * 0.10, 0.84);
            primitive_count += 1;
            prev_x = x;
            prev_y = y;
        }

        self.primitives[primitive_count] = makeFilledCircle(prev_x, prev_y, 4.0, 1.0, 0.92, 0.60, 0.88);
        primitive_count += 1;

        try frame.vectorDraw(
            pane,
            mode,
            wingui.RgbaBlendMode.replace,
            1,
            &clear,
            self.primitives[0..primitive_count],
        );

        var overlay_buffer: [112]u8 = undefined;
        const overlay_text = try std.fmt.bufPrint(&overlay_buffer, "Lissajous  ratio {d:.0}:{d:.0}  phase {d:.1}", .{ freq_x, freq_y, phase });
        try frame.drawText(
            pane,
            mode,
            wingui.RgbaBlendMode.alpha_over,
            0,
            &clear,
            overlay_text,
            12,
            12,
            0.96,
            0.90,
            0.94,
            0.95,
        );
    }
};

fn headingLabelFor(dir_x: i8, dir_y: i8) []const u8 {
    return switch (dir_y) {
        -1 => switch (dir_x) {
            -1 => "Northwest",
            0 => "North",
            1 => "Northeast",
            else => "North",
        },
        0 => switch (dir_x) {
            -1 => "West",
            0 => "Still",
            1 => "East",
            else => "Still",
        },
        1 => switch (dir_x) {
            -1 => "Southwest",
            0 => "South",
            1 => "Southeast",
            else => "South",
        },
        else => "North",
    };
}

fn applyDirectionalInput(state: *DemoState, frame: wingui.FrameView) bool {
    const left_down = frame.keyDown(wingui.Key.left) catch false;
    const right_down = frame.keyDown(wingui.Key.right) catch false;
    const up_down = frame.keyDown(wingui.Key.up) catch false;
    const down_down = frame.keyDown(wingui.Key.down) catch false;

    if (!left_down and !right_down and !up_down and !down_down) {
        return false;
    }

    const dir_x: i8 = (if (left_down) @as(i8, -1) else @as(i8, 0)) + (if (right_down) @as(i8, 1) else @as(i8, 0));
    const dir_y: i8 = (if (up_down) @as(i8, -1) else @as(i8, 0)) + (if (down_down) @as(i8, 1) else @as(i8, 0));
    return state.setDirection(dir_x, dir_y);
}

fn onFaster(state: *DemoState, _: *wingui.Runtime, _: wingui.EventView) void {
    state.increaseSpeed();
    state.republish() catch {};
}

fn onSlower(state: *DemoState, _: *wingui.Runtime, _: wingui.EventView) void {
    state.decreaseSpeed();
    state.republish() catch {};
}

fn onResetField(state: *DemoState, _: *wingui.Runtime, _: wingui.EventView) void {
    state.resetScene();
    state.republish() catch {};
}

fn onTogglePause(state: *DemoState, _: *wingui.Runtime, _: wingui.EventView) void {
    state.togglePause();
    state.republish() catch {};
}

fn onDraw2d(state: *DemoState, _: *wingui.Runtime, _: wingui.EventView) void {
    if (state.setDrawMode(.starfield_2d)) {
        state.republish() catch {};
    }
}

fn onDraw3d(state: *DemoState, _: *wingui.Runtime, _: wingui.EventView) void {
    if (state.setDrawMode(.starfield_3d)) {
        state.republish() catch {};
    }
}

fn onDrawPlasma(state: *DemoState, _: *wingui.Runtime, _: wingui.EventView) void {
    if (state.setDrawMode(.plasma)) {
        state.republish() catch {};
    }
}

fn onDrawBoids(state: *DemoState, _: *wingui.Runtime, _: wingui.EventView) void {
    if (state.setDrawMode(.boids)) {
        state.republish() catch {};
    }
}

fn onDrawLissajous(state: *DemoState, _: *wingui.Runtime, _: wingui.EventView) void {
    if (state.setDrawMode(.lissajous)) {
        state.republish() catch {};
    }
}

fn onExit(_: *DemoState, runtime: *wingui.Runtime, _: wingui.EventView) void {
    runtime.requestStop(0) catch {};
}

fn onFrame(state: *DemoState, runtime: *wingui.Runtime, frame: wingui.FrameView) void {
    const pane = frame.resolvePane("star_canvas") catch return;
    const layout = frame.paneLayout(pane) catch return;
    state.canvas_w = @floatFromInt(layout.width);
    state.canvas_h = @floatFromInt(layout.height);

    const escape_down = frame.keyDown(wingui.Key.escape) catch false;
    if (escape_down and !state.prev_escape_down) {
        runtime.requestStop(0) catch {};
    }
    state.prev_escape_down = escape_down;

    const space_down = frame.keyDown(wingui.Key.space) catch false;
    if (state.draw_mode == .starfield_3d) {
        if (state.warp_active != space_down) {
            state.warp_active = space_down;
            state.republish() catch {};
        }
    } else if (space_down and !state.prev_space_down) {
        state.togglePause();
        state.republish() catch {};
    }
    state.prev_space_down = space_down;

    if (applyDirectionalInput(state, frame)) {
        state.republish() catch {};
    }

    if (!state.paused) {
        state.advance(frame.deltaMs());
    }

    state.drawStarfield(frame, pane) catch {};
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

    var state = DemoState.init();
    state.runtime = &runtime;

    var desc = wingui.defaultRunDesc(demo_title_z);
    desc.columns = 118;
    desc.rows = 42;
    desc.auto_request_present = 1;

    runtime.bindEvent("starfield_faster", &state, onFaster) catch {
        return failWithMessage(demo_title_z);
    };
    runtime.bindEvent("starfield_slower", &state, onSlower) catch {
        return failWithMessage(demo_title_z);
    };
    runtime.bindEvent("starfield_reset", &state, onResetField) catch {
        return failWithMessage(demo_title_z);
    };
    runtime.bindEvent("starfield_toggle_pause", &state, onTogglePause) catch {
        return failWithMessage(demo_title_z);
    };
    runtime.bindEvent("starfield_draw_2d", &state, onDraw2d) catch {
        return failWithMessage(demo_title_z);
    };
    runtime.bindEvent("starfield_draw_3d", &state, onDraw3d) catch {
        return failWithMessage(demo_title_z);
    };
    runtime.bindEvent("starfield_draw_plasma", &state, onDrawPlasma) catch {
        return failWithMessage(demo_title_z);
    };
    runtime.bindEvent("starfield_draw_boids", &state, onDrawBoids) catch {
        return failWithMessage(demo_title_z);
    };
    runtime.bindEvent("starfield_draw_lissajous", &state, onDrawLissajous) catch {
        return failWithMessage(demo_title_z);
    };
    runtime.bindEvent("starfield_exit", &state, onExit) catch {
        return failWithMessage(demo_title_z);
    };
    runtime.setFrameHandler(&state, onFrame);

    state.republish() catch {
        return failWithMessage(demo_title_z);
    };

    const result = runtime.run(&desc) catch {
        return failWithMessage(demo_title_z);
    };
    return @intCast(result.exit_code);
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

const ProjectedStar = struct {
    x: f32,
    y: f32,
};

fn projectStar3d(width: f32, height: f32, x: f32, y: f32, depth: f32) ProjectedStar {
    const focal = @min(width, height) * 0.62;
    const center_x = width * 0.5;
    const center_y = height * 0.5;
    return .{
        .x = center_x + x * (focal / depth),
        .y = center_y + y * (focal / depth),
    };
}

fn bodySpeed(kind: u8, warp_active: bool) f32 {
    const base: f32 = if (kind == 1) 8.5 else 3.2;
    const warp_scale: f32 = if (warp_active) 1.8 else 1.0;
    return base * warp_scale;
}

fn bodyColour(kind: u8, depth: f32, phase: u32, frame_index: u64) u8 {
    if (kind == 1) {
        return 24;
    }
    const blink: u32 = @intCast((frame_index + phase) % 24);
    if (depth < 40.0) {
        return if (blink < 8) 31 else 30;
    }
    if (depth < 90.0) {
        return if (blink < 4) 29 else 28;
    }
    return if (blink < 2) 20 else 19;
}

fn paletteRgb(index: u8) [3]f32 {
    return switch (index) {
        19 => .{ 120.0 / 255.0, 145.0 / 255.0, 180.0 / 255.0 },
        20 => .{ 180.0 / 255.0, 220.0 / 255.0, 255.0 / 255.0 },
        21 => .{ 120.0 / 255.0, 180.0 / 255.0, 1.0 },
        24 => .{ 1.0, 215.0 / 255.0, 120.0 / 255.0 },
        26 => .{ 90.0 / 255.0, 140.0 / 255.0, 220.0 / 255.0 },
        28 => .{ 170.0 / 255.0, 180.0 / 255.0, 220.0 / 255.0 },
        29 => .{ 220.0 / 255.0, 225.0 / 255.0, 1.0 },
        30 => .{ 1.0, 235.0 / 255.0, 210.0 / 255.0 },
        31 => .{ 1.0, 1.0, 1.0 },
        else => .{ 0.75, 0.82, 1.0 },
    };
}

fn clampf(value: f32, min_value: f32, max_value: f32) f32 {
    return @max(min_value, @min(max_value, value));
}
