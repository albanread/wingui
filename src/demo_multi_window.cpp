#ifndef WINGUI_BUILD_DLL
#define WINGUI_BUILD_DLL
#endif

#include "wingui/terminal.h"

#include <cstring>

namespace {

struct DemoState {
    SuperTerminalWindowId bootstrap_window{};
    SuperTerminalWindowId left_window{};
    SuperTerminalWindowId right_window{};
    int open_windows = 0;
};

constexpr const char* kLeftWindowJson = R"JSON(
{
  "type": "window",
  "body": {
        "type": "stack",
    "children": [
            { "type": "heading", "text": "Left window" },
            { "type": "text", "text": "This window is independent from the right window." },
            { "type": "button", "text": "Close Left", "event": "close_left" }
    ]
  }
}
)JSON";

constexpr const char* kRightWindowJson = R"JSON(
{
  "type": "window",
  "body": {
        "type": "stack",
    "children": [
            { "type": "heading", "text": "Right window" },
            { "type": "text", "text": "Close either window without tearing down the app immediately." },
            { "type": "button", "text": "Close Right", "event": "close_right" }
    ]
  }
}
)JSON";

bool createDemoWindow(SuperTerminalClientContext* ctx,
                      const char* title_utf8,
                      const char* json_utf8,
                      SuperTerminalWindowId* out_window_id) {
    SuperTerminalWindowDesc desc{};
    desc.title_utf8 = title_utf8;
    desc.columns = 72;
    desc.rows = 26;
    desc.font_family_utf8 = "Consolas";
    desc.font_pixel_height = 18;
    desc.dpi_scale = 1.0f;
    desc.text_shader_path_utf8 = "shaders/text_grid.hlsl";
    desc.initial_ui_json_utf8 = json_utf8;
    return super_terminal_create_window(ctx, &desc, out_window_id) != 0;
}

bool closeTrackedWindow(SuperTerminalClientContext* ctx,
                        SuperTerminalWindowId window_id) {
    return window_id.value != 0 && super_terminal_close_window(ctx, window_id) != 0;
}

void markClosed(DemoState* state, SuperTerminalWindowId window_id) {
    if (!state || window_id.value == 0) return;
    if (state->left_window.value == window_id.value) {
        state->left_window = {};
        state->open_windows -= 1;
        return;
    }
    if (state->right_window.value == window_id.value) {
        state->right_window = {};
        state->open_windows -= 1;
    }
}

int32_t WINGUI_CALL demoSetup(SuperTerminalClientContext* ctx, void* user_data) {
    auto* state = static_cast<DemoState*>(user_data);
    if (!ctx || !state) return 0;

    state->bootstrap_window.value = 1;
    super_terminal_close_window(ctx, state->bootstrap_window);

    if (!createDemoWindow(ctx, "Multi-Window Left", kLeftWindowJson, &state->left_window)) {
        return 0;
    }
    if (!createDemoWindow(ctx, "Multi-Window Right", kRightWindowJson, &state->right_window)) {
        return 0;
    }

    state->open_windows = 2;
    return 1;
}

void WINGUI_CALL demoOnEvent(SuperTerminalClientContext* ctx,
                             const SuperTerminalEvent* event,
                             void* user_data) {
    auto* state = static_cast<DemoState*>(user_data);
    if (!ctx || !event || !state) return;

    if (event->type == SUPERTERMINAL_EVENT_CLOSE_REQUESTED) {
        closeTrackedWindow(ctx, event->window_id);
        return;
    }

    if (event->type == SUPERTERMINAL_EVENT_WINDOW_CLOSED) {
        markClosed(state, event->window_id);
        if (state->open_windows <= 0) {
            super_terminal_request_stop(ctx, 0);
        }
        return;
    }

    if (event->type == SUPERTERMINAL_EVENT_NATIVE_UI) {
        const char* payload = event->data.native_ui.payload_json_utf8;
        if (!payload) return;
        if (std::strstr(payload, "\"event\":\"close_left\"")) {
            closeTrackedWindow(ctx, state->left_window);
            return;
        }
        if (std::strstr(payload, "\"event\":\"close_right\"")) {
            closeTrackedWindow(ctx, state->right_window);
            return;
        }
    }
}

} // namespace

int main() {
    DemoState state{};

    SuperTerminalHostedAppDesc desc{};
    desc.title_utf8 = "Multi-Window Bootstrap";
    desc.columns = 40;
    desc.rows = 12;
    desc.font_family_utf8 = "Consolas";
    desc.font_pixel_height = 18;
    desc.dpi_scale = 1.0f;
    desc.text_shader_path_utf8 = "shaders/text_grid.hlsl";
    desc.user_data = &state;
    desc.setup = demoSetup;
    desc.on_event = demoOnEvent;
    desc.auto_request_present = 0;

    SuperTerminalRunResult result{};
    return super_terminal_run_hosted_app(&desc, &result) ? result.exit_code : 1;
}
