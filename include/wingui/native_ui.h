#pragma once

#include "wingui/wingui.h"

#ifdef __cplusplus
extern "C" {
#endif

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
WINGUI_API int64_t WINGUI_CALL wingui_native_open_url(const char* utf8);
WINGUI_API const char* WINGUI_CALL wingui_native_clipboard_get(void);
WINGUI_API int64_t WINGUI_CALL wingui_native_clipboard_set(const char* utf8);
WINGUI_API const char* WINGUI_CALL wingui_native_choose_open_file(const char* initial_path_utf8);
WINGUI_API const char* WINGUI_CALL wingui_native_choose_save_file(const char* initial_path_utf8);
WINGUI_API const char* WINGUI_CALL wingui_native_choose_folder(const char* initial_title_utf8);

#ifdef __cplusplus
}
#endif