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

typedef struct SuperTerminalWindowId {
    uint64_t value;
} SuperTerminalWindowId;

typedef struct SuperTerminalAssetId {
    uint64_t value;
} SuperTerminalAssetId;

typedef struct SuperTerminalWindowDesc {
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
} SuperTerminalWindowDesc;

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

typedef struct SuperTerminalChildViewBounds {
    float x0;
    float y0;
    float x1;
    float y1;
} SuperTerminalChildViewBounds;

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
    SUPERTERMINAL_CMD_CREATE_WINDOW = 1,
    SUPERTERMINAL_CMD_CLOSE_WINDOW = 2,
    SUPERTERMINAL_CMD_NATIVE_UI_PUBLISH = 3,
    SUPERTERMINAL_CMD_NATIVE_UI_PATCH = 4,
    SUPERTERMINAL_CMD_WINDOW_SET_TITLE = 5,
    SUPERTERMINAL_CMD_TEXT_GRID_WRITE_CELLS = 6,
    SUPERTERMINAL_CMD_TEXT_GRID_CLEAR_REGION = 7,
    SUPERTERMINAL_CMD_REQUEST_PRESENT = 8,
    SUPERTERMINAL_CMD_REQUEST_CLOSE = 9,
    SUPERTERMINAL_CMD_RGBA_UPLOAD_OWNED = 10,
    SUPERTERMINAL_CMD_FRAME_SWAP = 11,
    SUPERTERMINAL_CMD_RGBA_GPU_COPY = 12,
    SUPERTERMINAL_CMD_RGBA_ASSET_REGISTER_OWNED = 13,
    SUPERTERMINAL_CMD_RGBA_ASSET_BLIT_TO_PANE = 14,
    SUPERTERMINAL_CMD_INDEXED_UPLOAD_OWNED = 15,
    SUPERTERMINAL_CMD_SPRITE_DEFINE_OWNED = 16,
    SUPERTERMINAL_CMD_SPRITE_RENDER = 17,
    SUPERTERMINAL_CMD_VECTOR_DRAW_OWNED = 18,
    SUPERTERMINAL_CMD_INDEXED_FILL_RECT = 19,
    SUPERTERMINAL_CMD_INDEXED_DRAW_LINE = 20,
    SUPERTERMINAL_CMD_SURFACE_DRAW_TEXT_OWNED = 21,
    SUPERTERMINAL_CMD_SURFACE_DRAW_PRIMITIVES_OWNED = 22,
    SUPERTERMINAL_CMD_SURFACE_PUSH_CLIP_RECT = 23,
    SUPERTERMINAL_CMD_SURFACE_POP_CLIP_RECT = 24,
    SUPERTERMINAL_CMD_SURFACE_PUSH_OFFSET = 25,
    SUPERTERMINAL_CMD_SURFACE_POP_OFFSET = 26,
    SUPERTERMINAL_CMD_SURFACE_RESET_COMPOSITION = 27,
    SUPERTERMINAL_CMD_SURFACE_INSTALL_CHILD_VIEW_BOUNDS = 28,
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
    SUPERTERMINAL_EVENT_WINDOW_CREATED = 10,
    SUPERTERMINAL_EVENT_WINDOW_CLOSED = 11,
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
    SuperTerminalWindowId window_id;
    const char* json_utf8;
} SuperTerminalNativeUiPublish;

typedef struct SuperTerminalNativeUiPatch {
    SuperTerminalWindowId window_id;
    const char* patch_json_utf8;
} SuperTerminalNativeUiPatch;

typedef struct SuperTerminalSetTitle {
    SuperTerminalWindowId window_id;
    char title_utf8[256];
} SuperTerminalSetTitle;

typedef struct SuperTerminalCreateWindow {
    SuperTerminalWindowId window_id;
    SuperTerminalWindowDesc desc;
} SuperTerminalCreateWindow;

typedef struct SuperTerminalCloseWindow {
    SuperTerminalWindowId window_id;
} SuperTerminalCloseWindow;

typedef struct SuperTerminalTextGridCell {
    uint32_t row;
    uint32_t column;
    uint32_t codepoint;
    WinguiGraphicsColour foreground;
    WinguiGraphicsColour background;
} SuperTerminalTextGridCell;

typedef struct SuperTerminalTextGridWriteCells {
    SuperTerminalWindowId window_id;
    SuperTerminalPaneId pane_id;
    const SuperTerminalTextGridCell* cells;
    uint32_t cell_count;
} SuperTerminalTextGridWriteCells;

typedef struct SuperTerminalTextGridClearRegion {
    SuperTerminalWindowId window_id;
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
    SuperTerminalWindowId window_id;
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

typedef enum SuperTerminalRgbaContentBufferMode {
    SUPERTERMINAL_RGBA_CONTENT_BUFFER_FRAME = 0,
    SUPERTERMINAL_RGBA_CONTENT_BUFFER_PERSISTENT = 1,
} SuperTerminalRgbaContentBufferMode;

typedef struct SuperTerminalRgbaGpuCopy {
    SuperTerminalWindowId window_id;
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
    uint32_t content_buffer_mode; /* SuperTerminalRgbaContentBufferMode */
} SuperTerminalRgbaGpuCopy;

typedef struct SuperTerminalRgbaAssetRegisterOwned {
    SuperTerminalWindowId window_id;
    SuperTerminalAssetId asset_id;
    uint32_t width;
    uint32_t height;
    uint32_t source_pitch;
    void* bgra8_pixels;
    SuperTerminalFreeFn free_fn;
    void* free_user_data;
} SuperTerminalRgbaAssetRegisterOwned;

typedef struct SuperTerminalRgbaAssetBlitToPane {
    SuperTerminalWindowId window_id;
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
    SuperTerminalWindowId window_id;
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

/* Unique id for a sprite registered in a pane's sprite bank.
   Values are client-chosen; 0 is reserved/invalid. */
typedef struct SuperTerminalSpriteId {
    uint32_t value;
} SuperTerminalSpriteId;

/* Client-facing sprite instance: references bank id rather than atlas id. */
typedef struct SuperTerminalSpriteInstance {
    SuperTerminalSpriteId sprite_id;
    float x;
    float y;
    float rotation;
    float scale_x;
    float scale_y;
    float anchor_x;       /* 0..1 fraction of frame width  */
    float anchor_y;       /* 0..1 fraction of frame height */
    float alpha;
    uint32_t flags;       /* WINGUI_SPRITE_FLAG_* */
    uint32_t effect_type;
    float effect_param1;
    float effect_param2;
    uint8_t effect_colour[4];
    uint32_t palette_override; /* 0 = use sprite's own palette; 1-N = override */
} SuperTerminalSpriteInstance;

typedef struct SuperTerminalSpriteDefineOwned {
    SuperTerminalWindowId window_id;
    SuperTerminalPaneId pane_id;
    SuperTerminalSpriteId sprite_id;
    uint32_t frame_w;             /* width of one frame in pixels            */
    uint32_t frame_h;             /* height of one frame in pixels           */
    uint32_t frame_count;         /* frames laid out left-to-right in strip  */
    uint32_t frames_per_tick;     /* animation speed: advance 1 frame every N ticks (0 = static) */
    void*    pixels;              /* owned R8_UINT strip: frame_w*frame_count wide x frame_h tall */
    void*    palette;             /* owned WinguiGraphicsLinePalette (16 colours, colour 0 transparent) */
    SuperTerminalFreeFn free_fn;
    void*    free_user_data;
} SuperTerminalSpriteDefineOwned;

typedef struct SuperTerminalSpriteRender {
    SuperTerminalWindowId window_id;
    SuperTerminalPaneId pane_id;
    uint32_t target_width;        /* screen-space pixel width  (0 = use pane layout width)  */
    uint32_t target_height;       /* screen-space pixel height (0 = use pane layout height) */
    uint64_t sprite_tick;         /* animation clock; typically host->frame_index */
    void*    instances;           /* owned SuperTerminalSpriteInstance array */
    uint32_t instance_count;
    SuperTerminalFreeFn free_fn;
    void*    free_user_data;
} SuperTerminalSpriteRender;

typedef struct SuperTerminalVectorDrawOwned {
    SuperTerminalWindowId window_id;
    SuperTerminalPaneId pane_id;
    uint32_t buffer_index;        /* RGBA surface buffer to render into */
    uint32_t content_buffer_mode; /* SuperTerminalRgbaContentBufferMode */
    uint32_t blend_mode;          /* WINGUI_RGBA_BLIT_OPAQUE or WINGUI_RGBA_BLIT_ALPHA_OVER */
    int32_t  clear_before;        /* if non-zero, ClearRenderTargetView with clear_color first */
    float    clear_color[4];      /* RGBA, used when clear_before is set */
    void*    primitives;          /* owned WinguiVectorPrimitive[primitive_count] */
    uint32_t primitive_count;
    SuperTerminalFreeFn free_fn;
    void*    free_user_data;
} SuperTerminalVectorDrawOwned;

typedef struct SuperTerminalIndexedFillRect {
    SuperTerminalWindowId window_id;
    SuperTerminalPaneId pane_id;
    uint32_t buffer_index;   /* indexed surface buffer */
    uint32_t x;              /* destination rect */
    uint32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t palette_index;  /* 0-255 */
} SuperTerminalIndexedFillRect;

typedef struct SuperTerminalIndexedDrawLine {
    SuperTerminalWindowId window_id;
    SuperTerminalPaneId pane_id;
    uint32_t buffer_index;
    int32_t  x0;
    int32_t  y0;
    int32_t  x1;
    int32_t  y1;
    uint32_t palette_index;  /* 0-255 */
} SuperTerminalIndexedDrawLine;

typedef struct SuperTerminalSurfaceDrawTextOwned {
    SuperTerminalWindowId window_id;
    SuperTerminalPaneId pane_id;
    uint32_t buffer_index;
    uint32_t content_buffer_mode; /* SuperTerminalRgbaContentBufferMode */
    uint32_t blend_mode;          /* WINGUI_RGBA_BLIT_OPAQUE or WINGUI_RGBA_BLIT_ALPHA_OVER */
    int32_t clear_before;
    float clear_color[4];
    float origin_x;
    float origin_y;
    float color[4];
    void* text_utf8;              /* owned UTF-8 string */
    SuperTerminalFreeFn free_fn;
    void* free_user_data;
} SuperTerminalSurfaceDrawTextOwned;

typedef struct SuperTerminalSurfaceDrawPrimitivesOwned {
    SuperTerminalWindowId window_id;
    SuperTerminalPaneId pane_id;
    uint32_t buffer_index;
    uint32_t content_buffer_mode; /* SuperTerminalRgbaContentBufferMode */
    uint32_t blend_mode;
    int32_t clear_before;
    float clear_color[4];
    void* primitives;             /* owned WinguiSurfacePrimitive[primitive_count] */
    uint32_t primitive_count;
    void* path_points_xy;         /* owned float[path_point_count * 2] */
    uint32_t path_point_count;
    SuperTerminalFreeFn free_fn;
    void* free_user_data;
} SuperTerminalSurfaceDrawPrimitivesOwned;

typedef struct SuperTerminalSurfacePushClipRect {
    SuperTerminalWindowId window_id;
    SuperTerminalPaneId pane_id;
    float x0;
    float y0;
    float x1;
    float y1;
} SuperTerminalSurfacePushClipRect;

typedef struct SuperTerminalSurfacePopClipRect {
    SuperTerminalWindowId window_id;
    SuperTerminalPaneId pane_id;
} SuperTerminalSurfacePopClipRect;

typedef struct SuperTerminalSurfacePushOffset {
    SuperTerminalWindowId window_id;
    SuperTerminalPaneId pane_id;
    float dx;
    float dy;
} SuperTerminalSurfacePushOffset;

typedef struct SuperTerminalSurfacePopOffset {
    SuperTerminalWindowId window_id;
    SuperTerminalPaneId pane_id;
} SuperTerminalSurfacePopOffset;

typedef struct SuperTerminalSurfaceResetComposition {
    SuperTerminalWindowId window_id;
    SuperTerminalPaneId pane_id;
} SuperTerminalSurfaceResetComposition;

typedef struct SuperTerminalSurfaceInstallChildViewBounds {
    SuperTerminalWindowId window_id;
    SuperTerminalPaneId pane_id;
    int32_t child_id;
    float x0;
    float y0;
    float x1;
    float y1;
} SuperTerminalSurfaceInstallChildViewBounds;

typedef struct SuperTerminalCommand {
    SuperTerminalCommandType type;
    uint32_t sequence;
    union {
        SuperTerminalCreateWindow create_window;
        SuperTerminalCloseWindow close_window;
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
        SuperTerminalSpriteDefineOwned sprite_define_owned;
        SuperTerminalSpriteRender sprite_render;
        SuperTerminalVectorDrawOwned vector_draw_owned;
        SuperTerminalIndexedFillRect indexed_fill_rect;
        SuperTerminalIndexedDrawLine  indexed_draw_line;
        SuperTerminalSurfaceDrawTextOwned surface_draw_text_owned;
        SuperTerminalSurfaceDrawPrimitivesOwned surface_draw_primitives_owned;
        SuperTerminalSurfacePushClipRect surface_push_clip_rect;
        SuperTerminalSurfacePopClipRect surface_pop_clip_rect;
        SuperTerminalSurfacePushOffset surface_push_offset;
        SuperTerminalSurfacePopOffset surface_pop_offset;
        SuperTerminalSurfaceResetComposition surface_reset_composition;
        SuperTerminalSurfaceInstallChildViewBounds surface_install_child_view_bounds;
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
    SuperTerminalWindowId window_id;
    uint32_t pixel_width;
    uint32_t pixel_height;
    uint32_t columns;
    uint32_t rows;
    float dpi_scale;
    float cell_width;
    float cell_height;
} SuperTerminalResizeEvent;

typedef struct SuperTerminalFocusEvent {
    SuperTerminalWindowId window_id;
    int32_t focused;
} SuperTerminalFocusEvent;

typedef struct SuperTerminalNativeUiEvent {
    SuperTerminalWindowId window_id;
    char payload_json_utf8[512];
} SuperTerminalNativeUiEvent;

typedef struct SuperTerminalWindowCreatedEvent {
    SuperTerminalWindowId window_id;
} SuperTerminalWindowCreatedEvent;

typedef struct SuperTerminalWindowClosedEvent {
    SuperTerminalWindowId window_id;
} SuperTerminalWindowClosedEvent;

typedef struct SuperTerminalHostStoppingEvent {
    int32_t exit_code;
} SuperTerminalHostStoppingEvent;

typedef struct SuperTerminalEvent {
    SuperTerminalWindowId window_id;
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
        SuperTerminalWindowCreatedEvent window_created;
        SuperTerminalWindowClosedEvent window_closed;
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
    SuperTerminalWindowId window_id;
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

WINGUI_API int32_t WINGUI_CALL super_terminal_create_window(
    SuperTerminalClientContext* ctx,
    const SuperTerminalWindowDesc* desc,
    SuperTerminalWindowId* out_window_id);

WINGUI_API int32_t WINGUI_CALL super_terminal_close_window(
    SuperTerminalClientContext* ctx,
    SuperTerminalWindowId window_id);

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

WINGUI_API int32_t WINGUI_CALL super_terminal_resolve_pane_id_for_window(
    SuperTerminalClientContext* ctx,
    SuperTerminalWindowId window_id,
    const char* node_id_utf8,
    SuperTerminalPaneId* out_pane_id);

WINGUI_API int32_t WINGUI_CALL super_terminal_get_pane_layout(
    SuperTerminalClientContext* ctx,
    SuperTerminalPaneId pane_id,
    SuperTerminalPaneLayout* out_layout);

WINGUI_API int32_t WINGUI_CALL super_terminal_get_pane_layout_for_window(
    SuperTerminalClientContext* ctx,
    SuperTerminalWindowId window_id,
    SuperTerminalPaneId pane_id,
    SuperTerminalPaneLayout* out_layout);

WINGUI_API int32_t WINGUI_CALL super_terminal_get_surface_child_view_bounds(
    SuperTerminalClientContext* ctx,
    SuperTerminalPaneId pane_id,
    int32_t child_id,
    SuperTerminalChildViewBounds* out_bounds);

WINGUI_API int32_t WINGUI_CALL super_terminal_get_surface_child_view_bounds_for_window(
    SuperTerminalClientContext* ctx,
    SuperTerminalWindowId window_id,
    SuperTerminalPaneId pane_id,
    int32_t child_id,
    SuperTerminalChildViewBounds* out_bounds);

WINGUI_API int32_t WINGUI_CALL super_terminal_get_mouse_state(
    SuperTerminalClientContext* ctx,
    WinguiMouseState* out_state);

WINGUI_API int32_t WINGUI_CALL super_terminal_get_text_config(
    SuperTerminalClientContext* ctx,
    const char** out_font_family_utf8,
    int32_t* out_font_pixel_height,
    float* out_dpi_scale);

WINGUI_API int32_t WINGUI_CALL super_terminal_get_native_ui_patch_metrics(
    SuperTerminalClientContext* ctx,
    SuperTerminalNativeUiPatchMetrics* out_metrics);

WINGUI_API int32_t WINGUI_CALL super_terminal_publish_ui_json(
    SuperTerminalClientContext* ctx,
    const char* json_utf8);

WINGUI_API int32_t WINGUI_CALL super_terminal_publish_ui_json_for_window(
    SuperTerminalClientContext* ctx,
    SuperTerminalWindowId window_id,
    const char* json_utf8);

WINGUI_API int32_t WINGUI_CALL super_terminal_patch_ui_json(
    SuperTerminalClientContext* ctx,
    const char* patch_json_utf8);

WINGUI_API int32_t WINGUI_CALL super_terminal_patch_ui_json_for_window(
    SuperTerminalClientContext* ctx,
    SuperTerminalWindowId window_id,
    const char* patch_json_utf8);

WINGUI_API int32_t WINGUI_CALL super_terminal_set_title_utf8(
    SuperTerminalClientContext* ctx,
    const char* title_utf8);

WINGUI_API int32_t WINGUI_CALL super_terminal_set_title_for_window(
    SuperTerminalClientContext* ctx,
    SuperTerminalWindowId window_id,
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

/* Define (or redefine) a sprite in a pane's sprite bank.
   pixels: R8_UINT strip (frame_w*frame_count wide, frame_h tall); owned, freed after upload.
   palette: WinguiGraphicsLinePalette (16 colours); colour index 0 is transparent; owned, freed after upload.
   frames_per_tick: 0 means static (always frame 0). */
WINGUI_API int32_t WINGUI_CALL super_terminal_define_sprite(
    SuperTerminalClientContext* ctx,
    SuperTerminalPaneId pane_id,
    SuperTerminalSpriteId sprite_id,
    uint32_t frame_w,
    uint32_t frame_h,
    uint32_t frame_count,
    uint32_t frames_per_tick,
    void* pixels,
    void* palette,
    SuperTerminalFreeFn free_fn,
    void* free_user_data);

/* Submit a list of sprite instances for rendering on the next frame.
   instances: owned array of SuperTerminalSpriteInstance, freed after the command is consumed.
   sprite_tick: animation clock; pass tick->frame_index from on_frame for automatic animation.
   target_width/height: 0 = use pane layout dimensions. */
WINGUI_API int32_t WINGUI_CALL super_terminal_render_sprites(
    SuperTerminalClientContext* ctx,
    SuperTerminalPaneId pane_id,
    uint64_t sprite_tick,
    uint32_t target_width,
    uint32_t target_height,
    const SuperTerminalSpriteInstance* instances,
    uint32_t instance_count);

/* Retrieve the host's text glyph atlas info. Useful for client-side text
   layout via wingui_text_layout_with_atlas_info_utf8. */
WINGUI_API int32_t WINGUI_CALL super_terminal_get_glyph_atlas_info(
    SuperTerminalClientContext* ctx,
    WinguiGlyphAtlasInfo* out_info);

/* Submit a list of GPU vector primitives (rects, lines, circles, arcs, glyphs)
   for rendering into a buffer of an RGBA pane. The primitives array is copied
   immediately and the copy is freed after the command is consumed.
   blend_mode: WINGUI_RGBA_BLIT_OPAQUE replaces the destination, ALPHA_OVER composites.
    content_buffer_mode: FRAME means the pane content follows the host's frame buffer,
    PERSISTENT means draw into one persistent canvas buffer across frames.
    If clear_before is non-zero the buffer is cleared to clear_color first. */
WINGUI_API int32_t WINGUI_CALL super_terminal_vector_draw(
    SuperTerminalClientContext* ctx,
    SuperTerminalPaneId pane_id,
    uint32_t buffer_index,
     uint32_t content_buffer_mode,
    uint32_t blend_mode,
    int32_t clear_before,
    const float clear_color_rgba[4],
    const WinguiVectorPrimitive* primitives,
    uint32_t primitive_count);

WINGUI_API int32_t WINGUI_CALL super_terminal_surface_draw_text_utf8(
    SuperTerminalClientContext* ctx,
    SuperTerminalPaneId pane_id,
    uint32_t buffer_index,
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

WINGUI_API int32_t WINGUI_CALL super_terminal_surface_draw_primitives(
    SuperTerminalClientContext* ctx,
    SuperTerminalPaneId pane_id,
    uint32_t buffer_index,
    uint32_t content_buffer_mode,
    uint32_t blend_mode,
    int32_t clear_before,
    const float clear_color_rgba[4],
    const WinguiSurfacePrimitive* primitives,
    uint32_t primitive_count,
    const float* path_points_xy,
    uint32_t path_point_count);

WINGUI_API int32_t WINGUI_CALL super_terminal_surface_push_clip_rect(
    SuperTerminalClientContext* ctx,
    SuperTerminalPaneId pane_id,
    float x0,
    float y0,
    float x1,
    float y1);

WINGUI_API int32_t WINGUI_CALL super_terminal_surface_pop_clip_rect(
    SuperTerminalClientContext* ctx,
    SuperTerminalPaneId pane_id);

WINGUI_API int32_t WINGUI_CALL super_terminal_surface_push_offset(
    SuperTerminalClientContext* ctx,
    SuperTerminalPaneId pane_id,
    float dx,
    float dy);

WINGUI_API int32_t WINGUI_CALL super_terminal_surface_pop_offset(
    SuperTerminalClientContext* ctx,
    SuperTerminalPaneId pane_id);

WINGUI_API int32_t WINGUI_CALL super_terminal_surface_reset_composition(
    SuperTerminalClientContext* ctx,
    SuperTerminalPaneId pane_id);

WINGUI_API int32_t WINGUI_CALL super_terminal_surface_install_child_view_bounds(
    SuperTerminalClientContext* ctx,
    SuperTerminalPaneId pane_id,
    int32_t child_id,
    float x0,
    float y0,
    float x1,
    float y1);

/* Fill a rectangle in an indexed-colour pane buffer with a single palette index.
   Uses a compute shader; does not stall the CPU.
   palette_index 0 = transparent in the renderer. */
WINGUI_API int32_t WINGUI_CALL super_terminal_indexed_fill_rect(
    SuperTerminalClientContext* ctx,
    SuperTerminalPaneId pane_id,
    uint32_t buffer_index,
    uint32_t x,
    uint32_t y,
    uint32_t width,
    uint32_t height,
    uint32_t palette_index);

/* Draw a 1-pixel-wide line in an indexed-colour pane buffer. */
WINGUI_API int32_t WINGUI_CALL super_terminal_indexed_draw_line(
    SuperTerminalClientContext* ctx,
    SuperTerminalPaneId pane_id,
    uint32_t buffer_index,
    int32_t x0,
    int32_t y0,
    int32_t x1,
    int32_t y1,
    uint32_t palette_index);

#ifdef __cplusplus
}
#endif