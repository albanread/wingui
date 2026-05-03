#pragma once

#include "wingui/wingui.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct WinguiNativeUiSession WinguiNativeUiSession;

typedef int64_t (WINGUI_CALL *WinguiNativeDispatchEventJsonFn)(const char* event_json_utf8);
typedef int64_t (WINGUI_CALL *WinguiNativeStringCommandFn)(const char* utf8);
typedef const char* (WINGUI_CALL *WinguiNativeClipboardGetFn)(void);
typedef const char* (WINGUI_CALL *WinguiNativeStringDialogFn)(const char* utf8);

typedef enum WinguiNativeCommandType {
    WINGUI_NATIVE_COMMAND_NONE = 0,
    WINGUI_NATIVE_COMMAND_PUBLISH_JSON = 1,
    WINGUI_NATIVE_COMMAND_PATCH_JSON = 2,
    WINGUI_NATIVE_COMMAND_SHOW_HOST = 3,
} WinguiNativeCommandType;

typedef struct WinguiNativeCommand {
    WinguiNativeCommandType type;
    const char* payload_utf8;
} WinguiNativeCommand;

typedef enum WinguiNativeEventType {
    WINGUI_NATIVE_EVENT_NONE = 0,
    WINGUI_NATIVE_EVENT_DISPATCH_JSON = 1,
} WinguiNativeEventType;

typedef struct WinguiNativeEvent {
    WinguiNativeEventType type;
    char* payload_utf8;
    uint32_t payload_size;
    uint64_t sequence;
} WinguiNativeEvent;

typedef struct WinguiNativeCallbacks {
    WinguiNativeDispatchEventJsonFn dispatch_event_json;
    WinguiNativeStringCommandFn open_url;
    WinguiNativeClipboardGetFn clipboard_get;
    WinguiNativeStringCommandFn clipboard_set;
    WinguiNativeStringDialogFn choose_open_file;
    WinguiNativeStringDialogFn choose_save_file;
    WinguiNativeStringDialogFn choose_folder;
} WinguiNativeCallbacks;

typedef struct WinguiNativeEmbeddedHostDesc {
    void* parent_hwnd;
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
    int32_t visible;
} WinguiNativeEmbeddedHostDesc;

typedef struct WinguiNativeEmbeddedSessionDesc {
    WinguiNativeCallbacks callbacks;
    WinguiNativeEmbeddedHostDesc host;
    const char* initial_ui_json_utf8;
} WinguiNativeEmbeddedSessionDesc;

typedef struct WinguiNativeNodeBounds {
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
    int32_t visible;
} WinguiNativeNodeBounds;

typedef struct WinguiNativePatchMetrics {
    uint64_t publish_count;
    uint64_t patch_request_count;
    uint64_t direct_apply_count;
    uint64_t subtree_rebuild_count;
    uint64_t window_rebuild_count;
    uint64_t resize_reject_count;
    uint64_t failed_patch_count;
} WinguiNativePatchMetrics;

WINGUI_API WinguiNativeUiSession* WINGUI_CALL wingui_native_session_create(void);
WINGUI_API void WINGUI_CALL wingui_native_session_destroy(WinguiNativeUiSession* session);
WINGUI_API void WINGUI_CALL wingui_native_session_set_callbacks(
    WinguiNativeUiSession* session,
    const WinguiNativeCallbacks* callbacks);
WINGUI_API int64_t WINGUI_CALL wingui_native_session_available(WinguiNativeUiSession* session);
WINGUI_API const char* WINGUI_CALL wingui_native_session_backend_info(WinguiNativeUiSession* session);
WINGUI_API const char* WINGUI_CALL wingui_native_session_last_error_utf8(WinguiNativeUiSession* session);
WINGUI_API int64_t WINGUI_CALL wingui_native_session_enqueue_command(
    WinguiNativeUiSession* session,
    const WinguiNativeCommand* command);
WINGUI_API uint32_t WINGUI_CALL wingui_native_session_drain_command_queue(
    WinguiNativeUiSession* session,
    uint32_t max_commands);
WINGUI_API int64_t WINGUI_CALL wingui_native_session_poll_event(
    WinguiNativeUiSession* session,
    WinguiNativeEvent* out_event);
WINGUI_API void* WINGUI_CALL wingui_native_session_event_handle(WinguiNativeUiSession* session);
WINGUI_API void WINGUI_CALL wingui_native_session_release_event(
    WinguiNativeUiSession* session,
    WinguiNativeEvent* event);
WINGUI_API int64_t WINGUI_CALL wingui_native_session_publish_json(
    WinguiNativeUiSession* session,
    const char* utf8);
WINGUI_API int64_t WINGUI_CALL wingui_native_session_patch_json(
    WinguiNativeUiSession* session,
    const char* utf8);
WINGUI_API int64_t WINGUI_CALL wingui_native_session_host_run(WinguiNativeUiSession* session);
WINGUI_API int64_t WINGUI_CALL wingui_native_session_begin_embedded(
    WinguiNativeUiSession* session,
    const WinguiNativeEmbeddedSessionDesc* desc);
WINGUI_API int64_t WINGUI_CALL wingui_native_session_attach_embedded_host(
    WinguiNativeUiSession* session,
    const WinguiNativeEmbeddedHostDesc* desc);
WINGUI_API void WINGUI_CALL wingui_native_session_end_embedded(WinguiNativeUiSession* session);
WINGUI_API void WINGUI_CALL wingui_native_session_detach_embedded_host(WinguiNativeUiSession* session);
WINGUI_API int64_t WINGUI_CALL wingui_native_session_set_host_bounds(
    WinguiNativeUiSession* session,
    int32_t x,
    int32_t y,
    int32_t width,
    int32_t height);
WINGUI_API void* WINGUI_CALL wingui_native_session_host_hwnd(WinguiNativeUiSession* session);
WINGUI_API int64_t WINGUI_CALL wingui_native_session_handle_host_command(
    WinguiNativeUiSession* session,
    int32_t command_id);
WINGUI_API int64_t WINGUI_CALL wingui_native_session_get_content_size(
    WinguiNativeUiSession* session,
    int32_t* out_width,
    int32_t* out_height);
WINGUI_API int64_t WINGUI_CALL wingui_native_session_try_get_node_bounds(
    WinguiNativeUiSession* session,
    const char* node_id_utf8,
    WinguiNativeNodeBounds* out_bounds);
WINGUI_API int64_t WINGUI_CALL wingui_native_session_try_get_node_hwnd(
    WinguiNativeUiSession* session,
    const char* node_id_utf8,
    void** out_hwnd);
WINGUI_API int64_t WINGUI_CALL wingui_native_session_try_get_node_type_utf8(
    WinguiNativeUiSession* session,
    const char* node_id_utf8,
    char* buffer_utf8,
    uint32_t buffer_size);
WINGUI_API int64_t WINGUI_CALL wingui_native_session_copy_focused_pane_id_utf8(
    WinguiNativeUiSession* session,
    char* buffer_utf8,
    uint32_t buffer_size);
WINGUI_API int64_t WINGUI_CALL wingui_native_session_get_patch_metrics(
    WinguiNativeUiSession* session,
    WinguiNativePatchMetrics* out_metrics);
WINGUI_API int64_t WINGUI_CALL wingui_native_session_open_url(
    WinguiNativeUiSession* session,
    const char* utf8);
WINGUI_API const char* WINGUI_CALL wingui_native_session_clipboard_get(WinguiNativeUiSession* session);
WINGUI_API int64_t WINGUI_CALL wingui_native_session_clipboard_set(
    WinguiNativeUiSession* session,
    const char* utf8);
WINGUI_API const char* WINGUI_CALL wingui_native_session_choose_open_file(
    WinguiNativeUiSession* session,
    const char* initial_path_utf8);
WINGUI_API const char* WINGUI_CALL wingui_native_session_choose_save_file(
    WinguiNativeUiSession* session,
    const char* initial_path_utf8);
WINGUI_API const char* WINGUI_CALL wingui_native_session_choose_folder(
    WinguiNativeUiSession* session,
    const char* initial_title_utf8);

WINGUI_API void WINGUI_CALL wingui_native_set_callbacks(const WinguiNativeCallbacks* callbacks);
WINGUI_API int64_t WINGUI_CALL wingui_native_available(void);
WINGUI_API const char* WINGUI_CALL wingui_native_backend_info(void);
WINGUI_API const char* WINGUI_CALL wingui_native_last_error_utf8(void);
WINGUI_API int64_t WINGUI_CALL wingui_native_enqueue_command(const WinguiNativeCommand* command);
WINGUI_API uint32_t WINGUI_CALL wingui_native_drain_command_queue(uint32_t max_commands);
WINGUI_API int64_t WINGUI_CALL wingui_native_poll_event(WinguiNativeEvent* out_event);
WINGUI_API void* WINGUI_CALL wingui_native_event_handle(void);
WINGUI_API void WINGUI_CALL wingui_native_release_event(WinguiNativeEvent* event);
WINGUI_API int64_t WINGUI_CALL wingui_native_publish_json(const char* utf8);
WINGUI_API int64_t WINGUI_CALL wingui_native_patch_json(const char* utf8);
WINGUI_API int64_t WINGUI_CALL wingui_native_host_run(void);
WINGUI_API int64_t WINGUI_CALL wingui_native_begin_embedded_session(const WinguiNativeEmbeddedSessionDesc* desc);
WINGUI_API int64_t WINGUI_CALL wingui_native_attach_embedded_host(const WinguiNativeEmbeddedHostDesc* desc);
WINGUI_API void WINGUI_CALL wingui_native_end_embedded_session(void);
WINGUI_API void WINGUI_CALL wingui_native_detach_embedded_host(void);
WINGUI_API int64_t WINGUI_CALL wingui_native_set_host_bounds(int32_t x, int32_t y, int32_t width, int32_t height);
WINGUI_API void* WINGUI_CALL wingui_native_host_hwnd(void);
WINGUI_API int64_t WINGUI_CALL wingui_native_handle_host_command(int32_t command_id);
WINGUI_API int64_t WINGUI_CALL wingui_native_get_content_size(int32_t* out_width, int32_t* out_height);
WINGUI_API int64_t WINGUI_CALL wingui_native_try_get_node_bounds(const char* node_id_utf8, WinguiNativeNodeBounds* out_bounds);
WINGUI_API int64_t WINGUI_CALL wingui_native_try_get_node_hwnd(const char* node_id_utf8, void** out_hwnd);
WINGUI_API int64_t WINGUI_CALL wingui_native_try_get_node_type_utf8(const char* node_id_utf8, char* buffer_utf8, uint32_t buffer_size);
WINGUI_API int64_t WINGUI_CALL wingui_native_copy_focused_pane_id_utf8(char* buffer_utf8, uint32_t buffer_size);
WINGUI_API int64_t WINGUI_CALL wingui_native_get_patch_metrics(WinguiNativePatchMetrics* out_metrics);
WINGUI_API int64_t WINGUI_CALL wingui_native_open_url(const char* utf8);
WINGUI_API const char* WINGUI_CALL wingui_native_clipboard_get(void);
WINGUI_API int64_t WINGUI_CALL wingui_native_clipboard_set(const char* utf8);
WINGUI_API const char* WINGUI_CALL wingui_native_choose_open_file(const char* initial_path_utf8);
WINGUI_API const char* WINGUI_CALL wingui_native_choose_save_file(const char* initial_path_utf8);
WINGUI_API const char* WINGUI_CALL wingui_native_choose_folder(const char* initial_title_utf8);

#ifdef __cplusplus
}
#endif