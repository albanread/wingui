#include "wingui/spec_bind.h"
#include "wingui/wingui.h"

#include <windows.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct DemoState {
    WinguiSpecBindRuntime* runtime;
    char name[128];
    char notes[512];
    int enabled;
    int clicks;
    int event_count;
    char last_event[64];
    char spec_json[8192];
    SuperTerminalNativeUiPatchMetrics patch_metrics;
} DemoState;

static void copy_text(char* dst, size_t dst_size, const char* src) {
    if (!dst || dst_size == 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    strncpy_s(dst, dst_size, src, _TRUNCATE);
}

static void reset_state(DemoState* state) {
    copy_text(state->name, sizeof(state->name), "Ada Lovelace");
    copy_text(state->notes, sizeof(state->notes),
        "This window is driven by a raw JSON spec plus named C ABI bindings.");
    state->enabled = 1;
    state->clicks = 0;
    state->event_count = 0;
    memset(&state->patch_metrics, 0, sizeof(state->patch_metrics));
    copy_text(state->last_event, sizeof(state->last_event), "startup");
}

static void update_patch_metrics(DemoState* state) {
    SuperTerminalNativeUiPatchMetrics metrics;
    memset(&metrics, 0, sizeof(metrics));
    if (state->runtime && wingui_spec_bind_runtime_get_patch_metrics(state->runtime, &metrics)) {
        state->patch_metrics = metrics;
    }
}

static void json_escape(const char* src, char* dst, size_t dst_size) {
    size_t out = 0;
    if (!dst || dst_size == 0) {
        return;
    }

    if (!src) {
        dst[0] = '\0';
        return;
    }

    while (*src && out + 1 < dst_size) {
        const unsigned char ch = (unsigned char)*src++;
        if ((ch == '\\' || ch == '"') && out + 2 < dst_size) {
            dst[out++] = '\\';
            dst[out++] = (char)ch;
        } else if (ch == '\n' && out + 2 < dst_size) {
            dst[out++] = '\\';
            dst[out++] = 'n';
        } else if (ch == '\r' && out + 2 < dst_size) {
            dst[out++] = '\\';
            dst[out++] = 'r';
        } else if (ch == '\t' && out + 2 < dst_size) {
            dst[out++] = '\\';
            dst[out++] = 't';
        } else if (ch >= 0x20) {
            dst[out++] = (char)ch;
        }
    }

    dst[out] = '\0';
}

static const char* find_json_value(const char* json, const char* key) {
    static char pattern[64];
    const char* pos;
    if (!json || !key) {
        return NULL;
    }
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    pos = strstr(json, pattern);
    if (!pos) {
        return NULL;
    }
    pos += strlen(pattern);
    while (*pos && isspace((unsigned char)*pos)) {
        ++pos;
    }
    if (*pos != ':') {
        return NULL;
    }
    ++pos;
    while (*pos && isspace((unsigned char)*pos)) {
        ++pos;
    }
    return pos;
}

static int json_extract_string_field(const char* json, const char* key, char* dst, size_t dst_size) {
    const char* pos = find_json_value(json, key);
    size_t out = 0;
    if (!pos || *pos != '"' || !dst || dst_size == 0) {
        return 0;
    }

    ++pos;
    while (*pos && *pos != '"' && out + 1 < dst_size) {
        if (*pos == '\\') {
            ++pos;
            if (!*pos) {
                break;
            }
            switch (*pos) {
            case 'n': dst[out++] = '\n'; break;
            case 'r': dst[out++] = '\r'; break;
            case 't': dst[out++] = '\t'; break;
            case '\\': dst[out++] = '\\'; break;
            case '"': dst[out++] = '"'; break;
            default: dst[out++] = *pos; break;
            }
            ++pos;
            continue;
        }
        dst[out++] = *pos++;
    }
    dst[out] = '\0';
    return 1;
}

static int json_extract_bool_field(const char* json, const char* key, int* out_value) {
    const char* pos = find_json_value(json, key);
    if (!pos || !out_value) {
        return 0;
    }
    if (strncmp(pos, "true", 4) == 0) {
        *out_value = 1;
        return 1;
    }
    if (strncmp(pos, "false", 5) == 0) {
        *out_value = 0;
        return 1;
    }
    return 0;
}

static int rebuild_spec(DemoState* state) {
    char escaped_name[256];
    char escaped_notes[1024];
    char escaped_last_event[128];
    char clicks_text[64];
    char events_text[64];
    char patches_text[64];
    char rebuilds_text[64];
    char metrics_summary[256];

    update_patch_metrics(state);
    json_escape(state->name, escaped_name, sizeof(escaped_name));
    json_escape(state->notes, escaped_notes, sizeof(escaped_notes));
    json_escape(state->last_event, escaped_last_event, sizeof(escaped_last_event));
    snprintf(clicks_text, sizeof(clicks_text), "Clicks %d", state->clicks);
    snprintf(events_text, sizeof(events_text), "Events %d", state->event_count);
    snprintf(patches_text, sizeof(patches_text), "Patches %llu", (unsigned long long)state->patch_metrics.direct_apply_count);
    snprintf(rebuilds_text, sizeof(rebuilds_text), "Rebuilds %llu", (unsigned long long)(state->patch_metrics.subtree_rebuild_count + state->patch_metrics.window_rebuild_count));
    snprintf(metrics_summary,
             sizeof(metrics_summary),
             "Patch metrics: publish=%llu patch=%llu direct=%llu subtree=%llu window=%llu failed=%llu",
             (unsigned long long)state->patch_metrics.publish_count,
             (unsigned long long)state->patch_metrics.patch_request_count,
             (unsigned long long)state->patch_metrics.direct_apply_count,
             (unsigned long long)state->patch_metrics.subtree_rebuild_count,
             (unsigned long long)state->patch_metrics.window_rebuild_count,
             (unsigned long long)state->patch_metrics.failed_patch_count);

    return snprintf(
        state->spec_json,
        sizeof(state->spec_json),
        "{"
        "\n  \"type\": \"window\"," 
        "\n  \"id\": \"demo_c_window\"," 
        "\n  \"title\": \"Spec + Bind C Demo\","
        "\n  \"menuBar\": {"
        "\n    \"menus\": ["
        "\n      {"
        "\n        \"text\": \"File\","
        "\n        \"items\": ["
        "\n          { \"id\": \"menu_reset_demo\", \"text\": \"Reset state\" },"
        "\n          { \"separator\": true },"
        "\n          { \"id\": \"menu_exit\", \"text\": \"Exit\" }"
        "\n        ]"
        "\n      },"
        "\n      {"
        "\n        \"text\": \"State\","
        "\n        \"items\": ["
        "\n          { \"id\": \"menu_toggle_enabled\", \"text\": \"Enabled\", \"checked\": %s },"
        "\n          { \"id\": \"counter_reset\", \"text\": \"Reset counter\" }"
        "\n        ]"
        "\n      }"
        "\n    ]"
        "\n  },"
        "\n  \"commandBar\": {"
        "\n    \"items\": ["
        "\n      { \"id\": \"counter_up\", \"text\": \"Count click\" },"
        "\n      { \"id\": \"menu_toggle_enabled\", \"text\": \"Enabled\", \"checked\": %s },"
        "\n      { \"separator\": true },"
        "\n      { \"id\": \"menu_reset_demo\", \"text\": \"Reset state\" }"
        "\n    ]"
        "\n  },"
        "\n  \"statusBar\": {"
        "\n    \"parts\": ["
        "\n      { \"text\": \"%s\" },"
        "\n      { \"text\": \"%s\", \"width\": 90 },"
        "\n      { \"text\": \"%s\", \"width\": 90 },"
        "\n      { \"text\": \"%s\", \"width\": 100 },"
        "\n      { \"text\": \"%s\", \"width\": 110 },"
        "\n      { \"text\": \"Last %s\", \"width\": 180 }"
        "\n    ]"
        "\n  },"
        "\n  \"body\": {"
        "\n    \"type\": \"stack\","
        "\n    \"id\": \"demo_c_body\","
        "\n    \"gap\": 12,"
        "\n    \"children\": ["
        "\n      { \"type\": \"heading\", \"id\": \"demo_c_heading\", \"text\": \"Spec + Bind (C)\" },"
        "\n      { \"type\": \"text\", \"id\": \"demo_c_intro\", \"text\": \"This sample is written in plain C and uses only the public Spec + Bind ABI.\" },"
        "\n      { \"type\": \"text\", \"id\": \"demo_c_metrics\", \"text\": \"%s\" },"
        "\n      {"
        "\n        \"type\": \"card\","
        "\n        \"id\": \"demo_c_editable_card\","
        "\n        \"title\": \"Editable state\","
        "\n        \"children\": ["
        "\n          {"
        "\n            \"type\": \"form\","
        "\n            \"id\": \"demo_c_form\","
        "\n            \"gap\": 10,"
        "\n            \"children\": ["
        "\n              { \"type\": \"input\", \"id\": \"demo_c_name\", \"label\": \"Name\", \"value\": \"%s\", \"event\": \"name\" },"
        "\n              { \"type\": \"checkbox\", \"id\": \"demo_c_enabled\", \"label\": \"Enabled\", \"checked\": %s, \"event\": \"enabled\" },"
        "\n              { \"type\": \"textarea\", \"id\": \"demo_c_notes\", \"label\": \"Notes\", \"value\": \"%s\", \"event\": \"notes\" }"
        "\n            ]"
        "\n          }"
        "\n        ]"
        "\n      },"
        "\n      {"
        "\n        \"type\": \"card\","
        "\n        \"id\": \"demo_c_actions_card\","
        "\n        \"title\": \"Actions\","
        "\n        \"children\": ["
        "\n          {"
        "\n            \"type\": \"row\","
        "\n            \"id\": \"demo_c_actions_row\","
        "\n            \"gap\": 10,"
        "\n            \"children\": ["
        "\n              { \"type\": \"button\", \"id\": \"demo_c_counter_up\", \"text\": \"Count click\", \"event\": \"counter_up\" },"
        "\n              { \"type\": \"button\", \"id\": \"demo_c_counter_reset\", \"text\": \"Reset counter\", \"event\": \"counter_reset\" },"
        "\n              { \"type\": \"button\", \"id\": \"demo_c_toggle_enabled\", \"text\": \"%s\", \"event\": \"menu_toggle_enabled\" }"
        "\n            ]"
        "\n          },"
        "\n          { \"type\": \"text\", \"id\": \"demo_c_counter_label\", \"text\": \"Hello %s. The counter is %d.\" },"
        "\n          { \"type\": \"text\", \"id\": \"demo_c_enabled_label\", \"text\": \"%s\" }"
        "\n        ]"
        "\n      }"
        "\n    ]"
        "\n  }"
        "\n}",
        state->enabled ? "true" : "false",
        state->enabled ? "true" : "false",
        state->enabled ? "Mode enabled" : "Mode paused",
        clicks_text,
        events_text,
        patches_text,
        rebuilds_text,
        escaped_last_event,
        metrics_summary,
        escaped_name,
        state->enabled ? "true" : "false",
        escaped_notes,
        state->enabled ? "Pause" : "Resume",
        escaped_name,
        state->clicks,
        state->enabled ? "Interactions are active." : "Interactions are paused in the state model, but still editable.") > 0;
}

static int republish(DemoState* state) {
    if (!rebuild_spec(state)) {
        return 0;
    }
    return wingui_spec_bind_runtime_load_spec_json(state->runtime, state->spec_json);
}

static void mark_event(DemoState* state, const char* event_name) {
    state->event_count += 1;
    copy_text(state->last_event, sizeof(state->last_event), event_name);
}

static void WINGUI_CALL on_name(void* user_data, WinguiSpecBindRuntime* runtime, const WinguiSpecBindEventView* event_view) {
    DemoState* state = (DemoState*)user_data;
    char value[128];
    (void)runtime;
    if (json_extract_string_field(event_view->payload_json_utf8, "value", value, sizeof(value))) {
        copy_text(state->name, sizeof(state->name), value);
    }
    mark_event(state, "name");
    republish(state);
}

static void WINGUI_CALL on_notes(void* user_data, WinguiSpecBindRuntime* runtime, const WinguiSpecBindEventView* event_view) {
    DemoState* state = (DemoState*)user_data;
    char value[512];
    (void)runtime;
    if (json_extract_string_field(event_view->payload_json_utf8, "value", value, sizeof(value))) {
        copy_text(state->notes, sizeof(state->notes), value);
    }
    mark_event(state, "notes");
    republish(state);
}

static void WINGUI_CALL on_enabled(void* user_data, WinguiSpecBindRuntime* runtime, const WinguiSpecBindEventView* event_view) {
    DemoState* state = (DemoState*)user_data;
    int checked = 0;
    (void)runtime;
    if (json_extract_bool_field(event_view->payload_json_utf8, "checked", &checked)) {
        state->enabled = checked;
    }
    mark_event(state, "enabled");
    republish(state);
}

static void WINGUI_CALL on_toggle_enabled(void* user_data, WinguiSpecBindRuntime* runtime, const WinguiSpecBindEventView* event_view) {
    DemoState* state = (DemoState*)user_data;
    (void)runtime;
    (void)event_view;
    state->enabled = !state->enabled;
    mark_event(state, "menu_toggle_enabled");
    republish(state);
}

static void WINGUI_CALL on_counter_up(void* user_data, WinguiSpecBindRuntime* runtime, const WinguiSpecBindEventView* event_view) {
    DemoState* state = (DemoState*)user_data;
    (void)runtime;
    (void)event_view;
    state->clicks += 1;
    mark_event(state, "counter_up");
    republish(state);
}

static void WINGUI_CALL on_counter_reset(void* user_data, WinguiSpecBindRuntime* runtime, const WinguiSpecBindEventView* event_view) {
    DemoState* state = (DemoState*)user_data;
    (void)runtime;
    (void)event_view;
    state->clicks = 0;
    mark_event(state, "counter_reset");
    republish(state);
}

static void WINGUI_CALL on_reset_demo(void* user_data, WinguiSpecBindRuntime* runtime, const WinguiSpecBindEventView* event_view) {
    DemoState* state = (DemoState*)user_data;
    (void)runtime;
    (void)event_view;
    reset_state(state);
    mark_event(state, "menu_reset_demo");
    republish(state);
}

static void WINGUI_CALL on_exit(void* user_data, WinguiSpecBindRuntime* runtime, const WinguiSpecBindEventView* event_view) {
    DemoState* state = (DemoState*)user_data;
    (void)event_view;
    mark_event(state, "menu_exit");
    wingui_spec_bind_runtime_request_stop(runtime, 0);
}

static int fail_with_message(const char* title_utf8) {
    const char* message = wingui_last_error_utf8();
    MessageBoxA(NULL,
                (message && message[0]) ? message : "Unknown error",
                title_utf8,
                MB_OK | MB_ICONERROR);
    return 1;
}

int main(void) {
    WinguiSpecBindRuntime* runtime = NULL;
    DemoState state;
    WinguiSpecBindRunDesc desc;
    SuperTerminalRunResult result;
    int ok;

    memset(&state, 0, sizeof(state));
    memset(&desc, 0, sizeof(desc));
    memset(&result, 0, sizeof(result));

    if (!wingui_spec_bind_runtime_create(&runtime)) {
        return fail_with_message("Spec + Bind C Demo");
    }

    state.runtime = runtime;
    reset_state(&state);
    ok =
        wingui_spec_bind_runtime_bind_event(runtime, "name", on_name, &state) &&
        wingui_spec_bind_runtime_bind_event(runtime, "notes", on_notes, &state) &&
        wingui_spec_bind_runtime_bind_event(runtime, "enabled", on_enabled, &state) &&
        wingui_spec_bind_runtime_bind_event(runtime, "menu_toggle_enabled", on_toggle_enabled, &state) &&
        wingui_spec_bind_runtime_bind_event(runtime, "counter_up", on_counter_up, &state) &&
        wingui_spec_bind_runtime_bind_event(runtime, "counter_reset", on_counter_reset, &state) &&
        wingui_spec_bind_runtime_bind_event(runtime, "menu_reset_demo", on_reset_demo, &state) &&
        wingui_spec_bind_runtime_bind_event(runtime, "menu_exit", on_exit, &state) &&
        republish(&state);

    if (!ok) {
        wingui_spec_bind_runtime_destroy(runtime);
        return fail_with_message("Spec + Bind C Demo");
    }

    desc.title_utf8 = "Spec + Bind C Demo";
    desc.columns = 110;
    desc.rows = 34;
    desc.command_queue_capacity = 1024;
    desc.event_queue_capacity = 1024;
    desc.font_pixel_height = 18;
    desc.dpi_scale = 1.0f;
    desc.target_frame_ms = 16;
    desc.auto_request_present = 0;

    ok = wingui_spec_bind_runtime_run(runtime, &desc, &result);
    wingui_spec_bind_runtime_destroy(runtime);
    if (!ok) {
        return fail_with_message("Spec + Bind C Demo");
    }

    return result.exit_code;
}