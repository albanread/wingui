#ifndef WINGUI_BUILD_DLL
#define WINGUI_BUILD_DLL
#endif

#include "wingui/spec_bind.h"

#include "wingui/ui_model.h"

#include "wingui_internal.h"
#include "nlohmann/json.hpp"

#include <algorithm>
#include <cstring>
#include <mutex>
#include <new>
#include <string>
#include <utility>
#include <vector>

using Json = nlohmann::ordered_json;

struct SpecBindBinding {
    std::string event_name;
    WinguiSpecBindEventHandlerFn handler = nullptr;
    void* user_data = nullptr;
};

struct WinguiSpecBindRuntime {
    std::mutex mutex;
    std::string spec_json;
    std::vector<SpecBindBinding> bindings;
    WinguiSpecBindEventHandlerFn default_handler = nullptr;
    void* default_user_data = nullptr;
    SuperTerminalClientContext* active_context = nullptr;
    bool running = false;
};

namespace {

constexpr const char* kCloseRequestedEventName = "__close_requested";
constexpr const char* kHostStoppingEventName = "__host_stopping";
constexpr const char* kCloseRequestedPayload = "{\"event\":\"__close_requested\",\"type\":\"close-requested\"}";

struct BoundHandler {
    WinguiSpecBindEventHandlerFn handler = nullptr;
    void* user_data = nullptr;
};

WinguiSpecBindRunDesc defaultRunDesc() {
    WinguiSpecBindRunDesc desc{};
    desc.title_utf8 = "Wingui Spec + Bind";
    desc.columns = 120;
    desc.rows = 40;
    desc.command_queue_capacity = 1024;
    desc.event_queue_capacity = 1024;
    desc.font_pixel_height = 18;
    desc.dpi_scale = 1.0f;
    desc.target_frame_ms = 16;
    desc.auto_request_present = 0;
    return desc;
}

bool validateJsonSpec(const char* json_utf8, std::string* out_copy) {
    if (!json_utf8 || !json_utf8[0]) {
        wingui_set_last_error_string_internal("wingui_spec_bind_runtime_load_spec_json: json_utf8 was empty");
        return false;
    }

    auto parsed = Json::parse(json_utf8, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        wingui_set_last_error_string_internal("wingui_spec_bind_runtime_load_spec_json: invalid JSON object spec");
        return false;
    }

    if (out_copy) {
        *out_copy = json_utf8;
    }
    return true;
}

int32_t publishOrPatchSpec(
    SuperTerminalClientContext* ctx,
    const std::string& previous_spec_json,
    const std::string& next_spec_json) {
    if (previous_spec_json.empty()) {
        return super_terminal_publish_ui_json(ctx, next_spec_json.c_str());
    }

    const Json previous_spec = Json::parse(previous_spec_json, nullptr, false);
    const Json next_spec = Json::parse(next_spec_json, nullptr, false);
    if (previous_spec.is_discarded() || next_spec.is_discarded()) {
        return super_terminal_publish_ui_json(ctx, next_spec_json.c_str());
    }

    const auto patch_ops = wingui::ui_model_diff(previous_spec, next_spec);
    if (!patch_ops.has_value()) {
        return super_terminal_publish_ui_json(ctx, next_spec_json.c_str());
    }
    if (patch_ops->empty()) {
        return 1;
    }

    const std::string patch_json = wingui::ui_patch_ops_to_json(*patch_ops).dump();
    return super_terminal_patch_ui_json(ctx, patch_json.c_str());
}

std::pair<std::string, std::string> parseNativeUiEventFields(const char* payload_json_utf8) {
    if (!payload_json_utf8 || !payload_json_utf8[0]) {
        return {};
    }

    auto parsed = Json::parse(payload_json_utf8, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        return {};
    }

    std::string event_name;
    std::string source_name;
    if (parsed.contains("event") && parsed["event"].is_string()) {
        event_name = parsed["event"].get<std::string>();
    }
    if (parsed.contains("source") && parsed["source"].is_string()) {
        source_name = parsed["source"].get<std::string>();
    }
    return { std::move(event_name), std::move(source_name) };
}

BoundHandler findBoundHandler(WinguiSpecBindRuntime* runtime, const char* event_name_utf8) {
    std::lock_guard<std::mutex> lock(runtime->mutex);
    if (event_name_utf8 && event_name_utf8[0]) {
        const auto it = std::find_if(runtime->bindings.begin(), runtime->bindings.end(),
            [&](const SpecBindBinding& binding) {
                return binding.event_name == event_name_utf8;
            });
        if (it != runtime->bindings.end()) {
            return { it->handler, it->user_data };
        }
    }
    return { runtime->default_handler, runtime->default_user_data };
}

int32_t publishStoredSpec(WinguiSpecBindRuntime* runtime, SuperTerminalClientContext* ctx) {
    std::string spec_json;
    {
        std::lock_guard<std::mutex> lock(runtime->mutex);
        spec_json = runtime->spec_json;
    }

    if (spec_json.empty()) {
        wingui_set_last_error_string_internal("wingui_spec_bind_runtime_run: no spec JSON has been loaded");
        return 0;
    }

    if (!super_terminal_publish_ui_json(ctx, spec_json.c_str())) {
        wingui_set_last_error_string_internal("wingui_spec_bind_runtime_run: failed to publish initial UI JSON");
        return 0;
    }

    wingui_clear_last_error_internal();
    return 1;
}

int32_t WINGUI_CALL runtimeSetup(SuperTerminalClientContext* ctx, void* user_data) {
    auto* runtime = static_cast<WinguiSpecBindRuntime*>(user_data);
    {
        std::lock_guard<std::mutex> lock(runtime->mutex);
        runtime->active_context = ctx;
        runtime->running = true;
    }
    return publishStoredSpec(runtime, ctx);
}

void dispatchBoundEvent(
    WinguiSpecBindRuntime* runtime,
    const char* event_name_utf8,
    const char* payload_json_utf8,
    const char* source_utf8) {
    const BoundHandler bound = findBoundHandler(runtime, event_name_utf8);
    if (!bound.handler) {
        return;
    }

    WinguiSpecBindEventView view{};
    view.event_name_utf8 = event_name_utf8;
    view.payload_json_utf8 = payload_json_utf8;
    view.source_utf8 = source_utf8;
    bound.handler(bound.user_data, runtime, &view);
}

void WINGUI_CALL runtimeOnEvent(
    SuperTerminalClientContext* ctx,
    const SuperTerminalEvent* event,
    void* user_data) {
    (void)ctx;
    auto* runtime = static_cast<WinguiSpecBindRuntime*>(user_data);
    if (!event) {
        return;
    }

    if (event->type == SUPERTERMINAL_EVENT_NATIVE_UI) {
        const char* payload_json_utf8 = event->data.native_ui.payload_json_utf8;
        const auto [event_name, source_name] = parseNativeUiEventFields(payload_json_utf8);
        dispatchBoundEvent(
            runtime,
            event_name.empty() ? nullptr : event_name.c_str(),
            payload_json_utf8,
            source_name.empty() ? nullptr : source_name.c_str());
        return;
    }

    if (event->type == SUPERTERMINAL_EVENT_CLOSE_REQUESTED) {
        const BoundHandler bound = findBoundHandler(runtime, kCloseRequestedEventName);
        if (bound.handler) {
            WinguiSpecBindEventView view{};
            view.event_name_utf8 = kCloseRequestedEventName;
            view.payload_json_utf8 = kCloseRequestedPayload;
            view.source_utf8 = "host";
            bound.handler(bound.user_data, runtime, &view);
        } else {
            super_terminal_request_stop(ctx, 0);
        }
        return;
    }

    if (event->type == SUPERTERMINAL_EVENT_HOST_STOPPING) {
        const std::string payload = std::string("{\"event\":\"") + kHostStoppingEventName +
            "\",\"type\":\"host-stopping\",\"exit_code\":" +
            std::to_string(event->data.host_stopping.exit_code) + "}";
        dispatchBoundEvent(runtime, kHostStoppingEventName, payload.c_str(), "host");
    }
}

void WINGUI_CALL runtimeShutdown(void* user_data) {
    auto* runtime = static_cast<WinguiSpecBindRuntime*>(user_data);
    std::lock_guard<std::mutex> lock(runtime->mutex);
    runtime->active_context = nullptr;
    runtime->running = false;
}

} // namespace

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_runtime_create(
    WinguiSpecBindRuntime** out_runtime) {
    if (!out_runtime) {
        wingui_set_last_error_string_internal("wingui_spec_bind_runtime_create: out_runtime was null");
        return 0;
    }

    *out_runtime = new (std::nothrow) WinguiSpecBindRuntime();
    if (!*out_runtime) {
        wingui_set_last_error_string_internal("wingui_spec_bind_runtime_create: allocation failed");
        return 0;
    }

    wingui_clear_last_error_internal();
    return 1;
}

extern "C" WINGUI_API void WINGUI_CALL wingui_spec_bind_runtime_destroy(
    WinguiSpecBindRuntime* runtime) {
    delete runtime;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_runtime_load_spec_json(
    WinguiSpecBindRuntime* runtime,
    const char* json_utf8) {
    if (!runtime) {
        wingui_set_last_error_string_internal("wingui_spec_bind_runtime_load_spec_json: runtime was null");
        return 0;
    }

    std::string spec_copy;
    if (!validateJsonSpec(json_utf8, &spec_copy)) {
        return 0;
    }

    SuperTerminalClientContext* active_context = nullptr;
    std::string previous_json;
    std::string publish_json;
    {
        std::lock_guard<std::mutex> lock(runtime->mutex);
        previous_json = runtime->spec_json;
        runtime->spec_json = std::move(spec_copy);
        active_context = runtime->active_context;
        publish_json = runtime->spec_json;
    }

    if (active_context) {
        if (!publishOrPatchSpec(active_context, previous_json, publish_json)) {
            wingui_set_last_error_string_internal("wingui_spec_bind_runtime_load_spec_json: live publish/patch failed");
            return 0;
        }
    }

    wingui_clear_last_error_internal();
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_runtime_copy_spec_json(
    WinguiSpecBindRuntime* runtime,
    char* buffer_utf8,
    uint32_t buffer_size,
    uint32_t* out_required_size) {
    if (!runtime) {
        wingui_set_last_error_string_internal("wingui_spec_bind_runtime_copy_spec_json: runtime was null");
        return 0;
    }

    std::string spec_json;
    {
        std::lock_guard<std::mutex> lock(runtime->mutex);
        spec_json = runtime->spec_json;
    }

    const uint32_t required_size = static_cast<uint32_t>(spec_json.size() + 1);
    if (out_required_size) {
        *out_required_size = required_size;
    }
    if (spec_json.empty()) {
        wingui_set_last_error_string_internal("wingui_spec_bind_runtime_copy_spec_json: no spec JSON is loaded");
        return 0;
    }
    if (!buffer_utf8 || buffer_size < required_size) {
        wingui_set_last_error_string_internal("wingui_spec_bind_runtime_copy_spec_json: buffer was null or too small");
        return 0;
    }

    std::memcpy(buffer_utf8, spec_json.c_str(), required_size);
    wingui_clear_last_error_internal();
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_runtime_bind_event(
    WinguiSpecBindRuntime* runtime,
    const char* event_name_utf8,
    WinguiSpecBindEventHandlerFn handler,
    void* user_data) {
    if (!runtime || !event_name_utf8 || !event_name_utf8[0] || !handler) {
        wingui_set_last_error_string_internal("wingui_spec_bind_runtime_bind_event: invalid arguments");
        return 0;
    }

    std::lock_guard<std::mutex> lock(runtime->mutex);
    const auto it = std::find_if(runtime->bindings.begin(), runtime->bindings.end(),
        [&](const SpecBindBinding& binding) {
            return binding.event_name == event_name_utf8;
        });
    if (it != runtime->bindings.end()) {
        it->handler = handler;
        it->user_data = user_data;
    } else {
        runtime->bindings.push_back({ event_name_utf8, handler, user_data });
    }

    wingui_clear_last_error_internal();
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_runtime_unbind_event(
    WinguiSpecBindRuntime* runtime,
    const char* event_name_utf8) {
    if (!runtime || !event_name_utf8 || !event_name_utf8[0]) {
        wingui_set_last_error_string_internal("wingui_spec_bind_runtime_unbind_event: invalid arguments");
        return 0;
    }

    std::lock_guard<std::mutex> lock(runtime->mutex);
    const auto old_size = runtime->bindings.size();
    runtime->bindings.erase(
        std::remove_if(runtime->bindings.begin(), runtime->bindings.end(),
            [&](const SpecBindBinding& binding) {
                return binding.event_name == event_name_utf8;
            }),
        runtime->bindings.end());
    if (runtime->bindings.size() == old_size) {
        wingui_set_last_error_string_internal("wingui_spec_bind_runtime_unbind_event: binding was not found");
        return 0;
    }

    wingui_clear_last_error_internal();
    return 1;
}

extern "C" WINGUI_API void WINGUI_CALL wingui_spec_bind_runtime_clear_bindings(
    WinguiSpecBindRuntime* runtime) {
    if (!runtime) {
        return;
    }
    std::lock_guard<std::mutex> lock(runtime->mutex);
    runtime->bindings.clear();
}

extern "C" WINGUI_API void WINGUI_CALL wingui_spec_bind_runtime_set_default_handler(
    WinguiSpecBindRuntime* runtime,
    WinguiSpecBindEventHandlerFn handler,
    void* user_data) {
    if (!runtime) {
        return;
    }
    std::lock_guard<std::mutex> lock(runtime->mutex);
    runtime->default_handler = handler;
    runtime->default_user_data = user_data;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_runtime_request_stop(
    WinguiSpecBindRuntime* runtime,
    int32_t exit_code) {
    if (!runtime) {
        wingui_set_last_error_string_internal("wingui_spec_bind_runtime_request_stop: runtime was null");
        return 0;
    }

    SuperTerminalClientContext* active_context = nullptr;
    {
        std::lock_guard<std::mutex> lock(runtime->mutex);
        active_context = runtime->active_context;
    }
    if (!active_context) {
        wingui_set_last_error_string_internal("wingui_spec_bind_runtime_request_stop: runtime is not active");
        return 0;
    }
    if (!super_terminal_request_stop(active_context, exit_code)) {
        wingui_set_last_error_string_internal("wingui_spec_bind_runtime_request_stop: host stop request failed");
        return 0;
    }

    wingui_clear_last_error_internal();
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_runtime_get_patch_metrics(
    WinguiSpecBindRuntime* runtime,
    SuperTerminalNativeUiPatchMetrics* out_metrics) {
    if (!runtime || !out_metrics) {
        wingui_set_last_error_string_internal("wingui_spec_bind_runtime_get_patch_metrics: invalid arguments");
        return 0;
    }

    SuperTerminalClientContext* active_context = nullptr;
    {
        std::lock_guard<std::mutex> lock(runtime->mutex);
        active_context = runtime->active_context;
    }
    if (!active_context) {
        wingui_set_last_error_string_internal("wingui_spec_bind_runtime_get_patch_metrics: runtime is not active");
        return 0;
    }
    if (!super_terminal_get_native_ui_patch_metrics(active_context, out_metrics)) {
        wingui_set_last_error_string_internal("wingui_spec_bind_runtime_get_patch_metrics: query failed");
        return 0;
    }

    wingui_clear_last_error_internal();
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_runtime_run(
    WinguiSpecBindRuntime* runtime,
    const WinguiSpecBindRunDesc* desc,
    SuperTerminalRunResult* out_result) {
    if (!runtime) {
        wingui_set_last_error_string_internal("wingui_spec_bind_runtime_run: runtime was null");
        return 0;
    }

    WinguiSpecBindRunDesc run_desc = desc ? *desc : defaultRunDesc();
    if (run_desc.columns == 0) run_desc.columns = 120;
    if (run_desc.rows == 0) run_desc.rows = 40;
    if (run_desc.command_queue_capacity == 0) run_desc.command_queue_capacity = 1024;
    if (run_desc.event_queue_capacity == 0) run_desc.event_queue_capacity = 1024;
    if (run_desc.font_pixel_height == 0) run_desc.font_pixel_height = 18;
    if (run_desc.dpi_scale <= 0.0f) run_desc.dpi_scale = 1.0f;
    if (run_desc.target_frame_ms == 0) run_desc.target_frame_ms = 16;

    SuperTerminalHostedAppDesc hosted{};
    hosted.title_utf8 = run_desc.title_utf8 ? run_desc.title_utf8 : "Wingui Spec + Bind";
    hosted.columns = run_desc.columns;
    hosted.rows = run_desc.rows;
    hosted.flags = run_desc.flags;
    hosted.command_queue_capacity = run_desc.command_queue_capacity;
    hosted.event_queue_capacity = run_desc.event_queue_capacity;
    hosted.font_family_utf8 = run_desc.font_family_utf8;
    hosted.font_pixel_height = run_desc.font_pixel_height;
    hosted.dpi_scale = run_desc.dpi_scale;
    hosted.text_shader_path_utf8 = run_desc.text_shader_path_utf8;
    hosted.initial_ui_json_utf8 = nullptr;
    hosted.target_frame_ms = run_desc.target_frame_ms;
    hosted.auto_request_present = run_desc.auto_request_present;
    hosted.user_data = runtime;
    hosted.setup = runtimeSetup;
    hosted.on_event = runtimeOnEvent;
    hosted.on_frame = nullptr;
    hosted.shutdown = runtimeShutdown;

    wingui_clear_last_error_internal();
    return super_terminal_run_hosted_app(&hosted, out_result);
}