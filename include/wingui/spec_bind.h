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

typedef void (WINGUI_CALL *WinguiSpecBindEventHandlerFn)(
    void* user_data,
    WinguiSpecBindRuntime* runtime,
    const WinguiSpecBindEventView* event_view);

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

WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_runtime_request_stop(
    WinguiSpecBindRuntime* runtime,
    int32_t exit_code);

WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_runtime_get_patch_metrics(
    WinguiSpecBindRuntime* runtime,
    SuperTerminalNativeUiPatchMetrics* out_metrics);

WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_runtime_run(
    WinguiSpecBindRuntime* runtime,
    const WinguiSpecBindRunDesc* desc,
    SuperTerminalRunResult* out_result);

#ifdef __cplusplus
}
#endif