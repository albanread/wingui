#ifndef WINGUI_BUILD_DLL
#define WINGUI_BUILD_DLL
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <richedit.h>
#include <wincodec.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "wingui/native_ui.h"
#include "nlohmann/json.hpp"

namespace {

using ordered_json = nlohmann::ordered_json;
using Int64NoArgFn = int64_t (*)();

constexpr UINT kMsgNativeReload = WM_APP + 101;
constexpr UINT kMsgNativeShow = WM_APP + 102;
constexpr UINT kMsgNativePatch = WM_APP + 103;
constexpr wchar_t kNativeWindowClassName[] = L"WinSchemeUserAppNativeWindow";
constexpr wchar_t kScrollViewHostClassName[] = L"WinSchemeScrollViewHost";
constexpr UINT_PTR kContainerSubclassId = 0x57534E43; // WSNC
constexpr UINT_PTR kTransparentPaneSubclassId = 0x5753504E; // WSPN
constexpr UINT_PTR kCanvasSubclassId = 0x57534356;    // WSCV
constexpr UINT_PTR kImageSubclassId = 0x5753494D;     // WSIM
constexpr UINT_PTR kRichEditSubclassId = 0x57535245;  // WSRE
constexpr UINT_PTR kSplitViewSubclassId = 0x57535356; // WSSV
constexpr UINT_PTR kScrollViewSubclassId = 0x57534352; // WSCR
constexpr UINT_PTR kResizeTimerId = 1;     // debounce WM_SIZE rebuilds
constexpr UINT kResizeDebounceMs = 150;    // ms after last WM_SIZE before rebuild fires
constexpr int kDefaultWindowWidth = 960;
constexpr int kDefaultWindowHeight = 780;
constexpr int kRootPadding = 16;
constexpr int kDefaultGap = 12;
constexpr size_t kMaxQueuedNativeCommands = 512;
constexpr size_t kMaxQueuedNativeEvents = 512;

struct NativeQueuedCommand {
    WinguiNativeCommandType type = WINGUI_NATIVE_COMMAND_NONE;
    std::string payload;
};

struct NativeQueuedEvent {
    WinguiNativeEventType type = WINGUI_NATIVE_EVENT_NONE;
    std::string payload;
    uint64_t sequence = 0;
};

struct ControlBinding {
    std::string type;
    std::string event_name;
    std::string node_id;
    std::string text;
    std::string value_text;
    HWND companion_hwnd = nullptr;
    bool multiline = false;
    std::vector<std::string> column_keys;
    std::vector<std::string> option_values;
    std::vector<ordered_json> option_nodes;
    std::vector<ordered_json> row_objects;
    std::vector<HTREEITEM> item_handles;
    std::vector<HWND> child_hwnds;
    bool dragging = false;
    POINT drag_origin{};
    int drag_first_primary = 0;
    ordered_json data = ordered_json::object();
};

struct MeasuredSize;

bool isTruthyJson(const ordered_json& value);
std::string jsonString(const ordered_json& node, const char* key, const std::string& fallback);
int clampScrollOffset(int pos, int client_extent, int content_extent);
MeasuredSize measureNode(HDC hdc, const ordered_json& node, int max_width);
MeasuredSize layoutNode(HWND parent, HDC hdc, const ordered_json& node, int x, int y, int max_width);
MeasuredSize layoutChildrenStack(HWND parent,
                                 HDC hdc,
                                 const std::vector<const ordered_json*>& children,
                                 int x,
                                 int y,
                                 int max_width,
                                 int gap);
bool findUserAppNodeById(ordered_json& node, const std::string& id, ordered_json** found);
bool relayoutSplitViewControl(HWND container,
                              ControlBinding& binding,
                              const ordered_json& node,
                              bool rebuild_contents,
                              int override_first_primary = -1);
bool setUserAppNodeProp(ordered_json& spec, const std::string& node_id, const char* key, const ordered_json& value);
void rebuildNativeContainerContents(HWND container, const ordered_json& node);
void rebuildNativeWindow(HWND hwnd);
bool relayoutScrollViewControl(HWND container,
                               ControlBinding& binding,
                               const ordered_json& node,
                               bool rebuild_contents);
void repopulateTableRows(HWND hwnd, ControlBinding& binding, const ordered_json& rows, const std::string& selected_id);
ordered_json buildSplitSizesPayload(const ControlBinding& binding);
void dispatchUiEventJson(const ordered_json& event);
bool executeNativePublishJson(const char* utf8);
bool executeNativePatchJson(const char* utf8);
bool executeNativeHostRun();
bool attachEmbeddedNativeHost(const WinguiNativeEmbeddedHostDesc& desc);
bool ensureNativeThread();
bool validateUserAppSpec(const ordered_json& spec, std::string& error);
LRESULT CALLBACK nativeWndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
bool ensureScrollViewHostClassRegistered();
RECT tabContentRect(HWND hwnd);
RECT tabContentRectInParent(HWND hwnd);
int tabHeaderHeightFromContentRect(const RECT& content_rect, int control_height);
bool positionTabContentHost(HWND tab, ControlBinding& binding, int content_height);
ordered_json* findWindowStatusBarSpec(ordered_json& spec);
void applyStatusBarParts(HWND status_bar, const ordered_json& spec, int client_width);
ordered_json* findWindowCommandBarSpec(ordered_json& spec);
void applyCommandBarItems(HWND command_bar, const ordered_json& spec);
ordered_json snapshotSpec();
void installNativeMenuIfPresent(HWND hwnd, ordered_json& spec);
void installNativeCommandBarIfPresent(HWND hwnd, ordered_json& spec);
void refreshNativeViewport(HWND hwnd);
void updateNativeRootScrollbars(HWND hwnd);

struct GridMetrics {
    int columns = 1;
    int rows = 0;
    std::vector<int> column_widths;
    std::vector<int> row_heights;
};

struct MeasuredSize {
    int width = 0;
    int height = 0;
};

GridMetrics measureGridChildren(HDC hdc,
                                const std::vector<const ordered_json*>& children,
                                int columns,
                                int max_width,
                                int gap) {
    GridMetrics metrics;
    metrics.columns = std::max(1, columns);
    metrics.rows = children.empty() ? 0 : static_cast<int>((children.size() + static_cast<size_t>(metrics.columns) - 1) / static_cast<size_t>(metrics.columns));
    metrics.column_widths.assign(metrics.columns, 0);
    metrics.row_heights.assign(metrics.rows, 0);
    const int cell_limit = std::max(72, (max_width - std::max(0, metrics.columns - 1) * gap) / metrics.columns);
    for (size_t index = 0; index < children.size(); ++index) {
        const int column = static_cast<int>(index % static_cast<size_t>(metrics.columns));
        const int row = static_cast<int>(index / static_cast<size_t>(metrics.columns));
        MeasuredSize child_size = measureNode(hdc, *children[index], cell_limit);
        metrics.column_widths[column] = std::max(metrics.column_widths[column], child_size.width);
        metrics.row_heights[row] = std::max(metrics.row_heights[row], child_size.height);
    }
    return metrics;
}

MeasuredSize layoutChildrenGrid(HWND parent,
                                HDC hdc,
                                const std::vector<const ordered_json*>& children,
                                int x,
                                int y,
                                int max_width,
                                int padding,
                                int gap,
                                int columns) {
    GridMetrics metrics = measureGridChildren(hdc, children, columns, std::max(72, max_width - (padding * 2)), gap);
    int cursor_y = y + padding;
    for (int row = 0; row < metrics.rows; ++row) {
        int cursor_x = x + padding;
        for (int column = 0; column < metrics.columns; ++column) {
            const size_t index = static_cast<size_t>(row * metrics.columns + column);
            if (index >= children.size()) break;
            layoutNode(parent, hdc, *children[index], cursor_x, cursor_y, std::max(72, metrics.column_widths[column]));
            cursor_x += metrics.column_widths[column] + gap;
        }
        cursor_y += metrics.row_heights[row] + gap;
    }
    int total_width = padding * 2;
    for (size_t i = 0; i < metrics.column_widths.size(); ++i) {
        total_width += metrics.column_widths[i];
        if (i + 1 < metrics.column_widths.size()) total_width += gap;
    }
    int total_height = padding * 2;
    for (size_t i = 0; i < metrics.row_heights.size(); ++i) {
        total_height += metrics.row_heights[i];
        if (i + 1 < metrics.row_heights.size()) total_height += gap;
    }
    return {std::max(0, total_width), std::max(0, total_height)};
}

struct NativeHostState {
    std::mutex mutex;
    std::condition_variable cv;
    DWORD thread_id = 0;
    HWND hwnd = nullptr;
    HWND command_bar_hwnd = nullptr;
    HWND viewport_host = nullptr;
    HWND content_host = nullptr;
    HWND status_bar_hwnd = nullptr;
    bool ready = false;
    bool running = false;
    bool thread_started = false;
    ordered_json spec = ordered_json::object();
    std::unordered_map<HWND, ControlBinding> bindings;
    std::unordered_map<std::string, HWND> node_controls;
    std::unordered_map<std::string, int> preserved_scroll_y;
    std::unordered_map<int, ControlBinding> menu_command_bindings;
    std::unordered_map<int, ControlBinding> command_bar_bindings;
    std::unordered_map<int, ControlBinding> command_bindings;
    HMENU current_menu = nullptr;
    ordered_json pending_patch = nullptr;
    int next_control_id = 1000;
    HFONT ui_font = nullptr;
    HFONT heading_font = nullptr;
    bool suppress_events = false;
    int current_dpi = 96;
    int raw_client_width = 0;
    int raw_client_height = 0;
    int client_width = 0;
    int client_height = 0;
    int command_bar_height = 0;
    int status_bar_height = 0;
    int content_width = 0;
    int content_height = 0;
    HWND focused_rich_text_hwnd = nullptr;
    int scroll_x = 0;
    int scroll_y = 0;
    std::mutex command_mutex;
    std::deque<NativeQueuedCommand> command_queue;
    std::mutex event_mutex;
    std::deque<NativeQueuedEvent> event_queue;
    HANDLE event_handle = nullptr;
    uint64_t next_event_sequence = 1;
    bool embedded_mode = false;
    HWND parent_hwnd = nullptr;
    std::atomic<uint64_t> publish_count{0};
    std::atomic<uint64_t> patch_request_count{0};
    std::atomic<uint64_t> patch_direct_apply_count{0};
    std::atomic<uint64_t> patch_subtree_rebuild_count{0};
    std::atomic<uint64_t> patch_window_rebuild_count{0};
    std::atomic<uint64_t> patch_resize_reject_count{0};
    std::atomic<uint64_t> patch_failed_count{0};
};

struct NativeUiSessionImpl {
    NativeHostState state;
    WinguiNativeCallbacks callbacks{};
    std::string backend_info;
};

thread_local NativeUiSessionImpl* g_current_native_session = nullptr;
NativeUiSessionImpl g_default_native_session{};
std::mutex g_native_session_map_mutex;
std::unordered_map<HWND, NativeUiSessionImpl*> g_native_sessions_by_hwnd;

std::mutex g_native_trace_mutex;

NativeUiSessionImpl* defaultNativeSession() {
    return &g_default_native_session;
}

NativeUiSessionImpl* activeNativeSession() {
    return g_current_native_session ? g_current_native_session : defaultNativeSession();
}

NativeHostState& activeNativeState() {
    return activeNativeSession()->state;
}

WinguiNativeCallbacks& activeNativeCallbacks() {
    return activeNativeSession()->callbacks;
}

std::string& activeNativeBackendInfo() {
    return activeNativeSession()->backend_info;
}

NativeUiSessionImpl* lookupNativeSessionForWindow(HWND hwnd) {
    if (!hwnd) return activeNativeSession();

    std::lock_guard<std::mutex> lock(g_native_session_map_mutex);
    HWND current = hwnd;
    while (current) {
        auto it = g_native_sessions_by_hwnd.find(current);
        if (it != g_native_sessions_by_hwnd.end() && it->second) {
            return it->second;
        }
        auto* user_data = reinterpret_cast<NativeUiSessionImpl*>(GetWindowLongPtrW(current, GWLP_USERDATA));
        if (user_data) {
            return user_data;
        }
        HWND parent = GetParent(current);
        if (!parent || parent == current) break;
        current = parent;
    }
    return defaultNativeSession();
}

void registerNativeSessionWindow(NativeUiSessionImpl* session, HWND hwnd) {
    if (!session || !hwnd) return;
    std::lock_guard<std::mutex> lock(g_native_session_map_mutex);
    g_native_sessions_by_hwnd[hwnd] = session;
}

void unregisterNativeSessionWindow(HWND hwnd) {
    if (!hwnd) return;
    std::lock_guard<std::mutex> lock(g_native_session_map_mutex);
    g_native_sessions_by_hwnd.erase(hwnd);
}

struct ScopedNativeSession {
    NativeUiSessionImpl* previous = nullptr;

    explicit ScopedNativeSession(NativeUiSessionImpl* session)
        : previous(g_current_native_session) {
        g_current_native_session = session ? session : defaultNativeSession();
    }

    ~ScopedNativeSession() {
        g_current_native_session = previous;
    }
};

#define g_native activeNativeState()
#define g_native_callbacks activeNativeCallbacks()
#define g_backend_info activeNativeBackendInfo()

std::wstring nativeEventTracePath() {
    wchar_t path_buffer[MAX_PATH];
    const DWORD length = GetModuleFileNameW(nullptr, path_buffer, MAX_PATH);
    if (length == 0 || length == MAX_PATH) {
        return L"native_ui_event_trace.log";
    }

    std::wstring path(path_buffer, length);
    const size_t slash = path.find_last_of(L"\\/");
    if (slash != std::wstring::npos) {
        path.resize(slash + 1);
    } else {
        path.clear();
    }
    path += L"native_ui_event_trace.log";
    return path;
}

std::wstring nativePatchTracePath() {
    wchar_t path_buffer[MAX_PATH];
    const DWORD length = GetModuleFileNameW(nullptr, path_buffer, MAX_PATH);
    if (length == 0 || length == MAX_PATH) {
        return L"native_patch_trace.log";
    }

    std::wstring path(path_buffer, length);
    const size_t slash = path.find_last_of(L"\\/");
    if (slash != std::wstring::npos) {
        path.resize(slash + 1);
    } else {
        path.clear();
    }
    path += L"native_patch_trace.log";
    return path;
}

void appendNativePatchTraceLine(const std::string& line) {
    std::lock_guard<std::mutex> lock(g_native_trace_mutex);
    const std::wstring path = nativePatchTracePath();
    HANDLE file = CreateFileW(path.c_str(),
                              FILE_APPEND_DATA,
                              FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr,
                              OPEN_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL,
                              nullptr);
    if (file == INVALID_HANDLE_VALUE) return;

    DWORD written = 0;
    WriteFile(file, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
    WriteFile(file, "\r\n", 2, &written, nullptr);
    CloseHandle(file);
}

void traceNativePatchEvent(const std::string& message) {
    char buffer[1024];
    std::snprintf(buffer,
                  sizeof(buffer),
                  "[%llu][tid=%lu] %s",
                  static_cast<unsigned long long>(GetTickCount64()),
                  static_cast<unsigned long>(GetCurrentThreadId()),
                  message.c_str());
    appendNativePatchTraceLine(buffer);
}

size_t patchOperationCount(const ordered_json& patch) {
    if (patch.is_array()) return patch.size();
    auto ops_it = patch.find("ops");
    if (patch.is_object() && ops_it != patch.end() && ops_it->is_array()) {
        return ops_it->size();
    }
    return patch.is_object() ? 1u : 0u;
}

void traceNativePatchDecision(const char* decision,
                              const std::string& op_name,
                              const std::string& node_id = std::string(),
                              const std::string& detail = std::string()) {
    std::string line = std::string("decision=") + decision + " op=" + op_name;
    if (!node_id.empty()) line += " id=" + node_id;
    if (!detail.empty()) line += " detail=" + detail;
    traceNativePatchEvent(line);
}

void traceNativeUiEvent(const ordered_json& event) {
    const std::string payload = event.dump();
    const std::string event_name = event.value("event", std::string());
    const std::string node_id = event.value("id", std::string());
    const std::string source = event.value("source", std::string());

    char header[512];
    std::snprintf(header,
                  sizeof(header),
                  "[%llu][tid=%lu] event=%s id=%s source=%s bytes=%zu payload=",
                  static_cast<unsigned long long>(GetTickCount64()),
                  static_cast<unsigned long>(GetCurrentThreadId()),
                  event_name.empty() ? "<none>" : event_name.c_str(),
                  node_id.empty() ? "<none>" : node_id.c_str(),
                  source.empty() ? "<none>" : source.c_str(),
                  payload.size());

    appendNativePatchTraceLine(std::string(header) + payload);

    std::lock_guard<std::mutex> lock(g_native_trace_mutex);
    const std::wstring path = nativeEventTracePath();
    HANDLE file = CreateFileW(path.c_str(),
                              FILE_APPEND_DATA,
                              FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr,
                              OPEN_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL,
                              nullptr);
    if (file == INVALID_HANDLE_VALUE) return;

    const std::string line = std::string(header) + payload + "\r\n";
    DWORD written = 0;
    WriteFile(file, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
    CloseHandle(file);
}

void setNativeLastError(const std::string& error);

bool shouldTraceWorkspacePaneNodeId(const char* node_id_utf8) {
    if (!node_id_utf8 || !*node_id_utf8) return false;
    return std::strcmp(node_id_utf8, "workspace_editor") == 0 ||
        std::strcmp(node_id_utf8, "workspace_graphics") == 0 ||
        std::strcmp(node_id_utf8, "workspace_repl") == 0;
}

bool tryGetNativeNodeBounds(const char* node_id_utf8, WinguiNativeNodeBounds* out_bounds) {
    if (!node_id_utf8 || !out_bounds) {
        setNativeLastError("Native UI node bounds query requires an id and output buffer.");
        return false;
    }

    HWND node_hwnd = nullptr;
    HWND target_hwnd = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_native.mutex);
        auto it = g_native.node_controls.find(node_id_utf8);
        if (it == g_native.node_controls.end()) {
            std::memset(out_bounds, 0, sizeof(*out_bounds));
            return false;
        }
        node_hwnd = it->second;
        target_hwnd = (g_native.embedded_mode && g_native.parent_hwnd && IsWindow(g_native.parent_hwnd))
            ? g_native.parent_hwnd
            : g_native.hwnd;
    }

    if (!node_hwnd || !IsWindow(node_hwnd) || !target_hwnd || !IsWindow(target_hwnd)) {
        std::memset(out_bounds, 0, sizeof(*out_bounds));
        return false;
    }

    RECT rect{};
    if (!GetClientRect(node_hwnd, &rect)) {
        std::memset(out_bounds, 0, sizeof(*out_bounds));
        return false;
    }
    MapWindowPoints(node_hwnd, target_hwnd, reinterpret_cast<LPPOINT>(&rect), 2);
    out_bounds->x = rect.left;
    out_bounds->y = rect.top;
    out_bounds->width = std::max(0, static_cast<int32_t>(rect.right - rect.left));
    out_bounds->height = std::max(0, static_cast<int32_t>(rect.bottom - rect.top));
    out_bounds->visible = IsWindowVisible(node_hwnd) ? 1 : 0;
    if (shouldTraceWorkspacePaneNodeId(node_id_utf8)) {
        char buffer[512];
        std::snprintf(buffer,
                      sizeof(buffer),
                      "node-bounds id=%s hwnd=%p target=%p x=%ld y=%ld w=%ld h=%ld visible=%d",
                      node_id_utf8,
                      node_hwnd,
                      target_hwnd,
                      static_cast<long>(out_bounds->x),
                      static_cast<long>(out_bounds->y),
                      static_cast<long>(out_bounds->width),
                      static_cast<long>(out_bounds->height),
                      static_cast<int>(out_bounds->visible));
        traceNativePatchEvent(buffer);
    }
    return true;
}

bool tryGetNativeNodeHwnd(const char* node_id_utf8, void** out_hwnd) {
    if (!node_id_utf8 || !out_hwnd) {
        setNativeLastError("Native UI node HWND query requires an id and output buffer.");
        return false;
    }

    HWND node_hwnd = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_native.mutex);
        auto it = g_native.node_controls.find(node_id_utf8);
        if (it != g_native.node_controls.end()) {
            node_hwnd = it->second;
        }
    }

    *out_hwnd = (node_hwnd && IsWindow(node_hwnd)) ? node_hwnd : nullptr;
    if (shouldTraceWorkspacePaneNodeId(node_id_utf8)) {
        char buffer[256];
        std::snprintf(buffer,
                      sizeof(buffer),
                      "node-hwnd id=%s hwnd=%p",
                      node_id_utf8,
                      *out_hwnd);
        traceNativePatchEvent(buffer);
    }
    return *out_hwnd != nullptr;
}

bool tryGetNativeNodeTypeUtf8(const char* node_id_utf8, char* buffer_utf8, uint32_t buffer_size) {
    if (!node_id_utf8 || !buffer_utf8 || buffer_size == 0) {
        setNativeLastError("Native UI node type query requires an id and output buffer.");
        return false;
    }

    std::string type;
    {
        std::lock_guard<std::mutex> lock(g_native.mutex);
        auto node_it = g_native.node_controls.find(node_id_utf8);
        if (node_it == g_native.node_controls.end()) {
            buffer_utf8[0] = '\0';
            return false;
        }
        auto binding_it = g_native.bindings.find(node_it->second);
        if (binding_it == g_native.bindings.end()) {
            buffer_utf8[0] = '\0';
            return false;
        }
        type = binding_it->second.type;
    }

    std::snprintf(buffer_utf8, buffer_size, "%s", type.c_str());
    return buffer_utf8[0] != '\0';
}

bool findFocusedPaneIdInNode(const ordered_json& node, std::string& out_id) {
    if (!node.is_object()) return false;

    auto focused_pane_it = node.find("focusedPaneId");
    if (focused_pane_it != node.end() && focused_pane_it->is_string()) {
        out_id = focused_pane_it->get<std::string>();
        return true;
    }

    const std::string node_id = jsonString(node, "id", "");
    const std::string type = jsonString(node, "type", "");
    auto focused_it = node.find("focused");
    if (!node_id.empty() && focused_it != node.end() && isTruthyJson(*focused_it) &&
        (type == "split-pane" || type == "text-grid-pane" || type == "text-grid" ||
         type == "indexed-graphics" || type == "rgba-pane" || type == "pane")) {
        out_id = node_id;
        return true;
    }

    auto body_it = node.find("body");
    if (body_it != node.end() && findFocusedPaneIdInNode(*body_it, out_id)) {
        return true;
    }

    auto children_it = node.find("children");
    if (children_it != node.end() && children_it->is_array()) {
        for (const auto& child : *children_it) {
            if (findFocusedPaneIdInNode(child, out_id)) {
                return true;
            }
        }
    }

    auto tabs_it = node.find("tabs");
    if (tabs_it != node.end() && tabs_it->is_array()) {
        for (const auto& tab : *tabs_it) {
            if (!tab.is_object()) continue;
            auto content_it = tab.find("content");
            if (content_it != tab.end() && findFocusedPaneIdInNode(*content_it, out_id)) {
                return true;
            }
        }
    }

    return false;
}

bool copyFocusedPaneIdUtf8(char* buffer_utf8, uint32_t buffer_size) {
    if (!buffer_utf8 || buffer_size == 0) {
        setNativeLastError("Focused pane id buffer is required.");
        return false;
    }

    std::string focused_id;
    {
        std::lock_guard<std::mutex> lock(g_native.mutex);
        if (g_native.spec.is_object()) {
            findFocusedPaneIdInNode(g_native.spec, focused_id);
        }
    }

    std::memset(buffer_utf8, 0, buffer_size);
    if (!focused_id.empty()) {
        std::strncpy(buffer_utf8, focused_id.c_str(), buffer_size - 1);
        buffer_utf8[buffer_size - 1] = '\0';
    }
    return true;
}

struct MenuBuildResult {
    HMENU menu = nullptr;
    std::unordered_map<int, ControlBinding> command_bindings;
    int next_control_id = 1000;
    bool ok = true;
};

thread_local std::string g_native_last_error;
IWICImagingFactory* g_wic_factory = nullptr;

bool commandTypeNeedsPayload(WinguiNativeCommandType type) {
    return type == WINGUI_NATIVE_COMMAND_PUBLISH_JSON ||
           type == WINGUI_NATIVE_COMMAND_PATCH_JSON;
}

HANDLE ensureReactiveEventHandleLocked() {
    if (!g_native.event_handle) {
        g_native.event_handle = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    }
    return g_native.event_handle;
}

void syncReactiveEventSignalLocked() {
    HANDLE handle = ensureReactiveEventHandleLocked();
    if (!handle) return;
    if (g_native.event_queue.empty()) {
        ResetEvent(handle);
    } else {
        SetEvent(handle);
    }
}

void enqueueReactiveEvent(WinguiNativeEventType type, const std::string& payload) {
    std::lock_guard<std::mutex> lock(g_native.event_mutex);
    if (g_native.event_queue.size() >= kMaxQueuedNativeEvents) {
        g_native.event_queue.pop_front();
    }
    NativeQueuedEvent event;
    event.type = type;
    event.payload = payload;
    event.sequence = g_native.next_event_sequence++;
    g_native.event_queue.push_back(std::move(event));
    syncReactiveEventSignalLocked();
}

std::wstring utf8ToWide(const char* text) {
    if (!text || text[0] == '\0') return {};
    const int len = static_cast<int>(std::strlen(text));
    const int needed = MultiByteToWideChar(CP_UTF8, 0, text, len, nullptr, 0);
    if (needed <= 0) return {};
    std::wstring out(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text, len, out.data(), needed);
    return out;
}

std::wstring utf8ToWide(const std::string& text) {
    return utf8ToWide(text.c_str());
}

std::string wideToUtf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int needed = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return {};
    std::string out(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), needed, nullptr, nullptr);
    return out;
}

// ---------------------------------------------------------------------------
// Visual styles — ComCtl32 v6 activation context
// ---------------------------------------------------------------------------

// Write a minimal ComCtl32 v6 manifest to a temp file and return an
// activation context handle.  The temp file is deleted immediately after
// CreateActCtxW reads it.  Returns INVALID_HANDLE_VALUE on failure.
// Must be called before InitCommonControlsEx and CreateWindowExW.
HANDLE createComCtlV6ActivationContext() {
    wchar_t temp_path[MAX_PATH]{};
    wchar_t temp_file[MAX_PATH]{};
    if (!GetTempPathW(MAX_PATH, temp_path)) return INVALID_HANDLE_VALUE;
    if (!GetTempFileNameW(temp_path, L"wsnm", 0, temp_file)) return INVALID_HANDLE_VALUE;

    static const char kManifest[] =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<assembly xmlns=\"urn:schemas-microsoft-com:asm.v1\" manifestVersion=\"1.0\">"
        "<dependency><dependentAssembly><assemblyIdentity"
        " type=\"win32\" name=\"Microsoft.Windows.Common-Controls\""
        " version=\"6.0.0.0\" processorArchitecture=\"*\""
        " publicKeyToken=\"6595b64144ccf1df\" language=\"*\"/>"
        "</dependentAssembly></dependency></assembly>";

    HANDLE file = CreateFileW(temp_file, GENERIC_WRITE, 0, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return INVALID_HANDLE_VALUE;
    DWORD written = 0;
    WriteFile(file, kManifest, static_cast<DWORD>(sizeof(kManifest) - 1), &written, nullptr);
    CloseHandle(file);

    ACTCTXW actctx{};
    actctx.cbSize = sizeof(actctx);
    actctx.lpSource = temp_file;
    HANDLE hActCtx = CreateActCtxW(&actctx);
    DeleteFileW(temp_file);
    return hActCtx;
}

void initCommonControlsOnce() {
    static std::once_flag once;
    std::call_once(once, []() {
        LoadLibraryW(L"Msftedit.dll");
        const HRESULT init_hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (SUCCEEDED(init_hr) || init_hr == RPC_E_CHANGED_MODE) {
            CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                             IID_PPV_ARGS(&g_wic_factory));
        }
        INITCOMMONCONTROLSEX icc{};
        icc.dwSize = sizeof(icc);
        icc.dwICC = ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES |
                    ICC_TAB_CLASSES | ICC_LISTVIEW_CLASSES |
                    ICC_TREEVIEW_CLASSES |
                    ICC_DATE_CLASSES |
                    ICC_PROGRESS_CLASS | ICC_UPDOWN_CLASS |
                    ICC_BAR_CLASSES | ICC_COOL_CLASSES |
                    ICC_LINK_CLASS;
        InitCommonControlsEx(&icc);
    });
}

// ---------------------------------------------------------------------------
// DPI helpers
// ---------------------------------------------------------------------------

// Returns the DPI for a given window, using GetDpiForWindow (Win10+) with
// a GDI fallback for older systems.
int dpiForWindow(HWND hwnd) {
    using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
    static auto fn = reinterpret_cast<GetDpiForWindowFn>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    if (fn && hwnd) return static_cast<int>(fn(hwnd));
    HDC hdc = GetDC(hwnd);
    const int dpi = hdc ? GetDeviceCaps(hdc, LOGPIXELSX) : 96;
    if (hdc) ReleaseDC(hwnd, hdc);
    return dpi > 0 ? dpi : 96;
}

// ---------------------------------------------------------------------------
// Font creation — DPI-aware
// ---------------------------------------------------------------------------

HFONT createUiFont(bool heading, int dpi = 96) {
    // Use SystemParametersInfoForDpi (Windows 10 1607+) when available so
    // the message font metrics are correct for the target DPI rather than
    // the system DPI.  Fall back to the legacy API on older builds.
    using SpiForDpiFn = BOOL(WINAPI*)(UINT, UINT, PVOID, UINT, UINT);
    static auto spi_fn = reinterpret_cast<SpiForDpiFn>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "SystemParametersInfoForDpi"));

    NONCLIENTMETRICSW metrics{};
    metrics.cbSize = sizeof(metrics);
    bool ok = false;
    if (spi_fn && dpi != 96) {
        ok = spi_fn(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0,
                    static_cast<UINT>(dpi)) != 0;
    }
    if (!ok) {
        ok = SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0) != 0;
    }
    if (!ok) return static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

    LOGFONTW lf = metrics.lfMessageFont;
    if (heading) {
        lf.lfWeight = FW_BOLD;
        lf.lfHeight = static_cast<LONG>(lf.lfHeight * 1.35);
        if (lf.lfHeight == 0) lf.lfHeight = -static_cast<LONG>(22 * dpi / 96);
    }
    return CreateFontIndirectW(&lf);
}

// Destroy existing fonts and recreate them at the given DPI.
// Called once at startup and again on WM_DPICHANGED.
void recreateFonts(int dpi) {
    if (g_native.ui_font)      { DeleteObject(g_native.ui_font);      g_native.ui_font = nullptr; }
    if (g_native.heading_font) { DeleteObject(g_native.heading_font); g_native.heading_font = nullptr; }
    g_native.ui_font      = createUiFont(false, dpi);
    g_native.heading_font = createUiFont(true,  dpi);
    g_native.current_dpi  = dpi;
}

void ensureFonts() {
    if (!g_native.ui_font || !g_native.heading_font) {
        recreateFonts(g_native.current_dpi > 0 ? g_native.current_dpi : 96);
    }
}

std::string jsonString(const ordered_json& node, const char* key, const std::string& fallback = std::string()) {
    auto it = node.find(key);
    if (it == node.end()) return fallback;
    if (it->is_string()) return it->get<std::string>();
    if (it->is_boolean()) return it->get<bool>() ? "true" : "false";
    if (it->is_number_integer()) return std::to_string(it->get<int>());
    if (it->is_number_float()) return std::to_string(it->get<double>());
    return fallback;
}

int jsonInt(const ordered_json& node, const char* key, int fallback) {
    auto it = node.find(key);
    if (it == node.end()) return fallback;
    if (it->is_number_integer()) return it->get<int>();
    if (it->is_number_unsigned()) return static_cast<int>(it->get<unsigned int>());
    if (it->is_number_float()) return static_cast<int>(it->get<double>());
    return fallback;
}

double jsonDouble(const ordered_json& node, const char* key, double fallback) {
    auto it = node.find(key);
    if (it == node.end()) return fallback;
    if (it->is_number()) return it->get<double>();
    return fallback;
}

bool asciiIEquals(char left, char right) {
    return std::tolower(static_cast<unsigned char>(left)) ==
           std::tolower(static_cast<unsigned char>(right));
}

bool asciiStartsWithInsensitive(const std::string& text, const std::string& prefix) {
    if (text.size() < prefix.size()) return false;
    for (size_t i = 0; i < prefix.size(); ++i) {
        if (!asciiIEquals(text[i], prefix[i])) return false;
    }
    return true;
}

std::string trimAsciiWhitespace(const std::string& text) {
    size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start]))) {
        ++start;
    }
    size_t end = text.size();
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    return text.substr(start, end - start);
}

void appendDecodedHtmlEntity(std::string& out, const std::string& entity) {
    if (entity == "lt") {
        out.push_back('<');
    } else if (entity == "gt") {
        out.push_back('>');
    } else if (entity == "amp") {
        out.push_back('&');
    } else if (entity == "quot") {
        out.push_back('"');
    } else if (entity == "apos") {
        out.push_back('\'');
    } else if (entity == "nbsp") {
        out.push_back(' ');
    } else {
        out.append("&");
        out.append(entity);
        out.push_back(';');
    }
}

std::string htmlToPlainText(const std::string& html) {
    std::string out;
    out.reserve(html.size());
    for (size_t i = 0; i < html.size(); ++i) {
        const char ch = html[i];
        if (ch == '<') {
            const size_t end = html.find('>', i + 1);
            if (end == std::string::npos) break;
            std::string tag = trimAsciiWhitespace(html.substr(i + 1, end - i - 1));
            if (!tag.empty() && tag[0] == '/') tag = trimAsciiWhitespace(tag.substr(1));
            if (asciiStartsWithInsensitive(tag, "br") ||
                asciiStartsWithInsensitive(tag, "p") ||
                asciiStartsWithInsensitive(tag, "div") ||
                asciiStartsWithInsensitive(tag, "li") ||
                asciiStartsWithInsensitive(tag, "tr")) {
                if (out.empty() || out.back() != '\n') out.push_back('\n');
            }
            i = end;
            continue;
        }
        if (ch == '&') {
            const size_t end = html.find(';', i + 1);
            if (end != std::string::npos) {
                appendDecodedHtmlEntity(out, html.substr(i + 1, end - i - 1));
                i = end;
                continue;
            }
        }
        if (ch != '\r') out.push_back(ch);
    }

    std::string normalized;
    normalized.reserve(out.size());
    bool last_was_newline = false;
    for (char ch : out) {
        if (ch == '\n') {
            if (!last_was_newline || (normalized.empty() || normalized.back() != '\n')) {
                normalized.push_back('\n');
            }
            last_was_newline = true;
        } else {
            normalized.push_back(ch);
            last_was_newline = false;
        }
    }
    return normalized;
}

std::string escapeHtmlText(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (char ch : text) {
        switch (ch) {
        case '&': out.append("&amp;"); break;
        case '<': out.append("&lt;"); break;
        case '>': out.append("&gt;"); break;
        case '"': out.append("&quot;"); break;
        default: out.push_back(ch); break;
        }
    }
    return out;
}

std::string plainTextToHtml(const std::wstring& text) {
    const std::string utf8 = wideToUtf8(text);
    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= utf8.size()) {
        const size_t end = utf8.find('\n', start);
        std::string line = (end == std::string::npos)
            ? utf8.substr(start)
            : utf8.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
        if (end == std::string::npos) break;
        start = end + 1;
    }

    std::string html;
    std::string current_paragraph;
    auto flush_paragraph = [&]() {
        html.append("<p>");
        html.append(current_paragraph.empty() ? std::string() : current_paragraph);
        html.append("</p>");
        current_paragraph.clear();
    };

    for (const std::string& line : lines) {
        const std::string escaped = escapeHtmlText(line);
        if (escaped.empty()) {
            flush_paragraph();
            continue;
        }
        if (!current_paragraph.empty()) current_paragraph.append("<br/>");
        current_paragraph.append(escaped);
    }

    if (!current_paragraph.empty() || html.empty()) flush_paragraph();
    return html;
}

std::wstring controlWindowText(HWND hwnd) {
    const int len = GetWindowTextLengthW(hwnd);
    std::wstring value(static_cast<size_t>(std::max(len, 0)) + 1, L'\0');
    if (len > 0) {
        GetWindowTextW(hwnd, value.data(), len + 1);
    }
    value.resize(static_cast<size_t>(std::max(len, 0)));
    return value;
}

std::wstring normalizeLineEndings(std::wstring text) {
    std::wstring normalized;
    normalized.reserve(text.size());
    for (size_t index = 0; index < text.size(); ++index) {
        const wchar_t ch = text[index];
        if (ch == L'\r') {
            if (index + 1 < text.size() && text[index + 1] == L'\n') {
                ++index;
            }
            normalized.push_back(L'\n');
            continue;
        }
        normalized.push_back(ch);
    }
    return normalized;
}

struct EditViewState {
    DWORD selection_start = 0;
    DWORD selection_end = 0;
    int first_visible_line = 0;
};

struct RichEditStreamState {
    const char* input = nullptr;
    size_t input_size = 0;
    size_t input_offset = 0;
    std::string output;
};

DWORD CALLBACK richEditStreamInCallback(DWORD_PTR cookie, LPBYTE buffer, LONG cb, LONG* written) {
    if (!written) return 1;
    auto* state = reinterpret_cast<RichEditStreamState*>(cookie);
    if (!state || !state->input) {
        *written = 0;
        return 1;
    }
    const size_t remaining = state->input_size > state->input_offset
        ? (state->input_size - state->input_offset)
        : 0;
    const size_t count = std::min<size_t>(remaining, static_cast<size_t>(std::max<LONG>(0, cb)));
    if (count > 0) {
        std::memcpy(buffer, state->input + state->input_offset, count);
        state->input_offset += count;
    }
    *written = static_cast<LONG>(count);
    return 0;
}

DWORD CALLBACK richEditStreamOutCallback(DWORD_PTR cookie, LPBYTE buffer, LONG cb, LONG* written) {
    if (!written) return 1;
    auto* state = reinterpret_cast<RichEditStreamState*>(cookie);
    if (!state || !buffer || cb < 0) {
        *written = 0;
        return 1;
    }
    state->output.append(reinterpret_cast<const char*>(buffer), static_cast<size_t>(cb));
    *written = cb;
    return 0;
}

bool richEditSetRtf(HWND hwnd, const std::string& rtf) {
    if (!hwnd || !IsWindow(hwnd)) return false;
    RichEditStreamState state;
    state.input = rtf.data();
    state.input_size = rtf.size();
    EDITSTREAM stream{};
    stream.dwCookie = reinterpret_cast<DWORD_PTR>(&state);
    stream.pfnCallback = richEditStreamInCallback;
    SendMessageW(hwnd, EM_SETSEL, 0, -1);
    SendMessageW(hwnd, EM_STREAMIN, SF_RTF | SFF_SELECTION, reinterpret_cast<LPARAM>(&stream));
    SendMessageW(hwnd, EM_SETSEL, 0, 0);
    return stream.dwError == 0;
}

std::string richEditGetRtf(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) return std::string();
    RichEditStreamState state;
    EDITSTREAM stream{};
    stream.dwCookie = reinterpret_cast<DWORD_PTR>(&state);
    stream.pfnCallback = richEditStreamOutCallback;
    SendMessageW(hwnd, EM_STREAMOUT, SF_RTF, reinterpret_cast<LPARAM>(&stream));
    return stream.dwError == 0 ? state.output : std::string();
}

void dispatchRichTextChangeEvent(HWND hwnd, ControlBinding& binding) {
    if (!hwnd || !IsWindow(hwnd) || binding.event_name.empty()) return;
    binding.value_text = richEditGetRtf(hwnd);
    ordered_json event = ordered_json::object();
    event["type"] = "ui-event";
    event["event"] = binding.event_name;
    event["source"] = "native-win32";
    if (!binding.node_id.empty()) event["id"] = binding.node_id;
    if (!binding.text.empty()) event["text"] = binding.text;
    event["value"] = binding.value_text;
    dispatchUiEventJson(event);
}

void dispatchControlFocusEvent(const ControlBinding& binding, bool focused) {
    if (binding.event_name.empty()) return;
    ordered_json event = ordered_json::object();
    event["type"] = "ui-event";
    event["event"] = binding.event_name;
    event["source"] = "native-win32";
    if (!binding.node_id.empty()) event["id"] = binding.node_id;
    if (!binding.text.empty()) event["text"] = binding.text;
    event["focused"] = focused;
    event["controlType"] = binding.type;
    event["value"] = focused;
    dispatchUiEventJson(event);
}

bool toggleRichEditSelectionEffect(HWND hwnd, DWORD mask, DWORD effect) {
    if (!hwnd || !IsWindow(hwnd)) return false;

    CHARFORMAT2W format{};
    format.cbSize = sizeof(format);
    if (SendMessageW(hwnd, EM_GETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&format)) == 0) {
        return false;
    }

    const bool enabled = (format.dwMask & mask) != 0 && (format.dwEffects & effect) != 0;
    CHARFORMAT2W update{};
    update.cbSize = sizeof(update);
    update.dwMask = mask;
    update.dwEffects = enabled ? 0u : effect;
    return SendMessageW(hwnd, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&update)) != 0;
}

bool setRichEditParagraphAlignment(HWND hwnd, WORD alignment) {
    if (!hwnd || !IsWindow(hwnd)) return false;
    PARAFORMAT2 format{};
    format.cbSize = sizeof(format);
    format.dwMask = PFM_ALIGNMENT;
    format.wAlignment = alignment;
    return SendMessageW(hwnd, EM_SETPARAFORMAT, 0, reinterpret_cast<LPARAM>(&format)) != 0;
}

bool toggleRichEditBullets(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) return false;

    PARAFORMAT2 current{};
    current.cbSize = sizeof(current);
    SendMessageW(hwnd, EM_GETPARAFORMAT, 0, reinterpret_cast<LPARAM>(&current));

    PARAFORMAT2 update{};
    update.cbSize = sizeof(update);
    update.dwMask = PFM_NUMBERING | PFM_STARTINDENT | PFM_OFFSET;
    const bool enabled = (current.dwMask & PFM_NUMBERING) != 0 && current.wNumbering == PFN_BULLET;
    if (enabled) {
        update.wNumbering = 0;
        update.dxStartIndent = 0;
        update.dxOffset = 0;
    } else {
        update.wNumbering = PFN_BULLET;
        update.dxStartIndent = 360;
        update.dxOffset = -180;
    }
    return SendMessageW(hwnd, EM_SETPARAFORMAT, 0, reinterpret_cast<LPARAM>(&update)) != 0;
}

bool toggleRichEditNumbering(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) return false;

    PARAFORMAT2 current{};
    current.cbSize = sizeof(current);
    SendMessageW(hwnd, EM_GETPARAFORMAT, 0, reinterpret_cast<LPARAM>(&current));

    PARAFORMAT2 update{};
    update.cbSize = sizeof(update);
    update.dwMask = PFM_NUMBERING | PFM_STARTINDENT | PFM_OFFSET | PFM_NUMBERINGSTART | PFM_NUMBERINGSTYLE | PFM_NUMBERINGTAB;
    const bool enabled = (current.dwMask & PFM_NUMBERING) != 0 && current.wNumbering == PFN_ARABIC;
    if (enabled) {
        update.wNumbering = 0;
        update.dxStartIndent = 0;
        update.dxOffset = 0;
        update.wNumberingStart = 0;
        update.wNumberingStyle = 0;
        update.wNumberingTab = 0;
    } else {
        update.wNumbering = PFN_ARABIC;
        update.dxStartIndent = 360;
        update.dxOffset = -180;
        update.wNumberingStart = 1;
        update.wNumberingStyle = 0;
        update.wNumberingTab = 360;
    }
    return SendMessageW(hwnd, EM_SETPARAFORMAT, 0, reinterpret_cast<LPARAM>(&update)) != 0;
}

bool adjustRichEditParagraphIndent(HWND hwnd, LONG delta_twips) {
    if (!hwnd || !IsWindow(hwnd)) return false;

    PARAFORMAT2 current{};
    current.cbSize = sizeof(current);
    SendMessageW(hwnd, EM_GETPARAFORMAT, 0, reinterpret_cast<LPARAM>(&current));

    PARAFORMAT2 update{};
    update.cbSize = sizeof(update);
    update.dwMask = PFM_STARTINDENT;
    update.dxStartIndent = std::max<LONG>(0, current.dxStartIndent + delta_twips);
    return SendMessageW(hwnd, EM_SETPARAFORMAT, 0, reinterpret_cast<LPARAM>(&update)) != 0;
}

bool clearRichEditCharacterFormatting(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) return false;
    CHARFORMAT2W update{};
    update.cbSize = sizeof(update);
    update.dwMask = CFM_BOLD | CFM_ITALIC | CFM_UNDERLINE;
    update.dwEffects = 0;
    return SendMessageW(hwnd, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&update)) != 0;
}

bool clearRichEditParagraphFormatting(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) return false;
    PARAFORMAT2 update{};
    update.cbSize = sizeof(update);
    update.dwMask = PFM_ALIGNMENT | PFM_NUMBERING | PFM_STARTINDENT | PFM_OFFSET | PFM_NUMBERINGSTART | PFM_NUMBERINGSTYLE | PFM_NUMBERINGTAB;
    update.wAlignment = PFA_LEFT;
    update.wNumbering = 0;
    update.dxStartIndent = 0;
    update.dxOffset = 0;
    update.wNumberingStart = 0;
    update.wNumberingStyle = 0;
    update.wNumberingTab = 0;
    return SendMessageW(hwnd, EM_SETPARAFORMAT, 0, reinterpret_cast<LPARAM>(&update)) != 0;
}

bool adjustRichEditSelectionFontSize(HWND hwnd, LONG delta_twips) {
    if (!hwnd || !IsWindow(hwnd)) return false;

    CHARFORMAT2W current{};
    current.cbSize = sizeof(current);
    SendMessageW(hwnd, EM_GETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&current));

    LONG size = current.yHeight;
    if (size <= 0) size = 200;
    size = std::clamp<LONG>(size + delta_twips, 120, 1440);

    CHARFORMAT2W update{};
    update.cbSize = sizeof(update);
    update.dwMask = CFM_SIZE;
    update.yHeight = size;
    return SendMessageW(hwnd, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&update)) != 0;
}

bool applyRichEditFormatSelectionCommand(HWND hwnd, const std::string& command) {
    if (!hwnd || !IsWindow(hwnd)) return false;
    if (command == "bold") return toggleRichEditSelectionEffect(hwnd, CFM_BOLD, CFE_BOLD);
    if (command == "italic") return toggleRichEditSelectionEffect(hwnd, CFM_ITALIC, CFE_ITALIC);
    if (command == "underline") return toggleRichEditSelectionEffect(hwnd, CFM_UNDERLINE, CFE_UNDERLINE);
    if (command == "clear") return clearRichEditCharacterFormatting(hwnd);
    if (command == "align-left") return setRichEditParagraphAlignment(hwnd, PFA_LEFT);
    if (command == "align-center") return setRichEditParagraphAlignment(hwnd, PFA_CENTER);
    if (command == "align-right") return setRichEditParagraphAlignment(hwnd, PFA_RIGHT);
    if (command == "align-justify") return setRichEditParagraphAlignment(hwnd, PFA_JUSTIFY);
    if (command == "bullets") return toggleRichEditBullets(hwnd);
    if (command == "numbering") return toggleRichEditNumbering(hwnd);
    if (command == "indent") return adjustRichEditParagraphIndent(hwnd, 180);
    if (command == "outdent") return adjustRichEditParagraphIndent(hwnd, -180);
    return false;
}

LRESULT CALLBACK richEditSubclassProc(HWND hwnd,
                                      UINT message,
                                      WPARAM wparam,
                                      LPARAM lparam,
                                      UINT_PTR subclass_id,
                                      DWORD_PTR ref_data) {
    (void)subclass_id;
    (void)ref_data;
    ScopedNativeSession scoped(lookupNativeSessionForWindow(hwnd));

    enum class RichTextShortcut {
        None,
        Bold,
        Italic,
        Underline,
        AlignLeft,
        AlignCenter,
        AlignRight,
        AlignJustify,
        ToggleBullets,
        ToggleNumbering,
        Indent,
        Outdent,
        ClearCharacterFormatting,
        ClearParagraphFormatting,
        IncreaseFontSize,
        DecreaseFontSize,
    };

    auto rich_text_shortcut_for_key = [&](WPARAM key) -> RichTextShortcut {
        const bool ctrl_down = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        const bool shift_down = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        if (!ctrl_down && key == VK_TAB) {
            return shift_down ? RichTextShortcut::Outdent : RichTextShortcut::Indent;
        }
        switch (static_cast<int>(key)) {
        case 'B':
        case 0x02:
            return RichTextShortcut::Bold;
        case 'I':
        case 0x09:
            return RichTextShortcut::Italic;
        case 'U':
        case 0x15:
            return RichTextShortcut::Underline;
        case 'L':
        case 0x0C:
            return shift_down ? RichTextShortcut::ToggleBullets : RichTextShortcut::AlignLeft;
        case '7':
            return shift_down ? RichTextShortcut::ToggleNumbering : RichTextShortcut::None;
        case 'E':
        case 0x05:
            return RichTextShortcut::AlignCenter;
        case 'R':
        case 0x12:
            return RichTextShortcut::AlignRight;
        case 'J':
        case 0x0A:
            return RichTextShortcut::AlignJustify;
        case 'Q':
        case 0x11:
            return RichTextShortcut::ClearParagraphFormatting;
        case VK_SPACE:
            return RichTextShortcut::ClearCharacterFormatting;
        case VK_OEM_PERIOD:
            return shift_down ? RichTextShortcut::IncreaseFontSize : RichTextShortcut::None;
        case VK_OEM_COMMA:
            return shift_down ? RichTextShortcut::DecreaseFontSize : RichTextShortcut::None;
        default:
            return RichTextShortcut::None;
        }
    };

    if (message == WM_GETDLGCODE) {
        return DefSubclassProc(hwnd, message, wparam, lparam) | DLGC_WANTTAB;
    }

    if (message == WM_KEYDOWN) {
        auto it = g_native.bindings.find(hwnd);
        if (it != g_native.bindings.end() && it->second.type == "rich-text") {
            bool handled = false;
            switch (rich_text_shortcut_for_key(wparam)) {
            case RichTextShortcut::Bold:
                handled = toggleRichEditSelectionEffect(hwnd, CFM_BOLD, CFE_BOLD);
                break;
            case RichTextShortcut::Italic:
                handled = toggleRichEditSelectionEffect(hwnd, CFM_ITALIC, CFE_ITALIC);
                break;
            case RichTextShortcut::Underline:
                handled = toggleRichEditSelectionEffect(hwnd, CFM_UNDERLINE, CFE_UNDERLINE);
                break;
            case RichTextShortcut::AlignLeft:
                handled = setRichEditParagraphAlignment(hwnd, PFA_LEFT);
                break;
            case RichTextShortcut::AlignCenter:
                handled = setRichEditParagraphAlignment(hwnd, PFA_CENTER);
                break;
            case RichTextShortcut::AlignRight:
                handled = setRichEditParagraphAlignment(hwnd, PFA_RIGHT);
                break;
            case RichTextShortcut::AlignJustify:
                handled = setRichEditParagraphAlignment(hwnd, PFA_JUSTIFY);
                break;
            case RichTextShortcut::ToggleBullets:
                handled = toggleRichEditBullets(hwnd);
                break;
            case RichTextShortcut::ToggleNumbering:
                handled = toggleRichEditNumbering(hwnd);
                break;
            case RichTextShortcut::Indent:
                handled = adjustRichEditParagraphIndent(hwnd, 180);
                break;
            case RichTextShortcut::Outdent:
                handled = adjustRichEditParagraphIndent(hwnd, -180);
                break;
            case RichTextShortcut::ClearCharacterFormatting:
                handled = clearRichEditCharacterFormatting(hwnd);
                break;
            case RichTextShortcut::ClearParagraphFormatting:
                handled = clearRichEditParagraphFormatting(hwnd);
                break;
            case RichTextShortcut::IncreaseFontSize:
                handled = adjustRichEditSelectionFontSize(hwnd, 20);
                break;
            case RichTextShortcut::DecreaseFontSize:
                handled = adjustRichEditSelectionFontSize(hwnd, -20);
                break;
            case RichTextShortcut::None:
                break;
            }

            if (handled) {
                dispatchRichTextChangeEvent(hwnd, it->second);
                return 0;
            }
        }
    }

    if (message == WM_CHAR || message == WM_SYSCHAR) {
        const bool ctrl_down = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        if ((ctrl_down && rich_text_shortcut_for_key(wparam) != RichTextShortcut::None) || wparam == VK_TAB) {
            return 0;
        }
    }

    if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, richEditSubclassProc, kRichEditSubclassId);
    }
    return DefSubclassProc(hwnd, message, wparam, lparam);
}

void attachRichEditSubclass(HWND hwnd) {
    if (!hwnd) return;
    SetWindowSubclass(hwnd, richEditSubclassProc, kRichEditSubclassId, 0);
}

EditViewState captureEditViewState(HWND hwnd) {
    EditViewState state;
    SendMessageW(hwnd, EM_GETSEL,
                 reinterpret_cast<WPARAM>(&state.selection_start),
                 reinterpret_cast<LPARAM>(&state.selection_end));
    state.first_visible_line = static_cast<int>(SendMessageW(hwnd, EM_GETFIRSTVISIBLELINE, 0, 0));
    return state;
}

void restoreEditViewState(HWND hwnd, const EditViewState& state) {
    SendMessageW(hwnd,
                 EM_SETSEL,
                 static_cast<WPARAM>(state.selection_start),
                 static_cast<LPARAM>(state.selection_end));
    const int current_first_visible = static_cast<int>(SendMessageW(hwnd, EM_GETFIRSTVISIBLELINE, 0, 0));
    const int delta = state.first_visible_line - current_first_visible;
    if (delta != 0) {
        SendMessageW(hwnd, EM_LINESCROLL, 0, static_cast<LPARAM>(delta));
    }
}

bool tryParseUtf8Number(const std::string& text, ordered_json& out) {
    const std::string trimmed = trimAsciiWhitespace(text);
    if (trimmed.empty()) return false;
    char* end = nullptr;
    const double value = std::strtod(trimmed.c_str(), &end);
    if (!end || *end != '\0') return false;
    const double rounded = std::round(value);
    if (std::fabs(value - rounded) < 1e-9) {
        out = static_cast<int64_t>(rounded);
    } else {
        out = value;
    }
    return true;
}

bool parseIsoDateString(const std::string& text, SYSTEMTIME& out) {
    if (text.size() != 10 || text[4] != '-' || text[7] != '-') return false;
    const int year = std::atoi(text.substr(0, 4).c_str());
    const int month = std::atoi(text.substr(5, 2).c_str());
    const int day = std::atoi(text.substr(8, 2).c_str());
    if (year <= 0 || month < 1 || month > 12 || day < 1 || day > 31) return false;
    SYSTEMTIME st{};
    st.wYear = static_cast<WORD>(year);
    st.wMonth = static_cast<WORD>(month);
    st.wDay = static_cast<WORD>(day);
    FILETIME ft{};
    if (!SystemTimeToFileTime(&st, &ft)) return false;
    out = st;
    return true;
}

std::string formatIsoDateString(const SYSTEMTIME& st) {
    char buffer[16]{};
    std::snprintf(buffer, sizeof(buffer), "%04u-%02u-%02u",
                  static_cast<unsigned>(st.wYear),
                  static_cast<unsigned>(st.wMonth),
                  static_cast<unsigned>(st.wDay));
    return std::string(buffer);
}

bool parseIsoTimeString(const std::string& text, SYSTEMTIME& out) {
    if (text.size() != 5 || text[2] != ':') return false;
    const int hour = std::atoi(text.substr(0, 2).c_str());
    const int minute = std::atoi(text.substr(3, 2).c_str());
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59) return false;
    SYSTEMTIME st{};
    st.wYear = 2000;
    st.wMonth = 1;
    st.wDay = 1;
    st.wHour = static_cast<WORD>(hour);
    st.wMinute = static_cast<WORD>(minute);
    FILETIME ft{};
    if (!SystemTimeToFileTime(&st, &ft)) return false;
    out = st;
    return true;
}

std::string formatIsoTimeString(const SYSTEMTIME& st) {
    char buffer[16]{};
    std::snprintf(buffer, sizeof(buffer), "%02u:%02u",
                  static_cast<unsigned>(st.wHour),
                  static_cast<unsigned>(st.wMinute));
    return std::string(buffer);
}

bool applyDatePickerRange(HWND hwnd, const ordered_json& data) {
    SYSTEMTIME range[2]{};
    DWORD flags = 0;
    auto min_it = data.find("min");
    if (min_it != data.end() && min_it->is_string() && parseIsoDateString(min_it->get<std::string>(), range[0])) {
        flags |= GDTR_MIN;
    }
    auto max_it = data.find("max");
    if (max_it != data.end() && max_it->is_string() && parseIsoDateString(max_it->get<std::string>(), range[1])) {
        flags |= GDTR_MAX;
    }
    DateTime_SetRange(hwnd, flags, flags ? range : nullptr);
    return true;
}

bool applyTimePickerRange(HWND hwnd, const ordered_json& data) {
    SYSTEMTIME range[2]{};
    DWORD flags = 0;
    auto min_it = data.find("min");
    if (min_it != data.end() && min_it->is_string() && parseIsoTimeString(min_it->get<std::string>(), range[0])) {
        flags |= GDTR_MIN;
    }
    auto max_it = data.find("max");
    if (max_it != data.end() && max_it->is_string() && parseIsoTimeString(max_it->get<std::string>(), range[1])) {
        flags |= GDTR_MAX;
    }
    DateTime_SetRange(hwnd, flags, flags ? range : nullptr);
    return true;
}

const ordered_json* jsonObjectChild(const ordered_json& node, const char* key) {
    auto it = node.find(key);
    if (it == node.end() || !it->is_object()) return nullptr;
    return &(*it);
}

const ordered_json* jsonArrayChild(const ordered_json& node, const char* key) {
    auto it = node.find(key);
    if (it == node.end() || !it->is_array()) return nullptr;
    return &(*it);
}

std::vector<const ordered_json*> childrenOf(const ordered_json& node) {
    std::vector<const ordered_json*> out;
    if (const ordered_json* children = jsonArrayChild(node, "children")) {
        for (const auto& child : *children) {
            if (child.is_object()) out.push_back(&child);
        }
    }
    return out;
}

std::wstring nodeLabel(const ordered_json& node, const char* fallback_key = "text") {
    std::string text = jsonString(node, fallback_key);
    if (text.empty()) text = jsonString(node, "title");
    if (text.empty()) text = jsonString(node, "label");
    if (text.empty()) text = "[native renderer pending]";
    return utf8ToWide(text);
}

SIZE measureWrappedText(HDC hdc, HFONT font, const std::wstring& text, int max_width) {
    RECT rc{0, 0, std::max(max_width, 64), 1};
    HFONT previous = static_cast<HFONT>(SelectObject(hdc, font));
    DrawTextW(hdc, text.c_str(), static_cast<int>(text.size()), &rc, DT_CALCRECT | DT_WORDBREAK | DT_EDITCONTROL);
    SelectObject(hdc, previous);
    SIZE size{};
    size.cx = std::max(rc.right - rc.left, 32L);
    size.cy = std::max(rc.bottom - rc.top, 18L);
    return size;
}

SIZE measureSingleLineText(HDC hdc, HFONT font, const std::wstring& text) {
    SIZE size{};
    HFONT previous = static_cast<HFONT>(SelectObject(hdc, font));
    GetTextExtentPoint32W(hdc, text.c_str(), static_cast<int>(text.size()), &size);
    SelectObject(hdc, previous);
    size.cx = std::max(size.cx, 16L);
    size.cy = std::max(size.cy, 18L);
    return size;
}

MeasuredSize measureNode(HDC hdc, const ordered_json& node, int max_width);

MeasuredSize measureChildrenStack(HDC hdc, const std::vector<const ordered_json*>& children, int max_width, int gap) {
    MeasuredSize result{};
    bool first = true;
    for (const ordered_json* child : children) {
        MeasuredSize child_size = measureNode(hdc, *child, max_width);
        result.width = std::max(result.width, child_size.width);
        result.height += child_size.height;
        if (!first) result.height += gap;
        first = false;
    }
    return result;
}

std::vector<int> computeRowChildWidths(HDC hdc,
                                       const std::vector<const ordered_json*>& children,
                                       int available_width,
                                       int min_child_width) {
    std::vector<int> widths;
    if (children.empty()) return widths;

    const int safe_available = std::max(static_cast<int>(children.size()) * min_child_width, available_width);
    widths.reserve(children.size());

    int natural_total = 0;
    for (const ordered_json* child : children) {
        const MeasuredSize natural = measureNode(hdc, *child, safe_available);
        const int width = std::max(min_child_width, natural.width);
        widths.push_back(width);
        natural_total += width;
    }

    if (natural_total <= safe_available) {
        return widths;
    }

    int scaled_total = 0;
    for (int& width : widths) {
        width = std::max(min_child_width, static_cast<int>((static_cast<long long>(width) * safe_available) / std::max(1, natural_total)));
        scaled_total += width;
    }

    while (scaled_total > safe_available) {
        size_t widest_index = widths.size();
        int widest_width = min_child_width;
        for (size_t i = 0; i < widths.size(); ++i) {
            if (widths[i] > widest_width) {
                widest_width = widths[i];
                widest_index = i;
            }
        }
        if (widest_index == widths.size()) break;
        --widths[widest_index];
        --scaled_total;
    }

    return widths;
}

double splitPaneSizeValue(const ordered_json& pane, double fallback) {
    auto size_it = pane.find("size");
    if (size_it == pane.end()) return fallback;
    if (size_it->is_number_integer()) return static_cast<double>(size_it->get<int>());
    if (size_it->is_number_float()) return size_it->get<double>();
    return fallback;
}

bool splitPaneCollapsed(const ordered_json& pane) {
    auto collapsed_it = pane.find("collapsed");
    if (collapsed_it == pane.end()) return false;
    if (collapsed_it->is_boolean()) return collapsed_it->get<bool>();
    if (collapsed_it->is_string()) return collapsed_it->get<std::string>() == "true";
    if (collapsed_it->is_number_integer()) return collapsed_it->get<int>() != 0;
    return false;
}

struct SplitLayoutState {
    std::string orientation = "horizontal";
    int divider_size = 8;
    int total_primary = 0;
    int total_secondary = 0;
    int available_primary = 0;
    int first_primary = 0;
    int second_primary = 0;
    int first_min = 0;
    int second_min = 0;
    int first_max = 0;
    int second_max = 0;
    bool first_collapsed = false;
    bool second_collapsed = false;
    bool live_resize = true;
    bool disabled = false;
    double first_weight = 0.5;
    double second_weight = 0.5;
};

int splitPaneMaxSize(const ordered_json& pane, int fallback) {
    auto max_it = pane.find("maxSize");
    if (max_it == pane.end()) return fallback;
    if (max_it->is_number_integer()) return std::max(0, max_it->get<int>());
    if (max_it->is_number_float()) return std::max(0, static_cast<int>(std::round(max_it->get<double>())));
    return fallback;
}

SplitLayoutState computeSplitLayoutState(const ordered_json& node,
                                         int total_width,
                                         int total_height,
                                         int override_first_primary = -1) {
    SplitLayoutState layout;
    const auto panes = childrenOf(node);
    if (panes.size() < 2) return layout;

    layout.orientation = jsonString(node, "orientation", "horizontal");
    layout.divider_size = std::max(4, jsonInt(node, "dividerSize", 8));
    layout.total_primary = layout.orientation == "vertical" ? total_height : total_width;
    layout.total_secondary = layout.orientation == "vertical" ? total_width : total_height;
    layout.available_primary = std::max(0, layout.total_primary - layout.divider_size);
    layout.live_resize = !node.contains("liveResize") || isTruthyJson(node["liveResize"]);
    layout.disabled = node.contains("disabled") && isTruthyJson(node["disabled"]);

    const ordered_json& first_pane = *panes[0];
    const ordered_json& second_pane = *panes[1];
    layout.first_collapsed = splitPaneCollapsed(first_pane);
    layout.second_collapsed = splitPaneCollapsed(second_pane);
    layout.first_weight = layout.first_collapsed ? 0.0 : std::max(0.0, splitPaneSizeValue(first_pane, 0.5));
    layout.second_weight = layout.second_collapsed ? 0.0 : std::max(0.0, splitPaneSizeValue(second_pane, 0.5));
    layout.first_min = layout.first_collapsed ? 0 : std::max(0, jsonInt(first_pane, "minSize", 40));
    layout.second_min = layout.second_collapsed ? 0 : std::max(0, jsonInt(second_pane, "minSize", 40));
    layout.first_max = layout.first_collapsed ? 0 : std::min(layout.available_primary, splitPaneMaxSize(first_pane, layout.available_primary));
    layout.second_max = layout.second_collapsed ? 0 : std::min(layout.available_primary, splitPaneMaxSize(second_pane, layout.available_primary));

    if (layout.first_collapsed && layout.second_collapsed) {
        layout.first_primary = 0;
        layout.second_primary = layout.available_primary;
        layout.first_weight = 0.0;
        layout.second_weight = 1.0;
        return layout;
    }
    if (layout.first_collapsed) {
        layout.first_primary = 0;
        layout.second_primary = layout.available_primary;
        layout.first_weight = 0.0;
        layout.second_weight = 1.0;
        return layout;
    }
    if (layout.second_collapsed) {
        layout.first_primary = layout.available_primary;
        layout.second_primary = 0;
        layout.first_weight = 1.0;
        layout.second_weight = 0.0;
        return layout;
    }

    const int lower_bound = std::max(layout.first_min, std::max(0, layout.available_primary - layout.second_max));
    const int upper_bound = std::max(lower_bound, std::min(layout.first_max, layout.available_primary - layout.second_min));
    if (override_first_primary >= 0) {
        layout.first_primary = std::clamp(override_first_primary, lower_bound, upper_bound);
    } else {
        const double total_weight = std::max(0.0001, layout.first_weight + layout.second_weight);
        const int preferred = static_cast<int>(std::round((static_cast<double>(layout.available_primary) * layout.first_weight) / total_weight));
        layout.first_primary = std::clamp(preferred, lower_bound, upper_bound);
    }
    layout.second_primary = std::max(0, layout.available_primary - layout.first_primary);

    if (layout.available_primary > 0) {
        layout.first_weight = static_cast<double>(layout.first_primary) / static_cast<double>(layout.available_primary);
        layout.second_weight = static_cast<double>(layout.second_primary) / static_cast<double>(layout.available_primary);
    } else {
        layout.first_weight = 0.5;
        layout.second_weight = 0.5;
    }
    return layout;
}

MeasuredSize measureNode(HDC hdc, const ordered_json& node, int max_width) {
    const std::string type = jsonString(node, "type");
    if (type == "stack" || type == "toolbar" || type == "form") {
        const int padding = jsonInt(node, "padding", 0);
        const int gap = jsonInt(node, "gap", type == "form" ? 10 : kDefaultGap);
        MeasuredSize child = measureChildrenStack(hdc, childrenOf(node), std::max(64, max_width - (padding * 2)), gap);
        return {child.width + (padding * 2), child.height + (padding * 2)};
    }

    if (type == "row") {
        const int padding = jsonInt(node, "padding", 0);
        const int gap = jsonInt(node, "gap", kDefaultGap);
        const auto children = childrenOf(node);
        MeasuredSize result{};
        const int available_width = std::max(72, max_width - (padding * 2) - std::max(0, static_cast<int>(children.size()) - 1) * gap);
        const std::vector<int> child_widths = computeRowChildWidths(hdc, children, available_width, 72);
        bool first = true;
        for (size_t i = 0; i < children.size(); ++i) {
            const ordered_json* child = children[i];
            const int child_limit = i < child_widths.size() ? child_widths[i] : 72;
            MeasuredSize child_size = measureNode(hdc, *child, child_limit);
            result.width += child_size.width;
            if (!first) result.width += gap;
            result.height = std::max(result.height, child_size.height);
            first = false;
        }
        return {result.width + (padding * 2), result.height + (padding * 2)};
    }

    if (type == "grid") {
        const int padding = std::max(0, jsonInt(node, "padding", 0));
        const int gap = std::max(0, jsonInt(node, "gap", kDefaultGap));
        const int column_count = std::max(1, jsonInt(node, "columns", 2));
        const GridMetrics metrics = measureGridChildren(hdc, childrenOf(node), column_count, std::max(72, max_width - (padding * 2)), gap);
        int width = padding * 2;
        for (size_t i = 0; i < metrics.column_widths.size(); ++i) {
            width += metrics.column_widths[i];
            if (i + 1 < metrics.column_widths.size()) width += gap;
        }
        int height = padding * 2;
        for (size_t i = 0; i < metrics.row_heights.size(); ++i) {
            height += metrics.row_heights[i];
            if (i + 1 < metrics.row_heights.size()) height += gap;
        }
        return {width, height};
    }

    if (type == "scroll-view") {
        const int padding = std::max(0, jsonInt(node, "padding", 0));
        const int gap = std::max(0, jsonInt(node, "gap", kDefaultGap));
        const int explicit_width = jsonInt(node, "width", 0);
        const int explicit_height = jsonInt(node, "height", 0);
        const int view_width = explicit_width > 0 ? explicit_width : std::max(160, max_width);
        MeasuredSize child = measureChildrenStack(hdc, childrenOf(node), std::max(72, view_width - (padding * 2)), gap);
        const int width = explicit_width > 0 ? explicit_width : std::min(max_width, std::max(160, child.width + (padding * 2)));
        const int height = explicit_height > 0 ? explicit_height : std::clamp(child.height + (padding * 2), 120, 320);
        return {width, height};
    }

    if (type == "text") {
        const SIZE size = measureWrappedText(hdc, g_native.ui_font, nodeLabel(node), max_width);
        return {std::min(max_width, static_cast<int>(size.cx)), static_cast<int>(size.cy)};
    }

    if (type == "heading") {
        const SIZE size = measureWrappedText(hdc, g_native.heading_font, nodeLabel(node), max_width);
        return {std::min(max_width, static_cast<int>(size.cx)), static_cast<int>(size.cy + 4)};
    }

    if (type == "link") {
        const SIZE size = measureWrappedText(hdc, g_native.ui_font, nodeLabel(node), max_width);
        return {std::min(max_width, static_cast<int>(size.cx)), static_cast<int>(size.cy + 2)};
    }

    if (type == "image") {
        const std::wstring label = utf8ToWide(jsonString(node, "label"));
        int width = std::max(24, jsonInt(node, "width", 240));
        int height = std::max(24, jsonInt(node, "height", 160));
        if (!label.empty()) {
            const SIZE label_size = measureWrappedText(hdc, g_native.ui_font, label, width);
            width = std::max(width, static_cast<int>(label_size.cx));
            height += static_cast<int>(label_size.cy + 8);
        }
        return {width, height};
    }

    if (type == "button") {
        const SIZE size = measureSingleLineText(hdc, g_native.ui_font, nodeLabel(node));
        return {static_cast<int>(size.cx + 28), static_cast<int>(size.cy + 16)};
    }

    if (type == "checkbox" || type == "switch") {
        const std::wstring label = nodeLabel(node, "label");
        const SIZE size = measureSingleLineText(hdc, g_native.ui_font, label);
        return {std::min(max_width, static_cast<int>(size.cx + 32)), static_cast<int>(size.cy + 12)};
    }

    if (type == "input" || type == "number-input" || type == "date-picker" || type == "time-picker" || type == "select") {
        const std::wstring label = utf8ToWide(jsonString(node, "label"));
        int width = std::max(220, std::min(max_width, 360));
        int height = 28;
        if (!label.empty()) {
            const SIZE label_size = measureWrappedText(hdc, g_native.ui_font, label, width);
            width = std::max(width, static_cast<int>(label_size.cx));
            height += static_cast<int>(label_size.cy + 8);
        }
        return {width, height};
    }

    if (type == "textarea") {
        const std::wstring label = utf8ToWide(jsonString(node, "label"));
        const int rows = std::max(2, jsonInt(node, "rows", 4));
        int width = std::max(280, std::min(max_width, 420));
        int height = std::max(78, (rows * 22) + 12);
        if (!label.empty()) {
            const SIZE label_size = measureWrappedText(hdc, g_native.ui_font, label, width);
            width = std::max(width, static_cast<int>(label_size.cx));
            height += static_cast<int>(label_size.cy + 8);
        }
        return {width, height};
    }

    if (type == "rich-text") {
        const std::wstring label = utf8ToWide(jsonString(node, "label"));
        int width = std::max(320, std::min(max_width, 520));
        int height = std::max(120, jsonInt(node, "minHeight", 140));
        if (!label.empty()) {
            const SIZE label_size = measureWrappedText(hdc, g_native.ui_font, label, width);
            width = std::max(width, static_cast<int>(label_size.cx));
            height += static_cast<int>(label_size.cy + 8);
        }
        return {width, height};
    }

    if (type == "canvas") {
        const std::wstring label = utf8ToWide(jsonString(node, "label"));
        int width = std::max(80, jsonInt(node, "width", 320));
        int height = std::max(60, jsonInt(node, "height", 180));
        if (!label.empty()) {
            const SIZE label_size = measureWrappedText(hdc, g_native.ui_font, label, width);
            width = std::max(width, static_cast<int>(label_size.cx));
            height += static_cast<int>(label_size.cy + 8);
        }
        return {width, height};
    }

    if (type == "text-grid" || type == "text-grid-pane") {
        const std::wstring label = utf8ToWide(jsonString(node, "label"));
        int width = std::max(120, jsonInt(node, "width", std::max(160, jsonInt(node, "columns", 80) * 10)));
        int height = std::max(80, jsonInt(node, "height", std::max(80, jsonInt(node, "rows", 25) * 18)));
        if (!label.empty()) {
            const SIZE label_size = measureWrappedText(hdc, g_native.ui_font, label, width);
            width = std::max(width, static_cast<int>(label_size.cx));
            height += static_cast<int>(label_size.cy + 8);
        }
        return {width, height};
    }

    if (type == "indexed-graphics" || type == "rgba-pane" || type == "pane") {
        const std::wstring label = utf8ToWide(jsonString(node, "label"));
        int width = std::max(120, jsonInt(node, "width", 320));
        int height = std::max(80, jsonInt(node, "height", 180));
        if (!label.empty()) {
            const SIZE label_size = measureWrappedText(hdc, g_native.ui_font, label, width);
            width = std::max(width, static_cast<int>(label_size.cx));
            height += static_cast<int>(label_size.cy + 8);
        }
        return {width, height};
    }

    if (type == "tabs") {
        const std::wstring label = utf8ToWide(jsonString(node, "label"));
        int width = std::max(320, std::min(max_width, 700));
        int height = 34;
        if (!label.empty()) {
            const SIZE label_size = measureWrappedText(hdc, g_native.ui_font, label, width);
            width = std::max(width, static_cast<int>(label_size.cx));
            height += static_cast<int>(label_size.cy + 8);
        }
        const ordered_json* tabs = jsonArrayChild(node, "tabs");
        if (tabs && !tabs->empty()) {
            std::string active_value = jsonString(node, "value");
            const ordered_json* active_tab = nullptr;
            for (const auto& tab : *tabs) {
                if (!tab.is_object()) continue;
                if (!active_tab) active_tab = &tab;
                if (!active_value.empty() && jsonString(tab, "value") == active_value) {
                    active_tab = &tab;
                    break;
                }
            }
            if (active_tab) {
                auto content_it = active_tab->find("content");
                if (content_it != active_tab->end() && content_it->is_object()) {
                    MeasuredSize content_size = measureNode(hdc, *content_it, width);
                    height += 34 + 8 + content_size.height;
                    width = std::max(width, content_size.width);
                } else {
                    height += 80;
                }
            }
        }
        return {width, height};
    }

    if (type == "split-pane") {
        const int padding = std::max(0, jsonInt(node, "padding", 0));
        const int gap = std::max(0, jsonInt(node, "gap", kDefaultGap));
        MeasuredSize child = measureChildrenStack(hdc, childrenOf(node), std::max(64, max_width - (padding * 2)), gap);
        return {child.width + (padding * 2), child.height + (padding * 2)};
    }

    if (type == "split-view") {
        const auto panes = childrenOf(node);
        const std::string orientation = jsonString(node, "orientation", "horizontal");
        const int divider_size = std::max(4, jsonInt(node, "dividerSize", 8));
        const int explicit_height = jsonInt(node, "height", 0);
        const int explicit_width = jsonInt(node, "width", 0);
        if (panes.size() >= 2) {
            MeasuredSize first = measureNode(hdc, *panes[0], std::max(64, max_width / 2));
            MeasuredSize second = measureNode(hdc, *panes[1], std::max(64, max_width / 2));
            if (orientation == "vertical") {
                const int width = explicit_width > 0 ? explicit_width : std::max(first.width, second.width);
                const int height = explicit_height > 0 ? explicit_height : std::max(220, first.height + second.height + divider_size);
                return {std::min(std::max(width, 220), std::max(220, max_width)), height};
            }
            const int width = explicit_width > 0 ? explicit_width : std::max(320, first.width + second.width + divider_size);
            const int height = explicit_height > 0 ? explicit_height : std::max(220, std::max(first.height, second.height));
            return {std::max(320, width), height};
        }
        return {std::max(320, std::min(max_width, 720)), explicit_height > 0 ? explicit_height : 240};
    }

    if (type == "table") {
        const std::wstring label = utf8ToWide(jsonString(node, "label"));
        const ordered_json* columns = jsonArrayChild(node, "columns");
        const ordered_json* rows = jsonArrayChild(node, "rows");
        const int column_count = columns ? std::max(1, static_cast<int>(columns->size())) : 1;
        const int row_count = rows ? static_cast<int>(rows->size()) : 0;
        int width = std::max(420, std::min(max_width, 820));
        int height = std::max(180 + std::min(row_count, 10) * 22, jsonInt(node, "height", 0));
        if (!label.empty()) {
            const SIZE label_size = measureWrappedText(hdc, g_native.ui_font, label, width);
            width = std::max(width, static_cast<int>(label_size.cx));
            height += static_cast<int>(label_size.cy + 8);
        }
        width = std::max(width, column_count * 120);
        return {width, height};
    }

    if (type == "context-menu") {
        const int gap = std::max(0, jsonInt(node, "gap", 0));
        MeasuredSize child = measureChildrenStack(hdc, childrenOf(node), std::max(64, max_width), gap);
        return {std::max(24, child.width), std::max(24, child.height)};
    }

    if (type == "tree-view") {
        const std::wstring label = utf8ToWide(jsonString(node, "label"));
        const int width = std::max(280, std::min(max_width, 520));
        int height = 220;
        std::function<int(const ordered_json&)> count_items = [&](const ordered_json& items) -> int {
            if (!items.is_array()) return 0;
            int total = 0;
            for (const auto& item : items) {
                if (!item.is_object()) continue;
                total += 1;
                auto children_it = item.find("children");
                if (children_it != item.end()) {
                    total += count_items(*children_it);
                }
            }
            return total;
        };
        if (const ordered_json* items = jsonArrayChild(node, "items")) {
            height += std::min(12, count_items(*items)) * 18;
        }
        if (!label.empty()) {
            const SIZE label_size = measureWrappedText(hdc, g_native.ui_font, label, width);
            height += static_cast<int>(label_size.cy + 8);
        }
        return {width, std::min(height, 420)};
    }

    if (type == "list-box" || type == "radio-group") {
        const std::wstring label = utf8ToWide(jsonString(node, "label"));
        const ordered_json* options = jsonArrayChild(node, "options");
        const int option_count = options ? std::max(1, static_cast<int>(options->size())) : 1;
        int width = std::max(240, std::min(max_width, 420));
        int height = 0;
        if (type == "list-box") {
            const int rows = std::max(3, jsonInt(node, "rows", 6));
            height = std::max(90, rows * 22 + 8);
        } else {
            height = option_count * 28;
        }
        if (!label.empty()) {
            const SIZE label_size = measureWrappedText(hdc, g_native.ui_font, label, width);
            width = std::max(width, static_cast<int>(label_size.cx));
            height += static_cast<int>(label_size.cy + 8);
        }
        return {width, height};
    }

    if (type == "slider") {
        const std::wstring label = utf8ToWide(jsonString(node, "label"));
        int width = std::max(260, std::min(max_width, 420));
        int height = 42;
        if (!label.empty()) {
            const SIZE label_size = measureWrappedText(hdc, g_native.ui_font, label, width);
            width = std::max(width, static_cast<int>(label_size.cx + 48));
            height += static_cast<int>(label_size.cy + 8);
        }
        return {width, height};
    }

    if (type == "progress") {
        const std::wstring label = utf8ToWide(jsonString(node, "label"));
        int width = std::max(240, std::min(max_width, 420));
        int height = 26;
        if (!label.empty()) {
            const SIZE label_size = measureWrappedText(hdc, g_native.ui_font, label, width);
            width = std::max(width, static_cast<int>(label_size.cx));
            height += static_cast<int>(label_size.cy + 8);
        }
        return {width, height};
    }

    if (type == "badge") {
        const SIZE text_size = measureWrappedText(hdc, g_native.ui_font, nodeLabel(node), std::max(80, max_width));
        return {std::min(max_width, static_cast<int>(text_size.cx) + 20), static_cast<int>(text_size.cy) + 10};
    }

    if (type == "card") {
        const int padding = 12;
        const int gap = 10;
        MeasuredSize title = {0, 0};
        const std::string title_text = jsonString(node, "title");
        if (!title_text.empty()) {
            const SIZE title_size = measureWrappedText(hdc, g_native.heading_font, utf8ToWide(title_text), std::max(64, max_width - (padding * 2)));
            title = {std::min(max_width, static_cast<int>(title_size.cx)), static_cast<int>(title_size.cy + 2)};
        }
        MeasuredSize child = measureChildrenStack(hdc, childrenOf(node), std::max(64, max_width - (padding * 2)), gap);
        const int width = std::max(title.width, child.width) + (padding * 2);
        const int height = title.height + (title.height > 0 && child.height > 0 ? gap : 0) + child.height + (padding * 2);
        return {width, std::max(height, 48)};
    }

    if (type == "divider") {
        return {std::max(24, max_width), 8};
    }

    const SIZE size = measureWrappedText(hdc, g_native.ui_font, nodeLabel(node), max_width);
    return {std::min(max_width, static_cast<int>(size.cx)), static_cast<int>(size.cy)};
}

HWND createChildControl(const wchar_t* class_name,
                        const wchar_t* text,
                        DWORD style,
                        int x,
                        int y,
                        int width,
                        int height,
                        HWND parent,
                        HFONT font) {
    HWND hwnd = CreateWindowExW(
        0,
        class_name,
        text,
        WS_CHILD | WS_VISIBLE | style,
        x,
        y,
        width,
        height,
        parent,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr);
    if (hwnd && font) {
        SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
    return hwnd;
}

LRESULT CALLBACK containerForwardSubclassProc(HWND hwnd,
                                              UINT message,
                                              WPARAM wparam,
                                              LPARAM lparam,
                                              UINT_PTR subclass_id,
                                              DWORD_PTR ref_data) {
    (void)subclass_id;
    (void)ref_data;
    ScopedNativeSession scoped(lookupNativeSessionForWindow(hwnd));
    auto forward_to_native_host = [&](UINT msg, WPARAM wp, LPARAM lp) -> LRESULT {
        HWND target = nullptr;
        {
            std::lock_guard<std::mutex> lock(g_native.mutex);
            target = g_native.hwnd;
        }
        if (target && target != hwnd) {
            return SendMessageW(target, msg, wp, lp);
        }
        return DefSubclassProc(hwnd, msg, wp, lp);
    };
    switch (message) {
    case WM_COMMAND:
    case WM_HSCROLL:
    case WM_VSCROLL:
    case WM_CONTEXTMENU:
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
        return forward_to_native_host(message, wparam, lparam);
    case WM_NOTIFY:
        return forward_to_native_host(message, wparam, lparam);
    case WM_NCDESTROY:
        RemoveWindowSubclass(hwnd, containerForwardSubclassProc, kContainerSubclassId);
        break;
    }
    return DefSubclassProc(hwnd, message, wparam, lparam);
}

void attachContainerForwarding(HWND hwnd) {
    if (!hwnd) return;
    SetWindowSubclass(hwnd, containerForwardSubclassProc, kContainerSubclassId, 0);
}

LRESULT CALLBACK transparentPaneSubclassProc(HWND hwnd,
                                             UINT message,
                                             WPARAM wparam,
                                             LPARAM lparam,
                                             UINT_PTR subclass_id,
                                             DWORD_PTR ref_data) {
    (void)subclass_id;
    (void)ref_data;
    ScopedNativeSession scoped(lookupNativeSessionForWindow(hwnd));
    switch (message) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        {
            PAINTSTRUCT ps{};
            BeginPaint(hwnd, &ps);
            EndPaint(hwnd, &ps);
            return 0;
        }
    case WM_PRINTCLIENT:
        return 0;
    case WM_NCDESTROY:
        RemoveWindowSubclass(hwnd, transparentPaneSubclassProc, kTransparentPaneSubclassId);
        break;
    }
    return DefSubclassProc(hwnd, message, wparam, lparam);
}

void attachTransparentPaneSubclass(HWND hwnd) {
    if (!hwnd) return;
    SetWindowSubclass(hwnd, transparentPaneSubclassProc, kTransparentPaneSubclassId, 0);
}

bool splitViewHitDivider(const ControlBinding& binding, int x, int y) {
    const std::string orientation = jsonString(binding.data, "orientation", "horizontal");
    const int divider_size = std::max(4, jsonInt(binding.data, "dividerSize", 8));
    const int first_primary = std::max(0, jsonInt(binding.data, "firstPrimary", 0));
    if (orientation == "vertical") {
        return y >= first_primary && y < first_primary + divider_size;
    }
    return x >= first_primary && x < first_primary + divider_size;
}

void attachSplitViewSubclass(HWND hwnd);
void attachScrollViewSubclass(HWND hwnd);

HWND findBoundAncestorOfType(HWND start, const char* wanted_type) {
    HWND current = start;
    while (current) {
        auto it = g_native.bindings.find(current);
        if (it != g_native.bindings.end() && it->second.type == wanted_type) {
            return current;
        }
        current = GetParent(current);
    }
    return nullptr;
}

void snapshotScrollViewStateRecursive(HWND root) {
    if (!root || !IsWindow(root)) return;

    auto it = g_native.bindings.find(root);
    if (it != g_native.bindings.end() &&
        it->second.type == "scroll-view" &&
        !it->second.node_id.empty()) {
        g_native.preserved_scroll_y[it->second.node_id] = jsonInt(it->second.data, "scrollY", 0);
    }

    HWND child = GetWindow(root, GW_CHILD);
    while (child) {
        snapshotScrollViewStateRecursive(child);
        child = GetWindow(child, GW_HWNDNEXT);
    }
}

void restorePreservedScrollViewState(ControlBinding& binding) {
    if (binding.type != "scroll-view" || binding.node_id.empty()) return;

    auto it = g_native.preserved_scroll_y.find(binding.node_id);
    if (it == g_native.preserved_scroll_y.end()) return;

    binding.data["scrollY"] = it->second;
    g_native.preserved_scroll_y.erase(it);
}

void updateScrollViewViewport(HWND container, ControlBinding& binding) {
    if (!container || !IsWindow(container)) return;
    const int viewport_width = std::max(1, jsonInt(binding.data, "viewportWidth", 1));
    const int viewport_height = std::max(1, jsonInt(binding.data, "viewportHeight", 1));
    const int content_width = std::max(1, jsonInt(binding.data, "contentWidth", viewport_width));
    const int content_height = std::max(1, jsonInt(binding.data, "contentHeight", viewport_height));
    const int scroll_y = clampScrollOffset(jsonInt(binding.data, "scrollY", 0), viewport_height, content_height);
    binding.data["scrollY"] = scroll_y;

    ShowScrollBar(container, SB_VERT, content_height > viewport_height);

    SCROLLINFO si{};
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = std::max(0, content_height - 1);
    si.nPage = static_cast<UINT>(viewport_height);
    si.nPos = scroll_y;
    SetScrollInfo(container, SB_VERT, &si, TRUE);

    if (!binding.child_hwnds.empty() && IsWindow(binding.child_hwnds[0])) {
        MoveWindow(binding.child_hwnds[0],
                   0,
                   -scroll_y,
                   content_width,
                   content_height,
                   TRUE);
    }
}

bool scrollScrollViewByDelta(HWND container, ControlBinding& binding, int delta_pixels) {
    const int viewport_height = std::max(1, jsonInt(binding.data, "viewportHeight", 1));
    const int content_height = std::max(1, jsonInt(binding.data, "contentHeight", viewport_height));
    const int current = jsonInt(binding.data, "scrollY", 0);
    const int next = clampScrollOffset(current + delta_pixels, viewport_height, content_height);
    if (next == current) return true;
    binding.data["scrollY"] = next;
    updateScrollViewViewport(container, binding);
    return true;
}

bool handleScrollViewScrollBar(HWND container, ControlBinding& binding, WPARAM wparam) {
    SCROLLINFO si{};
    si.cbSize = sizeof(si);
    si.fMask = SIF_ALL;
    if (!GetScrollInfo(container, SB_VERT, &si)) return false;

    int next = si.nPos;
    switch (LOWORD(wparam)) {
    case SB_LINEUP:
        next -= 32;
        break;
    case SB_LINEDOWN:
        next += 32;
        break;
    case SB_PAGEUP:
        next -= static_cast<int>(si.nPage);
        break;
    case SB_PAGEDOWN:
        next += static_cast<int>(si.nPage);
        break;
    case SB_THUMBPOSITION:
    case SB_THUMBTRACK:
        next = si.nTrackPos;
        break;
    case SB_TOP:
        next = 0;
        break;
    case SB_BOTTOM:
        next = si.nMax;
        break;
    default:
        return false;
    }

    const int viewport_height = std::max(1, jsonInt(binding.data, "viewportHeight", 1));
    const int content_height = std::max(1, jsonInt(binding.data, "contentHeight", viewport_height));
    binding.data["scrollY"] = clampScrollOffset(next, viewport_height, content_height);
    updateScrollViewViewport(container, binding);
    return true;
}

LRESULT CALLBACK scrollViewSubclassProc(HWND hwnd,
                                        UINT message,
                                        WPARAM wparam,
                                        LPARAM lparam,
                                        UINT_PTR subclass_id,
                                        DWORD_PTR ref_data) {
    (void)subclass_id;
    (void)ref_data;
    ScopedNativeSession scoped(lookupNativeSessionForWindow(hwnd));
    switch (message) {
    case WM_VSCROLL:
        {
            auto it = g_native.bindings.find(hwnd);
            if (it != g_native.bindings.end() && it->second.type == "scroll-view" && handleScrollViewScrollBar(hwnd, it->second, wparam)) {
                return 0;
            }
        }
        break;
    case WM_MOUSEWHEEL:
        {
            auto it = g_native.bindings.find(hwnd);
            if (it != g_native.bindings.end() && it->second.type == "scroll-view") {
                const int delta = GET_WHEEL_DELTA_WPARAM(wparam);
                UINT lines = 3;
                SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
                int delta_pixels = 0;
                if (lines == WHEEL_PAGESCROLL) {
                    delta_pixels = (delta > 0 ? -1 : 1) * std::max(24, jsonInt(it->second.data, "viewportHeight", 1) - 48);
                } else {
                    const int steps = delta / WHEEL_DELTA;
                    delta_pixels = -steps * static_cast<int>(std::max<UINT>(1, lines)) * 32;
                }
                scrollScrollViewByDelta(hwnd, it->second, delta_pixels);
                return 0;
            }
        }
        break;
    case WM_NCDESTROY:
        RemoveWindowSubclass(hwnd, scrollViewSubclassProc, kScrollViewSubclassId);
        break;
    }
    return DefSubclassProc(hwnd, message, wparam, lparam);
}

void attachScrollViewSubclass(HWND hwnd) {
    if (!hwnd) return;
    SetWindowSubclass(hwnd, scrollViewSubclassProc, kScrollViewSubclassId, 0);
}

LRESULT CALLBACK splitViewSubclassProc(HWND hwnd,
                                       UINT message,
                                       WPARAM wparam,
                                       LPARAM lparam,
                                       UINT_PTR subclass_id,
                                       DWORD_PTR ref_data) {
    (void)subclass_id;
    (void)ref_data;
    ScopedNativeSession scoped(lookupNativeSessionForWindow(hwnd));
    switch (message) {
    case WM_SETCURSOR:
        if (LOWORD(lparam) == HTCLIENT) {
            auto it = g_native.bindings.find(hwnd);
            if (it != g_native.bindings.end() &&
                it->second.type == "split-view" &&
                !jsonString(it->second.data, "orientation").empty()) {
                POINT pt{};
                GetCursorPos(&pt);
                ScreenToClient(hwnd, &pt);
                const bool disabled = it->second.data.contains("disabled") && isTruthyJson(it->second.data["disabled"]);
                const bool draggable = !disabled &&
                    jsonDouble(it->second.data, "firstWeight", 0.5) > 0.0 &&
                    jsonDouble(it->second.data, "secondWeight", 0.5) > 0.0;
                if (draggable && splitViewHitDivider(it->second, pt.x, pt.y)) {
                    SetCursor(LoadCursorW(nullptr, jsonString(it->second.data, "orientation", "horizontal") == "vertical" ? IDC_SIZENS : IDC_SIZEWE));
                    return TRUE;
                }
            }
        }
        break;
    case WM_LBUTTONDOWN:
        {
            auto it = g_native.bindings.find(hwnd);
            if (it != g_native.bindings.end() && it->second.type == "split-view") {
                ControlBinding& binding = it->second;
                const bool disabled = binding.data.contains("disabled") && isTruthyJson(binding.data["disabled"]);
                if (!disabled) {
                    const int x = static_cast<int>(static_cast<short>(LOWORD(lparam)));
                    const int y = static_cast<int>(static_cast<short>(HIWORD(lparam)));
                    if (splitViewHitDivider(binding, x, y) &&
                        jsonDouble(binding.data, "firstWeight", 0.5) > 0.0 &&
                        jsonDouble(binding.data, "secondWeight", 0.5) > 0.0) {
                        binding.dragging = true;
                        binding.drag_origin = POINT{x, y};
                        binding.drag_first_primary = jsonInt(binding.data, "firstPrimary", 0);
                        SetCapture(hwnd);
                        return 0;
                    }
                }
            }
        }
        break;
    case WM_MOUSEMOVE:
        {
            auto it = g_native.bindings.find(hwnd);
            if (it != g_native.bindings.end() && it->second.type == "split-view" && it->second.dragging) {
                ControlBinding& binding = it->second;
                ordered_json node_spec = nullptr;
                {
                    std::lock_guard<std::mutex> lock(g_native.mutex);
                    ordered_json* found = nullptr;
                    if (!findUserAppNodeById(g_native.spec, binding.node_id, &found) || !found || !found->is_object()) {
                        return 0;
                    }
                    node_spec = *found;
                }
                const std::string orientation = jsonString(binding.data, "orientation", "horizontal");
                const int x = static_cast<int>(static_cast<short>(LOWORD(lparam)));
                const int y = static_cast<int>(static_cast<short>(HIWORD(lparam)));
                const int delta = orientation == "vertical" ? (y - binding.drag_origin.y) : (x - binding.drag_origin.x);
                relayoutSplitViewControl(hwnd, binding, node_spec, false, binding.drag_first_primary + delta);
                return 0;
            }
        }
        break;
    case WM_LBUTTONUP:
    case WM_CAPTURECHANGED:
        {
            auto it = g_native.bindings.find(hwnd);
            if (it != g_native.bindings.end() && it->second.type == "split-view" && it->second.dragging) {
                ControlBinding& binding = it->second;
                binding.dragging = false;
                if (GetCapture() == hwnd) {
                    ReleaseCapture();
                }
                ordered_json node_spec = nullptr;
                {
                    std::lock_guard<std::mutex> lock(g_native.mutex);
                    ordered_json* found = nullptr;
                    if (!findUserAppNodeById(g_native.spec, binding.node_id, &found) || !found || !found->is_object()) {
                        return 0;
                    }
                    node_spec = *found;
                }
                relayoutSplitViewControl(hwnd, binding, node_spec, true, jsonInt(binding.data, "firstPrimary", 0));
                {
                    std::lock_guard<std::mutex> lock(g_native.mutex);
                    const std::string first_id = binding.data.contains("paneIds") &&
                        binding.data["paneIds"].is_array() &&
                        binding.data["paneIds"].size() > 0 &&
                        binding.data["paneIds"][0].is_string()
                        ? binding.data["paneIds"][0].get<std::string>()
                        : "";
                    const std::string second_id = binding.data.contains("paneIds") &&
                        binding.data["paneIds"].is_array() &&
                        binding.data["paneIds"].size() > 1 &&
                        binding.data["paneIds"][1].is_string()
                        ? binding.data["paneIds"][1].get<std::string>()
                        : "";
                    if (!first_id.empty()) setUserAppNodeProp(g_native.spec, first_id, "size", binding.data["firstWeight"]);
                    if (!second_id.empty()) setUserAppNodeProp(g_native.spec, second_id, "size", binding.data["secondWeight"]);
                }
                if (!binding.event_name.empty()) {
                    ordered_json event = ordered_json::object();
                    event["type"] = "ui-event";
                    event["event"] = binding.event_name;
                    event["source"] = "native-win32";
                    if (!binding.node_id.empty()) event["id"] = binding.node_id;
                    event["orientation"] = jsonString(binding.data, "orientation", "horizontal");
                    event["sizes"] = buildSplitSizesPayload(binding);
                    dispatchUiEventJson(event);
                }
                return 0;
            }
        }
        break;
    case WM_NCDESTROY:
        RemoveWindowSubclass(hwnd, splitViewSubclassProc, kSplitViewSubclassId);
        break;
    }
    return DefSubclassProc(hwnd, message, wparam, lparam);
}

void attachSplitViewSubclass(HWND hwnd) {
    if (!hwnd) return;
    SetWindowSubclass(hwnd, splitViewSubclassProc, kSplitViewSubclassId, 0);
}

void registerNodeControl(HWND hwnd, const ControlBinding& binding) {
    if (!hwnd) return;
    g_native.bindings[hwnd] = binding;
    if (!binding.node_id.empty()) {
        g_native.node_controls[binding.node_id] = hwnd;
        if (shouldTraceWorkspacePaneNodeId(binding.node_id.c_str())) {
            char buffer[256];
            std::snprintf(buffer,
                          sizeof(buffer),
                          "register-node id=%s hwnd=%p type=%s",
                          binding.node_id.c_str(),
                          hwnd,
                          binding.type.c_str());
            traceNativePatchEvent(buffer);
        }
    }
}

void unregisterNodeControl(HWND hwnd) {
    auto binding_it = g_native.bindings.find(hwnd);
    if (binding_it != g_native.bindings.end()) {
        if (!binding_it->second.node_id.empty()) {
            auto node_it = g_native.node_controls.find(binding_it->second.node_id);
            if (node_it != g_native.node_controls.end() && node_it->second == hwnd) {
                g_native.node_controls.erase(node_it);
            }
        }
        g_native.bindings.erase(binding_it);
    }
}

void clearNativeChildrenOf(HWND parent) {
    HWND child = GetWindow(parent, GW_CHILD);
    while (child) {
        HWND next = GetWindow(child, GW_HWNDNEXT);
        clearNativeChildrenOf(child);
        unregisterNodeControl(child);
        DestroyWindow(child);
        child = next;
    }
}

ordered_json buildSplitSizesPayload(const ControlBinding& binding) {
    ordered_json sizes = ordered_json::array();
    auto pane_ids_it = binding.data.find("paneIds");
    if (pane_ids_it == binding.data.end() || !pane_ids_it->is_array() || pane_ids_it->size() < 2) {
        return sizes;
    }
    const std::string first_id = (*pane_ids_it)[0].is_string() ? (*pane_ids_it)[0].get<std::string>() : "";
    const std::string second_id = (*pane_ids_it)[1].is_string() ? (*pane_ids_it)[1].get<std::string>() : "";
    sizes.push_back(ordered_json{
        {"id", first_id},
        {"size", jsonDouble(binding.data, "firstWeight", 0.5)}
    });
    sizes.push_back(ordered_json{
        {"id", second_id},
        {"size", jsonDouble(binding.data, "secondWeight", 0.5)}
    });
    return sizes;
}

bool relayoutSplitViewControl(HWND container,
                              ControlBinding& binding,
                              const ordered_json& node,
                              bool rebuild_contents,
                              int override_first_primary) {
    if (!container || !IsWindow(container) || !node.is_object()) return false;
    const auto panes = childrenOf(node);
    if (panes.size() < 2) return false;

    RECT client{};
    GetClientRect(container, &client);
    const int width = std::max(24, static_cast<int>(client.right - client.left));
    const int height = std::max(24, static_cast<int>(client.bottom - client.top));
    const SplitLayoutState layout = computeSplitLayoutState(node, width, height, override_first_primary);

    if (binding.child_hwnds.size() != 3 ||
        !IsWindow(binding.child_hwnds[0]) ||
        !IsWindow(binding.child_hwnds[1]) ||
        !IsWindow(binding.child_hwnds[2])) {
        clearNativeChildrenOf(container);
        binding.child_hwnds.clear();

        HWND first_host = createChildControl(L"STATIC", L"", 0, 0, 0, 24, 24, container, nullptr);
        HWND divider = createChildControl(L"STATIC", L"", layout.orientation == "vertical" ? SS_ETCHEDHORZ : SS_ETCHEDVERT, 0, 0, 24, 24, container, nullptr);
        HWND second_host = createChildControl(L"STATIC", L"", 0, 0, 0, 24, 24, container, nullptr);
        if (!first_host || !divider || !second_host) return false;

        attachContainerForwarding(first_host);
        attachContainerForwarding(second_host);

        ControlBinding first_binding;
        first_binding.type = "split-pane";
        first_binding.node_id = jsonString(*panes[0], "id");
        registerNodeControl(first_host, first_binding);

        ControlBinding second_binding;
        second_binding.type = "split-pane";
        second_binding.node_id = jsonString(*panes[1], "id");
        registerNodeControl(second_host, second_binding);

        binding.child_hwnds = {first_host, divider, second_host};
        rebuild_contents = true;
    }

    binding.data["orientation"] = layout.orientation;
    binding.data["dividerSize"] = layout.divider_size;
    binding.data["firstWeight"] = layout.first_weight;
    binding.data["secondWeight"] = layout.second_weight;
    binding.data["firstPrimary"] = layout.first_primary;
    binding.data["secondPrimary"] = layout.second_primary;
    binding.data["availablePrimary"] = layout.available_primary;
    binding.data["disabled"] = layout.disabled;
    binding.data["liveResize"] = layout.live_resize;
    binding.data["paneIds"] = ordered_json::array({jsonString(*panes[0], "id"), jsonString(*panes[1], "id")});

    HWND first_host = binding.child_hwnds[0];
    HWND divider = binding.child_hwnds[1];
    HWND second_host = binding.child_hwnds[2];
    const DWORD divider_style = WS_CHILD | WS_VISIBLE | (layout.orientation == "vertical" ? SS_ETCHEDHORZ : SS_ETCHEDVERT);
    SetWindowLongPtrW(divider, GWL_STYLE, divider_style);

    if (layout.orientation == "vertical") {
        MoveWindow(first_host, 0, 0, layout.total_secondary, layout.first_primary, TRUE);
        MoveWindow(divider, 0, layout.first_primary, layout.total_secondary, layout.divider_size, TRUE);
        MoveWindow(second_host, 0, layout.first_primary + layout.divider_size, layout.total_secondary, layout.second_primary, TRUE);
    } else {
        MoveWindow(first_host, 0, 0, layout.first_primary, layout.total_secondary, TRUE);
        MoveWindow(divider, layout.first_primary, 0, layout.divider_size, layout.total_secondary, TRUE);
        MoveWindow(second_host, layout.first_primary + layout.divider_size, 0, layout.second_primary, layout.total_secondary, TRUE);
    }

    ShowWindow(first_host, layout.first_primary > 0 ? SW_SHOW : SW_HIDE);
    ShowWindow(second_host, layout.second_primary > 0 ? SW_SHOW : SW_HIDE);

    if (rebuild_contents) {
        auto rebuild_pane = [&](HWND pane_host, const ordered_json& pane) {
            clearNativeChildrenOf(pane_host);
            RECT pane_client{};
            GetClientRect(pane_host, &pane_client);
            HDC pane_hdc = GetDC(pane_host);
            if (!pane_hdc) return;
            ensureFonts();
            SelectObject(pane_hdc, g_native.ui_font);
            const int pane_width = std::max(24, static_cast<int>(pane_client.right - pane_client.left));
            const int padding = std::max(0, jsonInt(pane, "padding", 0));
            const int gap = std::max(0, jsonInt(pane, "gap", kDefaultGap));
            layoutChildrenStack(pane_host, pane_hdc, childrenOf(pane), padding, padding,
                std::max(24, pane_width - (padding * 2)), gap);
            ReleaseDC(pane_host, pane_hdc);
            InvalidateRect(pane_host, nullptr, TRUE);
        };
        rebuild_pane(first_host, *panes[0]);
        rebuild_pane(second_host, *panes[1]);
    } else {
        InvalidateRect(first_host, nullptr, TRUE);
        InvalidateRect(second_host, nullptr, TRUE);
    }

    InvalidateRect(divider, nullptr, TRUE);
    return true;
}

bool relayoutScrollViewControl(HWND container,
                               ControlBinding& binding,
                               const ordered_json& node,
                               bool rebuild_contents) {
    if (!container || !IsWindow(container) || !node.is_object()) return false;

    RECT client{};
    GetClientRect(container, &client);
    const int width = std::max(24, static_cast<int>(client.right - client.left));
    const int height = std::max(24, static_cast<int>(client.bottom - client.top));
    const int padding = std::max(0, jsonInt(node, "padding", 0));
    const int gap = std::max(0, jsonInt(node, "gap", kDefaultGap));
    const int scrollbar_width = std::max(12, GetSystemMetrics(SM_CXVSCROLL));

    if (binding.child_hwnds.size() != 1 || !IsWindow(binding.child_hwnds[0])) {
        clearNativeChildrenOf(container);
        binding.child_hwnds.clear();
        HWND content_host = createChildControl(L"STATIC", L"", 0, 0, 0, width, height, container, nullptr);
        if (!content_host) return false;
        attachContainerForwarding(content_host);
        binding.child_hwnds = {content_host};
        rebuild_contents = true;
    }

    HDC hdc = GetDC(container);
    if (!hdc) return false;
    ensureFonts();
    SelectObject(hdc, g_native.ui_font);

    int content_limit = std::max(72, width - (padding * 2));
    MeasuredSize child = measureChildrenStack(hdc, childrenOf(node), content_limit, gap);
    const bool needs_scroll_initial = child.height + (padding * 2) > height;
    if (needs_scroll_initial) {
        content_limit = std::max(72, width - (padding * 2) - scrollbar_width);
        child = measureChildrenStack(hdc, childrenOf(node), content_limit, gap);
    }
    ReleaseDC(container, hdc);

    const bool needs_scroll = child.height + (padding * 2) > height;
    const int viewport_width = std::max(24, width - (needs_scroll ? scrollbar_width : 0));
    const int content_width = std::max(24, viewport_width);
    const int content_height = std::max(height, child.height + (padding * 2));

    binding.data["viewportWidth"] = viewport_width;
    binding.data["viewportHeight"] = height;
    binding.data["contentWidth"] = content_width;
    binding.data["contentHeight"] = content_height;
    binding.data["scrollY"] = clampScrollOffset(jsonInt(binding.data, "scrollY", 0), height, content_height);

    HWND content_host = binding.child_hwnds[0];
    updateScrollViewViewport(container, binding);

    if (rebuild_contents) {
        clearNativeChildrenOf(content_host);
        HDC content_hdc = GetDC(content_host);
        if (!content_hdc) return false;
        ensureFonts();
        SelectObject(content_hdc, g_native.ui_font);
        layoutChildrenStack(content_host,
                            content_hdc,
                            childrenOf(node),
                            padding,
                            padding,
                            std::max(24, content_width - (padding * 2)),
                            gap);
        ReleaseDC(content_host, content_hdc);
    }

    InvalidateRect(content_host, nullptr, TRUE);
    return true;
}

bool refreshSplitViewControl(const std::string& node_id) {
    if (node_id.empty()) return false;

    ordered_json node_spec = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_native.mutex);
        ordered_json* found = nullptr;
        if (!findUserAppNodeById(g_native.spec, node_id, &found) || !found || !found->is_object()) {
            return false;
        }
        node_spec = *found;
    }

    auto control_it = g_native.node_controls.find(node_id);
    if (control_it == g_native.node_controls.end()) return false;
    auto binding_it = g_native.bindings.find(control_it->second);
    if (binding_it == g_native.bindings.end() || binding_it->second.type != "split-view") return false;
    return relayoutSplitViewControl(control_it->second, binding_it->second, node_spec, true);
}

bool refreshContainingSplitViewControl(HWND child_hwnd) {
    HWND current = child_hwnd;
    while (current) {
        auto it = g_native.bindings.find(current);
        if (it != g_native.bindings.end() && it->second.type == "split-view") {
            return refreshSplitViewControl(it->second.node_id);
        }
        current = GetParent(current);
    }
    return false;
}

void dispatchUiEventJson(const ordered_json& event) {
    traceNativeUiEvent(event);
    const std::string payload = event.dump();
    enqueueReactiveEvent(WINGUI_NATIVE_EVENT_DISPATCH_JSON, payload);
    auto fn = g_native_callbacks.dispatch_event_json;
    if (!fn) return;
    fn(payload.c_str());
}

void clearNativeLastError() {
    g_native_last_error.clear();
}

void setNativeLastError(const std::string& error) {
    g_native_last_error = error;
}

const char* nativeLastErrorUtf8() {
    return g_native_last_error.c_str();
}

bool copyReactiveEventToApi(const NativeQueuedEvent& queued, WinguiNativeEvent* out_event) {
    if (!out_event) return false;
    std::memset(out_event, 0, sizeof(*out_event));
    out_event->type = queued.type;
    out_event->sequence = queued.sequence;
    out_event->payload_size = static_cast<uint32_t>(queued.payload.size());
    char* payload = static_cast<char*>(std::malloc(static_cast<size_t>(out_event->payload_size) + 1));
    if (!payload) {
        setNativeLastError("Failed to allocate native-ui event payload buffer.");
        return false;
    }
    if (out_event->payload_size > 0) {
        std::memcpy(payload, queued.payload.data(), static_cast<size_t>(out_event->payload_size));
    }
    payload[out_event->payload_size] = '\0';
    out_event->payload_utf8 = payload;
    return true;
}

bool enqueueNativeCommandInternal(const WinguiNativeCommand* command) {
    if (!command) {
        setNativeLastError("Native UI command is required.");
        return false;
    }
    if (command->type == WINGUI_NATIVE_COMMAND_NONE) {
        setNativeLastError("Native UI command type is required.");
        return false;
    }
    if (commandTypeNeedsPayload(command->type) && !command->payload_utf8) {
        setNativeLastError("Native UI command payload is required.");
        return false;
    }
    std::lock_guard<std::mutex> lock(g_native.command_mutex);
    if (g_native.command_queue.size() >= kMaxQueuedNativeCommands) {
        setNativeLastError("Native UI command queue is full.");
        return false;
    }
    NativeQueuedCommand queued;
    queued.type = command->type;
    if (command->payload_utf8) queued.payload = command->payload_utf8;
    g_native.command_queue.push_back(std::move(queued));
    return true;
}

bool executeNativeCommand(const NativeQueuedCommand& command) {
    switch (command.type) {
        case WINGUI_NATIVE_COMMAND_PUBLISH_JSON:
            return executeNativePublishJson(command.payload.c_str());
        case WINGUI_NATIVE_COMMAND_PATCH_JSON:
            return executeNativePatchJson(command.payload.c_str());
        case WINGUI_NATIVE_COMMAND_SHOW_HOST:
            return executeNativeHostRun();
        case WINGUI_NATIVE_COMMAND_NONE:
        default:
            setNativeLastError("Unsupported native UI command type.");
            return false;
    }
}

std::wstring progressHeaderText(const ControlBinding& binding, const std::string& value_text) {
    if (binding.text.empty()) return L"";
    return utf8ToWide(binding.text + "  " + value_text);
}

std::wstring linkControlText(const std::string& text) {
    return L"<a>" + utf8ToWide(text.empty() ? "Link" : text) + L"</a>";
}

int hexDigitValue(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    if (ch >= 'a' && ch <= 'f') return 10 + (ch - 'a');
    return -1;
}

COLORREF parseHexColorString(const std::string& value, COLORREF fallback) {
    if (value.size() != 7 || value[0] != '#') return fallback;
    int digits[6]{};
    for (int i = 0; i < 6; ++i) {
        digits[i] = hexDigitValue(value[static_cast<size_t>(i + 1)]);
        if (digits[i] < 0) return fallback;
    }
    const int r = digits[0] * 16 + digits[1];
    const int g = digits[2] * 16 + digits[3];
    const int b = digits[4] * 16 + digits[5];
    return RGB(r, g, b);
}

void drawCanvasCommands(HDC hdc, const RECT& rc, const ordered_json& commands) {
    HBRUSH background = CreateSolidBrush(RGB(255, 255, 255));
    FillRect(hdc, &rc, background);
    DeleteObject(background);
    SetBkMode(hdc, TRANSPARENT);

    if (commands.is_array()) {
        for (const auto& command : commands) {
            if (!command.is_object()) continue;

            const std::string op = jsonString(command, "op");
            const COLORREF fill = parseHexColorString(jsonString(command, "fill"), RGB(255, 255, 255));
            const COLORREF stroke = parseHexColorString(jsonString(command, "stroke"), RGB(29, 36, 49));
            const int line_width = std::max(1, jsonInt(command, "lineWidth", 1));

            if (op == "clear") {
                HBRUSH brush = CreateSolidBrush(fill);
                FillRect(hdc, &rc, brush);
                DeleteObject(brush);
                continue;
            }

            if (op == "rect") {
                const int x = static_cast<int>(jsonDouble(command, "x", 0));
                const int y = static_cast<int>(jsonDouble(command, "y", 0));
                const int width = std::max(0, static_cast<int>(jsonDouble(command, "width", 0)));
                const int height = std::max(0, static_cast<int>(jsonDouble(command, "height", 0)));
                const bool has_fill = command.contains("fill");
                const bool has_stroke = command.contains("stroke");
                if (has_fill) {
                    HBRUSH brush = CreateSolidBrush(fill);
                    RECT box{x, y, x + width, y + height};
                    FillRect(hdc, &box, brush);
                    DeleteObject(brush);
                }
                if (has_stroke) {
                    HPEN pen = CreatePen(PS_SOLID, line_width, stroke);
                    HGDIOBJ previous_pen = SelectObject(hdc, pen);
                    HGDIOBJ previous_brush = SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
                    Rectangle(hdc, x, y, x + width, y + height);
                    SelectObject(hdc, previous_brush);
                    SelectObject(hdc, previous_pen);
                    DeleteObject(pen);
                }
                continue;
            }

            if (op == "line") {
                HPEN pen = CreatePen(PS_SOLID, line_width, stroke);
                HGDIOBJ previous_pen = SelectObject(hdc, pen);
                MoveToEx(hdc,
                         static_cast<int>(jsonDouble(command, "x1", 0)),
                         static_cast<int>(jsonDouble(command, "y1", 0)),
                         nullptr);
                LineTo(hdc,
                       static_cast<int>(jsonDouble(command, "x2", 0)),
                       static_cast<int>(jsonDouble(command, "y2", 0)));
                SelectObject(hdc, previous_pen);
                DeleteObject(pen);
                continue;
            }

            if (op == "circle") {
                const int cx = static_cast<int>(jsonDouble(command, "cx", 0));
                const int cy = static_cast<int>(jsonDouble(command, "cy", 0));
                const int radius = std::max(0, static_cast<int>(jsonDouble(command, "radius", 0)));
                HPEN pen = CreatePen(PS_SOLID, line_width, stroke);
                HBRUSH brush = command.contains("fill")
                    ? CreateSolidBrush(fill)
                    : static_cast<HBRUSH>(GetStockObject(HOLLOW_BRUSH));
                HGDIOBJ previous_pen = SelectObject(hdc, pen);
                HGDIOBJ previous_brush = SelectObject(hdc, brush);
                Ellipse(hdc, cx - radius, cy - radius, cx + radius, cy + radius);
                SelectObject(hdc, previous_brush);
                SelectObject(hdc, previous_pen);
                DeleteObject(pen);
                if (command.contains("fill")) DeleteObject(brush);
                continue;
            }

            if (op == "text") {
                const int size = std::max(10, jsonInt(command, "size", 16));
                HFONT font = CreateFontW(
                    -MulDiv(size, g_native.current_dpi > 0 ? g_native.current_dpi : 96, 96),
                    0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                    OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                    DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
                HGDIOBJ previous_font = SelectObject(hdc, font ? font : g_native.ui_font);
                const COLORREF previous_color = SetTextColor(hdc, fill);
                const std::wstring text = utf8ToWide(jsonString(command, "text"));
                TextOutW(hdc,
                         static_cast<int>(jsonDouble(command, "x", 0)),
                         static_cast<int>(jsonDouble(command, "y", 0)),
                         text.c_str(),
                         static_cast<int>(text.size()));
                SetTextColor(hdc, previous_color);
                SelectObject(hdc, previous_font);
                if (font) DeleteObject(font);
                continue;
            }
        }
    }

    HPEN border_pen = CreatePen(PS_SOLID, 1, RGB(199, 211, 227));
    HGDIOBJ previous_pen = SelectObject(hdc, border_pen);
    HGDIOBJ previous_brush = SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
    Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
    SelectObject(hdc, previous_brush);
    SelectObject(hdc, previous_pen);
    DeleteObject(border_pen);
}

LRESULT CALLBACK canvasSubclassProc(HWND hwnd,
                                    UINT message,
                                    WPARAM wparam,
                                    LPARAM lparam,
                                    UINT_PTR subclass_id,
                                    DWORD_PTR ref_data) {
    (void)subclass_id;
    (void)ref_data;
    ScopedNativeSession scoped(lookupNativeSessionForWindow(hwnd));
    switch (message) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        {
            PAINTSTRUCT ps{};
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc{};
            GetClientRect(hwnd, &rc);
            auto it = g_native.bindings.find(hwnd);
            if (it != g_native.bindings.end()) {
                auto commands_it = it->second.data.find("commands");
                const ordered_json& commands = (commands_it != it->second.data.end()) ? *commands_it : ordered_json::array();
                drawCanvasCommands(hdc, rc, commands);
            } else {
                HBRUSH brush = CreateSolidBrush(RGB(255, 255, 255));
                FillRect(hdc, &rc, brush);
                DeleteObject(brush);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
    case WM_LBUTTONUP:
        if (g_native.suppress_events) return 0;
        {
            auto it = g_native.bindings.find(hwnd);
            if (it != g_native.bindings.end() && !it->second.event_name.empty()) {
                ordered_json event = ordered_json::object();
                event["type"] = "ui-event";
                event["event"] = it->second.event_name;
                event["source"] = "native-win32";
                if (!it->second.node_id.empty()) event["id"] = it->second.node_id;
                if (!it->second.text.empty()) event["text"] = it->second.text;
                event["x"] = static_cast<int>(static_cast<short>(LOWORD(lparam)));
                event["y"] = static_cast<int>(static_cast<short>(HIWORD(lparam)));
                dispatchUiEventJson(event);
            }
        }
        return 0;
    case WM_NCDESTROY:
        RemoveWindowSubclass(hwnd, canvasSubclassProc, kCanvasSubclassId);
        break;
    }
    return DefSubclassProc(hwnd, message, wparam, lparam);
}

void attachCanvasSubclass(HWND hwnd) {
    if (!hwnd) return;
    SetWindowSubclass(hwnd, canvasSubclassProc, kCanvasSubclassId, 0);
}

std::wstring localImagePathFromSource(const std::string& src) {
    if (src.empty()) return {};
    std::string value = src;
    if (asciiStartsWithInsensitive(value, "file:///")) {
        value = value.substr(8);
    } else if (asciiStartsWithInsensitive(value, "file://")) {
        value = value.substr(7);
    } else if (value.find("://") != std::string::npos) {
        return {};
    }
    std::replace(value.begin(), value.end(), '/', '\\');
    return utf8ToWide(value);
}

void drawImagePlaceholder(HDC hdc, const RECT& rc, const std::wstring& message) {
    HBRUSH brush = CreateSolidBrush(RGB(250, 252, 255));
    FillRect(hdc, &rc, brush);
    DeleteObject(brush);
    HPEN border_pen = CreatePen(PS_SOLID, 1, RGB(199, 211, 227));
    HGDIOBJ previous_pen = SelectObject(hdc, border_pen);
    HGDIOBJ previous_brush = SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
    Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
    SelectObject(hdc, previous_brush);
    SelectObject(hdc, previous_pen);
    DeleteObject(border_pen);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(93, 104, 120));
    DrawTextW(hdc, message.c_str(), static_cast<int>(message.size()),
              const_cast<RECT*>(&rc), DT_CENTER | DT_VCENTER | DT_WORDBREAK);
}

bool loadImagePixelsFromFile(const std::wstring& path,
                             std::vector<BYTE>& pixels,
                             UINT& width,
                             UINT& height) {
    width = 0;
    height = 0;
    pixels.clear();
    if (!g_wic_factory) return false;

    IWICBitmapDecoder* decoder = nullptr;
    HRESULT hr = g_wic_factory->CreateDecoderFromFilename(
        path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder);
    if (FAILED(hr) || !decoder) return false;

    IWICBitmapFrameDecode* frame = nullptr;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr) || !frame) {
        decoder->Release();
        return false;
    }

    hr = frame->GetSize(&width, &height);
    if (FAILED(hr) || width == 0 || height == 0) {
        frame->Release();
        decoder->Release();
        return false;
    }

    IWICFormatConverter* converter = nullptr;
    hr = g_wic_factory->CreateFormatConverter(&converter);
    if (FAILED(hr) || !converter) {
        frame->Release();
        decoder->Release();
        return false;
    }

    hr = converter->Initialize(
        frame,
        GUID_WICPixelFormat32bppPBGRA,
        WICBitmapDitherTypeNone,
        nullptr,
        0.0,
        WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) {
        converter->Release();
        frame->Release();
        decoder->Release();
        return false;
    }

    const UINT stride = width * 4;
    const UINT buffer_size = stride * height;
    pixels.resize(buffer_size);
    hr = converter->CopyPixels(nullptr, stride, buffer_size, pixels.data());

    converter->Release();
    frame->Release();
    decoder->Release();

    return SUCCEEDED(hr);
}

void drawImageControl(HDC hdc, const RECT& rc, const ControlBinding& binding) {
    const std::wstring alt = utf8ToWide(jsonString(binding.data, "alt", "Image"));
    const std::wstring path = localImagePathFromSource(binding.value_text);
    if (path.empty()) {
        drawImagePlaceholder(hdc, rc, alt.empty() ? L"Image source must be a local file path" : alt);
        return;
    }

    std::vector<BYTE> pixels;
    UINT image_width = 0;
    UINT image_height = 0;
    if (!loadImagePixelsFromFile(path, pixels, image_width, image_height)) {
        drawImagePlaceholder(hdc, rc, alt.empty() ? L"Image file could not be loaded" : alt);
        return;
    }
    if (image_width == 0 || image_height == 0) {
        drawImagePlaceholder(hdc, rc, alt.empty() ? L"Image has no dimensions" : alt);
        return;
    }

    HBRUSH background = CreateSolidBrush(RGB(255, 255, 255));
    FillRect(hdc, &rc, background);
    DeleteObject(background);

    const int box_width = std::max(1, static_cast<int>(rc.right - rc.left));
    const int box_height = std::max(1, static_cast<int>(rc.bottom - rc.top));
    const std::string fit = jsonString(binding.data, "fit", "contain");

    double scale_x = static_cast<double>(box_width) / static_cast<double>(image_width);
    double scale_y = static_cast<double>(box_height) / static_cast<double>(image_height);
    double scale = (fit == "cover") ? std::max(scale_x, scale_y) : std::min(scale_x, scale_y);
    if (fit == "fill" || fit == "stretch") {
        scale = 1.0;
        scale_x = static_cast<double>(box_width) / static_cast<double>(image_width);
        scale_y = static_cast<double>(box_height) / static_cast<double>(image_height);
    } else {
        scale_x = scale;
        scale_y = scale;
    }

    const int draw_width = std::max(1, static_cast<int>(image_width * scale_x));
    const int draw_height = std::max(1, static_cast<int>(image_height * scale_y));
    const int draw_x = rc.left + ((box_width - draw_width) / 2);
    const int draw_y = rc.top + ((box_height - draw_height) / 2);

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = static_cast<LONG>(image_width);
    bmi.bmiHeader.biHeight = -static_cast<LONG>(image_height);
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* dib_bits = nullptr;
    HBITMAP dib = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &dib_bits, nullptr, 0);
    if (dib && dib_bits) {
        std::memcpy(dib_bits, pixels.data(), pixels.size());
        HDC memdc = CreateCompatibleDC(hdc);
        if (memdc) {
            HGDIOBJ old_bitmap = SelectObject(memdc, dib);
            BLENDFUNCTION blend{};
            blend.BlendOp = AC_SRC_OVER;
            blend.SourceConstantAlpha = 255;
            blend.AlphaFormat = AC_SRC_ALPHA;
            SetStretchBltMode(hdc, HALFTONE);
            AlphaBlend(hdc, draw_x, draw_y, draw_width, draw_height,
                       memdc, 0, 0, static_cast<int>(image_width), static_cast<int>(image_height), blend);
            SelectObject(memdc, old_bitmap);
            DeleteDC(memdc);
        }
        DeleteObject(dib);
    } else {
        drawImagePlaceholder(hdc, rc, alt.empty() ? L"Image draw failed" : alt);
        return;
    }

    HPEN border_pen = CreatePen(PS_SOLID, 1, RGB(199, 211, 227));
    HGDIOBJ previous_pen = SelectObject(hdc, border_pen);
    HGDIOBJ previous_brush = SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
    Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
    SelectObject(hdc, previous_brush);
    SelectObject(hdc, previous_pen);
    DeleteObject(border_pen);
}

LRESULT CALLBACK imageSubclassProc(HWND hwnd,
                                   UINT message,
                                   WPARAM wparam,
                                   LPARAM lparam,
                                   UINT_PTR subclass_id,
                                   DWORD_PTR ref_data) {
    (void)subclass_id;
    (void)ref_data;
    ScopedNativeSession scoped(lookupNativeSessionForWindow(hwnd));
    switch (message) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        {
            PAINTSTRUCT ps{};
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc{};
            GetClientRect(hwnd, &rc);
            auto it = g_native.bindings.find(hwnd);
            if (it != g_native.bindings.end()) {
                drawImageControl(hdc, rc, it->second);
            } else {
                drawImagePlaceholder(hdc, rc, L"Image");
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
    case WM_LBUTTONUP:
        if (g_native.suppress_events) return 0;
        {
            auto it = g_native.bindings.find(hwnd);
            if (it != g_native.bindings.end() && !it->second.event_name.empty()) {
                ordered_json event = ordered_json::object();
                event["type"] = "ui-event";
                event["event"] = it->second.event_name;
                event["source"] = "native-win32";
                if (!it->second.node_id.empty()) event["id"] = it->second.node_id;
                if (!it->second.text.empty()) event["text"] = it->second.text;
                if (!it->second.value_text.empty()) {
                    event["src"] = it->second.value_text;
                    event["value"] = it->second.value_text;
                }
                event["x"] = static_cast<int>(static_cast<short>(LOWORD(lparam)));
                event["y"] = static_cast<int>(static_cast<short>(HIWORD(lparam)));
                dispatchUiEventJson(event);
            }
        }
        return 0;
    case WM_NCDESTROY:
        RemoveWindowSubclass(hwnd, imageSubclassProc, kImageSubclassId);
        break;
    }
    return DefSubclassProc(hwnd, message, wparam, lparam);
}

void attachImageSubclass(HWND hwnd) {
    if (!hwnd) return;
    SetWindowSubclass(hwnd, imageSubclassProc, kImageSubclassId, 0);
}

void dispatchUiEvent(const ControlBinding& binding) {
    if (binding.event_name.empty()) return;
    ordered_json event = ordered_json::object();
    event["type"] = "ui-event";
    event["event"] = binding.event_name;
    event["source"] = "native-win32";
    if (!binding.node_id.empty()) event["id"] = binding.node_id;
    if (!binding.text.empty()) event["text"] = binding.text;
    dispatchUiEventJson(event);
}

void clearNativeControls(HWND hwnd) {
    snapshotScrollViewStateRecursive(hwnd);
    clearNativeChildrenOf(hwnd);
    g_native.bindings.clear();
    g_native.node_controls.clear();
    g_native.menu_command_bindings.clear();
    g_native.command_bar_bindings.clear();
    g_native.command_bindings.clear();
    g_native.command_bar_hwnd = nullptr;
    g_native.viewport_host = nullptr;
    g_native.content_host = nullptr;
    g_native.status_bar_hwnd = nullptr;
    g_native.raw_client_width = 0;
    g_native.raw_client_height = 0;
    g_native.client_width = 0;
    g_native.client_height = 0;
    g_native.command_bar_height = 0;
    g_native.status_bar_height = 0;
    g_native.content_width = 0;
    g_native.content_height = 0;
    g_native.focused_rich_text_hwnd = nullptr;
    g_native.next_control_id = 1000;
}

void clearNativeMenu(HWND hwnd) {
    HMENU old_menu = g_native.current_menu;
    g_native.current_menu = nullptr;
    g_native.menu_command_bindings.clear();
    g_native.command_bindings = g_native.command_bar_bindings;
    HWND menu_owner = (g_native.embedded_mode && g_native.parent_hwnd && IsWindow(g_native.parent_hwnd))
        ? g_native.parent_hwnd
        : hwnd;
    if (menu_owner && IsWindow(menu_owner)) {
        SetMenu(menu_owner, nullptr);
        DrawMenuBar(menu_owner);
    }
    if (old_menu) {
        DestroyMenu(old_menu);
    }
}

bool isTruthyJson(const ordered_json& value) {
    if (value.is_boolean()) return value.get<bool>();
    if (value.is_string()) return value.get<std::string>() == "true";
    if (value.is_number_integer()) return value.get<int>() != 0;
    return false;
}

bool windowTextEquals(HWND hwnd, const std::wstring& text) {
    const int len = GetWindowTextLengthW(hwnd);
    std::wstring current(static_cast<size_t>(std::max(len, 0)), L'\0');
    if (len > 0) {
        GetWindowTextW(hwnd, current.data(), len + 1);
    }
    return current == text;
}

bool selectComboByValue(HWND hwnd, ControlBinding& binding, const std::string& value) {
    auto it = std::find(binding.option_values.begin(), binding.option_values.end(), value);
    if (it == binding.option_values.end()) return false;
    const int index = static_cast<int>(std::distance(binding.option_values.begin(), it));
    SendMessageW(hwnd, CB_SETCURSEL, static_cast<WPARAM>(index), 0);
    binding.value_text = value;
    return true;
}

std::string choiceValue(const ordered_json& option, const std::string& fallback) {
    return jsonString(option, "value", jsonString(option, "text", jsonString(option, "label", fallback)));
}

std::wstring choiceText(const ordered_json& option, const std::string& fallback) {
    return utf8ToWide(jsonString(option, "text", jsonString(option, "label", fallback)));
}

bool copyPatchedNodeForMeasurement(const std::string& node_id,
                                   const ordered_json& props,
                                   ordered_json* out_old_node,
                                   ordered_json* out_new_node) {
    if (node_id.empty() || !out_old_node || !out_new_node) return false;

    std::lock_guard<std::mutex> lock(g_native.mutex);
    ordered_json* target = nullptr;
    if (!findUserAppNodeById(g_native.spec, node_id, &target) || !target || !target->is_object()) {
        return false;
    }

    *out_old_node = *target;
    *out_new_node = *target;
    for (auto it = props.begin(); it != props.end(); ++it) {
        (*out_new_node)[it.key()] = it.value();
    }
    return true;
}

bool patchWouldResizeNode(HWND control,
                          const std::string& node_id,
                          const ordered_json& props) {
    if (!control || !IsWindow(control) || node_id.empty()) return true;

    ordered_json old_node = nullptr;
    ordered_json new_node = nullptr;
    if (!copyPatchedNodeForMeasurement(node_id, props, &old_node, &new_node)) {
        return true;
    }

    HWND measure_host = GetParent(control);
    if (!measure_host || !IsWindow(measure_host)) {
        measure_host = control;
    }

    RECT rc{};
    GetClientRect(measure_host, &rc);
    const int max_width = std::max(24, static_cast<int>(rc.right - rc.left));

    HDC hdc = GetDC(measure_host);
    if (!hdc) return true;
    ensureFonts();
    SelectObject(hdc, g_native.ui_font);
    const MeasuredSize old_size = measureNode(hdc, old_node, max_width);
    const MeasuredSize new_size = measureNode(hdc, new_node, max_width);
    ReleaseDC(measure_host, hdc);
    return old_size.width != new_size.width || old_size.height != new_size.height;
}

bool syncComboOptions(HWND hwnd,
                      ControlBinding& binding,
                      const ordered_json& options,
                      const std::string& selected_value) {
    if (!options.is_array()) return false;

    SendMessageW(hwnd, CB_RESETCONTENT, 0, 0);
    binding.option_values.clear();

    int selected_index = -1;
    int option_index = 0;
    for (const auto& option : options) {
        if (!option.is_object()) continue;
        const std::string option_value = choiceValue(option, "option-" + std::to_string(option_index));
        const std::wstring option_text = choiceText(option, option_value);
        SendMessageW(hwnd, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(option_text.c_str()));
        binding.option_values.push_back(option_value);
        if (selected_index < 0 && option_value == selected_value) {
            selected_index = option_index;
        }
        ++option_index;
    }

    if (selected_index >= 0) {
        SendMessageW(hwnd, CB_SETCURSEL, static_cast<WPARAM>(selected_index), 0);
        binding.value_text = selected_value;
    } else if (!binding.option_values.empty()) {
        SendMessageW(hwnd, CB_SETCURSEL, 0, 0);
        binding.value_text = selected_value;
    } else {
        binding.value_text = selected_value;
    }
    return true;
}

bool selectListBoxByValue(HWND hwnd, ControlBinding& binding, const std::string& value) {
    auto it = std::find(binding.option_values.begin(), binding.option_values.end(), value);
    if (it == binding.option_values.end()) return false;
    const int index = static_cast<int>(std::distance(binding.option_values.begin(), it));
    SendMessageW(hwnd, LB_SETCURSEL, static_cast<WPARAM>(index), 0);
    binding.value_text = value;
    return true;
}

bool syncListBoxOptions(HWND hwnd,
                        ControlBinding& binding,
                        const ordered_json& options,
                        const std::string& selected_value) {
    if (!options.is_array()) return false;

    SendMessageW(hwnd, LB_RESETCONTENT, 0, 0);
    binding.option_values.clear();

    int selected_index = -1;
    int option_index = 0;
    for (const auto& option : options) {
        if (!option.is_object()) continue;
        const std::string option_value = choiceValue(option, "option-" + std::to_string(option_index));
        const std::wstring option_text = choiceText(option, option_value);
        SendMessageW(hwnd, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(option_text.c_str()));
        binding.option_values.push_back(option_value);
        if (selected_index < 0 && option_value == selected_value) {
            selected_index = option_index;
        }
        ++option_index;
    }

    if (selected_index >= 0) {
        SendMessageW(hwnd, LB_SETCURSEL, static_cast<WPARAM>(selected_index), 0);
        binding.value_text = selected_value;
    } else {
        binding.value_text = selected_value;
    }
    return true;
}

bool selectRadioByValue(HWND hwnd, ControlBinding& binding, const std::string& value) {
    HWND parent = GetParent(hwnd);
    if (!parent) return false;
    bool handled = false;
    for (auto& entry : g_native.bindings) {
        ControlBinding& candidate = entry.second;
        if (candidate.type != "radio-group" || candidate.node_id != binding.node_id) continue;
        if (GetParent(entry.first) != parent) continue;
        const bool checked = (candidate.value_text == value);
        SendMessageW(entry.first, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
        handled = handled || checked;
    }
    return handled;
}

std::string selectedRadioGroupValue(HWND hwnd, const ControlBinding& binding) {
    HWND parent = GetParent(hwnd);
    if (!parent) return "";
    for (HWND child = GetWindow(parent, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
        auto it = g_native.bindings.find(child);
        if (it == g_native.bindings.end()) continue;
        const ControlBinding& candidate = it->second;
        if (candidate.type != "radio-group" || candidate.node_id != binding.node_id) continue;
        if (SendMessageW(child, BM_GETCHECK, 0, 0) == BST_CHECKED) {
            return candidate.value_text;
        }
    }
    return "";
}

bool syncRadioGroupOptions(HWND hwnd,
                           ControlBinding& binding,
                           const ordered_json& options,
                           const std::string& selected_value) {
    if (!options.is_array()) return false;
    HWND parent = GetParent(hwnd);
    if (!parent) return false;

    std::vector<std::string> option_values;
    std::vector<std::wstring> option_texts;
    int option_index = 0;
    for (const auto& option : options) {
        if (!option.is_object()) continue;
        const std::string option_value = choiceValue(option, "option-" + std::to_string(option_index));
        option_values.push_back(option_value);
        option_texts.push_back(choiceText(option, option_value));
        ++option_index;
    }

    std::vector<HWND> radio_hwnds;
    for (HWND child = GetWindow(parent, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
        auto it = g_native.bindings.find(child);
        if (it == g_native.bindings.end()) continue;
        if (it->second.type == "radio-group" && it->second.node_id == binding.node_id) {
            radio_hwnds.push_back(child);
        }
    }

    if (radio_hwnds.size() != option_values.size()) return false;

    for (size_t index = 0; index < radio_hwnds.size(); ++index) {
        HWND radio = radio_hwnds[index];
        auto it = g_native.bindings.find(radio);
        if (it == g_native.bindings.end()) return false;
        SetWindowTextW(radio, option_texts[index].c_str());
        it->second.value_text = option_values[index];
    }

    if (!selected_value.empty()) {
        selectRadioByValue(hwnd, binding, selected_value);
    }
    return true;
}

bool selectTabByValue(HWND hwnd, ControlBinding& binding, const std::string& value) {
    auto it = std::find(binding.option_values.begin(), binding.option_values.end(), value);
    if (it == binding.option_values.end()) return false;
    const int index = static_cast<int>(std::distance(binding.option_values.begin(), it));
    TabCtrl_SetCurSel(hwnd, index);
    binding.value_text = value;
    return true;
}

RECT tabContentRect(HWND hwnd) {
    RECT rc{};
    if (!hwnd || !IsWindow(hwnd)) return rc;
    GetClientRect(hwnd, &rc);
    TabCtrl_AdjustRect(hwnd, FALSE, &rc);
    return rc;
}

RECT tabContentRectInParent(HWND hwnd) {
    RECT rc = tabContentRect(hwnd);
    HWND parent = hwnd ? GetParent(hwnd) : nullptr;
    if (!parent || !IsWindow(parent)) return rc;
    MapWindowPoints(hwnd, parent, reinterpret_cast<LPPOINT>(&rc), 2);
    return rc;
}

int tabHeaderHeightFromContentRect(const RECT& content_rect, int control_height) {
    const int bottom_frame = std::max(0, control_height - static_cast<int>(content_rect.bottom));
    return std::max(24, static_cast<int>(content_rect.top) + bottom_frame);
}

bool positionTabContentHost(HWND tab, ControlBinding& binding, int content_height) {
    HWND content_host = binding.companion_hwnd;
    if (!tab || !IsWindow(tab) || !content_host || !IsWindow(content_host)) return false;
    HWND parent = GetParent(tab);
    if (!parent || !IsWindow(parent)) return false;

    RECT tab_rect{};
    GetWindowRect(tab, &tab_rect);
    MapWindowPoints(nullptr, parent, reinterpret_cast<LPPOINT>(&tab_rect), 2);

    const int content_offset_x = jsonInt(binding.data, "contentOffsetX", 0);
    const int content_offset_y = jsonInt(binding.data, "contentOffsetY", 0);
    const int tab_width = std::max(24, static_cast<int>(tab_rect.right - tab_rect.left));
    const int content_width = std::max(24, jsonInt(binding.data, "contentWidth", tab_width));
    const int host_height = std::max(24, content_height);
    binding.data["contentHostHeight"] = host_height;

    return SetWindowPos(content_host,
                        tab,
                        tab_rect.left + content_offset_x,
                        tab_rect.top + content_offset_y,
                        content_width,
                        host_height,
                        SWP_NOACTIVATE) != FALSE;
}

bool syncTabsControl(HWND hwnd,
                     ControlBinding& binding,
                     const ordered_json& tabs,
                     const std::string& selected_value) {
    if (!tabs.is_array()) return false;
    HWND content_host = binding.companion_hwnd;
    if (!content_host || !IsWindow(content_host)) return false;

    TabCtrl_DeleteAllItems(hwnd);
    binding.option_values.clear();
    binding.option_nodes.clear();

    int selected_index = -1;
    int index = 0;
    for (const auto& tab_node : tabs) {
        if (!tab_node.is_object()) continue;
        const std::string value = jsonString(tab_node, "value", "tab-" + std::to_string(index));
        const std::wstring text = utf8ToWide(jsonString(tab_node, "text", jsonString(tab_node, "label", value)));
        TCITEMW item{};
        item.mask = TCIF_TEXT;
        item.pszText = const_cast<wchar_t*>(text.c_str());
        TabCtrl_InsertItem(hwnd, index, &item);
        binding.option_values.push_back(value);
        binding.option_nodes.push_back(tab_node);
        if (selected_index < 0 && value == selected_value) {
            selected_index = index;
        }
        ++index;
    }

    if (selected_index < 0 && !binding.option_values.empty()) {
        selected_index = 0;
    }
    if (selected_index >= 0) {
        TabCtrl_SetCurSel(hwnd, selected_index);
        binding.value_text = binding.option_values[static_cast<size_t>(selected_index)];
    } else {
        binding.value_text.clear();
    }

    g_native.suppress_events = true;
    snapshotScrollViewStateRecursive(content_host);
    clearNativeChildrenOf(content_host);
    g_native.suppress_events = false;

    int content_host_height = std::max(40, jsonInt(binding.data, "contentHostHeight", 40));

    if (selected_index >= 0 && static_cast<size_t>(selected_index) < binding.option_nodes.size()) {
        const ordered_json& active_tab = binding.option_nodes[static_cast<size_t>(selected_index)];
        auto content_it = active_tab.find("content");
        if (content_it != active_tab.end() && content_it->is_object()) {
            HDC hdc = GetDC(content_host);
            if (!hdc) return false;
            ensureFonts();
            SelectObject(hdc, g_native.ui_font);
            const int content_width = std::max(24, jsonInt(binding.data, "contentWidth", 24) - 16);
            const MeasuredSize measured = measureNode(hdc, *content_it, content_width);
            content_host_height = std::max(40, measured.height + 16);
            if (!positionTabContentHost(hwnd, binding, content_host_height)) {
                ReleaseDC(content_host, hdc);
                return false;
            }
            layoutNode(content_host, hdc, *content_it, 8, 8, content_width);
            ReleaseDC(content_host, hdc);
        }
    } else if (!positionTabContentHost(hwnd, binding, content_host_height)) {
        return false;
    }

    InvalidateRect(content_host, nullptr, TRUE);
    UpdateWindow(content_host);
    return true;
}

bool selectTableById(HWND hwnd, ControlBinding& binding, const std::string& row_id) {
    auto it = std::find(binding.option_values.begin(), binding.option_values.end(), row_id);
    if (it == binding.option_values.end()) return false;
    const int index = static_cast<int>(std::distance(binding.option_values.begin(), it));
    ListView_SetItemState(hwnd, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_SetItemState(hwnd, index, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_EnsureVisible(hwnd, index, FALSE);
    binding.value_text = row_id;
    return true;
}

bool syncTableColumns(HWND hwnd,
                      ControlBinding& binding,
                      const ordered_json& columns,
                      const ordered_json& rows,
                      const std::string& selected_id) {
    if (!columns.is_array()) return false;

    while (ListView_DeleteColumn(hwnd, 0)) {
    }

    RECT rc{};
    GetClientRect(hwnd, &rc);
    const int control_width = std::max(110, static_cast<int>(rc.right - rc.left));

    std::vector<std::string> column_keys;
    int column_index = 0;
    for (const auto& column : columns) {
        if (!column.is_object()) continue;
        const std::string key = jsonString(column, "key", "col-" + std::to_string(column_index));
        const std::wstring title = utf8ToWide(jsonString(column, "title", key));
        LVCOLUMNW list_column{};
        list_column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        list_column.pszText = const_cast<wchar_t*>(title.c_str());
        list_column.cx = std::max(110, control_width / std::max(1, static_cast<int>(columns.size())));
        list_column.iSubItem = column_index;
        ListView_InsertColumn(hwnd, column_index, &list_column);
        column_keys.push_back(key);
        ++column_index;
    }

    binding.column_keys = std::move(column_keys);
    repopulateTableRows(hwnd, binding, rows, selected_id);
    return true;
}

std::string treeItemText(const ordered_json& item) {
    std::string text = jsonString(item, "text");
    if (text.empty()) text = jsonString(item, "label");
    if (text.empty()) text = jsonString(item, "id", "Item");
    return text;
}

ordered_json normalizeExpandedIds(const ordered_json* expanded_ids) {
    ordered_json out = ordered_json::array();
    if (!expanded_ids || !expanded_ids->is_array()) return out;
    for (const auto& value : *expanded_ids) {
        if (value.is_string()) {
            out.push_back(value.get<std::string>());
        }
    }
    return out;
}

std::unordered_set<std::string> expandedIdSet(const ordered_json& expanded_ids) {
    std::unordered_set<std::string> out;
    if (!expanded_ids.is_array()) return out;
    for (const auto& value : expanded_ids) {
        if (value.is_string()) out.insert(value.get<std::string>());
    }
    return out;
}

void clearTreeBinding(ControlBinding& binding) {
    binding.option_values.clear();
    binding.option_nodes.clear();
    binding.item_handles.clear();
}

void insertTreeItems(HWND hwnd,
                     HTREEITEM parent,
                     const ordered_json& items,
                     ControlBinding& binding,
                     const std::unordered_set<std::string>& expanded_ids) {
    if (!items.is_array()) return;
    for (const auto& item : items) {
        if (!item.is_object()) continue;
        const std::string item_id = jsonString(item, "id");
        const std::wstring item_text = utf8ToWide(treeItemText(item));
        const size_t flat_index = binding.option_values.size();

        TVINSERTSTRUCTW insert{};
        insert.hParent = parent;
        insert.hInsertAfter = TVI_LAST;
        insert.item.mask = TVIF_TEXT | TVIF_PARAM;
        insert.item.pszText = const_cast<wchar_t*>(item_text.c_str());
        insert.item.lParam = static_cast<LPARAM>(flat_index);

        HTREEITEM handle = TreeView_InsertItem(hwnd, &insert);
        binding.option_values.push_back(item_id);
        binding.option_nodes.push_back(item);
        binding.item_handles.push_back(handle);

        auto children_it = item.find("children");
        if (children_it != item.end() && children_it->is_array()) {
            insertTreeItems(hwnd, handle, *children_it, binding, expanded_ids);
        }

        if (handle && expanded_ids.find(item_id) != expanded_ids.end()) {
            TreeView_Expand(hwnd, handle, TVE_EXPAND);
        }
    }
}

bool selectTreeById(HWND hwnd, ControlBinding& binding, const std::string& item_id) {
    auto it = std::find(binding.option_values.begin(), binding.option_values.end(), item_id);
    if (it == binding.option_values.end()) return false;
    const size_t index = static_cast<size_t>(std::distance(binding.option_values.begin(), it));
    if (index >= binding.item_handles.size()) return false;
    HTREEITEM handle = binding.item_handles[index];
    if (!handle) return false;
    TreeView_SelectItem(hwnd, handle);
    TreeView_EnsureVisible(hwnd, handle);
    binding.value_text = item_id;
    return true;
}

ordered_json collectExpandedTreeIds(HWND hwnd, const ControlBinding& binding) {
    ordered_json out = ordered_json::array();
    for (size_t i = 0; i < binding.item_handles.size() && i < binding.option_values.size(); ++i) {
        HTREEITEM handle = binding.item_handles[i];
        if (!handle) continue;
        const UINT state = TreeView_GetItemState(hwnd, handle, TVIS_EXPANDED);
        if ((state & TVIS_EXPANDED) != 0) {
            out.push_back(binding.option_values[i]);
        }
    }
    return out;
}

void applyTreeExpandedIds(HWND hwnd, ControlBinding& binding, const ordered_json& expanded_ids) {
    const auto wanted = expandedIdSet(expanded_ids);
    for (size_t i = 0; i < binding.item_handles.size() && i < binding.option_values.size(); ++i) {
        HTREEITEM handle = binding.item_handles[i];
        if (!handle) continue;
        const bool should_expand = wanted.find(binding.option_values[i]) != wanted.end();
        TreeView_Expand(hwnd, handle, should_expand ? TVE_EXPAND : TVE_COLLAPSE);
    }
    binding.data["expandedIds"] = normalizeExpandedIds(&expanded_ids);
}

void repopulateTreeView(HWND hwnd,
                        ControlBinding& binding,
                        const ordered_json& items,
                        const std::string& selected_id,
                        const ordered_json& expanded_ids) {
    TreeView_DeleteAllItems(hwnd);
    clearTreeBinding(binding);
    binding.data["items"] = items.is_array() ? items : ordered_json::array();
    binding.data["expandedIds"] = normalizeExpandedIds(&expanded_ids);
    const auto wanted = expandedIdSet(binding.data["expandedIds"]);
    insertTreeItems(hwnd, TVI_ROOT, binding.data["items"], binding, wanted);
    if (!selected_id.empty()) {
        selectTreeById(hwnd, binding, selected_id);
    } else {
        TreeView_SelectItem(hwnd, nullptr);
        binding.value_text.clear();
    }
}

bool syncTreeView(HWND hwnd,
                  ControlBinding& binding,
                  const ordered_json& items,
                  const std::string& selected_id,
                  const ordered_json& expanded_ids) {
    const ordered_json normalized_items = items.is_array() ? items : ordered_json::array();
    const ordered_json normalized_expanded = normalizeExpandedIds(&expanded_ids);
    if (!binding.data.is_object() ||
        !binding.data.contains("items") ||
        binding.data["items"] != normalized_items) {
        repopulateTreeView(hwnd, binding, normalized_items, selected_id, normalized_expanded);
        return true;
    }

    bool handled = false;
    if (!binding.data.contains("expandedIds") || binding.data["expandedIds"] != normalized_expanded) {
        applyTreeExpandedIds(hwnd, binding, normalized_expanded);
        handled = true;
    }
    if (selected_id != binding.value_text) {
        handled = selectTreeById(hwnd, binding, selected_id) || handled;
    }
    return handled;
}

MeasuredSize layoutNode(HWND parent, HDC hdc, const ordered_json& node, int x, int y, int max_width);
MeasuredSize layoutChildrenStack(HWND parent,
                                 HDC hdc,
                                 const std::vector<const ordered_json*>& children,
                                 int x,
                                 int y,
                                 int max_width,
                                 int gap);
bool findUserAppNodeById(ordered_json& node, const std::string& id, ordered_json** found);
void rebuildNativeContainerContents(HWND container, const ordered_json& node);

bool isPatchableContainerType(const std::string& type) {
    return type == "stack" || type == "row" || type == "toolbar" || type == "card" ||
        type == "form" || type == "grid" || type == "scroll-view" ||
        type == "context-menu" || type == "split-view";
}

bool findNearestPatchableAncestorNode(ordered_json& node,
                                      const std::string& target_id,
                                      ordered_json* current_patchable,
                                      ordered_json** found) {
    if (!node.is_object()) return false;

    ordered_json* next_patchable = current_patchable;
    const std::string node_type = jsonString(node, "type");
    if (isPatchableContainerType(node_type)) {
        next_patchable = &node;
    }

    auto id_it = node.find("id");
    if (id_it != node.end() && id_it->is_string() && id_it->get<std::string>() == target_id) {
        *found = next_patchable;
        return true;
    }

    auto children_it = node.find("children");
    if (children_it != node.end() && children_it->is_array()) {
        for (auto& child : *children_it) {
            if (findNearestPatchableAncestorNode(child, target_id, next_patchable, found)) return true;
        }
    }

    auto body_it = node.find("body");
    if (body_it != node.end() && body_it->is_object()) {
        if (findNearestPatchableAncestorNode(*body_it, target_id, next_patchable, found)) return true;
    }

    return false;
}

bool tryRebuildAncestorContainerForNode(const std::string& node_id, std::string* rebuilt_ancestor_id = nullptr) {
    if (node_id.empty()) return false;

    std::string ancestor_id;
    ordered_json ancestor_spec = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_native.mutex);
        ordered_json* ancestor = nullptr;
        if (!findNearestPatchableAncestorNode(g_native.spec, node_id, nullptr, &ancestor) ||
            !ancestor ||
            !ancestor->is_object()) {
            return false;
        }
        ancestor_id = jsonString(*ancestor, "id");
        if (ancestor_id.empty()) return false;
        ancestor_spec = *ancestor;
    }

    auto control_it = g_native.node_controls.find(ancestor_id);
    if (control_it == g_native.node_controls.end()) return false;
    if (rebuilt_ancestor_id) *rebuilt_ancestor_id = ancestor_id;
    rebuildNativeContainerContents(control_it->second, ancestor_spec);
    return true;
}

bool refreshTreeViewControl(const std::string& node_id) {
    if (node_id.empty()) return false;

    ordered_json node_spec = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_native.mutex);
        ordered_json* found = nullptr;
        if (!findUserAppNodeById(g_native.spec, node_id, &found) || !found || !found->is_object()) {
            return false;
        }
        node_spec = *found;
    }

    auto control_it = g_native.node_controls.find(node_id);
    if (control_it == g_native.node_controls.end()) return false;
    auto binding_it = g_native.bindings.find(control_it->second);
    if (binding_it == g_native.bindings.end()) return false;
    if (binding_it->second.type != "tree-view") return false;

    ControlBinding& binding = binding_it->second;
    binding.text = jsonString(node_spec, "label");
    if (binding.companion_hwnd && IsWindow(binding.companion_hwnd)) {
        const std::wstring label = utf8ToWide(binding.text);
        if (!windowTextEquals(binding.companion_hwnd, label)) {
            SetWindowTextW(binding.companion_hwnd, label.c_str());
        }
    }

    const ordered_json* items = jsonArrayChild(node_spec, "items");
    const ordered_json* expanded_ids = jsonArrayChild(node_spec, "expandedIds");
    const std::string selected_id = jsonString(node_spec, "selectedId");
    return syncTreeView(control_it->second,
                        binding,
                        items ? *items : ordered_json::array(),
                        selected_id,
                        expanded_ids ? *expanded_ids : ordered_json::array());
}

// Full rebuild — wipes and reinserts every row.
// Used by syncTableRows when a reorder is detected, and for the initial
// population in the layout pass.
void repopulateTableRows(HWND hwnd, ControlBinding& binding, const ordered_json& rows, const std::string& selected_id) {
    ListView_DeleteAllItems(hwnd);
    binding.option_values.clear();
    binding.row_objects.clear();
    if (!rows.is_array()) return;

    int row_index = 0;
    for (const auto& row : rows) {
        if (!row.is_object()) continue;
        const std::string row_id = jsonString(row, "id", "row-" + std::to_string(row_index));
        binding.option_values.push_back(row_id);
        binding.row_objects.push_back(row);

        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = row_index;
        const std::wstring first_text = utf8ToWide(binding.column_keys.empty() ? row_id : jsonString(row, binding.column_keys[0].c_str()));
        item.pszText = const_cast<wchar_t*>(first_text.c_str());
        const int inserted = ListView_InsertItem(hwnd, &item);

        for (size_t col = 1; col < binding.column_keys.size(); ++col) {
            const std::wstring text = utf8ToWide(jsonString(row, binding.column_keys[col].c_str()));
            ListView_SetItemText(hwnd, inserted, static_cast<int>(col), const_cast<wchar_t*>(text.c_str()));
        }

        if (!selected_id.empty() && row_id == selected_id) {
            ListView_SetItemState(hwnd, inserted, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        }
        ++row_index;
    }
    binding.value_text = selected_id;
}

// Apply a single cell's text to the ListView, skipping the SetItemText call
// when the text hasn't changed (avoids unnecessary redraws).
static void updateCellIfChanged(HWND hwnd, int row, int col, const std::wstring& new_text) {
    wchar_t buf[512]{};
    LVITEMW query{};
    query.iSubItem = col;
    query.pszText = buf;
    query.cchTextMax = static_cast<int>(std::size(buf));
    SendMessageW(hwnd, LVM_GETITEMTEXTW, static_cast<WPARAM>(row),
                 reinterpret_cast<LPARAM>(&query));
    if (new_text != buf) {
        ListView_SetItemText(hwnd, row, col, const_cast<wchar_t*>(new_text.c_str()));
    }
}

// Incremental table sync.  Diffs the current ListView rows against the new
// row list and applies the minimum set of operations:
//
//  - identical rows (same id, same data)  → skipped entirely
//  - changed rows (same id, different data) → cells updated in-place
//  - deleted rows (in old, not in new)   → removed back-to-front
//  - inserted rows (in new, not in old)  → inserted at the correct position
//  - reordered rows                       → full repopulate fallback
//
// The algorithm is O(n) in the common cases (append, delete-from-end,
// cell-update) and O(n²) worst-case only for the reorder detection step,
// which exits early via fallback before doing any ListView work.
void syncTableRows(HWND hwnd, ControlBinding& binding, const ordered_json& rows, const std::string& selected_id) {
    // Build new row list.
    std::vector<std::string> new_ids;
    std::vector<ordered_json> new_objects;
    if (rows.is_array()) {
        int fallback = 0;
        for (const auto& row : rows) {
            if (!row.is_object()) { ++fallback; continue; }
            new_ids.push_back(jsonString(row, "id", "row-" + std::to_string(fallback)));
            new_objects.push_back(row);
            ++fallback;
        }
    }

    // Fast path: nothing changed at all.
    if (new_ids == binding.option_values && new_objects == binding.row_objects) {
        if (selected_id != binding.value_text) {
            g_native.suppress_events = true;
            ListView_SetItemState(hwnd, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
            for (size_t i = 0; i < new_ids.size(); ++i) {
                if (new_ids[i] == selected_id) {
                    ListView_SetItemState(hwnd, static_cast<int>(i),
                        LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                    break;
                }
            }
            g_native.suppress_events = false;
            binding.value_text = selected_id;
        }
        return;
    }

    // Build new-ID lookup map (id → new position).
    std::unordered_map<std::string, size_t> new_id_pos;
    new_id_pos.reserve(new_ids.size());
    for (size_t i = 0; i < new_ids.size(); ++i) new_id_pos[new_ids[i]] = i;

    // Detect reordering: the surviving old rows must appear in the same
    // relative order in new_ids as in the old list.  If they don't, fall
    // back to full repopulate — implementing ListView moves is complex and
    // this case is rare in practice.
    {
        size_t last_new_pos = 0;
        bool first = true;
        for (const auto& old_id : binding.option_values) {
            auto it = new_id_pos.find(old_id);
            if (it == new_id_pos.end()) continue; // deleted row — skip
            if (!first && it->second < last_new_pos) {
                // Reorder detected.
                repopulateTableRows(hwnd, binding, rows, selected_id);
                return;
            }
            last_new_pos = it->second;
            first = false;
        }
    }

    // Step 1: delete rows not present in the new list, back-to-front so
    // that earlier indices remain valid as we remove later ones.
    {
        std::vector<int> del_indices;
        for (size_t i = 0; i < binding.option_values.size(); ++i) {
            if (new_id_pos.find(binding.option_values[i]) == new_id_pos.end()) {
                del_indices.push_back(static_cast<int>(i));
            }
        }
        for (int i = static_cast<int>(del_indices.size()) - 1; i >= 0; --i) {
            ListView_DeleteItem(hwnd, del_indices[i]);
        }
    }

    // Rebuild binding vectors to only surviving rows in their original order.
    {
        std::vector<std::string> surv_ids;
        std::vector<ordered_json> surv_objs;
        for (size_t i = 0; i < binding.option_values.size(); ++i) {
            if (new_id_pos.count(binding.option_values[i])) {
                surv_ids.push_back(std::move(binding.option_values[i]));
                surv_objs.push_back(std::move(binding.row_objects[i]));
            }
        }
        binding.option_values = std::move(surv_ids);
        binding.row_objects   = std::move(surv_objs);
    }

    // Step 2: walk new_ids in order; for each position either update an
    // existing row in-place or insert a new one.
    //
    // Invariant: at new_ids[ni], the ListView already contains exactly
    // (survivor_cursor) surviving rows followed by whatever remains.
    // The surviving row at survivor_cursor is at ListView index ni
    // (because we have inserted (ni - survivor_cursor) new rows ahead of it).
    size_t survivor_cursor = 0;
    std::vector<std::string> final_ids;
    std::vector<ordered_json> final_objs;
    final_ids.reserve(new_ids.size());
    final_objs.reserve(new_ids.size());

    for (size_t ni = 0; ni < new_ids.size(); ++ni) {
        const std::string&    new_id  = new_ids[ni];
        const ordered_json&   new_obj = new_objects[ni];
        const int             lv_pos  = static_cast<int>(ni);

        if (survivor_cursor < binding.option_values.size() &&
            binding.option_values[survivor_cursor] == new_id) {
            // Existing row — update changed cells in-place.
            if (new_obj != binding.row_objects[survivor_cursor]) {
                for (size_t col = 0; col < binding.column_keys.size(); ++col) {
                    updateCellIfChanged(hwnd, lv_pos, static_cast<int>(col),
                        utf8ToWide(jsonString(new_obj, binding.column_keys[col].c_str())));
                }
            }
            ++survivor_cursor;
        } else {
            // New row — insert at lv_pos.
            LVITEMW item{};
            item.mask  = LVIF_TEXT;
            item.iItem = lv_pos;
            const std::wstring first_text = utf8ToWide(
                binding.column_keys.empty() ? new_id :
                jsonString(new_obj, binding.column_keys[0].c_str()));
            item.pszText = const_cast<wchar_t*>(first_text.c_str());
            const int inserted = ListView_InsertItem(hwnd, &item);
            for (size_t col = 1; col < binding.column_keys.size(); ++col) {
                const std::wstring text = utf8ToWide(
                    jsonString(new_obj, binding.column_keys[col].c_str()));
                ListView_SetItemText(hwnd, inserted, static_cast<int>(col),
                                     const_cast<wchar_t*>(text.c_str()));
            }
        }

        final_ids.push_back(new_id);
        final_objs.push_back(new_obj);
    }

    binding.option_values = std::move(final_ids);
    binding.row_objects   = std::move(final_objs);

    // Update selection.
    g_native.suppress_events = true;
    ListView_SetItemState(hwnd, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    for (size_t i = 0; i < new_ids.size(); ++i) {
        if (new_ids[i] == selected_id) {
            ListView_SetItemState(hwnd, static_cast<int>(i),
                LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            ListView_EnsureVisible(hwnd, static_cast<int>(i), FALSE);
            break;
        }
    }
    g_native.suppress_events = false;
    binding.value_text = selected_id;
}

bool applyNativeNodePropsPatch(HWND hwnd, const std::string& node_id, const ordered_json& props) {
    auto control_it = g_native.node_controls.find(node_id);
    if (control_it == g_native.node_controls.end()) return false;
    auto binding_it = g_native.bindings.find(control_it->second);
    if (binding_it == g_native.bindings.end()) return false;

    HWND control = control_it->second;
    ControlBinding& binding = binding_it->second;
    const std::string type = binding.type;

    if (type == "text" || type == "heading" || type == "badge" || type == "button") {
        auto text_it = props.find("text");
        if (text_it == props.end() || !text_it->is_string()) return false;
        const std::wstring text = utf8ToWide(text_it->get<std::string>());
        if (!windowTextEquals(control, text)) {
            SetWindowTextW(control, text.c_str());
        }
        binding.text = text_it->get<std::string>();
        return true;
    }

    if (type == "link") {
        bool handled = false;
        auto text_it = props.find("text");
        if (text_it != props.end() && text_it->is_string()) {
            const std::wstring link_text = linkControlText(text_it->get<std::string>());
            if (!windowTextEquals(control, link_text)) {
                SetWindowTextW(control, link_text.c_str());
            }
            binding.text = text_it->get<std::string>();
            handled = true;
        }
        auto href_it = props.find("href");
        if (href_it != props.end() && href_it->is_string()) {
            binding.value_text = href_it->get<std::string>();
            handled = true;
        }
        return handled;
    }

    if (type == "image") {
        if (props.contains("width") || props.contains("height")) return false;
        bool handled = false;
        auto label_it = props.find("label");
        if (label_it != props.end() && label_it->is_string()) {
            binding.text = label_it->get<std::string>();
            if (binding.companion_hwnd && IsWindow(binding.companion_hwnd)) {
                const std::wstring label = utf8ToWide(binding.text);
                if (!windowTextEquals(binding.companion_hwnd, label)) {
                    SetWindowTextW(binding.companion_hwnd, label.c_str());
                }
            }
            handled = true;
        }
        auto src_it = props.find("src");
        if (src_it != props.end() && src_it->is_string()) {
            binding.value_text = src_it->get<std::string>();
            handled = true;
        }
        auto alt_it = props.find("alt");
        if (alt_it != props.end() && alt_it->is_string()) {
            binding.data["alt"] = alt_it->get<std::string>();
            handled = true;
        }
        auto fit_it = props.find("fit");
        if (fit_it != props.end() && fit_it->is_string()) {
            binding.data["fit"] = fit_it->get<std::string>();
            handled = true;
        }
        if (handled) {
            InvalidateRect(control, nullptr, TRUE);
            UpdateWindow(control);
        }
        return handled;
    }

    if (type == "number-input") {
        bool handled = false;
        auto label_it = props.find("label");
        if (label_it != props.end() && label_it->is_string()) {
            binding.text = label_it->get<std::string>();
            if (binding.companion_hwnd && IsWindow(binding.companion_hwnd)) {
                const std::wstring label = utf8ToWide(binding.text);
                if (!windowTextEquals(binding.companion_hwnd, label)) {
                    SetWindowTextW(binding.companion_hwnd, label.c_str());
                }
            }
            handled = true;
        }
        auto value_it = props.find("value");
        if (value_it != props.end() && (value_it->is_number() || value_it->is_string())) {
            const std::wstring value = utf8ToWide(jsonString(props, "value"));
            if (!windowTextEquals(control, value)) {
                g_native.suppress_events = true;
                SetWindowTextW(control, value.c_str());
                g_native.suppress_events = false;
            }
            binding.value_text = jsonString(props, "value");
            handled = true;
        }
        auto min_it = props.find("min");
        if (min_it != props.end() && (min_it->is_number() || min_it->is_string())) {
            binding.data["min"] = *min_it;
            handled = true;
        }
        auto max_it = props.find("max");
        if (max_it != props.end() && (max_it->is_number() || max_it->is_string())) {
            binding.data["max"] = *max_it;
            handled = true;
        }
        auto step_it = props.find("step");
        if (step_it != props.end() && (step_it->is_number() || step_it->is_string())) {
            binding.data["step"] = *step_it;
            handled = true;
        }
        return handled;
    }

    if (type == "date-picker") {
        bool handled = false;
        auto label_it = props.find("label");
        if (label_it != props.end() && label_it->is_string()) {
            binding.text = label_it->get<std::string>();
            if (binding.companion_hwnd && IsWindow(binding.companion_hwnd)) {
                const std::wstring label = utf8ToWide(binding.text);
                if (!windowTextEquals(binding.companion_hwnd, label)) {
                    SetWindowTextW(binding.companion_hwnd, label.c_str());
                }
            }
            handled = true;
        }
        auto value_it = props.find("value");
        if (value_it != props.end() && value_it->is_string()) {
            const std::string value = value_it->get<std::string>();
            SYSTEMTIME st{};
            g_native.suppress_events = true;
            if (value.empty()) {
                DateTime_SetSystemtime(control, GDT_NONE, nullptr);
            } else if (parseIsoDateString(value, st)) {
                DateTime_SetSystemtime(control, GDT_VALID, &st);
            }
            g_native.suppress_events = false;
            binding.value_text = value;
            handled = true;
        }
        auto min_it = props.find("min");
        if (min_it != props.end() && min_it->is_string()) {
            binding.data["min"] = min_it->get<std::string>();
            handled = true;
        }
        auto max_it = props.find("max");
        if (max_it != props.end() && max_it->is_string()) {
            binding.data["max"] = max_it->get<std::string>();
            handled = true;
        }
        if (handled) {
            applyDatePickerRange(control, binding.data);
        }
        return handled;
    }

    if (type == "time-picker") {
        bool handled = false;
        auto label_it = props.find("label");
        if (label_it != props.end() && label_it->is_string()) {
            binding.text = label_it->get<std::string>();
            if (binding.companion_hwnd && IsWindow(binding.companion_hwnd)) {
                const std::wstring label = utf8ToWide(binding.text);
                if (!windowTextEquals(binding.companion_hwnd, label)) {
                    SetWindowTextW(binding.companion_hwnd, label.c_str());
                }
            }
            handled = true;
        }
        auto value_it = props.find("value");
        if (value_it != props.end() && value_it->is_string()) {
            const std::string value = value_it->get<std::string>();
            SYSTEMTIME st{};
            g_native.suppress_events = true;
            if (value.empty()) {
                DateTime_SetSystemtime(control, GDT_NONE, nullptr);
            } else if (parseIsoTimeString(value, st)) {
                DateTime_SetSystemtime(control, GDT_VALID, &st);
            }
            g_native.suppress_events = false;
            binding.value_text = value;
            handled = true;
        }
        auto min_it = props.find("min");
        if (min_it != props.end() && min_it->is_string()) {
            binding.data["min"] = min_it->get<std::string>();
            handled = true;
        }
        auto max_it = props.find("max");
        if (max_it != props.end() && max_it->is_string()) {
            binding.data["max"] = max_it->get<std::string>();
            handled = true;
        }
        if (handled) {
            applyTimePickerRange(control, binding.data);
        }
        return handled;
    }

    if (type == "input" || type == "textarea") {
        auto value_it = props.find("value");
        if (value_it == props.end() || !value_it->is_string()) return false;
        const std::wstring value = utf8ToWide(value_it->get<std::string>());
        const std::wstring current = binding.multiline ? normalizeLineEndings(controlWindowText(control)) : controlWindowText(control);
        const std::wstring desired = binding.multiline ? normalizeLineEndings(value) : value;
        if (current != desired) {
            EditViewState view_state{};
            if (binding.multiline) {
                view_state = captureEditViewState(control);
            }
            g_native.suppress_events = true;
            SetWindowTextW(control, value.c_str());
            if (binding.multiline) {
                restoreEditViewState(control, view_state);
            }
            g_native.suppress_events = false;
        }
        binding.value_text = value_it->get<std::string>();
        return true;
    }

    if (type == "rich-text") {
        bool handled = false;
        auto label_it = props.find("label");
        if (label_it != props.end() && label_it->is_string()) {
            binding.text = label_it->get<std::string>();
            if (binding.companion_hwnd && IsWindow(binding.companion_hwnd)) {
                const std::wstring label = utf8ToWide(binding.text);
                if (!windowTextEquals(binding.companion_hwnd, label)) {
                    SetWindowTextW(binding.companion_hwnd, label.c_str());
                }
            }
            handled = true;
        }
        auto value_it = props.find("value");
        if (value_it != props.end() && value_it->is_string()) {
            const std::string value = value_it->get<std::string>();
            if (binding.value_text != value) {
                const EditViewState view_state = captureEditViewState(control);
                g_native.suppress_events = true;
                richEditSetRtf(control, value);
                restoreEditViewState(control, view_state);
                g_native.suppress_events = false;
            }
            binding.value_text = value;
            handled = true;
        }
        return handled;
    }

    if (type == "checkbox" || type == "switch") {
        bool handled = false;
        auto checked_it = props.find("checked");
        if (checked_it != props.end()) {
            SendMessageW(control, BM_SETCHECK, isTruthyJson(*checked_it) ? BST_CHECKED : BST_UNCHECKED, 0);
            handled = true;
        }
        auto label_it = props.find("label");
        if (label_it != props.end() && label_it->is_string()) {
            const std::wstring label = utf8ToWide(label_it->get<std::string>());
            if (!windowTextEquals(control, label)) {
                SetWindowTextW(control, label.c_str());
            }
            binding.text = label_it->get<std::string>();
            handled = true;
        }
        return handled;
    }

    if (type == "select") {
        bool handled = false;
        std::string selected_value = binding.value_text;
        auto value_it = props.find("value");
        if (value_it != props.end() && value_it->is_string()) {
            selected_value = value_it->get<std::string>();
        }
        auto options_it = props.find("options");
        if (options_it != props.end() && options_it->is_array()) {
            handled = syncComboOptions(control, binding, *options_it, selected_value);
        }
        if (value_it != props.end() && value_it->is_string()) {
            handled = selectComboByValue(control, binding, value_it->get<std::string>()) || handled;
        }
        return handled;
    }

    if (type == "canvas") {
        if (props.contains("width") || props.contains("height")) return false;
        bool handled = false;
        auto label_it = props.find("label");
        if (label_it != props.end() && label_it->is_string()) {
            binding.text = label_it->get<std::string>();
            if (binding.companion_hwnd && IsWindow(binding.companion_hwnd)) {
                const std::wstring label = utf8ToWide(binding.text);
                if (!windowTextEquals(binding.companion_hwnd, label)) {
                    SetWindowTextW(binding.companion_hwnd, label.c_str());
                }
            }
            handled = true;
        }
        auto commands_it = props.find("commands");
        if (commands_it != props.end() && commands_it->is_array()) {
            binding.data["commands"] = *commands_it;
            InvalidateRect(control, nullptr, TRUE);
            UpdateWindow(control);
            handled = true;
        }
        return handled;
    }

    if (type == "list-box") {
        bool handled = false;
        std::string selected_value = binding.value_text;
        auto value_it = props.find("value");
        if (value_it != props.end() && value_it->is_string()) {
            selected_value = value_it->get<std::string>();
        }
        auto options_it = props.find("options");
        if (options_it != props.end() && options_it->is_array()) {
            handled = syncListBoxOptions(control, binding, *options_it, selected_value);
        }
        if (value_it != props.end() && value_it->is_string()) {
            handled = selectListBoxByValue(control, binding, value_it->get<std::string>()) || handled;
        }
        return handled;
    }

    if (type == "radio-group") {
        bool handled = false;
        std::string selected_value = selectedRadioGroupValue(control, binding);
        auto value_it = props.find("value");
        if (value_it != props.end() && value_it->is_string()) {
            selected_value = value_it->get<std::string>();
        }
        auto options_it = props.find("options");
        if (options_it != props.end() && options_it->is_array()) {
            if (patchWouldResizeNode(control, node_id, props)) {
                g_native.patch_resize_reject_count.fetch_add(1, std::memory_order_relaxed);
                return false;
            }
            handled = syncRadioGroupOptions(control, binding, *options_it, selected_value);
        }
        if (value_it != props.end() && value_it->is_string()) {
            handled = selectRadioByValue(control, binding, value_it->get<std::string>()) || handled;
        }
        return handled;
    }

    if (type == "tabs") {
        const bool affects_layout = props.contains("tabs") || props.contains("value");
        if (affects_layout && patchWouldResizeNode(control, node_id, props)) {
            g_native.patch_resize_reject_count.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        bool handled = false;
        std::string new_value = binding.value_text;
        auto value_it = props.find("value");
        if (value_it != props.end() && value_it->is_string()) {
            new_value = value_it->get<std::string>();
        }
        auto tabs_it = props.find("tabs");
        if (tabs_it != props.end() && tabs_it->is_array()) {
            handled = syncTabsControl(control, binding, *tabs_it, new_value);
            if (!handled) return false;
        }
        if (value_it == props.end() || !value_it->is_string()) {
            return handled;
        }

        // Update the tab header selection.
        if (!selectTabByValue(control, binding, new_value)) return false;

        // Re-layout the content area for the newly selected tab.
        // companion_hwnd is the dedicated content container created at layout time.
        HWND content_host = binding.companion_hwnd;
        if (!content_host || !IsWindow(content_host)) return false;

        auto it = std::find(binding.option_values.begin(), binding.option_values.end(), new_value);
        if (it == binding.option_values.end()) return false;
        const int index = static_cast<int>(std::distance(binding.option_values.begin(), it));
        if (index < 0 || index >= static_cast<int>(binding.option_nodes.size())) return false;
        const ordered_json& tab_node = binding.option_nodes[static_cast<size_t>(index)];

        // Suppress events while recreating content controls so that
        // programmatic EN_CHANGE / BN_CLICKED notifications don't fire.
        g_native.suppress_events = true;
        snapshotScrollViewStateRecursive(content_host);
        clearNativeChildrenOf(content_host);
        g_native.suppress_events = false;

        auto content_it = tab_node.find("content");
        if (content_it != tab_node.end() && content_it->is_object()) {
            HDC hdc = GetDC(content_host);
            if (!hdc) return false;
            ensureFonts();
            SelectObject(hdc, g_native.ui_font);
            const int content_width = std::max(24, jsonInt(binding.data, "contentWidth", 24) - 16);
            const MeasuredSize measured = measureNode(hdc, *content_it, content_width);
            if (!positionTabContentHost(control, binding, std::max(40, measured.height + 16))) {
                ReleaseDC(content_host, hdc);
                return false;
            }
            layoutNode(content_host, hdc, *content_it, 8, 8, content_width);
            ReleaseDC(content_host, hdc);
        }
        InvalidateRect(content_host, nullptr, TRUE);
        return true;
    }

    if (type == "slider") {
        bool handled = false;

        auto label_it = props.find("label");
        if (label_it != props.end() && label_it->is_string()) {
            binding.text = label_it->get<std::string>();
            if (binding.companion_hwnd && IsWindow(binding.companion_hwnd)) {
                const std::wstring label = utf8ToWide(binding.text);
                if (!windowTextEquals(binding.companion_hwnd, label)) {
                    SetWindowTextW(binding.companion_hwnd, label.c_str());
                }
                InvalidateRect(binding.companion_hwnd, nullptr, TRUE);
                UpdateWindow(binding.companion_hwnd);
            }
            handled = true;
        }

        auto value_it = props.find("value");
        if (value_it != props.end() && value_it->is_number()) {
            const int value = value_it->is_number_integer()
                ? value_it->get<int>()
                : static_cast<int>(value_it->get<double>());
            SendMessageW(control, TBM_SETPOS, TRUE, value);
            binding.value_text = std::to_string(value);
            handled = true;
        }

        return handled;
    }

    if (type == "progress") {
        bool handled = false;

        auto label_it = props.find("label");
        if (label_it != props.end() && label_it->is_string()) {
            binding.text = label_it->get<std::string>();
            handled = true;
        }

        auto min_it = props.find("min");
        auto max_it = props.find("max");
        if ((min_it != props.end() && min_it->is_number()) ||
            (max_it != props.end() && max_it->is_number())) {
            const LRESULT range = SendMessageW(control, PBM_GETRANGE, 0, 0);
            const int current_min = LOWORD(range);
            const int current_max = HIWORD(range);
            const int next_min = (min_it != props.end() && min_it->is_number())
                ? (min_it->is_number_integer() ? min_it->get<int>() : static_cast<int>(min_it->get<double>()))
                : current_min;
            const int next_max = (max_it != props.end() && max_it->is_number())
                ? (max_it->is_number_integer() ? max_it->get<int>() : static_cast<int>(max_it->get<double>()))
                : current_max;
            SendMessageW(control, PBM_SETRANGE32, next_min, next_max);
            handled = true;
        }

        auto value_it = props.find("value");
        if (value_it != props.end() && value_it->is_number()) {
            const int value = value_it->is_number_integer()
                ? value_it->get<int>()
                : static_cast<int>(value_it->get<double>());
            SendMessageW(control, PBM_SETPOS, value, 0);
            binding.value_text = std::to_string(value);
            handled = true;
        }

        if (binding.companion_hwnd) {
            const std::wstring header = progressHeaderText(binding, binding.value_text);
            if (!header.empty() && !windowTextEquals(binding.companion_hwnd, header)) {
                SetWindowTextW(binding.companion_hwnd, header.c_str());
            }
            InvalidateRect(binding.companion_hwnd, nullptr, TRUE);
            UpdateWindow(binding.companion_hwnd);
        }

        if (handled) {
            InvalidateRect(control, nullptr, TRUE);
            UpdateWindow(control);
        }
        return handled;
    }

    if (type == "table") {
        bool handled = false;
        auto rows_it = props.find("rows");
        auto columns_it = props.find("columns");
        std::string selected_id = binding.value_text;
        auto selected_it = props.find("selectedId");
        if (selected_it != props.end() && selected_it->is_string()) {
            selected_id = selected_it->get<std::string>();
        }
        if (columns_it != props.end() && columns_it->is_array()) {
            const ordered_json rows = (rows_it != props.end() && rows_it->is_array())
                ? *rows_it
                : ordered_json(binding.row_objects);
            handled = syncTableColumns(control, binding, *columns_it, rows, selected_id);
        }
        if (rows_it != props.end() && rows_it->is_array()) {
            if (columns_it == props.end()) {
                syncTableRows(control, binding, *rows_it, selected_id);
            }
            handled = true;
        }
        if (selected_it != props.end() && selected_it->is_string()) {
            handled = selectTableById(control, binding, selected_it->get<std::string>()) || handled;
        }
        return handled;
    }

    if (type == "tree-view") {
        bool handled = false;

        auto label_it = props.find("label");
        if (label_it != props.end() && label_it->is_string()) {
            binding.text = label_it->get<std::string>();
            if (binding.companion_hwnd && IsWindow(binding.companion_hwnd)) {
                const std::wstring label = utf8ToWide(binding.text);
                if (!windowTextEquals(binding.companion_hwnd, label)) {
                    SetWindowTextW(binding.companion_hwnd, label.c_str());
                }
            }
            handled = true;
        }

        std::string selected_id = binding.value_text;
        auto selected_it = props.find("selectedId");
        if (selected_it != props.end() && selected_it->is_string()) {
            selected_id = selected_it->get<std::string>();
        }

        ordered_json expanded_ids = binding.data.contains("expandedIds")
            ? binding.data["expandedIds"]
            : ordered_json::array();
        auto expanded_it = props.find("expandedIds");
        if (expanded_it != props.end() && expanded_it->is_array()) {
            expanded_ids = *expanded_it;
        }

        auto items_it = props.find("items");
        if (items_it != props.end() && items_it->is_array()) {
            syncTreeView(control, binding, *items_it, selected_id, expanded_ids);
            handled = true;
        } else {
            if (expanded_it != props.end() && expanded_it->is_array()) {
                applyTreeExpandedIds(control, binding, expanded_ids);
                handled = true;
            }
            if (selected_it != props.end() && selected_it->is_string()) {
                handled = selectTreeById(control, binding, selected_id) || handled;
            }
        }

        return handled;
    }

    if (type == "split-view") {
        if (props.contains("width") || props.contains("height")) return false;
        return refreshSplitViewControl(node_id);
    }

    if (type == "split-pane") {
        return refreshContainingSplitViewControl(control);
    }

    if (type == "context-menu") {
        bool handled = false;
        auto items_it = props.find("items");
        if (items_it != props.end() && items_it->is_array()) {
            binding.data["items"] = *items_it;
            handled = true;
        }
        auto disabled_it = props.find("disabled");
        if (disabled_it != props.end()) {
            binding.data["disabled"] = *disabled_it;
            handled = true;
        }
        return handled;
    }

    return false;
}

bool applyNativePatchOperation(HWND hwnd, const ordered_json& op) {
    if (!op.is_object()) return false;
    const std::string op_name = jsonString(op, "op");
    if (op_name.empty()) return false;

    if (op_name == "set-window-props") {
        auto props_it = op.find("props");
        if (props_it == op.end() || !props_it->is_object()) return false;
        bool handled_directly = false;
        for (auto it = props_it->begin(); it != props_it->end(); ++it) {
            if (it.key() == "title") {
                if (!it.value().is_string()) return false;
                const std::wstring title = utf8ToWide(it.value().get<std::string>());
                SetWindowTextW(hwnd, title.empty() ? L"WinScheme Native UI" : title.c_str());
                handled_directly = true;
                continue;
            }
            if (it.key() == "menuBar") {
                ordered_json spec = snapshotSpec();
                if (!spec.is_object()) return false;
                installNativeMenuIfPresent(hwnd, spec);
                refreshNativeViewport(hwnd);
                updateNativeRootScrollbars(hwnd);
                handled_directly = true;
                continue;
            }
            if (it.key() == "commandBar") {
                if (!it.value().is_object() || !g_native.command_bar_hwnd || !IsWindow(g_native.command_bar_hwnd)) {
                    handled_directly = false;
                    break;
                }
                applyCommandBarItems(g_native.command_bar_hwnd, it.value());
                refreshNativeViewport(hwnd);
                updateNativeRootScrollbars(hwnd);
                handled_directly = true;
                continue;
            }
            if (it.key() == "statusBar") {
                if (!it.value().is_object() || !g_native.status_bar_hwnd || !IsWindow(g_native.status_bar_hwnd)) {
                    handled_directly = false;
                    break;
                }
                applyStatusBarParts(g_native.status_bar_hwnd, it.value(), std::max(1, g_native.raw_client_width));
                refreshNativeViewport(hwnd);
                updateNativeRootScrollbars(hwnd);
                handled_directly = true;
                continue;
            }
            handled_directly = false;
            break;
        }

        if (handled_directly) {
            g_native.patch_direct_apply_count.fetch_add(1, std::memory_order_relaxed);
            traceNativePatchDecision("direct", op_name, std::string(), "window-props");
            return true;
        }

        rebuildNativeWindow(hwnd);
        g_native.patch_window_rebuild_count.fetch_add(1, std::memory_order_relaxed);
        traceNativePatchDecision("window-rebuild", op_name);
        return true;
    }

    if (op_name == "set-node-props") {
        const std::string node_id = jsonString(op, "id");
        if (node_id.empty()) return false;
        auto props_it = op.find("props");
        if (props_it == op.end() || !props_it->is_object()) return false;
        if (applyNativeNodePropsPatch(hwnd, node_id, *props_it)) {
            g_native.patch_direct_apply_count.fetch_add(1, std::memory_order_relaxed);
            traceNativePatchDecision("direct", op_name, node_id);
            return true;
        }
        std::string rebuilt_ancestor_id;
        if (tryRebuildAncestorContainerForNode(node_id, &rebuilt_ancestor_id)) {
            g_native.patch_subtree_rebuild_count.fetch_add(1, std::memory_order_relaxed);
            traceNativePatchDecision("subtree-rebuild", op_name, node_id, rebuilt_ancestor_id);
            return true;
        }
        traceNativePatchDecision("failed", op_name, node_id);
        return false;
    }

    if (op_name == "tree-replace-items" ||
        op_name == "tree-insert-item" ||
        op_name == "tree-remove-item" ||
        op_name == "tree-move-item" ||
        op_name == "tree-set-item-props" ||
        op_name == "tree-set-expanded-ids" ||
        op_name == "tree-set-selected-id") {
        const std::string node_id = jsonString(op, "id");
        if (node_id.empty()) return false;
        if (refreshTreeViewControl(node_id)) {
            g_native.patch_direct_apply_count.fetch_add(1, std::memory_order_relaxed);
            traceNativePatchDecision("direct", op_name, node_id);
            return true;
        }
        std::string rebuilt_ancestor_id;
        if (tryRebuildAncestorContainerForNode(node_id, &rebuilt_ancestor_id)) {
            g_native.patch_subtree_rebuild_count.fetch_add(1, std::memory_order_relaxed);
            traceNativePatchDecision("subtree-rebuild", op_name, node_id, rebuilt_ancestor_id);
            return true;
        }
        traceNativePatchDecision("failed", op_name, node_id);
        return false;
    }

    if (op_name == "replace-children" || op_name == "append-child" || op_name == "insert-child" || op_name == "remove-child" || op_name == "move-child") {
        const std::string node_id = jsonString(op, "id");
        if (node_id.empty()) return false;
        auto control_it = g_native.node_controls.find(node_id);
        if (control_it == g_native.node_controls.end()) return false;
        auto binding_it = g_native.bindings.find(control_it->second);
        if (binding_it == g_native.bindings.end()) return false;
        const std::string type = binding_it->second.type;
        if (!isPatchableContainerType(type)) return false;

        ordered_json node_spec = nullptr;
        {
            std::lock_guard<std::mutex> lock(g_native.mutex);
            ordered_json* target = nullptr;
            if (!findUserAppNodeById(g_native.spec, node_id, &target) || !target) return false;
            node_spec = *target;
        }
        rebuildNativeContainerContents(control_it->second, node_spec);
        g_native.patch_subtree_rebuild_count.fetch_add(1, std::memory_order_relaxed);
        traceNativePatchDecision("subtree-rebuild", op_name, node_id, node_id);
        return true;
    }

    traceNativePatchDecision("unsupported", op_name);
    return false;
}

bool applyNativePatchDocument(HWND hwnd, const ordered_json& patch) {
    if (patch.is_array()) {
        for (const auto& op : patch) {
            if (!applyNativePatchOperation(hwnd, op)) return false;
        }
        return true;
    }

    if (patch.is_object()) {
        auto ops_it = patch.find("ops");
        if (ops_it != patch.end() && ops_it->is_array()) {
            return applyNativePatchDocument(hwnd, *ops_it);
        }
        return applyNativePatchOperation(hwnd, patch);
    }

    return false;
}

MeasuredSize layoutChildrenRow(HWND parent,
                               HDC hdc,
                               const std::vector<const ordered_json*>& children,
                               int x,
                               int y,
                               int max_width,
                               int padding,
                               int gap) {
    const int available_width = std::max(72, max_width - (padding * 2) - std::max(0, static_cast<int>(children.size()) - 1) * gap);
    const std::vector<int> child_widths = computeRowChildWidths(hdc, children, available_width, 72);
    int cursor_x = x + padding;
    int max_height = 0;
    bool first = true;
    for (size_t i = 0; i < children.size(); ++i) {
        const ordered_json* child = children[i];
        if (!first) cursor_x += gap;
        const int child_limit = i < child_widths.size() ? child_widths[i] : 72;
        MeasuredSize child_size = layoutNode(parent, hdc, *child, cursor_x, y + padding, child_limit);
        cursor_x += child_size.width;
        max_height = std::max(max_height, child_size.height);
        first = false;
    }
    return {std::max(0, cursor_x - x + padding), max_height + (padding * 2)};
}

void rebuildNativeContainerContents(HWND container, const ordered_json& node) {
    snapshotScrollViewStateRecursive(container);
    clearNativeChildrenOf(container);
    ensureFonts();
    RECT client{};
    GetClientRect(container, &client);
    const int width = std::max(24, static_cast<int>(client.right - client.left));
    const std::string type = jsonString(node, "type");
    HDC hdc = GetDC(container);
    if (!hdc) return;

    if (type == "stack" || type == "toolbar" || type == "form") {
        const int padding = jsonInt(node, "padding", 0);
        const int gap = jsonInt(node, "gap", type == "form" ? 10 : kDefaultGap);
        layoutChildrenStack(container, hdc, childrenOf(node), padding, padding, std::max(24, width - (padding * 2)), gap);
    } else if (type == "row") {
        const int padding = jsonInt(node, "padding", 0);
        const int gap = jsonInt(node, "gap", kDefaultGap);
        layoutChildrenRow(container, hdc, childrenOf(node), 0, 0, width, padding, gap);
    } else if (type == "grid") {
        const int padding = jsonInt(node, "padding", 0);
        const int gap = jsonInt(node, "gap", kDefaultGap);
        const int columns = std::max(1, jsonInt(node, "columns", 2));
        layoutChildrenGrid(container, hdc, childrenOf(node), 0, 0, width, padding, gap, columns);
    } else if (type == "card") {
        const int padding = 12;
        std::wstring title = utf8ToWide(jsonString(node, "title"));
        int cursor_y = padding + (title.empty() ? 0 : 22);
        if (!title.empty()) cursor_y += 4;
        layoutChildrenStack(container, hdc, childrenOf(node), padding, cursor_y, std::max(24, width - (padding * 2)), 10);
    } else if (type == "scroll-view") {
        auto it = g_native.bindings.find(container);
        if (it != g_native.bindings.end()) {
            relayoutScrollViewControl(container, it->second, node, true);
        }
    }

    ReleaseDC(container, hdc);
}

MeasuredSize layoutChildrenStack(HWND parent,
                                 HDC hdc,
                                 const std::vector<const ordered_json*>& children,
                                 int x,
                                 int y,
                                 int max_width,
                                 int gap) {
    MeasuredSize result{};
    int cursor_y = y;
    bool first = true;
    for (const ordered_json* child : children) {
        if (!first) cursor_y += gap;
        MeasuredSize child_size = layoutNode(parent, hdc, *child, x, cursor_y, max_width);
        result.width = std::max(result.width, child_size.width);
        result.height = (cursor_y - y) + child_size.height;
        cursor_y += child_size.height;
        first = false;
    }
    return result;
}

MeasuredSize layoutNode(HWND parent, HDC hdc, const ordered_json& node, int x, int y, int max_width) {
    const std::string type = jsonString(node, "type");

    if (type == "stack" || type == "toolbar" || type == "form") {
        const int padding = jsonInt(node, "padding", 0);
        const int gap = jsonInt(node, "gap", type == "form" ? 10 : kDefaultGap);
        MeasuredSize size = measureNode(hdc, node, max_width);
        HWND container = createChildControl(L"STATIC", L"", 0, x, y, std::max(24, size.width), std::max(24, size.height), parent, nullptr);
        if (container) {
            attachContainerForwarding(container);
            ControlBinding binding;
            binding.type = type;
            binding.node_id = jsonString(node, "id");
            registerNodeControl(container, binding);
            layoutChildrenStack(container, hdc, childrenOf(node), padding, padding, std::max(24, size.width - (padding * 2)), gap);
        }
        return size;
    }

    if (type == "row") {
        const int padding = jsonInt(node, "padding", 0);
        const int gap = jsonInt(node, "gap", kDefaultGap);
        MeasuredSize size = measureNode(hdc, node, max_width);
        HWND container = createChildControl(L"STATIC", L"", 0, x, y, std::max(24, size.width), std::max(24, size.height), parent, nullptr);
        if (container) {
            attachContainerForwarding(container);
            ControlBinding binding;
            binding.type = type;
            binding.node_id = jsonString(node, "id");
            registerNodeControl(container, binding);
            layoutChildrenRow(container, hdc, childrenOf(node), 0, 0, size.width, padding, gap);
        }
        return size;
    }

    if (type == "grid") {
        const int padding = jsonInt(node, "padding", 0);
        const int gap = jsonInt(node, "gap", kDefaultGap);
        const int columns = std::max(1, jsonInt(node, "columns", 2));
        MeasuredSize size = measureNode(hdc, node, max_width);
        HWND container = createChildControl(L"STATIC", L"", 0, x, y, std::max(24, size.width), std::max(24, size.height), parent, nullptr);
        if (container) {
            attachContainerForwarding(container);
            ControlBinding binding;
            binding.type = type;
            binding.node_id = jsonString(node, "id");
            registerNodeControl(container, binding);
            layoutChildrenGrid(container, hdc, childrenOf(node), 0, 0, size.width, padding, gap, columns);
        }
        return size;
    }

    if (type == "scroll-view") {
        MeasuredSize size = measureNode(hdc, node, max_width);
        if (!ensureScrollViewHostClassRegistered()) {
            return size;
        }
        HWND container = createChildControl(kScrollViewHostClassName, L"", WS_VSCROLL | WS_CLIPCHILDREN, x, y, std::max(24, size.width), std::max(24, size.height), parent, nullptr);
        if (container) {
            attachScrollViewSubclass(container);
            ControlBinding binding;
            binding.type = type;
            binding.node_id = jsonString(node, "id");
            restorePreservedScrollViewState(binding);
            registerNodeControl(container, binding);
            auto it = g_native.bindings.find(container);
            if (it != g_native.bindings.end()) {
                relayoutScrollViewControl(container, it->second, node, true);
            }
        }
        return size;
    }

    if (type == "text") {
        const std::wstring text = nodeLabel(node);
        MeasuredSize size = measureNode(hdc, node, max_width);
        HWND label = createChildControl(L"STATIC", text.c_str(), SS_LEFT, x, y, std::max(40, max_width), size.height, parent, g_native.ui_font);
        if (label) {
            ControlBinding binding;
            binding.type = type;
            binding.node_id = jsonString(node, "id");
            binding.text = jsonString(node, "text");
            registerNodeControl(label, binding);
        }
        return {std::max(40, max_width), size.height};
    }

    if (type == "heading") {
        const std::wstring text = nodeLabel(node);
        MeasuredSize size = measureNode(hdc, node, max_width);
        HWND label = createChildControl(L"STATIC", text.c_str(), SS_LEFT, x, y, std::max(40, max_width), size.height, parent, g_native.heading_font);
        if (label) {
            ControlBinding binding;
            binding.type = type;
            binding.node_id = jsonString(node, "id");
            binding.text = jsonString(node, "text");
            registerNodeControl(label, binding);
        }
        return {std::max(40, max_width), size.height};
    }

    if (type == "link") {
        const std::wstring text = linkControlText(jsonString(node, "text"));
        MeasuredSize size = measureNode(hdc, node, max_width);
        HWND link = createChildControl(WC_LINK, text.c_str(), WS_TABSTOP, x, y, std::max(40, max_width), size.height + 4, parent, g_native.ui_font);
        if (link) {
            ControlBinding binding;
            binding.type = type;
            binding.event_name = jsonString(node, "event", "link-click");
            binding.node_id = jsonString(node, "id");
            binding.text = jsonString(node, "text");
            binding.value_text = jsonString(node, "href");
            registerNodeControl(link, binding);
        }
        return {std::max(40, max_width), size.height + 4};
    }

    if (type == "image") {
        const std::wstring label = utf8ToWide(jsonString(node, "label"));
        const int control_width = std::max(24, jsonInt(node, "width", 240));
        const int control_height = std::max(24, jsonInt(node, "height", 160));
        int cursor_y = y;
        HWND label_hwnd = nullptr;
        if (!label.empty()) {
            const SIZE label_size = measureWrappedText(hdc, g_native.ui_font, label, control_width);
            label_hwnd = createChildControl(L"STATIC", label.c_str(), SS_LEFT, x, cursor_y, control_width, label_size.cy, parent, g_native.ui_font);
            cursor_y += static_cast<int>(label_size.cy + 8);
        }
        HWND image = createChildControl(L"STATIC", L"", SS_NOTIFY, x, cursor_y, control_width, control_height, parent, nullptr);
        if (image) {
            attachImageSubclass(image);
            ControlBinding binding;
            binding.type = type;
            binding.event_name = jsonString(node, "event");
            binding.node_id = jsonString(node, "id");
            binding.text = jsonString(node, "label");
            binding.value_text = jsonString(node, "src");
            binding.companion_hwnd = label_hwnd;
            binding.data["alt"] = jsonString(node, "alt");
            binding.data["fit"] = jsonString(node, "fit", "contain");
            registerNodeControl(image, binding);
        }
        return {control_width, (cursor_y - y) + control_height};
    }

    if (type == "number-input") {
        const std::wstring label = utf8ToWide(jsonString(node, "label"));
        const std::wstring value = utf8ToWide(jsonString(node, "value"));
        const int control_width = std::max(220, std::min(max_width, 360));
        const int control_height = 24;
        int cursor_y = y;
        HWND label_hwnd = nullptr;
        if (!label.empty()) {
            const SIZE label_size = measureWrappedText(hdc, g_native.ui_font, label, control_width);
            label_hwnd = createChildControl(L"STATIC", label.c_str(), SS_LEFT, x, cursor_y, control_width, label_size.cy, parent, g_native.ui_font);
            cursor_y += static_cast<int>(label_size.cy + 8);
        }
        HWND edit = createChildControl(L"EDIT", value.c_str(), WS_TABSTOP | WS_BORDER | ES_LEFT | ES_AUTOHSCROLL, x, cursor_y, control_width, control_height, parent, g_native.ui_font);
        if (edit) {
            ControlBinding binding;
            binding.type = type;
            binding.event_name = jsonString(node, "event", "number-input-change");
            binding.node_id = jsonString(node, "id");
            binding.text = jsonString(node, "label");
            binding.value_text = jsonString(node, "value");
            binding.companion_hwnd = label_hwnd;
            if (node.contains("min")) binding.data["min"] = node["min"];
            if (node.contains("max")) binding.data["max"] = node["max"];
            if (node.contains("step")) binding.data["step"] = node["step"];
            registerNodeControl(edit, binding);
        }
        return {control_width, (cursor_y - y) + control_height};
    }

    if (type == "date-picker") {
        const std::wstring label = utf8ToWide(jsonString(node, "label"));
        const int control_width = std::max(220, std::min(max_width, 360));
        const int control_height = 24;
        int cursor_y = y;
        HWND label_hwnd = nullptr;
        if (!label.empty()) {
            const SIZE label_size = measureWrappedText(hdc, g_native.ui_font, label, control_width);
            label_hwnd = createChildControl(L"STATIC", label.c_str(), SS_LEFT, x, cursor_y, control_width, label_size.cy, parent, g_native.ui_font);
            cursor_y += static_cast<int>(label_size.cy + 8);
        }
        HWND date = createChildControl(DATETIMEPICK_CLASSW, L"", WS_TABSTOP | DTS_SHORTDATEFORMAT, x, cursor_y, control_width, control_height, parent, g_native.ui_font);
        if (date) {
            ControlBinding binding;
            binding.type = type;
            binding.event_name = jsonString(node, "event", "date-picker-change");
            binding.node_id = jsonString(node, "id");
            binding.text = jsonString(node, "label");
            binding.value_text = jsonString(node, "value");
            binding.companion_hwnd = label_hwnd;
            if (node.contains("min")) binding.data["min"] = node["min"];
            if (node.contains("max")) binding.data["max"] = node["max"];
            applyDatePickerRange(date, binding.data);
            if (binding.value_text.empty()) {
                DateTime_SetSystemtime(date, GDT_NONE, nullptr);
            } else {
                SYSTEMTIME st{};
                if (parseIsoDateString(binding.value_text, st)) {
                    DateTime_SetSystemtime(date, GDT_VALID, &st);
                }
            }
            registerNodeControl(date, binding);
        }
        return {control_width, (cursor_y - y) + control_height};
    }

    if (type == "time-picker") {
        const std::wstring label = utf8ToWide(jsonString(node, "label"));
        const int control_width = std::max(220, std::min(max_width, 360));
        const int control_height = 24;
        int cursor_y = y;
        HWND label_hwnd = nullptr;
        if (!label.empty()) {
            const SIZE label_size = measureWrappedText(hdc, g_native.ui_font, label, control_width);
            label_hwnd = createChildControl(L"STATIC", label.c_str(), SS_LEFT, x, cursor_y, control_width, label_size.cy, parent, g_native.ui_font);
            cursor_y += static_cast<int>(label_size.cy + 8);
        }
        HWND time = createChildControl(DATETIMEPICK_CLASSW, L"", WS_TABSTOP | DTS_TIMEFORMAT | DTS_UPDOWN, x, cursor_y, control_width, control_height, parent, g_native.ui_font);
        if (time) {
            SendMessageW(time, DTM_SETFORMATW, 0, reinterpret_cast<LPARAM>(L"HH':'mm"));
            ControlBinding binding;
            binding.type = type;
            binding.event_name = jsonString(node, "event", "time-picker-change");
            binding.node_id = jsonString(node, "id");
            binding.text = jsonString(node, "label");
            binding.value_text = jsonString(node, "value");
            binding.companion_hwnd = label_hwnd;
            if (node.contains("min")) binding.data["min"] = node["min"];
            if (node.contains("max")) binding.data["max"] = node["max"];
            if (node.contains("step")) binding.data["step"] = node["step"];
            applyTimePickerRange(time, binding.data);
            if (binding.value_text.empty()) {
                DateTime_SetSystemtime(time, GDT_NONE, nullptr);
            } else {
                SYSTEMTIME st{};
                if (parseIsoTimeString(binding.value_text, st)) {
                    DateTime_SetSystemtime(time, GDT_VALID, &st);
                }
            }
            registerNodeControl(time, binding);
        }
        return {control_width, (cursor_y - y) + control_height};
    }

    if (type == "button") {
        const std::wstring text = nodeLabel(node);
        MeasuredSize size = measureNode(hdc, node, max_width);
        HWND button = createChildControl(L"BUTTON", text.c_str(), BS_PUSHBUTTON | WS_TABSTOP, x, y, size.width, size.height, parent, g_native.ui_font);
        if (button) {
            ControlBinding binding;
            binding.type = type;
            binding.event_name = jsonString(node, "event");
            binding.node_id = jsonString(node, "id");
            binding.text = jsonString(node, "text");
            registerNodeControl(button, binding);
        }
        return size;
    }

    if (type == "checkbox" || type == "switch") {
        const std::wstring label = nodeLabel(node, "label");
        MeasuredSize size = measureNode(hdc, node, max_width);
        HWND button = createChildControl(L"BUTTON", label.c_str(), BS_AUTOCHECKBOX | WS_TABSTOP, x, y, size.width, size.height, parent, g_native.ui_font);
        if (button) {
            SendMessageW(button, BM_SETCHECK, jsonString(node, "checked") == "true" ? BST_CHECKED : BST_UNCHECKED, 0);
            ControlBinding binding;
            binding.type = type;
            binding.event_name = jsonString(node, "event", type == "switch" ? "switch-change" : "checkbox-change");
            binding.node_id = jsonString(node, "id");
            binding.text = jsonString(node, "label");
            registerNodeControl(button, binding);
        }
        return size;
    }

    if (type == "input" || type == "textarea") {
        const bool multiline = type == "textarea";
        const std::wstring label = utf8ToWide(jsonString(node, "label"));
        const std::wstring value = utf8ToWide(jsonString(node, "value"));
        const int rows = std::max(2, jsonInt(node, "rows", 4));
        const int control_width = std::max(multiline ? 280 : 220, std::min(max_width, multiline ? 420 : 360));
        const int control_height = multiline ? std::max(78, (rows * 22) + 12) : 24;
        int cursor_y = y;
        if (!label.empty()) {
            const SIZE label_size = measureWrappedText(hdc, g_native.ui_font, label, control_width);
            createChildControl(L"STATIC", label.c_str(), SS_LEFT, x, cursor_y, control_width, label_size.cy, parent, g_native.ui_font);
            cursor_y += static_cast<int>(label_size.cy + 8);
        }
        DWORD edit_style = WS_TABSTOP | WS_BORDER | ES_LEFT;
        if (multiline) {
            edit_style |= ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL;
        } else {
            edit_style |= ES_AUTOHSCROLL;
        }
        HWND edit = createChildControl(L"EDIT", value.c_str(), edit_style, x, cursor_y, control_width, control_height, parent, g_native.ui_font);
        if (edit) {
            ControlBinding binding;
            binding.type = type;
            binding.event_name = jsonString(node, "event", multiline ? "textarea-change" : "input-change");
            binding.node_id = jsonString(node, "id");
            binding.text = jsonString(node, "label");
            binding.value_text = jsonString(node, "value");
            binding.multiline = multiline;
            registerNodeControl(edit, binding);
        }
        return {control_width, (cursor_y - y) + control_height};
    }

    if (type == "rich-text") {
        const std::wstring label = utf8ToWide(jsonString(node, "label"));
        const std::string value_rtf = jsonString(node, "value");
        const int control_width = std::max(320, std::min(max_width, 520));
        const int control_height = std::max(120, jsonInt(node, "minHeight", 140));
        int cursor_y = y;
        HWND label_hwnd = nullptr;
        if (!label.empty()) {
            const SIZE label_size = measureWrappedText(hdc, g_native.ui_font, label, control_width);
            label_hwnd = createChildControl(L"STATIC", label.c_str(), SS_LEFT, x, cursor_y, control_width, label_size.cy, parent, g_native.ui_font);
            cursor_y += static_cast<int>(label_size.cy + 8);
        }
        DWORD edit_style = WS_TABSTOP | WS_BORDER | ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | ES_WANTRETURN;
        HWND rich = createChildControl(MSFTEDIT_CLASS, L"", edit_style, x, cursor_y, control_width, control_height, parent, g_native.ui_font);
        if (rich) {
            SendMessageW(rich, EM_SETBKGNDCOLOR, 0, RGB(255, 255, 255));
            const LRESULT event_mask = SendMessageW(rich, EM_GETEVENTMASK, 0, 0);
            SendMessageW(rich, EM_SETEVENTMASK, 0, event_mask | ENM_CHANGE);
            attachRichEditSubclass(rich);
            ControlBinding binding;
            binding.type = type;
            binding.event_name = jsonString(node, "event", "rich-text-change");
            binding.node_id = jsonString(node, "id");
            binding.text = jsonString(node, "label");
            binding.value_text = value_rtf;
            binding.multiline = true;
            binding.companion_hwnd = label_hwnd;
            if (!value_rtf.empty()) {
                richEditSetRtf(rich, value_rtf);
            }
            registerNodeControl(rich, binding);
        }
        return {control_width, (cursor_y - y) + control_height};
    }

    if (type == "select") {
        const std::wstring label = utf8ToWide(jsonString(node, "label"));
        const std::string selected_value = jsonString(node, "value");
        const int control_width = std::max(220, std::min(max_width, 360));
        int cursor_y = y;
        if (!label.empty()) {
            const SIZE label_size = measureWrappedText(hdc, g_native.ui_font, label, control_width);
            createChildControl(L"STATIC", label.c_str(), SS_LEFT, x, cursor_y, control_width, label_size.cy, parent, g_native.ui_font);
            cursor_y += static_cast<int>(label_size.cy + 8);
        }
        HWND combo = createChildControl(WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, x, cursor_y, control_width, 220, parent, g_native.ui_font);
        if (combo) {
            ControlBinding binding;
            binding.type = type;
            binding.event_name = jsonString(node, "event", "select-change");
            binding.node_id = jsonString(node, "id");
            binding.text = jsonString(node, "label");
            int selected_index = -1;
            if (const ordered_json* options = jsonArrayChild(node, "options")) {
                int index = 0;
                for (const auto& option : *options) {
                    if (!option.is_object()) continue;
                    const std::string option_value = jsonString(option, "value", jsonString(option, "text", jsonString(option, "label")));
                    const std::wstring option_text = utf8ToWide(jsonString(option, "text", jsonString(option, "label", option_value)));
                    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(option_text.c_str()));
                    binding.option_values.push_back(option_value);
                    if (selected_index < 0 && option_value == selected_value) selected_index = index;
                    ++index;
                }
            }
            if (selected_index >= 0) {
                SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(selected_index), 0);
            } else if (!binding.option_values.empty()) {
                SendMessageW(combo, CB_SETCURSEL, 0, 0);
            }
            binding.value_text = selected_value;
            registerNodeControl(combo, binding);
        }
        return {control_width, (cursor_y - y) + 28};
    }

    if (type == "canvas") {
        const std::wstring label = utf8ToWide(jsonString(node, "label"));
        const int control_width = std::max(80, jsonInt(node, "width", 320));
        const int control_height = std::max(60, jsonInt(node, "height", 180));
        int cursor_y = y;
        HWND label_hwnd = nullptr;
        if (!label.empty()) {
            const SIZE label_size = measureWrappedText(hdc, g_native.ui_font, label, control_width);
            label_hwnd = createChildControl(L"STATIC", label.c_str(), SS_LEFT, x, cursor_y, control_width, label_size.cy, parent, g_native.ui_font);
            cursor_y += static_cast<int>(label_size.cy + 8);
        }
        HWND canvas = createChildControl(L"STATIC", L"", SS_NOTIFY, x, cursor_y, control_width, control_height, parent, nullptr);
        if (canvas) {
            attachCanvasSubclass(canvas);
            ControlBinding binding;
            binding.type = type;
            binding.event_name = jsonString(node, "event");
            binding.node_id = jsonString(node, "id");
            binding.text = jsonString(node, "label");
            binding.companion_hwnd = label_hwnd;
            if (const ordered_json* commands = jsonArrayChild(node, "commands")) {
                binding.data["commands"] = *commands;
            } else {
                binding.data["commands"] = ordered_json::array();
            }
            registerNodeControl(canvas, binding);
        }
        return {control_width, (cursor_y - y) + control_height};
    }

    if (type == "text-grid" || type == "text-grid-pane" || type == "indexed-graphics" || type == "rgba-pane" || type == "pane") {
        const std::wstring label = utf8ToWide(jsonString(node, "label"));
        MeasuredSize measured = measureNode(hdc, node, max_width);
        const int control_width = std::max(24, measured.width);
        int cursor_y = y;
        HWND label_hwnd = nullptr;
        if (!label.empty()) {
            const SIZE label_size = measureWrappedText(hdc, g_native.ui_font, label, control_width);
            label_hwnd = createChildControl(L"STATIC", label.c_str(), SS_LEFT, x, cursor_y, control_width, label_size.cy, parent, g_native.ui_font);
            cursor_y += static_cast<int>(label_size.cy + 8);
        }
        const int pane_height = std::max(24, measured.height - (cursor_y - y));
        HWND pane_host = createChildControl(L"STATIC", L"", WS_CLIPCHILDREN | WS_CLIPSIBLINGS, x, cursor_y, control_width, pane_height, parent, nullptr);
        if (pane_host) {
            attachContainerForwarding(pane_host);
            attachTransparentPaneSubclass(pane_host);
            ControlBinding binding;
            binding.type = type;
            binding.event_name = jsonString(node, "event");
            binding.node_id = jsonString(node, "id");
            binding.text = jsonString(node, "label");
            binding.companion_hwnd = label_hwnd;
            registerNodeControl(pane_host, binding);
        }
        return {control_width, (cursor_y - y) + pane_height};
    }

    if (type == "tabs") {
        const std::wstring label = utf8ToWide(jsonString(node, "label"));
        const int control_width = std::max(320, std::min(max_width, 700));
        int cursor_y = y;
        if (!label.empty()) {
            const SIZE label_size = measureWrappedText(hdc, g_native.ui_font, label, control_width);
            createChildControl(L"STATIC", label.c_str(), SS_LEFT, x, cursor_y, control_width, label_size.cy, parent, g_native.ui_font);
            cursor_y += static_cast<int>(label_size.cy + 8);
        }

        constexpr int kProvisionalTabHeight = 220;
        HWND tab = createChildControl(WC_TABCONTROLW, L"", WS_TABSTOP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, x, cursor_y, control_width, kProvisionalTabHeight, parent, g_native.ui_font);
        int selected_index = 0;
        std::string selected_value = jsonString(node, "value");
        ControlBinding binding;
        binding.type = type;
        binding.event_name = jsonString(node, "event", "tabs-change");
        binding.node_id = jsonString(node, "id");
        binding.text = jsonString(node, "label");

        if (const ordered_json* tabs = jsonArrayChild(node, "tabs")) {
            int index = 0;
            for (const auto& tab_node : *tabs) {
                if (!tab_node.is_object()) continue;
                const std::string value = jsonString(tab_node, "value", "tab-" + std::to_string(index));
                const std::wstring text = utf8ToWide(jsonString(tab_node, "text", jsonString(tab_node, "label", value)));
                TCITEMW item{};
                item.mask = TCIF_TEXT;
                item.pszText = const_cast<wchar_t*>(text.c_str());
                TabCtrl_InsertItem(tab, index, &item);
                binding.option_values.push_back(value);
                binding.option_nodes.push_back(tab_node);
                if (!selected_value.empty() && value == selected_value) selected_index = index;
                ++index;
            }
        }

        if (!binding.option_values.empty()) {
            TabCtrl_SetCurSel(tab, selected_index);
        }
        binding.value_text = selected_value;

        // Create a dedicated content host for this tabs node.
        // Storing it as companion_hwnd lets the patch path clear and
        // re-layout only the content area on a value change, without
        // rebuilding the ancestor container.
        const ordered_json* active_content = nullptr;
        if (!binding.option_nodes.empty()) {
            const ordered_json& active_tab = binding.option_nodes[static_cast<size_t>(std::clamp(selected_index, 0, static_cast<int>(binding.option_nodes.size()) - 1))];
            auto content_it = active_tab.find("content");
            if (content_it != active_tab.end() && content_it->is_object()) {
                active_content = &*content_it;
            }
        }
        const MeasuredSize pre_size = active_content ? measureNode(hdc, *active_content, control_width - 16) : MeasuredSize{};
        const int content_host_height = pre_size.height > 0 ? pre_size.height + 16 : 40;

        const int tab_header_height = 28;
        const int content_offset_x = 0;
        const int content_offset_y = tab_header_height + 4;
        const int content_width = control_width;

        if (tab) {
            SetWindowPos(tab, nullptr, x, cursor_y, control_width, tab_header_height, SWP_NOZORDER | SWP_NOACTIVATE);
        }

        const int content_x = x + content_offset_x;
        const int content_y = cursor_y + content_offset_y;

        HWND content_host = createChildControl(L"STATIC", L"", WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
            content_x, content_y, content_width, content_host_height, parent, nullptr);
        if (content_host) attachContainerForwarding(content_host);
        binding.companion_hwnd = content_host;
        binding.data["contentOffsetX"] = content_offset_x;
        binding.data["contentOffsetY"] = content_offset_y;
        binding.data["contentWidth"] = content_width;
        binding.data["contentHostHeight"] = content_host_height;
        registerNodeControl(tab, binding);

        if (content_host && tab) {
            SetWindowPos(content_host,
                         tab,
                         content_x,
                         content_y,
                         content_width,
                         content_host_height,
                         SWP_NOACTIVATE);
        }

        if (active_content && content_host) {
            MeasuredSize content = layoutNode(content_host, hdc, *active_content, 8, 8, std::max(24, content_width - 16));
            return {control_width, (cursor_y - y) + content_offset_y + std::max(content_host_height, content.height + 16)};
        }

        if (content_host) {
            createChildControl(L"STATIC", L"No tab content", SS_LEFT, 8, 8, std::max(24, content_width - 16), 24, content_host, g_native.ui_font);
        }
        return {control_width, (cursor_y - y) + content_offset_y + std::max(content_host_height, 40)};
    }

    if (type == "context-menu") {
        const int gap = std::max(0, jsonInt(node, "gap", 0));
        MeasuredSize measured = measureNode(hdc, node, max_width);
        HWND container = createChildControl(L"STATIC", L"", 0, x, y,
            std::max(24, measured.width), std::max(24, measured.height), parent, nullptr);
        if (container) {
            attachContainerForwarding(container);
            ControlBinding binding;
            binding.type = type;
            binding.node_id = jsonString(node, "id");
            if (const ordered_json* items = jsonArrayChild(node, "items")) {
                binding.data["items"] = *items;
            } else {
                binding.data["items"] = ordered_json::array();
            }
            auto disabled_it = node.find("disabled");
            if (disabled_it != node.end()) binding.data["disabled"] = *disabled_it;
            registerNodeControl(container, binding);
            layoutChildrenStack(container, hdc, childrenOf(node), 0, 0, std::max(24, measured.width), gap);
        }
        return measured;
    }

    if (type == "split-pane") {
        const int padding = std::max(0, jsonInt(node, "padding", 0));
        const int gap = std::max(0, jsonInt(node, "gap", kDefaultGap));
        MeasuredSize measured = measureNode(hdc, node, max_width);
        HWND container = createChildControl(L"STATIC", L"", 0, x, y,
            std::max(24, measured.width), std::max(24, measured.height), parent, nullptr);
        if (container) {
            attachContainerForwarding(container);
            ControlBinding binding;
            binding.type = type;
            binding.node_id = jsonString(node, "id");
            registerNodeControl(container, binding);
            layoutChildrenStack(container, hdc, childrenOf(node), padding, padding,
                std::max(24, measured.width - (padding * 2)), gap);
        }
        return measured;
    }

    if (type == "split-view") {
        MeasuredSize measured = measureNode(hdc, node, max_width);
        HWND container = createChildControl(L"STATIC", L"", 0, x, y,
            std::max(24, measured.width), std::max(24, measured.height), parent, nullptr);
        if (container) {
            attachContainerForwarding(container);
            attachSplitViewSubclass(container);
            ControlBinding binding;
            binding.type = type;
            binding.event_name = jsonString(node, "event", "split-resize");
            binding.node_id = jsonString(node, "id");
            registerNodeControl(container, binding);
            relayoutSplitViewControl(container, g_native.bindings[container], node, true);
        }
        return measured;
    }

    if (type == "table") {
        const std::wstring label = utf8ToWide(jsonString(node, "label"));
        const int control_width = std::max(420, std::min(max_width, 820));
        const int control_height = std::max(180, jsonInt(node, "height", 240));
        int cursor_y = y;
        if (!label.empty()) {
            const SIZE label_size = measureWrappedText(hdc, g_native.ui_font, label, control_width);
            createChildControl(L"STATIC", label.c_str(), SS_LEFT, x, cursor_y, control_width, label_size.cy, parent, g_native.ui_font);
            cursor_y += static_cast<int>(label_size.cy + 8);
        }

        HWND table = createChildControl(WC_LISTVIEWW, L"", LVS_REPORT | LVS_SINGLESEL | WS_TABSTOP | WS_BORDER, x, cursor_y, control_width, control_height, parent, g_native.ui_font);
        if (table) {
            ListView_SetExtendedListViewStyle(table, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_GRIDLINES);

            ControlBinding binding;
            binding.type = type;
            binding.event_name = jsonString(node, "event", "table-select");
            binding.node_id = jsonString(node, "id");
            binding.text = jsonString(node, "label");

            std::vector<std::string> column_keys;
            if (const ordered_json* columns = jsonArrayChild(node, "columns")) {
                int column_index = 0;
                for (const auto& column : *columns) {
                    if (!column.is_object()) continue;
                    const std::string key = jsonString(column, "key", "col-" + std::to_string(column_index));
                    const std::wstring title = utf8ToWide(jsonString(column, "title", key));
                    LVCOLUMNW list_column{};
                    list_column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
                    list_column.pszText = const_cast<wchar_t*>(title.c_str());
                    list_column.cx = std::max(110, control_width / std::max(1, static_cast<int>(columns->size())));
                    list_column.iSubItem = column_index;
                    ListView_InsertColumn(table, column_index, &list_column);
                    column_keys.push_back(key);
                    ++column_index;
                }
            }
            binding.column_keys = column_keys;

            if (const ordered_json* rows = jsonArrayChild(node, "rows")) {
                int row_index = 0;
                const std::string selected_id = jsonString(node, "selectedId");
                for (const auto& row : *rows) {
                    if (!row.is_object()) continue;
                    const std::string row_id = jsonString(row, "id", "row-" + std::to_string(row_index));
                    binding.option_values.push_back(row_id);
                    binding.row_objects.push_back(row);

                    LVITEMW item{};
                    item.mask = LVIF_TEXT;
                    item.iItem = row_index;
                    const std::wstring first_text = utf8ToWide(column_keys.empty() ? row_id : jsonString(row, column_keys[0].c_str()));
                    item.pszText = const_cast<wchar_t*>(first_text.c_str());
                    const int inserted = ListView_InsertItem(table, &item);

                    for (size_t col = 1; col < column_keys.size(); ++col) {
                        const std::wstring text = utf8ToWide(jsonString(row, column_keys[col].c_str()));
                        ListView_SetItemText(table, inserted, static_cast<int>(col), const_cast<wchar_t*>(text.c_str()));
                    }

                    if (!selected_id.empty() && row_id == selected_id) {
                        ListView_SetItemState(table, inserted, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                    }
                    ++row_index;
                }
            }

            binding.value_text = jsonString(node, "selectedId");
            registerNodeControl(table, binding);
        }
        return {control_width, (cursor_y - y) + control_height};
    }

    if (type == "tree-view") {
        const std::wstring label = utf8ToWide(jsonString(node, "label"));
        const int control_width = std::max(280, std::min(max_width, 520));
        const int control_height = std::max(220, std::min(420, jsonInt(node, "height", 300)));
        int cursor_y = y;
        HWND label_hwnd = nullptr;
        if (!label.empty()) {
            const SIZE label_size = measureWrappedText(hdc, g_native.ui_font, label, control_width);
            label_hwnd = createChildControl(L"STATIC", label.c_str(), SS_LEFT, x, cursor_y, control_width, label_size.cy, parent, g_native.ui_font);
            cursor_y += static_cast<int>(label_size.cy + 8);
        }

        DWORD style = WS_TABSTOP | WS_BORDER | TVS_SHOWSELALWAYS;
        auto show_buttons_it = node.find("showButtons");
        const bool show_buttons = (show_buttons_it == node.end()) ? true : isTruthyJson(*show_buttons_it);
        auto show_root_lines_it = node.find("showRootLines");
        const bool show_root_lines = (show_root_lines_it == node.end()) ? true : isTruthyJson(*show_root_lines_it);
        if (show_buttons) style |= TVS_HASBUTTONS;
        if (show_root_lines) style |= TVS_LINESATROOT | TVS_HASLINES;

        HWND tree = createChildControl(WC_TREEVIEWW, L"", style, x, cursor_y, control_width, control_height, parent, g_native.ui_font);
        if (tree) {
            ControlBinding binding;
            binding.type = type;
            binding.event_name = jsonString(node, "event", "tree-select");
            binding.node_id = jsonString(node, "id");
            binding.text = jsonString(node, "label");
            binding.companion_hwnd = label_hwnd;
            binding.data["toggleEvent"] = jsonString(node, "toggleEvent", "tree-toggle");
            binding.data["activateEvent"] = jsonString(node, "activateEvent", "tree-activate");

            const ordered_json* items = jsonArrayChild(node, "items");
            const ordered_json* expanded_ids = jsonArrayChild(node, "expandedIds");
            const std::string selected_id = jsonString(node, "selectedId");
            repopulateTreeView(tree,
                               binding,
                               items ? *items : ordered_json::array(),
                               selected_id,
                               expanded_ids ? *expanded_ids : ordered_json::array());
            registerNodeControl(tree, binding);
        }
        return {control_width, (cursor_y - y) + control_height};
    }

    if (type == "list-box") {
        const std::wstring label = utf8ToWide(jsonString(node, "label"));
        const std::string selected_value = jsonString(node, "value");
        const int rows = std::max(3, jsonInt(node, "rows", 6));
        const int control_width = std::max(240, std::min(max_width, 420));
        int cursor_y = y;
        if (!label.empty()) {
            const SIZE label_size = measureWrappedText(hdc, g_native.ui_font, label, control_width);
            createChildControl(L"STATIC", label.c_str(), SS_LEFT, x, cursor_y, control_width, label_size.cy, parent, g_native.ui_font);
            cursor_y += static_cast<int>(label_size.cy + 8);
        }
        const int control_height = std::max(90, rows * 22 + 8);
        HWND list = createChildControl(WC_LISTBOXW, L"", LBS_NOTIFY | WS_TABSTOP | WS_BORDER | WS_VSCROLL, x, cursor_y, control_width, control_height, parent, g_native.ui_font);
        if (list) {
            ControlBinding binding;
            binding.type = type;
            binding.event_name = jsonString(node, "event", "list-box-change");
            binding.node_id = jsonString(node, "id");
            binding.text = jsonString(node, "label");
            int selected_index = -1;
            if (const ordered_json* options = jsonArrayChild(node, "options")) {
                int index = 0;
                for (const auto& option : *options) {
                    if (!option.is_object()) continue;
                    const std::string option_value = jsonString(option, "value", jsonString(option, "text", jsonString(option, "label")));
                    const std::wstring option_text = utf8ToWide(jsonString(option, "text", jsonString(option, "label", option_value)));
                    SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(option_text.c_str()));
                    binding.option_values.push_back(option_value);
                    if (selected_index < 0 && option_value == selected_value) selected_index = index;
                    ++index;
                }
            }
            if (selected_index >= 0) {
                SendMessageW(list, LB_SETCURSEL, static_cast<WPARAM>(selected_index), 0);
            }
            binding.value_text = selected_value;
            registerNodeControl(list, binding);
        }
        return {control_width, (cursor_y - y) + control_height};
    }

    if (type == "radio-group") {
        const std::wstring label = utf8ToWide(jsonString(node, "label"));
        const std::string selected_value = jsonString(node, "value");
        const int control_width = std::max(240, std::min(max_width, 420));
        int cursor_y = y;
        if (!label.empty()) {
            const SIZE label_size = measureWrappedText(hdc, g_native.ui_font, label, control_width);
            createChildControl(L"STATIC", label.c_str(), SS_LEFT, x, cursor_y, control_width, label_size.cy, parent, g_native.ui_font);
            cursor_y += static_cast<int>(label_size.cy + 8);
        }
        if (const ordered_json* options = jsonArrayChild(node, "options")) {
            int index = 0;
            for (const auto& option : *options) {
                if (!option.is_object()) continue;
                const std::string option_value = jsonString(option, "value", jsonString(option, "text", jsonString(option, "label")));
                const std::wstring option_text = utf8ToWide(jsonString(option, "text", jsonString(option, "label", option_value)));
                const DWORD style = WS_TABSTOP | BS_AUTORADIOBUTTON | (index == 0 ? WS_GROUP : 0);
                HWND radio = createChildControl(L"BUTTON", option_text.c_str(), style, x, cursor_y, control_width, 24, parent, g_native.ui_font);
                if (radio) {
                    SendMessageW(radio, BM_SETCHECK, option_value == selected_value ? BST_CHECKED : BST_UNCHECKED, 0);
                    ControlBinding binding;
                    binding.type = type;
                    binding.event_name = jsonString(node, "event", "radio-change");
                    binding.node_id = jsonString(node, "id");
                    binding.text = jsonString(node, "label");
                    binding.value_text = option_value;
                    registerNodeControl(radio, binding);
                }
                cursor_y += 28;
                ++index;
            }
        }
        return {control_width, cursor_y - y};
    }

    if (type == "slider") {
        const std::wstring label = utf8ToWide(jsonString(node, "label"));
        const int min_value = jsonInt(node, "min", 0);
        const int max_value = jsonInt(node, "max", 100);
        const int current_value = jsonInt(node, "value", min_value);
        const int control_width = std::max(260, std::min(max_width, 420));
        int cursor_y = y;
        HWND label_hwnd = nullptr;
        if (!label.empty()) {
            const SIZE label_size = measureWrappedText(hdc, g_native.ui_font, label, control_width);
            label_hwnd = createChildControl(L"STATIC", label.c_str(), SS_LEFT, x, cursor_y, control_width, label_size.cy, parent, g_native.ui_font);
            cursor_y += static_cast<int>(label_size.cy + 8);
        }
        HWND slider = createChildControl(TRACKBAR_CLASSW, L"", TBS_AUTOTICKS | WS_TABSTOP, x, cursor_y, control_width, 32, parent, g_native.ui_font);
        if (slider) {
            SendMessageW(slider, TBM_SETRANGEMIN, TRUE, min_value);
            SendMessageW(slider, TBM_SETRANGEMAX, TRUE, max_value);
            SendMessageW(slider, TBM_SETPOS, TRUE, current_value);
            const int step = std::max(1, jsonInt(node, "step", 1));
            SendMessageW(slider, TBM_SETTICFREQ, step, 0);
            ControlBinding binding;
            binding.type = type;
            binding.event_name = jsonString(node, "event", "slider-change");
            binding.node_id = jsonString(node, "id");
            binding.text = jsonString(node, "label");
            binding.value_text = std::to_string(current_value);
            binding.companion_hwnd = label_hwnd;
            registerNodeControl(slider, binding);
        }
        return {control_width, (cursor_y - y) + 32};
    }

    if (type == "progress") {
        const std::wstring label = utf8ToWide(jsonString(node, "label"));
        const int value = jsonInt(node, "value", 0);
        const int min_value = jsonInt(node, "min", 0);
        const int max_value = jsonInt(node, "max", 100);
        const int control_width = std::max(240, std::min(max_width, 420));
        int cursor_y = y;
        HWND header_hwnd = nullptr;
        if (!label.empty()) {
            std::wstring header = label + L"  " + utf8ToWide(std::to_string(value));
            const SIZE label_size = measureWrappedText(hdc, g_native.ui_font, header, control_width);
            header_hwnd = createChildControl(L"STATIC", header.c_str(), SS_LEFT, x, cursor_y, control_width, label_size.cy, parent, g_native.ui_font);
            cursor_y += static_cast<int>(label_size.cy + 8);
        }
        HWND progress = createChildControl(PROGRESS_CLASSW, L"", 0, x, cursor_y, control_width, 22, parent, g_native.ui_font);
        if (progress) {
            SendMessageW(progress, PBM_SETRANGE32, min_value, max_value);
            SendMessageW(progress, PBM_SETPOS, value, 0);
            ControlBinding binding;
            binding.type = type;
            binding.node_id = jsonString(node, "id");
            binding.text = jsonString(node, "label");
            binding.value_text = std::to_string(value);
            binding.companion_hwnd = header_hwnd;
            registerNodeControl(progress, binding);
        }
        return {control_width, (cursor_y - y) + 22};
    }

    if (type == "badge") {
        const std::wstring text = nodeLabel(node);
        MeasuredSize size = measureNode(hdc, node, max_width);
        HWND badge = createChildControl(L"STATIC", text.c_str(), SS_CENTER, x, y, size.width, size.height, parent, g_native.ui_font);
        if (badge) {
            ControlBinding binding;
            binding.type = type;
            binding.node_id = jsonString(node, "id");
            binding.text = jsonString(node, "text");
            registerNodeControl(badge, binding);
        }
        return size;
    }

    if (type == "card") {
        const int padding = 12;
        MeasuredSize measured = measureNode(hdc, node, max_width);
        std::wstring title = utf8ToWide(jsonString(node, "title"));
        HWND group = createChildControl(L"BUTTON", title.c_str(), BS_GROUPBOX, x, y, std::max(measured.width, 80), std::max(measured.height, 48), parent, g_native.ui_font);
        if (group) {
            attachContainerForwarding(group);
            ControlBinding binding;
            binding.type = type;
            binding.node_id = jsonString(node, "id");
            binding.text = jsonString(node, "title");
            registerNodeControl(group, binding);
            int cursor_y = padding + (title.empty() ? 0 : 22);
            if (!title.empty()) cursor_y += 4;
            layoutChildrenStack(group, hdc, childrenOf(node), padding, cursor_y, std::max(24, measured.width - (padding * 2)), 10);
        }
        return measured;
    }

    if (type == "divider") {
        HWND line = createChildControl(L"STATIC", L"", SS_ETCHEDHORZ, x, y + 3, std::max(24, max_width), 2, parent, nullptr);
        (void)line;
        return {std::max(24, max_width), 8};
    }

    const std::wstring text = L"[native pending] " + nodeLabel(node);
    MeasuredSize size = measureNode(hdc, node, max_width);
    createChildControl(L"STATIC", text.c_str(), SS_LEFT, x, y, std::max(40, max_width), size.height, parent, g_native.ui_font);
    return {std::max(40, max_width), size.height};
}

ordered_json snapshotSpec() {
    std::lock_guard<std::mutex> lock(g_native.mutex);
    return g_native.spec;
}

int clampScrollOffset(int pos, int client_extent, int content_extent) {
    return std::clamp(pos, 0, std::max(0, content_extent - client_extent));
}

RECT workAreaForRect(const RECT& rect) {
    MONITORINFO monitor_info{};
    monitor_info.cbSize = sizeof(monitor_info);
    const HMONITOR monitor = MonitorFromRect(&rect, MONITOR_DEFAULTTONEAREST);
    if (monitor && GetMonitorInfoW(monitor, &monitor_info)) {
        return monitor_info.rcWork;
    }

    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    return work;
}

RECT clampWindowRectToWorkArea(const RECT& rect) {
    RECT clamped = rect;
    const RECT work = workAreaForRect(rect);
    const int work_width = std::max(1, static_cast<int>(work.right - work.left));
    const int work_height = std::max(1, static_cast<int>(work.bottom - work.top));
    const int width = std::min(std::max(1, static_cast<int>(rect.right - rect.left)), work_width);
    const int height = std::min(std::max(1, static_cast<int>(rect.bottom - rect.top)), work_height);

    clamped.left = std::clamp(rect.left, work.left, work.right - width);
    clamped.top = std::clamp(rect.top, work.top, work.bottom - height);
    clamped.right = clamped.left + width;
    clamped.bottom = clamped.top + height;
    return clamped;
}

void syncNativeContentHostPosition() {
    if (!g_native.content_host || !IsWindow(g_native.content_host)) return;
    g_native.scroll_x = clampScrollOffset(g_native.scroll_x, g_native.client_width, g_native.content_width);
    g_native.scroll_y = clampScrollOffset(g_native.scroll_y, g_native.client_height, g_native.content_height);
    MoveWindow(g_native.content_host,
               -g_native.scroll_x,
               -g_native.scroll_y,
               std::max(1, g_native.content_width),
               std::max(1, g_native.content_height),
               TRUE);
}

void updateNativeRootScrollbars(HWND hwnd) {
    g_native.scroll_x = clampScrollOffset(g_native.scroll_x, g_native.client_width, g_native.content_width);
    g_native.scroll_y = clampScrollOffset(g_native.scroll_y, g_native.client_height, g_native.content_height);

    ShowScrollBar(hwnd, SB_HORZ, g_native.content_width > g_native.client_width);
    ShowScrollBar(hwnd, SB_VERT, g_native.content_height > g_native.client_height);

    SCROLLINFO hsi{};
    hsi.cbSize = sizeof(hsi);
    hsi.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    hsi.nMin = 0;
    hsi.nMax = std::max(0, g_native.content_width - 1);
    hsi.nPage = static_cast<UINT>(std::max(0, g_native.client_width));
    hsi.nPos = g_native.scroll_x;
    SetScrollInfo(hwnd, SB_HORZ, &hsi, TRUE);

    SCROLLINFO vsi{};
    vsi.cbSize = sizeof(vsi);
    vsi.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    vsi.nMin = 0;
    vsi.nMax = std::max(0, g_native.content_height - 1);
    vsi.nPage = static_cast<UINT>(std::max(0, g_native.client_height));
    vsi.nPos = g_native.scroll_y;
    SetScrollInfo(hwnd, SB_VERT, &vsi, TRUE);

    syncNativeContentHostPosition();
}

void refreshNativeScrollMetrics(HWND hwnd) {
    RECT client{};
    GetClientRect(hwnd, &client);
    const int initial_raw_client_width = std::max(1, static_cast<int>(client.right - client.left));
    const int initial_raw_client_height = std::max(1, static_cast<int>(client.bottom - client.top));
    refreshNativeViewport(hwnd);

    const int layout_width = std::max(120, g_native.client_width - (kRootPadding * 2));
    ordered_json spec = snapshotSpec();
    if (!spec.is_object()) spec = ordered_json::object();

    if (ordered_json* status_bar_spec = findWindowStatusBarSpec(spec)) {
        applyStatusBarParts(g_native.status_bar_hwnd, *status_bar_spec, std::max(1, g_native.raw_client_width));
        refreshNativeViewport(hwnd);
    }

    HDC hdc = GetDC(g_native.content_host && IsWindow(g_native.content_host) ? g_native.content_host : hwnd);
    if (!hdc) return;
    ensureFonts();
    SelectObject(hdc, g_native.ui_font);

    MeasuredSize measured{};
    const ordered_json* body = jsonObjectChild(spec, "body");
    if (body) {
        measured = measureNode(hdc, *body, layout_width);
    } else {
        measured = {layout_width, 24};
    }
    ReleaseDC(g_native.content_host && IsWindow(g_native.content_host) ? g_native.content_host : hwnd, hdc);

    g_native.content_width = std::max(g_native.client_width, measured.width + (kRootPadding * 2));
    g_native.content_height = std::max(g_native.client_height, measured.height + (kRootPadding * 2));
    updateNativeRootScrollbars(hwnd);

    RECT final_client{};
    GetClientRect(hwnd, &final_client);
    const int final_raw_client_width = std::max(1, static_cast<int>(final_client.right - final_client.left));
    const int final_raw_client_height = std::max(1, static_cast<int>(final_client.bottom - final_client.top));
    if (final_raw_client_width != initial_raw_client_width || final_raw_client_height != initial_raw_client_height) {
        rebuildNativeWindow(hwnd);
    }
}

bool handleRootScrollBar(HWND hwnd, int bar, WPARAM wparam) {
    SCROLLINFO si{};
    si.cbSize = sizeof(si);
    si.fMask = SIF_ALL;
    if (!GetScrollInfo(hwnd, bar, &si)) return false;

    int next = si.nPos;
    switch (LOWORD(wparam)) {
    case SB_LINELEFT:
        next -= 32;
        break;
    case SB_LINERIGHT:
        next += 32;
        break;
    case SB_PAGELEFT:
        next -= static_cast<int>(si.nPage);
        break;
    case SB_PAGERIGHT:
        next += static_cast<int>(si.nPage);
        break;
    case SB_THUMBPOSITION:
    case SB_THUMBTRACK:
        next = si.nTrackPos;
        break;
    case SB_TOP:
        next = 0;
        break;
    case SB_BOTTOM:
        next = si.nMax;
        break;
    default:
        return false;
    }

    const int client_extent = bar == SB_HORZ ? g_native.client_width : g_native.client_height;
    const int content_extent = bar == SB_HORZ ? g_native.content_width : g_native.content_height;
    next = clampScrollOffset(next, client_extent, content_extent);

    if (bar == SB_HORZ) {
        if (next == g_native.scroll_x) return true;
        g_native.scroll_x = next;
    } else {
        if (next == g_native.scroll_y) return true;
        g_native.scroll_y = next;
    }
    updateNativeRootScrollbars(hwnd);
    return true;
}

ordered_json* findTopLevelMenuBarNode(ordered_json& body) {
    if (!body.is_object()) return nullptr;
    if (jsonString(body, "type") == "menu-bar") return &body;
    auto children_it = body.find("children");
    if (children_it != body.end() && children_it->is_array()) {
        for (auto& child : *children_it) {
            if (child.is_object() && jsonString(child, "type") == "menu-bar") {
                return &child;
            }
        }
    }
    return nullptr;
}

ordered_json* findWindowMenuBarSpec(ordered_json& spec) {
    if (!spec.is_object()) return nullptr;
    auto menu_it = spec.find("menuBar");
    if (menu_it != spec.end() && menu_it->is_object()) {
        return &(*menu_it);
    }
    auto body_it = spec.find("body");
    if (body_it != spec.end() && body_it->is_object()) {
        return findTopLevelMenuBarNode(*body_it);
    }
    return nullptr;
}

ordered_json* findWindowStatusBarSpec(ordered_json& spec) {
    if (!spec.is_object()) return nullptr;
    auto status_it = spec.find("statusBar");
    if (status_it != spec.end() && status_it->is_object()) {
        return &(*status_it);
    }
    return nullptr;
}

ordered_json* findWindowCommandBarSpec(ordered_json& spec) {
    if (!spec.is_object()) return nullptr;
    auto command_it = spec.find("commandBar");
    if (command_it != spec.end() && command_it->is_object()) {
        return &(*command_it);
    }
    return nullptr;
}

void rebuildCombinedCommandBindings() {
    g_native.command_bindings = g_native.menu_command_bindings;
    for (const auto& [command_id, binding] : g_native.command_bar_bindings) {
        g_native.command_bindings[command_id] = binding;
    }
}

void removeMenuBarNode(ordered_json& body) {
    if (!body.is_object()) return;
    if (jsonString(body, "type") == "menu-bar") {
        body = ordered_json::object({{"type", "stack"}, {"children", ordered_json::array()}});
        return;
    }
    auto children_it = body.find("children");
    if (children_it != body.end() && children_it->is_array()) {
        auto& children = *children_it;
        children.erase(
            std::remove_if(
                children.begin(),
                children.end(),
                [](const ordered_json& child) {
                    return child.is_object() && jsonString(child, "type") == "menu-bar";
                }),
            children.end());
    }
}

void applyStatusBarParts(HWND status_bar, const ordered_json& spec, int client_width) {
    if (!status_bar || !IsWindow(status_bar)) return;

    const ordered_json* parts = jsonArrayChild(spec, "parts");
    if (!parts || parts->empty()) {
        int single_part_right = -1;
        SendMessageW(status_bar, SB_SETPARTS, 1, reinterpret_cast<LPARAM>(&single_part_right));
        const std::wstring text = utf8ToWide(jsonString(spec, "text", ""));
        SendMessageW(status_bar, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(text.c_str()));
        return;
    }

    const int part_count = static_cast<int>(parts->size());
    std::vector<int> widths(static_cast<size_t>(part_count), 0);
    std::vector<int> right_edges(static_cast<size_t>(part_count), -1);
    int total_fixed_width = 0;
    int flexible_count = 0;

    for (int index = 0; index < part_count; ++index) {
        const ordered_json& part = (*parts)[static_cast<size_t>(index)];
        const int requested_width = std::max(0, jsonInt(part, "width", 0));
        widths[static_cast<size_t>(index)] = requested_width;
        if (requested_width > 0) {
            total_fixed_width += requested_width;
        } else {
            ++flexible_count;
        }
    }

    const int remaining_width = std::max(0, client_width - total_fixed_width);
    const int flexible_width = flexible_count > 0 ? (remaining_width / flexible_count) : 0;
    int flexible_remainder = flexible_count > 0 ? (remaining_width % flexible_count) : 0;
    int cursor = 0;
    for (int index = 0; index < part_count; ++index) {
        int width = widths[static_cast<size_t>(index)];
        if (width <= 0) {
            width = flexible_width;
            if (flexible_remainder > 0) {
                ++width;
                --flexible_remainder;
            }
        }
        cursor += std::max(0, width);
        right_edges[static_cast<size_t>(index)] = (index + 1 == part_count) ? -1 : cursor;
    }

    if (flexible_count == 0 && !right_edges.empty()) {
        right_edges.back() = -1;
    }

    SendMessageW(status_bar,
                 SB_SETPARTS,
                 static_cast<WPARAM>(part_count),
                 reinterpret_cast<LPARAM>(right_edges.data()));
    for (int index = 0; index < part_count; ++index) {
        const ordered_json& part = (*parts)[static_cast<size_t>(index)];
        const std::wstring text = utf8ToWide(jsonString(part, "text", ""));
        SendMessageW(status_bar,
                     SB_SETTEXTW,
                     static_cast<WPARAM>(index),
                     reinterpret_cast<LPARAM>(text.c_str()));
    }
}

int layoutNativeStatusBar(HWND hwnd) {
    if (!g_native.status_bar_hwnd || !IsWindow(g_native.status_bar_hwnd)) {
        g_native.status_bar_height = 0;
        return 0;
    }

    SendMessageW(g_native.status_bar_hwnd, WM_SIZE, 0, 0);
    RECT status_rect{};
    GetWindowRect(g_native.status_bar_hwnd, &status_rect);
    const int status_height = std::max(0, static_cast<int>(status_rect.bottom - status_rect.top));
    SetWindowPos(g_native.status_bar_hwnd,
                 HWND_TOP,
                 0,
                 std::max(0, g_native.raw_client_height - status_height),
                 std::max(1, g_native.raw_client_width),
                 std::max(1, status_height),
                 SWP_NOACTIVATE);
    g_native.status_bar_height = status_height;
    return status_height;
}

int layoutNativeCommandBar(HWND hwnd) {
    if (!g_native.command_bar_hwnd || !IsWindow(g_native.command_bar_hwnd)) {
        g_native.command_bar_height = 0;
        return 0;
    }

    SendMessageW(g_native.command_bar_hwnd, TB_AUTOSIZE, 0, 0);
    RECT command_rect{};
    GetWindowRect(g_native.command_bar_hwnd, &command_rect);
    const int command_height = std::max(0, static_cast<int>(command_rect.bottom - command_rect.top));
    SetWindowPos(g_native.command_bar_hwnd,
                 HWND_TOP,
                 0,
                 0,
                 std::max(1, g_native.raw_client_width),
                 std::max(1, command_height),
                 SWP_NOACTIVATE);
    g_native.command_bar_height = command_height;
    return command_height;
}

void refreshNativeViewport(HWND hwnd) {
    RECT client{};
    GetClientRect(hwnd, &client);
    g_native.raw_client_width = std::max(1, static_cast<int>(client.right - client.left));
    g_native.raw_client_height = std::max(1, static_cast<int>(client.bottom - client.top));

    const int command_height = layoutNativeCommandBar(hwnd);
    const int status_height = layoutNativeStatusBar(hwnd);
    g_native.client_width = g_native.raw_client_width;
    g_native.client_height = std::max(1, g_native.raw_client_height - command_height - status_height);

    if (g_native.viewport_host && IsWindow(g_native.viewport_host)) {
        SetWindowPos(g_native.viewport_host,
                     nullptr,
                     0,
                     command_height,
                     std::max(1, g_native.client_width),
                     std::max(1, g_native.client_height),
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

bool buildMenuItemTree(const ordered_json& menu_node, MenuBuildResult& result, HMENU menu);

HWND nativeMenuOwnerWindow(HWND hwnd) {
    if (g_native.embedded_mode && g_native.parent_hwnd && IsWindow(g_native.parent_hwnd)) {
        return g_native.parent_hwnd;
    }
    return hwnd;
}

bool tryHandleNativeMenuCommand(const ControlBinding& binding) {
    const std::string prefix = "format-selection:";
    if (binding.value_text.rfind(prefix, 0) != 0) {
        return false;
    }

    HWND rich_text = g_native.focused_rich_text_hwnd;
    if (!rich_text || !IsWindow(rich_text)) {
        g_native.focused_rich_text_hwnd = nullptr;
        return true;
    }

    auto it = g_native.bindings.find(rich_text);
    if (it == g_native.bindings.end() || it->second.type != "rich-text") {
        g_native.focused_rich_text_hwnd = nullptr;
        return true;
    }

    const std::string command = binding.value_text.substr(prefix.size());
    if (applyRichEditFormatSelectionCommand(rich_text, command)) {
        dispatchRichTextChangeEvent(rich_text, it->second);
    }
    return true;
}

void dispatchCommandBindingEvent(const ControlBinding& binding) {
    if (tryHandleNativeMenuCommand(binding)) {
        return;
    }
    ordered_json event = ordered_json::object();
    event["type"] = "ui-event";
    event["event"] = binding.event_name;
    event["source"] = "native-win32";
    if (!binding.node_id.empty()) event["id"] = binding.node_id;
    if (!binding.text.empty()) event["text"] = binding.text;
    if (!binding.value_text.empty()) event["value"] = binding.value_text;
    dispatchUiEventJson(event);
}

void applyCommandBarItems(HWND command_bar, const ordered_json& spec) {
    if (!command_bar || !IsWindow(command_bar)) return;

    SendMessageW(command_bar, TB_BUTTONSTRUCTSIZE, static_cast<WPARAM>(sizeof(TBBUTTON)), 0);
    SendMessageW(command_bar, TB_SETEXTENDEDSTYLE, 0, TBSTYLE_EX_MIXEDBUTTONS);
    SendMessageW(command_bar, TB_SETMAXTEXTROWS, 1, 0);
    while (SendMessageW(command_bar, TB_BUTTONCOUNT, 0, 0) > 0) {
        SendMessageW(command_bar, TB_DELETEBUTTON, 0, 0);
    }

    std::unordered_map<int, ControlBinding> command_bar_bindings;
    const ordered_json* items = jsonArrayChild(spec, "items");
    if (items && !items->empty()) {
        std::vector<TBBUTTON> buttons;
        std::vector<std::wstring> button_texts;
        buttons.reserve(items->size());
        button_texts.reserve(items->size());

        for (const auto& item : *items) {
            if (!item.is_object()) continue;

            TBBUTTON button{};
            if (item.contains("separator") && isTruthyJson(item["separator"])) {
                button.fsStyle = BTNS_SEP;
                button.iBitmap = 8;
                buttons.push_back(button);
                continue;
            }

            button_texts.push_back(utf8ToWide(jsonString(item, "text", jsonString(item, "label", "Command"))));
            const std::wstring& text = button_texts.back();
            const bool disabled = item.contains("disabled") && isTruthyJson(item["disabled"]);
            const bool checked = item.contains("checked") && isTruthyJson(item["checked"]);
            const int command_id = g_native.next_control_id++;

            button.idCommand = command_id;
            button.fsState = static_cast<BYTE>((disabled ? 0 : TBSTATE_ENABLED) | (checked ? TBSTATE_CHECKED : 0));
            button.fsStyle = static_cast<BYTE>((checked ? BTNS_CHECK : BTNS_BUTTON) | BTNS_AUTOSIZE | BTNS_SHOWTEXT);
            button.iBitmap = I_IMAGENONE;
            button.iString = reinterpret_cast<INT_PTR>(text.c_str());
            buttons.push_back(button);

            ControlBinding binding;
            binding.type = "command-bar-item";
            binding.node_id = jsonString(item, "id");
            binding.event_name = jsonString(item, "event", binding.node_id);
            binding.text = jsonString(item, "text", jsonString(item, "label", "Command"));
            binding.value_text = jsonString(item, "value");
            command_bar_bindings[command_id] = std::move(binding);
        }

        if (!buttons.empty()) {
            SendMessageW(command_bar,
                         TB_ADDBUTTONSW,
                         static_cast<WPARAM>(buttons.size()),
                         reinterpret_cast<LPARAM>(buttons.data()));
        }
    }

    g_native.command_bar_bindings = std::move(command_bar_bindings);
    rebuildCombinedCommandBindings();
    SendMessageW(command_bar, TB_AUTOSIZE, 0, 0);
    InvalidateRect(command_bar, nullptr, TRUE);
}

bool buildMenuBar(const ordered_json& menu_bar, MenuBuildResult& result) {
    result.menu = CreateMenu();
    if (!result.menu) return false;
    const ordered_json* menus = jsonArrayChild(menu_bar, "menus");
    if (!menus) {
        menus = jsonArrayChild(menu_bar, "children");
    }
    if (!menus) return true;
    for (const auto& item : *menus) {
        if (!item.is_object()) {
            result.ok = false;
            return false;
        }
        const std::wstring text = utf8ToWide(jsonString(item, "text", "Menu"));
        HMENU submenu = CreatePopupMenu();
        if (!submenu) {
            result.ok = false;
            return false;
        }
        if (!buildMenuItemTree(item, result, submenu)) {
            DestroyMenu(submenu);
            result.ok = false;
            return false;
        }
        if (!AppendMenuW(result.menu, MF_POPUP, reinterpret_cast<UINT_PTR>(submenu), text.c_str())) {
            DestroyMenu(submenu);
            result.ok = false;
            return false;
        }
    }
    return true;
}

bool showContextMenuForBinding(HWND owner, const ControlBinding& binding, POINT screen_pt) {
    if (binding.data.contains("disabled") && isTruthyJson(binding.data["disabled"])) return true;

    MenuBuildResult result;
    result.next_control_id = g_native.next_control_id;
    ordered_json menu_node = ordered_json::object();
    menu_node["items"] = binding.data.contains("items") ? binding.data["items"] : ordered_json::array();

    HMENU popup = CreatePopupMenu();
    if (!popup) return false;
    result.menu = popup;
    if (!buildMenuItemTree(menu_node, result, popup) || !result.ok) {
        DestroyMenu(popup);
        return false;
    }

    SetForegroundWindow(owner);
    const UINT flags = TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_TOPALIGN;
    const int command_id = static_cast<int>(TrackPopupMenu(popup, flags, screen_pt.x, screen_pt.y, 0, owner, nullptr));
    if (command_id != 0) {
        auto it = result.command_bindings.find(command_id);
        if (it != result.command_bindings.end() && !it->second.event_name.empty()) {
            dispatchCommandBindingEvent(it->second);
        }
    }
    DestroyMenu(popup);
    PostMessageW(owner, WM_NULL, 0, 0);
    return true;
}

bool buildMenuItemTree(const ordered_json& menu_node, MenuBuildResult& result, HMENU menu) {
    if (!menu) return false;
    const ordered_json* items = jsonArrayChild(menu_node, "items");
    if (!items) return true;
    for (const auto& item : *items) {
        if (!item.is_object()) {
            result.ok = false;
            return false;
        }
        if (item.contains("separator") && isTruthyJson(item["separator"])) {
            if (!AppendMenuW(menu, MF_SEPARATOR, 0, nullptr)) {
                result.ok = false;
                return false;
            }
            continue;
        }
        const std::wstring text = utf8ToWide(jsonString(item, "text", jsonString(item, "label", "Item")));
        const bool disabled = item.contains("disabled") && isTruthyJson(item["disabled"]);
        const bool checked = item.contains("checked") && isTruthyJson(item["checked"]);
        const ordered_json* nested_items = jsonArrayChild(item, "items");
        if (nested_items && !nested_items->empty()) {
            HMENU submenu = CreatePopupMenu();
            if (!submenu) {
                result.ok = false;
                return false;
            }
            if (!buildMenuItemTree(item, result, submenu)) {
                DestroyMenu(submenu);
                result.ok = false;
                return false;
            }
            const UINT flags = MF_POPUP | (disabled ? MF_GRAYED : 0) | (checked ? MF_CHECKED : 0);
            if (!AppendMenuW(menu, flags, reinterpret_cast<UINT_PTR>(submenu), text.c_str())) {
                DestroyMenu(submenu);
                result.ok = false;
                return false;
            }
            continue;
        }
        const int command_id = result.next_control_id++;
        const UINT flags = MF_STRING | (disabled ? MF_GRAYED : 0) | (checked ? MF_CHECKED : 0);
        if (!AppendMenuW(menu, flags, static_cast<UINT_PTR>(command_id), text.c_str())) {
            result.ok = false;
            return false;
        }
        ControlBinding binding;
        binding.type = "menu-item";
        binding.node_id = jsonString(item, "id");
        binding.event_name = jsonString(item, "event", binding.node_id);
        binding.text = jsonString(item, "text", jsonString(item, "label", "Item"));
        binding.value_text = jsonString(item, "value");
        result.command_bindings[command_id] = std::move(binding);
    }
    return true;
}

void installNativeMenuIfPresent(HWND hwnd, ordered_json& spec) {
    HMENU new_menu = nullptr;
    std::unordered_map<int, ControlBinding> new_command_bindings;
    int next_control_id = g_native.next_control_id;

    if (ordered_json* body = const_cast<ordered_json*>(jsonObjectChild(spec, "body"))) {
        ordered_json body_copy = *body;
        if (ordered_json* menu_bar = findWindowMenuBarSpec(spec)) {
            MenuBuildResult result;
            result.next_control_id = g_native.next_control_id;
            if (buildMenuBar(*menu_bar, result) && result.menu && result.ok) {
                new_menu = result.menu;
                new_command_bindings = std::move(result.command_bindings);
                next_control_id = result.next_control_id;
                if (findTopLevelMenuBarNode(body_copy)) {
                    removeMenuBarNode(body_copy);
                    spec["body"] = body_copy;
                }
            } else if (result.menu) {
                DestroyMenu(result.menu);
            }
        }
    }

    HMENU old_menu = g_native.current_menu;
    g_native.current_menu = new_menu;
    g_native.menu_command_bindings = std::move(new_command_bindings);
    g_native.next_control_id = next_control_id;
    rebuildCombinedCommandBindings();
    const HWND menu_owner = nativeMenuOwnerWindow(hwnd);
    SetMenu(menu_owner, new_menu);
    if (old_menu) {
        DestroyMenu(old_menu);
    }
    DrawMenuBar(menu_owner);
    if (g_native.embedded_mode && g_native.parent_hwnd && IsWindow(g_native.parent_hwnd) && hwnd && IsWindow(hwnd)) {
        RECT client{};
        if (GetClientRect(g_native.parent_hwnd, &client)) {
            SetWindowPos(hwnd,
                         HWND_TOP,
                         0,
                         0,
                         std::max(1, static_cast<int>(client.right - client.left)),
                         std::max(1, static_cast<int>(client.bottom - client.top)),
                         SWP_SHOWWINDOW);
        }
    }
}

void installNativeCommandBarIfPresent(HWND hwnd, ordered_json& spec) {
    g_native.command_bar_hwnd = nullptr;
    g_native.command_bar_height = 0;

    ordered_json* command_bar_spec = findWindowCommandBarSpec(spec);
    if (!command_bar_spec) {
        g_native.command_bar_bindings.clear();
        rebuildCombinedCommandBindings();
        return;
    }

    DWORD style = CCS_TOP | CCS_NOPARENTALIGN | CCS_NORESIZE | TBSTYLE_FLAT | TBSTYLE_LIST | TBSTYLE_TOOLTIPS;
    HWND command_bar = createChildControl(TOOLBARCLASSNAMEW,
                                          L"",
                                          style,
                                          0,
                                          0,
                                          std::max(1, g_native.raw_client_width),
                                          30,
                                          hwnd,
                                          g_native.ui_font);
    if (!command_bar) return;

    g_native.command_bar_hwnd = command_bar;
    applyCommandBarItems(command_bar, *command_bar_spec);
    layoutNativeCommandBar(hwnd);
}

void installNativeStatusBarIfPresent(HWND hwnd, ordered_json& spec) {
    g_native.status_bar_hwnd = nullptr;
    g_native.status_bar_height = 0;

    ordered_json* status_bar_spec = findWindowStatusBarSpec(spec);
    if (!status_bar_spec) return;

    DWORD style = CCS_BOTTOM | SBARS_TOOLTIPS;
    if (!g_native.embedded_mode) style |= SBARS_SIZEGRIP;

    HWND status_bar = createChildControl(STATUSCLASSNAMEW,
                                         L"",
                                         style,
                                         0,
                                         0,
                                         std::max(1, g_native.raw_client_width),
                                         24,
                                         hwnd,
                                         g_native.ui_font);
    if (!status_bar) return;

    g_native.status_bar_hwnd = status_bar;
    applyStatusBarParts(status_bar, *status_bar_spec, std::max(1, g_native.raw_client_width));
    layoutNativeStatusBar(hwnd);
}

void rebuildNativeWindow(HWND hwnd) {
    ensureFonts();
    clearNativeControls(hwnd);

    ordered_json spec = snapshotSpec();
    if (!spec.is_object()) spec = ordered_json::object();

    const std::wstring title = utf8ToWide(jsonString(spec, "title", "WinScheme Native UI"));
    SetWindowTextW(hwnd, title.empty() ? L"WinScheme Native UI" : title.c_str());
    installNativeMenuIfPresent(hwnd, spec);
    refreshNativeViewport(hwnd);
    installNativeCommandBarIfPresent(hwnd, spec);
    refreshNativeViewport(hwnd);
    installNativeStatusBarIfPresent(hwnd, spec);
    refreshNativeViewport(hwnd);

    const int layout_width = std::max(120, g_native.client_width - (kRootPadding * 2));

    HWND viewport_host = createChildControl(L"STATIC",
                                            L"",
                                            0,
                                            0,
                                            g_native.command_bar_height,
                                            std::max(1, g_native.client_width),
                                            std::max(1, g_native.client_height),
                                            hwnd,
                                            nullptr);
    if (viewport_host) attachContainerForwarding(viewport_host);
    g_native.viewport_host = viewport_host;

    HWND content_host = createChildControl(L"STATIC",
                                           L"",
                                           0,
                                           0,
                                           0,
                                           std::max(1, g_native.client_width),
                                           std::max(1, g_native.client_height),
                                           viewport_host ? viewport_host : hwnd,
                                           nullptr);
    if (content_host) attachContainerForwarding(content_host);
    g_native.content_host = content_host;
    layoutNativeStatusBar(hwnd);

    HDC hdc = GetDC(content_host ? content_host : (viewport_host ? viewport_host : hwnd));
    if (!hdc) return;
    ensureFonts();
    SelectObject(hdc, g_native.ui_font);

    const ordered_json* body = jsonObjectChild(spec, "body");
    if (body) {
        const MeasuredSize measured = measureNode(hdc, *body, layout_width);
        g_native.content_width = std::max(g_native.client_width, measured.width + (kRootPadding * 2));
        g_native.content_height = std::max(g_native.client_height, measured.height + (kRootPadding * 2));
        updateNativeRootScrollbars(hwnd);
        layoutNode(content_host ? content_host : hwnd, hdc, *body, kRootPadding, kRootPadding, layout_width);
    } else {
        g_native.content_width = g_native.client_width;
        g_native.content_height = g_native.client_height;
        updateNativeRootScrollbars(hwnd);
        createChildControl(
            L"STATIC",
            L"No native body spec has been published yet.",
            SS_LEFT,
            kRootPadding,
            kRootPadding,
            layout_width,
            24,
            content_host ? content_host : hwnd,
            g_native.ui_font);
    }

    ReleaseDC(content_host ? content_host : (viewport_host ? viewport_host : hwnd), hdc);
}

void dispatchNativeWindowMessage(UINT message) {
    HWND hwnd = nullptr;
    DWORD thread_id = 0;
    bool embedded_mode = false;
    {
        std::lock_guard<std::mutex> lock(g_native.mutex);
        hwnd = g_native.hwnd;
        thread_id = g_native.thread_id;
        embedded_mode = g_native.embedded_mode;
    }
    if (!hwnd) return;

    if (embedded_mode && thread_id == GetCurrentThreadId()) {
        SendMessageW(hwnd, message, 0, 0);
    } else {
        PostMessageW(hwnd, message, 0, 0);
    }
}

bool ensureNativeWindowClassRegistered() {
    WNDCLASSW wc{};
    wc.lpfnWndProc = nativeWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kNativeWindowClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    if (RegisterClassW(&wc) != 0) {
        return true;
    }
    return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

bool ensureScrollViewHostClassRegistered() {
    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kScrollViewHostClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    if (RegisterClassW(&wc) != 0) {
        return true;
    }
    return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

bool ensureNativeHostWindowAvailable() {
    {
        std::lock_guard<std::mutex> lock(g_native.mutex);
        if (g_native.hwnd) return true;
        if (g_native.embedded_mode) {
            setNativeLastError("Embedded native UI host is not attached.");
            return false;
        }
    }
    return ensureNativeThread();
}

bool findUserAppNodeById(ordered_json& node, const std::string& id, ordered_json** found) {
    if (!node.is_object()) return false;
    auto id_it = node.find("id");
    if (id_it != node.end() && id_it->is_string() && id_it->get<std::string>() == id) {
        *found = &node;
        return true;
    }
    auto children_it = node.find("children");
    if (children_it != node.end() && children_it->is_array()) {
        for (auto& child : *children_it) {
            if (findUserAppNodeById(child, id, found)) return true;
        }
    }
    auto body_it = node.find("body");
    if (body_it != node.end() && body_it->is_object()) {
        if (findUserAppNodeById(*body_it, id, found)) return true;
    }
    return false;
}

bool findTreeItemById(ordered_json& items,
                      const std::string& item_id,
                      ordered_json** found,
                      ordered_json** parent_array,
                      size_t* found_index) {
    if (!items.is_array()) return false;
    for (size_t i = 0; i < items.size(); ++i) {
        auto& item = items[i];
        if (!item.is_object()) continue;
        auto id_it = item.find("id");
        if (id_it != item.end() && id_it->is_string() && id_it->get<std::string>() == item_id) {
            if (found) *found = &item;
            if (parent_array) *parent_array = &items;
            if (found_index) *found_index = i;
            return true;
        }
        auto children_it = item.find("children");
        if (children_it != item.end() && children_it->is_array()) {
            if (findTreeItemById(*children_it, item_id, found, parent_array, found_index)) return true;
        }
    }
    return false;
}

void collectTreeItemIds(const ordered_json& item, std::unordered_set<std::string>& ids) {
    if (!item.is_object()) return;
    auto id_it = item.find("id");
    if (id_it != item.end() && id_it->is_string()) {
        ids.insert(id_it->get<std::string>());
    }
    auto children_it = item.find("children");
    if (children_it != item.end() && children_it->is_array()) {
        for (const auto& child : *children_it) {
            collectTreeItemIds(child, ids);
        }
    }
}

void pruneTreeStateForRemovedItem(ordered_json& tree_node, const ordered_json& removed_item) {
    std::unordered_set<std::string> removed_ids;
    collectTreeItemIds(removed_item, removed_ids);

    auto selected_it = tree_node.find("selectedId");
    if (selected_it != tree_node.end() && selected_it->is_string() &&
        removed_ids.find(selected_it->get<std::string>()) != removed_ids.end()) {
        tree_node["selectedId"] = "";
    }

    auto expanded_it = tree_node.find("expandedIds");
    if (expanded_it != tree_node.end() && expanded_it->is_array()) {
        ordered_json next = ordered_json::array();
        for (const auto& value : *expanded_it) {
            if (value.is_string() && removed_ids.find(value.get<std::string>()) == removed_ids.end()) {
                next.push_back(value.get<std::string>());
            }
        }
        tree_node["expandedIds"] = next;
    }
}

bool applyUserAppPatchOperation(ordered_json& spec, const ordered_json& op) {
    if (!op.is_object()) return false;
    const std::string op_name = jsonString(op, "op");
    if (op_name.empty()) return false;

    if (op_name == "set-window-props") {
        auto props_it = op.find("props");
        if (props_it == op.end() || !props_it->is_object()) return false;
        for (auto it = props_it->begin(); it != props_it->end(); ++it) {
            spec[it.key()] = it.value();
        }
        return true;
    }

    const std::string node_id = jsonString(op, "id");
    if (node_id.empty()) return false;

    ordered_json* target = nullptr;
    if (!findUserAppNodeById(spec, node_id, &target) || !target) return false;

    if (op_name == "set-node-props") {
        auto props_it = op.find("props");
        if (props_it == op.end() || !props_it->is_object()) return false;
        for (auto it = props_it->begin(); it != props_it->end(); ++it) {
            (*target)[it.key()] = it.value();
        }
        return true;
    }

    if (op_name == "tree-replace-items") {
        auto items_it = op.find("items");
        if (items_it == op.end() || !items_it->is_array()) return false;
        (*target)["items"] = *items_it;
        return true;
    }

    if (op_name == "tree-set-expanded-ids") {
        auto expanded_it = op.find("expandedIds");
        if (expanded_it == op.end() || !expanded_it->is_array()) return false;
        (*target)["expandedIds"] = *expanded_it;
        return true;
    }

    if (op_name == "tree-set-selected-id") {
        auto selected_it = op.find("selectedId");
        if (selected_it == op.end()) return false;
        (*target)["selectedId"] = *selected_it;
        return true;
    }

    if (jsonString(*target, "type") == "tree-view") {
        auto& items = (*target)["items"];
        if (!items.is_array()) items = ordered_json::array();

        if (op_name == "tree-insert-item") {
            auto item_it = op.find("item");
            if (item_it == op.end() || !item_it->is_object()) return false;
            std::string parent_id;
            auto parent_it = op.find("parentId");
            if (parent_it != op.end() && parent_it->is_string()) parent_id = parent_it->get<std::string>();
            ordered_json* destination = &items;
            if (!parent_id.empty()) {
                ordered_json* parent_item = nullptr;
                if (!findTreeItemById(items, parent_id, &parent_item, nullptr, nullptr) || !parent_item) return false;
                auto children_it = parent_item->find("children");
                if (children_it == parent_item->end() || !children_it->is_array()) {
                    (*parent_item)["children"] = ordered_json::array();
                }
                destination = &(*parent_item)["children"];
            }
            const int index = jsonInt(op, "index", static_cast<int>(destination->size()));
            const int insert_pos = std::clamp(index, 0, static_cast<int>(destination->size()));
            destination->insert(destination->begin() + static_cast<ordered_json::difference_type>(insert_pos), *item_it);
            return true;
        }

        if (op_name == "tree-remove-item") {
            auto item_id_it = op.find("itemId");
            if (item_id_it == op.end() || !item_id_it->is_string()) return false;
            ordered_json* parent_array = nullptr;
            size_t found_index = 0;
            ordered_json* item = nullptr;
            if (!findTreeItemById(items, item_id_it->get<std::string>(), &item, &parent_array, &found_index) ||
                !parent_array || !item) {
                return false;
            }
            const ordered_json removed_item = *item;
            parent_array->erase(parent_array->begin() + static_cast<ordered_json::difference_type>(found_index));
            pruneTreeStateForRemovedItem(*target, removed_item);
            return true;
        }

        if (op_name == "tree-move-item") {
            auto item_id_it = op.find("itemId");
            if (item_id_it == op.end() || !item_id_it->is_string()) return false;
            ordered_json* parent_array = nullptr;
            size_t found_index = 0;
            ordered_json* item = nullptr;
            if (!findTreeItemById(items, item_id_it->get<std::string>(), &item, &parent_array, &found_index) ||
                !parent_array || !item) {
                return false;
            }
            ordered_json moved_item = *item;
            parent_array->erase(parent_array->begin() + static_cast<ordered_json::difference_type>(found_index));

            std::string parent_id;
            auto parent_it = op.find("parentId");
            if (parent_it != op.end() && parent_it->is_string()) parent_id = parent_it->get<std::string>();
            ordered_json* destination = &items;
            if (!parent_id.empty()) {
                ordered_json* parent_item = nullptr;
                if (!findTreeItemById(items, parent_id, &parent_item, nullptr, nullptr) || !parent_item) return false;
                auto children_it = parent_item->find("children");
                if (children_it == parent_item->end() || !children_it->is_array()) {
                    (*parent_item)["children"] = ordered_json::array();
                }
                destination = &(*parent_item)["children"];
            }
            const int index = jsonInt(op, "index", static_cast<int>(destination->size()));
            const int insert_pos = std::clamp(index, 0, static_cast<int>(destination->size()));
            destination->insert(destination->begin() + static_cast<ordered_json::difference_type>(insert_pos), moved_item);
            return true;
        }

        if (op_name == "tree-set-item-props") {
            auto item_id_it = op.find("itemId");
            auto props_it = op.find("props");
            if (item_id_it == op.end() || !item_id_it->is_string() || props_it == op.end() || !props_it->is_object()) {
                return false;
            }
            ordered_json* item = nullptr;
            if (!findTreeItemById(items, item_id_it->get<std::string>(), &item, nullptr, nullptr) || !item) {
                return false;
            }
            for (auto it = props_it->begin(); it != props_it->end(); ++it) {
                (*item)[it.key()] = it.value();
            }
            return true;
        }
    }

    if (op_name == "replace-children") {
        auto children_it = op.find("children");
        if (children_it == op.end() || !children_it->is_array()) return false;
        (*target)["children"] = *children_it;
        return true;
    }

    auto& children = (*target)["children"];
    if (!children.is_array()) children = ordered_json::array();

    if (op_name == "append-child") {
        auto child_it = op.find("child");
        if (child_it == op.end() || !child_it->is_object()) return false;
        children.push_back(*child_it);
        return true;
    }

    if (op_name == "insert-child") {
        auto child_it = op.find("child");
        if (child_it == op.end() || !child_it->is_object()) return false;
        const int index = jsonInt(op, "index", static_cast<int>(children.size()));
        const int insert_pos = std::clamp(index, 0, static_cast<int>(children.size()));
        children.insert(children.begin() + static_cast<ordered_json::difference_type>(insert_pos), *child_it);
        return true;
    }

    if (op_name == "remove-child") {
        auto child_id_it = op.find("childId");
        if (child_id_it != op.end() && child_id_it->is_string()) {
            const std::string child_id = child_id_it->get<std::string>();
            for (auto it = children.begin(); it != children.end(); ++it) {
                auto id_it = it->find("id");
                if (id_it != it->end() && id_it->is_string() && id_it->get<std::string>() == child_id) {
                    children.erase(it);
                    return true;
                }
            }
            return false;
        }
        const int index = jsonInt(op, "index", 0);
        if (index < 0 || index >= static_cast<int>(children.size())) return false;
        children.erase(children.begin() + static_cast<ordered_json::difference_type>(index));
        return true;
    }

    if (op_name == "move-child") {
        auto child_id_it = op.find("childId");
        if (child_id_it == op.end() || !child_id_it->is_string()) return false;
        const std::string child_id = child_id_it->get<std::string>();
        const int index = jsonInt(op, "index", 0);
        auto found = children.end();
        for (auto it = children.begin(); it != children.end(); ++it) {
            auto id_it = it->find("id");
            if (id_it != it->end() && id_it->is_string() && id_it->get<std::string>() == child_id) {
                found = it;
                break;
            }
        }
        if (found == children.end()) return false;
        ordered_json moved = *found;
        children.erase(found);
        const int insert_pos = std::clamp(index, 0, static_cast<int>(children.size()));
        children.insert(children.begin() + static_cast<ordered_json::difference_type>(insert_pos), moved);
        return true;
    }

    return false;
}

bool applyUserAppPatchDocument(ordered_json& spec, const ordered_json& patch) {
    if (patch.is_array()) {
        for (const auto& op : patch) {
            if (!applyUserAppPatchOperation(spec, op)) return false;
        }
        return true;
    }

    if (patch.is_object()) {
        auto ops_it = patch.find("ops");
        if (ops_it != patch.end() && ops_it->is_array()) {
            return applyUserAppPatchDocument(spec, *ops_it);
        }
        return applyUserAppPatchOperation(spec, patch);
    }

    return false;
}

bool setUserAppNodeStringProp(ordered_json& spec, const std::string& node_id, const char* key, const std::string& value) {
    if (node_id.empty()) return false;
    ordered_json* target = nullptr;
    if (!findUserAppNodeById(spec, node_id, &target) || !target) return false;
    (*target)[key] = value;
    return true;
}

bool setUserAppNodeProp(ordered_json& spec, const std::string& node_id, const char* key, const ordered_json& value) {
    if (node_id.empty()) return false;
    ordered_json* target = nullptr;
    if (!findUserAppNodeById(spec, node_id, &target) || !target) return false;
    (*target)[key] = value;
    return true;
}

LRESULT CALLBACK nativeWndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    NativeUiSessionImpl* session = lookupNativeSessionForWindow(hwnd);
    if (message == WM_NCCREATE) {
        CREATESTRUCTW* create_struct = reinterpret_cast<CREATESTRUCTW*>(lparam);
        session = create_struct && create_struct->lpCreateParams
            ? static_cast<NativeUiSessionImpl*>(create_struct->lpCreateParams)
            : activeNativeSession();
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(session));
        registerNativeSessionWindow(session, hwnd);
        return TRUE;
    }
    ScopedNativeSession scoped(session);
    switch (message) {
    case WM_CREATE:
        {
            std::lock_guard<std::mutex> lock(g_native.mutex);
            g_native.hwnd = hwnd;
        }
        rebuildNativeWindow(hwnd);
        return 0;

    case WM_GETMINMAXINFO:
        {
            MINMAXINFO* info = reinterpret_cast<MINMAXINFO*>(lparam);
            if (!info) return 0;
            RECT window_rect{};
            if (!GetWindowRect(hwnd, &window_rect)) {
                window_rect = RECT{0, 0, kDefaultWindowWidth, kDefaultWindowHeight};
            }
            const RECT work = workAreaForRect(window_rect);
            info->ptMaxPosition.x = work.left;
            info->ptMaxPosition.y = work.top;
            info->ptMaxSize.x = std::max(1, static_cast<int>(work.right - work.left));
            info->ptMaxSize.y = std::max(1, static_cast<int>(work.bottom - work.top));
            info->ptMaxTrackSize.x = info->ptMaxSize.x;
            info->ptMaxTrackSize.y = info->ptMaxSize.y;
            return 0;
        }

    case WM_SIZE:
        {
            const int width = std::max(0, static_cast<int>(LOWORD(lparam)));
            const int height = std::max(0, static_cast<int>(HIWORD(lparam)));
            bool size_unchanged = false;
            {
                std::lock_guard<std::mutex> lock(g_native.mutex);
                size_unchanged = g_native.content_host &&
                    width == g_native.raw_client_width &&
                    height == g_native.raw_client_height;
            }
            if (size_unchanged) {
                return 0;
            }
        }
        // Debounce: defer the rebuild until the user has stopped dragging
        // the window frame for kResizeDebounceMs milliseconds.  Without this,
        // every pixel of resize triggers a full control destroy+recreate cycle
        // (typically hundreds of times during a single drag).
        KillTimer(hwnd, kResizeTimerId);
        SetTimer(hwnd, kResizeTimerId, kResizeDebounceMs, nullptr);
        return 0;

    case WM_TIMER:
        if (wparam == kResizeTimerId) {
            KillTimer(hwnd, kResizeTimerId);
            {
                RECT client{};
                GetClientRect(hwnd, &client);
                const int width = std::max(1, static_cast<int>(client.right - client.left));
                const int height = std::max(1, static_cast<int>(client.bottom - client.top));
                if (g_native.content_host &&
                    width == g_native.raw_client_width &&
                    height == g_native.raw_client_height) {
                    traceNativePatchEvent("resize-timer skip-rebuild current-size-already-laid-out");
                    refreshNativeViewport(hwnd);
                    return 0;
                }
            }
            rebuildNativeWindow(hwnd);
            return 0;
        }
        break;

    case kMsgNativeReload:
        traceNativePatchEvent("reload-window");
        rebuildNativeWindow(hwnd);
        return 0;

    case kMsgNativePatch:
        {
            ordered_json patch = nullptr;
            {
                std::lock_guard<std::mutex> lock(g_native.mutex);
                patch = g_native.pending_patch;
                g_native.pending_patch = nullptr;
            }
            g_native.suppress_events = true;
            traceNativePatchEvent(std::string("patch-dispatch ops=") + std::to_string(patchOperationCount(patch)));
            const bool patched = applyNativePatchDocument(hwnd, patch);
            g_native.suppress_events = false;
            if (!patched) {
                g_native.patch_failed_count.fetch_add(1, std::memory_order_relaxed);
                g_native.patch_window_rebuild_count.fetch_add(1, std::memory_order_relaxed);
                traceNativePatchEvent("patch-document failed -> window rebuild");
                rebuildNativeWindow(hwnd);
            } else {
                traceNativePatchEvent("patch-document applied");
                refreshNativeScrollMetrics(hwnd);
            }
        }
        return 0;

    case kMsgNativeShow:
        if (g_native.embedded_mode) {
            ShowWindow(hwnd, SW_SHOW);
        } else {
            rebuildNativeWindow(hwnd);
            ShowWindow(hwnd, SW_SHOW);
            SetForegroundWindow(hwnd);
        }
        return 0;

    case WM_CONTEXTMENU:
        if (g_native.suppress_events) return 0;
        {
            HWND target = reinterpret_cast<HWND>(wparam);
            if (!target) target = GetFocus();
            ControlBinding* context_binding = nullptr;
            HWND current = target;
            while (current) {
                auto it = g_native.bindings.find(current);
                if (it != g_native.bindings.end() && it->second.type == "context-menu") {
                    context_binding = &it->second;
                    break;
                }
                if (current == hwnd) break;
                current = GetParent(current);
            }
            if (context_binding) {
                POINT screen_pt{};
                screen_pt.x = static_cast<int>(static_cast<short>(LOWORD(lparam)));
                screen_pt.y = static_cast<int>(static_cast<short>(HIWORD(lparam)));
                if (screen_pt.x == -1 && screen_pt.y == -1) {
                    if (!GetCursorPos(&screen_pt)) {
                        RECT rc{};
                        GetWindowRect(target ? target : hwnd, &rc);
                        screen_pt.x = rc.left + ((rc.right - rc.left) / 2);
                        screen_pt.y = rc.top + ((rc.bottom - rc.top) / 2);
                    }
                }
                if (showContextMenuForBinding(hwnd, *context_binding, screen_pt)) {
                    return 0;
                }
            }
        }
        break;

    case WM_COMMAND:
        if (g_native.suppress_events) return 0;
        if (lparam == 0 || reinterpret_cast<HWND>(lparam) == g_native.command_bar_hwnd) {
            const int command_id = LOWORD(wparam);
            auto command_it = g_native.command_bindings.find(command_id);
            if (command_it != g_native.command_bindings.end()) {
                dispatchCommandBindingEvent(command_it->second);
                return 0;
            }
        }
        if (lparam != 0) {
            HWND control = reinterpret_cast<HWND>(lparam);
            auto it = g_native.bindings.find(control);
            if (it != g_native.bindings.end()) {
                ControlBinding& binding = it->second;
                if (HIWORD(wparam) == BN_CLICKED &&
                    (binding.type == "button" || binding.type == "checkbox" || binding.type == "switch")) {
                    if (binding.type == "button") {
                        dispatchUiEvent(binding);
                    } else {
                        const bool checked = SendMessageW(control, BM_GETCHECK, 0, 0) == BST_CHECKED;
                        ordered_json event = ordered_json::object();
                        event["type"] = "ui-event";
                        event["event"] = binding.event_name;
                        event["source"] = "native-win32";
                        if (!binding.node_id.empty()) event["id"] = binding.node_id;
                        if (!binding.text.empty()) event["text"] = binding.text;
                        event["checked"] = checked;
                        event["value"] = checked;
                        dispatchUiEventJson(event);
                    }
                    return 0;
                }

                if (HIWORD(wparam) == EN_CHANGE &&
                    (binding.type == "input" || binding.type == "textarea" || binding.type == "rich-text" || binding.type == "number-input")) {
                    const std::wstring value = controlWindowText(control);
                    ordered_json event = ordered_json::object();
                    event["type"] = "ui-event";
                    event["event"] = binding.event_name;
                    event["source"] = "native-win32";
                    if (!binding.node_id.empty()) event["id"] = binding.node_id;
                    if (!binding.text.empty()) event["text"] = binding.text;
                    if (binding.type == "rich-text") {
                        binding.value_text = richEditGetRtf(control);
                        event["value"] = binding.value_text;
                    } else if (binding.type == "number-input") {
                        const std::string utf8_value = wideToUtf8(value);
                        ordered_json parsed = nullptr;
                        if (tryParseUtf8Number(utf8_value, parsed)) {
                            event["value"] = parsed;
                        } else {
                            event["value"] = utf8_value;
                        }
                        event["rawValue"] = utf8_value;
                        binding.value_text = utf8_value;
                    } else {
                        binding.value_text = wideToUtf8(binding.multiline ? normalizeLineEndings(value) : value);
                        event["value"] = binding.value_text;
                    }
                    dispatchUiEventJson(event);
                    return 0;
                }

                if ((HIWORD(wparam) == EN_SETFOCUS || HIWORD(wparam) == EN_KILLFOCUS) &&
                    (binding.type == "input" || binding.type == "textarea" || binding.type == "rich-text" || binding.type == "number-input")) {
                    if (binding.type == "rich-text") {
                        if (HIWORD(wparam) == EN_SETFOCUS) {
                            g_native.focused_rich_text_hwnd = control;
                        } else if (g_native.focused_rich_text_hwnd == control) {
                            g_native.focused_rich_text_hwnd = nullptr;
                        }
                    }
                    dispatchControlFocusEvent(binding, HIWORD(wparam) == EN_SETFOCUS);
                    return 0;
                }

                if (HIWORD(wparam) == CBN_SELCHANGE && binding.type == "select") {
                    const LRESULT index = SendMessageW(control, CB_GETCURSEL, 0, 0);
                    std::string selected_value;
                    if (index >= 0 && index < static_cast<LRESULT>(binding.option_values.size())) {
                        selected_value = binding.option_values[static_cast<size_t>(index)];
                    }
                    ordered_json event = ordered_json::object();
                    event["type"] = "ui-event";
                    event["event"] = binding.event_name;
                    event["source"] = "native-win32";
                    if (!binding.node_id.empty()) event["id"] = binding.node_id;
                    if (!binding.text.empty()) event["text"] = binding.text;
                    event["value"] = selected_value;
                    dispatchUiEventJson(event);
                    return 0;
                }

                if (HIWORD(wparam) == LBN_SELCHANGE && binding.type == "list-box") {
                    const LRESULT index = SendMessageW(control, LB_GETCURSEL, 0, 0);
                    std::string selected_value;
                    if (index >= 0 && index < static_cast<LRESULT>(binding.option_values.size())) {
                        selected_value = binding.option_values[static_cast<size_t>(index)];
                    }
                    ordered_json event = ordered_json::object();
                    event["type"] = "ui-event";
                    event["event"] = binding.event_name;
                    event["source"] = "native-win32";
                    if (!binding.node_id.empty()) event["id"] = binding.node_id;
                    if (!binding.text.empty()) event["text"] = binding.text;
                    event["value"] = selected_value;
                    dispatchUiEventJson(event);
                    return 0;
                }

                if (HIWORD(wparam) == BN_CLICKED && binding.type == "radio-group") {
                    ordered_json event = ordered_json::object();
                    event["type"] = "ui-event";
                    event["event"] = binding.event_name;
                    event["source"] = "native-win32";
                    if (!binding.node_id.empty()) event["id"] = binding.node_id;
                    if (!binding.text.empty()) event["text"] = binding.text;
                    event["value"] = binding.value_text;
                    dispatchUiEventJson(event);
                    return 0;
                }
            }
        }
        break;

    case WM_HSCROLL:
        if (g_native.suppress_events) return 0;
        if (lparam == 0 && handleRootScrollBar(hwnd, SB_HORZ, wparam)) {
            return 0;
        }
        if (lparam != 0) {
            HWND control = reinterpret_cast<HWND>(lparam);
            auto it = g_native.bindings.find(control);
            if (it != g_native.bindings.end() && it->second.type == "slider") {
                const int code = LOWORD(wparam);
                if (code == TB_THUMBPOSITION || code == TB_ENDTRACK || code == SB_ENDSCROLL || code == TB_LINEUP ||
                    code == TB_LINEDOWN || code == TB_PAGEUP || code == TB_PAGEDOWN) {
                    const int value = static_cast<int>(SendMessageW(control, TBM_GETPOS, 0, 0));
                    ordered_json event = ordered_json::object();
                    event["type"] = "ui-event";
                    event["event"] = it->second.event_name;
                    event["source"] = "native-win32";
                    if (!it->second.node_id.empty()) event["id"] = it->second.node_id;
                    if (!it->second.text.empty()) event["text"] = it->second.text;
                    event["value"] = value;
                    dispatchUiEventJson(event);
                    return 0;
                }
            }
        }
        break;

    case WM_VSCROLL:
        if (g_native.suppress_events) return 0;
        if (lparam == 0 && handleRootScrollBar(hwnd, SB_VERT, wparam)) {
            return 0;
        }
        break;

    case WM_MOUSEWHEEL:
        if (g_native.suppress_events) return 0;
        {
            const int delta = GET_WHEEL_DELTA_WPARAM(wparam);
            POINT screen_pt{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            HWND hovered = WindowFromPoint(screen_pt);
            HWND scroll_view = findBoundAncestorOfType(hovered, "scroll-view");
            if (scroll_view) {
                auto it = g_native.bindings.find(scroll_view);
                if (it != g_native.bindings.end()) {
                    UINT lines = 3;
                    SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
                    int delta_pixels = 0;
                    if (lines == WHEEL_PAGESCROLL) {
                        delta_pixels = (delta > 0 ? -1 : 1) * std::max(24, jsonInt(it->second.data, "viewportHeight", 1) - 48);
                    } else {
                        const int steps = delta / WHEEL_DELTA;
                        delta_pixels = -steps * static_cast<int>(std::max<UINT>(1, lines)) * 32;
                    }
                    scrollScrollViewByDelta(scroll_view, it->second, delta_pixels);
                    return 0;
                }
            }
            UINT lines = 3;
            SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
            if (lines == WHEEL_PAGESCROLL) {
                const int direction = delta > 0 ? -1 : 1;
                g_native.scroll_y += direction * std::max(24, g_native.client_height - 48);
            } else {
                const int steps = delta / WHEEL_DELTA;
                g_native.scroll_y -= steps * static_cast<int>(std::max<UINT>(1, lines)) * 32;
            }
            updateNativeRootScrollbars(hwnd);
            return 0;
        }

    case WM_MOUSEHWHEEL:
        if (g_native.suppress_events) return 0;
        {
            const int delta = GET_WHEEL_DELTA_WPARAM(wparam);
            const int steps = delta / WHEEL_DELTA;
            g_native.scroll_x -= steps * 48;
            updateNativeRootScrollbars(hwnd);
            return 0;
        }

    case WM_NOTIFY:
        if (g_native.suppress_events) return 0;
        if (lparam != 0) {
            const NMHDR* hdr = reinterpret_cast<const NMHDR*>(lparam);
            auto it = g_native.bindings.find(hdr->hwndFrom);
            if (it != g_native.bindings.end()) {
                ControlBinding& binding = it->second;
                if ((hdr->code == NM_CLICK || hdr->code == NM_RETURN) && binding.type == "link") {
                    ordered_json event = ordered_json::object();
                    event["type"] = "ui-event";
                    event["event"] = binding.event_name;
                    event["source"] = "native-win32";
                    if (!binding.node_id.empty()) event["id"] = binding.node_id;
                    if (!binding.text.empty()) event["text"] = binding.text;
                    if (!binding.value_text.empty()) {
                        event["href"] = binding.value_text;
                        event["value"] = binding.value_text;
                    }
                    dispatchUiEventJson(event);
                    return 0;
                }
                if (hdr->code == DTN_DATETIMECHANGE && binding.type == "date-picker") {
                    const NMDATETIMECHANGE* info = reinterpret_cast<const NMDATETIMECHANGE*>(lparam);
                    std::string selected_value;
                    if (info->dwFlags == GDT_VALID) {
                        selected_value = formatIsoDateString(info->st);
                    }
                    ordered_json event = ordered_json::object();
                    event["type"] = "ui-event";
                    event["event"] = binding.event_name;
                    event["source"] = "native-win32";
                    if (!binding.node_id.empty()) event["id"] = binding.node_id;
                    if (!binding.text.empty()) event["text"] = binding.text;
                    event["value"] = selected_value;
                    dispatchUiEventJson(event);
                    {
                        std::lock_guard<std::mutex> lock(g_native.mutex);
                        setUserAppNodeStringProp(g_native.spec, binding.node_id, "value", selected_value);
                    }
                    return 0;
                }
                if (hdr->code == DTN_DATETIMECHANGE && binding.type == "time-picker") {
                    const NMDATETIMECHANGE* info = reinterpret_cast<const NMDATETIMECHANGE*>(lparam);
                    std::string selected_value;
                    if (info->dwFlags == GDT_VALID) {
                        selected_value = formatIsoTimeString(info->st);
                    }
                    ordered_json event = ordered_json::object();
                    event["type"] = "ui-event";
                    event["event"] = binding.event_name;
                    event["source"] = "native-win32";
                    if (!binding.node_id.empty()) event["id"] = binding.node_id;
                    if (!binding.text.empty()) event["text"] = binding.text;
                    event["value"] = selected_value;
                    dispatchUiEventJson(event);
                    {
                        std::lock_guard<std::mutex> lock(g_native.mutex);
                        setUserAppNodeStringProp(g_native.spec, binding.node_id, "value", selected_value);
                    }
                    return 0;
                }
                if (hdr->code == TCN_SELCHANGE && binding.type == "tabs") {
                    const int index = TabCtrl_GetCurSel(hdr->hwndFrom);
                    std::string selected_value;
                    if (index >= 0 && index < static_cast<int>(binding.option_values.size())) {
                        selected_value = binding.option_values[static_cast<size_t>(index)];
                    }
                    ordered_json event = ordered_json::object();
                    event["type"] = "ui-event";
                    event["event"] = binding.event_name;
                    event["source"] = "native-win32";
                    if (!binding.node_id.empty()) event["id"] = binding.node_id;
                    if (!binding.text.empty()) event["text"] = binding.text;
                    event["value"] = selected_value;
                    dispatchUiEventJson(event);
                    {
                        std::lock_guard<std::mutex> lock(g_native.mutex);
                        setUserAppNodeStringProp(g_native.spec, binding.node_id, "value", selected_value);
                    }
                    return 0;
                }
                if (hdr->code == TVN_SELCHANGEDW && binding.type == "tree-view") {
                    const NMTREEVIEWW* info = reinterpret_cast<const NMTREEVIEWW*>(lparam);
                    if (info->itemNew.hItem) {
                        TVITEMW item{};
                        item.mask = TVIF_PARAM;
                        item.hItem = info->itemNew.hItem;
                        if (TreeView_GetItem(hdr->hwndFrom, &item) &&
                            item.lParam >= 0 &&
                            static_cast<size_t>(item.lParam) < binding.option_values.size() &&
                            static_cast<size_t>(item.lParam) < binding.option_nodes.size()) {
                            const size_t index = static_cast<size_t>(item.lParam);
                            const std::string& item_id = binding.option_values[index];
                            const ordered_json& item_node = binding.option_nodes[index];
                            ordered_json event = ordered_json::object();
                            event["type"] = "ui-event";
                            event["event"] = binding.event_name;
                            event["source"] = "native-win32";
                            if (!binding.node_id.empty()) event["id"] = binding.node_id;
                            if (!binding.text.empty()) event["text"] = binding.text;
                            event["value"] = item_id;
                            event["itemId"] = item_id;
                            event["selectedId"] = item_id;
                            event["item"] = item_node;
                            auto tag_it = item_node.find("tag");
                            if (tag_it != item_node.end()) event["tag"] = *tag_it;
                            dispatchUiEventJson(event);
                            binding.value_text = item_id;
                            {
                                std::lock_guard<std::mutex> lock(g_native.mutex);
                                setUserAppNodeStringProp(g_native.spec, binding.node_id, "selectedId", item_id);
                            }
                            return 0;
                        }
                    }
                }
                if (hdr->code == TVN_ITEMEXPANDEDW && binding.type == "tree-view") {
                    const NMTREEVIEWW* info = reinterpret_cast<const NMTREEVIEWW*>(lparam);
                    if (info->itemNew.hItem) {
                        TVITEMW item{};
                        item.mask = TVIF_PARAM;
                        item.hItem = info->itemNew.hItem;
                        if (TreeView_GetItem(hdr->hwndFrom, &item) &&
                            item.lParam >= 0 &&
                            static_cast<size_t>(item.lParam) < binding.option_values.size() &&
                            static_cast<size_t>(item.lParam) < binding.option_nodes.size()) {
                            const size_t index = static_cast<size_t>(item.lParam);
                            const ordered_json expanded_ids = collectExpandedTreeIds(hdr->hwndFrom, binding);
                            binding.data["expandedIds"] = expanded_ids;
                            {
                                std::lock_guard<std::mutex> lock(g_native.mutex);
                                setUserAppNodeProp(g_native.spec, binding.node_id, "expandedIds", expanded_ids);
                            }
                            const std::string toggle_event = binding.data.value("toggleEvent", std::string("tree-toggle"));
                            if (!toggle_event.empty()) {
                                ordered_json event = ordered_json::object();
                                event["type"] = "ui-event";
                                event["event"] = toggle_event;
                                event["source"] = "native-win32";
                                if (!binding.node_id.empty()) event["id"] = binding.node_id;
                                if (!binding.text.empty()) event["text"] = binding.text;
                                event["itemId"] = binding.option_values[index];
                                event["expanded"] = (info->itemNew.state & TVIS_EXPANDED) != 0;
                                event["expandedIds"] = expanded_ids;
                                event["item"] = binding.option_nodes[index];
                                auto tag_it = binding.option_nodes[index].find("tag");
                                if (tag_it != binding.option_nodes[index].end()) event["tag"] = *tag_it;
                                dispatchUiEventJson(event);
                            }
                            return 0;
                        }
                    }
                }
                if ((hdr->code == NM_DBLCLK || hdr->code == NM_RETURN) && binding.type == "tree-view") {
                    const std::string activate_event = binding.data.value("activateEvent", std::string("tree-activate"));
                    if (!activate_event.empty()) {
                        HTREEITEM selected = TreeView_GetSelection(hdr->hwndFrom);
                        if (selected) {
                            TVITEMW item{};
                            item.mask = TVIF_PARAM;
                            item.hItem = selected;
                            if (TreeView_GetItem(hdr->hwndFrom, &item) &&
                                item.lParam >= 0 &&
                                static_cast<size_t>(item.lParam) < binding.option_values.size() &&
                                static_cast<size_t>(item.lParam) < binding.option_nodes.size()) {
                                const size_t index = static_cast<size_t>(item.lParam);
                                ordered_json event = ordered_json::object();
                                event["type"] = "ui-event";
                                event["event"] = activate_event;
                                event["source"] = "native-win32";
                                if (!binding.node_id.empty()) event["id"] = binding.node_id;
                                if (!binding.text.empty()) event["text"] = binding.text;
                                event["value"] = binding.option_values[index];
                                event["itemId"] = binding.option_values[index];
                                event["selectedId"] = binding.option_values[index];
                                event["item"] = binding.option_nodes[index];
                                auto tag_it = binding.option_nodes[index].find("tag");
                                if (tag_it != binding.option_nodes[index].end()) event["tag"] = *tag_it;
                                dispatchUiEventJson(event);
                                return 0;
                            }
                        }
                    }
                }
                if (hdr->code == LVN_ITEMCHANGED && binding.type == "table") {
                    const NMLISTVIEW* info = reinterpret_cast<const NMLISTVIEW*>(lparam);
                    const bool became_selected =
                        (info->uNewState & LVIS_SELECTED) != 0 &&
                        (info->uOldState & LVIS_SELECTED) == 0;
                    if (became_selected && info->iItem >= 0 &&
                        info->iItem < static_cast<int>(binding.option_values.size()) &&
                        info->iItem < static_cast<int>(binding.row_objects.size())) {
                        const std::string& row_id = binding.option_values[static_cast<size_t>(info->iItem)];
                        ordered_json event = ordered_json::object();
                        event["type"] = "ui-event";
                        event["event"] = binding.event_name;
                        event["source"] = "native-win32";
                        if (!binding.node_id.empty()) event["id"] = binding.node_id;
                        if (!binding.text.empty()) event["text"] = binding.text;
                        event["value"] = row_id;
                        event["rowId"] = row_id;
                        event["row"] = binding.row_objects[static_cast<size_t>(info->iItem)];
                        dispatchUiEventJson(event);
                        {
                            std::lock_guard<std::mutex> lock(g_native.mutex);
                            setUserAppNodeStringProp(g_native.spec, binding.node_id, "selectedId", row_id);
                        }
                        return 0;
                    }
                }
            }
        }
        break;

    case WM_DPICHANGED: {
        // Fired when the window is moved to a monitor with a different DPI
        // or when display settings change.  wParam high word = new Y DPI.
        // lParam = OS-suggested window rect at the new scale.
        const int new_dpi = static_cast<int>(HIWORD(wparam));
        recreateFonts(new_dpi);
        // Cancel any pending resize-debounce rebuild; we will rebuild
        // immediately below at the correct size and DPI.
        KillTimer(hwnd, kResizeTimerId);
        const RECT* suggested = reinterpret_cast<const RECT*>(lparam);
        const RECT clamped = suggested
            ? clampWindowRectToWorkArea(*suggested)
            : clampWindowRectToWorkArea(RECT{0, 0, kDefaultWindowWidth, kDefaultWindowHeight});
        SetWindowPos(hwnd, nullptr,
                     clamped.left, clamped.top,
                     clamped.right  - clamped.left,
                     clamped.bottom - clamped.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        rebuildNativeWindow(hwnd);
        return 0;
    }

    case WM_CLOSE:
        ShowWindow(hwnd, SW_HIDE);
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, kResizeTimerId);
        clearNativeMenu(hwnd);
        {
            std::lock_guard<std::mutex> lock(g_native.mutex);
            g_native.hwnd = nullptr;
            g_native.ready = false;
            if (g_native.embedded_mode) {
                g_native.running = false;
                g_native.parent_hwnd = nullptr;
            }
        }
        return 0;

    case WM_NCDESTROY:
        unregisterNativeSessionWindow(hwnd);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

void nativeHostThreadMain(NativeUiSessionImpl* session) {
    ScopedNativeSession scoped(session);
    // --- Visual styles -------------------------------------------------
    // Activate a ComCtl32 v6 activation context for this thread so that
    // every control created here uses themed, modern rendering.  The
    // context is kept active for the full lifetime of the message loop.
    HANDLE hActCtx = createComCtlV6ActivationContext();
    ULONG_PTR actctx_cookie = 0;
    if (hActCtx != INVALID_HANDLE_VALUE) {
        ActivateActCtx(hActCtx, &actctx_cookie);
    }

    // --- Per-Monitor DPI v2 -------------------------------------------
    // Opt this thread's window into per-monitor DPI awareness (Win 10+).
    // Falls back silently on older builds — the process-level
    // SetProcessDPIAware() set in main.cpp still provides system-dpi
    // awareness as a floor.
    using SetThreadDpiCtxFn = HANDLE(WINAPI*)(HANDLE);
    if (auto fn = reinterpret_cast<SetThreadDpiCtxFn>(
            GetProcAddress(GetModuleHandleW(L"user32.dll"),
                           "SetThreadDpiAwarenessContext"))) {
        // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 = (HANDLE)-4
        fn(reinterpret_cast<HANDLE>(static_cast<intptr_t>(-4)));
    }

    initCommonControlsOnce();
    ensureFonts();
    if (!ensureNativeWindowClassRegistered()) {
        {
            std::lock_guard<std::mutex> lock(g_native.mutex);
            g_native.running = false;
            g_native.ready = false;
            g_native.hwnd = nullptr;
        }
        g_native.cv.notify_all();
        if (actctx_cookie) DeactivateActCtx(0, actctx_cookie);
        if (hActCtx != INVALID_HANDLE_VALUE) ReleaseActCtx(hActCtx);
        return;
    }

    const DWORD window_style = WS_OVERLAPPEDWINDOW | WS_VSCROLL | WS_HSCROLL;
    const RECT initial_rect = [&]() {
        RECT rect{0, 0, kDefaultWindowWidth, kDefaultWindowHeight};
        AdjustWindowRectEx(&rect, window_style, FALSE, 0);
        rect = clampWindowRectToWorkArea(rect);
        const RECT work = workAreaForRect(rect);
        const int width = static_cast<int>(rect.right - rect.left);
        const int height = static_cast<int>(rect.bottom - rect.top);
        const int work_width = static_cast<int>(work.right - work.left);
        const int work_height = static_cast<int>(work.bottom - work.top);
        rect.left = work.left + std::max(0, (work_width - width) / 2);
        rect.top = work.top + std::max(0, (work_height - height) / 2);
        rect.right = rect.left + width;
        rect.bottom = rect.top + height;
        return rect;
    }();

    HWND hwnd = CreateWindowExW(
        0,
        kNativeWindowClassName,
        L"WinScheme Native UI",
        window_style,
        initial_rect.left,
        initial_rect.top,
        initial_rect.right - initial_rect.left,
        initial_rect.bottom - initial_rect.top,
        nullptr,
        nullptr,
        GetModuleHandleW(nullptr),
        activeNativeSession());

    {
        std::lock_guard<std::mutex> lock(g_native.mutex);
        g_native.thread_id = GetCurrentThreadId();
        g_native.hwnd = hwnd;
        g_native.ready = hwnd != nullptr;
        g_native.running = true;
    }
    g_native.cv.notify_all();

    if (hwnd) {
        // Recreate fonts at the window's actual DPI now that we have an HWND.
        // On a 96-DPI primary monitor this is a no-op; on a high-DPI monitor
        // or when the window spawns on a secondary monitor it picks up the
        // correct scale from the start.
        const int initial_dpi = dpiForWindow(hwnd);
        if (initial_dpi != g_native.current_dpi) {
            recreateFonts(initial_dpi);
        }
        ShowWindow(hwnd, SW_HIDE);
        UpdateWindow(hwnd);
    }

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    {
        std::lock_guard<std::mutex> lock(g_native.mutex);
        g_native.running = false;
        g_native.ready = false;
        g_native.hwnd = nullptr;
    }
    g_native.cv.notify_all();

    // Release the visual-styles activation context now that the message
    // loop has exited and no more windows will be created on this thread.
    if (actctx_cookie) DeactivateActCtx(0, actctx_cookie);
    if (hActCtx != INVALID_HANDLE_VALUE) ReleaseActCtx(hActCtx);
}

bool ensureNativeThread() {
    std::unique_lock<std::mutex> lock(g_native.mutex);
    if (g_native.running && g_native.hwnd) return true;
    if (!g_native.thread_started) {
        NativeUiSessionImpl* session = activeNativeSession();
        std::thread thread(nativeHostThreadMain, session);
        thread.detach();
        g_native.thread_started = true;
    }
    g_native.cv.wait(lock, []() { return g_native.ready || g_native.running; });
    return g_native.hwnd != nullptr;
}

bool hostBridgeAvailable() {
    return true;
}

bool attachEmbeddedNativeHost(const WinguiNativeEmbeddedHostDesc& desc) {
    if (!desc.parent_hwnd) {
        setNativeLastError("Embedded native UI parent HWND is required.");
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(g_native.mutex);
        if (g_native.hwnd || g_native.running) {
            setNativeLastError("Native UI host is already active.");
            return false;
        }
    }

    initCommonControlsOnce();
    ensureFonts();
    if (!ensureNativeWindowClassRegistered()) {
        setNativeLastError("Native UI window class registration failed.");
        return false;
    }

    const DWORD window_style = WS_CHILD | WS_VSCROLL | WS_HSCROLL |
        WS_CLIPCHILDREN | WS_CLIPSIBLINGS | (desc.visible ? WS_VISIBLE : 0u);
    HWND hwnd = CreateWindowExW(
        0,
        kNativeWindowClassName,
        L"WinScheme Native UI Embedded",
        window_style,
        desc.x,
        desc.y,
        desc.width,
        desc.height,
        static_cast<HWND>(desc.parent_hwnd),
        nullptr,
        GetModuleHandleW(nullptr),
        activeNativeSession());
    if (!hwnd) {
        setNativeLastError("Embedded native UI host window creation failed.");
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(g_native.mutex);
        g_native.thread_id = GetCurrentThreadId();
        g_native.hwnd = hwnd;
        g_native.parent_hwnd = static_cast<HWND>(desc.parent_hwnd);
        g_native.ready = true;
        g_native.running = true;
        g_native.embedded_mode = true;
    }

    if (!desc.visible) {
        ShowWindow(hwnd, SW_HIDE);
    }
    return true;
}

bool executeNativePublishJson(const char* utf8) {
    if (!utf8) {
        setNativeLastError("Native UI spec JSON is required.");
        return false;
    }
    ordered_json spec = ordered_json::parse(utf8, nullptr, false);
    if (spec.is_discarded() || !spec.is_object()) {
        setNativeLastError("Native UI spec JSON must be a JSON object.");
        return false;
    }
    std::string validation_error;
    if (!validateUserAppSpec(spec, validation_error)) {
        setNativeLastError(validation_error);
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(g_native.mutex);
        g_native.spec = std::move(spec);
    }
    g_native.publish_count.fetch_add(1, std::memory_order_relaxed);
    traceNativePatchEvent(std::string("publish bytes=") + std::to_string(std::strlen(utf8)));
    if (!ensureNativeHostWindowAvailable()) return false;
    dispatchNativeWindowMessage(kMsgNativeReload);
    return true;
}

bool executeNativePatchJson(const char* utf8) {
    if (!utf8) {
        setNativeLastError("Native UI patch JSON is required.");
        return false;
    }
    ordered_json patch = ordered_json::parse(utf8, nullptr, false);
    if (patch.is_discarded()) {
        setNativeLastError("Native UI patch JSON must be valid JSON.");
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(g_native.mutex);
        if (!g_native.spec.is_object()) {
            setNativeLastError("No published native UI spec is available for patching.");
            return false;
        }
        ordered_json next_spec = g_native.spec;
        if (!applyUserAppPatchDocument(next_spec, patch)) {
            setNativeLastError("Native UI patch could not be applied to the current spec.");
            return false;
        }
        std::string validation_error;
        if (!validateUserAppSpec(next_spec, validation_error)) {
            setNativeLastError(validation_error);
            return false;
        }
        g_native.spec = std::move(next_spec);
        g_native.pending_patch = patch;
    }
    g_native.patch_request_count.fetch_add(1, std::memory_order_relaxed);
    traceNativePatchEvent(std::string("patch-request bytes=") + std::to_string(std::strlen(utf8)) +
                          " ops=" + std::to_string(patchOperationCount(patch)));
    if (!ensureNativeHostWindowAvailable()) return false;
    dispatchNativeWindowMessage(kMsgNativePatch);
    return true;
}

bool executeNativeHostRun() {
    if (!ensureNativeHostWindowAvailable()) return false;
    dispatchNativeWindowMessage(kMsgNativeShow);
    return true;
}

const char* jsonTypeName(const ordered_json& value) {
    if (value.is_object()) return "object";
    if (value.is_array()) return "array";
    if (value.is_string()) return "string";
    if (value.is_boolean()) return "boolean";
    if (value.is_number()) return "number";
    if (value.is_null()) return "null";
    return "value";
}

bool validateUserAppNode(const ordered_json& node, const std::string& path, std::string& error);

bool validateUserAppObjectArray(const ordered_json& value,
                                const std::string& path,
                                const char* field_name,
                                std::string& error) {
    if (!value.is_array()) {
        error = "Invalid native-ui form at " + path + "/" + field_name +
                ": expected array, got " + jsonTypeName(value) + ".";
        return false;
    }
    for (size_t i = 0; i < value.size(); ++i) {
        const auto& child = value[i];
        if (!child.is_object()) {
                error = "Invalid native-ui form at " + path + "/" + field_name + "/" + std::to_string(i) +
                    ": expected object, got " + jsonTypeName(child) + ".";
            return false;
        }
    }
    return true;
}

bool validateTreeItems(const ordered_json& value, const std::string& path, std::string& error) {
    if (!value.is_array()) {
        error = "Invalid native-ui form at " + path +
                ": expected array, got " + jsonTypeName(value) + ".";
        return false;
    }
    for (size_t i = 0; i < value.size(); ++i) {
        const auto& item = value[i];
        if (!item.is_object()) {
                error = "Invalid native-ui form at " + path + "/" + std::to_string(i) +
                    ": expected object, got " + jsonTypeName(item) + ".";
            return false;
        }
        auto children_it = item.find("children");
        if (children_it != item.end()) {
            if (!validateTreeItems(*children_it, path + "/" + std::to_string(i) + "/children", error)) {
                return false;
            }
        }
    }
    return true;
}

bool validateUserAppNode(const ordered_json& node, const std::string& path, std::string& error) {
    if (!node.is_object()) {
        error = "Invalid native-ui form at " + path + ": expected object, got " + std::string(jsonTypeName(node)) + ".";
        return false;
    }

    auto type_it = node.find("type");
    if (type_it == node.end() || !type_it->is_string() || type_it->get<std::string>().empty()) {
        error = "Invalid native-ui form at " + path + ": missing string 'type'.";
        return false;
    }
    const std::string type = type_it->get<std::string>();

    auto id_it = node.find("id");
    if (id_it != node.end() && !id_it->is_string()) {
        error = "Invalid native-ui form at " + path + "/id: expected string, got " + std::string(jsonTypeName(*id_it)) + ".";
        return false;
    }

    auto body_it = node.find("body");
    if (body_it != node.end()) {
        if (!body_it->is_object()) {
            error = "Invalid native-ui form at " + path + "/body: expected object, got " + std::string(jsonTypeName(*body_it)) + ".";
            return false;
        }
        if (!validateUserAppNode(*body_it, path + "/body", error)) return false;
    } else if (type == "window") {
        error = "Invalid native-ui form at " + path + ": window is missing 'body'.";
        return false;
    }

    auto children_it = node.find("children");
    if (children_it != node.end()) {
        if (!children_it->is_array()) {
            error = "Invalid native-ui form at " + path + "/children: expected array, got " + std::string(jsonTypeName(*children_it)) + ".";
            return false;
        }
        for (size_t i = 0; i < children_it->size(); ++i) {
            const auto& child = (*children_it)[i];
            if (!child.is_object()) {
                error = "Invalid native-ui form at " + path + "/children/" + std::to_string(i) +
                        ": expected node object, got " + jsonTypeName(child) + ".";
                return false;
            }
            if (!validateUserAppNode(child, path + "/children/" + std::to_string(i), error)) return false;
        }
    }

    if ((type == "select" || type == "list-box" || type == "radio-group") && node.contains("options")) {
        if (!validateUserAppObjectArray(node["options"], path, "options", error)) return false;
    }

    if (type == "tabs" && node.contains("tabs")) {
        if (!validateUserAppObjectArray(node["tabs"], path, "tabs", error)) return false;
        for (size_t i = 0; i < node["tabs"].size(); ++i) {
            const auto& tab = node["tabs"][i];
            auto content_it = tab.find("content");
            if (content_it != tab.end()) {
                if (!content_it->is_object()) {
                        error = "Invalid native-ui form at " + path + "/tabs/" + std::to_string(i) +
                            "/content: expected object, got " + jsonTypeName(*content_it) + ".";
                    return false;
                }
                if (!validateUserAppNode(*content_it, path + "/tabs/" + std::to_string(i) + "/content", error)) return false;
            }
        }
    }

    if (type == "table") {
        if (node.contains("columns") && !validateUserAppObjectArray(node["columns"], path, "columns", error)) return false;
        if (node.contains("rows") && !validateUserAppObjectArray(node["rows"], path, "rows", error)) return false;
    }

    if (type == "tree-view") {
        if (node.contains("items") && !validateTreeItems(node["items"], path + "/items", error)) return false;
        if (node.contains("expandedIds") && !node["expandedIds"].is_array()) {
            error = "Invalid native-ui form at " + path + "/expandedIds: expected array, got " +
                jsonTypeName(node["expandedIds"]) + ".";
            return false;
        }
    }

    if (type == "split-view") {
        auto children_it = node.find("children");
        if (children_it == node.end() || !children_it->is_array() || children_it->size() != 2) {
            error = "Invalid native-ui form at " + path + ": split-view requires exactly two split-pane children.";
            return false;
        }
        for (size_t i = 0; i < children_it->size(); ++i) {
            const auto& child = (*children_it)[i];
            if (!child.is_object() || jsonString(child, "type") != "split-pane") {
                error = "Invalid native-ui form at " + path + "/children/" + std::to_string(i) +
                        ": expected split-pane child.";
                return false;
            }
        }
    }

    if (type == "context-menu") {
        if (node.contains("items") && !validateUserAppObjectArray(node["items"], path, "items", error)) return false;
    }

    if (type == "canvas" && node.contains("commands")) {
        if (!validateUserAppObjectArray(node["commands"], path, "commands", error)) return false;
    }

    return true;
}

bool validateUserAppSpec(const ordered_json& spec, std::string& error) {
    return validateUserAppNode(spec, "window", error);
}

NativeUiSessionImpl* requireNativeSession(WinguiNativeUiSession* session) {
    return session ? reinterpret_cast<NativeUiSessionImpl*>(session) : defaultNativeSession();
}

WinguiNativeUiSession* defaultNativeSessionHandle() {
    return reinterpret_cast<WinguiNativeUiSession*>(defaultNativeSession());
}

} // namespace

extern "C" WINGUI_API WinguiNativeUiSession* WINGUI_CALL wingui_native_session_create(void) {
    clearNativeLastError();
    auto* session = new (std::nothrow) NativeUiSessionImpl();
    if (!session) {
        setNativeLastError("Failed to allocate native UI session.");
    }
    return reinterpret_cast<WinguiNativeUiSession*>(session);
}

extern "C" WINGUI_API void WINGUI_CALL wingui_native_session_destroy(WinguiNativeUiSession* session) {
    NativeUiSessionImpl* native_session = requireNativeSession(session);
    if (!session || native_session == defaultNativeSession()) return;
    {
        ScopedNativeSession scoped(native_session);
        HWND hwnd = nullptr;
        {
            std::lock_guard<std::mutex> lock(g_native.mutex);
            hwnd = g_native.hwnd;
        }
        if (hwnd && IsWindow(hwnd)) {
            DestroyWindow(hwnd);
        }
        if (native_session->state.event_handle) {
            CloseHandle(native_session->state.event_handle);
            native_session->state.event_handle = nullptr;
        }
        if (native_session->state.current_menu) {
            DestroyMenu(native_session->state.current_menu);
            native_session->state.current_menu = nullptr;
        }
        if (native_session->state.ui_font) {
            DeleteObject(native_session->state.ui_font);
            native_session->state.ui_font = nullptr;
        }
        if (native_session->state.heading_font) {
            DeleteObject(native_session->state.heading_font);
            native_session->state.heading_font = nullptr;
        }
    }
    delete native_session;
}

extern "C" WINGUI_API void WINGUI_CALL wingui_native_session_set_callbacks(
    WinguiNativeUiSession* session,
    const WinguiNativeCallbacks* callbacks) {
    ScopedNativeSession scoped(requireNativeSession(session));
    g_native_callbacks = callbacks ? *callbacks : WinguiNativeCallbacks{};
}

extern "C" WINGUI_API int64_t WINGUI_CALL wingui_native_session_available(WinguiNativeUiSession* session) {
    ScopedNativeSession scoped(requireNativeSession(session));
    return hostBridgeAvailable() ? 1 : 0;
}

extern "C" WINGUI_API const char* WINGUI_CALL wingui_native_session_backend_info(WinguiNativeUiSession* session) {
    ScopedNativeSession scoped(requireNativeSession(session));
    g_backend_info = "backend=wingui-native-win32;status=fast-patch-p1;dispatch=";
    g_backend_info += g_native_callbacks.dispatch_event_json ? "callback+queue" : "queue";
    return g_backend_info.c_str();
}

extern "C" WINGUI_API const char* WINGUI_CALL wingui_native_session_last_error_utf8(WinguiNativeUiSession* session) {
    ScopedNativeSession scoped(requireNativeSession(session));
    return nativeLastErrorUtf8();
}

extern "C" WINGUI_API int64_t WINGUI_CALL wingui_native_session_enqueue_command(
    WinguiNativeUiSession* session,
    const WinguiNativeCommand* command) {
    ScopedNativeSession scoped(requireNativeSession(session));
    clearNativeLastError();
    return enqueueNativeCommandInternal(command) ? 1 : 0;
}

extern "C" WINGUI_API uint32_t WINGUI_CALL wingui_native_session_drain_command_queue(
    WinguiNativeUiSession* session,
    uint32_t max_commands) {
    ScopedNativeSession scoped(requireNativeSession(session));
    clearNativeLastError();
    uint32_t drained = 0;
    for (;;) {
        NativeQueuedCommand command;
        {
            std::lock_guard<std::mutex> lock(g_native.command_mutex);
            if (g_native.command_queue.empty()) break;
            if (max_commands != 0 && drained >= max_commands) break;
            command = std::move(g_native.command_queue.front());
            g_native.command_queue.pop_front();
        }
        if (!executeNativeCommand(command)) {
            break;
        }
        ++drained;
    }
    return drained;
}

extern "C" WINGUI_API int64_t WINGUI_CALL wingui_native_session_poll_event(
    WinguiNativeUiSession* session,
    WinguiNativeEvent* out_event) {
    ScopedNativeSession scoped(requireNativeSession(session));
    clearNativeLastError();
    if (!out_event) {
        setNativeLastError("Native UI event output is required.");
        return 0;
    }
    NativeQueuedEvent queued;
    {
        std::lock_guard<std::mutex> lock(g_native.event_mutex);
        if (g_native.event_queue.empty()) {
            std::memset(out_event, 0, sizeof(*out_event));
            syncReactiveEventSignalLocked();
            return 0;
        }
        queued = std::move(g_native.event_queue.front());
        g_native.event_queue.pop_front();
        syncReactiveEventSignalLocked();
    }
    return copyReactiveEventToApi(queued, out_event) ? 1 : 0;
}

extern "C" WINGUI_API void* WINGUI_CALL wingui_native_session_event_handle(WinguiNativeUiSession* session) {
    ScopedNativeSession scoped(requireNativeSession(session));
    std::lock_guard<std::mutex> lock(g_native.event_mutex);
    return ensureReactiveEventHandleLocked();
}

extern "C" WINGUI_API void WINGUI_CALL wingui_native_session_release_event(
    WinguiNativeUiSession* session,
    WinguiNativeEvent* event) {
    ScopedNativeSession scoped(requireNativeSession(session));
    if (!event) return;
    if (event->payload_utf8) {
        std::free(event->payload_utf8);
    }
    std::memset(event, 0, sizeof(*event));
}

extern "C" WINGUI_API int64_t WINGUI_CALL wingui_native_session_publish_json(
    WinguiNativeUiSession* session,
    const char* utf8) {
    ScopedNativeSession scoped(requireNativeSession(session));
    clearNativeLastError();
    return executeNativePublishJson(utf8) ? 1 : 0;
}

extern "C" WINGUI_API int64_t WINGUI_CALL wingui_native_session_patch_json(
    WinguiNativeUiSession* session,
    const char* utf8) {
    ScopedNativeSession scoped(requireNativeSession(session));
    clearNativeLastError();
    return executeNativePatchJson(utf8) ? 1 : 0;
}

extern "C" WINGUI_API int64_t WINGUI_CALL wingui_native_session_host_run(WinguiNativeUiSession* session) {
    ScopedNativeSession scoped(requireNativeSession(session));
    clearNativeLastError();
    return executeNativeHostRun() ? 1 : 0;
}

extern "C" WINGUI_API int64_t WINGUI_CALL wingui_native_session_begin_embedded(
    WinguiNativeUiSession* session,
    const WinguiNativeEmbeddedSessionDesc* desc) {
    ScopedNativeSession scoped(requireNativeSession(session));
    clearNativeLastError();
    if (!desc) {
        setNativeLastError("Embedded native UI session descriptor is required.");
        return 0;
    }

    wingui_native_session_set_callbacks(session, &desc->callbacks);
    if (!attachEmbeddedNativeHost(desc->host)) {
        return 0;
    }

    if (desc->initial_ui_json_utf8 && desc->initial_ui_json_utf8[0] != '\0') {
        if (!executeNativePublishJson(desc->initial_ui_json_utf8)) {
            wingui_native_session_detach_embedded_host(session);
            return 0;
        }
    }

    return 1;
}

extern "C" WINGUI_API int64_t WINGUI_CALL wingui_native_session_attach_embedded_host(
    WinguiNativeUiSession* session,
    const WinguiNativeEmbeddedHostDesc* desc) {
    ScopedNativeSession scoped(requireNativeSession(session));
    clearNativeLastError();
    if (!desc) {
        setNativeLastError("Embedded native UI host descriptor is required.");
        return 0;
    }
    return attachEmbeddedNativeHost(*desc) ? 1 : 0;
}

extern "C" WINGUI_API void WINGUI_CALL wingui_native_session_end_embedded(WinguiNativeUiSession* session) {
    wingui_native_session_detach_embedded_host(session);
    wingui_native_session_set_callbacks(session, nullptr);
}

extern "C" WINGUI_API void WINGUI_CALL wingui_native_session_detach_embedded_host(WinguiNativeUiSession* session) {
    ScopedNativeSession scoped(requireNativeSession(session));
    clearNativeLastError();
    HWND hwnd = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_native.mutex);
        if (!g_native.embedded_mode) return;
        hwnd = g_native.hwnd;
    }
    if (hwnd && IsWindow(hwnd)) {
        DestroyWindow(hwnd);
    }
    std::lock_guard<std::mutex> lock(g_native.mutex);
    g_native.hwnd = nullptr;
    g_native.parent_hwnd = nullptr;
    g_native.ready = false;
    g_native.running = false;
    g_native.embedded_mode = false;
}

extern "C" WINGUI_API int64_t WINGUI_CALL wingui_native_session_set_host_bounds(
    WinguiNativeUiSession* session,
    int32_t x,
    int32_t y,
    int32_t width,
    int32_t height) {
    ScopedNativeSession scoped(requireNativeSession(session));
    clearNativeLastError();
    HWND hwnd = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_native.mutex);
        hwnd = g_native.hwnd;
    }
    if (!hwnd || !IsWindow(hwnd)) {
        setNativeLastError("Native UI host window is not available.");
        return 0;
    }
    bool embedded_mode = false;
    {
        std::lock_guard<std::mutex> lock(g_native.mutex);
        embedded_mode = g_native.embedded_mode;
    }
    if (embedded_mode) {
        return MoveWindow(hwnd,
                          x,
                          y,
                          std::max(1, width),
                          std::max(1, height),
                          TRUE) ? 1 : 0;
    }
    const RECT clamped = clampWindowRectToWorkArea(RECT{x, y, x + std::max(1, width), y + std::max(1, height)});
    return MoveWindow(hwnd,
                      clamped.left,
                      clamped.top,
                      clamped.right - clamped.left,
                      clamped.bottom - clamped.top,
                      TRUE) ? 1 : 0;
}

extern "C" WINGUI_API void* WINGUI_CALL wingui_native_session_host_hwnd(WinguiNativeUiSession* session) {
    ScopedNativeSession scoped(requireNativeSession(session));
    std::lock_guard<std::mutex> lock(g_native.mutex);
    return g_native.hwnd;
}

extern "C" WINGUI_API int64_t WINGUI_CALL wingui_native_session_handle_host_command(
    WinguiNativeUiSession* session,
    int32_t command_id) {
    ScopedNativeSession scoped(requireNativeSession(session));
    clearNativeLastError();
    if (command_id <= 0 || g_native.suppress_events) {
        return 0;
    }
    auto command_it = g_native.command_bindings.find(command_id);
    if (command_it == g_native.command_bindings.end()) {
        return 0;
    }
    if (command_it->second.event_name.empty()) {
        return 0;
    }
    dispatchCommandBindingEvent(command_it->second);
    return 1;
}

extern "C" WINGUI_API int64_t WINGUI_CALL wingui_native_session_get_content_size(
    WinguiNativeUiSession* session,
    int32_t* out_width,
    int32_t* out_height) {
    ScopedNativeSession scoped(requireNativeSession(session));
    if (!out_width || !out_height) {
        setNativeLastError("Native UI content size requires width and height outputs.");
        return 0;
    }
    std::lock_guard<std::mutex> lock(g_native.mutex);
    *out_width = g_native.content_width;
    *out_height = g_native.content_height;
    clearNativeLastError();
    return 1;
}

extern "C" WINGUI_API int64_t WINGUI_CALL wingui_native_session_try_get_node_bounds(
    WinguiNativeUiSession* session,
    const char* node_id_utf8,
    WinguiNativeNodeBounds* out_bounds) {
    ScopedNativeSession scoped(requireNativeSession(session));
    clearNativeLastError();
    return tryGetNativeNodeBounds(node_id_utf8, out_bounds) ? 1 : 0;
}

extern "C" WINGUI_API int64_t WINGUI_CALL wingui_native_session_try_get_node_hwnd(
    WinguiNativeUiSession* session,
    const char* node_id_utf8,
    void** out_hwnd) {
    ScopedNativeSession scoped(requireNativeSession(session));
    clearNativeLastError();
    return tryGetNativeNodeHwnd(node_id_utf8, out_hwnd) ? 1 : 0;
}

extern "C" WINGUI_API int64_t WINGUI_CALL wingui_native_session_try_get_node_type_utf8(
    WinguiNativeUiSession* session,
    const char* node_id_utf8,
    char* buffer_utf8,
    uint32_t buffer_size) {
    ScopedNativeSession scoped(requireNativeSession(session));
    clearNativeLastError();
    return tryGetNativeNodeTypeUtf8(node_id_utf8, buffer_utf8, buffer_size) ? 1 : 0;
}

extern "C" WINGUI_API int64_t WINGUI_CALL wingui_native_session_copy_focused_pane_id_utf8(
    WinguiNativeUiSession* session,
    char* buffer_utf8,
    uint32_t buffer_size) {
    ScopedNativeSession scoped(requireNativeSession(session));
    clearNativeLastError();
    return copyFocusedPaneIdUtf8(buffer_utf8, buffer_size) ? 1 : 0;
}

extern "C" WINGUI_API int64_t WINGUI_CALL wingui_native_session_get_patch_metrics(
    WinguiNativeUiSession* session,
    WinguiNativePatchMetrics* out_metrics) {
    ScopedNativeSession scoped(requireNativeSession(session));
    if (!out_metrics) {
        setNativeLastError("Native UI patch metrics require an output buffer.");
        return 0;
    }
    out_metrics->publish_count = g_native.publish_count.load(std::memory_order_relaxed);
    out_metrics->patch_request_count = g_native.patch_request_count.load(std::memory_order_relaxed);
    out_metrics->direct_apply_count = g_native.patch_direct_apply_count.load(std::memory_order_relaxed);
    out_metrics->subtree_rebuild_count = g_native.patch_subtree_rebuild_count.load(std::memory_order_relaxed);
    out_metrics->window_rebuild_count = g_native.patch_window_rebuild_count.load(std::memory_order_relaxed);
    out_metrics->resize_reject_count = g_native.patch_resize_reject_count.load(std::memory_order_relaxed);
    out_metrics->failed_patch_count = g_native.patch_failed_count.load(std::memory_order_relaxed);
    clearNativeLastError();
    return 1;
}

extern "C" WINGUI_API int64_t WINGUI_CALL wingui_native_session_open_url(
    WinguiNativeUiSession* session,
    const char* utf8) {
    ScopedNativeSession scoped(requireNativeSession(session));
    auto fn = g_native_callbacks.open_url;
    return fn ? fn(utf8) : 0;
}

extern "C" WINGUI_API const char* WINGUI_CALL wingui_native_session_clipboard_get(WinguiNativeUiSession* session) {
    ScopedNativeSession scoped(requireNativeSession(session));
    auto fn = g_native_callbacks.clipboard_get;
    return fn ? fn() : "";
}

extern "C" WINGUI_API int64_t WINGUI_CALL wingui_native_session_clipboard_set(
    WinguiNativeUiSession* session,
    const char* utf8) {
    ScopedNativeSession scoped(requireNativeSession(session));
    auto fn = g_native_callbacks.clipboard_set;
    return fn ? fn(utf8) : 0;
}

extern "C" WINGUI_API const char* WINGUI_CALL wingui_native_session_choose_open_file(
    WinguiNativeUiSession* session,
    const char* initial_path_utf8) {
    ScopedNativeSession scoped(requireNativeSession(session));
    auto fn = g_native_callbacks.choose_open_file;
    return fn ? fn(initial_path_utf8) : "";
}

extern "C" WINGUI_API const char* WINGUI_CALL wingui_native_session_choose_save_file(
    WinguiNativeUiSession* session,
    const char* initial_path_utf8) {
    ScopedNativeSession scoped(requireNativeSession(session));
    auto fn = g_native_callbacks.choose_save_file;
    return fn ? fn(initial_path_utf8) : "";
}

extern "C" WINGUI_API const char* WINGUI_CALL wingui_native_session_choose_folder(
    WinguiNativeUiSession* session,
    const char* initial_title_utf8) {
    ScopedNativeSession scoped(requireNativeSession(session));
    auto fn = g_native_callbacks.choose_folder;
    return fn ? fn(initial_title_utf8) : "";
}

extern "C" WINGUI_API void WINGUI_CALL wingui_native_set_callbacks(const WinguiNativeCallbacks* callbacks) {
    wingui_native_session_set_callbacks(defaultNativeSessionHandle(), callbacks);
}

extern "C" WINGUI_API int64_t WINGUI_CALL wingui_native_available(void) {
    return wingui_native_session_available(defaultNativeSessionHandle());
}

extern "C" WINGUI_API const char* WINGUI_CALL wingui_native_backend_info(void) {
    return wingui_native_session_backend_info(defaultNativeSessionHandle());
}

extern "C" WINGUI_API const char* WINGUI_CALL wingui_native_last_error_utf8(void) {
    return wingui_native_session_last_error_utf8(defaultNativeSessionHandle());
}

extern "C" WINGUI_API int64_t WINGUI_CALL wingui_native_enqueue_command(const WinguiNativeCommand* command) {
    return wingui_native_session_enqueue_command(defaultNativeSessionHandle(), command);
}

extern "C" WINGUI_API uint32_t WINGUI_CALL wingui_native_drain_command_queue(uint32_t max_commands) {
    return wingui_native_session_drain_command_queue(defaultNativeSessionHandle(), max_commands);
}

extern "C" WINGUI_API int64_t WINGUI_CALL wingui_native_poll_event(WinguiNativeEvent* out_event) {
    return wingui_native_session_poll_event(defaultNativeSessionHandle(), out_event);
}

extern "C" WINGUI_API void* WINGUI_CALL wingui_native_event_handle(void) {
    return wingui_native_session_event_handle(defaultNativeSessionHandle());
}

extern "C" WINGUI_API void WINGUI_CALL wingui_native_release_event(WinguiNativeEvent* event) {
    wingui_native_session_release_event(defaultNativeSessionHandle(), event);
}

extern "C" WINGUI_API int64_t WINGUI_CALL wingui_native_publish_json(const char* utf8) {
    return wingui_native_session_publish_json(defaultNativeSessionHandle(), utf8);
}

extern "C" WINGUI_API int64_t WINGUI_CALL wingui_native_patch_json(const char* utf8) {
    return wingui_native_session_patch_json(defaultNativeSessionHandle(), utf8);
}

extern "C" WINGUI_API int64_t WINGUI_CALL wingui_native_host_run(void) {
    return wingui_native_session_host_run(defaultNativeSessionHandle());
}

extern "C" WINGUI_API int64_t WINGUI_CALL wingui_native_begin_embedded_session(const WinguiNativeEmbeddedSessionDesc* desc) {
    return wingui_native_session_begin_embedded(defaultNativeSessionHandle(), desc);
}

extern "C" WINGUI_API int64_t WINGUI_CALL wingui_native_attach_embedded_host(const WinguiNativeEmbeddedHostDesc* desc) {
    return wingui_native_session_attach_embedded_host(defaultNativeSessionHandle(), desc);
}

extern "C" WINGUI_API void WINGUI_CALL wingui_native_end_embedded_session(void) {
    wingui_native_session_end_embedded(defaultNativeSessionHandle());
}

extern "C" WINGUI_API void WINGUI_CALL wingui_native_detach_embedded_host(void) {
    wingui_native_session_detach_embedded_host(defaultNativeSessionHandle());
}

extern "C" WINGUI_API int64_t WINGUI_CALL wingui_native_set_host_bounds(int32_t x, int32_t y, int32_t width, int32_t height) {
    return wingui_native_session_set_host_bounds(defaultNativeSessionHandle(), x, y, width, height);
}

extern "C" WINGUI_API void* WINGUI_CALL wingui_native_host_hwnd(void) {
    return wingui_native_session_host_hwnd(defaultNativeSessionHandle());
}

extern "C" WINGUI_API int64_t WINGUI_CALL wingui_native_handle_host_command(int32_t command_id) {
    return wingui_native_session_handle_host_command(defaultNativeSessionHandle(), command_id);
}

extern "C" WINGUI_API int64_t WINGUI_CALL wingui_native_get_content_size(int32_t* out_width, int32_t* out_height) {
    return wingui_native_session_get_content_size(defaultNativeSessionHandle(), out_width, out_height);
}

extern "C" WINGUI_API int64_t WINGUI_CALL wingui_native_try_get_node_bounds(const char* node_id_utf8, WinguiNativeNodeBounds* out_bounds) {
    return wingui_native_session_try_get_node_bounds(defaultNativeSessionHandle(), node_id_utf8, out_bounds);
}

extern "C" WINGUI_API int64_t WINGUI_CALL wingui_native_try_get_node_hwnd(const char* node_id_utf8, void** out_hwnd) {
    return wingui_native_session_try_get_node_hwnd(defaultNativeSessionHandle(), node_id_utf8, out_hwnd);
}

extern "C" WINGUI_API int64_t WINGUI_CALL wingui_native_try_get_node_type_utf8(const char* node_id_utf8, char* buffer_utf8, uint32_t buffer_size) {
    return wingui_native_session_try_get_node_type_utf8(defaultNativeSessionHandle(), node_id_utf8, buffer_utf8, buffer_size);
}

extern "C" WINGUI_API int64_t WINGUI_CALL wingui_native_copy_focused_pane_id_utf8(char* buffer_utf8, uint32_t buffer_size) {
    return wingui_native_session_copy_focused_pane_id_utf8(defaultNativeSessionHandle(), buffer_utf8, buffer_size);
}

extern "C" WINGUI_API int64_t WINGUI_CALL wingui_native_get_patch_metrics(WinguiNativePatchMetrics* out_metrics) {
    return wingui_native_session_get_patch_metrics(defaultNativeSessionHandle(), out_metrics);
}

extern "C" WINGUI_API int64_t WINGUI_CALL wingui_native_open_url(const char* utf8) {
    return wingui_native_session_open_url(defaultNativeSessionHandle(), utf8);
}

extern "C" WINGUI_API const char* WINGUI_CALL wingui_native_clipboard_get(void) {
    return wingui_native_session_clipboard_get(defaultNativeSessionHandle());
}

extern "C" WINGUI_API int64_t WINGUI_CALL wingui_native_clipboard_set(const char* utf8) {
    return wingui_native_session_clipboard_set(defaultNativeSessionHandle(), utf8);
}

extern "C" WINGUI_API const char* WINGUI_CALL wingui_native_choose_open_file(const char* initial_path_utf8) {
    return wingui_native_session_choose_open_file(defaultNativeSessionHandle(), initial_path_utf8);
}

extern "C" WINGUI_API const char* WINGUI_CALL wingui_native_choose_save_file(const char* initial_path_utf8) {
    return wingui_native_session_choose_save_file(defaultNativeSessionHandle(), initial_path_utf8);
}

extern "C" WINGUI_API const char* WINGUI_CALL wingui_native_choose_folder(const char* initial_title_utf8) {
    return wingui_native_session_choose_folder(defaultNativeSessionHandle(), initial_title_utf8);
}
