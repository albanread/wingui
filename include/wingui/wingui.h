#pragma once

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#  define WINGUI_CALL __cdecl
#  if defined(WINGUI_BUILD_DLL)
#    define WINGUI_API __declspec(dllexport)
#  else
#    define WINGUI_API __declspec(dllimport)
#  endif
#else
#  define WINGUI_CALL
#  define WINGUI_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct WinguiContext WinguiContext;
typedef struct WinguiWindow WinguiWindow;
typedef struct WinguiMenu WinguiMenu;
typedef struct WinguiTextGridRenderer WinguiTextGridRenderer;
typedef struct WinguiIndexedGraphicsRenderer WinguiIndexedGraphicsRenderer;
typedef struct WinguiRgbaPaneRenderer WinguiRgbaPaneRenderer;
typedef struct WinguiRgbaSurface WinguiRgbaSurface;
typedef struct WinguiRgbaBlitter WinguiRgbaBlitter;
typedef struct WinguiIndexedSurface WinguiIndexedSurface;

typedef enum WinguiRgbaBlitMode {
    WINGUI_RGBA_BLIT_OPAQUE = 0,
    WINGUI_RGBA_BLIT_ALPHA_OVER = 1,
} WinguiRgbaBlitMode;

typedef struct WinguiImageData {
    uint8_t* pixels;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
} WinguiImageData;

typedef intptr_t (WINGUI_CALL *WinguiWindowProc)(
    WinguiWindow* window,
    void* user_data,
    uint32_t message,
    uintptr_t wparam,
    intptr_t lparam,
    int32_t* handled);

typedef struct WinguiWindowDesc {
    const char* class_name_utf8;
    const char* title_utf8;
    void* parent;
    void* menu;
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
    uint32_t style;
    uint32_t ex_style;
    uint32_t class_style;
    void* icon;
    void* small_icon;
    void* cursor;
    void* background_brush;
    WinguiWindowProc window_proc;
    void* user_data;
} WinguiWindowDesc;

typedef struct WinguiMouseState {
    int32_t x;
    int32_t y;
    uint32_t buttons;
    int32_t inside_client;
} WinguiMouseState;

typedef struct WinguiKeyboardState {
    uint8_t pressed[256];
} WinguiKeyboardState;

enum {
    WINGUI_MOUSE_BUTTON_LEFT = 1u << 0,
    WINGUI_MOUSE_BUTTON_RIGHT = 1u << 1,
    WINGUI_MOUSE_BUTTON_MIDDLE = 1u << 2,
    WINGUI_MOUSE_BUTTON_X1 = 1u << 3,
    WINGUI_MOUSE_BUTTON_X2 = 1u << 4,
};

enum {
    WINGUI_KEY_BACKSPACE = 0x08,
    WINGUI_KEY_TAB = 0x09,
    WINGUI_KEY_ENTER = 0x0d,
    WINGUI_KEY_SHIFT = 0x10,
    WINGUI_KEY_CONTROL = 0x11,
    WINGUI_KEY_ALT = 0x12,
    WINGUI_KEY_ESCAPE = 0x1b,
    WINGUI_KEY_SPACE = 0x20,
    WINGUI_KEY_LEFT = 0x25,
    WINGUI_KEY_UP = 0x26,
    WINGUI_KEY_RIGHT = 0x27,
    WINGUI_KEY_DOWN = 0x28,
};

typedef struct WinguiContextDesc {
    void* hwnd;
    uint32_t width;
    uint32_t height;
    uint32_t buffer_count;
    uint32_t vsync_interval;
} WinguiContextDesc;

typedef struct WinguiTextGridUniforms {
    float viewport_width;
    float viewport_height;
    float cell_width;
    float cell_height;
    float atlas_width;
    float atlas_height;
    float row_origin;
    float effects_mode;
} WinguiTextGridUniforms;

typedef struct WinguiGlyphInstance {
    float pos_x;
    float pos_y;
    float uv_x;
    float uv_y;
    uint8_t fg[4];
    uint8_t bg[4];
    uint32_t flags;
} WinguiGlyphInstance;

typedef struct WinguiTextGridFrame {
    const WinguiGlyphInstance* instances;
    uint32_t instance_count;
    uint32_t reserved0;
    WinguiTextGridUniforms uniforms;
} WinguiTextGridFrame;

typedef struct WinguiGlyphAtlasInfo {
    float atlas_width;
    float atlas_height;
    float cell_width;
    float cell_height;
    uint32_t cols;
    uint32_t rows;
    uint32_t first_codepoint;
    uint32_t glyph_count;
    float ascent;
    float descent;
    float leading;
    uint32_t reserved0;
} WinguiGlyphAtlasInfo;

typedef struct WinguiGlyphAtlasDesc {
    const char* font_family_utf8;
    int32_t font_pixel_height;
    float dpi_scale;
    uint32_t first_codepoint;
    uint32_t glyph_count;
    uint32_t cols;
    uint32_t rows;
} WinguiGlyphAtlasDesc;

typedef struct WinguiGlyphAtlasBitmap {
    uint8_t* pixels_rgba;
    uint32_t width;
    uint32_t height;
    WinguiGlyphAtlasInfo info;
} WinguiGlyphAtlasBitmap;

typedef struct WinguiTextGridRendererDesc {
    WinguiContext* context;
    const char* shader_path_utf8;
} WinguiTextGridRendererDesc;

typedef struct WinguiGraphicsColour {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
} WinguiGraphicsColour;

typedef struct WinguiGraphicsLinePalette {
    WinguiGraphicsColour colours[16];
} WinguiGraphicsLinePalette;

typedef struct WinguiIndexedPaneLayout {
    float origin_x;
    float origin_y;
    float shown_width;
    float shown_height;
    float scale_x;
    float scale_y;
} WinguiIndexedPaneLayout;

typedef struct WinguiIndexedGraphicsFrame {
    const uint8_t* indexed_pixels;
    const WinguiGraphicsLinePalette* line_palettes;
    const WinguiGraphicsColour* global_palette;
    uint32_t screen_width;
    uint32_t screen_height;
    uint32_t buffer_width;
    uint32_t buffer_height;
    int32_t scroll_x;
    int32_t scroll_y;
    uint32_t pixel_aspect_num;
    uint32_t pixel_aspect_den;
} WinguiIndexedGraphicsFrame;

typedef struct WinguiSpriteAtlasEntry {
    uint32_t atlas_x;
    uint32_t atlas_y;
    uint32_t width;
    uint32_t height;
    uint32_t frame_count;
    uint32_t frame_w;
    uint32_t frame_h;
    uint32_t palette_offset;
} WinguiSpriteAtlasEntry;

typedef struct WinguiSpriteInstance {
    float x;
    float y;
    float rotation;
    float scale_x;
    float scale_y;
    float anchor_x;
    float anchor_y;
    uint32_t atlas_entry_id;
    uint32_t frame;
    uint32_t flags;
    uint32_t priority;
    float alpha;
    uint32_t effect_type;
    float effect_param1;
    float effect_param2;
    uint8_t effect_colour[4];
    uint32_t palette_override;
    uint32_t collision_group;
    uint32_t reserved0[2];
} WinguiSpriteInstance;

typedef struct WinguiIndexedGraphicsRendererDesc {
    WinguiContext* context;
    const char* graphics_shader_path_utf8;
    const char* sprite_shader_path_utf8;
    uint32_t sprite_atlas_size;
    uint32_t sprite_max_palettes;
} WinguiIndexedGraphicsRendererDesc;

typedef struct WinguiRgbaPaneRendererDesc {
    WinguiContext* context;
    const char* shader_path_utf8;
    uint32_t buffer_count;
} WinguiRgbaPaneRendererDesc;

typedef struct WinguiRectU32 {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
} WinguiRectU32;

enum {
    WINGUI_SPRITE_FLAG_VISIBLE = 1u << 0,
    WINGUI_SPRITE_FLAG_FLIP_H = 1u << 1,
    WINGUI_SPRITE_FLAG_FLIP_V = 1u << 2,
    WINGUI_SPRITE_FLAG_ADDITIVE = 1u << 3,
};

enum {
    WINGUI_AUDIO_WAVEFORM_SINE = 0,
    WINGUI_AUDIO_WAVEFORM_SQUARE = 1,
    WINGUI_AUDIO_WAVEFORM_SAWTOOTH = 2,
    WINGUI_AUDIO_WAVEFORM_TRIANGLE = 3,
    WINGUI_AUDIO_WAVEFORM_NOISE = 4,
    WINGUI_AUDIO_WAVEFORM_PULSE = 5,
};

enum {
    WINGUI_AUDIO_FILTER_NONE = 0,
    WINGUI_AUDIO_FILTER_LOW_PASS = 1,
    WINGUI_AUDIO_FILTER_HIGH_PASS = 2,
    WINGUI_AUDIO_FILTER_BAND_PASS = 3,
};

enum {
    WINGUI_ABC_STATE_STOPPED = 0,
    WINGUI_ABC_STATE_PLAYING = 1,
    WINGUI_ABC_STATE_PAUSED = 2,
};

WINGUI_API const char* WINGUI_CALL wingui_last_error_utf8(void);
WINGUI_API uint32_t WINGUI_CALL wingui_version_major(void);
WINGUI_API uint32_t WINGUI_CALL wingui_version_minor(void);
WINGUI_API uint32_t WINGUI_CALL wingui_version_patch(void);

WINGUI_API int32_t WINGUI_CALL wingui_create_window_utf8(const WinguiWindowDesc* desc, WinguiWindow** out_window);
WINGUI_API void WINGUI_CALL wingui_destroy_window(WinguiWindow* window);
WINGUI_API int32_t WINGUI_CALL wingui_window_show(WinguiWindow* window, int32_t show_command);
WINGUI_API int32_t WINGUI_CALL wingui_window_close(WinguiWindow* window);
WINGUI_API int32_t WINGUI_CALL wingui_window_set_title_utf8(WinguiWindow* window, const char* title_utf8);
WINGUI_API int32_t WINGUI_CALL wingui_window_set_menu(WinguiWindow* window, WinguiMenu* menu);
WINGUI_API int32_t WINGUI_CALL wingui_window_redraw_menu_bar(WinguiWindow* window);
WINGUI_API void* WINGUI_CALL wingui_window_hwnd(WinguiWindow* window);
WINGUI_API void WINGUI_CALL wingui_window_set_user_data(WinguiWindow* window, void* user_data);
WINGUI_API void* WINGUI_CALL wingui_window_user_data(WinguiWindow* window);
WINGUI_API int32_t WINGUI_CALL wingui_window_client_size(WinguiWindow* window, int32_t* out_width, int32_t* out_height);
WINGUI_API int32_t WINGUI_CALL wingui_window_get_key_state(WinguiWindow* window, uint32_t virtual_key);
WINGUI_API int32_t WINGUI_CALL wingui_window_get_keyboard_state(WinguiWindow* window, WinguiKeyboardState* out_state);
WINGUI_API int32_t WINGUI_CALL wingui_window_get_mouse_state(WinguiWindow* window, WinguiMouseState* out_state);

WINGUI_API int32_t WINGUI_CALL wingui_create_menu_bar(WinguiMenu** out_menu);
WINGUI_API int32_t WINGUI_CALL wingui_create_popup_menu_handle(WinguiMenu** out_menu);
WINGUI_API void WINGUI_CALL wingui_destroy_menu(WinguiMenu* menu);
WINGUI_API int32_t WINGUI_CALL wingui_menu_append_item_utf8(WinguiMenu* menu, uint32_t flags, uint32_t command_id, const char* text_utf8);
WINGUI_API int32_t WINGUI_CALL wingui_menu_append_separator(WinguiMenu* menu);
WINGUI_API int32_t WINGUI_CALL wingui_menu_append_submenu_utf8(WinguiMenu* menu, WinguiMenu* submenu, uint32_t flags, const char* text_utf8);
WINGUI_API int32_t WINGUI_CALL wingui_menu_remove_item(WinguiMenu* menu, uint32_t command_id);
WINGUI_API int32_t WINGUI_CALL wingui_menu_set_item_enabled(WinguiMenu* menu, uint32_t command_id, int32_t enabled);
WINGUI_API int32_t WINGUI_CALL wingui_menu_set_item_checked(WinguiMenu* menu, uint32_t command_id, int32_t checked);
WINGUI_API int32_t WINGUI_CALL wingui_menu_set_item_label_utf8(WinguiMenu* menu, uint32_t command_id, const char* text_utf8);
WINGUI_API void* WINGUI_CALL wingui_menu_native_handle(WinguiMenu* menu);

WINGUI_API int32_t WINGUI_CALL wingui_pump_message(int32_t wait_for_message, int32_t* out_exit_code);
WINGUI_API void WINGUI_CALL wingui_post_quit_message(int32_t exit_code);

WINGUI_API int32_t WINGUI_CALL wingui_build_glyph_atlas_utf8(const WinguiGlyphAtlasDesc* desc, WinguiGlyphAtlasBitmap* out_bitmap);
WINGUI_API void WINGUI_CALL wingui_free_glyph_atlas_bitmap(WinguiGlyphAtlasBitmap* bitmap);

WINGUI_API int32_t WINGUI_CALL wingui_create_text_grid_renderer(const WinguiTextGridRendererDesc* desc, WinguiTextGridRenderer** out_renderer);
WINGUI_API void WINGUI_CALL wingui_destroy_text_grid_renderer(WinguiTextGridRenderer* renderer);
WINGUI_API int32_t WINGUI_CALL wingui_text_grid_renderer_set_atlas(WinguiTextGridRenderer* renderer, const WinguiGlyphAtlasBitmap* bitmap);
WINGUI_API int32_t WINGUI_CALL wingui_text_grid_renderer_render(
    WinguiTextGridRenderer* renderer,
    int32_t viewport_x,
    int32_t viewport_y,
    int32_t viewport_width,
    int32_t viewport_height,
    const WinguiTextGridFrame* frame);

WINGUI_API int32_t WINGUI_CALL wingui_create_indexed_graphics_renderer(
    const WinguiIndexedGraphicsRendererDesc* desc,
    WinguiIndexedGraphicsRenderer** out_renderer);
WINGUI_API void WINGUI_CALL wingui_destroy_indexed_graphics_renderer(WinguiIndexedGraphicsRenderer* renderer);
WINGUI_API int32_t WINGUI_CALL wingui_compute_indexed_pane_layout(
    int32_t viewport_x,
    int32_t viewport_y,
    int32_t viewport_width,
    int32_t viewport_height,
    uint32_t screen_width,
    uint32_t screen_height,
    uint32_t pixel_aspect_num,
    uint32_t pixel_aspect_den,
    WinguiIndexedPaneLayout* out_layout);
WINGUI_API int32_t WINGUI_CALL wingui_indexed_graphics_render_pane(
    WinguiIndexedGraphicsRenderer* renderer,
    int32_t viewport_x,
    int32_t viewport_y,
    int32_t viewport_width,
    int32_t viewport_height,
    const WinguiIndexedGraphicsFrame* frame,
    WinguiIndexedPaneLayout* out_layout);

WINGUI_API int32_t WINGUI_CALL wingui_create_indexed_surface(
    WinguiContext* context,
    uint32_t buffer_count,
    WinguiIndexedSurface** out_surface);
WINGUI_API void WINGUI_CALL wingui_destroy_indexed_surface(WinguiIndexedSurface* surface);
WINGUI_API int32_t WINGUI_CALL wingui_indexed_surface_ensure_buffers(
    WinguiIndexedSurface* surface,
    uint32_t width,
    uint32_t height);
WINGUI_API int32_t WINGUI_CALL wingui_indexed_surface_get_buffer_info(
    WinguiIndexedSurface* surface,
    uint32_t* out_width,
    uint32_t* out_height,
    uint32_t* out_buffer_count);
WINGUI_API int32_t WINGUI_CALL wingui_indexed_surface_upload_pixels_region(
    WinguiIndexedSurface* surface,
    uint32_t buffer_index,
    WinguiRectU32 destination_region,
    const uint8_t* indexed_pixels,
    uint32_t source_pitch);
WINGUI_API int32_t WINGUI_CALL wingui_indexed_surface_upload_line_palettes(
    WinguiIndexedSurface* surface,
    uint32_t buffer_index,
    uint32_t start_row,
    uint32_t row_count,
    const WinguiGraphicsLinePalette* palettes);
WINGUI_API int32_t WINGUI_CALL wingui_indexed_surface_upload_global_palette(
    WinguiIndexedSurface* surface,
    uint32_t buffer_index,
    uint32_t start_index,
    uint32_t colour_count,
    const WinguiGraphicsColour* colours);
WINGUI_API int32_t WINGUI_CALL wingui_indexed_surface_render(
    WinguiIndexedGraphicsRenderer* renderer,
    WinguiIndexedSurface* surface,
    int32_t viewport_x,
    int32_t viewport_y,
    int32_t viewport_width,
    int32_t viewport_height,
    uint32_t screen_width,
    uint32_t screen_height,
    int32_t scroll_x,
    int32_t scroll_y,
    uint32_t pixel_aspect_num,
    uint32_t pixel_aspect_den,
    uint32_t buffer_index,
    WinguiIndexedPaneLayout* out_layout);
WINGUI_API int32_t WINGUI_CALL wingui_indexed_graphics_upload_sprite_atlas_region(
    WinguiIndexedGraphicsRenderer* renderer,
    uint32_t atlas_x,
    uint32_t atlas_y,
    uint32_t width,
    uint32_t height,
    const uint8_t* pixels,
    uint32_t source_pitch);
WINGUI_API int32_t WINGUI_CALL wingui_indexed_graphics_upload_sprite_palettes(
    WinguiIndexedGraphicsRenderer* renderer,
    const WinguiGraphicsLinePalette* palettes,
    uint32_t palette_count);
WINGUI_API int32_t WINGUI_CALL wingui_indexed_graphics_render_sprites(
    WinguiIndexedGraphicsRenderer* renderer,
    uint32_t target_width,
    uint32_t target_height,
    const WinguiIndexedPaneLayout* layout,
    const WinguiSpriteAtlasEntry* atlas_entries,
    uint32_t atlas_entry_count,
    const WinguiSpriteInstance* instances,
    uint32_t instance_count);

WINGUI_API int32_t WINGUI_CALL wingui_create_rgba_pane_renderer(
    const WinguiRgbaPaneRendererDesc* desc,
    WinguiRgbaPaneRenderer** out_renderer);
WINGUI_API void WINGUI_CALL wingui_destroy_rgba_pane_renderer(WinguiRgbaPaneRenderer* renderer);
WINGUI_API int32_t WINGUI_CALL wingui_create_rgba_surface(
    WinguiContext* context,
    uint32_t buffer_count,
    WinguiRgbaSurface** out_surface);
WINGUI_API void WINGUI_CALL wingui_destroy_rgba_surface(WinguiRgbaSurface* surface);
WINGUI_API int32_t WINGUI_CALL wingui_rgba_surface_ensure_buffers(
    WinguiRgbaSurface* surface,
    uint32_t width,
    uint32_t height);
WINGUI_API int32_t WINGUI_CALL wingui_rgba_surface_get_buffer_info(
    WinguiRgbaSurface* surface,
    uint32_t* out_width,
    uint32_t* out_height,
    uint32_t* out_buffer_count);
WINGUI_API int32_t WINGUI_CALL wingui_rgba_surface_upload_bgra8(
    WinguiRgbaSurface* surface,
    uint32_t buffer_index,
    const uint8_t* pixels,
    uint32_t source_pitch);
WINGUI_API int32_t WINGUI_CALL wingui_rgba_surface_upload_bgra8_region(
    WinguiRgbaSurface* surface,
    uint32_t buffer_index,
    WinguiRectU32 destination_region,
    const uint8_t* pixels,
    uint32_t source_pitch);
WINGUI_API int32_t WINGUI_CALL wingui_rgba_surface_copy_region(
    WinguiRgbaSurface* surface,
    uint32_t dst_buffer_index,
    uint32_t dst_x,
    uint32_t dst_y,
    uint32_t src_buffer_index,
    WinguiRectU32 source_region);
WINGUI_API int32_t WINGUI_CALL wingui_rgba_surface_copy_from_surface(
    WinguiRgbaSurface* dst_surface,
    uint32_t dst_buffer_index,
    uint32_t dst_x,
    uint32_t dst_y,
    WinguiRgbaSurface* src_surface,
    uint32_t src_buffer_index,
    WinguiRectU32 source_region);
WINGUI_API int32_t WINGUI_CALL wingui_create_rgba_blitter(
    WinguiContext* context,
    const char* shader_path_utf8,
    WinguiRgbaBlitter** out_blitter);
WINGUI_API void WINGUI_CALL wingui_destroy_rgba_blitter(WinguiRgbaBlitter* blitter);
WINGUI_API int32_t WINGUI_CALL wingui_rgba_surface_shader_blit(
    WinguiRgbaBlitter* blitter,
    WinguiRgbaSurface* dst_surface,
    uint32_t dst_buffer_index,
    WinguiRectU32 dst_rect,
    WinguiRgbaSurface* src_surface,
    uint32_t src_buffer_index,
    WinguiRectU32 src_rect,
    float tint_r,
    float tint_g,
    float tint_b,
    float tint_a,
    uint32_t blend_mode);
WINGUI_API int32_t WINGUI_CALL wingui_rgba_surface_render(
    WinguiRgbaPaneRenderer* renderer,
    WinguiRgbaSurface* surface,
    int32_t viewport_x,
    int32_t viewport_y,
    int32_t viewport_width,
    int32_t viewport_height,
    uint32_t screen_width,
    uint32_t screen_height,
    uint32_t pixel_aspect_num,
    uint32_t pixel_aspect_den,
    uint32_t buffer_index,
    WinguiIndexedPaneLayout* out_layout);
WINGUI_API int32_t WINGUI_CALL wingui_rgba_pane_ensure_buffers(
    WinguiRgbaPaneRenderer* renderer,
    uint32_t width,
    uint32_t height);
WINGUI_API int32_t WINGUI_CALL wingui_rgba_pane_get_buffer_info(
    WinguiRgbaPaneRenderer* renderer,
    uint32_t* out_width,
    uint32_t* out_height,
    uint32_t* out_buffer_count);
WINGUI_API int32_t WINGUI_CALL wingui_rgba_pane_upload_bgra8(
    WinguiRgbaPaneRenderer* renderer,
    uint32_t buffer_index,
    const uint8_t* pixels,
    uint32_t source_pitch);
WINGUI_API int32_t WINGUI_CALL wingui_rgba_pane_upload_bgra8_region(
    WinguiRgbaPaneRenderer* renderer,
    uint32_t buffer_index,
    WinguiRectU32 destination_region,
    const uint8_t* pixels,
    uint32_t source_pitch);
WINGUI_API int32_t WINGUI_CALL wingui_rgba_pane_copy_region(
    WinguiRgbaPaneRenderer* renderer,
    uint32_t dst_buffer_index,
    uint32_t dst_x,
    uint32_t dst_y,
    uint32_t src_buffer_index,
    WinguiRectU32 source_region);
WINGUI_API int32_t WINGUI_CALL wingui_rgba_pane_render(
    WinguiRgbaPaneRenderer* renderer,
    int32_t viewport_x,
    int32_t viewport_y,
    int32_t viewport_width,
    int32_t viewport_height,
    uint32_t screen_width,
    uint32_t screen_height,
    uint32_t pixel_aspect_num,
    uint32_t pixel_aspect_den,
    uint32_t buffer_index,
    WinguiIndexedPaneLayout* out_layout);
WINGUI_API int32_t WINGUI_CALL wingui_rgba_pane_save_buffer_utf8(
    WinguiRgbaPaneRenderer* renderer,
    uint32_t buffer_index,
    const char* path_utf8);
WINGUI_API int32_t WINGUI_CALL wingui_rgba_pane_save_buffer_resized_utf8(
    WinguiRgbaPaneRenderer* renderer,
    uint32_t buffer_index,
    const char* path_utf8,
    uint32_t output_width,
    uint32_t output_height);
WINGUI_API int32_t WINGUI_CALL wingui_rgba_pane_load_image_into_buffer_utf8(
    WinguiRgbaPaneRenderer* renderer,
    uint32_t buffer_index,
    const char* path_utf8);

WINGUI_API int32_t WINGUI_CALL wingui_load_image_utf8(const char* path_utf8, WinguiImageData* out_image);
WINGUI_API void WINGUI_CALL wingui_free_image(WinguiImageData* image);
WINGUI_API int32_t WINGUI_CALL wingui_save_bgra8_image_utf8(
    const char* path_utf8,
    const uint8_t* pixels,
    uint32_t width,
    uint32_t height,
    uint32_t stride);
WINGUI_API int32_t WINGUI_CALL wingui_save_bgra8_image_resized_utf8(
    const char* path_utf8,
    const uint8_t* pixels,
    uint32_t width,
    uint32_t height,
    uint32_t stride,
    uint32_t output_width,
    uint32_t output_height);
WINGUI_API int32_t WINGUI_CALL wingui_indexed_graphics_load_image_into_sprite_atlas_utf8(
    WinguiIndexedGraphicsRenderer* renderer,
    uint32_t atlas_x,
    uint32_t atlas_y,
    const char* path_utf8);

WINGUI_API int32_t WINGUI_CALL wingui_audio_init(void);
WINGUI_API void WINGUI_CALL wingui_audio_shutdown(void);
WINGUI_API int32_t WINGUI_CALL wingui_audio_is_initialized(void);
WINGUI_API void WINGUI_CALL wingui_audio_stop_all(void);
WINGUI_API uint32_t WINGUI_CALL wingui_audio_create_beep(float frequency, float duration);
WINGUI_API uint32_t WINGUI_CALL wingui_audio_create_zap(float frequency, float duration);
WINGUI_API uint32_t WINGUI_CALL wingui_audio_create_tone(float frequency, float duration, int32_t waveform);
WINGUI_API uint32_t WINGUI_CALL wingui_audio_create_note(
    float midi_note,
    float duration,
    int32_t waveform,
    float attack,
    float decay,
    float sustain,
    float release);
WINGUI_API uint32_t WINGUI_CALL wingui_audio_create_noise(int32_t noise_type, float duration);
WINGUI_API uint32_t WINGUI_CALL wingui_audio_create_fm(float carrier, float modulator, float index, float duration);
WINGUI_API uint32_t WINGUI_CALL wingui_audio_create_filtered_tone(
    float frequency,
    float duration,
    int32_t waveform,
    int32_t filter_type,
    float cutoff,
    float resonance);
WINGUI_API uint32_t WINGUI_CALL wingui_audio_create_filtered_note(
    float midi_note,
    float duration,
    int32_t waveform,
    float attack,
    float decay,
    float sustain,
    float release,
    int32_t filter_type,
    float cutoff,
    float resonance);
WINGUI_API int32_t WINGUI_CALL wingui_audio_play(uint32_t sound_id, float volume, float pan);
WINGUI_API int32_t WINGUI_CALL wingui_audio_play_simple(uint32_t sound_id);
WINGUI_API void WINGUI_CALL wingui_audio_stop_sound(uint32_t sound_id);
WINGUI_API int32_t WINGUI_CALL wingui_audio_is_sound_playing(uint32_t sound_id);
WINGUI_API float WINGUI_CALL wingui_audio_sound_duration(uint32_t sound_id);
WINGUI_API int32_t WINGUI_CALL wingui_audio_free_sound(uint32_t sound_id);
WINGUI_API void WINGUI_CALL wingui_audio_free_all(void);
WINGUI_API void WINGUI_CALL wingui_audio_set_master_volume(float volume);
WINGUI_API float WINGUI_CALL wingui_audio_get_master_volume(void);
WINGUI_API int32_t WINGUI_CALL wingui_audio_sound_exists(uint32_t sound_id);
WINGUI_API uint32_t WINGUI_CALL wingui_audio_sound_count(void);
WINGUI_API uint64_t WINGUI_CALL wingui_audio_sound_memory_usage(void);
WINGUI_API float WINGUI_CALL wingui_audio_note_to_frequency(int32_t midi_note);
WINGUI_API int32_t WINGUI_CALL wingui_audio_frequency_to_note(float frequency);
WINGUI_API int32_t WINGUI_CALL wingui_audio_export_wav_utf8(uint32_t sound_id, const char* path_utf8, float volume);

WINGUI_API int32_t WINGUI_CALL wingui_midi_init(void);
WINGUI_API void WINGUI_CALL wingui_midi_shutdown(void);
WINGUI_API int32_t WINGUI_CALL wingui_midi_is_initialized(void);
WINGUI_API void WINGUI_CALL wingui_midi_reset(void);
WINGUI_API int32_t WINGUI_CALL wingui_midi_short_message(uint32_t message);
WINGUI_API int32_t WINGUI_CALL wingui_midi_program_change(uint8_t channel, uint8_t program);
WINGUI_API int32_t WINGUI_CALL wingui_midi_control_change(uint8_t channel, uint8_t controller, uint8_t value);
WINGUI_API int32_t WINGUI_CALL wingui_midi_note_on(uint8_t channel, uint8_t note, uint8_t velocity);
WINGUI_API int32_t WINGUI_CALL wingui_midi_note_off(uint8_t channel, uint8_t note, uint8_t velocity);

WINGUI_API int32_t WINGUI_CALL wingui_abc_init(void);
WINGUI_API void WINGUI_CALL wingui_abc_shutdown(void);
WINGUI_API uint32_t WINGUI_CALL wingui_abc_load_utf8(const char* abc_text_utf8);
WINGUI_API int32_t WINGUI_CALL wingui_abc_play_utf8(const char* abc_text_utf8, float volume);
WINGUI_API int32_t WINGUI_CALL wingui_abc_play_utf8_simple(const char* abc_text_utf8);
WINGUI_API int32_t WINGUI_CALL wingui_abc_play(uint32_t music_id, float volume);
WINGUI_API int32_t WINGUI_CALL wingui_abc_play_simple(uint32_t music_id);
WINGUI_API void WINGUI_CALL wingui_abc_stop_all(void);
WINGUI_API void WINGUI_CALL wingui_abc_pause_all(void);
WINGUI_API void WINGUI_CALL wingui_abc_resume_all(void);
WINGUI_API void WINGUI_CALL wingui_abc_set_master_volume(float volume);
WINGUI_API float WINGUI_CALL wingui_abc_get_master_volume(void);
WINGUI_API int32_t WINGUI_CALL wingui_abc_free(uint32_t music_id);
WINGUI_API void WINGUI_CALL wingui_abc_free_all(void);
WINGUI_API int32_t WINGUI_CALL wingui_abc_is_playing(void);
WINGUI_API int32_t WINGUI_CALL wingui_abc_is_playing_id(uint32_t music_id);
WINGUI_API int32_t WINGUI_CALL wingui_abc_state(void);
WINGUI_API int32_t WINGUI_CALL wingui_abc_exists(uint32_t music_id);
WINGUI_API uint32_t WINGUI_CALL wingui_abc_count(void);
WINGUI_API float WINGUI_CALL wingui_abc_get_tempo(uint32_t music_id);
WINGUI_API int32_t WINGUI_CALL wingui_abc_export_midi_utf8(uint32_t music_id, const char* path_utf8);

WINGUI_API int32_t WINGUI_CALL wingui_create_context(const WinguiContextDesc* desc, WinguiContext** out_context);
WINGUI_API void WINGUI_CALL wingui_destroy_context(WinguiContext* context);
WINGUI_API int32_t WINGUI_CALL wingui_resize_context(WinguiContext* context, uint32_t width, uint32_t height);
WINGUI_API int32_t WINGUI_CALL wingui_begin_frame(WinguiContext* context, float red, float green, float blue, float alpha);
WINGUI_API int32_t WINGUI_CALL wingui_present(WinguiContext* context, uint32_t sync_interval);

WINGUI_API void* WINGUI_CALL wingui_d3d11_device(WinguiContext* context);
WINGUI_API void* WINGUI_CALL wingui_d3d11_context(WinguiContext* context);
WINGUI_API void* WINGUI_CALL wingui_dxgi_swap_chain(WinguiContext* context);
WINGUI_API void* WINGUI_CALL wingui_d3d11_render_target_view(WinguiContext* context);

WINGUI_API float WINGUI_CALL wingui_current_dpi_scale(void* hwnd);

WINGUI_API int32_t WINGUI_CALL wingui_compile_shader_from_file_utf8(
    const char* path_utf8,
    const char* entry_utf8,
    const char* target_utf8,
    void** out_blob,
    size_t* out_size);
WINGUI_API void WINGUI_CALL wingui_release_blob(void* blob);

#ifdef __cplusplus
}
#endif