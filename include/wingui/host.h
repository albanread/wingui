#pragma once

#include <stdint.h>

#include "wingui.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct WinguiHostClientContext WinguiHostClientContext;

enum {
    WINGUI_HOST_WAIT_INFINITE = 0xffffffffu,
};

enum {
    WINGUI_HOST_APP_ENABLE_AUDIO = 1u << 0,
};

typedef enum WinguiHostHostErrorCode {
    WINGUI_HOST_HOST_ERROR_NONE = 0,
    WINGUI_HOST_HOST_ERROR_INVALID_ARGUMENT = 1,
    WINGUI_HOST_HOST_ERROR_WINDOW_CREATE = 2,
    WINGUI_HOST_HOST_ERROR_CONTEXT_CREATE = 3,
    WINGUI_HOST_HOST_ERROR_GLYPH_ATLAS_CREATE = 4,
    WINGUI_HOST_HOST_ERROR_RENDERER_CREATE = 5,
    WINGUI_HOST_HOST_ERROR_CLIENT_START = 6,
    WINGUI_HOST_HOST_ERROR_MESSAGE_LOOP = 7,
} WinguiHostHostErrorCode;

typedef enum WinguiHostCommandType {
    WINGUI_HOST_CMD_NOP = 0,
    WINGUI_HOST_CMD_UI_PUBLISH_JSON = 1,
    WINGUI_HOST_CMD_UI_PATCH_JSON = 2,
    WINGUI_HOST_CMD_UPDATE_TEXT_GRID = 3,
    WINGUI_HOST_CMD_UPDATE_INDEXED_GRAPHICS = 4,
    WINGUI_HOST_CMD_UPDATE_RGBA_PANE = 5,
    WINGUI_HOST_CMD_SET_TITLE = 6,
    WINGUI_HOST_CMD_REQUEST_PRESENT = 7,
    WINGUI_HOST_CMD_REQUEST_CLOSE = 8,
} WinguiHostCommandType;

typedef enum WinguiHostEventType {
    WINGUI_HOST_EVENT_NONE = 0,
    WINGUI_HOST_EVENT_KEY = 1,
    WINGUI_HOST_EVENT_CHAR = 2,
    WINGUI_HOST_EVENT_MOUSE = 3,
    WINGUI_HOST_EVENT_RESIZE = 4,
    WINGUI_HOST_EVENT_FOCUS = 5,
    WINGUI_HOST_EVENT_CLOSE_REQUESTED = 6,
    WINGUI_HOST_EVENT_HOST_STOPPING = 7,
} WinguiHostEventType;

typedef enum WinguiHostMouseEventKind {
    WINGUI_HOST_MOUSE_MOVE = 0,
    WINGUI_HOST_MOUSE_BUTTON_DOWN = 1,
    WINGUI_HOST_MOUSE_BUTTON_UP = 2,
    WINGUI_HOST_MOUSE_WHEEL = 3,
} WinguiHostMouseEventKind;

typedef struct WinguiHostUiPublish {
    const char* json_utf8;
} WinguiHostUiPublish;

typedef struct WinguiHostUiPatch {
    const char* json_patch_utf8;
} WinguiHostUiPatch;

typedef struct WinguiHostUpdateTextGrid {
    const WinguiTextGridFrame* frame;
} WinguiHostUpdateTextGrid;

typedef struct WinguiHostUpdateIndexedGraphics {
    const WinguiIndexedGraphicsFrame* frame;
    const WinguiSpriteAtlasEntry* atlas_entries;
    uint32_t atlas_entry_count;
    const WinguiSpriteInstance* instances;
    uint32_t instance_count;
} WinguiHostUpdateIndexedGraphics;

typedef struct WinguiHostUpdateRgbaPane {
    uint32_t buffer_index;
} WinguiHostUpdateRgbaPane;

typedef struct WinguiHostSetTitle {
    char title_utf8[256];
} WinguiHostSetTitle;

typedef struct WinguiHostCommand {
    WinguiHostCommandType type;
    uint32_t sequence;
    union {
        WinguiHostUiPublish ui_publish;
        WinguiHostUiPatch ui_patch;
        WinguiHostUpdateTextGrid text_grid_update;
        WinguiHostUpdateIndexedGraphics graphics_update;
        WinguiHostUpdateRgbaPane rgba_update;
        WinguiHostSetTitle set_title;
    } data;
} WinguiHostCommand;

typedef struct WinguiHostKeyEvent {
    uint32_t virtual_key;
    uint32_t repeat_count;
    int32_t is_down;
    uint32_t modifiers;
} WinguiHostKeyEvent;

typedef struct WinguiHostCharEvent {
    uint32_t codepoint;
} WinguiHostCharEvent;

typedef struct WinguiHostMouseEvent {
    uint32_t kind;
    int32_t x;
    int32_t y;
    int32_t wheel_delta;
    uint32_t buttons;
    uint32_t button_mask;
} WinguiHostMouseEvent;

typedef struct WinguiHostResizeEvent {
    uint32_t pixel_width;
    uint32_t pixel_height;
    uint32_t columns;
    uint32_t rows;
    float dpi_scale;
    float cell_width;
    float cell_height;
} WinguiHostResizeEvent;

typedef struct WinguiHostFocusEvent {
    int32_t focused;
} WinguiHostFocusEvent;

typedef struct WinguiHostHostStoppingEvent {
    int32_t exit_code;
} WinguiHostHostStoppingEvent;

typedef struct WinguiHostEvent {
    WinguiHostEventType type;
    uint32_t sequence;
    union {
        WinguiHostKeyEvent key;
        WinguiHostCharEvent character;
        WinguiHostMouseEvent mouse;
        WinguiHostResizeEvent resize;
        WinguiHostFocusEvent focus;
        WinguiHostHostStoppingEvent host_stopping;
    } data;
} WinguiHostEvent;

typedef struct WinguiHostAppDesc {
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
    void* user_data;
    int32_t (WINGUI_CALL *startup)(WinguiHostClientContext* ctx, void* user_data);
    void (WINGUI_CALL *shutdown)(void* user_data);
} WinguiHostAppDesc;

typedef struct WinguiHostRunResult {
    int32_t exit_code;
    int32_t host_error_code;
    char message_utf8[256];
} WinguiHostRunResult;

WINGUI_API int32_t WINGUI_CALL WINGUI_HOST_run(
    const WinguiHostAppDesc* desc,
    WinguiHostRunResult* out_result);

WINGUI_API int32_t WINGUI_CALL WINGUI_HOST_enqueue(
    WinguiHostClientContext* ctx,
    const WinguiHostCommand* command);

WINGUI_API int32_t WINGUI_CALL WINGUI_HOST_wait_event(
    WinguiHostClientContext* ctx,
    uint32_t timeout_ms,
    WinguiHostEvent* out_event);

WINGUI_API void* WINGUI_CALL WINGUI_HOST_event_handle(
    WinguiHostClientContext* ctx);

WINGUI_API int32_t WINGUI_CALL WINGUI_HOST_request_stop(
    WinguiHostClientContext* ctx,
    int32_t exit_code);

WINGUI_API int32_t WINGUI_CALL WINGUI_HOST_get_key_state(
    WinguiHostClientContext* ctx,
    uint32_t virtual_key);

WINGUI_API int32_t WINGUI_CALL WINGUI_HOST_get_mouse_state(
    WinguiHostClientContext* ctx,
    WinguiMouseState* out_state);

WINGUI_API int32_t WINGUI_CALL wingui_host_publish_ui_json(
    WinguiHostClientContext* ctx,
    const char* json_utf8);

WINGUI_API int32_t WINGUI_CALL wingui_host_patch_ui_json(
    WinguiHostClientContext* ctx,
    const char* json_patch_utf8);

WINGUI_API int32_t WINGUI_CALL wingui_host_update_text_grid(
    WinguiHostClientContext* ctx,
    const WinguiTextGridFrame* frame);

WINGUI_API int32_t WINGUI_CALL wingui_host_update_indexed_graphics(
    WinguiHostClientContext* ctx,
    const WinguiIndexedGraphicsFrame* frame,
    const WinguiSpriteAtlasEntry* atlas_entries,
    uint32_t atlas_entry_count,
    const WinguiSpriteInstance* instances,
    uint32_t instance_count);

WINGUI_API int32_t WINGUI_CALL wingui_host_update_rgba_pane(
    WinguiHostClientContext* ctx,
    uint32_t buffer_index);

WINGUI_API int32_t WINGUI_CALL WINGUI_HOST_set_title_utf8(
    WinguiHostClientContext* ctx,
    const char* title_utf8);

WINGUI_API int32_t WINGUI_CALL WINGUI_HOST_present(
    WinguiHostClientContext* ctx);

#ifdef __cplusplus
}
#endif
