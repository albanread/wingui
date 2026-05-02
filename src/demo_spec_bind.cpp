#include "wingui/spec_bind.h"
#include "wingui/wingui.h"
#include "nlohmann/json.hpp"

#include <windows.h>

#include <cstdio>
#include <string>

using Json = nlohmann::ordered_json;

namespace {

struct DemoState {
    WinguiSpecBindRuntime* runtime = nullptr;
    std::string name = "Ada Lovelace";
    std::string notes = "This window is driven by a raw JSON spec plus named C ABI bindings.";
    bool enabled = true;
    int clicks = 0;
    int event_count = 0;
    std::string last_event = "startup";
};

std::string build_spec_json(const DemoState& state) {
    Json spec;
    spec["type"] = "window";
    spec["title"] = "Spec + Bind Demo";

    spec["menuBar"] = {
        { "menus", Json::array({
            {
                { "text", "File" },
                { "items", Json::array({
                    {
                        { "id", "menu_reset_demo" },
                        { "text", "Reset state" }
                    },
                    {
                        { "separator", true }
                    },
                    {
                        { "id", "menu_exit" },
                        { "text", "Exit" }
                    }
                }) }
            },
            {
                { "text", "State" },
                { "items", Json::array({
                    {
                        { "id", "menu_toggle_enabled" },
                        { "text", "Enabled" },
                        { "checked", state.enabled }
                    },
                    {
                        { "id", "counter_reset" },
                        { "text", "Reset counter" }
                    }
                }) }
            }
        }) }
    };

    spec["commandBar"] = {
        { "items", Json::array({
            {
                { "id", "counter_up" },
                { "text", "Count click" }
            },
            {
                { "id", "menu_toggle_enabled" },
                { "text", "Enabled" },
                { "checked", state.enabled }
            },
            {
                { "separator", true }
            },
            {
                { "id", "menu_reset_demo" },
                { "text", "Reset state" }
            }
        }) }
    };

    char clicks_text[64];
    char events_text[64];
    std::snprintf(clicks_text, sizeof(clicks_text), "Clicks %d", state.clicks);
    std::snprintf(events_text, sizeof(events_text), "Events %d", state.event_count);
    spec["statusBar"] = {
        { "parts", Json::array({
            {
                { "text", state.enabled ? "Mode enabled" : "Mode paused" }
            },
            {
                { "text", clicks_text },
                { "width", 90 }
            },
            {
                { "text", events_text },
                { "width", 90 }
            },
            {
                { "text", std::string("Last ") + state.last_event },
                { "width", 180 }
            }
        }) }
    };

    spec["body"] = {
        { "type", "stack" },
        { "gap", 12 },
        { "children", Json::array({
            {
                { "type", "heading" },
                { "text", "Spec + Bind" }
            },
            {
                { "type", "text" },
                { "text", "This demo uses the new runtime directly: it loads a JSON window spec, binds event names to callbacks, and republishes the full spec when state changes." }
            },
            {
                { "type", "card" },
                { "title", "Editable state" },
                { "children", Json::array({
                    {
                        { "type", "form" },
                        { "gap", 10 },
                        { "children", Json::array({
                            {
                                { "type", "input" },
                                { "label", "Name" },
                                { "value", state.name },
                                { "event", "name" }
                            },
                            {
                                { "type", "checkbox" },
                                { "label", "Enabled" },
                                { "checked", state.enabled },
                                { "event", "enabled" }
                            },
                            {
                                { "type", "textarea" },
                                { "label", "Notes" },
                                { "value", state.notes },
                                { "event", "notes" }
                            }
                        }) }
                    }
                }) }
            },
            {
                { "type", "card" },
                { "title", "Actions" },
                { "children", Json::array({
                    {
                        { "type", "row" },
                        { "gap", 10 },
                        { "children", Json::array({
                            {
                                { "type", "button" },
                                { "text", "Count click" },
                                { "event", "counter_up" }
                            },
                            {
                                { "type", "button" },
                                { "text", "Reset counter" },
                                { "event", "counter_reset" }
                            },
                            {
                                { "type", "button" },
                                { "text", state.enabled ? "Pause" : "Resume" },
                                { "event", "menu_toggle_enabled" }
                            }
                        }) }
                    },
                    {
                        { "type", "text" },
                        { "text", std::string("Hello ") + state.name + ". The counter is " + std::to_string(state.clicks) + "." }
                    },
                    {
                        { "type", "text" },
                        { "text", state.enabled ? "Interactions are active." : "Interactions are paused in the state model, but still editable." }
                    }
                }) }
            }
        }) }
    };

    return spec.dump(2);
}

bool republish(DemoState* state) {
    return wingui_spec_bind_runtime_load_spec_json(state->runtime, build_spec_json(*state).c_str()) != 0;
}

Json parse_payload(const WinguiSpecBindEventView* event_view) {
    if (!event_view || !event_view->payload_json_utf8 || !event_view->payload_json_utf8[0]) {
        return Json();
    }
    return Json::parse(event_view->payload_json_utf8, nullptr, false);
}

void WINGUI_CALL on_name(void* user_data, WinguiSpecBindRuntime*, const WinguiSpecBindEventView* event_view) {
    auto* state = static_cast<DemoState*>(user_data);
    const Json payload = parse_payload(event_view);
    if (payload.is_object() && payload.contains("value") && payload["value"].is_string()) {
        state->name = payload["value"].get<std::string>();
    }
    state->event_count += 1;
    state->last_event = "name";
    republish(state);
}

void WINGUI_CALL on_notes(void* user_data, WinguiSpecBindRuntime*, const WinguiSpecBindEventView* event_view) {
    auto* state = static_cast<DemoState*>(user_data);
    const Json payload = parse_payload(event_view);
    if (payload.is_object() && payload.contains("value") && payload["value"].is_string()) {
        state->notes = payload["value"].get<std::string>();
    }
    state->event_count += 1;
    state->last_event = "notes";
    republish(state);
}

void WINGUI_CALL on_enabled(void* user_data, WinguiSpecBindRuntime*, const WinguiSpecBindEventView* event_view) {
    auto* state = static_cast<DemoState*>(user_data);
    const Json payload = parse_payload(event_view);
    if (payload.is_object() && payload.contains("checked") && payload["checked"].is_boolean()) {
        state->enabled = payload["checked"].get<bool>();
    }
    state->event_count += 1;
    state->last_event = "enabled";
    republish(state);
}

void WINGUI_CALL on_toggle_enabled(void* user_data, WinguiSpecBindRuntime*, const WinguiSpecBindEventView*) {
    auto* state = static_cast<DemoState*>(user_data);
    state->enabled = !state->enabled;
    state->event_count += 1;
    state->last_event = "menu_toggle_enabled";
    republish(state);
}

void WINGUI_CALL on_counter_up(void* user_data, WinguiSpecBindRuntime*, const WinguiSpecBindEventView*) {
    auto* state = static_cast<DemoState*>(user_data);
    state->clicks += 1;
    state->event_count += 1;
    state->last_event = "counter_up";
    republish(state);
}

void WINGUI_CALL on_counter_reset(void* user_data, WinguiSpecBindRuntime*, const WinguiSpecBindEventView*) {
    auto* state = static_cast<DemoState*>(user_data);
    state->clicks = 0;
    state->event_count += 1;
    state->last_event = "counter_reset";
    republish(state);
}

void WINGUI_CALL on_reset_demo(void* user_data, WinguiSpecBindRuntime*, const WinguiSpecBindEventView*) {
    auto* state = static_cast<DemoState*>(user_data);
    state->name = "Ada Lovelace";
    state->notes = "This window is driven by a raw JSON spec plus named C ABI bindings.";
    state->enabled = true;
    state->clicks = 0;
    state->event_count += 1;
    state->last_event = "menu_reset_demo";
    republish(state);
}

void WINGUI_CALL on_exit(void* user_data, WinguiSpecBindRuntime* runtime, const WinguiSpecBindEventView*) {
    auto* state = static_cast<DemoState*>(user_data);
    state->event_count += 1;
    state->last_event = "menu_exit";
    wingui_spec_bind_runtime_request_stop(runtime, 0);
}

int fail_with_message(const char* title_utf8) {
    const char* message = wingui_last_error_utf8();
    MessageBoxA(nullptr,
                (message && message[0]) ? message : "Unknown error",
                title_utf8,
                MB_OK | MB_ICONERROR);
    return 1;
}

} // namespace

int main() {
    WinguiSpecBindRuntime* runtime = nullptr;
    if (!wingui_spec_bind_runtime_create(&runtime)) {
        return fail_with_message("Spec + Bind Demo");
    }

    DemoState state;
    state.runtime = runtime;

    const bool ok =
        wingui_spec_bind_runtime_bind_event(runtime, "name", on_name, &state) != 0 &&
        wingui_spec_bind_runtime_bind_event(runtime, "notes", on_notes, &state) != 0 &&
        wingui_spec_bind_runtime_bind_event(runtime, "enabled", on_enabled, &state) != 0 &&
        wingui_spec_bind_runtime_bind_event(runtime, "menu_toggle_enabled", on_toggle_enabled, &state) != 0 &&
        wingui_spec_bind_runtime_bind_event(runtime, "counter_up", on_counter_up, &state) != 0 &&
        wingui_spec_bind_runtime_bind_event(runtime, "counter_reset", on_counter_reset, &state) != 0 &&
        wingui_spec_bind_runtime_bind_event(runtime, "menu_reset_demo", on_reset_demo, &state) != 0 &&
        wingui_spec_bind_runtime_bind_event(runtime, "menu_exit", on_exit, &state) != 0 &&
        wingui_spec_bind_runtime_load_spec_json(runtime, build_spec_json(state).c_str()) != 0;

    if (!ok) {
        wingui_spec_bind_runtime_destroy(runtime);
        return fail_with_message("Spec + Bind Demo");
    }

    WinguiSpecBindRunDesc desc{};
    desc.title_utf8 = "Spec + Bind Demo";
    desc.columns = 110;
    desc.rows = 34;
    desc.command_queue_capacity = 1024;
    desc.event_queue_capacity = 1024;
    desc.font_pixel_height = 18;
    desc.dpi_scale = 1.0f;
    desc.target_frame_ms = 16;
    desc.auto_request_present = 0;

    SuperTerminalRunResult result{};
    const int32_t run_ok = wingui_spec_bind_runtime_run(runtime, &desc, &result);
    wingui_spec_bind_runtime_destroy(runtime);
    if (!run_ok) {
        return fail_with_message("Spec + Bind Demo");
    }
    return result.exit_code;
}