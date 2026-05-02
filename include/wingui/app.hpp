// wingui/app.hpp — C++20 application-level wrapper for the SuperTerminal API.
//
// Drawing usage:
//   wg::App{}.title("My App").columns(80).rows(24)
//       .on_frame([&](wg::Frame& f) {
//           auto p = f.pane(my_pane_id);
//           p.fill_rect(0, 0, 100, 50, 3);
//           wg::PrimitiveList prims;
//           prims.add_rect_filled(10, 10, 90, 40, wg::Color::hex(0xFF4444))
//                .add_text(f.glyph_atlas_info(), "Hello", 12, 14, wg::Color::white());
//           p.vector_draw(prims);
//       })
//       .on_event([&](const wg::Event& e) { if (e.is_close_requested()) ... })
//       .run();
//
// Declarative layout usage:
//   wg::Layout layout;
//   wg::App{}.title("My App").layout(layout)
//       .on_setup([&](SuperTerminalClientContext* ctx) {
//           return layout.render([&] {
//               return wg::ui_window("My App",
//                   wg::ui_split_view("horizontal", "main-split",
//                       wg::ui_rgba_pane("canvas", 640, 480, "canvas-input"),
//                       wg::ui_stack({
//                           wg::ui_button("Go", "btn-go"),
//                           wg::ui_slider("Speed", layout.state().get("speed", 0.5), "speed"),
//                       })));
//           });
//       })
//       .on_event([&](const wg::Event& e) {
//           if (e.is_native_ui()) {
//               auto ev = wg::ui_parse_event(e.native_ui().payload_json_utf8);
//               if (wg::ui_event_name(ev) == "speed")
//                   layout.state().set("speed", ev["value"]);
//               layout.rerender();
//           }
//       })
//       .run();

#pragma once

#include "wingui/terminal.h"
#include "wingui/ui_model.h"

#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <cmath>
#include <cstdint>

namespace wg {

// ---------------------------------------------------------------------------
// Color
// ---------------------------------------------------------------------------

struct Color {
    float r = 0.f, g = 0.f, b = 0.f, a = 1.f;

    Color() = default;
    constexpr Color(float r, float g, float b, float a = 1.f) : r(r), g(g), b(b), a(a) {}

    // 0xRRGGBB, optional alpha 0-1
    static constexpr Color hex(uint32_t rgb, float a = 1.f) {
        return { ((rgb >> 16) & 0xFFu) / 255.f,
                 ((rgb >>  8) & 0xFFu) / 255.f,
                 ( rgb        & 0xFFu) / 255.f, a };
    }
    static constexpr Color rgba8(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
        return { r / 255.f, g / 255.f, b / 255.f, a / 255.f };
    }
    static constexpr Color grey(float v, float a = 1.f) { return { v, v, v, a }; }

    Color with_alpha(float new_a) const { return { r, g, b, new_a }; }

    WinguiGraphicsColour to_grid() const {
        WinguiGraphicsColour c;
        c.r = static_cast<uint8_t>(r * 255.f);
        c.g = static_cast<uint8_t>(g * 255.f);
        c.b = static_cast<uint8_t>(b * 255.f);
        c.a = static_cast<uint8_t>(a * 255.f);
        return c;
    }

    static constexpr Color black()       { return {0.f, 0.f, 0.f, 1.f}; }
    static constexpr Color white()       { return {1.f, 1.f, 1.f, 1.f}; }
    static constexpr Color transparent() { return {0.f, 0.f, 0.f, 0.f}; }
};

enum class RgbaContentBufferMode : uint32_t {
    Frame = SUPERTERMINAL_RGBA_CONTENT_BUFFER_FRAME,
    Persistent = SUPERTERMINAL_RGBA_CONTENT_BUFFER_PERSISTENT,
};

// ---------------------------------------------------------------------------
// PrimitiveList — builder for WinguiVectorPrimitive arrays
// ---------------------------------------------------------------------------

class PrimitiveList {
public:
    PrimitiveList() = default;

    // Filled rounded rectangle. radius=0 gives sharp corners.
    PrimitiveList& add_rect_filled(float x0, float y0, float x1, float y1,
                                   Color color, float radius = 0.f) {
        WinguiVectorPrimitive p{};
        p.bounds_min_x = x0; p.bounds_min_y = y0;
        p.bounds_max_x = x1; p.bounds_max_y = y1;
        p.param0[0] = radius;
        p.color[0] = color.r; p.color[1] = color.g;
        p.color[2] = color.b; p.color[3] = color.a;
        p.shape = WINGUI_VECTOR_RECT_FILLED;
        prims_.push_back(p);
        return *this;
    }

    // Stroked rounded rectangle. half_stroke >= 0.5 to stay visible.
    PrimitiveList& add_rect_stroked(float x0, float y0, float x1, float y1,
                                    Color color, float half_stroke = 1.f, float radius = 0.f) {
        WinguiVectorPrimitive p{};
        p.bounds_min_x = x0; p.bounds_min_y = y0;
        p.bounds_max_x = x1; p.bounds_max_y = y1;
        p.param0[0] = radius;
        p.param0[1] = half_stroke;
        p.color[0] = color.r; p.color[1] = color.g;
        p.color[2] = color.b; p.color[3] = color.a;
        p.shape = WINGUI_VECTOR_RECT_STROKED;
        prims_.push_back(p);
        return *this;
    }

    // Anti-aliased line capsule (rounded ends). half_thickness >= 0.5.
    PrimitiveList& add_line(float px0, float py0, float px1, float py1,
                            Color color, float half_thickness = 1.f) {
        const float pad = half_thickness + 1.f;
        WinguiVectorPrimitive p{};
        p.bounds_min_x = std::fmin(px0, px1) - pad;
        p.bounds_min_y = std::fmin(py0, py1) - pad;
        p.bounds_max_x = std::fmax(px0, px1) + pad;
        p.bounds_max_y = std::fmax(py0, py1) + pad;
        p.param0[0] = px0; p.param0[1] = py0;
        p.param0[2] = px1; p.param0[3] = py1;
        p.param1[0] = half_thickness;
        p.color[0] = color.r; p.color[1] = color.g;
        p.color[2] = color.b; p.color[3] = color.a;
        p.shape = WINGUI_VECTOR_LINE;
        prims_.push_back(p);
        return *this;
    }

    // Filled disc.
    PrimitiveList& add_circle_filled(float cx, float cy, float radius, Color color) {
        WinguiVectorPrimitive p{};
        p.bounds_min_x = cx - radius - 1.f; p.bounds_min_y = cy - radius - 1.f;
        p.bounds_max_x = cx + radius + 1.f; p.bounds_max_y = cy + radius + 1.f;
        p.param0[0] = cx; p.param0[1] = cy; p.param0[2] = radius;
        p.color[0] = color.r; p.color[1] = color.g;
        p.color[2] = color.b; p.color[3] = color.a;
        p.shape = WINGUI_VECTOR_CIRCLE_FILLED;
        prims_.push_back(p);
        return *this;
    }

    // Circle outline. half_stroke >= 0.5.
    PrimitiveList& add_circle_stroked(float cx, float cy, float radius,
                                      Color color, float half_stroke = 1.f) {
        const float outer = radius + half_stroke + 1.f;
        WinguiVectorPrimitive p{};
        p.bounds_min_x = cx - outer; p.bounds_min_y = cy - outer;
        p.bounds_max_x = cx + outer; p.bounds_max_y = cy + outer;
        p.param0[0] = cx; p.param0[1] = cy;
        p.param0[2] = radius; p.param0[3] = half_stroke;
        p.color[0] = color.r; p.color[1] = color.g;
        p.color[2] = color.b; p.color[3] = color.a;
        p.shape = WINGUI_VECTOR_CIRCLE_STROKED;
        prims_.push_back(p);
        return *this;
    }

    // Partial arc. rotation_rad rotates the bisector from +Y (clockwise).
    // half_aperture_rad is half the angular span (0 = line, pi = full circle).
    PrimitiveList& add_arc(float cx, float cy, float radius,
                           Color color, float rotation_rad, float half_aperture_rad,
                           float half_stroke = 1.f) {
        const float outer = radius + half_stroke + 1.f;
        WinguiVectorPrimitive p{};
        p.bounds_min_x = cx - outer; p.bounds_min_y = cy - outer;
        p.bounds_max_x = cx + outer; p.bounds_max_y = cy + outer;
        p.param0[0] = cx; p.param0[1] = cy;
        p.param0[2] = radius; p.param0[3] = half_stroke;
        p.param1[0] = rotation_rad;
        p.param1[1] = half_aperture_rad;
        p.color[0] = color.r; p.color[1] = color.g;
        p.color[2] = color.b; p.color[3] = color.a;
        p.shape = WINGUI_VECTOR_ARC;
        prims_.push_back(p);
        return *this;
    }

    // All glyphs from a UTF-8 string starting at (x, y) in surface pixels.
    // atlas_info must come from frame.glyph_atlas_info() or super_terminal_get_glyph_atlas_info().
    PrimitiveList& add_text(const WinguiGlyphAtlasInfo& atlas, const char* text_utf8,
                            float x, float y, Color color) {
        uint32_t count = 0;
        wingui_text_layout_with_atlas_info_utf8(
            &atlas, text_utf8,
            x, y,
            color.r, color.g, color.b, color.a,
            nullptr, &count, 0);  // query count
        if (count == 0) return *this;
        const size_t base = prims_.size();
        prims_.resize(base + count);
        wingui_text_layout_with_atlas_info_utf8(
            &atlas, text_utf8,
            x, y,
            color.r, color.g, color.b, color.a,
            prims_.data() + base, &count, count);
        prims_.resize(base + count);
        return *this;
    }

    // Draw everything into pane using the pane's current buffer.
    // Returns false on error.
    bool draw_to(SuperTerminalClientContext* ctx, SuperTerminalPaneId pane_id,
                 uint32_t buffer_index,
             RgbaContentBufferMode content_buffer_mode = RgbaContentBufferMode::Frame,
                 uint32_t blend_mode = WINGUI_RGBA_BLIT_ALPHA_OVER,
                 bool clear_first = false,
                 Color clear_color = Color::transparent()) const {
        if (prims_.empty() && !clear_first) return true;
        const float cc[4] = { clear_color.r, clear_color.g, clear_color.b, clear_color.a };
        return super_terminal_vector_draw(
            ctx, pane_id, buffer_index, static_cast<uint32_t>(content_buffer_mode), blend_mode,
            clear_first ? 1 : 0, cc,
            prims_.data(), static_cast<uint32_t>(prims_.size())) != 0;
    }

    void clear() { prims_.clear(); }
    bool empty() const { return prims_.empty(); }
    size_t size() const { return prims_.size(); }
    std::span<const WinguiVectorPrimitive> span() const { return prims_; }

private:
    std::vector<WinguiVectorPrimitive> prims_;
};

// ---------------------------------------------------------------------------
// SpriteList — builder for SuperTerminalSpriteInstance arrays
// ---------------------------------------------------------------------------

class SpriteList {
public:
    SpriteList() = default;

    SpriteList& add(SuperTerminalSpriteId sprite_id, float x, float y,
                    float scale_x = 1.f, float scale_y = 1.f,
                    float rotation = 0.f,
                    float anchor_x = 0.f, float anchor_y = 0.f,
                    float alpha = 1.f) {
        SuperTerminalSpriteInstance inst{};
        inst.sprite_id = sprite_id;
        inst.x = x; inst.y = y;
        inst.scale_x = scale_x; inst.scale_y = scale_y;
        inst.rotation = rotation;
        inst.anchor_x = anchor_x; inst.anchor_y = anchor_y;
        inst.alpha = alpha;
        instances_.push_back(inst);
        return *this;
    }

    // Access the last added instance to set flags/effects/palette.
    SuperTerminalSpriteInstance& last() { return instances_.back(); }

    bool render_to(SuperTerminalClientContext* ctx, SuperTerminalPaneId pane_id,
                   uint64_t sprite_tick,
                   uint32_t target_w = 0, uint32_t target_h = 0) const {
        if (instances_.empty()) return true;
        return super_terminal_render_sprites(
            ctx, pane_id, sprite_tick, target_w, target_h,
            instances_.data(), static_cast<uint32_t>(instances_.size())) != 0;
    }

    void clear() { instances_.clear(); }
    bool empty() const { return instances_.empty(); }
    size_t size() const { return instances_.size(); }

private:
    std::vector<SuperTerminalSpriteInstance> instances_;
};

// ---------------------------------------------------------------------------
// Pane — drawing handle for one pane surface, buffer index baked in.
// Obtain via Frame::pane() so the buffer index is automatically current.
// ---------------------------------------------------------------------------

class Pane {
public:
    Pane(SuperTerminalClientContext* ctx, SuperTerminalPaneId id, uint32_t buffer_index)
        : ctx_(ctx), id_(id), buf_(buffer_index) {}

    SuperTerminalPaneId id() const { return id_; }
    uint32_t buffer_index() const { return buf_; }
    bool valid() const { return id_.value != 0 && ctx_ != nullptr; }
    SuperTerminalClientContext* ctx() const { return ctx_; }

    SuperTerminalPaneLayout layout() const {
        SuperTerminalPaneLayout raw{};
        super_terminal_get_pane_layout(ctx_, id_, &raw);
        return raw;
    }

    // ---- Text grid --------------------------------------------------------

    bool write_cells(std::span<const SuperTerminalTextGridCell> cells) const {
        return super_terminal_text_grid_write_cells(
            ctx_, id_, cells.data(), static_cast<uint32_t>(cells.size())) != 0;
    }

    // Single cell convenience.
    bool write_cell(uint32_t row, uint32_t col, uint32_t codepoint,
                    Color fg = Color::white(), Color bg = Color::black()) const {
        SuperTerminalTextGridCell c{ row, col, codepoint, fg.to_grid(), bg.to_grid() };
        return super_terminal_text_grid_write_cells(ctx_, id_, &c, 1) != 0;
    }

    bool clear_region(uint32_t row, uint32_t col,
                      uint32_t width, uint32_t height,
                      uint32_t fill_codepoint = ' ',
                      Color fg = Color::white(), Color bg = Color::black()) const {
        return super_terminal_text_grid_clear_region(
            ctx_, id_, row, col, width, height,
            fill_codepoint, fg.to_grid(), bg.to_grid()) != 0;
    }

    // ---- Indexed compute-shader drawing -----------------------------------

    bool fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                   uint32_t palette_index) const {
        return super_terminal_indexed_fill_rect(
            ctx_, id_, buf_, x, y, w, h, palette_index) != 0;
    }

    bool draw_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                   uint32_t palette_index) const {
        return super_terminal_indexed_draw_line(
            ctx_, id_, buf_, x0, y0, x1, y1, palette_index) != 0;
    }

    // Axis-aligned stroked rectangle (4 lines, no fills).
    bool stroke_rect(int32_t x, int32_t y, int32_t w, int32_t h,
                     uint32_t palette_index) const {
        if (w <= 0 || h <= 0) return true;
        const int32_t x1 = x + w - 1, y1 = y + h - 1;
        bool ok = draw_line(x,  y,  x1, y,  palette_index);
        ok     &= draw_line(x,  y1, x1, y1, palette_index);
        ok     &= draw_line(x,  y,  x,  y1, palette_index);
        ok     &= draw_line(x1, y,  x1, y1, palette_index);
        return ok;
    }

    // ---- RGBA GPU vector drawing ------------------------------------------

    bool vector_draw(std::span<const WinguiVectorPrimitive> prims,
                     RgbaContentBufferMode content_buffer_mode = RgbaContentBufferMode::Frame,
                     uint32_t blend_mode = WINGUI_RGBA_BLIT_ALPHA_OVER,
                     bool clear_first = false,
                     Color clear_color = Color::transparent()) const {
        const float cc[4] = { clear_color.r, clear_color.g, clear_color.b, clear_color.a };
        return super_terminal_vector_draw(
            ctx_, id_, buf_, static_cast<uint32_t>(content_buffer_mode), blend_mode,
            clear_first ? 1 : 0, cc,
            prims.data(), static_cast<uint32_t>(prims.size())) != 0;
    }

    bool vector_draw(const PrimitiveList& prims,
                     RgbaContentBufferMode content_buffer_mode = RgbaContentBufferMode::Frame,
                     uint32_t blend_mode = WINGUI_RGBA_BLIT_ALPHA_OVER,
                     bool clear_first = false,
                     Color clear_color = Color::transparent()) const {
        return prims.draw_to(ctx_, id_, buf_, content_buffer_mode, blend_mode, clear_first, clear_color);
    }

    // ---- Sprites ----------------------------------------------------------

    bool define_sprite(SuperTerminalSpriteId sprite_id,
                       uint32_t frame_w, uint32_t frame_h,
                       uint32_t frame_count, uint32_t frames_per_tick,
                       void* pixels, void* palette,
                       SuperTerminalFreeFn free_fn = nullptr,
                       void* free_user_data = nullptr) const {
        return super_terminal_define_sprite(
            ctx_, id_, sprite_id,
            frame_w, frame_h, frame_count, frames_per_tick,
            pixels, palette, free_fn, free_user_data) != 0;
    }

    bool render_sprites(const SpriteList& sprites, uint64_t sprite_tick,
                        uint32_t target_w = 0, uint32_t target_h = 0) const {
        return sprites.render_to(ctx_, id_, sprite_tick, target_w, target_h);
    }

    bool render_sprites(std::span<const SuperTerminalSpriteInstance> instances,
                        uint64_t sprite_tick,
                        uint32_t target_w = 0, uint32_t target_h = 0) const {
        return super_terminal_render_sprites(
            ctx_, id_, sprite_tick, target_w, target_h,
            instances.data(), static_cast<uint32_t>(instances.size())) != 0;
    }

    // ---- RGBA asset blit --------------------------------------------------

    bool blit_asset(SuperTerminalAssetId asset_id,
                    uint32_t src_x, uint32_t src_y,
                    uint32_t region_w, uint32_t region_h,
                    uint32_t dst_x, uint32_t dst_y) const {
        return super_terminal_asset_blit_to_pane(
            ctx_, asset_id,
            src_x, src_y, region_w, region_h,
            id_, buf_, dst_x, dst_y) != 0;
    }

private:
    SuperTerminalClientContext* ctx_;
    SuperTerminalPaneId id_;
    uint32_t buf_;
};

// ---------------------------------------------------------------------------
// Event — thin wrapper giving typed access to SuperTerminalEvent.
// ---------------------------------------------------------------------------

class Event {
public:
    explicit Event(const SuperTerminalEvent* e) : raw_(e) {}

    SuperTerminalEventType type() const { return raw_->type; }

    bool is_key()            const { return raw_->type == SUPERTERMINAL_EVENT_KEY; }
    bool is_char()           const { return raw_->type == SUPERTERMINAL_EVENT_CHAR; }
    bool is_mouse()          const { return raw_->type == SUPERTERMINAL_EVENT_MOUSE; }
    bool is_pane_input()     const { return raw_->type == SUPERTERMINAL_EVENT_PANE_INPUT; }
    bool is_resize()         const { return raw_->type == SUPERTERMINAL_EVENT_RESIZE; }
    bool is_focus()          const { return raw_->type == SUPERTERMINAL_EVENT_FOCUS; }
    bool is_native_ui()      const { return raw_->type == SUPERTERMINAL_EVENT_NATIVE_UI; }
    bool is_close_requested()const { return raw_->type == SUPERTERMINAL_EVENT_CLOSE_REQUESTED; }
    bool is_host_stopping()  const { return raw_->type == SUPERTERMINAL_EVENT_HOST_STOPPING; }

    const SuperTerminalKeyEvent&         key()          const { return raw_->data.key; }
    const SuperTerminalCharEvent&        character()    const { return raw_->data.character; }
    const SuperTerminalMouseEvent&       mouse()        const { return raw_->data.mouse; }
    const SuperTerminalPaneInputEvent&   pane_input()   const { return raw_->data.pane_input; }
    const SuperTerminalResizeEvent&      resize()       const { return raw_->data.resize; }
    const SuperTerminalFocusEvent&       focus()        const { return raw_->data.focus; }
    const SuperTerminalNativeUiEvent&    native_ui()    const { return raw_->data.native_ui; }
    const SuperTerminalHostStoppingEvent& host_stopping() const { return raw_->data.host_stopping; }

    const SuperTerminalEvent* raw() const { return raw_; }

private:
    const SuperTerminalEvent* raw_;
};

// ---------------------------------------------------------------------------
// Frame — per-frame context. Valid only within the on_frame callback.
// ---------------------------------------------------------------------------

class Frame {
public:
    Frame(SuperTerminalClientContext* ctx, const SuperTerminalFrameTick* tick)
        : ctx_(ctx), tick_(tick) {}

    // Frame metadata
    uint64_t index()               const { return tick_->frame_index; }
    uint64_t elapsed_ms()          const { return tick_->elapsed_ms; }
    uint64_t delta_ms()            const { return tick_->delta_ms; }
    uint32_t buffer_index()        const { return tick_->buffer_index; }
    uint32_t active_buffer_index() const { return tick_->active_buffer_index; }
    uint32_t buffer_count()        const { return tick_->buffer_count; }
    const SuperTerminalFrameTick* raw_tick() const { return tick_; }

    // Get a Pane handle bound to the current draw buffer.
    Pane pane(SuperTerminalPaneId id) const {
        return Pane(ctx_, id, tick_->buffer_index);
    }

    // Look up a pane by its native_ui node id string.
    Pane pane(const char* node_id_utf8) const {
        SuperTerminalPaneId id{};
        super_terminal_resolve_pane_id_utf8(ctx_, node_id_utf8, &id);
        return Pane(ctx_, id, tick_->buffer_index);
    }

    // Glyph atlas info for text layout via PrimitiveList::add_text().
    WinguiGlyphAtlasInfo glyph_atlas_info() const {
        WinguiGlyphAtlasInfo info{};
        super_terminal_get_glyph_atlas_info(ctx_, &info);
        return info;
    }

    bool request_present() const {
        return super_terminal_request_present(ctx_) != 0;
    }

    SuperTerminalClientContext* ctx() const { return ctx_; }

private:
    SuperTerminalClientContext* ctx_;
    const SuperTerminalFrameTick* tick_;
};

// ---------------------------------------------------------------------------
// Layout — reactive declarative UI model.
//
// Wraps wingui::UiModel + wingui::UiState and wires their publish/patch
// delegates to the live SuperTerminal context.  Obtain a layout reference
// via App::layout() and call render() once during setup to publish the
// initial tree.  Call rerender() whenever state changes to diff and patch.
//
// push/pop_event_depth is called automatically by App around every event
// dispatch so rerenders triggered inside an event handler are deferred and
// coalesced to a single patch after the handler returns.
// ---------------------------------------------------------------------------

class Layout {
public:
    Layout() = default;

    // Attach to a live context.  Called automatically by App during setup.
    void attach(SuperTerminalClientContext* ctx) {
        ctx_ = ctx;
        model_.set_publish_fn([ctx](const std::string& json) {
            return super_terminal_publish_ui_json(ctx, json.c_str()) != 0;
        });
        model_.set_patch_fn([ctx](const std::string& json) {
            return super_terminal_patch_ui_json(ctx, json.c_str()) != 0;
        });
    }

    // Register a render function and immediately publish the initial tree.
    // render_fn must return a wingui::UiWindow (use wg::ui_window(...)).
    bool render(std::function<wingui::UiWindow()> fn) {
        return model_.render(std::move(fn));
    }

    // Diff against the last published tree and send a minimal patch.
    // Forces a full republish if the structure changed beyond what the diff
    // engine can reconcile.
    bool rerender() { return model_.rerender(); }

    // Publish a static window immediately, bypassing the diff cache.
    bool show(const wingui::UiWindow& w) { return model_.show(w); }

    // Forget the cached tree; the next rerender() will be a full republish.
    void reset_cache() { model_.reset_cache(); }

    // Direct access to the underlying model and state store.
    wingui::UiModel& model() { return model_; }
    wingui::UiState& state() { return state_; }
    const wingui::UiState& state() const { return state_; }

    bool attached() const { return ctx_ != nullptr; }

    // Called by App; not normally needed directly.
    void push_event_depth() { model_.push_event_depth(); }
    void pop_event_depth()  { model_.pop_event_depth(); }

private:
    SuperTerminalClientContext* ctx_ = nullptr;
    wingui::UiModel model_;
    wingui::UiState state_;
};

// ---------------------------------------------------------------------------
// App — builder-pattern entry point.
//
//   wg::App{}
//       .title("Hello")
//       .columns(80).rows(24)
//       .frame_rate(16)          // ~60 fps
//       .on_event([](const wg::Event& e) { ... })
//       .on_frame([](wg::Frame& f)       { ... })
//       .run();
// ---------------------------------------------------------------------------

class App {
public:
    App() = default;

    // Builder setters
    App& title(std::string_view t)            { title_    = t; return *this; }
    App& font(std::string_view family,
              int32_t pixel_height = 0)        { font_     = family;
                                                 desc_.font_pixel_height = pixel_height;
                                                 return *this; }
    App& columns(uint32_t c)                  { desc_.columns = c; return *this; }
    App& rows(uint32_t r)                     { desc_.rows = r; return *this; }
    App& dpi_scale(float s)                   { desc_.dpi_scale = s; return *this; }
    App& frame_rate(uint32_t target_ms)       { desc_.target_frame_ms = target_ms; return *this; }
    App& auto_present(bool v = true)          { desc_.auto_request_present = v ? 1 : 0; return *this; }
    App& command_queue_capacity(uint32_t n)   { desc_.command_queue_capacity = n; return *this; }
    App& event_queue_capacity(uint32_t n)     { desc_.event_queue_capacity = n; return *this; }
    App& initial_ui(std::string_view json)    { initial_ui_ = json; return *this; }
    App& text_shader_path(std::string_view p) { text_shader_ = p; return *this; }
    // Attach a reactive layout. attach() is called automatically before setup.
    App& layout(Layout& l) { layout_ = &l; return *this; }

    // Callbacks — all optional
    using SetupFn    = std::function<bool(SuperTerminalClientContext*)>;
    using EventFn    = std::function<void(const Event&)>;
    using FrameFn    = std::function<void(Frame&)>;
    using ShutdownFn = std::function<void()>;

    App& on_setup(SetupFn f)     { setup_fn_    = std::move(f); return *this; }
    App& on_event(EventFn f)     { event_fn_    = std::move(f); return *this; }
    App& on_frame(FrameFn f)     { frame_fn_    = std::move(f); return *this; }
    App& on_shutdown(ShutdownFn f){ shutdown_fn_ = std::move(f); return *this; }

    // Run the application. Blocks until the window closes.
    // Returns the exit code passed to super_terminal_request_stop().
    int run(SuperTerminalRunResult* out_result = nullptr) {
        // Commit string pointers just before launch.
        desc_.title_utf8       = title_.empty()       ? nullptr : title_.c_str();
        desc_.font_family_utf8 = font_.empty()        ? nullptr : font_.c_str();
        desc_.initial_ui_json_utf8 = initial_ui_.empty() ? nullptr : initial_ui_.c_str();
        desc_.text_shader_path_utf8 = text_shader_.empty() ? nullptr : text_shader_.c_str();
        desc_.user_data  = this;
        desc_.setup    = setup_fn_    ? &App::s_setup    : nullptr;
        desc_.on_event = event_fn_    ? &App::s_on_event : nullptr;
        desc_.on_frame = frame_fn_    ? &App::s_on_frame : nullptr;
        desc_.shutdown = shutdown_fn_ ? &App::s_shutdown : nullptr;

        SuperTerminalRunResult local{};
        super_terminal_run_hosted_app(&desc_, out_result ? out_result : &local);
        return out_result ? out_result->exit_code : local.exit_code;
    }

private:
    static int32_t WINGUI_CALL s_setup(SuperTerminalClientContext* ctx, void* ud) {
        auto* self = static_cast<App*>(ud);
        if (self->layout_) self->layout_->attach(ctx);
        return (!self->setup_fn_ || self->setup_fn_(ctx)) ? 1 : 0;
    }
    static void WINGUI_CALL s_on_event(SuperTerminalClientContext* ctx,
                                       const SuperTerminalEvent* e, void* ud) {
        auto* self = static_cast<App*>(ud);
        if (self->layout_) self->layout_->push_event_depth();
        Event ev(e);
        self->event_fn_(ev);
        if (self->layout_) self->layout_->pop_event_depth();
    }
    static void WINGUI_CALL s_on_frame(SuperTerminalClientContext* ctx,
                                       const SuperTerminalFrameTick* tick, void* ud) {
        auto* self = static_cast<App*>(ud);
        Frame frame(ctx, tick);
        self->frame_fn_(frame);
    }
    static void WINGUI_CALL s_shutdown(void* ud) {
        auto* self = static_cast<App*>(ud);
        self->shutdown_fn_();
    }

    SuperTerminalHostedAppDesc desc_{};
    std::string title_;
    std::string font_;
    std::string initial_ui_;
    std::string text_shader_;
    Layout*    layout_ = nullptr;
    SetupFn    setup_fn_;
    EventFn    event_fn_;
    FrameFn    frame_fn_;
    ShutdownFn shutdown_fn_;
};

// ---------------------------------------------------------------------------
// Free helpers
// ---------------------------------------------------------------------------

// Look up a pane by node id string, bound to an explicit buffer index.
inline Pane resolve_pane(SuperTerminalClientContext* ctx,
                         const char* node_id_utf8,
                         uint32_t buffer_index = 0) {
    SuperTerminalPaneId id{};
    super_terminal_resolve_pane_id_utf8(ctx, node_id_utf8, &id);
    return Pane(ctx, id, buffer_index);
}

// Register an RGBA asset that the caller owns.
// Returns the assigned asset id (or {0} on failure).
inline SuperTerminalAssetId register_rgba_asset(
    SuperTerminalClientContext* ctx,
    uint32_t w, uint32_t h,
    void* bgra8_pixels, uint32_t pitch,
    SuperTerminalFreeFn free_fn = nullptr,
    void* free_user_data = nullptr) {
    SuperTerminalAssetId id{};
    super_terminal_register_rgba_asset_owned(
        ctx, w, h, bgra8_pixels, pitch, free_fn, free_user_data, &id);
    return id;
}

inline void request_stop(SuperTerminalClientContext* ctx, int32_t exit_code = 0) {
    super_terminal_request_stop(ctx, exit_code);
}

inline bool key_down(SuperTerminalClientContext* ctx, uint32_t virtual_key) {
    return super_terminal_get_key_state(ctx, virtual_key) != 0;
}

// ---------------------------------------------------------------------------
// Re-export wingui:: UI builders into wg:: for single-namespace usage.
// All wg::ui_*() functions return wingui::UiNode / wingui::UiWindow.
// ---------------------------------------------------------------------------

using wingui::UiNode;
using wingui::UiWindow;
using wingui::UiState;
using wingui::UiModel;
using wingui::UiValue;
using wingui::uv;

// Layouts & containers
using wingui::ui_window;
using wingui::ui_stack;
using wingui::ui_row;
using wingui::ui_toolbar;
using wingui::ui_card;
using wingui::ui_scroll_view;
using wingui::ui_grid;
using wingui::ui_form;
using wingui::ui_divider;
using wingui::ui_split_pane;
using wingui::ui_split_view;

// Text / media
using wingui::ui_text;
using wingui::ui_heading;
using wingui::ui_link;
using wingui::ui_image;
using wingui::ui_badge;

// Inputs
using wingui::ui_button;
using wingui::ui_input;
using wingui::ui_number_input;
using wingui::ui_date_picker;
using wingui::ui_time_picker;
using wingui::ui_textarea;
using wingui::ui_rich_text;
using wingui::ui_checkbox;
using wingui::ui_switch_toggle;
using wingui::ui_slider;
using wingui::ui_progress;
using wingui::ui_canvas;
using wingui::ui_rtf_from_plain_text;
using wingui::ui_rtf_from_html;
using wingui::ui_rtf_from_markdown;

// D3D surface panes
using wingui::ui_text_grid;
using wingui::ui_indexed_graphics;
using wingui::ui_rgba_pane;

// Collections
using wingui::ui_select;
using wingui::ui_list_box;
using wingui::ui_radio_group;
using wingui::ui_table;
using wingui::ui_tree_view;
using wingui::ui_tabs;
using wingui::ui_context_menu;

// Data helpers (return plain Json, not UiNode)
using wingui::ui_option;
using wingui::ui_column;
using wingui::ui_table_row;
using wingui::ui_tree_item;
using wingui::ui_tab;

// Menu helpers (return plain Json)
using wingui::ui_menu_item;
using wingui::ui_menu_item_checked;
using wingui::ui_menu_item_disabled;
using wingui::ui_menu_separator;
using wingui::ui_menu_submenu;
using wingui::ui_menu;
using wingui::ui_menu_bar;
using wingui::ui_status_part;
using wingui::ui_status_bar;

// Event parsing
using wingui::ui_parse_event;
using wingui::ui_event_name;
using wingui::ui_event_ref;

} // namespace wg
