#pragma once

#include "wingui/terminal.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct WinguiSpecBindRuntime WinguiSpecBindRuntime;

typedef struct WinguiSpecBindEventView {
    const char* event_name_utf8;
    const char* payload_json_utf8;
    const char* source_utf8;
} WinguiSpecBindEventView;

typedef struct WinguiSpecBindFrameView WinguiSpecBindFrameView;

typedef struct WinguiSpecBindPaneRef {
    SuperTerminalPaneId pane_id;
    uint32_t buffer_index;
    uint32_t active_buffer_index;
} WinguiSpecBindPaneRef;

typedef void (WINGUI_CALL *WinguiSpecBindEventHandlerFn)(
    void* user_data,
    WinguiSpecBindRuntime* runtime,
    const WinguiSpecBindEventView* event_view);

typedef void (WINGUI_CALL *WinguiSpecBindFrameHandlerFn)(
    void* user_data,
    WinguiSpecBindRuntime* runtime,
    const WinguiSpecBindFrameView* frame_view);

typedef struct WinguiSpecBindRunDesc {
    const char* title_utf8;
    uint32_t columns;
    uint32_t rows;
    uint32_t flags;
    uint32_t command_queue_capacity;
    uint32_t event_queue_capacity;
    const char* font_family_utf8;
    int32_t font_pixel_height;
    float dpi_scale;
    const char* text_shader_path_utf8;
    uint32_t target_frame_ms;
    int32_t auto_request_present;
} WinguiSpecBindRunDesc;

WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_runtime_create(
    WinguiSpecBindRuntime** out_runtime);

WINGUI_API void WINGUI_CALL wingui_spec_bind_runtime_destroy(
    WinguiSpecBindRuntime* runtime);

WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_runtime_load_spec_json(
    WinguiSpecBindRuntime* runtime,
    const char* json_utf8);

WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_runtime_copy_spec_json(
    WinguiSpecBindRuntime* runtime,
    char* buffer_utf8,
    uint32_t buffer_size,
    uint32_t* out_required_size);

WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_runtime_bind_event(
    WinguiSpecBindRuntime* runtime,
    const char* event_name_utf8,
    WinguiSpecBindEventHandlerFn handler,
    void* user_data);

WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_runtime_unbind_event(
    WinguiSpecBindRuntime* runtime,
    const char* event_name_utf8);

WINGUI_API void WINGUI_CALL wingui_spec_bind_runtime_clear_bindings(
    WinguiSpecBindRuntime* runtime);

WINGUI_API void WINGUI_CALL wingui_spec_bind_runtime_set_default_handler(
    WinguiSpecBindRuntime* runtime,
    WinguiSpecBindEventHandlerFn handler,
    void* user_data);

WINGUI_API void WINGUI_CALL wingui_spec_bind_runtime_set_frame_handler(
    WinguiSpecBindRuntime* runtime,
    WinguiSpecBindFrameHandlerFn handler,
    void* user_data);

WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_runtime_request_stop(
    WinguiSpecBindRuntime* runtime,
    int32_t exit_code);

WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_runtime_get_patch_metrics(
    WinguiSpecBindRuntime* runtime,
    SuperTerminalNativeUiPatchMetrics* out_metrics);

WINGUI_API uint64_t WINGUI_CALL wingui_spec_bind_frame_index(
    const WinguiSpecBindFrameView* frame_view);

WINGUI_API uint64_t WINGUI_CALL wingui_spec_bind_frame_elapsed_ms(
    const WinguiSpecBindFrameView* frame_view);

WINGUI_API uint64_t WINGUI_CALL wingui_spec_bind_frame_delta_ms(
    const WinguiSpecBindFrameView* frame_view);

WINGUI_API uint32_t WINGUI_CALL wingui_spec_bind_frame_target_frame_ms(
    const WinguiSpecBindFrameView* frame_view);

WINGUI_API uint32_t WINGUI_CALL wingui_spec_bind_frame_buffer_index(
    const WinguiSpecBindFrameView* frame_view);

WINGUI_API uint32_t WINGUI_CALL wingui_spec_bind_frame_active_buffer_index(
    const WinguiSpecBindFrameView* frame_view);

WINGUI_API uint32_t WINGUI_CALL wingui_spec_bind_frame_buffer_count(
    const WinguiSpecBindFrameView* frame_view);

WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_get_key_state(
    const WinguiSpecBindFrameView* frame_view,
    uint32_t virtual_key);

WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_get_keyboard_state(
    const WinguiSpecBindFrameView* frame_view,
    WinguiKeyboardState* out_state);

WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_resolve_pane_utf8(
    const WinguiSpecBindFrameView* frame_view,
    const char* node_id_utf8,
    WinguiSpecBindPaneRef* out_pane);

WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_bind_pane(
    const WinguiSpecBindFrameView* frame_view,
    SuperTerminalPaneId pane_id,
    WinguiSpecBindPaneRef* out_pane);

WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_get_pane_layout(
    const WinguiSpecBindFrameView* frame_view,
    WinguiSpecBindPaneRef pane,
    SuperTerminalPaneLayout* out_layout);

WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_request_present(
    const WinguiSpecBindFrameView* frame_view);

WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_get_glyph_atlas_info(
    const WinguiSpecBindFrameView* frame_view,
    WinguiGlyphAtlasInfo* out_info);

WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_text_grid_write_cells(
    const WinguiSpecBindFrameView* frame_view,
    WinguiSpecBindPaneRef pane,
    const SuperTerminalTextGridCell* cells,
    uint32_t cell_count);

WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_text_grid_clear_region(
    const WinguiSpecBindFrameView* frame_view,
    WinguiSpecBindPaneRef pane,
    uint32_t row,
    uint32_t column,
    uint32_t width,
    uint32_t height,
    uint32_t fill_codepoint,
    WinguiGraphicsColour foreground,
    WinguiGraphicsColour background);

WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_indexed_graphics_upload(
    const WinguiSpecBindFrameView* frame_view,
    WinguiSpecBindPaneRef pane,
    const SuperTerminalIndexedGraphicsFrame* frame);

WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_rgba_upload(
    const WinguiSpecBindFrameView* frame_view,
    WinguiSpecBindPaneRef pane,
    const SuperTerminalRgbaFrame* frame);

WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_rgba_gpu_copy(
    const WinguiSpecBindFrameView* frame_view,
    WinguiSpecBindPaneRef dst_pane,
    uint32_t dst_x,
    uint32_t dst_y,
    WinguiSpecBindPaneRef src_pane,
    uint32_t src_x,
    uint32_t src_y,
    uint32_t region_width,
    uint32_t region_height);

WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_register_rgba_asset_owned(
    const WinguiSpecBindFrameView* frame_view,
    uint32_t width,
    uint32_t height,
    void* bgra8_pixels,
    uint32_t source_pitch,
    SuperTerminalFreeFn free_fn,
    void* free_user_data,
    SuperTerminalAssetId* out_asset_id);

WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_asset_blit_to_pane(
    const WinguiSpecBindFrameView* frame_view,
    SuperTerminalAssetId asset_id,
    uint32_t src_x,
    uint32_t src_y,
    uint32_t region_width,
    uint32_t region_height,
    WinguiSpecBindPaneRef dst_pane,
    uint32_t dst_x,
    uint32_t dst_y);

WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_define_sprite(
    const WinguiSpecBindFrameView* frame_view,
    WinguiSpecBindPaneRef pane,
    SuperTerminalSpriteId sprite_id,
    uint32_t frame_w,
    uint32_t frame_h,
    uint32_t frame_count,
    uint32_t frames_per_tick,
    void* pixels,
    void* palette,
    SuperTerminalFreeFn free_fn,
    void* free_user_data);

WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_render_sprites(
    const WinguiSpecBindFrameView* frame_view,
    WinguiSpecBindPaneRef pane,
    uint64_t sprite_tick,
    uint32_t target_width,
    uint32_t target_height,
    const SuperTerminalSpriteInstance* instances,
    uint32_t instance_count);

WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_vector_draw(
    const WinguiSpecBindFrameView* frame_view,
    WinguiSpecBindPaneRef pane,
    uint32_t content_buffer_mode,
    uint32_t blend_mode,
    int32_t clear_before,
    const float clear_color_rgba[4],
    const WinguiVectorPrimitive* primitives,
    uint32_t primitive_count);

WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_draw_line(
    const WinguiSpecBindFrameView* frame_view,
    WinguiSpecBindPaneRef pane,
    uint32_t content_buffer_mode,
    uint32_t blend_mode,
    int32_t clear_before,
    const float clear_color_rgba[4],
    float x0,
    float y0,
    float x1,
    float y1,
    float half_thickness,
    float color_r,
    float color_g,
    float color_b,
    float color_a);

WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_fill_rect(
    const WinguiSpecBindFrameView* frame_view,
    WinguiSpecBindPaneRef pane,
    uint32_t content_buffer_mode,
    uint32_t blend_mode,
    int32_t clear_before,
    const float clear_color_rgba[4],
    float x0,
    float y0,
    float x1,
    float y1,
    float corner_radius,
    float color_r,
    float color_g,
    float color_b,
    float color_a);

WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_stroke_rect(
    const WinguiSpecBindFrameView* frame_view,
    WinguiSpecBindPaneRef pane,
    uint32_t content_buffer_mode,
    uint32_t blend_mode,
    int32_t clear_before,
    const float clear_color_rgba[4],
    float x0,
    float y0,
    float x1,
    float y1,
    float half_thickness,
    float corner_radius,
    float color_r,
    float color_g,
    float color_b,
    float color_a);

WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_fill_circle(
    const WinguiSpecBindFrameView* frame_view,
    WinguiSpecBindPaneRef pane,
    uint32_t content_buffer_mode,
    uint32_t blend_mode,
    int32_t clear_before,
    const float clear_color_rgba[4],
    float cx,
    float cy,
    float radius,
    float color_r,
    float color_g,
    float color_b,
    float color_a);

WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_stroke_circle(
    const WinguiSpecBindFrameView* frame_view,
    WinguiSpecBindPaneRef pane,
    uint32_t content_buffer_mode,
    uint32_t blend_mode,
    int32_t clear_before,
    const float clear_color_rgba[4],
    float cx,
    float cy,
    float radius,
    float half_thickness,
    float color_r,
    float color_g,
    float color_b,
    float color_a);

WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_draw_arc(
    const WinguiSpecBindFrameView* frame_view,
    WinguiSpecBindPaneRef pane,
    uint32_t content_buffer_mode,
    uint32_t blend_mode,
    int32_t clear_before,
    const float clear_color_rgba[4],
    float cx,
    float cy,
    float radius,
    float half_thickness,
    float rotation_rad,
    float half_aperture_rad,
    float color_r,
    float color_g,
    float color_b,
    float color_a);

WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_draw_text_utf8(
    const WinguiSpecBindFrameView* frame_view,
    WinguiSpecBindPaneRef pane,
    uint32_t content_buffer_mode,
    uint32_t blend_mode,
    int32_t clear_before,
    const float clear_color_rgba[4],
    const char* text_utf8,
    float origin_x,
    float origin_y,
    float color_r,
    float color_g,
    float color_b,
    float color_a);

WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_indexed_fill_rect(
    const WinguiSpecBindFrameView* frame_view,
    WinguiSpecBindPaneRef pane,
    uint32_t x,
    uint32_t y,
    uint32_t width,
    uint32_t height,
    uint32_t palette_index);

WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_indexed_draw_line(
    const WinguiSpecBindFrameView* frame_view,
    WinguiSpecBindPaneRef pane,
    int32_t x0,
    int32_t y0,
    int32_t x1,
    int32_t y1,
    uint32_t palette_index);

WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_runtime_run(
    WinguiSpecBindRuntime* runtime,
    const WinguiSpecBindRunDesc* desc,
    SuperTerminalRunResult* out_result);

#ifdef __cplusplus
}
#endif