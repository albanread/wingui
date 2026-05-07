#ifndef WINGUI_BUILD_DLL
#define WINGUI_BUILD_DLL
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "wingui/spec_bind.h"

#include "wingui/ui_model.h"

#include "wingui_internal.h"
#include "nlohmann/json.hpp"

#include <windows.h>
#include <dwrite.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <unordered_map>
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
    WinguiSpecBindFrameHandlerFn frame_handler = nullptr;
    void* frame_user_data = nullptr;
    SuperTerminalClientContext* active_context = nullptr;
    bool running = false;
    // Per-pane inbox map — key is pane_id.value
    std::mutex inbox_map_mutex;
    std::unordered_map<uint64_t, std::unique_ptr<struct PaneInbox>> pane_inboxes;
};

struct WinguiSpecBindFrameView {
    SuperTerminalClientContext* ctx = nullptr;
    const SuperTerminalFrameTick* tick = nullptr;
};

// ---------------------------------------------------------------------------
// Pane inbox — SPSC lock-free ring buffer
// ---------------------------------------------------------------------------

constexpr uint32_t kPaneInboxCapacity = 64;
constexpr uint32_t kPaneMsgKindCap    = 32;
constexpr uint32_t kPaneMsgDetailCap  = 128;

struct PaneMsg {
    char kind[kPaneMsgKindCap];
    char detail[kPaneMsgDetailCap];
};

struct PaneInbox {
    std::atomic<uint32_t> write_pos{0};
    std::atomic<uint32_t> read_pos{0};
    PaneMsg slots[kPaneInboxCapacity];

    bool post(const char* kind, const char* detail) {
        const uint32_t w = write_pos.load(std::memory_order_relaxed);
        const uint32_t r = read_pos.load(std::memory_order_acquire);
        if (w - r >= kPaneInboxCapacity) {
            return false; // full — drop
        }
        PaneMsg& slot = slots[w % kPaneInboxCapacity];
        strncpy(slot.kind,   kind   ? kind   : "", kPaneMsgKindCap   - 1);
        strncpy(slot.detail, detail ? detail : "", kPaneMsgDetailCap - 1);
        slot.kind  [kPaneMsgKindCap   - 1] = '\0';
        slot.detail[kPaneMsgDetailCap - 1] = '\0';
        write_pos.store(w + 1, std::memory_order_release);
        return true;
    }

    bool poll(char* kind_out, uint32_t kind_cap, char* detail_out, uint32_t detail_cap) {
        const uint32_t r = read_pos.load(std::memory_order_relaxed);
        const uint32_t w = write_pos.load(std::memory_order_acquire);
        if (r == w) {
            return false; // empty
        }
        const PaneMsg& slot = slots[r % kPaneInboxCapacity];
        if (kind_out   && kind_cap   > 0) { strncpy(kind_out,   slot.kind,   kind_cap   - 1); kind_out  [kind_cap   - 1] = '\0'; }
        if (detail_out && detail_cap > 0) { strncpy(detail_out, slot.detail, detail_cap - 1); detail_out[detail_cap - 1] = '\0'; }
        read_pos.store(r + 1, std::memory_order_release);
        return true;
    }
};

PaneInbox* getOrCreateInbox(WinguiSpecBindRuntime* runtime, uint64_t pane_value) {
    std::lock_guard<std::mutex> lock(runtime->inbox_map_mutex);
    auto it = runtime->pane_inboxes.find(pane_value);
    if (it != runtime->pane_inboxes.end()) {
        return it->second.get();
    }
    auto inbox = std::make_unique<PaneInbox>();
    PaneInbox* ptr = inbox.get();
    runtime->pane_inboxes.emplace(pane_value, std::move(inbox));
    return ptr;
}

PaneInbox* findInbox(WinguiSpecBindRuntime* runtime, uint64_t pane_value) {
    std::lock_guard<std::mutex> lock(runtime->inbox_map_mutex);
    auto it = runtime->pane_inboxes.find(pane_value);
    return (it != runtime->pane_inboxes.end()) ? it->second.get() : nullptr;
}

// Process-wide active runtime pointer (set at run start, cleared at shutdown).
WinguiSpecBindRuntime* g_spec_bind_active_runtime = nullptr;

namespace {

constexpr const char* kCloseRequestedEventName = "__close_requested";
constexpr const char* kHostStoppingEventName = "__host_stopping";
constexpr const char* kCloseRequestedPayload = "{\"event\":\"__close_requested\",\"type\":\"close-requested\"}";

struct BoundHandler {
    WinguiSpecBindEventHandlerFn handler = nullptr;
    void* user_data = nullptr;
};

struct BoundFrameHandler {
    WinguiSpecBindFrameHandlerFn handler = nullptr;
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

BoundFrameHandler findFrameHandler(WinguiSpecBindRuntime* runtime) {
    std::lock_guard<std::mutex> lock(runtime->mutex);
    return { runtime->frame_handler, runtime->frame_user_data };
}

bool validateFrameView(const WinguiSpecBindFrameView* frame_view, const char* caller) {
    if (!frame_view || !frame_view->ctx || !frame_view->tick) {
        wingui_set_last_error_string_internal((std::string(caller) + ": frame_view was invalid").c_str());
        return false;
    }
    return true;
}

bool validatePaneRef(WinguiSpecBindPaneRef pane, const char* caller) {
    if (pane.pane_id.value == 0) {
        wingui_set_last_error_string_internal((std::string(caller) + ": pane was invalid").c_str());
        return false;
    }
    return true;
}

bool bindPaneRef(const WinguiSpecBindFrameView* frame_view,
                 SuperTerminalPaneId pane_id,
                 WinguiSpecBindPaneRef* out_pane,
                 const char* caller) {
    if (!validateFrameView(frame_view, caller)) {
        return false;
    }
    if (!out_pane || pane_id.value == 0) {
        wingui_set_last_error_string_internal((std::string(caller) + ": invalid pane arguments").c_str());
        return false;
    }

    out_pane->pane_id = pane_id;
    out_pane->window_id = frame_view->tick->window_id;
    out_pane->buffer_index = frame_view->tick->buffer_index;
    out_pane->active_buffer_index = frame_view->tick->active_buffer_index;
    wingui_clear_last_error_internal();
    return true;
}

WinguiSurfacePrimitive makeSurfacePrimitiveBase(float color_r,
                                                float color_g,
                                                float color_b,
                                                float color_a,
                                                uint32_t kind) {
    WinguiSurfacePrimitive primitive{};
    primitive.color[0] = color_r;
    primitive.color[1] = color_g;
    primitive.color[2] = color_b;
    primitive.color[3] = color_a;
    primitive.kind = kind;
    return primitive;
}

int32_t drawSurfacePrimitiveBatch(const WinguiSpecBindFrameView* frame_view,
                                  WinguiSpecBindPaneRef pane,
                                  uint32_t content_buffer_mode,
                                  uint32_t blend_mode,
                                  int32_t clear_before,
                                  const float clear_color_rgba[4],
                                  const std::vector<WinguiSurfacePrimitive>& primitives,
                                  const std::vector<float>& path_points_xy,
                                  const char* caller) {
    if (!validateFrameView(frame_view, caller) || !validatePaneRef(pane, caller)) {
        return 0;
    }
    if (primitives.empty()) {
        wingui_clear_last_error_internal();
        return 1;
    }
    if (!super_terminal_surface_draw_primitives(
            frame_view->ctx,
            pane.pane_id,
            pane.buffer_index,
            content_buffer_mode,
            blend_mode,
            clear_before,
            clear_color_rgba,
            primitives.data(),
            static_cast<uint32_t>(primitives.size()),
            path_points_xy.empty() ? nullptr : path_points_xy.data(),
            static_cast<uint32_t>(path_points_xy.size() / 2u))) {
        wingui_set_last_error_string_internal((std::string(caller) + ": draw failed").c_str());
        return 0;
    }
    wingui_clear_last_error_internal();
    return 1;
}

int32_t drawSingleSurfacePrimitive(const WinguiSpecBindFrameView* frame_view,
                                   WinguiSpecBindPaneRef pane,
                                   uint32_t content_buffer_mode,
                                   uint32_t blend_mode,
                                   int32_t clear_before,
                                   const float clear_color_rgba[4],
                                   const WinguiSurfacePrimitive& primitive,
                                   const char* caller) {
    const std::vector<WinguiSurfacePrimitive> primitives{primitive};
    const std::vector<float> path_points_xy;
    return drawSurfacePrimitiveBatch(
        frame_view,
        pane,
        content_buffer_mode,
        blend_mode,
        clear_before,
        clear_color_rgba,
        primitives,
        path_points_xy,
        caller);
}

WinguiSurfacePrimitive makeLineSurfacePrimitive(float x0,
                                                float y0,
                                                float x1,
                                                float y1,
                                                float half_thickness,
                                                float color_r,
                                                float color_g,
                                                float color_b,
                                                float color_a) {
    WinguiSurfacePrimitive primitive = makeSurfacePrimitiveBase(
        color_r,
        color_g,
        color_b,
        color_a,
        WINGUI_SURFACE_DRAW_LINE);
    primitive.param0[0] = x0;
    primitive.param0[1] = y0;
    primitive.param0[2] = x1;
    primitive.param0[3] = y1;
    primitive.param1[0] = half_thickness;
    return primitive;
}

uint32_t oval_segment_count(float radius_x, float radius_y) {
    const float max_radius = std::max(radius_x, radius_y);
    const float estimate = std::ceil(max_radius * 0.75f);
    return static_cast<uint32_t>(std::clamp(estimate, 16.0f, 256.0f));
}

struct TextLayoutLine {
    uint32_t start_index = 0;
    uint32_t length = 0;
    float y = 0.0f;
};

struct TextLayoutInfo {
    std::vector<std::pair<float, float>> insertion_points;
    std::vector<TextLayoutLine> lines;
    float width = 0.0f;
    float height = 0.0f;
    uint32_t char_count = 0;
};

bool decodeUtf8Codepoint(const char*& cursor, const char* end, uint32_t& out_cp) {
    if (cursor >= end) {
        return false;
    }
    const unsigned char b0 = static_cast<unsigned char>(*cursor++);
    if (b0 < 0x80) {
        out_cp = b0;
        return true;
    }
    auto consumeContinuation = [&](uint32_t& acc, int count) -> bool {
        for (int i = 0; i < count; ++i) {
            if (cursor >= end) {
                return false;
            }
            const unsigned char b = static_cast<unsigned char>(*cursor++);
            if ((b & 0xC0) != 0x80) {
                return false;
            }
            acc = (acc << 6) | (b & 0x3F);
        }
        return true;
    };

    uint32_t acc = 0;
    if ((b0 & 0xE0) == 0xC0) {
        acc = b0 & 0x1F;
        if (!consumeContinuation(acc, 1)) {
            return false;
        }
    } else if ((b0 & 0xF0) == 0xE0) {
        acc = b0 & 0x0F;
        if (!consumeContinuation(acc, 2)) {
            return false;
        }
    } else if ((b0 & 0xF8) == 0xF0) {
        acc = b0 & 0x07;
        if (!consumeContinuation(acc, 3)) {
            return false;
        }
    } else {
        return false;
    }
    out_cp = acc;
    return true;
}

bool buildTextLayoutInfo(const WinguiGlyphAtlasInfo& atlas_info,
                         const char* text_utf8,
                         float origin_x,
                         float origin_y,
                         TextLayoutInfo* out_info,
                         const char* caller) {
    if (!text_utf8 || !out_info) {
        wingui_set_last_error_string_internal((std::string(caller) + ": invalid arguments").c_str());
        return false;
    }
    if (atlas_info.cell_width <= 0.0f || atlas_info.cell_height <= 0.0f) {
        wingui_set_last_error_string_internal((std::string(caller) + ": invalid atlas info").c_str());
        return false;
    }

    TextLayoutInfo info{};
    info.insertion_points.emplace_back(origin_x, origin_y);
    info.lines.push_back(TextLayoutLine{0, 0, origin_y});

    const char* cursor = text_utf8;
    const char* end = cursor + std::strlen(text_utf8);
    float pen_x = origin_x;
    float pen_y = origin_y;
    float max_x = origin_x;
    uint32_t current_index = 0;
    uint32_t visible_on_line = 0;

    while (cursor < end) {
        uint32_t cp = 0;
        if (!decodeUtf8Codepoint(cursor, end, cp)) {
            break;
        }
        if (cp == '\r') {
            current_index += 1;
            info.insertion_points.emplace_back(pen_x, pen_y);
            continue;
        }
        if (cp == '\n') {
            max_x = std::max(max_x, pen_x);
            current_index += 1;
            pen_x = origin_x;
            pen_y += atlas_info.cell_height;
            visible_on_line = 0;
            info.insertion_points.emplace_back(pen_x, pen_y);
            info.lines.push_back(TextLayoutLine{current_index, 0, pen_y});
            continue;
        }

        pen_x += atlas_info.cell_width;
        max_x = std::max(max_x, pen_x);
        current_index += 1;
        visible_on_line += 1;
        info.lines.back().length = visible_on_line;
        info.insertion_points.emplace_back(pen_x, pen_y);
    }

    info.char_count = current_index;
    info.width = std::max(0.0f, max_x - origin_x);
    info.height = std::max(atlas_info.cell_height, (pen_y - origin_y) + atlas_info.cell_height);
    *out_info = std::move(info);
    wingui_clear_last_error_internal();
    return true;
}

bool getFrameGlyphAtlasInfo(const WinguiSpecBindFrameView* frame_view,
                           WinguiGlyphAtlasInfo* out_info,
                           const char* caller) {
    if (!validateFrameView(frame_view, caller) || !out_info) {
        return false;
    }
    if (!super_terminal_get_glyph_atlas_info(frame_view->ctx, out_info)) {
        wingui_set_last_error_string_internal((std::string(caller) + ": glyph atlas query failed").c_str());
        return false;
    }
    return true;
}

template <typename T>
void releaseCom(T*& ptr) {
    if (ptr) {
        ptr->Release();
        ptr = nullptr;
    }
}

struct SurfaceTextConfig {
    std::wstring font_family = L"Consolas";
    float font_size = 16.0f;
    float dpi_scale = 1.0f;
};

struct SurfaceDecodedText {
    std::wstring wide_text;
    std::vector<uint32_t> char_to_utf16;
    uint32_t char_count = 0;
};

IDWriteFactory* getSharedDWriteFactory() {
    static std::once_flag once;
    static IDWriteFactory* factory = nullptr;
    static HRESULT init_hr = E_FAIL;
    std::call_once(once, []() {
        init_hr = DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED,
            __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(&factory));
    });
    return SUCCEEDED(init_hr) ? factory : nullptr;
}

std::wstring utf8ToWideText(const char* text_utf8) {
    if (!text_utf8 || !*text_utf8) {
        return {};
    }
    const int input_len = static_cast<int>(std::strlen(text_utf8));
    const int needed = MultiByteToWideChar(CP_UTF8, 0, text_utf8, input_len, nullptr, 0);
    if (needed <= 0) {
        return {};
    }
    std::wstring result(static_cast<size_t>(needed), L'\0');
    const int written = MultiByteToWideChar(CP_UTF8, 0, text_utf8, input_len, result.data(), needed);
    if (written <= 0) {
        return {};
    }
    return result;
}

bool decodeSurfaceText(const char* text_utf8,
                       SurfaceDecodedText* out_decoded,
                       const char* caller) {
    if (!text_utf8 || !out_decoded) {
        wingui_set_last_error_string_internal((std::string(caller) + ": invalid arguments").c_str());
        return false;
    }

    SurfaceDecodedText decoded{};
    decoded.wide_text = utf8ToWideText(text_utf8);
    decoded.char_to_utf16.push_back(0);

    for (size_t index = 0; index < decoded.wide_text.size();) {
        const wchar_t ch = decoded.wide_text[index];
        size_t advance = 1;
        if (ch >= 0xD800 && ch <= 0xDBFF) {
            if (index + 1 < decoded.wide_text.size()) {
                const wchar_t next = decoded.wide_text[index + 1];
                if (next >= 0xDC00 && next <= 0xDFFF) {
                    advance = 2;
                }
            }
        }
        index += advance;
        decoded.char_to_utf16.push_back(static_cast<uint32_t>(index));
        decoded.char_count += 1;
    }

    *out_decoded = std::move(decoded);
    return true;
}

uint32_t charIndexFromUtf16Position(const SurfaceDecodedText& decoded, uint32_t utf16_position) {
    const uint32_t clamped = std::min<uint32_t>(utf16_position, static_cast<uint32_t>(decoded.wide_text.size()));
    const auto it = std::upper_bound(decoded.char_to_utf16.begin(), decoded.char_to_utf16.end(), clamped);
    if (it == decoded.char_to_utf16.begin()) {
        return 0;
    }
    return static_cast<uint32_t>((it - decoded.char_to_utf16.begin()) - 1);
}

bool getSurfaceTextConfig(const WinguiSpecBindFrameView* frame_view,
                          SurfaceTextConfig* out_config,
                          const char* caller) {
    if (!validateFrameView(frame_view, caller) || !out_config) {
        return false;
    }

    const char* family_utf8 = nullptr;
    int32_t font_pixel_height = 0;
    float dpi_scale = 1.0f;
    if (!super_terminal_get_text_config(frame_view->ctx, &family_utf8, &font_pixel_height, &dpi_scale)) {
        wingui_set_last_error_string_internal((std::string(caller) + ": text config query failed").c_str());
        return false;
    }

    SurfaceTextConfig config{};
    config.font_family = utf8ToWideText((family_utf8 && *family_utf8) ? family_utf8 : "Consolas");
    if (config.font_family.empty()) {
        config.font_family = L"Consolas";
    }
    config.dpi_scale = dpi_scale > 0.0f ? dpi_scale : 1.0f;
    config.font_size = static_cast<float>(font_pixel_height > 0 ? font_pixel_height : 16) * config.dpi_scale;
    *out_config = std::move(config);
    return true;
}

bool createSurfaceTextLayout(const WinguiSpecBindFrameView* frame_view,
                             const char* text_utf8,
                             IDWriteTextLayout** out_layout,
                             SurfaceDecodedText* out_decoded,
                             const char* caller) {
    if (!out_layout || !out_decoded) {
        wingui_set_last_error_string_internal((std::string(caller) + ": invalid outputs").c_str());
        return false;
    }
    *out_layout = nullptr;

    IDWriteFactory* factory = getSharedDWriteFactory();
    if (!factory) {
        wingui_set_last_error_string_internal((std::string(caller) + ": DirectWrite factory unavailable").c_str());
        return false;
    }

    SurfaceTextConfig config{};
    if (!getSurfaceTextConfig(frame_view, &config, caller)) {
        return false;
    }

    SurfaceDecodedText decoded{};
    if (!decodeSurfaceText(text_utf8, &decoded, caller)) {
        return false;
    }

    IDWriteTextFormat* format = nullptr;
    HRESULT hr = factory->CreateTextFormat(
        config.font_family.c_str(),
        nullptr,
        DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        config.font_size,
        L"en-us",
        &format);
    if (FAILED(hr)) {
        wingui_set_last_error_hresult_internal((std::string(caller) + ": CreateTextFormat failed").c_str(), hr);
        return false;
    }

    format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

    const FLOAT max_width = 32768.0f;
    const FLOAT max_height = 32768.0f;
    IDWriteTextLayout* layout = nullptr;
    hr = factory->CreateTextLayout(
        decoded.wide_text.c_str(),
        static_cast<UINT32>(decoded.wide_text.size()),
        format,
        max_width,
        max_height,
        &layout);
    releaseCom(format);
    if (FAILED(hr)) {
        wingui_set_last_error_hresult_internal((std::string(caller) + ": CreateTextLayout failed").c_str(), hr);
        return false;
    }

    *out_layout = layout;
    *out_decoded = std::move(decoded);
    return true;
}

int32_t publishStoredSpec(WinguiSpecBindRuntime* runtime, SuperTerminalClientContext* ctx) {
    std::string spec_json;
    {
        std::lock_guard<std::mutex> lock(runtime->mutex);
        spec_json = runtime->spec_json;
    }

    if (spec_json.empty()) {
        fprintf(stderr, "[spec_bind] publishStoredSpec: no spec JSON stored — cannot publish\n");
        fflush(stderr);
        wingui_set_last_error_string_internal("wingui_spec_bind_runtime_run: no spec JSON has been loaded");
        return 0;
    }

    fprintf(stderr, "[spec_bind] publishStoredSpec: publishing spec (len=%zu): %.300s%s\n",
        spec_json.size(), spec_json.c_str(),
        spec_json.size() > 300 ? "..." : "");
    fflush(stderr);

    if (!super_terminal_publish_ui_json(ctx, spec_json.c_str())) {
        fprintf(stderr, "[spec_bind] publishStoredSpec: super_terminal_publish_ui_json failed\n");
        fflush(stderr);
        wingui_set_last_error_string_internal("wingui_spec_bind_runtime_run: failed to publish initial UI JSON");
        return 0;
    }

    fprintf(stderr, "[spec_bind] publishStoredSpec: OK\n");
    fflush(stderr);
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
    g_spec_bind_active_runtime = runtime;
    return publishStoredSpec(runtime, ctx);
}

void dispatchBoundEvent(
    WinguiSpecBindRuntime* runtime,
    SuperTerminalWindowId window_id,
    const char* event_name_utf8,
    const char* payload_json_utf8,
    const char* source_utf8) {
    const BoundHandler bound = findBoundHandler(runtime, event_name_utf8);
    if (!bound.handler) {
        return;
    }

    WinguiSpecBindEventView view{};
    view.window_id = window_id;
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
            event->window_id,
            event_name.empty() ? nullptr : event_name.c_str(),
            payload_json_utf8,
            source_name.empty() ? nullptr : source_name.c_str());
        return;
    }

    if (event->type == SUPERTERMINAL_EVENT_CLOSE_REQUESTED) {
        const BoundHandler bound = findBoundHandler(runtime, kCloseRequestedEventName);
        if (bound.handler) {
            WinguiSpecBindEventView view{};
            view.window_id = event->window_id;
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
        dispatchBoundEvent(runtime, event->window_id, kHostStoppingEventName, payload.c_str(), "host");
    }
}

void WINGUI_CALL runtimeOnFrame(
    SuperTerminalClientContext* ctx,
    const SuperTerminalFrameTick* tick,
    void* user_data) {
    auto* runtime = static_cast<WinguiSpecBindRuntime*>(user_data);
    if (!ctx || !tick || !runtime) {
        return;
    }

    const BoundFrameHandler bound = findFrameHandler(runtime);
    if (!bound.handler) {
        return;
    }

    WinguiSpecBindFrameView frame_view{};
    frame_view.ctx = ctx;
    frame_view.tick = tick;
    bound.handler(bound.user_data, runtime, &frame_view);
}

void WINGUI_CALL runtimeShutdown(void* user_data) {
    auto* runtime = static_cast<WinguiSpecBindRuntime*>(user_data);
    std::lock_guard<std::mutex> lock(runtime->mutex);
    runtime->active_context = nullptr;
    runtime->running = false;
    g_spec_bind_active_runtime = nullptr;
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

    fprintf(stderr, "[spec_bind] load_spec_json: stored spec (len=%zu) active_ctx=%s\n",
        publish_json.size(), active_context ? "yes" : "no (will publish on run)");
    fflush(stderr);

    if (active_context) {
        fprintf(stderr, "[spec_bind] load_spec_json: live publish/patch: %.300s%s\n",
            publish_json.c_str(), publish_json.size() > 300 ? "..." : "");
        fflush(stderr);
        if (!publishOrPatchSpec(active_context, previous_json, publish_json)) {
            fprintf(stderr, "[spec_bind] load_spec_json: live publish/patch FAILED\n");
            fflush(stderr);
            wingui_set_last_error_string_internal("wingui_spec_bind_runtime_load_spec_json: live publish/patch failed");
            return 0;
        }
        fprintf(stderr, "[spec_bind] load_spec_json: live publish/patch OK\n");
        fflush(stderr);
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

extern "C" WINGUI_API void WINGUI_CALL wingui_spec_bind_runtime_set_frame_handler(
    WinguiSpecBindRuntime* runtime,
    WinguiSpecBindFrameHandlerFn handler,
    void* user_data) {
    if (!runtime) {
        return;
    }
    std::lock_guard<std::mutex> lock(runtime->mutex);
    runtime->frame_handler = handler;
    runtime->frame_user_data = user_data;
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

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_runtime_create_window(
    WinguiSpecBindRuntime* runtime,
    const SuperTerminalWindowDesc* desc,
    SuperTerminalWindowId* out_window_id) {
    if (!runtime || !desc || !out_window_id) {
        wingui_set_last_error_string_internal("wingui_spec_bind_runtime_create_window: invalid arguments");
        return 0;
    }

    SuperTerminalClientContext* active_context = nullptr;
    {
        std::lock_guard<std::mutex> lock(runtime->mutex);
        active_context = runtime->active_context;
    }
    if (!active_context) {
        wingui_set_last_error_string_internal("wingui_spec_bind_runtime_create_window: runtime is not active");
        return 0;
    }
    return super_terminal_create_window(active_context, desc, out_window_id);
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_runtime_close_window(
    WinguiSpecBindRuntime* runtime,
    SuperTerminalWindowId window_id) {
    if (!runtime || window_id.value == 0) {
        wingui_set_last_error_string_internal("wingui_spec_bind_runtime_close_window: invalid arguments");
        return 0;
    }

    SuperTerminalClientContext* active_context = nullptr;
    {
        std::lock_guard<std::mutex> lock(runtime->mutex);
        active_context = runtime->active_context;
    }
    if (!active_context) {
        wingui_set_last_error_string_internal("wingui_spec_bind_runtime_close_window: runtime is not active");
        return 0;
    }
    return super_terminal_close_window(active_context, window_id);
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

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_runtime_resolve_pane_id_utf8(
    WinguiSpecBindRuntime* runtime,
    const char* node_id_utf8,
    SuperTerminalPaneId* out_pane_id) {
    if (!runtime || !node_id_utf8 || !node_id_utf8[0] || !out_pane_id) {
        wingui_set_last_error_string_internal("wingui_spec_bind_runtime_resolve_pane_id_utf8: invalid arguments");
        return 0;
    }

    SuperTerminalClientContext* active_context = nullptr;
    {
        std::lock_guard<std::mutex> lock(runtime->mutex);
        active_context = runtime->active_context;
    }
    if (!active_context) {
        wingui_set_last_error_string_internal("wingui_spec_bind_runtime_resolve_pane_id_utf8: runtime is not active");
        return 0;
    }
    if (!super_terminal_resolve_pane_id_utf8(active_context, node_id_utf8, out_pane_id)) {
        wingui_set_last_error_string_internal("wingui_spec_bind_runtime_resolve_pane_id_utf8: pane resolution failed");
        return 0;
    }

    wingui_clear_last_error_internal();
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_runtime_resolve_pane_id_for_window(
    WinguiSpecBindRuntime* runtime,
    SuperTerminalWindowId window_id,
    const char* node_id_utf8,
    SuperTerminalPaneId* out_pane_id) {
    if (!runtime || window_id.value == 0 || !node_id_utf8 || !node_id_utf8[0] || !out_pane_id) {
        wingui_set_last_error_string_internal("wingui_spec_bind_runtime_resolve_pane_id_for_window: invalid arguments");
        return 0;
    }

    SuperTerminalClientContext* active_context = nullptr;
    {
        std::lock_guard<std::mutex> lock(runtime->mutex);
        active_context = runtime->active_context;
    }
    if (!active_context) {
        wingui_set_last_error_string_internal("wingui_spec_bind_runtime_resolve_pane_id_for_window: runtime is not active");
        return 0;
    }
    if (!super_terminal_resolve_pane_id_for_window(active_context, window_id, node_id_utf8, out_pane_id)) {
        wingui_set_last_error_string_internal("wingui_spec_bind_runtime_resolve_pane_id_for_window: pane resolution failed");
        return 0;
    }

    wingui_clear_last_error_internal();
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_runtime_text_grid_write_cells(
    WinguiSpecBindRuntime* runtime,
    SuperTerminalPaneId pane_id,
    const SuperTerminalTextGridCell* cells,
    uint32_t cell_count) {
    if (!runtime) {
        wingui_set_last_error_string_internal("wingui_spec_bind_runtime_text_grid_write_cells: runtime was null");
        return 0;
    }

    SuperTerminalClientContext* active_context = nullptr;
    {
        std::lock_guard<std::mutex> lock(runtime->mutex);
        active_context = runtime->active_context;
    }
    if (!active_context) {
        wingui_set_last_error_string_internal("wingui_spec_bind_runtime_text_grid_write_cells: runtime is not active");
        return 0;
    }
    if (!super_terminal_text_grid_write_cells(active_context, pane_id, cells, cell_count)) {
        wingui_set_last_error_string_internal("wingui_spec_bind_runtime_text_grid_write_cells: write failed");
        return 0;
    }

    wingui_clear_last_error_internal();
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_runtime_text_grid_clear_region(
    WinguiSpecBindRuntime* runtime,
    SuperTerminalPaneId pane_id,
    uint32_t row,
    uint32_t column,
    uint32_t width,
    uint32_t height,
    uint32_t fill_codepoint,
    WinguiGraphicsColour foreground,
    WinguiGraphicsColour background) {
    if (!runtime) {
        wingui_set_last_error_string_internal("wingui_spec_bind_runtime_text_grid_clear_region: runtime was null");
        return 0;
    }

    SuperTerminalClientContext* active_context = nullptr;
    {
        std::lock_guard<std::mutex> lock(runtime->mutex);
        active_context = runtime->active_context;
    }
    if (!active_context) {
        wingui_set_last_error_string_internal("wingui_spec_bind_runtime_text_grid_clear_region: runtime is not active");
        return 0;
    }
    if (!super_terminal_text_grid_clear_region(
            active_context,
            pane_id,
            row,
            column,
            width,
            height,
            fill_codepoint,
            foreground,
            background)) {
        wingui_set_last_error_string_internal("wingui_spec_bind_runtime_text_grid_clear_region: clear failed");
        return 0;
    }

    wingui_clear_last_error_internal();
    return 1;
}

extern "C" WINGUI_API uint64_t WINGUI_CALL wingui_spec_bind_frame_index(
    const WinguiSpecBindFrameView* frame_view) {
    return (frame_view && frame_view->tick) ? frame_view->tick->frame_index : 0;
}

extern "C" WINGUI_API uint64_t WINGUI_CALL wingui_spec_bind_frame_elapsed_ms(
    const WinguiSpecBindFrameView* frame_view) {
    return (frame_view && frame_view->tick) ? frame_view->tick->elapsed_ms : 0;
}

extern "C" WINGUI_API uint64_t WINGUI_CALL wingui_spec_bind_frame_delta_ms(
    const WinguiSpecBindFrameView* frame_view) {
    return (frame_view && frame_view->tick) ? frame_view->tick->delta_ms : 0;
}

extern "C" WINGUI_API uint32_t WINGUI_CALL wingui_spec_bind_frame_target_frame_ms(
    const WinguiSpecBindFrameView* frame_view) {
    return (frame_view && frame_view->tick) ? frame_view->tick->target_frame_ms : 0;
}

extern "C" WINGUI_API uint32_t WINGUI_CALL wingui_spec_bind_frame_buffer_index(
    const WinguiSpecBindFrameView* frame_view) {
    return (frame_view && frame_view->tick) ? frame_view->tick->buffer_index : 0;
}

extern "C" WINGUI_API uint32_t WINGUI_CALL wingui_spec_bind_frame_active_buffer_index(
    const WinguiSpecBindFrameView* frame_view) {
    return (frame_view && frame_view->tick) ? frame_view->tick->active_buffer_index : 0;
}

extern "C" WINGUI_API uint32_t WINGUI_CALL wingui_spec_bind_frame_buffer_count(
    const WinguiSpecBindFrameView* frame_view) {
    return (frame_view && frame_view->tick) ? frame_view->tick->buffer_count : 0;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_get_key_state(
    const WinguiSpecBindFrameView* frame_view,
    uint32_t virtual_key) {
    if (!validateFrameView(frame_view, "wingui_spec_bind_frame_get_key_state")) {
        return 0;
    }
    return super_terminal_get_key_state(frame_view->ctx, virtual_key);
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_get_keyboard_state(
    const WinguiSpecBindFrameView* frame_view,
    WinguiKeyboardState* out_state) {
    if (!validateFrameView(frame_view, "wingui_spec_bind_frame_get_keyboard_state")) {
        return 0;
    }
    return super_terminal_get_keyboard_state(frame_view->ctx, out_state);
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_resolve_pane_utf8(
    const WinguiSpecBindFrameView* frame_view,
    const char* node_id_utf8,
    WinguiSpecBindPaneRef* out_pane) {
    if (!validateFrameView(frame_view, "wingui_spec_bind_frame_resolve_pane_utf8")) {
        return 0;
    }
    if (!node_id_utf8 || !node_id_utf8[0] || !out_pane) {
        wingui_set_last_error_string_internal("wingui_spec_bind_frame_resolve_pane_utf8: invalid arguments");
        return 0;
    }

    SuperTerminalPaneId pane_id{};
    if (!super_terminal_resolve_pane_id_utf8(frame_view->ctx, node_id_utf8, &pane_id)) {
        wingui_set_last_error_string_internal("wingui_spec_bind_frame_resolve_pane_utf8: pane resolution failed");
        return 0;
    }

    return bindPaneRef(frame_view, pane_id, out_pane, "wingui_spec_bind_frame_resolve_pane_utf8") ? 1 : 0;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_bind_pane(
    const WinguiSpecBindFrameView* frame_view,
    SuperTerminalPaneId pane_id,
    WinguiSpecBindPaneRef* out_pane) {
    return bindPaneRef(frame_view, pane_id, out_pane, "wingui_spec_bind_frame_bind_pane") ? 1 : 0;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_get_pane_layout(
    const WinguiSpecBindFrameView* frame_view,
    WinguiSpecBindPaneRef pane,
    SuperTerminalPaneLayout* out_layout) {
    if (!validateFrameView(frame_view, "wingui_spec_bind_frame_get_pane_layout") ||
        !validatePaneRef(pane, "wingui_spec_bind_frame_get_pane_layout") ||
        !out_layout) {
        if (out_layout == nullptr) {
            wingui_set_last_error_string_internal("wingui_spec_bind_frame_get_pane_layout: out_layout was null");
        }
        return 0;
    }
    if (!super_terminal_get_pane_layout(frame_view->ctx, pane.pane_id, out_layout)) {
        wingui_set_last_error_string_internal("wingui_spec_bind_frame_get_pane_layout: query failed");
        return 0;
    }
    wingui_clear_last_error_internal();
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_request_present(
    const WinguiSpecBindFrameView* frame_view) {
    if (!validateFrameView(frame_view, "wingui_spec_bind_frame_request_present")) {
        return 0;
    }
    if (!super_terminal_request_present(frame_view->ctx)) {
        wingui_set_last_error_string_internal("wingui_spec_bind_frame_request_present: request failed");
        return 0;
    }
    wingui_clear_last_error_internal();
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_get_glyph_atlas_info(
    const WinguiSpecBindFrameView* frame_view,
    WinguiGlyphAtlasInfo* out_info) {
    if (!validateFrameView(frame_view, "wingui_spec_bind_frame_get_glyph_atlas_info") || !out_info) {
        if (!out_info) {
            wingui_set_last_error_string_internal("wingui_spec_bind_frame_get_glyph_atlas_info: out_info was null");
        }
        return 0;
    }
    if (!super_terminal_get_glyph_atlas_info(frame_view->ctx, out_info)) {
        wingui_set_last_error_string_internal("wingui_spec_bind_frame_get_glyph_atlas_info: query failed");
        return 0;
    }
    wingui_clear_last_error_internal();
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_text_grid_write_cells(
    const WinguiSpecBindFrameView* frame_view,
    WinguiSpecBindPaneRef pane,
    const SuperTerminalTextGridCell* cells,
    uint32_t cell_count) {
    if (!validateFrameView(frame_view, "wingui_spec_bind_frame_text_grid_write_cells") ||
        !validatePaneRef(pane, "wingui_spec_bind_frame_text_grid_write_cells")) {
        return 0;
    }
    if (!super_terminal_frame_text_grid_write_cells(frame_view->ctx, frame_view->tick, pane.pane_id, cells, cell_count)) {
        wingui_set_last_error_string_internal("wingui_spec_bind_frame_text_grid_write_cells: write failed");
        return 0;
    }
    wingui_clear_last_error_internal();
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_text_grid_clear_region(
    const WinguiSpecBindFrameView* frame_view,
    WinguiSpecBindPaneRef pane,
    uint32_t row,
    uint32_t column,
    uint32_t width,
    uint32_t height,
    uint32_t fill_codepoint,
    WinguiGraphicsColour foreground,
    WinguiGraphicsColour background) {
    if (!validateFrameView(frame_view, "wingui_spec_bind_frame_text_grid_clear_region") ||
        !validatePaneRef(pane, "wingui_spec_bind_frame_text_grid_clear_region")) {
        return 0;
    }
    if (!super_terminal_frame_text_grid_clear_region(
            frame_view->ctx,
            frame_view->tick,
            pane.pane_id,
            row,
            column,
            width,
            height,
            fill_codepoint,
            foreground,
            background)) {
        wingui_set_last_error_string_internal("wingui_spec_bind_frame_text_grid_clear_region: clear failed");
        return 0;
    }
    wingui_clear_last_error_internal();
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_indexed_graphics_upload(
    const WinguiSpecBindFrameView* frame_view,
    WinguiSpecBindPaneRef pane,
    const SuperTerminalIndexedGraphicsFrame* frame) {
    if (!validateFrameView(frame_view, "wingui_spec_bind_frame_indexed_graphics_upload") ||
        !validatePaneRef(pane, "wingui_spec_bind_frame_indexed_graphics_upload")) {
        return 0;
    }
    if (!super_terminal_frame_indexed_graphics_upload(frame_view->ctx, frame_view->tick, pane.pane_id, frame)) {
        wingui_set_last_error_string_internal("wingui_spec_bind_frame_indexed_graphics_upload: upload failed");
        return 0;
    }
    wingui_clear_last_error_internal();
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_rgba_upload(
    const WinguiSpecBindFrameView* frame_view,
    WinguiSpecBindPaneRef pane,
    const SuperTerminalRgbaFrame* frame) {
    if (!validateFrameView(frame_view, "wingui_spec_bind_frame_rgba_upload") ||
        !validatePaneRef(pane, "wingui_spec_bind_frame_rgba_upload")) {
        return 0;
    }
    if (!super_terminal_frame_rgba_upload(frame_view->ctx, frame_view->tick, pane.pane_id, frame)) {
        wingui_set_last_error_string_internal("wingui_spec_bind_frame_rgba_upload: upload failed");
        return 0;
    }
    wingui_clear_last_error_internal();
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_rgba_gpu_copy(
    const WinguiSpecBindFrameView* frame_view,
    WinguiSpecBindPaneRef dst_pane,
    uint32_t dst_x,
    uint32_t dst_y,
    WinguiSpecBindPaneRef src_pane,
    uint32_t src_x,
    uint32_t src_y,
    uint32_t region_width,
    uint32_t region_height) {
    if (!validateFrameView(frame_view, "wingui_spec_bind_frame_rgba_gpu_copy") ||
        !validatePaneRef(dst_pane, "wingui_spec_bind_frame_rgba_gpu_copy") ||
        !validatePaneRef(src_pane, "wingui_spec_bind_frame_rgba_gpu_copy")) {
        return 0;
    }
    if (!super_terminal_rgba_gpu_copy(
            frame_view->ctx,
            dst_pane.pane_id,
            dst_pane.buffer_index,
            dst_x,
            dst_y,
            src_pane.pane_id,
            src_pane.buffer_index,
            src_x,
            src_y,
            region_width,
            region_height)) {
        wingui_set_last_error_string_internal("wingui_spec_bind_frame_rgba_gpu_copy: copy failed");
        return 0;
    }
    wingui_clear_last_error_internal();
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_register_rgba_asset_owned(
    const WinguiSpecBindFrameView* frame_view,
    uint32_t width,
    uint32_t height,
    void* bgra8_pixels,
    uint32_t source_pitch,
    SuperTerminalFreeFn free_fn,
    void* free_user_data,
    SuperTerminalAssetId* out_asset_id) {
    if (!validateFrameView(frame_view, "wingui_spec_bind_frame_register_rgba_asset_owned")) {
        return 0;
    }
    if (!super_terminal_register_rgba_asset_owned(
            frame_view->ctx,
            width,
            height,
            bgra8_pixels,
            source_pitch,
            free_fn,
            free_user_data,
            out_asset_id)) {
        wingui_set_last_error_string_internal("wingui_spec_bind_frame_register_rgba_asset_owned: register failed");
        return 0;
    }
    wingui_clear_last_error_internal();
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_asset_blit_to_pane(
    const WinguiSpecBindFrameView* frame_view,
    SuperTerminalAssetId asset_id,
    uint32_t src_x,
    uint32_t src_y,
    uint32_t region_width,
    uint32_t region_height,
    WinguiSpecBindPaneRef dst_pane,
    uint32_t dst_x,
    uint32_t dst_y) {
    if (!validateFrameView(frame_view, "wingui_spec_bind_frame_asset_blit_to_pane") ||
        !validatePaneRef(dst_pane, "wingui_spec_bind_frame_asset_blit_to_pane")) {
        return 0;
    }
    if (!super_terminal_asset_blit_to_pane(
            frame_view->ctx,
            asset_id,
            src_x,
            src_y,
            region_width,
            region_height,
            dst_pane.pane_id,
            dst_pane.buffer_index,
            dst_x,
            dst_y)) {
        wingui_set_last_error_string_internal("wingui_spec_bind_frame_asset_blit_to_pane: blit failed");
        return 0;
    }
    wingui_clear_last_error_internal();
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_define_sprite(
    const WinguiSpecBindFrameView* frame_view,
    WinguiSpecBindPaneRef pane,
    SuperTerminalSpriteId sprite_id,
    uint32_t frame_w,
    uint32_t frame_h,
    uint32_t frame_count,
    uint32_t frames_per_tick,
    void* pixels,
    void* palette,
    SuperTerminalFreeFn free_fn,
    void* free_user_data) {
    if (!validateFrameView(frame_view, "wingui_spec_bind_frame_define_sprite") ||
        !validatePaneRef(pane, "wingui_spec_bind_frame_define_sprite")) {
        return 0;
    }
    if (!super_terminal_define_sprite(
            frame_view->ctx,
            pane.pane_id,
            sprite_id,
            frame_w,
            frame_h,
            frame_count,
            frames_per_tick,
            pixels,
            palette,
            free_fn,
            free_user_data)) {
        wingui_set_last_error_string_internal("wingui_spec_bind_frame_define_sprite: define failed");
        return 0;
    }
    wingui_clear_last_error_internal();
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_render_sprites(
    const WinguiSpecBindFrameView* frame_view,
    WinguiSpecBindPaneRef pane,
    uint64_t sprite_tick,
    uint32_t target_width,
    uint32_t target_height,
    const SuperTerminalSpriteInstance* instances,
    uint32_t instance_count) {
    if (!validateFrameView(frame_view, "wingui_spec_bind_frame_render_sprites") ||
        !validatePaneRef(pane, "wingui_spec_bind_frame_render_sprites")) {
        return 0;
    }
    if (!super_terminal_render_sprites(
            frame_view->ctx,
            pane.pane_id,
            sprite_tick,
            target_width,
            target_height,
            instances,
            instance_count)) {
        wingui_set_last_error_string_internal("wingui_spec_bind_frame_render_sprites: render failed");
        return 0;
    }
    wingui_clear_last_error_internal();
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_vector_draw(
    const WinguiSpecBindFrameView* frame_view,
    WinguiSpecBindPaneRef pane,
    uint32_t content_buffer_mode,
    uint32_t blend_mode,
    int32_t clear_before,
    const float clear_color_rgba[4],
    const WinguiVectorPrimitive* primitives,
    uint32_t primitive_count) {
    if (!validateFrameView(frame_view, "wingui_spec_bind_frame_vector_draw") ||
        !validatePaneRef(pane, "wingui_spec_bind_frame_vector_draw")) {
        return 0;
    }
    if (!super_terminal_vector_draw(
            frame_view->ctx,
            pane.pane_id,
            pane.buffer_index,
            content_buffer_mode,
            blend_mode,
            clear_before,
            clear_color_rgba,
            primitives,
            primitive_count)) {
        wingui_set_last_error_string_internal("wingui_spec_bind_frame_vector_draw: draw failed");
        return 0;
    }
    wingui_clear_last_error_internal();
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_draw_line(
    const WinguiSpecBindFrameView* frame_view,
    WinguiSpecBindPaneRef pane,
    uint32_t content_buffer_mode,
    uint32_t blend_mode,
    int32_t clear_before,
    const float clear_color_rgba[4],
    float x0,
    float y0,
    float x1,
    float y1,
    float half_thickness,
    float color_r,
    float color_g,
    float color_b,
    float color_a) {
    const WinguiSurfacePrimitive primitive = makeLineSurfacePrimitive(
        x0, y0, x1, y1, half_thickness, color_r, color_g, color_b, color_a);
    return drawSingleSurfacePrimitive(frame_view, pane, content_buffer_mode, blend_mode, clear_before, clear_color_rgba, primitive, "wingui_spec_bind_frame_draw_line");
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_fill_rect(
    const WinguiSpecBindFrameView* frame_view,
    WinguiSpecBindPaneRef pane,
    uint32_t content_buffer_mode,
    uint32_t blend_mode,
    int32_t clear_before,
    const float clear_color_rgba[4],
    float x0,
    float y0,
    float x1,
    float y1,
    float corner_radius,
    float color_r,
    float color_g,
    float color_b,
    float color_a) {
    WinguiSurfacePrimitive primitive = makeSurfacePrimitiveBase(color_r, color_g, color_b, color_a, WINGUI_SURFACE_FILL_RECT);
    primitive.param0[0] = x0;
    primitive.param0[1] = y0;
    primitive.param0[2] = x1;
    primitive.param0[3] = y1;
    primitive.param1[0] = corner_radius;
    return drawSingleSurfacePrimitive(frame_view, pane, content_buffer_mode, blend_mode, clear_before, clear_color_rgba, primitive, "wingui_spec_bind_frame_fill_rect");
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_stroke_rect(
    const WinguiSpecBindFrameView* frame_view,
    WinguiSpecBindPaneRef pane,
    uint32_t content_buffer_mode,
    uint32_t blend_mode,
    int32_t clear_before,
    const float clear_color_rgba[4],
    float x0,
    float y0,
    float x1,
    float y1,
    float half_thickness,
    float corner_radius,
    float color_r,
    float color_g,
    float color_b,
    float color_a) {
    WinguiSurfacePrimitive primitive = makeSurfacePrimitiveBase(color_r, color_g, color_b, color_a, WINGUI_SURFACE_STROKE_RECT);
    primitive.param0[0] = x0;
    primitive.param0[1] = y0;
    primitive.param0[2] = x1;
    primitive.param0[3] = y1;
    primitive.param1[0] = corner_radius;
    primitive.param1[1] = half_thickness;
    return drawSingleSurfacePrimitive(frame_view, pane, content_buffer_mode, blend_mode, clear_before, clear_color_rgba, primitive, "wingui_spec_bind_frame_stroke_rect");
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_fill_circle(
    const WinguiSpecBindFrameView* frame_view,
    WinguiSpecBindPaneRef pane,
    uint32_t content_buffer_mode,
    uint32_t blend_mode,
    int32_t clear_before,
    const float clear_color_rgba[4],
    float cx,
    float cy,
    float radius,
    float color_r,
    float color_g,
    float color_b,
    float color_a) {
    WinguiSurfacePrimitive primitive = makeSurfacePrimitiveBase(color_r, color_g, color_b, color_a, WINGUI_SURFACE_FILL_ELLIPSE);
    primitive.param0[0] = cx;
    primitive.param0[1] = cy;
    primitive.param0[2] = radius;
    primitive.param0[3] = radius;
    return drawSingleSurfacePrimitive(frame_view, pane, content_buffer_mode, blend_mode, clear_before, clear_color_rgba, primitive, "wingui_spec_bind_frame_fill_circle");
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_stroke_circle(
    const WinguiSpecBindFrameView* frame_view,
    WinguiSpecBindPaneRef pane,
    uint32_t content_buffer_mode,
    uint32_t blend_mode,
    int32_t clear_before,
    const float clear_color_rgba[4],
    float cx,
    float cy,
    float radius,
    float half_thickness,
    float color_r,
    float color_g,
    float color_b,
    float color_a) {
    WinguiSurfacePrimitive primitive = makeSurfacePrimitiveBase(color_r, color_g, color_b, color_a, WINGUI_SURFACE_STROKE_ELLIPSE);
    primitive.param0[0] = cx;
    primitive.param0[1] = cy;
    primitive.param0[2] = radius;
    primitive.param0[3] = radius;
    primitive.param1[0] = half_thickness;
    return drawSingleSurfacePrimitive(frame_view, pane, content_buffer_mode, blend_mode, clear_before, clear_color_rgba, primitive, "wingui_spec_bind_frame_stroke_circle");
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_draw_arc(
    const WinguiSpecBindFrameView* frame_view,
    WinguiSpecBindPaneRef pane,
    uint32_t content_buffer_mode,
    uint32_t blend_mode,
    int32_t clear_before,
    const float clear_color_rgba[4],
    float cx,
    float cy,
    float radius,
    float half_thickness,
    float rotation_rad,
    float half_aperture_rad,
    float color_r,
    float color_g,
    float color_b,
    float color_a) {
    WinguiSurfacePrimitive primitive = makeSurfacePrimitiveBase(color_r, color_g, color_b, color_a, WINGUI_SURFACE_DRAW_ARC);
    primitive.param0[0] = cx;
    primitive.param0[1] = cy;
    primitive.param0[2] = radius;
    primitive.param0[3] = half_thickness;
    primitive.param1[0] = rotation_rad;
    primitive.param1[1] = half_aperture_rad;
    return drawSingleSurfacePrimitive(frame_view, pane, content_buffer_mode, blend_mode, clear_before, clear_color_rgba, primitive, "wingui_spec_bind_frame_draw_arc");
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_fill_oval(
    const WinguiSpecBindFrameView* frame_view,
    WinguiSpecBindPaneRef pane,
    uint32_t content_buffer_mode,
    uint32_t blend_mode,
    int32_t clear_before,
    const float clear_color_rgba[4],
    float x0,
    float y0,
    float x1,
    float y1,
    float color_r,
    float color_g,
    float color_b,
    float color_a) {
    if (!validateFrameView(frame_view, "wingui_spec_bind_frame_fill_oval") ||
        !validatePaneRef(pane, "wingui_spec_bind_frame_fill_oval")) {
        return 0;
    }

    const float left = std::min(x0, x1);
    const float top = std::min(y0, y1);
    const float right = std::max(x0, x1);
    const float bottom = std::max(y0, y1);
    const float radius_x = (right - left) * 0.5f;
    const float radius_y = (bottom - top) * 0.5f;
    if (radius_x <= 0.0f || radius_y <= 0.0f) {
        wingui_clear_last_error_internal();
        return 1;
    }

    WinguiSurfacePrimitive primitive = makeSurfacePrimitiveBase(color_r, color_g, color_b, color_a, WINGUI_SURFACE_FILL_ELLIPSE);
    primitive.param0[0] = left + radius_x;
    primitive.param0[1] = top + radius_y;
    primitive.param0[2] = radius_x;
    primitive.param0[3] = radius_y;
    return drawSingleSurfacePrimitive(frame_view, pane, content_buffer_mode, blend_mode, clear_before, clear_color_rgba, primitive, "wingui_spec_bind_frame_fill_oval");
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_stroke_oval(
    const WinguiSpecBindFrameView* frame_view,
    WinguiSpecBindPaneRef pane,
    uint32_t content_buffer_mode,
    uint32_t blend_mode,
    int32_t clear_before,
    const float clear_color_rgba[4],
    float x0,
    float y0,
    float x1,
    float y1,
    float half_thickness,
    float color_r,
    float color_g,
    float color_b,
    float color_a) {
    if (!validateFrameView(frame_view, "wingui_spec_bind_frame_stroke_oval") ||
        !validatePaneRef(pane, "wingui_spec_bind_frame_stroke_oval")) {
        return 0;
    }

    const float left = std::min(x0, x1);
    const float top = std::min(y0, y1);
    const float right = std::max(x0, x1);
    const float bottom = std::max(y0, y1);
    const float radius_x = (right - left) * 0.5f;
    const float radius_y = (bottom - top) * 0.5f;
    if (radius_x <= 0.0f || radius_y <= 0.0f) {
        wingui_clear_last_error_internal();
        return 1;
    }

    WinguiSurfacePrimitive primitive = makeSurfacePrimitiveBase(color_r, color_g, color_b, color_a, WINGUI_SURFACE_STROKE_ELLIPSE);
    primitive.param0[0] = left + radius_x;
    primitive.param0[1] = top + radius_y;
    primitive.param0[2] = radius_x;
    primitive.param0[3] = radius_y;
    primitive.param1[0] = half_thickness;
    return drawSingleSurfacePrimitive(frame_view, pane, content_buffer_mode, blend_mode, clear_before, clear_color_rgba, primitive, "wingui_spec_bind_frame_stroke_oval");
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_draw_path(
    const WinguiSpecBindFrameView* frame_view,
    WinguiSpecBindPaneRef pane,
    uint32_t content_buffer_mode,
    uint32_t blend_mode,
    int32_t clear_before,
    const float clear_color_rgba[4],
    const float* points_xy,
    uint32_t point_count,
    int32_t closed,
    float half_thickness,
    float color_r,
    float color_g,
    float color_b,
    float color_a) {
    if (!validateFrameView(frame_view, "wingui_spec_bind_frame_draw_path") ||
        !validatePaneRef(pane, "wingui_spec_bind_frame_draw_path")) {
        return 0;
    }
    if (point_count == 0) {
        wingui_clear_last_error_internal();
        return 1;
    }
    if (!points_xy) {
        wingui_set_last_error_string_internal("wingui_spec_bind_frame_draw_path: points_xy was null");
        return 0;
    }
    if (point_count < 2) {
        wingui_clear_last_error_internal();
        return 1;
    }

    std::vector<WinguiSurfacePrimitive> primitives;
    std::vector<float> path_buffer(points_xy, points_xy + static_cast<size_t>(point_count) * 2u);
    WinguiSurfacePrimitive primitive = makeSurfacePrimitiveBase(color_r, color_g, color_b, color_a, WINGUI_SURFACE_DRAW_PATH);
    primitive.flags = closed ? WINGUI_SURFACE_PATH_CLOSED : 0u;
    primitive.point_offset = 0;
    primitive.point_count = point_count;
    primitive.param1[0] = half_thickness;
    primitives.push_back(primitive);

    return drawSurfacePrimitiveBatch(frame_view, pane, content_buffer_mode, blend_mode, clear_before, clear_color_rgba, primitives, path_buffer, "wingui_spec_bind_frame_draw_path");
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_draw_text_utf8(
    const WinguiSpecBindFrameView* frame_view,
    WinguiSpecBindPaneRef pane,
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
    float color_a) {
    if (!validateFrameView(frame_view, "wingui_spec_bind_frame_draw_text_utf8") ||
        !validatePaneRef(pane, "wingui_spec_bind_frame_draw_text_utf8")) {
        return 0;
    }
    if (!text_utf8) {
        wingui_set_last_error_string_internal("wingui_spec_bind_frame_draw_text_utf8: text_utf8 was null");
        return 0;
    }

    if (!super_terminal_surface_draw_text_utf8(
            frame_view->ctx,
            pane.pane_id,
            pane.buffer_index,
            content_buffer_mode,
            blend_mode,
            clear_before,
            clear_color_rgba,
            text_utf8,
            origin_x,
            origin_y,
            color_r,
            color_g,
            color_b,
            color_a)) {
        wingui_set_last_error_string_internal("wingui_spec_bind_frame_draw_text_utf8: draw failed");
        return 0;
    }

    wingui_clear_last_error_internal();
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_mark_rect(
    const WinguiSpecBindFrameView* frame_view,
    WinguiSpecBindPaneRef pane,
    uint32_t content_buffer_mode,
    uint32_t blend_mode,
    int32_t clear_before,
    const float clear_color_rgba[4],
    int32_t mode,
    float x0,
    float y0,
    float x1,
    float y1) {
    float color_r = 0.30f;
    float color_g = 0.58f;
    float color_b = 1.0f;
    float color_a = 0.28f;
    if (mode == 1) {
        color_r = 1.0f;
        color_g = 1.0f;
        color_b = 1.0f;
        color_a = 0.25f;
    } else if (mode == 2) {
        color_r = 0.0f;
        color_g = 0.0f;
        color_b = 0.0f;
        color_a = 0.22f;
    }
    return wingui_spec_bind_frame_fill_rect(
        frame_view,
        pane,
        content_buffer_mode,
        blend_mode,
        clear_before,
        clear_color_rgba,
        x0,
        y0,
        x1,
        y1,
        0.0f,
        color_r,
        color_g,
        color_b,
        color_a);
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_caret(
    const WinguiSpecBindFrameView* frame_view,
    WinguiSpecBindPaneRef pane,
    uint32_t content_buffer_mode,
    uint32_t blend_mode,
    int32_t clear_before,
    const float clear_color_rgba[4],
    float x0,
    float y0,
    float x1,
    float y1,
    float color_r,
    float color_g,
    float color_b,
    float color_a) {
    return wingui_spec_bind_frame_fill_rect(
        frame_view,
        pane,
        content_buffer_mode,
        blend_mode,
        clear_before,
        clear_color_rgba,
        x0,
        y0,
        x1,
        y1,
        0.0f,
        color_r,
        color_g,
        color_b,
        color_a);
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_selection_range(
    const WinguiSpecBindFrameView* frame_view,
    WinguiSpecBindPaneRef pane,
    uint32_t content_buffer_mode,
    uint32_t blend_mode,
    int32_t clear_before,
    const float clear_color_rgba[4],
    float x0,
    float y0,
    float x1,
    float y1,
    float color_r,
    float color_g,
    float color_b,
    float color_a) {
    return wingui_spec_bind_frame_fill_rect(
        frame_view,
        pane,
        content_buffer_mode,
        blend_mode,
        clear_before,
        clear_color_rgba,
        x0,
        y0,
        x1,
        y1,
        0.0f,
        color_r,
        color_g,
        color_b,
        color_a);
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_focus_ring(
    const WinguiSpecBindFrameView* frame_view,
    WinguiSpecBindPaneRef pane,
    uint32_t content_buffer_mode,
    uint32_t blend_mode,
    int32_t clear_before,
    const float clear_color_rgba[4],
    float x0,
    float y0,
    float x1,
    float y1,
    float half_thickness,
    float corner_radius,
    float color_r,
    float color_g,
    float color_b,
    float color_a) {
    return wingui_spec_bind_frame_stroke_rect(
        frame_view,
        pane,
        content_buffer_mode,
        blend_mode,
        clear_before,
        clear_color_rgba,
        x0,
        y0,
        x1,
        y1,
        half_thickness,
        corner_radius,
        color_r,
        color_g,
        color_b,
        color_a);
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_surface_push_clip_rect(
    const WinguiSpecBindFrameView* frame_view,
    WinguiSpecBindPaneRef pane,
    float x0,
    float y0,
    float x1,
    float y1) {
    if (!validateFrameView(frame_view, "wingui_spec_bind_frame_surface_push_clip_rect") ||
        !validatePaneRef(pane, "wingui_spec_bind_frame_surface_push_clip_rect")) {
        return 0;
    }
    if (!super_terminal_surface_push_clip_rect(frame_view->ctx, pane.pane_id, x0, y0, x1, y1)) {
        wingui_set_last_error_string_internal("wingui_spec_bind_frame_surface_push_clip_rect: enqueue failed");
        return 0;
    }
    wingui_clear_last_error_internal();
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_surface_pop_clip_rect(
    const WinguiSpecBindFrameView* frame_view,
    WinguiSpecBindPaneRef pane) {
    if (!validateFrameView(frame_view, "wingui_spec_bind_frame_surface_pop_clip_rect") ||
        !validatePaneRef(pane, "wingui_spec_bind_frame_surface_pop_clip_rect")) {
        return 0;
    }
    if (!super_terminal_surface_pop_clip_rect(frame_view->ctx, pane.pane_id)) {
        wingui_set_last_error_string_internal("wingui_spec_bind_frame_surface_pop_clip_rect: enqueue failed");
        return 0;
    }
    wingui_clear_last_error_internal();
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_surface_push_offset(
    const WinguiSpecBindFrameView* frame_view,
    WinguiSpecBindPaneRef pane,
    float dx,
    float dy) {
    if (!validateFrameView(frame_view, "wingui_spec_bind_frame_surface_push_offset") ||
        !validatePaneRef(pane, "wingui_spec_bind_frame_surface_push_offset")) {
        return 0;
    }
    if (!super_terminal_surface_push_offset(frame_view->ctx, pane.pane_id, dx, dy)) {
        wingui_set_last_error_string_internal("wingui_spec_bind_frame_surface_push_offset: enqueue failed");
        return 0;
    }
    wingui_clear_last_error_internal();
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_surface_pop_offset(
    const WinguiSpecBindFrameView* frame_view,
    WinguiSpecBindPaneRef pane) {
    if (!validateFrameView(frame_view, "wingui_spec_bind_frame_surface_pop_offset") ||
        !validatePaneRef(pane, "wingui_spec_bind_frame_surface_pop_offset")) {
        return 0;
    }
    if (!super_terminal_surface_pop_offset(frame_view->ctx, pane.pane_id)) {
        wingui_set_last_error_string_internal("wingui_spec_bind_frame_surface_pop_offset: enqueue failed");
        return 0;
    }
    wingui_clear_last_error_internal();
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_surface_reset_composition(
    const WinguiSpecBindFrameView* frame_view,
    WinguiSpecBindPaneRef pane) {
    if (!validateFrameView(frame_view, "wingui_spec_bind_frame_surface_reset_composition") ||
        !validatePaneRef(pane, "wingui_spec_bind_frame_surface_reset_composition")) {
        return 0;
    }
    if (!super_terminal_surface_reset_composition(frame_view->ctx, pane.pane_id)) {
        wingui_set_last_error_string_internal("wingui_spec_bind_frame_surface_reset_composition: enqueue failed");
        return 0;
    }
    wingui_clear_last_error_internal();
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_surface_install_child_view_bounds(
    const WinguiSpecBindFrameView* frame_view,
    WinguiSpecBindPaneRef pane,
    int32_t child_id,
    float x0,
    float y0,
    float x1,
    float y1) {
    if (!validateFrameView(frame_view, "wingui_spec_bind_frame_surface_install_child_view_bounds") ||
        !validatePaneRef(pane, "wingui_spec_bind_frame_surface_install_child_view_bounds")) {
        return 0;
    }
    if (!super_terminal_surface_install_child_view_bounds(frame_view->ctx, pane.pane_id, child_id, x0, y0, x1, y1)) {
        wingui_set_last_error_string_internal("wingui_spec_bind_frame_surface_install_child_view_bounds: enqueue failed");
        return 0;
    }
    wingui_clear_last_error_internal();
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_measure_text_utf8(
    const WinguiSpecBindFrameView* frame_view,
    const char* text_utf8,
    float* out_width,
    float* out_height,
    uint32_t* out_char_count) {
    if (!out_width || !out_height || !out_char_count) {
        wingui_set_last_error_string_internal("wingui_spec_bind_frame_measure_text_utf8: output pointer was null");
        return 0;
    }

    IDWriteTextLayout* layout = nullptr;
    SurfaceDecodedText decoded{};
    if (!createSurfaceTextLayout(frame_view, text_utf8, &layout, &decoded, "wingui_spec_bind_frame_measure_text_utf8")) {
        return 0;
    }

    DWRITE_TEXT_METRICS metrics{};
    const HRESULT hr = layout->GetMetrics(&metrics);
    if (FAILED(hr)) {
        releaseCom(layout);
        wingui_set_last_error_hresult_internal("wingui_spec_bind_frame_measure_text_utf8: GetMetrics failed", hr);
        return 0;
    }

    *out_width = metrics.widthIncludingTrailingWhitespace;
    *out_height = metrics.height;
    *out_char_count = decoded.char_count;
    releaseCom(layout);
    wingui_clear_last_error_internal();
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_char_index_at_point_utf8(
    const WinguiSpecBindFrameView* frame_view,
    const char* text_utf8,
    float origin_x,
    float origin_y,
    float x,
    float y,
    uint32_t* out_char_index) {
    if (!out_char_index) {
        wingui_set_last_error_string_internal("wingui_spec_bind_frame_char_index_at_point_utf8: out_char_index was null");
        return 0;
    }

    IDWriteTextLayout* layout = nullptr;
    SurfaceDecodedText decoded{};
    if (!createSurfaceTextLayout(frame_view, text_utf8, &layout, &decoded, "wingui_spec_bind_frame_char_index_at_point_utf8")) {
        return 0;
    }

    BOOL is_trailing_hit = FALSE;
    BOOL is_inside = FALSE;
    DWRITE_HIT_TEST_METRICS metrics{};
    const HRESULT hr = layout->HitTestPoint(
        x - origin_x,
        y - origin_y,
        &is_trailing_hit,
        &is_inside,
        &metrics);
    if (FAILED(hr)) {
        releaseCom(layout);
        wingui_set_last_error_hresult_internal("wingui_spec_bind_frame_char_index_at_point_utf8: HitTestPoint failed", hr);
        return 0;
    }

    uint32_t utf16_position = metrics.textPosition + (is_trailing_hit ? metrics.length : 0u);
    *out_char_index = charIndexFromUtf16Position(decoded, utf16_position);
    releaseCom(layout);
    wingui_clear_last_error_internal();
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_point_at_char_index_utf8(
    const WinguiSpecBindFrameView* frame_view,
    const char* text_utf8,
    float origin_x,
    float origin_y,
    uint32_t char_index,
    float* out_x,
    float* out_y) {
    if (!out_x || !out_y) {
        wingui_set_last_error_string_internal("wingui_spec_bind_frame_point_at_char_index_utf8: output pointer was null");
        return 0;
    }

    IDWriteTextLayout* layout = nullptr;
    SurfaceDecodedText decoded{};
    if (!createSurfaceTextLayout(frame_view, text_utf8, &layout, &decoded, "wingui_spec_bind_frame_point_at_char_index_utf8")) {
        return 0;
    }

    if (decoded.char_to_utf16.empty()) {
        *out_x = origin_x;
        *out_y = origin_y;
        releaseCom(layout);
        wingui_clear_last_error_internal();
        return 1;
    }

    const uint32_t clamped_index = std::min<uint32_t>(char_index, decoded.char_count);
    const UINT32 utf16_position = decoded.char_to_utf16[clamped_index];
    FLOAT hit_x = 0.0f;
    FLOAT hit_y = 0.0f;
    DWRITE_HIT_TEST_METRICS metrics{};
    const HRESULT hr = layout->HitTestTextPosition(utf16_position, FALSE, &hit_x, &hit_y, &metrics);
    if (FAILED(hr)) {
        releaseCom(layout);
        wingui_set_last_error_hresult_internal("wingui_spec_bind_frame_point_at_char_index_utf8: HitTestTextPosition failed", hr);
        return 0;
    }

    *out_x = origin_x + hit_x;
    *out_y = origin_y + hit_y;
    releaseCom(layout);
    wingui_clear_last_error_internal();
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_indexed_fill_rect(
    const WinguiSpecBindFrameView* frame_view,
    WinguiSpecBindPaneRef pane,
    uint32_t x,
    uint32_t y,
    uint32_t width,
    uint32_t height,
    uint32_t palette_index) {
    if (!validateFrameView(frame_view, "wingui_spec_bind_frame_indexed_fill_rect") ||
        !validatePaneRef(pane, "wingui_spec_bind_frame_indexed_fill_rect")) {
        return 0;
    }
    if (!super_terminal_indexed_fill_rect(
            frame_view->ctx,
            pane.pane_id,
            pane.buffer_index,
            x,
            y,
            width,
            height,
            palette_index)) {
        wingui_set_last_error_string_internal("wingui_spec_bind_frame_indexed_fill_rect: fill failed");
        return 0;
    }
    wingui_clear_last_error_internal();
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_indexed_draw_line(
    const WinguiSpecBindFrameView* frame_view,
    WinguiSpecBindPaneRef pane,
    int32_t x0,
    int32_t y0,
    int32_t x1,
    int32_t y1,
    uint32_t palette_index) {
    if (!validateFrameView(frame_view, "wingui_spec_bind_frame_indexed_draw_line") ||
        !validatePaneRef(pane, "wingui_spec_bind_frame_indexed_draw_line")) {
        return 0;
    }
    if (!super_terminal_indexed_draw_line(
            frame_view->ctx,
            pane.pane_id,
            pane.buffer_index,
            x0,
            y0,
            x1,
            y1,
            palette_index)) {
        wingui_set_last_error_string_internal("wingui_spec_bind_frame_indexed_draw_line: draw failed");
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
    hosted.on_frame = runtimeOnFrame;
    hosted.shutdown = runtimeShutdown;

    wingui_clear_last_error_internal();
    return super_terminal_run_hosted_app(&hosted, out_result);
}

// ---------------------------------------------------------------------------
// Pane inbox public API
// ---------------------------------------------------------------------------

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_post_pane_msg(
    WinguiSpecBindRuntime* runtime,
    SuperTerminalPaneId pane_id,
    const char* kind_utf8,
    const char* detail_utf8)
{
    if (!runtime || pane_id.value == 0) {
        return 0;
    }
    PaneInbox* inbox = getOrCreateInbox(runtime, pane_id.value);
    return inbox->post(kind_utf8, detail_utf8) ? 1 : 0;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_spec_bind_frame_poll_pane_msg(
    const WinguiSpecBindFrameView* frame_view,
    SuperTerminalPaneId pane_id,
    char* kind_out,
    uint32_t kind_cap,
    char* detail_out,
    uint32_t detail_cap)
{
    if (!frame_view || !frame_view->ctx || pane_id.value == 0) {
        return 0;
    }
    // Find runtime via frame_view context — locate runtime from active_context.
    // We use a simple approach: search the frame_view's runtime field stored
    // as user_data. The frame handler receives the runtime pointer as user_data
    // via the callback, but the frame_view itself doesn't carry it.
    // Instead, use a module-level accessor: the frame_view's context lets us
    // reach the runtime through the stored pointer in runtimeOnFrame's closure.
    // Practical approach: the pane inbox lookup only needs the runtime pointer.
    // Since there is only one runtime per process in the NewCP design, we can
    // use a global accessor similar to the Rust side.
    // The runtime is stored in the active_context field at run start.
    // We expose a simple global for this purpose.
    WinguiSpecBindRuntime* runtime = g_spec_bind_active_runtime;
    if (!runtime) {
        return 0;
    }
    PaneInbox* inbox = findInbox(runtime, pane_id.value);
    if (!inbox) {
        return 0;
    }
    return inbox->poll(kind_out, kind_cap, detail_out, detail_cap) ? 1 : 0;
}