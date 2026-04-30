#pragma once

#include "wingui/wingui.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SuperTerminalClientContext SuperTerminalClientContext;

enum {
    SUPERTERMINAL_WAIT_INFINITE = 0xffffffffu,
};

typedef struct SuperTerminalPaneId {
    uint64_t value;
} SuperTerminalPaneId;

typedef struct SuperTerminalAssetId {
    uint64_t value;
} SuperTerminalAssetId;

typedef struct SuperTerminalPaneLayout {
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
    int32_t visible;
    uint32_t columns;
    uint32_t rows;
    float cell_width;
    float cell_height;
} SuperTerminalPaneLayout;

typedef enum SuperTerminalHostErrorCode {
    SUPERTERMINAL_HOST_ERROR_NONE = 0,
    SUPERTERMINAL_HOST_ERROR_INVALID_ARGUMENT = 1,
    SUPERTERMINAL_HOST_ERROR_WINDOW_CREATE = 2,
    SUPERTERMINAL_HOST_ERROR_CONTEXT_CREATE = 3,
    SUPERTERMINAL_HOST_ERROR_GLYPH_ATLAS_CREATE = 4,
    SUPERTERMINAL_HOST_ERROR_RENDERER_CREATE = 5,
    SUPERTERMINAL_HOST_ERROR_NATIVE_UI_ATTACH = 6,
    SUPERTERMINAL_HOST_ERROR_CLIENT_START = 7,
    SUPERTERMINAL_HOST_ERROR_MESSAGE_LOOP = 8,
} SuperTerminalHostErrorCode;

typedef enum SuperTerminalCommandType {
    SUPERTERMINAL_CMD_NOP = 0,
    SUPERTERMINAL_CMD_NATIVE_UI_PUBLISH = 1,
    SUPERTERMINAL_CMD_NATIVE_UI_PATCH = 2,
    SUPERTERMINAL_CMD_WINDOW_SET_TITLE = 3,
    SUPERTERMINAL_CMD_TEXT_GRID_WRITE_CELLS = 4,
    SUPERTERMINAL_CMD_TEXT_GRID_CLEAR_REGION = 5,
    SUPERTERMINAL_CMD_REQUEST_PRESENT = 6,
    SUPERTERMINAL_CMD_REQUEST_CLOSE = 7,
    SUPERTERMINAL_CMD_RGBA_UPLOAD_OWNED = 8,
    SUPERTERMINAL_CMD_FRAME_SWAP = 9,
    SUPERTERMINAL_CMD_RGBA_GPU_COPY = 10,
    SUPERTERMINAL_CMD_RGBA_ASSET_REGISTER_OWNED = 11,
    SUPERTERMINAL_CMD_RGBA_ASSET_BLIT_TO_PANE = 12,
    SUPERTERMINAL_CMD_INDEXED_UPLOAD_OWNED = 13,
} SuperTerminalCommandType;

typedef void (WINGUI_CALL *SuperTerminalFreeFn)(void* user_data, void* buffer);

typedef enum SuperTerminalEventType {
    SUPERTERMINAL_EVENT_NONE = 0,
    SUPERTERMINAL_EVENT_KEY = 1,
    SUPERTERMINAL_EVENT_CHAR = 2,
    SUPERTERMINAL_EVENT_MOUSE = 3,
    SUPERTERMINAL_EVENT_PANE_INPUT = 4,
    SUPERTERMINAL_EVENT_RESIZE = 5,
    SUPERTERMINAL_EVENT_FOCUS = 6,
    SUPERTERMINAL_EVENT_NATIVE_UI = 7,
    SUPERTERMINAL_EVENT_CLOSE_REQUESTED = 8,
    SUPERTERMINAL_EVENT_HOST_STOPPING = 9,
} SuperTerminalEventType;

typedef enum SuperTerminalMouseEventKind {
    SUPERTERMINAL_MOUSE_MOVE = 0,
    SUPERTERMINAL_MOUSE_BUTTON_DOWN = 1,
    SUPERTERMINAL_MOUSE_BUTTON_UP = 2,
    SUPERTERMINAL_MOUSE_WHEEL = 3,
} SuperTerminalMouseEventKind;

typedef enum SuperTerminalPaneInputDeviceKind {
    SUPERTERMINAL_PANE_INPUT_MOUSE = 0,
    SUPERTERMINAL_PANE_INPUT_KEYBOARD = 1,
    SUPERTERMINAL_PANE_INPUT_FOCUS = 2,
} SuperTerminalPaneInputDeviceKind;

typedef struct SuperTerminalNativeUiPublish {
    const char* json_utf8;
} SuperTerminalNativeUiPublish;

typedef struct SuperTerminalNativeUiPatch {
    const char* patch_json_utf8;
} SuperTerminalNativeUiPatch;

typedef struct SuperTerminalSetTitle {
    char title_utf8[256];
} SuperTerminalSetTitle;

typedef struct SuperTerminalTextGridCell {
    uint32_t row;
    uint32_t column;
    uint32_t codepoint;
    WinguiGraphicsColour foreground;
    WinguiGraphicsColour background;
} SuperTerminalTextGridCell;

typedef struct SuperTerminalTextGridWriteCells {
    SuperTerminalPaneId pane_id;
    const SuperTerminalTextGridCell* cells;
    uint32_t cell_count;
} SuperTerminalTextGridWriteCells;

typedef struct SuperTerminalTextGridClearRegion {
    SuperTerminalPaneId pane_id;
    uint32_t row;
    uint32_t column;
    uint32_t width;
    uint32_t height;
    uint32_t fill_codepoint;
    WinguiGraphicsColour foreground;
    WinguiGraphicsColour background;
} SuperTerminalTextGridClearRegion;

typedef struct SuperTerminalIndexedGraphicsFrame {
    uint32_t screen_width;
    uint32_t screen_height;
    uint32_t buffer_width;
    uint32_t buffer_height;
    int32_t scroll_x;
    int32_t scroll_y;
    uint32_t pixel_aspect_num;
    uint32_t pixel_aspect_den;
    const uint8_t* indexed_pixels;
    const WinguiGraphicsLinePalette* line_palettes;
    const WinguiGraphicsColour* global_palette;
    uint32_t global_palette_count;
} SuperTerminalIndexedGraphicsFrame;

typedef struct SuperTerminalRgbaFrame {
    uint32_t width;
    uint32_t height;
    uint32_t source_pitch;
    uint32_t screen_width;
    uint32_t screen_height;
    uint32_t pixel_aspect_num;
    uint32_t pixel_aspect_den;
    const uint8_t* bgra8_pixels;
} SuperTerminalRgbaFrame;

typedef struct SuperTerminalRgbaUploadOwned {
    SuperTerminalPaneId pane_id;
    uint32_t buffer_index;
    uint32_t dst_x;
    uint32_t dst_y;
    uint32_t region_width;
    uint32_t region_height;
    uint32_t source_pitch;
    uint32_t surface_width;
    uint32_t surface_height;
    uint32_t screen_width;
    uint32_t screen_height;
    uint32_t pixel_aspect_num;
    uint32_t pixel_aspect_den;
    void* bgra8_pixels;
    SuperTerminalFreeFn free_fn;
    void* free_user_data;
} SuperTerminalRgbaUploadOwned;

typedef struct SuperTerminalRgbaGpuCopy {
    SuperTerminalPaneId dst_pane_id;
    uint32_t dst_buffer_index;
    uint32_t dst_x;
    uint32_t dst_y;
    SuperTerminalPaneId src_pane_id;
    uint32_t src_buffer_index;
    uint32_t src_x;
    uint32_t src_y;
    uint32_t region_width;
    uint32_t region_height;
} SuperTerminalRgbaGpuCopy;

typedef struct SuperTerminalRgbaAssetRegisterOwned {
    SuperTerminalAssetId asset_id;
    uint32_t width;
    uint32_t height;
    uint32_t source_pitch;
    void* bgra8_pixels;
    SuperTerminalFreeFn free_fn;
    void* free_user_data;
} SuperTerminalRgbaAssetRegisterOwned;

typedef struct SuperTerminalRgbaAssetBlitToPane {
    SuperTerminalAssetId asset_id;
    uint32_t src_x;
    uint32_t src_y;
    uint32_t region_width;
    uint32_t region_height;
    SuperTerminalPaneId dst_pane_id;
    uint32_t dst_buffer_index;
    uint32_t dst_x;
    uint32_t dst_y;
} SuperTerminalRgbaAssetBlitToPane;

typedef struct SuperTerminalIndexedUploadOwned {
    SuperTerminalPaneId pane_id;
    uint32_t buffer_index;
    uint32_t buffer_width;
    uint32_t buffer_height;
    uint32_t screen_width;
    uint32_t screen_height;
    int32_t  scroll_x;
    int32_t  scroll_y;
    uint32_t pixel_aspect_num;
    uint32_t pixel_aspect_den;
    uint32_t global_palette_count;
    void*    indexed_pixels;       /* owned uint8 buffer of size buffer_width*buffer_height */
    void*    line_palettes;        /* owned WinguiGraphicsLinePalette[buffer_height] */
    void*    global_palette;       /* owned WinguiGraphicsColour[global_palette_count] */
    SuperTerminalFreeFn free_fn;   /* called once per non-null buffer above */
    void*    free_user_data;
} SuperTerminalIndexedUploadOwned;

typedef struct SuperTerminalCommand {
    SuperTerminalCommandType type;
    uint32_t sequence;
    union {
        SuperTerminalNativeUiPublish native_ui_publish;
        SuperTerminalNativeUiPatch native_ui_patch;
        SuperTerminalSetTitle set_title;
        SuperTerminalTextGridWriteCells text_grid_write_cells;
        SuperTerminalTextGridClearRegion text_grid_clear_region;
        SuperTerminalRgbaUploadOwned rgba_upload_owned;
        SuperTerminalRgbaGpuCopy rgba_gpu_copy;
        SuperTerminalRgbaAssetRegisterOwned rgba_asset_register_owned;
        SuperTerminalRgbaAssetBlitToPane rgba_asset_blit_to_pane;
        SuperTerminalIndexedUploadOwned indexed_upload_owned;
    } data;
} SuperTerminalCommand;

typedef struct SuperTerminalKeyEvent {
    uint32_t virtual_key;
    uint32_t repeat_count;
    int32_t is_down;
    uint32_t modifiers;
} SuperTerminalKeyEvent;

typedef struct SuperTerminalCharEvent {
    uint32_t codepoint;
} SuperTerminalCharEvent;

typedef struct SuperTerminalMouseEvent {
    uint32_t kind;
    int32_t x;
    int32_t y;
    int32_t wheel_delta;
    uint32_t buttons;
    uint32_t button_mask;
} SuperTerminalMouseEvent;

typedef struct SuperTerminalPaneInputEvent {
    SuperTerminalPaneId pane_id;
    uint32_t device_kind;
    uint32_t event_kind;
    int32_t x;
    int32_t y;
    int32_t wheel_delta;
    uint32_t buttons;
    uint32_t button_mask;
    uint32_t virtual_key;
    uint32_t modifiers;
    int32_t focused;
} SuperTerminalPaneInputEvent;

typedef struct SuperTerminalResizeEvent {
    uint32_t pixel_width;
    uint32_t pixel_height;
    uint32_t columns;
    uint32_t rows;
    float dpi_scale;
    float cell_width;
    float cell_height;
} SuperTerminalResizeEvent;

typedef struct SuperTerminalFocusEvent {
    int32_t focused;
} SuperTerminalFocusEvent;

typedef struct SuperTerminalNativeUiEvent {
    char payload_json_utf8[512];
} SuperTerminalNativeUiEvent;

typedef struct SuperTerminalHostStoppingEvent {
    int32_t exit_code;
} SuperTerminalHostStoppingEvent;

typedef struct SuperTerminalEvent {
    SuperTerminalEventType type;
    uint32_t sequence;
    union {
        SuperTerminalKeyEvent key;
        SuperTerminalCharEvent character;
        SuperTerminalMouseEvent mouse;
        SuperTerminalPaneInputEvent pane_input;
        SuperTerminalResizeEvent resize;
        SuperTerminalFocusEvent focus;
        SuperTerminalNativeUiEvent native_ui;
        SuperTerminalHostStoppingEvent host_stopping;
    } data;
} SuperTerminalEvent;

typedef struct SuperTerminalAppDesc {
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
    const char* initial_ui_json_utf8;
    void* user_data;
    int32_t (WINGUI_CALL *startup)(SuperTerminalClientContext* ctx, void* user_data);
    void (WINGUI_CALL *shutdown)(void* user_data);
} SuperTerminalAppDesc;

typedef struct SuperTerminalRunResult {
    int32_t exit_code;
    int32_t host_error_code;
    char message_utf8[256];
} SuperTerminalRunResult;

typedef struct SuperTerminalFrameTick {
    uint64_t frame_index;
    uint64_t elapsed_ms;
    uint64_t delta_ms;
    uint32_t target_frame_ms;
    uint32_t active_buffer_index;
    uint32_t buffer_index;
    uint32_t buffer_count;
} SuperTerminalFrameTick;

typedef int32_t (WINGUI_CALL *SuperTerminalHostedSetupFn)(
    SuperTerminalClientContext* ctx,
    void* user_data);

typedef void (WINGUI_CALL *SuperTerminalHostedEventFn)(
    SuperTerminalClientContext* ctx,
    const SuperTerminalEvent* event,
    void* user_data);

typedef void (WINGUI_CALL *SuperTerminalHostedFrameFn)(
    SuperTerminalClientContext* ctx,
    const SuperTerminalFrameTick* tick,
    void* user_data);

typedef void (WINGUI_CALL *SuperTerminalHostedShutdownFn)(
    void* user_data);

typedef struct SuperTerminalHostedAppDesc {
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
    const char* initial_ui_json_utf8;
    uint32_t target_frame_ms;
    int32_t auto_request_present;
    void* user_data;
    SuperTerminalHostedSetupFn setup;
    SuperTerminalHostedEventFn on_event;
    SuperTerminalHostedFrameFn on_frame;
    SuperTerminalHostedShutdownFn shutdown;
} SuperTerminalHostedAppDesc;

typedef struct SuperTerminalNativeUiPatchMetrics {
    uint64_t publish_count;
    uint64_t patch_request_count;
    uint64_t direct_apply_count;
    uint64_t subtree_rebuild_count;
    uint64_t window_rebuild_count;
    uint64_t resize_reject_count;
    uint64_t failed_patch_count;
} SuperTerminalNativeUiPatchMetrics;

WINGUI_API int32_t WINGUI_CALL super_terminal_run(
    const SuperTerminalAppDesc* desc,
    SuperTerminalRunResult* out_result);

WINGUI_API int32_t WINGUI_CALL super_terminal_run_hosted_app(
    const SuperTerminalHostedAppDesc* desc,
    SuperTerminalRunResult* out_result);

WINGUI_API int32_t WINGUI_CALL super_terminal_enqueue(
    SuperTerminalClientContext* ctx,
    const SuperTerminalCommand* command);

WINGUI_API int32_t WINGUI_CALL super_terminal_wait_event(
    SuperTerminalClientContext* ctx,
    uint32_t timeout_ms,
    SuperTerminalEvent* out_event);

WINGUI_API void* WINGUI_CALL super_terminal_event_handle(
    SuperTerminalClientContext* ctx);

WINGUI_API int32_t WINGUI_CALL super_terminal_request_stop(
    SuperTerminalClientContext* ctx,
    int32_t exit_code);

WINGUI_API int32_t WINGUI_CALL super_terminal_get_key_state(
    SuperTerminalClientContext* ctx,
    uint32_t virtual_key);

WINGUI_API int32_t WINGUI_CALL super_terminal_get_keyboard_state(
    SuperTerminalClientContext* ctx,
    WinguiKeyboardState* out_state);

WINGUI_API int32_t WINGUI_CALL super_terminal_resolve_pane_id_utf8(
    SuperTerminalClientContext* ctx,
    const char* node_id_utf8,
    SuperTerminalPaneId* out_pane_id);

WINGUI_API int32_t WINGUI_CALL super_terminal_get_pane_layout(
    SuperTerminalClientContext* ctx,
    SuperTerminalPaneId pane_id,
    SuperTerminalPaneLayout* out_layout);

WINGUI_API int32_t WINGUI_CALL super_terminal_get_mouse_state(
    SuperTerminalClientContext* ctx,
    WinguiMouseState* out_state);

WINGUI_API int32_t WINGUI_CALL super_terminal_get_native_ui_patch_metrics(
    SuperTerminalClientContext* ctx,
    SuperTerminalNativeUiPatchMetrics* out_metrics);

WINGUI_API int32_t WINGUI_CALL super_terminal_publish_ui_json(
    SuperTerminalClientContext* ctx,
    const char* json_utf8);

WINGUI_API int32_t WINGUI_CALL super_terminal_patch_ui_json(
    SuperTerminalClientContext* ctx,
    const char* patch_json_utf8);

WINGUI_API int32_t WINGUI_CALL super_terminal_set_title_utf8(
    SuperTerminalClientContext* ctx,
    const char* title_utf8);

WINGUI_API int32_t WINGUI_CALL super_terminal_text_grid_write_cells(
    SuperTerminalClientContext* ctx,
    SuperTerminalPaneId pane_id,
    const SuperTerminalTextGridCell* cells,
    uint32_t cell_count);

WINGUI_API int32_t WINGUI_CALL super_terminal_frame_text_grid_write_cells(
    SuperTerminalClientContext* ctx,
    const SuperTerminalFrameTick* tick,
    SuperTerminalPaneId pane_id,
    const SuperTerminalTextGridCell* cells,
    uint32_t cell_count);

WINGUI_API int32_t WINGUI_CALL super_terminal_text_grid_clear_region(
    SuperTerminalClientContext* ctx,
    SuperTerminalPaneId pane_id,
    uint32_t row,
    uint32_t column,
    uint32_t width,
    uint32_t height,
    uint32_t fill_codepoint,
    WinguiGraphicsColour foreground,
    WinguiGraphicsColour background);

WINGUI_API int32_t WINGUI_CALL super_terminal_frame_text_grid_clear_region(
    SuperTerminalClientContext* ctx,
    const SuperTerminalFrameTick* tick,
    SuperTerminalPaneId pane_id,
    uint32_t row,
    uint32_t column,
    uint32_t width,
    uint32_t height,
    uint32_t fill_codepoint,
    WinguiGraphicsColour foreground,
    WinguiGraphicsColour background);

WINGUI_API int32_t WINGUI_CALL super_terminal_frame_indexed_graphics_upload(
    SuperTerminalClientContext* ctx,
    const SuperTerminalFrameTick* tick,
    SuperTerminalPaneId pane_id,
    const SuperTerminalIndexedGraphicsFrame* frame);

WINGUI_API int32_t WINGUI_CALL super_terminal_frame_rgba_upload(
    SuperTerminalClientContext* ctx,
    const SuperTerminalFrameTick* tick,
    SuperTerminalPaneId pane_id,
    const SuperTerminalRgbaFrame* frame);

WINGUI_API int32_t WINGUI_CALL super_terminal_rgba_gpu_copy(
    SuperTerminalClientContext* ctx,
    SuperTerminalPaneId dst_pane_id,
    uint32_t dst_buffer_index,
    uint32_t dst_x,
    uint32_t dst_y,
    SuperTerminalPaneId src_pane_id,
    uint32_t src_buffer_index,
    uint32_t src_x,
    uint32_t src_y,
    uint32_t region_width,
    uint32_t region_height);

WINGUI_API int32_t WINGUI_CALL super_terminal_register_rgba_asset_owned(
    SuperTerminalClientContext* ctx,
    uint32_t width,
    uint32_t height,
    void* bgra8_pixels,
    uint32_t source_pitch,
    SuperTerminalFreeFn free_fn,
    void* free_user_data,
    SuperTerminalAssetId* out_asset_id);

WINGUI_API int32_t WINGUI_CALL super_terminal_asset_blit_to_pane(
    SuperTerminalClientContext* ctx,
    SuperTerminalAssetId asset_id,
    uint32_t src_x,
    uint32_t src_y,
    uint32_t region_width,
    uint32_t region_height,
    SuperTerminalPaneId dst_pane_id,
    uint32_t dst_buffer_index,
    uint32_t dst_x,
    uint32_t dst_y);

WINGUI_API int32_t WINGUI_CALL super_terminal_request_present(
    SuperTerminalClientContext* ctx);

#ifdef __cplusplus
}
#endif