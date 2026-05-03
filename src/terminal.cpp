#ifndef WINGUI_BUILD_DLL
#define WINGUI_BUILD_DLL
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "wingui/terminal.h"

#include "wingui/native_ui.h"
#include "wingui_internal.h"

#include <windows.h>
#include <windowsx.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

struct SuperTerminalRuntimeHost;

struct SuperTerminalHostedAppRuntime;

struct SuperTerminalClientContext {
    SuperTerminalRuntimeHost* host = nullptr;
};

struct SuperTerminalHostedAppRuntime {
    SuperTerminalHostedAppDesc desc{};
};

constexpr uint32_t kSuperTerminalWakeMessage = WM_APP + 0x61;
constexpr uint32_t kDefaultQueueCapacity = 1024;
constexpr uint32_t kDefaultColumns = 80;
constexpr uint32_t kDefaultRows = 25;
constexpr char kDefaultShaderPath[] = "shaders/text_grid.hlsl";
constexpr char kDefaultGraphicsShaderPath[] = "shaders/graphics.hlsl";
constexpr char kDefaultSpriteShaderPath[] = "shaders/sprite.hlsl";

std::mutex g_terminal_trace_mutex;

std::wstring terminalTracePath() {
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

void traceNativePatchEvent(const std::string& message) {
    char buffer[1024];
    std::snprintf(buffer,
                  sizeof(buffer),
                  "[%llu][tid=%lu] %s",
                  static_cast<unsigned long long>(GetTickCount64()),
                  static_cast<unsigned long>(GetCurrentThreadId()),
                  message.c_str());

    std::lock_guard<std::mutex> lock(g_terminal_trace_mutex);
    const std::wstring path = terminalTracePath();
    HANDLE file = CreateFileW(path.c_str(),
                              FILE_APPEND_DATA,
                              FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr,
                              OPEN_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL,
                              nullptr);
    if (file == INVALID_HANDLE_VALUE) return;

    DWORD written = 0;
    WriteFile(file, buffer, static_cast<DWORD>(std::strlen(buffer)), &written, nullptr);
    WriteFile(file, "\r\n", 2, &written, nullptr);
    CloseHandle(file);
}

bool shouldTraceWorkspacePaneNodeId(const std::string& node_id) {
    return node_id == "workspace_editor" ||
        node_id == "workspace_graphics" ||
        node_id == "workspace_repl";
}

struct TerminalCell {
    uint32_t codepoint = ' ';
    WinguiGraphicsColour foreground{255, 255, 255, 255};
    WinguiGraphicsColour background{0, 0, 0, 255};
};

struct TerminalSurface {
    uint32_t columns = 0;
    uint32_t rows = 0;
    std::vector<TerminalCell> cells;
};

struct BufferedTerminalSurface {
    TerminalSurface buffers[2];
};

enum PaneRenderKind : uint32_t {
    PANE_RENDER_TEXT_GRID = 0,
    PANE_RENDER_INDEXED = 1,
    PANE_RENDER_RGBA = 2,
};

/* One entry in a per-pane sprite bank. Mirrors WinguiSpriteAtlasEntry plus
   animation metadata. atlas_entry.atlas_x/y/width/height/frame_w/h/frame_count
   are filled in when the sprite is uploaded to the shared sprite atlas. */
struct SpriteBankEntry {
    uint32_t sprite_id = 0;
    WinguiSpriteAtlasEntry atlas_entry{};
    uint32_t frames_per_tick = 0;  /* 0 = static */
    bool uploaded = false;
};

/* Simple shelf/row atlas packer. Tracks allocation within the
   shared sprite atlas (sprite_atlas_size x sprite_atlas_size). */
struct ShelfPacker {
    uint32_t atlas_size = 0;
    uint32_t shelf_x = 0;      /* current x in the active shelf */
    uint32_t shelf_y = 0;      /* top-y of the active shelf     */
    uint32_t shelf_h = 0;      /* height of the active shelf    */

    void init(uint32_t size) {
        atlas_size = size;
        shelf_x = shelf_y = shelf_h = 0;
    }

    /* Returns false if there is no room. */
    bool alloc(uint32_t w, uint32_t h, uint32_t& out_x, uint32_t& out_y) {
        if (!atlas_size || !w || !h || w > atlas_size || h > atlas_size) return false;
        if (shelf_x + w > atlas_size) {
            /* advance to next shelf */
            shelf_y += shelf_h;
            shelf_x = 0;
            shelf_h = 0;
        }
        if (shelf_y + h > atlas_size) return false;
        out_x = shelf_x;
        out_y = shelf_y;
        shelf_x += w;
        if (h > shelf_h) shelf_h = h;
        return true;
    }
};

struct RegisteredPane {
    SuperTerminalPaneId pane_id{};
    std::string node_id;
    SuperTerminalPaneLayout layout{};
    uint32_t render_kind = PANE_RENDER_TEXT_GRID;
    BufferedTerminalSurface surface;
    std::vector<WinguiGlyphInstance> text_grid_glyph_instances[2];
    bool text_grid_cache_dirty[2]{true, true};
    uint64_t text_grid_cache_build_count[2]{0, 0};
    uint64_t text_grid_cache_hit_count[2]{0, 0};
    uint64_t text_grid_gpu_upload_count[2]{0, 0};
    void* text_grid_hwnd = nullptr;
    WinguiContext* text_grid_context = nullptr;
    WinguiTextGridRenderer* pane_text_renderer = nullptr;
    int32_t pane_text_renderer_atlas_set = 0;
    WinguiIndexedSurface* indexed_surface_handle = nullptr;
    uint32_t indexed_screen_width = 0;
    uint32_t indexed_screen_height = 0;
    int32_t indexed_scroll_x = 0;
    int32_t indexed_scroll_y = 0;
    uint32_t indexed_pixel_aspect_num = 1;
    uint32_t indexed_pixel_aspect_den = 1;
    WinguiRgbaSurface* rgba_surface_handle = nullptr;
    uint32_t rgba_content_buffer_mode = SUPERTERMINAL_RGBA_CONTENT_BUFFER_FRAME;
    uint32_t rgba_surface_buffer_count = 0;
    uint32_t rgba_screen_width = 0;
    uint32_t rgba_screen_height = 0;
    uint32_t rgba_pixel_aspect_num = 1;
    uint32_t rgba_pixel_aspect_den = 1;
    void* rgba_hwnd = nullptr;
    WinguiContext* rgba_context = nullptr;
    WinguiRgbaPaneRenderer* pane_rgba_renderer = nullptr;
    WinguiVectorRenderer* pane_vector_renderer = nullptr;
    int32_t pane_vector_renderer_atlas_set = 0;
    std::vector<SpriteBankEntry> sprite_bank;
};

template <typename T>
struct RingQueue {
    std::vector<T> items;
    size_t head = 0;
    size_t tail = 0;
    size_t count = 0;
    HANDLE event_handle = nullptr;
    std::mutex mutex;

    bool init(uint32_t capacity, bool create_event_handle) {
        if (capacity == 0) capacity = 1;
        items.resize(capacity);
        if (create_event_handle) {
            event_handle = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (!event_handle) {
                items.clear();
                return false;
            }
        }
        return true;
    }

    void shutdown() {
        if (event_handle) {
            CloseHandle(event_handle);
            event_handle = nullptr;
        }
        items.clear();
        head = 0;
        tail = 0;
        count = 0;
    }

    bool push(const T& value) {
        std::lock_guard<std::mutex> lock(mutex);
        if (items.empty() || count >= items.size()) {
            return false;
        }
        items[tail] = value;
        tail = (tail + 1) % items.size();
        ++count;
        if (event_handle) {
            SetEvent(event_handle);
        }
        return true;
    }

    bool pop(T* out_value) {
        std::lock_guard<std::mutex> lock(mutex);
        if (!out_value || count == 0 || items.empty()) {
            return false;
        }
        *out_value = items[head];
        head = (head + 1) % items.size();
        --count;
        if (event_handle && count == 0) {
            ResetEvent(event_handle);
        }
        return true;
    }

    bool empty() {
        std::lock_guard<std::mutex> lock(mutex);
        return count == 0;
    }
};

struct SuperTerminalRuntimeHost {
    SuperTerminalAppDesc desc{};
    WinguiWindow* window = nullptr;
    WinguiContext* context = nullptr;
    WinguiTextGridRenderer* text_renderer = nullptr;
    WinguiIndexedGraphicsRenderer* indexed_renderer = nullptr;
    WinguiRgbaPaneRenderer* rgba_renderer = nullptr;
    WinguiVectorRenderer* vector_renderer = nullptr;
    int32_t vector_renderer_init_attempted = 0;
    int32_t vector_renderer_atlas_set = 0;
    WinguiIndexedFillRenderer* indexed_fill_renderer = nullptr;
    int32_t indexed_fill_renderer_init_attempted = 0;
    WinguiGlyphAtlasBitmap atlas{};
    RingQueue<SuperTerminalCommand> command_queue;
    RingQueue<SuperTerminalEvent> event_queue;
    SuperTerminalClientContext client_ctx{};
    BufferedTerminalSurface surface;
    std::vector<RegisteredPane> panes;
    std::vector<WinguiGlyphInstance> glyph_instances;
    std::mutex pane_mutex;
    std::thread client_thread;
    std::atomic<int32_t> stop_requested{0};
    std::atomic<int32_t> render_dirty{0};
    std::atomic<int32_t> client_finished{0};
    std::atomic<int32_t> close_event_sent{0};
    std::atomic<int32_t> window_focused{0};
    std::atomic<uint32_t> command_sequence{0};
    std::atomic<uint32_t> event_sequence{0};
    std::atomic<uint32_t> display_buffer_index{0};
    std::atomic<uint64_t> next_asset_id{1};
    std::unordered_map<uint64_t, WinguiRgbaSurface*> rgba_assets;
    std::mutex asset_mutex;
    ShelfPacker sprite_atlas_packer;
    int32_t exit_code = 0;
    int32_t host_error_code = SUPERTERMINAL_HOST_ERROR_NONE;
    std::string message;
    bool native_attached = false;
    SuperTerminalPaneId active_pane_id{};
};

namespace {

SuperTerminalRuntimeHost* g_active_host = nullptr;

void setHostError(SuperTerminalRuntimeHost* host, int32_t code, const char* message) {
    if (host) {
        host->host_error_code = code;
        host->message = message ? message : "";
    }
    wingui_set_last_error_string_internal(message ? message : "super_terminal failure");
}

void copyUtf8Truncate(char* dst, size_t dst_size, const char* src) {
    if (!dst || dst_size == 0) return;
    dst[0] = '\0';
    if (!src) return;
    std::strncpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';
}

bool paneIdsEqual(SuperTerminalPaneId left, SuperTerminalPaneId right) {
    return left.value == right.value;
}

uint64_t hashPaneNodeId(const char* text) {
    if (!text || !*text) return 0;
    uint64_t hash = 14695981039346656037ull;
    for (const unsigned char* ptr = reinterpret_cast<const unsigned char*>(text); *ptr; ++ptr) {
        hash ^= static_cast<uint64_t>(*ptr);
        hash *= 1099511628211ull;
    }
    return hash == 0 ? 1 : hash;
}

bool initSurface(TerminalSurface* surface, uint32_t columns, uint32_t rows);
bool initBufferedSurface(BufferedTerminalSurface* surface, uint32_t columns, uint32_t rows);
uint32_t currentModifiers();
bool pushEvent(SuperTerminalRuntimeHost* host, const SuperTerminalEvent& event);

RegisteredPane* findRegisteredPaneLocked(SuperTerminalRuntimeHost* host, SuperTerminalPaneId pane_id) {
    if (!host || pane_id.value == 0) return nullptr;
    for (RegisteredPane& pane : host->panes) {
        if (paneIdsEqual(pane.pane_id, pane_id)) {
            return &pane;
        }
    }
    return nullptr;
}

RegisteredPane* ensureRegisteredPaneLocked(SuperTerminalRuntimeHost* host, SuperTerminalPaneId pane_id) {
    if (!host || pane_id.value == 0) return nullptr;
    if (RegisteredPane* pane = findRegisteredPaneLocked(host, pane_id)) {
        return pane;
    }
    host->panes.push_back(RegisteredPane{});
    RegisteredPane& pane = host->panes.back();
    pane.pane_id = pane_id;
    initBufferedSurface(&pane.surface,
        host->desc.columns ? host->desc.columns : kDefaultColumns,
        host->desc.rows ? host->desc.rows : kDefaultRows);
    return &pane;
}

BufferedTerminalSurface* textGridSurfaceSetForPaneLocked(SuperTerminalRuntimeHost* host, SuperTerminalPaneId pane_id, bool create_if_missing) {
    if (!host) return nullptr;
    if (pane_id.value == 0) {
        return &host->surface;
    }
    RegisteredPane* pane = create_if_missing
        ? ensureRegisteredPaneLocked(host, pane_id)
        : findRegisteredPaneLocked(host, pane_id);
    return pane ? &pane->surface : nullptr;
}

RegisteredPane* registeredPaneForIdLocked(SuperTerminalRuntimeHost* host, SuperTerminalPaneId pane_id, bool create_if_missing) {
    if (!host) return nullptr;
    return create_if_missing ? ensureRegisteredPaneLocked(host, pane_id) : findRegisteredPaneLocked(host, pane_id);
}

uint32_t paneRenderKindForNodeType(const char* node_type_utf8) {
    if (!node_type_utf8 || !*node_type_utf8) return PANE_RENDER_TEXT_GRID;
    if (std::strcmp(node_type_utf8, "rgba-pane") == 0) return PANE_RENDER_RGBA;
    if (std::strcmp(node_type_utf8, "indexed-graphics") == 0) return PANE_RENDER_INDEXED;
    return PANE_RENDER_TEXT_GRID;
}

bool assignPaneNodeIdLocked(SuperTerminalRuntimeHost* host,
                           SuperTerminalPaneId pane_id,
                           const char* node_id_utf8,
                           const char* error_prefix) {
    if (!host || pane_id.value == 0 || !node_id_utf8 || !*node_id_utf8) return false;

    RegisteredPane* pane = ensureRegisteredPaneLocked(host, pane_id);
    if (!pane) {
        if (error_prefix) {
            std::string message = std::string(error_prefix) + ": failed to allocate pane state";
            wingui_set_last_error_string_internal(message.c_str());
        }
        return false;
    }

    if (!pane->node_id.empty() && pane->node_id != node_id_utf8) {
        if (error_prefix) {
            std::string message = std::string(error_prefix) + ": pane id collision between node ids '" + pane->node_id + "' and '" + node_id_utf8 + "'";
            wingui_set_last_error_string_internal(message.c_str());
        }
        return false;
    }

    pane->node_id = node_id_utf8;
    char node_type_utf8[64]{};
    if (wingui_native_try_get_node_type_utf8(node_id_utf8, node_type_utf8, static_cast<uint32_t>(sizeof(node_type_utf8)))) {
        pane->render_kind = paneRenderKindForNodeType(node_type_utf8);
        if (shouldTraceWorkspacePaneNodeId(pane->node_id)) {
            char buffer[256];
            std::snprintf(buffer,
                          sizeof(buffer),
                          "classify-pane id=%s type=%s render=%u",
                          pane->node_id.c_str(),
                          node_type_utf8,
                          static_cast<unsigned>(pane->render_kind));
            traceNativePatchEvent(buffer);
        }
    }
    return true;
}

std::string paneNodeIdForTrace(SuperTerminalRuntimeHost* host, SuperTerminalPaneId pane_id) {
    if (!host || pane_id.value == 0) return std::string();
    std::lock_guard<std::mutex> lock(host->pane_mutex);
    if (RegisteredPane* pane = registeredPaneForIdLocked(host, pane_id, false)) {
        return pane->node_id;
    }
    return std::string();
}

TerminalSurface* surfaceBuffer(BufferedTerminalSurface* surface_set, uint32_t buffer_index) {
    if (!surface_set) return nullptr;
    return &surface_set->buffers[buffer_index & 1u];
}

TerminalSurface* textGridSurfaceForPaneBuffer(SuperTerminalRuntimeHost* host,
                                              SuperTerminalPaneId pane_id,
                                              bool create_if_missing,
                                              uint32_t buffer_index) {
    if (!host) return nullptr;
    std::lock_guard<std::mutex> lock(host->pane_mutex);
    BufferedTerminalSurface* surface_set = textGridSurfaceSetForPaneLocked(host, pane_id, create_if_missing);
    return surfaceBuffer(surface_set, buffer_index);
}

void swapDisplayedBuffers(SuperTerminalRuntimeHost* host) {
    if (!host) return;
    host->display_buffer_index.store(host->display_buffer_index.load(std::memory_order_acquire) ^ 1u, std::memory_order_release);
}

bool copyPaneLayoutFromCache(SuperTerminalRuntimeHost* host, SuperTerminalPaneId pane_id, SuperTerminalPaneLayout* out_layout) {
    if (!host || !out_layout || pane_id.value == 0) return false;
    std::lock_guard<std::mutex> lock(host->pane_mutex);
    RegisteredPane* pane = findRegisteredPaneLocked(host, pane_id);
    if (!pane) return false;
    *out_layout = pane->layout;
    return pane->layout.width > 0 && pane->layout.height > 0;
}

bool resolvePaneLayout(SuperTerminalRuntimeHost* host, SuperTerminalPaneId pane_id, SuperTerminalPaneLayout* out_layout) {
    if (!host || !out_layout) return false;

    if (pane_id.value == 0) {
        int32_t client_width = 0;
        int32_t client_height = 0;
        if (!wingui_window_client_size(host->window, &client_width, &client_height)) {
            return false;
        }
        out_layout->x = 0;
        out_layout->y = 0;
        out_layout->width = std::max(client_width, 0);
        out_layout->height = std::max(client_height, 0);
        out_layout->visible = 1;
        out_layout->cell_width = host->atlas.info.cell_width;
        out_layout->cell_height = host->atlas.info.cell_height;
        out_layout->columns = std::max<uint32_t>(1, static_cast<uint32_t>(out_layout->width / std::max(1.0f, out_layout->cell_width)));
        out_layout->rows = std::max<uint32_t>(1, static_cast<uint32_t>(out_layout->height / std::max(1.0f, out_layout->cell_height)));
        return true;
    }

    std::string node_id;
    {
        std::lock_guard<std::mutex> lock(host->pane_mutex);
        RegisteredPane* pane = findRegisteredPaneLocked(host, pane_id);
        if (!pane || pane->node_id.empty()) {
            return false;
        }
        node_id = pane->node_id;
    }

    WinguiNativeNodeBounds bounds{};
    if (!wingui_native_try_get_node_bounds(node_id.c_str(), &bounds)) {
        return false;
    }

    out_layout->x = bounds.x;
    out_layout->y = bounds.y;
    out_layout->width = bounds.width;
    out_layout->height = bounds.height;
    out_layout->visible = bounds.visible;
    out_layout->cell_width = host->atlas.info.cell_width;
    out_layout->cell_height = host->atlas.info.cell_height;
    out_layout->columns = std::max<uint32_t>(1, static_cast<uint32_t>(std::max(bounds.width, 0) / std::max(1.0f, out_layout->cell_width)));
    out_layout->rows = std::max<uint32_t>(1, static_cast<uint32_t>(std::max(bounds.height, 0) / std::max(1.0f, out_layout->cell_height)));

    std::lock_guard<std::mutex> lock(host->pane_mutex);
    if (RegisteredPane* pane = findRegisteredPaneLocked(host, pane_id)) {
        pane->layout = *out_layout;
        if (shouldTraceWorkspacePaneNodeId(node_id)) {
            char buffer[512];
            std::snprintf(buffer,
                          sizeof(buffer),
                          "resolve-pane-layout id=%s pane=%llu x=%d y=%d w=%d h=%d visible=%d cols=%u rows=%u",
                          node_id.c_str(),
                          static_cast<unsigned long long>(pane_id.value),
                          static_cast<int>(out_layout->x),
                          static_cast<int>(out_layout->y),
                          static_cast<int>(out_layout->width),
                          static_cast<int>(out_layout->height),
                          static_cast<int>(out_layout->visible),
                          static_cast<unsigned>(out_layout->columns),
                          static_cast<unsigned>(out_layout->rows));
            traceNativePatchEvent(buffer);
        }
    }
    return true;
}

bool paneLayoutContainsPoint(const SuperTerminalPaneLayout& layout, int32_t x, int32_t y) {
    if (layout.visible == 0 || layout.width <= 0 || layout.height <= 0) return false;
    return x >= layout.x && y >= layout.y && x < (layout.x + layout.width) && y < (layout.y + layout.height);
}

POINT mousePointForMessage(uint32_t message, intptr_t lparam, const WinguiMouseState& mouse_state, HWND hwnd) {
    POINT point{};
    if (message == WM_MOUSEWHEEL) {
        point.x = GET_X_LPARAM(lparam);
        point.y = GET_Y_LPARAM(lparam);
        if (hwnd) {
            ScreenToClient(hwnd, &point);
        }
    } else {
        point.x = mouse_state.x;
        point.y = mouse_state.y;
    }
    return point;
}

SuperTerminalPaneId hitTestPane(SuperTerminalRuntimeHost* host, int32_t x, int32_t y) {
    if (!host) return {};
    std::vector<SuperTerminalPaneId> pane_ids;
    {
        std::lock_guard<std::mutex> lock(host->pane_mutex);
        pane_ids.reserve(host->panes.size());
        for (const RegisteredPane& pane : host->panes) {
            pane_ids.push_back(pane.pane_id);
        }
    }

    for (auto it = pane_ids.rbegin(); it != pane_ids.rend(); ++it) {
        SuperTerminalPaneLayout layout{};
        if (!resolvePaneLayout(host, *it, &layout)) {
            continue;
        }
        if (paneLayoutContainsPoint(layout, x, y)) {
            return *it;
        }
    }
    return {};
}

void pushPaneFocusEvent(SuperTerminalRuntimeHost* host, SuperTerminalPaneId pane_id, int32_t focused) {
    if (!host || pane_id.value == 0) return;
    SuperTerminalEvent event{};
    event.type = SUPERTERMINAL_EVENT_PANE_INPUT;
    event.sequence = host->event_sequence.fetch_add(1, std::memory_order_relaxed) + 1;
    event.data.pane_input.pane_id = pane_id;
    event.data.pane_input.device_kind = SUPERTERMINAL_PANE_INPUT_FOCUS;
    event.data.pane_input.event_kind = 0;
    event.data.pane_input.modifiers = currentModifiers();
    event.data.pane_input.focused = focused;
    pushEvent(host, event);
}

void setActivePane(SuperTerminalRuntimeHost* host, SuperTerminalPaneId pane_id) {
    if (!host || paneIdsEqual(host->active_pane_id, pane_id)) return;
    const int32_t window_focused = host->window_focused.load(std::memory_order_acquire);
    const SuperTerminalPaneId previous = host->active_pane_id;
    host->active_pane_id = pane_id;
    if (window_focused) {
        pushPaneFocusEvent(host, previous, 0);
        pushPaneFocusEvent(host, pane_id, 1);
    }
}

void syncActivePaneFromDeclarativeFocus(SuperTerminalRuntimeHost* host) {
    if (!host) return;
    char node_id_utf8[128]{};
    if (!wingui_native_copy_focused_pane_id_utf8(node_id_utf8, static_cast<uint32_t>(sizeof(node_id_utf8)))) {
        return;
    }
    if (node_id_utf8[0] == '\0') {
        setActivePane(host, SuperTerminalPaneId{});
        return;
    }

    SuperTerminalPaneId pane_id{};
    pane_id.value = hashPaneNodeId(node_id_utf8);
    {
        std::lock_guard<std::mutex> lock(host->pane_mutex);
        if (!assignPaneNodeIdLocked(host, pane_id, node_id_utf8, "syncActivePaneFromDeclarativeFocus")) {
            return;
        }
    }
    setActivePane(host, pane_id);
}

void pushPaneMouseEvent(SuperTerminalRuntimeHost* host,
                        SuperTerminalPaneId pane_id,
                        const SuperTerminalMouseEvent& mouse,
                        uint32_t modifiers) {
    if (!host || pane_id.value == 0) return;
    SuperTerminalEvent event{};
    event.type = SUPERTERMINAL_EVENT_PANE_INPUT;
    event.sequence = host->event_sequence.fetch_add(1, std::memory_order_relaxed) + 1;
    event.data.pane_input.pane_id = pane_id;
    event.data.pane_input.device_kind = SUPERTERMINAL_PANE_INPUT_MOUSE;
    event.data.pane_input.event_kind = mouse.kind;
    event.data.pane_input.x = mouse.x;
    event.data.pane_input.y = mouse.y;
    event.data.pane_input.wheel_delta = mouse.wheel_delta;
    event.data.pane_input.buttons = mouse.buttons;
    event.data.pane_input.button_mask = mouse.button_mask;
    event.data.pane_input.modifiers = modifiers;
    pushEvent(host, event);
}

void pushPaneKeyEvent(SuperTerminalRuntimeHost* host, SuperTerminalPaneId pane_id, const SuperTerminalKeyEvent& key) {
    if (!host || pane_id.value == 0) return;
    SuperTerminalEvent event{};
    event.type = SUPERTERMINAL_EVENT_PANE_INPUT;
    event.sequence = host->event_sequence.fetch_add(1, std::memory_order_relaxed) + 1;
    event.data.pane_input.pane_id = pane_id;
    event.data.pane_input.device_kind = SUPERTERMINAL_PANE_INPUT_KEYBOARD;
    event.data.pane_input.event_kind = key.is_down ? 1u : 0u;
    event.data.pane_input.virtual_key = key.virtual_key;
    event.data.pane_input.modifiers = key.modifiers;
    event.data.pane_input.focused = host->window_focused.load(std::memory_order_acquire);
    pushEvent(host, event);
}

uint32_t currentModifiers() {
    uint32_t modifiers = 0;
    if ((GetKeyState(VK_SHIFT) & 0x8000) != 0) modifiers |= 1u << 0;
    if ((GetKeyState(VK_CONTROL) & 0x8000) != 0) modifiers |= 1u << 1;
    if ((GetKeyState(VK_MENU) & 0x8000) != 0) modifiers |= 1u << 2;
    return modifiers;
}

bool pushEvent(SuperTerminalRuntimeHost* host, const SuperTerminalEvent& event) {
    if (!host) return false;
    return host->event_queue.push(event);
}

void sendHostStoppingEvent(SuperTerminalRuntimeHost* host, int32_t exit_code) {
    if (!host) return;
    SuperTerminalEvent event{};
    event.type = SUPERTERMINAL_EVENT_HOST_STOPPING;
    event.sequence = host->event_sequence.fetch_add(1, std::memory_order_relaxed) + 1;
    event.data.host_stopping.exit_code = exit_code;
    pushEvent(host, event);
}

void requestStopInternal(SuperTerminalRuntimeHost* host, int32_t exit_code, bool close_window) {
    if (!host) return;
    int32_t expected = 0;
    if (host->stop_requested.compare_exchange_strong(expected, 1, std::memory_order_acq_rel)) {
        host->exit_code = exit_code;
        sendHostStoppingEvent(host, exit_code);
    }
    if (close_window && host->window) {
        HWND hwnd = static_cast<HWND>(wingui_window_hwnd(host->window));
        if (hwnd) {
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
        }
    }
}

bool initSurface(TerminalSurface* surface, uint32_t columns, uint32_t rows) {
    if (!surface || columns == 0 || rows == 0) return false;
    surface->columns = columns;
    surface->rows = rows;
    surface->cells.assign(static_cast<size_t>(columns) * rows, TerminalCell{});
    return true;
}

bool initBufferedSurface(BufferedTerminalSurface* surface, uint32_t columns, uint32_t rows) {
    if (!surface) return false;
    return initSurface(&surface->buffers[0], columns, rows) &&
           initSurface(&surface->buffers[1], columns, rows);
}

TerminalCell* surfaceCell(TerminalSurface* surface, uint32_t row, uint32_t column) {
    if (!surface || row >= surface->rows || column >= surface->columns) return nullptr;
    return &surface->cells[static_cast<size_t>(row) * surface->columns + column];
}

bool resizeSurfacePreserve(TerminalSurface* surface, uint32_t columns, uint32_t rows) {
    if (!surface || columns == 0 || rows == 0) return false;
    std::vector<TerminalCell> new_cells(static_cast<size_t>(columns) * rows, TerminalCell{});
    const uint32_t copy_rows = std::min(surface->rows, rows);
    const uint32_t copy_cols = std::min(surface->columns, columns);
    for (uint32_t row = 0; row < copy_rows; ++row) {
        for (uint32_t col = 0; col < copy_cols; ++col) {
            new_cells[static_cast<size_t>(row) * columns + col] =
                surface->cells[static_cast<size_t>(row) * surface->columns + col];
        }
    }
    surface->columns = columns;
    surface->rows = rows;
    surface->cells.swap(new_cells);
    return true;
}

bool resizeBufferedSurfacePreserve(BufferedTerminalSurface* surface, uint32_t columns, uint32_t rows) {
    if (!surface) return false;
    return resizeSurfacePreserve(&surface->buffers[0], columns, rows) &&
           resizeSurfacePreserve(&surface->buffers[1], columns, rows);
}

uint32_t clampCodepoint(const SuperTerminalRuntimeHost& host, uint32_t codepoint) {
    const uint32_t first = host.atlas.info.first_codepoint;
    const uint32_t count = host.atlas.info.glyph_count;
    if (count == 0) return ' ';
    if (codepoint < first || codepoint >= first + count) {
        const uint32_t fallback = static_cast<uint32_t>('?');
        if (fallback >= first && fallback < first + count) {
            return fallback;
        }
        return first;
    }
    return codepoint;
}

bool enqueueResizeEvent(SuperTerminalRuntimeHost* host, uint32_t pixel_width, uint32_t pixel_height) {
    if (!host) return false;
    const TerminalSurface& front_surface = host->surface.buffers[host->display_buffer_index.load(std::memory_order_acquire) & 1u];
    SuperTerminalEvent event{};
    event.type = SUPERTERMINAL_EVENT_RESIZE;
    event.sequence = host->event_sequence.fetch_add(1, std::memory_order_relaxed) + 1;
    event.data.resize.pixel_width = pixel_width;
    event.data.resize.pixel_height = pixel_height;
    event.data.resize.columns = front_surface.columns;
    event.data.resize.rows = front_surface.rows;
    event.data.resize.dpi_scale = host->desc.dpi_scale > 0.0f ? host->desc.dpi_scale : 1.0f;
    event.data.resize.cell_width = host->atlas.info.cell_width;
    event.data.resize.cell_height = host->atlas.info.cell_height;
    return pushEvent(host, event);
}

bool updateSurfaceForClientSize(SuperTerminalRuntimeHost* host) {
    if (!host || !host->window) return false;
    int32_t client_width = 0;
    int32_t client_height = 0;
    if (!wingui_window_client_size(host->window, &client_width, &client_height)) {
        return false;
    }
    const uint32_t new_columns = std::max<uint32_t>(1, static_cast<uint32_t>(client_width / std::max(1.0f, host->atlas.info.cell_width)));
    const uint32_t new_rows = std::max<uint32_t>(1, static_cast<uint32_t>(client_height / std::max(1.0f, host->atlas.info.cell_height)));
    const TerminalSurface* front_surface = &host->surface.buffers[host->display_buffer_index.load(std::memory_order_acquire) & 1u];
    const bool changed = new_columns != front_surface->columns || new_rows != front_surface->rows;
    if (changed) {
        std::lock_guard<std::mutex> lock(host->pane_mutex);
        if (!resizeBufferedSurfacePreserve(&host->surface, new_columns, new_rows)) {
            return false;
        }
    }
    enqueueResizeEvent(host, static_cast<uint32_t>(std::max(client_width, 0)), static_cast<uint32_t>(std::max(client_height, 0)));
    host->render_dirty.store(1, std::memory_order_release);
    return true;
}

void clearSurfaceRegion(TerminalSurface* surface, const SuperTerminalTextGridClearRegion& clear_region) {
    if (!surface) return;
    const uint32_t row_end = std::min(surface->rows, clear_region.row + clear_region.height);
    const uint32_t col_end = std::min(surface->columns, clear_region.column + clear_region.width);
    for (uint32_t row = clear_region.row; row < row_end; ++row) {
        for (uint32_t col = clear_region.column; col < col_end; ++col) {
            TerminalCell* cell = surfaceCell(surface, row, col);
            if (!cell) continue;
            cell->codepoint = clear_region.fill_codepoint ? clear_region.fill_codepoint : ' ';
            cell->foreground = clear_region.foreground;
            cell->background = clear_region.background;
        }
    }
}

void writeCellsToSurface(TerminalSurface* surface, const SuperTerminalTextGridWriteCells& write_cells, const SuperTerminalRuntimeHost& host) {
    if (!surface || !write_cells.cells) return;
    for (uint32_t i = 0; i < write_cells.cell_count; ++i) {
        const SuperTerminalTextGridCell& src = write_cells.cells[i];
        TerminalCell* cell = surfaceCell(surface, src.row, src.column);
        if (!cell) continue;
        cell->codepoint = clampCodepoint(host, src.codepoint);
        cell->foreground = src.foreground;
        cell->background = src.background;
    }
}

void writeCellsToMirroredSurface(SuperTerminalRuntimeHost* host, const SuperTerminalTextGridWriteCells& write_cells) {
    if (!host || !write_cells.cells) return;
    std::lock_guard<std::mutex> lock(host->pane_mutex);
    BufferedTerminalSurface* surface_set = textGridSurfaceSetForPaneLocked(host, write_cells.pane_id, true);
    if (!surface_set) return;
    RegisteredPane* pane = registeredPaneForIdLocked(host, write_cells.pane_id, false);
    if (pane) {
        if (shouldTraceWorkspacePaneNodeId(pane->node_id) && write_cells.cell_count > 0) {
            const SuperTerminalTextGridCell& first = write_cells.cells[0];
            char buffer[512];
            std::snprintf(buffer,
                          sizeof(buffer),
                          "queue-write-cells id=%s pane=%llu surface=%p count=%u first=(r%u,c%u,cp%u)",
                          pane->node_id.c_str(),
                          static_cast<unsigned long long>(write_cells.pane_id.value),
                          static_cast<void*>(surface_set),
                          static_cast<unsigned>(write_cells.cell_count),
                          static_cast<unsigned>(first.row),
                          static_cast<unsigned>(first.column),
                          static_cast<unsigned>(first.codepoint));
            traceNativePatchEvent(buffer);
        }
    }
    writeCellsToSurface(&surface_set->buffers[0], write_cells, *host);
    writeCellsToSurface(&surface_set->buffers[1], write_cells, *host);
    if (pane) {
        pane->text_grid_cache_dirty[0] = true;
        pane->text_grid_cache_dirty[1] = true;
    }
}

void clearMirroredSurfaceRegion(SuperTerminalRuntimeHost* host, const SuperTerminalTextGridClearRegion& clear_region) {
    if (!host) return;
    std::lock_guard<std::mutex> lock(host->pane_mutex);
    BufferedTerminalSurface* surface_set = textGridSurfaceSetForPaneLocked(host, clear_region.pane_id, true);
    if (!surface_set) return;
    RegisteredPane* pane = registeredPaneForIdLocked(host, clear_region.pane_id, false);
    if (pane) {
        if (shouldTraceWorkspacePaneNodeId(pane->node_id)) {
            char buffer[512];
            std::snprintf(buffer,
                          sizeof(buffer),
                          "queue-clear-region id=%s pane=%llu surface=%p row=%u col=%u w=%u h=%u fill=%u",
                          pane->node_id.c_str(),
                          static_cast<unsigned long long>(clear_region.pane_id.value),
                          static_cast<void*>(surface_set),
                          static_cast<unsigned>(clear_region.row),
                          static_cast<unsigned>(clear_region.column),
                          static_cast<unsigned>(clear_region.width),
                          static_cast<unsigned>(clear_region.height),
                          static_cast<unsigned>(clear_region.fill_codepoint));
            traceNativePatchEvent(buffer);
        }
    }
    clearSurfaceRegion(&surface_set->buffers[0], clear_region);
    clearSurfaceRegion(&surface_set->buffers[1], clear_region);
    if (pane) {
        pane->text_grid_cache_dirty[0] = true;
        pane->text_grid_cache_dirty[1] = true;
    }
}

void buildGlyphInstances(const SuperTerminalRuntimeHost& host,
                         TerminalSurface* surface,
                         std::vector<WinguiGlyphInstance>* out_instances) {
    if (!surface || !out_instances) return;

    out_instances->resize(surface->cells.size());
    const uint32_t atlas_cols = std::max<uint32_t>(1, host.atlas.info.cols);
    for (uint32_t row = 0; row < surface->rows; ++row) {
        for (uint32_t col = 0; col < surface->columns; ++col) {
            const TerminalCell& cell = surface->cells[static_cast<size_t>(row) * surface->columns + col];
            const uint32_t codepoint = clampCodepoint(host, cell.codepoint);
            const uint32_t glyph_index = codepoint - host.atlas.info.first_codepoint;
            WinguiGlyphInstance& instance = (*out_instances)[static_cast<size_t>(row) * surface->columns + col];
            instance.pos_x = static_cast<float>(col);
            instance.pos_y = static_cast<float>(row);
            instance.uv_x = static_cast<float>((glyph_index % atlas_cols)) * host.atlas.info.cell_width;
            instance.uv_y = static_cast<float>((glyph_index / atlas_cols)) * host.atlas.info.cell_height;
            instance.fg[0] = cell.foreground.r;
            instance.fg[1] = cell.foreground.g;
            instance.fg[2] = cell.foreground.b;
            instance.fg[3] = cell.foreground.a;
            instance.bg[0] = cell.background.r;
            instance.bg[1] = cell.background.g;
            instance.bg[2] = cell.background.b;
            instance.bg[3] = cell.background.a;
            instance.flags = 0;
        }
    }
}

bool renderTextGridSurface(SuperTerminalRuntimeHost* host, TerminalSurface* surface, const SuperTerminalPaneLayout& layout) {
    if (!host || !surface || layout.visible == 0 || layout.width <= 0 || layout.height <= 0) return true;

    buildGlyphInstances(*host, surface, &host->glyph_instances);

    WinguiTextGridFrame frame{};
    frame.instances = host->glyph_instances.data();
    frame.instance_count = static_cast<uint32_t>(host->glyph_instances.size());
    frame.uniforms.viewport_width = static_cast<float>(std::max(layout.width, 1));
    frame.uniforms.viewport_height = static_cast<float>(std::max(layout.height, 1));
    frame.uniforms.cell_width = host->atlas.info.cell_width;
    frame.uniforms.cell_height = host->atlas.info.cell_height;
    frame.uniforms.atlas_width = host->atlas.info.atlas_width;
    frame.uniforms.atlas_height = host->atlas.info.atlas_height;
    frame.uniforms.row_origin = 0.0f;
    frame.uniforms.effects_mode = 0.0f;
    return wingui_text_grid_renderer_render(host->text_renderer, layout.x, layout.y, layout.width, layout.height, &frame) != 0;
}

bool renderTextGridSurfaceWithRenderer(SuperTerminalRuntimeHost* host,
                                       WinguiTextGridRenderer* renderer,
                                       TerminalSurface* surface,
                                       int32_t x,
                                       int32_t y,
                                       int32_t width,
                                       int32_t height) {
    if (!host || !renderer || !surface || width <= 0 || height <= 0) return true;

    buildGlyphInstances(*host, surface, &host->glyph_instances);

    WinguiTextGridFrame frame{};
    frame.instances = host->glyph_instances.data();
    frame.instance_count = static_cast<uint32_t>(host->glyph_instances.size());
    frame.uniforms.viewport_width = static_cast<float>(std::max(width, 1));
    frame.uniforms.viewport_height = static_cast<float>(std::max(height, 1));
    frame.uniforms.cell_width = host->atlas.info.cell_width;
    frame.uniforms.cell_height = host->atlas.info.cell_height;
    frame.uniforms.atlas_width = host->atlas.info.atlas_width;
    frame.uniforms.atlas_height = host->atlas.info.atlas_height;
    frame.uniforms.row_origin = 0.0f;
    frame.uniforms.effects_mode = 0.0f;
    return wingui_text_grid_renderer_render(renderer, x, y, width, height, &frame) != 0;
}

bool renderIndexedPane(SuperTerminalRuntimeHost* host,
                       const RegisteredPane& pane,
                       const SuperTerminalPaneLayout& layout,
                       uint32_t buffer_index) {
    if (!host || !host->indexed_renderer || !pane.indexed_surface_handle) return true;
    if (layout.visible == 0 || layout.width <= 0 || layout.height <= 0) return true;
    return wingui_indexed_surface_render(
        host->indexed_renderer,
        pane.indexed_surface_handle,
        layout.x, layout.y, layout.width, layout.height,
        pane.indexed_screen_width,
        pane.indexed_screen_height,
        pane.indexed_scroll_x,
        pane.indexed_scroll_y,
        pane.indexed_pixel_aspect_num,
        pane.indexed_pixel_aspect_den,
        buffer_index & 1u,
        nullptr) != 0;
}

void destroyPaneRgbaPresenter(RegisteredPane& pane) {
    if (pane.pane_vector_renderer) {
        wingui_destroy_vector_renderer(pane.pane_vector_renderer);
        pane.pane_vector_renderer = nullptr;
    }
    if (pane.pane_rgba_renderer) {
        wingui_destroy_rgba_pane_renderer(pane.pane_rgba_renderer);
        pane.pane_rgba_renderer = nullptr;
    }
    if (pane.rgba_surface_handle) {
        wingui_destroy_rgba_surface(pane.rgba_surface_handle);
        pane.rgba_surface_handle = nullptr;
    }
    if (pane.rgba_context) {
        wingui_destroy_context(pane.rgba_context);
        pane.rgba_context = nullptr;
    }
    pane.rgba_hwnd = nullptr;
    pane.rgba_surface_buffer_count = 0;
    pane.pane_vector_renderer_atlas_set = 0;
}

void destroyPaneTextGridPresenter(RegisteredPane& pane) {
    if (pane.pane_text_renderer) {
        wingui_destroy_text_grid_renderer(pane.pane_text_renderer);
        pane.pane_text_renderer = nullptr;
    }
    if (pane.text_grid_context) {
        wingui_destroy_context(pane.text_grid_context);
        pane.text_grid_context = nullptr;
    }
    pane.text_grid_hwnd = nullptr;
    pane.pane_text_renderer_atlas_set = 0;
}

bool ensurePaneTextGridPresenter(SuperTerminalRuntimeHost* host,
                                 RegisteredPane& pane,
                                 const SuperTerminalPaneLayout& layout) {
    if (!host || pane.node_id.empty() || layout.width <= 0 || layout.height <= 0) return false;

    void* node_hwnd_raw = nullptr;
    if (!wingui_native_try_get_node_hwnd(pane.node_id.c_str(), &node_hwnd_raw) || !node_hwnd_raw) {
        return false;
    }

    const int32_t target_width = std::max(layout.width, 1);
    const int32_t target_height = std::max(layout.height, 1);
    if (pane.text_grid_hwnd != node_hwnd_raw || !pane.text_grid_context || !pane.pane_text_renderer) {
        if (shouldTraceWorkspacePaneNodeId(pane.node_id)) {
            char buffer[512];
            std::snprintf(buffer,
                          sizeof(buffer),
                          "attach-text-grid-presenter id=%s hwnd=%p old_hwnd=%p w=%d h=%d",
                          pane.node_id.c_str(),
                          node_hwnd_raw,
                          pane.text_grid_hwnd,
                          static_cast<int>(target_width),
                          static_cast<int>(target_height));
            traceNativePatchEvent(buffer);
        }
        destroyPaneTextGridPresenter(pane);

        WinguiContextDesc context_desc{};
        context_desc.hwnd = node_hwnd_raw;
        context_desc.width = static_cast<uint32_t>(target_width);
        context_desc.height = static_cast<uint32_t>(target_height);
        context_desc.buffer_count = 2;
        context_desc.vsync_interval = 1;
        if (!wingui_create_context(&context_desc, &pane.text_grid_context)) {
            destroyPaneTextGridPresenter(pane);
            return false;
        }

        WinguiTextGridRendererDesc renderer_desc{};
        renderer_desc.context = pane.text_grid_context;
        renderer_desc.shader_path_utf8 = host->desc.text_shader_path_utf8 && *host->desc.text_shader_path_utf8 ? host->desc.text_shader_path_utf8 : kDefaultShaderPath;
        if (!wingui_create_text_grid_renderer(&renderer_desc, &pane.pane_text_renderer)) {
            destroyPaneTextGridPresenter(pane);
            return false;
        }

        if (!wingui_text_grid_renderer_set_atlas(pane.pane_text_renderer, &host->atlas)) {
            destroyPaneTextGridPresenter(pane);
            return false;
        }

        pane.text_grid_hwnd = node_hwnd_raw;
        pane.pane_text_renderer_atlas_set = 1;
    }

    if (pane.pane_text_renderer && !pane.pane_text_renderer_atlas_set) {
        if (wingui_text_grid_renderer_set_atlas(pane.pane_text_renderer, &host->atlas)) {
            pane.pane_text_renderer_atlas_set = 1;
        }
    }

    wingui_resize_context(pane.text_grid_context, static_cast<uint32_t>(target_width), static_cast<uint32_t>(target_height));
    if (shouldTraceWorkspacePaneNodeId(pane.node_id)) {
        char buffer[256];
        std::snprintf(buffer,
                      sizeof(buffer),
                      "resize-text-grid-context id=%s hwnd=%p w=%d h=%d",
                      pane.node_id.c_str(),
                      pane.text_grid_hwnd,
                      static_cast<int>(target_width),
                      static_cast<int>(target_height));
        traceNativePatchEvent(buffer);
    }
    return true;
}

bool renderTextGridPane(SuperTerminalRuntimeHost* host,
                        RegisteredPane& pane,
                        TerminalSurface* surface,
                        const SuperTerminalPaneLayout& layout,
                        uint32_t buffer_index) {
    if (layout.visible == 0 || layout.width <= 0 || layout.height <= 0) return true;
    if (!ensurePaneTextGridPresenter(host, pane, layout)) return true;

    std::vector<WinguiGlyphInstance>& cached_instances = pane.text_grid_glyph_instances[buffer_index & 1u];
    bool& cache_dirty = pane.text_grid_cache_dirty[buffer_index & 1u];
    uint64_t& cache_build_count = pane.text_grid_cache_build_count[buffer_index & 1u];
    uint64_t& cache_hit_count = pane.text_grid_cache_hit_count[buffer_index & 1u];
    uint64_t& gpu_upload_count = pane.text_grid_gpu_upload_count[buffer_index & 1u];
    if (cache_dirty || cached_instances.size() != surface->cells.size()) {
        buildGlyphInstances(*host, surface, &cached_instances);
        if (!wingui_text_grid_renderer_upload_instances(
                pane.pane_text_renderer,
                cached_instances.data(),
                static_cast<uint32_t>(cached_instances.size()))) {
            return false;
        }
        gpu_upload_count += 1;
        cache_dirty = false;
        cache_build_count += 1;
        if (shouldTraceWorkspacePaneNodeId(pane.node_id)) {
            char buffer[512];
            std::snprintf(buffer,
                          sizeof(buffer),
                          "text-cache-rebuild id=%s buf=%u builds=%llu hits=%llu cells=%u dirty=%u",
                          pane.node_id.c_str(),
                          static_cast<unsigned>(buffer_index & 1u),
                          static_cast<unsigned long long>(cache_build_count),
                          static_cast<unsigned long long>(cache_hit_count),
                          static_cast<unsigned>(cached_instances.size()),
                          1u);
            traceNativePatchEvent(buffer);

            std::snprintf(buffer,
                          sizeof(buffer),
                          "text-gpu-upload id=%s buf=%u uploads=%llu builds=%llu hits=%llu cells=%u",
                          pane.node_id.c_str(),
                          static_cast<unsigned>(buffer_index & 1u),
                          static_cast<unsigned long long>(gpu_upload_count),
                          static_cast<unsigned long long>(cache_build_count),
                          static_cast<unsigned long long>(cache_hit_count),
                          static_cast<unsigned>(cached_instances.size()));
            traceNativePatchEvent(buffer);
        }
    } else {
        cache_hit_count += 1;
        if (shouldTraceWorkspacePaneNodeId(pane.node_id) &&
            (cache_hit_count <= 3 || (cache_hit_count % 120u) == 0u)) {
            char buffer[512];
            std::snprintf(buffer,
                          sizeof(buffer),
                          "text-cache-hit id=%s buf=%u builds=%llu hits=%llu cells=%u",
                          pane.node_id.c_str(),
                          static_cast<unsigned>(buffer_index & 1u),
                          static_cast<unsigned long long>(cache_build_count),
                          static_cast<unsigned long long>(cache_hit_count),
                          static_cast<unsigned>(cached_instances.size()));
            traceNativePatchEvent(buffer);
        }
    }

    if (!wingui_begin_frame(pane.text_grid_context, 0.0f, 0.0f, 0.0f, 1.0f)) return false;
    WinguiTextGridFrame frame{};
    frame.instances = cached_instances.data();
    frame.instance_count = static_cast<uint32_t>(cached_instances.size());
    frame.uniforms.viewport_width = static_cast<float>(std::max(layout.width, 1));
    frame.uniforms.viewport_height = static_cast<float>(std::max(layout.height, 1));
    frame.uniforms.cell_width = host->atlas.info.cell_width;
    frame.uniforms.cell_height = host->atlas.info.cell_height;
    frame.uniforms.atlas_width = host->atlas.info.atlas_width;
    frame.uniforms.atlas_height = host->atlas.info.atlas_height;
    frame.uniforms.row_origin = 0.0f;
    frame.uniforms.effects_mode = 0.0f;
    if (!wingui_text_grid_renderer_render_uploaded(pane.pane_text_renderer, 0, 0, layout.width, layout.height, &frame.uniforms)) return false;
    return wingui_present(pane.text_grid_context, 1) != 0;
}

bool ensurePaneRgbaPresenter(SuperTerminalRuntimeHost* host,
                             RegisteredPane& pane,
                             const SuperTerminalPaneLayout& layout) {
    if (!host || pane.node_id.empty() || layout.width <= 0 || layout.height <= 0) return false;

    void* node_hwnd_raw = nullptr;
    if (!wingui_native_try_get_node_hwnd(pane.node_id.c_str(), &node_hwnd_raw) || !node_hwnd_raw) {
        return false;
    }

    const int32_t target_width = std::max(layout.width, 1);
    const int32_t target_height = std::max(layout.height, 1);
    const uint32_t required_surface_buffer_count =
        pane.rgba_content_buffer_mode == SUPERTERMINAL_RGBA_CONTENT_BUFFER_PERSISTENT ? 1u : 2u;
    if (pane.rgba_hwnd != node_hwnd_raw || !pane.rgba_context || !pane.pane_rgba_renderer ||
        !pane.rgba_surface_handle || !pane.pane_vector_renderer ||
        pane.rgba_surface_buffer_count != required_surface_buffer_count) {
        if (shouldTraceWorkspacePaneNodeId(pane.node_id)) {
            char buffer[512];
            std::snprintf(buffer,
                          sizeof(buffer),
                          "attach-rgba-presenter id=%s hwnd=%p old_hwnd=%p w=%d h=%d buffers=%u mode=%u",
                          pane.node_id.c_str(),
                          node_hwnd_raw,
                          pane.rgba_hwnd,
                          static_cast<int>(target_width),
                          static_cast<int>(target_height),
                          static_cast<unsigned>(required_surface_buffer_count),
                          static_cast<unsigned>(pane.rgba_content_buffer_mode));
            traceNativePatchEvent(buffer);
        }
        destroyPaneRgbaPresenter(pane);

        WinguiContextDesc context_desc{};
        context_desc.hwnd = node_hwnd_raw;
        context_desc.width = static_cast<uint32_t>(target_width);
        context_desc.height = static_cast<uint32_t>(target_height);
        context_desc.buffer_count = 2;
        context_desc.vsync_interval = 1;
        if (!wingui_create_context(&context_desc, &pane.rgba_context)) {
            destroyPaneRgbaPresenter(pane);
            return false;
        }

        WinguiRgbaPaneRendererDesc rgba_desc{};
        rgba_desc.context = pane.rgba_context;
        rgba_desc.shader_path_utf8 = kDefaultGraphicsShaderPath;
        rgba_desc.buffer_count = 2;
        if (!wingui_create_rgba_pane_renderer(&rgba_desc, &pane.pane_rgba_renderer)) {
            destroyPaneRgbaPresenter(pane);
            return false;
        }

        if (!wingui_create_rgba_surface(pane.rgba_context, required_surface_buffer_count, &pane.rgba_surface_handle)) {
            destroyPaneRgbaPresenter(pane);
            return false;
        }

        if (!wingui_create_vector_renderer(pane.rgba_context, nullptr, &pane.pane_vector_renderer)) {
            destroyPaneRgbaPresenter(pane);
            return false;
        }
        if (host->atlas.pixels_rgba && wingui_vector_renderer_set_glyph_atlas(pane.pane_vector_renderer, &host->atlas)) {
            pane.pane_vector_renderer_atlas_set = 1;
        }

        pane.rgba_hwnd = node_hwnd_raw;
        pane.rgba_surface_buffer_count = required_surface_buffer_count;
    }

    if (pane.pane_vector_renderer && !pane.pane_vector_renderer_atlas_set && host->atlas.pixels_rgba) {
        if (wingui_vector_renderer_set_glyph_atlas(pane.pane_vector_renderer, &host->atlas)) {
            pane.pane_vector_renderer_atlas_set = 1;
        }
    }

    wingui_resize_context(pane.rgba_context, static_cast<uint32_t>(target_width), static_cast<uint32_t>(target_height));
    pane.rgba_screen_width = static_cast<uint32_t>(target_width);
    pane.rgba_screen_height = static_cast<uint32_t>(target_height);
    pane.rgba_pixel_aspect_num = pane.rgba_pixel_aspect_num ? pane.rgba_pixel_aspect_num : 1u;
    pane.rgba_pixel_aspect_den = pane.rgba_pixel_aspect_den ? pane.rgba_pixel_aspect_den : 1u;
    if (shouldTraceWorkspacePaneNodeId(pane.node_id)) {
        char buffer[256];
        std::snprintf(buffer,
                      sizeof(buffer),
                      "resize-rgba-context id=%s hwnd=%p w=%d h=%d buffers=%u",
                      pane.node_id.c_str(),
                      pane.rgba_hwnd,
                      static_cast<int>(target_width),
                      static_cast<int>(target_height),
                      static_cast<unsigned>(pane.rgba_surface_buffer_count));
        traceNativePatchEvent(buffer);
    }
    return wingui_rgba_surface_ensure_buffers(
        pane.rgba_surface_handle,
        pane.rgba_screen_width,
        pane.rgba_screen_height) != 0;
}

void resizeHostWindowToNativeContent(SuperTerminalRuntimeHost* host) {
    if (!host || !host->window || !host->native_attached) return;

    int32_t desired_client_width = 0;
    int32_t desired_client_height = 0;
    if (!wingui_native_get_content_size(&desired_client_width, &desired_client_height)) return;
    if (desired_client_width <= 0 || desired_client_height <= 0) return;

    int32_t current_client_width = 0;
    int32_t current_client_height = 0;
    if (!wingui_window_client_size(host->window, &current_client_width, &current_client_height)) return;
    if (current_client_width == desired_client_width && current_client_height == desired_client_height) return;

    HWND hwnd = static_cast<HWND>(wingui_window_hwnd(host->window));
    if (!hwnd) return;

    RECT target_rect{0, 0, desired_client_width, desired_client_height};
    const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE));
    const DWORD ex_style = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_EXSTYLE));
    using AdjustWindowRectExForDpiFn = BOOL(WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);
    static auto adjust_for_dpi = reinterpret_cast<AdjustWindowRectExForDpiFn>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "AdjustWindowRectExForDpi"));
    using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
    static auto get_dpi_for_window = reinterpret_cast<GetDpiForWindowFn>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    if (adjust_for_dpi && get_dpi_for_window) {
        adjust_for_dpi(&target_rect, style, GetMenu(hwnd) != nullptr, ex_style, get_dpi_for_window(hwnd));
    } else {
        AdjustWindowRectEx(&target_rect, style, GetMenu(hwnd) != nullptr, ex_style);
    }
    int window_width = (target_rect.right - target_rect.left) > 0 ? static_cast<int>(target_rect.right - target_rect.left) : 1;
    int window_height = (target_rect.bottom - target_rect.top) > 0 ? static_cast<int>(target_rect.bottom - target_rect.top) : 1;

    MONITORINFO monitor_info{};
    monitor_info.cbSize = sizeof(monitor_info);
    const HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    if (monitor && GetMonitorInfoW(monitor, &monitor_info)) {
        const RECT work = monitor_info.rcWork;
        const int work_width = std::max(1, static_cast<int>(work.right - work.left));
        const int work_height = std::max(1, static_cast<int>(work.bottom - work.top));
        window_width = std::min(window_width, work_width);
        window_height = std::min(window_height, work_height);

        RECT current_rect{};
        if (GetWindowRect(hwnd, &current_rect)) {
            const int clamped_x = std::clamp(static_cast<int>(current_rect.left), static_cast<int>(work.left), static_cast<int>(work.right) - window_width);
            const int clamped_y = std::clamp(static_cast<int>(current_rect.top), static_cast<int>(work.top), static_cast<int>(work.bottom) - window_height);
            SetWindowPos(hwnd,
                nullptr,
                clamped_x,
                clamped_y,
                window_width,
                window_height,
                SWP_NOZORDER | SWP_NOACTIVATE);
            return;
        }
    }

    SetWindowPos(hwnd,
        nullptr,
        0,
        0,
        window_width,
        window_height,
        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

bool renderRgbaPane(SuperTerminalRuntimeHost* host,
                    RegisteredPane& pane,
                    const SuperTerminalPaneLayout& layout,
                    uint32_t buffer_index) {
    if (layout.visible == 0 || layout.width <= 0 || layout.height <= 0) return true;
    if (!ensurePaneRgbaPresenter(host, pane, layout)) return true;
    const uint32_t pane_buffer_index =
        pane.rgba_content_buffer_mode == SUPERTERMINAL_RGBA_CONTENT_BUFFER_PERSISTENT ? 0u : (buffer_index & 1u);
    if (!wingui_begin_frame(pane.rgba_context, 0.0f, 0.0f, 0.0f, 1.0f)) return false;
    const bool ok = wingui_rgba_surface_render(
        pane.pane_rgba_renderer,
        pane.rgba_surface_handle,
        0,
        0,
        layout.width,
        layout.height,
        pane.rgba_screen_width,
        pane.rgba_screen_height,
        pane.rgba_pixel_aspect_num,
        pane.rgba_pixel_aspect_den,
        pane_buffer_index,
        nullptr) != 0;
    if (!ok) return false;
    return wingui_present(pane.rgba_context, 1) != 0;
}

bool renderSurface(SuperTerminalRuntimeHost* host) {
    if (!host) return false;
    if (!host->native_attached && (!host->context || !host->text_renderer)) return false;

    bool parent_frame_started = false;
    bool parent_rendered_any = false;
    auto begin_parent_frame = [&]() -> bool {
        if (parent_frame_started) return true;
        if (!wingui_begin_frame(host->context, 0.0f, 0.0f, 0.0f, 1.0f)) {
            return false;
        }
        parent_frame_started = true;
        return true;
    };
    const uint32_t display_buffer_index = host->display_buffer_index.load(std::memory_order_acquire) & 1u;
    std::vector<SuperTerminalPaneId> pane_ids;
    {
        std::lock_guard<std::mutex> lock(host->pane_mutex);
        pane_ids.reserve(host->panes.size());
        for (const RegisteredPane& pane : host->panes) {
            pane_ids.push_back(pane.pane_id);
        }
    }

    for (SuperTerminalPaneId pane_id : pane_ids) {
        SuperTerminalPaneLayout layout{};
        if (!resolvePaneLayout(host, pane_id, &layout) || layout.visible == 0) {
            continue;
        }
        uint32_t render_kind = PANE_RENDER_TEXT_GRID;
        {
            std::lock_guard<std::mutex> lock(host->pane_mutex);
            if (RegisteredPane* pane = registeredPaneForIdLocked(host, pane_id, false)) {
                render_kind = pane->render_kind;
            }
        }

        if (render_kind == PANE_RENDER_INDEXED) {
            bool has_handle = false;
            bool ok = true;
            {
                std::lock_guard<std::mutex> lock(host->pane_mutex);
                if (RegisteredPane* pane = registeredPaneForIdLocked(host, pane_id, false)) {
                    has_handle = pane->indexed_surface_handle != nullptr;
                    if (has_handle) {
                        if (!begin_parent_frame()) return false;
                        ok = renderIndexedPane(host, *pane, layout, display_buffer_index);
                    }
                }
            }
            if (!ok) return false;
            parent_rendered_any = parent_rendered_any || has_handle;
            continue;
        }

        if (render_kind == PANE_RENDER_RGBA) {
            bool has_handle = false;
            bool ok = true;
            {
                std::lock_guard<std::mutex> lock(host->pane_mutex);
                if (RegisteredPane* pane = registeredPaneForIdLocked(host, pane_id, false)) {
                    has_handle = pane->rgba_surface_handle != nullptr;
                    if (has_handle) {
                        ok = renderRgbaPane(host, *pane, layout, display_buffer_index);
                    }
                }
            }
            if (!ok) return false;
            continue;
        }

        TerminalSurface* surface = textGridSurfaceForPaneBuffer(host, pane_id, false, display_buffer_index);
        if (!surface) continue;
        if ((surface->columns != layout.columns || surface->rows != layout.rows) && layout.columns > 0 && layout.rows > 0) {
            std::lock_guard<std::mutex> lock(host->pane_mutex);
            if (BufferedTerminalSurface* surface_set = textGridSurfaceSetForPaneLocked(host, pane_id, false)) {
                resizeBufferedSurfacePreserve(surface_set, layout.columns, layout.rows);
                if (RegisteredPane* pane = registeredPaneForIdLocked(host, pane_id, false)) {
                    pane->text_grid_cache_dirty[0] = true;
                    pane->text_grid_cache_dirty[1] = true;
                }
                surface = &surface_set->buffers[display_buffer_index];
            }
        }
        if (host->native_attached) {
            bool ok = true;
            {
                std::lock_guard<std::mutex> lock(host->pane_mutex);
                if (RegisteredPane* pane = registeredPaneForIdLocked(host, pane_id, false)) {
                    ok = renderTextGridPane(host, *pane, surface, layout, display_buffer_index);
                }
            }
            if (!ok) return false;
        } else {
            if (!begin_parent_frame()) return false;
            if (!renderTextGridSurface(host, surface, layout)) {
                return false;
            }
            parent_rendered_any = true;
        }
    }

    if (!parent_rendered_any && !host->native_attached) {
        if (!begin_parent_frame()) return false;
        SuperTerminalPaneLayout root_layout{};
        if (!resolvePaneLayout(host, SuperTerminalPaneId{}, &root_layout)) {
            return false;
        }
        TerminalSurface* root_surface = &host->surface.buffers[display_buffer_index];
        if ((root_surface->columns != root_layout.columns || root_surface->rows != root_layout.rows) && root_layout.columns > 0 && root_layout.rows > 0) {
            std::lock_guard<std::mutex> lock(host->pane_mutex);
            resizeBufferedSurfacePreserve(&host->surface, root_layout.columns, root_layout.rows);
            root_surface = &host->surface.buffers[display_buffer_index];
        }
        if (!renderTextGridSurface(host, root_surface, root_layout)) {
            return false;
        }
    }

    if (parent_frame_started) {
        if (!wingui_present(host->context, 1)) {
            return false;
        }
        if (HWND native_hwnd = static_cast<HWND>(wingui_native_host_hwnd())) {
            SetWindowPos(native_hwnd, HWND_TOP, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        }
    }
    host->render_dirty.store(0, std::memory_order_release);
    return true;
}

int64_t WINGUI_CALL nativeDispatchEventJson(const char* event_json_utf8) {
    if (!g_active_host || !event_json_utf8) return 0;
    SuperTerminalEvent event{};
    event.type = SUPERTERMINAL_EVENT_NATIVE_UI;
    event.sequence = g_active_host->event_sequence.fetch_add(1, std::memory_order_relaxed) + 1;
    copyUtf8Truncate(event.data.native_ui.payload_json_utf8, sizeof(event.data.native_ui.payload_json_utf8), event_json_utf8);
    return pushEvent(g_active_host, event) ? 1 : 0;
}

void applyCommand(SuperTerminalRuntimeHost* host, const SuperTerminalCommand& command) {
    if (!host) return;
    switch (command.type) {
        case SUPERTERMINAL_CMD_NATIVE_UI_PUBLISH:
            if (command.data.native_ui_publish.json_utf8) {
                if (!wingui_native_publish_json(command.data.native_ui_publish.json_utf8)) {
                }
                wingui_native_host_run();
                resizeHostWindowToNativeContent(host);
                syncActivePaneFromDeclarativeFocus(host);
                free(const_cast<char*>(command.data.native_ui_publish.json_utf8));
            }
            break;
        case SUPERTERMINAL_CMD_NATIVE_UI_PATCH:
            if (command.data.native_ui_patch.patch_json_utf8) {
                if (!wingui_native_patch_json(command.data.native_ui_patch.patch_json_utf8)) {
                }
                syncActivePaneFromDeclarativeFocus(host);
                free(const_cast<char*>(command.data.native_ui_patch.patch_json_utf8));
            }
            break;
        case SUPERTERMINAL_CMD_WINDOW_SET_TITLE:
            wingui_window_set_title_utf8(host->window, command.data.set_title.title_utf8);
            break;
        case SUPERTERMINAL_CMD_TEXT_GRID_WRITE_CELLS:
            writeCellsToMirroredSurface(host, command.data.text_grid_write_cells);
            free(const_cast<SuperTerminalTextGridCell*>(command.data.text_grid_write_cells.cells));
            host->render_dirty.store(1, std::memory_order_release);
            break;
        case SUPERTERMINAL_CMD_TEXT_GRID_CLEAR_REGION:
            clearMirroredSurfaceRegion(host, command.data.text_grid_clear_region);
            host->render_dirty.store(1, std::memory_order_release);
            break;
        case SUPERTERMINAL_CMD_REQUEST_PRESENT:
            host->render_dirty.store(1, std::memory_order_release);
            break;
        case SUPERTERMINAL_CMD_REQUEST_CLOSE:
            requestStopInternal(host, host->exit_code, true);
            break;
        case SUPERTERMINAL_CMD_RGBA_UPLOAD_OWNED: {
            const SuperTerminalRgbaUploadOwned& up = command.data.rgba_upload_owned;
            void* owned = up.bgra8_pixels;
            if (host->context && host->rgba_renderer && up.pane_id.value != 0 && owned &&
                up.surface_width > 0 && up.surface_height > 0 && up.region_width > 0 && up.region_height > 0) {
                WinguiRgbaSurface* handle = nullptr;
                {
                    std::lock_guard<std::mutex> lock(host->pane_mutex);
                    RegisteredPane* pane = ensureRegisteredPaneLocked(host, up.pane_id);
                    if (pane) {
                        pane->render_kind = PANE_RENDER_RGBA;
                        if (!pane->rgba_surface_handle) {
                            wingui_create_rgba_surface(host->context, 2, &pane->rgba_surface_handle);
                        }
                        handle = pane->rgba_surface_handle;
                        pane->rgba_screen_width = up.screen_width ? up.screen_width : up.surface_width;
                        pane->rgba_screen_height = up.screen_height ? up.screen_height : up.surface_height;
                        pane->rgba_pixel_aspect_num = up.pixel_aspect_num ? up.pixel_aspect_num : 1u;
                        pane->rgba_pixel_aspect_den = up.pixel_aspect_den ? up.pixel_aspect_den : 1u;
                    }
                }
                if (handle) {
                    if (wingui_rgba_surface_ensure_buffers(handle, up.surface_width, up.surface_height)) {
                        WinguiRectU32 region{ up.dst_x, up.dst_y, up.region_width, up.region_height };
                        wingui_rgba_surface_upload_bgra8_region(handle, up.buffer_index & 1u, region, static_cast<const uint8_t*>(owned), up.source_pitch);
                    }
                }
                host->render_dirty.store(1, std::memory_order_release);
            }
            if (owned) {
                if (up.free_fn) {
                    up.free_fn(up.free_user_data, owned);
                } else {
                    delete[] static_cast<uint8_t*>(owned);
                }
            }
            break;
        }
        case SUPERTERMINAL_CMD_FRAME_SWAP:
            swapDisplayedBuffers(host);
            host->render_dirty.store(1, std::memory_order_release);
            break;
        case SUPERTERMINAL_CMD_RGBA_GPU_COPY: {
            const SuperTerminalRgbaGpuCopy& cp = command.data.rgba_gpu_copy;
            if (cp.region_width == 0 || cp.region_height == 0 ||
                cp.dst_pane_id.value == 0 || cp.src_pane_id.value == 0) {
                break;
            }
            WinguiRgbaSurface* dst_handle = nullptr;
            WinguiRgbaSurface* src_handle = nullptr;
            {
                std::lock_guard<std::mutex> lock(host->pane_mutex);
                if (RegisteredPane* dst_pane = registeredPaneForIdLocked(host, cp.dst_pane_id, false)) {
                    dst_handle = dst_pane->rgba_surface_handle;
                }
                if (RegisteredPane* src_pane = registeredPaneForIdLocked(host, cp.src_pane_id, false)) {
                    src_handle = src_pane->rgba_surface_handle;
                }
            }
            if (dst_handle && src_handle) {
                WinguiRectU32 region{ cp.src_x, cp.src_y, cp.region_width, cp.region_height };
                wingui_rgba_surface_copy_from_surface(
                    dst_handle,
                    cp.dst_buffer_index & 1u,
                    cp.dst_x,
                    cp.dst_y,
                    src_handle,
                    cp.src_buffer_index & 1u,
                    region);
                host->render_dirty.store(1, std::memory_order_release);
            }
            break;
        }
        case SUPERTERMINAL_CMD_RGBA_ASSET_REGISTER_OWNED: {
            const SuperTerminalRgbaAssetRegisterOwned& reg = command.data.rgba_asset_register_owned;
            void* owned = reg.bgra8_pixels;
            const uint32_t pitch = reg.source_pitch ? reg.source_pitch : reg.width * 4u;
            if (host->context && reg.asset_id.value != 0 && owned &&
                reg.width > 0 && reg.height > 0 && pitch >= reg.width * 4u) {
                WinguiRgbaSurface* asset = nullptr;
                if (wingui_create_rgba_surface(host->context, 1, &asset) && asset) {
                    if (wingui_rgba_surface_ensure_buffers(asset, reg.width, reg.height)) {
                        WinguiRectU32 region{ 0, 0, reg.width, reg.height };
                        wingui_rgba_surface_upload_bgra8_region(asset, 0, region, static_cast<const uint8_t*>(owned), pitch);
                        std::lock_guard<std::mutex> lock(host->asset_mutex);
                        auto& slot = host->rgba_assets[reg.asset_id.value];
                        if (slot) {
                            wingui_destroy_rgba_surface(slot);
                        }
                        slot = asset;
                    } else {
                        wingui_destroy_rgba_surface(asset);
                    }
                }
            }
            if (owned) {
                if (reg.free_fn) {
                    reg.free_fn(reg.free_user_data, owned);
                } else {
                    delete[] static_cast<uint8_t*>(owned);
                }
            }
            break;
        }
        case SUPERTERMINAL_CMD_RGBA_ASSET_BLIT_TO_PANE: {
            const SuperTerminalRgbaAssetBlitToPane& blit = command.data.rgba_asset_blit_to_pane;
            if (blit.region_width == 0 || blit.region_height == 0 ||
                blit.asset_id.value == 0 || blit.dst_pane_id.value == 0) {
                break;
            }
            WinguiRgbaSurface* asset = nullptr;
            {
                std::lock_guard<std::mutex> lock(host->asset_mutex);
                auto it = host->rgba_assets.find(blit.asset_id.value);
                if (it != host->rgba_assets.end()) asset = it->second;
            }
            WinguiRgbaSurface* dst_handle = nullptr;
            {
                std::lock_guard<std::mutex> lock(host->pane_mutex);
                if (RegisteredPane* dst_pane = registeredPaneForIdLocked(host, blit.dst_pane_id, false)) {
                    dst_handle = dst_pane->rgba_surface_handle;
                }
            }
            if (asset && dst_handle) {
                WinguiRectU32 region{ blit.src_x, blit.src_y, blit.region_width, blit.region_height };
                wingui_rgba_surface_copy_from_surface(
                    dst_handle,
                    blit.dst_buffer_index & 1u,
                    blit.dst_x,
                    blit.dst_y,
                    asset,
                    0,
                    region);
                host->render_dirty.store(1, std::memory_order_release);
            }
            break;
        }
        case SUPERTERMINAL_CMD_INDEXED_UPLOAD_OWNED: {
            const SuperTerminalIndexedUploadOwned& up = command.data.indexed_upload_owned;
            void* owned_pixels = up.indexed_pixels;
            void* owned_lines = up.line_palettes;
            void* owned_global = up.global_palette;
            if (host->context && host->indexed_renderer && up.pane_id.value != 0 &&
                owned_pixels && owned_lines && owned_global &&
                up.buffer_width > 0 && up.buffer_height > 0 && up.global_palette_count > 0) {
                WinguiIndexedSurface* handle = nullptr;
                {
                    std::lock_guard<std::mutex> lock(host->pane_mutex);
                    RegisteredPane* pane = ensureRegisteredPaneLocked(host, up.pane_id);
                    if (pane) {
                        pane->render_kind = PANE_RENDER_INDEXED;
                        if (!pane->indexed_surface_handle) {
                            wingui_create_indexed_surface(host->context, 2, &pane->indexed_surface_handle);
                        }
                        handle = pane->indexed_surface_handle;
                        pane->indexed_screen_width = up.screen_width ? up.screen_width : up.buffer_width;
                        pane->indexed_screen_height = up.screen_height ? up.screen_height : up.buffer_height;
                        pane->indexed_scroll_x = up.scroll_x;
                        pane->indexed_scroll_y = up.scroll_y;
                        pane->indexed_pixel_aspect_num = up.pixel_aspect_num ? up.pixel_aspect_num : 1u;
                        pane->indexed_pixel_aspect_den = up.pixel_aspect_den ? up.pixel_aspect_den : 1u;
                    }
                }
                if (handle && wingui_indexed_surface_ensure_buffers(handle, up.buffer_width, up.buffer_height)) {
                    const uint32_t buf_idx = up.buffer_index & 1u;
                    WinguiRectU32 region{ 0, 0, up.buffer_width, up.buffer_height };
                    wingui_indexed_surface_upload_pixels_region(handle, buf_idx, region,
                        static_cast<const uint8_t*>(owned_pixels), up.buffer_width);
                    wingui_indexed_surface_upload_line_palettes(handle, buf_idx, 0, up.buffer_height,
                        static_cast<const WinguiGraphicsLinePalette*>(owned_lines));
                    wingui_indexed_surface_upload_global_palette(handle, buf_idx, 0, up.global_palette_count,
                        static_cast<const WinguiGraphicsColour*>(owned_global));
                }
                host->render_dirty.store(1, std::memory_order_release);
            }
            auto free_one = [&](void* p) {
                if (!p) return;
                if (up.free_fn) up.free_fn(up.free_user_data, p);
                else delete[] static_cast<uint8_t*>(p);
            };
            free_one(owned_pixels);
            free_one(owned_lines);
            free_one(owned_global);
            break;
        }
        case SUPERTERMINAL_CMD_SPRITE_DEFINE_OWNED: {
            const SuperTerminalSpriteDefineOwned& def = command.data.sprite_define_owned;
            void* owned_pixels = def.pixels;
            void* owned_palette = def.palette;
            auto free_blobs = [&]() {
                if (def.free_fn) {
                    if (owned_pixels) def.free_fn(def.free_user_data, owned_pixels);
                    if (owned_palette) def.free_fn(def.free_user_data, owned_palette);
                } else {
                    delete[] static_cast<uint8_t*>(owned_pixels);
                    delete[] static_cast<uint8_t*>(owned_palette);
                }
            };
            if (host->indexed_renderer && def.pane_id.value != 0 && def.sprite_id.value != 0 &&
                owned_pixels && owned_palette && def.frame_w > 0 && def.frame_h > 0 && def.frame_count > 0) {
                const uint32_t strip_w = def.frame_w * def.frame_count;
                uint32_t atlas_x = 0, atlas_y = 0;
                SpriteBankEntry* existing = nullptr;
                {
                    std::lock_guard<std::mutex> lock(host->pane_mutex);
                    RegisteredPane* pane = ensureRegisteredPaneLocked(host, def.pane_id);
                    if (pane) {
                        for (SpriteBankEntry& e : pane->sprite_bank) {
                            if (e.sprite_id == def.sprite_id.value) { existing = &e; break; }
                        }
                        if (!existing) {
                            pane->sprite_bank.push_back(SpriteBankEntry{});
                            existing = &pane->sprite_bank.back();
                            existing->sprite_id = def.sprite_id.value;
                        }
                        /* Reuse existing atlas region if same dimensions, else allocate new. */
                        if (!existing->uploaded ||
                            existing->atlas_entry.frame_w != def.frame_w ||
                            existing->atlas_entry.frame_h != def.frame_h ||
                            existing->atlas_entry.frame_count != def.frame_count) {
                            if (!host->sprite_atlas_packer.alloc(strip_w, def.frame_h, atlas_x, atlas_y)) {
                                existing = nullptr; /* atlas full */
                            } else {
                                existing->atlas_entry.atlas_x = atlas_x;
                                existing->atlas_entry.atlas_y = atlas_y;
                                existing->atlas_entry.width = strip_w;
                                existing->atlas_entry.height = def.frame_h;
                                existing->atlas_entry.frame_w = def.frame_w;
                                existing->atlas_entry.frame_h = def.frame_h;
                                existing->atlas_entry.frame_count = def.frame_count;
                                /* palette row = index into sprite_bank */
                                existing->atlas_entry.palette_offset = static_cast<uint32_t>(
                                    existing - pane->sprite_bank.data());
                                existing->frames_per_tick = def.frames_per_tick;
                                existing->uploaded = false;
                            }
                        } else {
                            existing->frames_per_tick = def.frames_per_tick;
                            atlas_x = existing->atlas_entry.atlas_x;
                            atlas_y = existing->atlas_entry.atlas_y;
                        }
                    }
                }
                if (existing) {
                    wingui_indexed_graphics_upload_sprite_atlas_region(
                        host->indexed_renderer,
                        atlas_x, atlas_y,
                        strip_w, def.frame_h,
                        static_cast<const uint8_t*>(owned_pixels),
                        strip_w);
                    WinguiGraphicsLinePalette palette_row = *static_cast<const WinguiGraphicsLinePalette*>(owned_palette);
                    wingui_indexed_graphics_upload_sprite_palettes(
                        host->indexed_renderer, &palette_row, 1);
                    {
                        std::lock_guard<std::mutex> lock(host->pane_mutex);
                        if (RegisteredPane* pane = registeredPaneForIdLocked(host, def.pane_id, false)) {
                            for (SpriteBankEntry& e : pane->sprite_bank) {
                                if (e.sprite_id == def.sprite_id.value) { e.uploaded = true; break; }
                            }
                        }
                    }
                }
            }
            free_blobs();
            break;
        }
        case SUPERTERMINAL_CMD_SPRITE_RENDER: {
            const SuperTerminalSpriteRender& sr = command.data.sprite_render;
            void* owned_instances = sr.instances;
            if (host->indexed_renderer && sr.pane_id.value != 0 && owned_instances && sr.instance_count > 0) {
                SuperTerminalPaneLayout layout{};
                std::vector<WinguiSpriteAtlasEntry> atlas_entries;
                std::vector<WinguiSpriteInstance> wingui_instances;
                {
                    std::lock_guard<std::mutex> lock(host->pane_mutex);
                    RegisteredPane* pane = registeredPaneForIdLocked(host, sr.pane_id, false);
                    if (pane && !pane->sprite_bank.empty()) {
                        /* Build a contiguous atlas_entries table indexed by sprite_bank index.
                           Build wingui instance list resolving sprite_id → atlas_entry_id. */
                        atlas_entries.resize(pane->sprite_bank.size());
                        for (size_t i = 0; i < pane->sprite_bank.size(); ++i) {
                            atlas_entries[i] = pane->sprite_bank[i].atlas_entry;
                        }
                        const auto* src = static_cast<const SuperTerminalSpriteInstance*>(owned_instances);
                        wingui_instances.reserve(sr.instance_count);
                        for (uint32_t i = 0; i < sr.instance_count; ++i) {
                            const SuperTerminalSpriteInstance& si = src[i];
                            if (!si.sprite_id.value) continue;
                            uint32_t bank_idx = UINT32_MAX;
                            uint32_t frames_per_tick = 0;
                            for (size_t j = 0; j < pane->sprite_bank.size(); ++j) {
                                if (pane->sprite_bank[j].sprite_id == si.sprite_id.value &&
                                    pane->sprite_bank[j].uploaded) {
                                    bank_idx = static_cast<uint32_t>(j);
                                    frames_per_tick = pane->sprite_bank[j].frames_per_tick;
                                    break;
                                }
                            }
                            if (bank_idx == UINT32_MAX) continue;
                            WinguiSpriteInstance wi{};
                            wi.x = si.x;
                            wi.y = si.y;
                            wi.rotation = si.rotation;
                            wi.scale_x = si.scale_x > 0.0f ? si.scale_x : 1.0f;
                            wi.scale_y = si.scale_y > 0.0f ? si.scale_y : 1.0f;
                            wi.anchor_x = si.anchor_x;
                            wi.anchor_y = si.anchor_y;
                            wi.atlas_entry_id = bank_idx;
                            wi.frame = (frames_per_tick > 0)
                                ? static_cast<uint32_t>(sr.sprite_tick / frames_per_tick)
                                : 0u;
                            wi.flags = si.flags | WINGUI_SPRITE_FLAG_VISIBLE;
                            wi.alpha = si.alpha > 0.0f ? si.alpha : 1.0f;
                            wi.effect_type = si.effect_type;
                            wi.effect_param1 = si.effect_param1;
                            wi.effect_param2 = si.effect_param2;
                            std::memcpy(wi.effect_colour, si.effect_colour, 4);
                            wi.palette_override = si.palette_override;
                            wingui_instances.push_back(wi);
                        }
                    }
                    resolvePaneLayout(host, sr.pane_id, &layout);
                }
                if (!wingui_instances.empty() && !atlas_entries.empty()) {
                    const uint32_t tw = sr.target_width ? sr.target_width : static_cast<uint32_t>(std::max(0, layout.width));
                    const uint32_t th = sr.target_height ? sr.target_height : static_cast<uint32_t>(std::max(0, layout.height));
                    WinguiIndexedPaneLayout pane_layout{};
                    pane_layout.origin_x = static_cast<float>(layout.x);
                    pane_layout.origin_y = static_cast<float>(layout.y);
                    pane_layout.shown_width = static_cast<float>(layout.width);
                    pane_layout.shown_height = static_cast<float>(layout.height);
                    pane_layout.scale_x = tw > 0 ? static_cast<float>(layout.width) / static_cast<float>(tw) : 1.0f;
                    pane_layout.scale_y = th > 0 ? static_cast<float>(layout.height) / static_cast<float>(th) : 1.0f;
                    wingui_indexed_graphics_render_sprites(
                        host->indexed_renderer,
                        tw, th,
                        &pane_layout,
                        atlas_entries.data(),
                        static_cast<uint32_t>(atlas_entries.size()),
                        wingui_instances.data(),
                        static_cast<uint32_t>(wingui_instances.size()));
                    host->render_dirty.store(1, std::memory_order_release);
                }
            }
            if (owned_instances) {
                if (sr.free_fn) sr.free_fn(sr.free_user_data, owned_instances);
                else delete[] static_cast<uint8_t*>(owned_instances);
            }
            break;
        }
        case SUPERTERMINAL_CMD_VECTOR_DRAW_OWNED: {
            const SuperTerminalVectorDrawOwned& vd = command.data.vector_draw_owned;
            void* owned_prims = vd.primitives;
            auto free_prims = [&]() {
                if (!owned_prims) return;
                if (vd.free_fn) vd.free_fn(vd.free_user_data, owned_prims);
                else delete[] static_cast<uint8_t*>(owned_prims);
            };
            if (vd.pane_id.value == 0) { free_prims(); break; }

            WinguiRgbaSurface* surface = nullptr;
            uint32_t surface_w = 0;
            uint32_t surface_h = 0;
            uint32_t target_buffer_index = 0;
            SuperTerminalPaneLayout layout{};
            resolvePaneLayout(host, vd.pane_id, &layout);
            WinguiVectorRenderer* vector_renderer = nullptr;
            {
                std::lock_guard<std::mutex> lock(host->pane_mutex);
                RegisteredPane* pane = ensureRegisteredPaneLocked(host, vd.pane_id);
                if (pane) {
                    pane->render_kind = PANE_RENDER_RGBA;
                    pane->rgba_content_buffer_mode = vd.content_buffer_mode;
                    if (ensurePaneRgbaPresenter(host, *pane, layout)) {
                        surface = pane->rgba_surface_handle;
                        surface_w = pane->rgba_screen_width;
                        surface_h = pane->rgba_screen_height;
                        vector_renderer = pane->pane_vector_renderer;
                        target_buffer_index =
                            pane->rgba_content_buffer_mode == SUPERTERMINAL_RGBA_CONTENT_BUFFER_PERSISTENT ? 0u : (vd.buffer_index & 1u);
                    }
                }
            }
            if (!surface) {
                free_prims();
                break;
            }

            if (surface_w == 0 || surface_h == 0) {
                free_prims();
                break;
            }
            if (!vector_renderer) { free_prims(); break; }

            if (vd.clear_before) {
                wingui_rgba_surface_clear(
                    surface, target_buffer_index,
                    vd.clear_color[0], vd.clear_color[1],
                    vd.clear_color[2], vd.clear_color[3]);
            }
            if (vd.primitive_count > 0 && owned_prims) {
                wingui_vector_renderer_render(
                    vector_renderer,
                    surface,
                    target_buffer_index,
                    static_cast<const WinguiVectorPrimitive*>(owned_prims),
                    vd.primitive_count,
                    vd.blend_mode);
            }
            host->render_dirty.store(1, std::memory_order_release);
            free_prims();
            break;
        }
        case SUPERTERMINAL_CMD_INDEXED_FILL_RECT: {
            const SuperTerminalIndexedFillRect& fr = command.data.indexed_fill_rect;
            if (!host->context || fr.pane_id.value == 0 || fr.width == 0 || fr.height == 0) break;

            WinguiIndexedSurface* surface = nullptr;
            {
                std::lock_guard<std::mutex> lock(host->pane_mutex);
                RegisteredPane* pane = registeredPaneForIdLocked(host, fr.pane_id, false);
                if (pane) surface = pane->indexed_surface_handle;
            }
            if (!surface) break;

            /* Lazy-create the fill renderer */
            if (!host->indexed_fill_renderer && !host->indexed_fill_renderer_init_attempted) {
                host->indexed_fill_renderer_init_attempted = 1;
                wingui_create_indexed_fill_renderer(host->context, nullptr, &host->indexed_fill_renderer);
            }
            if (!host->indexed_fill_renderer) break;

            wingui_indexed_surface_fill_rect(
                host->indexed_fill_renderer, surface,
                fr.buffer_index, fr.x, fr.y, fr.width, fr.height, fr.palette_index);
            host->render_dirty.store(1, std::memory_order_release);
            break;
        }
        case SUPERTERMINAL_CMD_INDEXED_DRAW_LINE: {
            const SuperTerminalIndexedDrawLine& ln = command.data.indexed_draw_line;
            if (!host->context || ln.pane_id.value == 0) break;

            WinguiIndexedSurface* surface = nullptr;
            {
                std::lock_guard<std::mutex> lock(host->pane_mutex);
                RegisteredPane* pane = registeredPaneForIdLocked(host, ln.pane_id, false);
                if (pane) surface = pane->indexed_surface_handle;
            }
            if (!surface) break;

            if (!host->indexed_fill_renderer && !host->indexed_fill_renderer_init_attempted) {
                host->indexed_fill_renderer_init_attempted = 1;
                wingui_create_indexed_fill_renderer(host->context, nullptr, &host->indexed_fill_renderer);
            }
            if (!host->indexed_fill_renderer) break;

            wingui_indexed_surface_draw_line(
                host->indexed_fill_renderer, surface,
                ln.buffer_index, ln.x0, ln.y0, ln.x1, ln.y1, ln.palette_index);
            host->render_dirty.store(1, std::memory_order_release);
            break;
        }
        case SUPERTERMINAL_CMD_NOP:
        default:
            break;
    }
}

void drainCommands(SuperTerminalRuntimeHost* host) {
    if (!host) return;
    SuperTerminalCommand command{};
    while (host->command_queue.pop(&command)) {
        applyCommand(host, command);
    }
}

intptr_t WINGUI_CALL hostWindowProc(
    WinguiWindow* window,
    void* user_data,
    uint32_t message,
    uintptr_t wparam,
    intptr_t lparam,
    int32_t* handled) {
    auto* host = static_cast<SuperTerminalRuntimeHost*>(user_data);
    if (!host || !handled) {
        return 0;
    }

    switch (message) {
        case WM_ERASEBKGND:
            *handled = 1;
            return 1;
        case WM_COMMAND:
            if (lparam == 0 && wingui_native_handle_host_command(static_cast<int32_t>(LOWORD(wparam))) != 0) {
                *handled = 1;
                return 0;
            }
            break;
        case WM_CLOSE: {
            if (host->close_event_sent.exchange(1, std::memory_order_acq_rel) == 0) {
                SuperTerminalEvent event{};
                event.type = SUPERTERMINAL_EVENT_CLOSE_REQUESTED;
                event.sequence = host->event_sequence.fetch_add(1, std::memory_order_relaxed) + 1;
                pushEvent(host, event);
            }
            requestStopInternal(host, host->exit_code, false);
            DestroyWindow(static_cast<HWND>(wingui_window_hwnd(window)));
            *handled = 1;
            return 0;
        }
        case WM_DESTROY:
            PostQuitMessage(host->exit_code);
            *handled = 1;
            return 0;
        case WM_SIZE:
            if (wparam != SIZE_MINIMIZED && host->context) {
                const uint32_t width = static_cast<uint32_t>(LOWORD(lparam));
                const uint32_t height = static_cast<uint32_t>(HIWORD(lparam));
                wingui_resize_context(host->context, width, height);
                wingui_native_set_host_bounds(0, 0, static_cast<int32_t>(width), static_cast<int32_t>(height));
                if (HWND native_hwnd = static_cast<HWND>(wingui_native_host_hwnd())) {
                    SetWindowPos(native_hwnd, HWND_TOP, 0, 0, static_cast<int32_t>(width), static_cast<int32_t>(height),
                        SWP_SHOWWINDOW);
                }
                updateSurfaceForClientSize(host);
            }
            break;
        case WM_SETFOCUS:
        case WM_KILLFOCUS: {
            host->window_focused.store(message == WM_SETFOCUS ? 1 : 0, std::memory_order_release);
            SuperTerminalEvent event{};
            event.type = SUPERTERMINAL_EVENT_FOCUS;
            event.sequence = host->event_sequence.fetch_add(1, std::memory_order_relaxed) + 1;
            event.data.focus.focused = message == WM_SETFOCUS ? 1 : 0;
            pushEvent(host, event);
            pushPaneFocusEvent(host, host->active_pane_id, message == WM_SETFOCUS ? 1 : 0);
            break;
        }
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYUP: {
            SuperTerminalEvent event{};
            event.type = SUPERTERMINAL_EVENT_KEY;
            event.sequence = host->event_sequence.fetch_add(1, std::memory_order_relaxed) + 1;
            event.data.key.virtual_key = static_cast<uint32_t>(wparam);
            event.data.key.repeat_count = static_cast<uint32_t>(lparam & 0xffffu);
            event.data.key.is_down = (message == WM_KEYDOWN || message == WM_SYSKEYDOWN) ? 1 : 0;
            event.data.key.modifiers = currentModifiers();
            pushEvent(host, event);
            pushPaneKeyEvent(host, host->active_pane_id, event.data.key);
            break;
        }
        case WM_CHAR:
        case WM_SYSCHAR: {
            SuperTerminalEvent event{};
            event.type = SUPERTERMINAL_EVENT_CHAR;
            event.sequence = host->event_sequence.fetch_add(1, std::memory_order_relaxed) + 1;
            event.data.character.codepoint = static_cast<uint32_t>(wparam);
            pushEvent(host, event);
            break;
        }
        case WM_MOUSEMOVE:
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
        case WM_XBUTTONDOWN:
        case WM_XBUTTONUP:
        case WM_MOUSEWHEEL: {
            WinguiMouseState mouse_state{};
            if (wingui_window_get_mouse_state(window, &mouse_state)) {
                const HWND hwnd = static_cast<HWND>(wingui_window_hwnd(window));
                const POINT mouse_point = mousePointForMessage(message, lparam, mouse_state, hwnd);
                const uint32_t modifiers = currentModifiers();
                SuperTerminalEvent event{};
                event.type = SUPERTERMINAL_EVENT_MOUSE;
                event.sequence = host->event_sequence.fetch_add(1, std::memory_order_relaxed) + 1;
                event.data.mouse.x = mouse_point.x;
                event.data.mouse.y = mouse_point.y;
                event.data.mouse.buttons = mouse_state.buttons;
                event.data.mouse.wheel_delta = message == WM_MOUSEWHEEL ? GET_WHEEL_DELTA_WPARAM(wparam) : 0;
                event.data.mouse.button_mask = 0;
                event.data.mouse.kind = SUPERTERMINAL_MOUSE_MOVE;
                if (message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN || message == WM_MBUTTONDOWN || message == WM_XBUTTONDOWN) {
                    event.data.mouse.kind = SUPERTERMINAL_MOUSE_BUTTON_DOWN;
                } else if (message == WM_LBUTTONUP || message == WM_RBUTTONUP || message == WM_MBUTTONUP || message == WM_XBUTTONUP) {
                    event.data.mouse.kind = SUPERTERMINAL_MOUSE_BUTTON_UP;
                } else if (message == WM_MOUSEWHEEL) {
                    event.data.mouse.kind = SUPERTERMINAL_MOUSE_WHEEL;
                }
                switch (message) {
                    case WM_LBUTTONDOWN:
                    case WM_LBUTTONUP:
                        event.data.mouse.button_mask = WINGUI_MOUSE_BUTTON_LEFT;
                        break;
                    case WM_RBUTTONDOWN:
                    case WM_RBUTTONUP:
                        event.data.mouse.button_mask = WINGUI_MOUSE_BUTTON_RIGHT;
                        break;
                    case WM_MBUTTONDOWN:
                    case WM_MBUTTONUP:
                        event.data.mouse.button_mask = WINGUI_MOUSE_BUTTON_MIDDLE;
                        break;
                    case WM_XBUTTONDOWN:
                    case WM_XBUTTONUP:
                        event.data.mouse.button_mask = GET_XBUTTON_WPARAM(wparam) == XBUTTON1 ? WINGUI_MOUSE_BUTTON_X1 : WINGUI_MOUSE_BUTTON_X2;
                        break;
                    default:
                        break;
                }
                pushEvent(host, event);

                const SuperTerminalPaneId hit_pane = hitTestPane(host, mouse_point.x, mouse_point.y);
                if (message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN || message == WM_MBUTTONDOWN || message == WM_XBUTTONDOWN) {
                    setActivePane(host, hit_pane);
                }
                pushPaneMouseEvent(host, hit_pane, event.data.mouse, modifiers);
            }
            break;
        }
        default:
            if (message == kSuperTerminalWakeMessage) {
                *handled = 1;
                return 0;
            }
            break;
    }

    *handled = 0;
    return 0;
}

bool initWindowAndRenderer(SuperTerminalRuntimeHost* host) {
    if (!host) return false;

    WinguiGlyphAtlasDesc atlas_desc{};
    atlas_desc.font_family_utf8 = host->desc.font_family_utf8 && *host->desc.font_family_utf8 ? host->desc.font_family_utf8 : "Consolas";
    atlas_desc.font_pixel_height = host->desc.font_pixel_height > 0 ? host->desc.font_pixel_height : 16;
    atlas_desc.dpi_scale = host->desc.dpi_scale > 0.0f ? host->desc.dpi_scale : 1.0f;
    atlas_desc.first_codepoint = 32;
    atlas_desc.glyph_count = 95;
    atlas_desc.cols = 16;
    atlas_desc.rows = 6;
    if (!wingui_build_glyph_atlas_utf8(&atlas_desc, &host->atlas)) {
        setHostError(host, SUPERTERMINAL_HOST_ERROR_GLYPH_ATLAS_CREATE, wingui_last_error_utf8());
        return false;
    }

    RECT rect{0, 0,
        static_cast<LONG>(std::max<uint32_t>(1, host->desc.columns) * static_cast<uint32_t>(host->atlas.info.cell_width)),
        static_cast<LONG>(std::max<uint32_t>(1, host->desc.rows) * static_cast<uint32_t>(host->atlas.info.cell_height))};
    using AdjustWindowRectExForDpiFn = BOOL(WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);
    static auto adjust_for_dpi = reinterpret_cast<AdjustWindowRectExForDpiFn>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "AdjustWindowRectExForDpi"));
    const UINT initial_dpi = host->desc.dpi_scale > 0.0f
        ? static_cast<UINT>(std::max(96.0f, host->desc.dpi_scale * 96.0f))
        : 96u;
    if (adjust_for_dpi) {
        adjust_for_dpi(&rect, WS_OVERLAPPEDWINDOW, FALSE, 0, initial_dpi);
    } else {
        AdjustWindowRectEx(&rect, WS_OVERLAPPEDWINDOW, FALSE, 0);
    }

    WinguiWindowDesc window_desc{};
    window_desc.class_name_utf8 = "SuperTerminalWindow";
    window_desc.title_utf8 = host->desc.title_utf8 && *host->desc.title_utf8 ? host->desc.title_utf8 : "SuperTerminal";
    window_desc.width = rect.right - rect.left;
    window_desc.height = rect.bottom - rect.top;
    window_desc.style = WS_OVERLAPPEDWINDOW;
    window_desc.window_proc = hostWindowProc;
    window_desc.user_data = host;
    if (!wingui_create_window_utf8(&window_desc, &host->window)) {
        setHostError(host, SUPERTERMINAL_HOST_ERROR_WINDOW_CREATE, wingui_last_error_utf8());
        return false;
    }

    int32_t client_width = 0;
    int32_t client_height = 0;
    if (!wingui_window_client_size(host->window, &client_width, &client_height)) {
        setHostError(host, SUPERTERMINAL_HOST_ERROR_WINDOW_CREATE, wingui_last_error_utf8());
        return false;
    }

    WinguiNativeCallbacks native_callbacks{};
    native_callbacks.dispatch_event_json = nativeDispatchEventJson;
    wingui_native_set_callbacks(&native_callbacks);

    WinguiNativeEmbeddedHostDesc native_desc{};
    native_desc.parent_hwnd = wingui_window_hwnd(host->window);
    native_desc.x = 0;
    native_desc.y = 0;
    native_desc.width = client_width;
    native_desc.height = client_height;
    native_desc.visible = 1;
    if (!wingui_native_attach_embedded_host(&native_desc)) {
        setHostError(host, SUPERTERMINAL_HOST_ERROR_NATIVE_UI_ATTACH, wingui_native_last_error_utf8());
        return false;
    }
    host->native_attached = true;
    host->sprite_atlas_packer.init(2048u);
    return true;
}

void shutdownHost(SuperTerminalRuntimeHost* host) {
    if (!host) return;
    if (host->client_thread.joinable()) {
        host->client_thread.join();
    }
    if (host->native_attached) {
        wingui_native_detach_embedded_host();
        host->native_attached = false;
    }
    {
        SuperTerminalCommand leftover{};
        while (host->command_queue.pop(&leftover)) {
            if (leftover.type == SUPERTERMINAL_CMD_RGBA_UPLOAD_OWNED) {
                void* owned = leftover.data.rgba_upload_owned.bgra8_pixels;
                if (owned) {
                    if (leftover.data.rgba_upload_owned.free_fn) {
                        leftover.data.rgba_upload_owned.free_fn(leftover.data.rgba_upload_owned.free_user_data, owned);
                    } else {
                        delete[] static_cast<uint8_t*>(owned);
                    }
                }
            } else if (leftover.type == SUPERTERMINAL_CMD_RGBA_ASSET_REGISTER_OWNED) {
                void* owned = leftover.data.rgba_asset_register_owned.bgra8_pixels;
                if (owned) {
                    if (leftover.data.rgba_asset_register_owned.free_fn) {
                        leftover.data.rgba_asset_register_owned.free_fn(leftover.data.rgba_asset_register_owned.free_user_data, owned);
                    } else {
                        delete[] static_cast<uint8_t*>(owned);
                    }
                }
            } else if (leftover.type == SUPERTERMINAL_CMD_INDEXED_UPLOAD_OWNED) {
                const auto& up = leftover.data.indexed_upload_owned;
                auto free_one = [&](void* p) {
                    if (!p) return;
                    if (up.free_fn) up.free_fn(up.free_user_data, p);
                    else delete[] static_cast<uint8_t*>(p);
                };
                free_one(up.indexed_pixels);
                free_one(up.line_palettes);
                free_one(up.global_palette);
            } else if (leftover.type == SUPERTERMINAL_CMD_SPRITE_DEFINE_OWNED) {
                const auto& def = leftover.data.sprite_define_owned;
                auto free_blob = [&](void* p) {
                    if (!p) return;
                    if (def.free_fn) def.free_fn(def.free_user_data, p);
                    else delete[] static_cast<uint8_t*>(p);
                };
                free_blob(def.pixels);
                free_blob(def.palette);
            } else if (leftover.type == SUPERTERMINAL_CMD_SPRITE_RENDER) {
                const auto& sr = leftover.data.sprite_render;
                if (sr.instances) {
                    if (sr.free_fn) sr.free_fn(sr.free_user_data, sr.instances);
                    else delete[] static_cast<uint8_t*>(sr.instances);
                }
            } else if (leftover.type == SUPERTERMINAL_CMD_VECTOR_DRAW_OWNED) {
                const auto& vd = leftover.data.vector_draw_owned;
                if (vd.primitives) {
                    if (vd.free_fn) vd.free_fn(vd.free_user_data, vd.primitives);
                    else delete[] static_cast<uint8_t*>(vd.primitives);
                }
            }
        }
    }
    {
        std::lock_guard<std::mutex> lock(host->asset_mutex);
        for (auto& kv : host->rgba_assets) {
            if (kv.second) wingui_destroy_rgba_surface(kv.second);
        }
        host->rgba_assets.clear();
    }
    {
        std::lock_guard<std::mutex> lock(host->pane_mutex);
        for (RegisteredPane& pane : host->panes) {
            if (pane.text_grid_context || pane.pane_text_renderer) {
                destroyPaneTextGridPresenter(pane);
            }
            if (pane.rgba_surface_handle) {
                destroyPaneRgbaPresenter(pane);
            }
            if (pane.indexed_surface_handle) {
                wingui_destroy_indexed_surface(pane.indexed_surface_handle);
                pane.indexed_surface_handle = nullptr;
            }
        }
    }
    if (host->rgba_renderer) {
        wingui_destroy_rgba_pane_renderer(host->rgba_renderer);
        host->rgba_renderer = nullptr;
    }
    if (host->vector_renderer) {
        wingui_destroy_vector_renderer(host->vector_renderer);
        host->vector_renderer = nullptr;
    }
    if (host->indexed_fill_renderer) {
        wingui_destroy_indexed_fill_renderer(host->indexed_fill_renderer);
        host->indexed_fill_renderer = nullptr;
    }
    if (host->indexed_renderer) {
        wingui_destroy_indexed_graphics_renderer(host->indexed_renderer);
        host->indexed_renderer = nullptr;
    }
    if (host->text_renderer) {
        wingui_destroy_text_grid_renderer(host->text_renderer);
        host->text_renderer = nullptr;
    }
    if (host->context) {
        wingui_destroy_context(host->context);
        host->context = nullptr;
    }
    if (host->window) {
        wingui_destroy_window(host->window);
        host->window = nullptr;
    }
    wingui_free_glyph_atlas_bitmap(&host->atlas);
    host->event_queue.shutdown();
    host->command_queue.shutdown();
}

void clientThreadMain(SuperTerminalRuntimeHost* host) {
    int32_t client_exit_code = 0;
    if (host && host->desc.startup) {
        try {
            client_exit_code = host->desc.startup(&host->client_ctx, host->desc.user_data);
        } catch (...) {
            client_exit_code = -1;
            setHostError(host, SUPERTERMINAL_HOST_ERROR_CLIENT_START, "super_terminal_run: client startup threw an exception");
        }
    }
    if (host && host->exit_code == 0) {
        host->exit_code = client_exit_code;
    }
    if (host) {
        host->client_finished.store(1, std::memory_order_release);
        requestStopInternal(host, host->exit_code, true);
    }
}

bool pumpMessages(SuperTerminalRuntimeHost* host) {
    if (!host) return false;

    const bool should_wait = host->render_dirty.load(std::memory_order_acquire) == 0 && host->command_queue.empty();
    MSG msg{};
    if (should_wait) {
        const BOOL result = GetMessageW(&msg, nullptr, 0, 0);
        if (result == -1) {
            setHostError(host, SUPERTERMINAL_HOST_ERROR_MESSAGE_LOOP, "super_terminal_run: GetMessageW failed");
            return false;
        }
        if (result == 0) {
            host->exit_code = static_cast<int32_t>(msg.wParam);
            return false;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            host->exit_code = static_cast<int32_t>(msg.wParam);
            return false;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return true;
}

void fillRunResult(const SuperTerminalRuntimeHost& host, SuperTerminalRunResult* out_result) {
    if (!out_result) return;
    out_result->exit_code = host.exit_code;
    out_result->host_error_code = host.host_error_code;
    copyUtf8Truncate(out_result->message_utf8, sizeof(out_result->message_utf8), host.message.c_str());
}

int32_t hostedAppStartup(SuperTerminalClientContext* ctx, void* user_data) {
    SuperTerminalHostedAppRuntime* runtime = static_cast<SuperTerminalHostedAppRuntime*>(user_data);
    if (!ctx || !runtime) {
        wingui_set_last_error_string_internal("super_terminal_run_hosted_app: invalid startup state");
        return -1;
    }

    if (runtime->desc.setup && !runtime->desc.setup(ctx, runtime->desc.user_data)) {
        if (!wingui_last_error_utf8() || !*wingui_last_error_utf8()) {
            wingui_set_last_error_string_internal("super_terminal_run_hosted_app: setup callback failed");
        }
        return -1;
    }

    const bool frame_enabled = runtime->desc.on_frame != nullptr;
    const uint32_t target_frame_ms = runtime->desc.target_frame_ms ? runtime->desc.target_frame_ms : 16u;
    uint64_t frame_index = 0;
    const uint64_t start_ms = GetTickCount64();
    uint64_t last_frame_ms = start_ms;
    uint64_t next_frame_ms = start_ms;

    for (;;) {
        uint32_t wait_timeout = SUPERTERMINAL_WAIT_INFINITE;
        if (frame_enabled) {
            const uint64_t now_ms = GetTickCount64();
            wait_timeout = now_ms >= next_frame_ms
                ? 0u
                : static_cast<uint32_t>(std::min<uint64_t>(next_frame_ms - now_ms, 0xffffffffull));
        }

        SuperTerminalEvent event{};
        if (super_terminal_wait_event(ctx, wait_timeout, &event)) {
            if (runtime->desc.on_event) {
                runtime->desc.on_event(ctx, &event, runtime->desc.user_data);
            } else if (event.type == SUPERTERMINAL_EVENT_CLOSE_REQUESTED) {
                super_terminal_request_stop(ctx, 0);
            }

            if (event.type == SUPERTERMINAL_EVENT_HOST_STOPPING) {
                return event.data.host_stopping.exit_code;
            }
            continue;
        }

        const char* wait_error = wingui_last_error_utf8();
        if (wait_error && *wait_error) {
            return -1;
        }
        if (!frame_enabled) {
            continue;
        }

        const uint64_t now_ms = GetTickCount64();
        if (now_ms < next_frame_ms) {
            continue;
        }

        SuperTerminalFrameTick tick{};
        tick.frame_index = ++frame_index;
        tick.elapsed_ms = now_ms - start_ms;
        tick.delta_ms = frame_index == 1 ? 0 : (now_ms - last_frame_ms);
        tick.target_frame_ms = target_frame_ms;
        tick.active_buffer_index = ctx->host->display_buffer_index.load(std::memory_order_acquire) & 1u;
        tick.buffer_index = tick.active_buffer_index ^ 1u;
        tick.buffer_count = 2u;
        runtime->desc.on_frame(ctx, &tick, runtime->desc.user_data);
        SuperTerminalCommand swap_cmd{};
        swap_cmd.type = SUPERTERMINAL_CMD_FRAME_SWAP;
        super_terminal_enqueue(ctx, &swap_cmd);
        if (runtime->desc.auto_request_present) {
            super_terminal_request_present(ctx);
        }
        last_frame_ms = now_ms;
        next_frame_ms = now_ms + target_frame_ms;
    }
}

void hostedAppShutdown(void* user_data) {
    SuperTerminalHostedAppRuntime* runtime = static_cast<SuperTerminalHostedAppRuntime*>(user_data);
    if (runtime && runtime->desc.shutdown) {
        runtime->desc.shutdown(runtime->desc.user_data);
    }
}

} // namespace

extern "C" WINGUI_API int32_t WINGUI_CALL super_terminal_run(
    const SuperTerminalAppDesc* desc,
    SuperTerminalRunResult* out_result) {
    if (out_result) {
        std::memset(out_result, 0, sizeof(*out_result));
    }
    if (!desc || !desc->startup) {
        wingui_set_last_error_string_internal("super_terminal_run: invalid arguments");
        return 0;
    }

    SuperTerminalRuntimeHost host{};
    host.desc = *desc;
    host.client_ctx.host = &host;
    host.desc.columns = host.desc.columns ? host.desc.columns : kDefaultColumns;
    host.desc.rows = host.desc.rows ? host.desc.rows : kDefaultRows;
    host.message.clear();

    if (!host.command_queue.init(host.desc.command_queue_capacity ? host.desc.command_queue_capacity : kDefaultQueueCapacity, false)) {
        setHostError(&host, SUPERTERMINAL_HOST_ERROR_INVALID_ARGUMENT, "super_terminal_run: failed to initialize command queue");
        fillRunResult(host, out_result);
        return 0;
    }
    if (!host.event_queue.init(host.desc.event_queue_capacity ? host.desc.event_queue_capacity : kDefaultQueueCapacity, true)) {
        setHostError(&host, SUPERTERMINAL_HOST_ERROR_INVALID_ARGUMENT, "super_terminal_run: failed to initialize event queue");
        shutdownHost(&host);
        fillRunResult(host, out_result);
        return 0;
    }
    if (!initBufferedSurface(&host.surface, host.desc.columns, host.desc.rows)) {
        setHostError(&host, SUPERTERMINAL_HOST_ERROR_INVALID_ARGUMENT, "super_terminal_run: failed to initialize terminal surface");
        shutdownHost(&host);
        fillRunResult(host, out_result);
        return 0;
    }
    if (!initWindowAndRenderer(&host)) {
        shutdownHost(&host);
        fillRunResult(host, out_result);
        return 0;
    }

    g_active_host = &host;

    if (host.desc.initial_ui_json_utf8 && *host.desc.initial_ui_json_utf8) {
        wingui_native_publish_json(host.desc.initial_ui_json_utf8);
        wingui_native_host_run();
        syncActivePaneFromDeclarativeFocus(&host);
    }

    host.render_dirty.store(1, std::memory_order_release);
    updateSurfaceForClientSize(&host);

    host.client_thread = std::thread(clientThreadMain, &host);
    if (!wingui_window_show(host.window, SW_SHOWDEFAULT)) {
        setHostError(&host, SUPERTERMINAL_HOST_ERROR_WINDOW_CREATE, wingui_last_error_utf8());
        requestStopInternal(&host, -1, true);
    }

    while (true) {
        drainCommands(&host);
        if (host.render_dirty.load(std::memory_order_acquire) != 0) {
            if (!renderSurface(&host)) {
                setHostError(&host, SUPERTERMINAL_HOST_ERROR_RENDERER_CREATE, wingui_last_error_utf8());
                requestStopInternal(&host, -1, true);
            }
        }
        if (!pumpMessages(&host)) {
            break;
        }
    }

    requestStopInternal(&host, host.exit_code, false);
    if (host.desc.shutdown) {
        host.desc.shutdown(host.desc.user_data);
    }
    shutdownHost(&host);
    g_active_host = nullptr;
    fillRunResult(host, out_result);
    wingui_clear_last_error_internal();
    return host.host_error_code == SUPERTERMINAL_HOST_ERROR_NONE ? 1 : 0;
}

extern "C" WINGUI_API int32_t WINGUI_CALL super_terminal_run_hosted_app(
    const SuperTerminalHostedAppDesc* desc,
    SuperTerminalRunResult* out_result) {
    if (out_result) {
        std::memset(out_result, 0, sizeof(*out_result));
    }
    if (!desc) {
        wingui_set_last_error_string_internal("super_terminal_run_hosted_app: invalid arguments");
        return 0;
    }

    SuperTerminalHostedAppRuntime runtime{};
    runtime.desc = *desc;

    SuperTerminalAppDesc app_desc{};
    app_desc.title_utf8 = desc->title_utf8;
    app_desc.columns = desc->columns;
    app_desc.rows = desc->rows;
    app_desc.flags = desc->flags;
    app_desc.command_queue_capacity = desc->command_queue_capacity;
    app_desc.event_queue_capacity = desc->event_queue_capacity;
    app_desc.font_family_utf8 = desc->font_family_utf8;
    app_desc.font_pixel_height = desc->font_pixel_height;
    app_desc.dpi_scale = desc->dpi_scale;
    app_desc.text_shader_path_utf8 = desc->text_shader_path_utf8;
    app_desc.initial_ui_json_utf8 = desc->initial_ui_json_utf8;
    app_desc.user_data = &runtime;
    app_desc.startup = hostedAppStartup;
    app_desc.shutdown = hostedAppShutdown;
    return super_terminal_run(&app_desc, out_result);
}

extern "C" WINGUI_API int32_t WINGUI_CALL super_terminal_enqueue(
    SuperTerminalClientContext* ctx,
    const SuperTerminalCommand* command) {
    if (!ctx || !ctx->host || !command) {
        wingui_set_last_error_string_internal("super_terminal_enqueue: invalid arguments");
        return 0;
    }
    SuperTerminalCommand command_copy = *command;
    command_copy.sequence = ctx->host->command_sequence.fetch_add(1, std::memory_order_relaxed) + 1;
    if (!ctx->host->command_queue.push(command_copy)) {
        wingui_set_last_error_string_internal("super_terminal_enqueue: queue is full");
        return 0;
    }
    HWND hwnd = static_cast<HWND>(wingui_window_hwnd(ctx->host->window));
    if (hwnd) {
        PostMessageW(hwnd, kSuperTerminalWakeMessage, 0, 0);
    }
    wingui_clear_last_error_internal();
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL super_terminal_wait_event(
    SuperTerminalClientContext* ctx,
    uint32_t timeout_ms,
    SuperTerminalEvent* out_event) {
    if (!ctx || !ctx->host || !out_event) {
        wingui_set_last_error_string_internal("super_terminal_wait_event: invalid arguments");
        return 0;
    }
    const DWORD wait_ms = timeout_ms == SUPERTERMINAL_WAIT_INFINITE ? INFINITE : timeout_ms;
    const DWORD wait_result = WaitForSingleObject(ctx->host->event_queue.event_handle, wait_ms);
    if (wait_result == WAIT_TIMEOUT) {
        wingui_clear_last_error_internal();
        return 0;
    }
    if (wait_result != WAIT_OBJECT_0) {
        wingui_set_last_error_string_internal("super_terminal_wait_event: wait failed");
        return 0;
    }
    if (!ctx->host->event_queue.pop(out_event)) {
        wingui_clear_last_error_internal();
        return 0;
    }
    wingui_clear_last_error_internal();
    return 1;
}

extern "C" WINGUI_API void* WINGUI_CALL super_terminal_event_handle(
    SuperTerminalClientContext* ctx) {
    if (!ctx || !ctx->host) {
        wingui_set_last_error_string_internal("super_terminal_event_handle: invalid arguments");
        return nullptr;
    }
    wingui_clear_last_error_internal();
    return ctx->host->event_queue.event_handle;
}

extern "C" WINGUI_API int32_t WINGUI_CALL super_terminal_request_stop(
    SuperTerminalClientContext* ctx,
    int32_t exit_code) {
    if (!ctx || !ctx->host) {
        wingui_set_last_error_string_internal("super_terminal_request_stop: invalid arguments");
        return 0;
    }
    requestStopInternal(ctx->host, exit_code, true);
    wingui_clear_last_error_internal();
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL super_terminal_get_key_state(
    SuperTerminalClientContext* ctx,
    uint32_t virtual_key) {
    if (!ctx || !ctx->host || !ctx->host->window) {
        wingui_set_last_error_string_internal("super_terminal_get_key_state: invalid arguments");
        return 0;
    }
    return wingui_window_get_key_state(ctx->host->window, virtual_key);
}

extern "C" WINGUI_API int32_t WINGUI_CALL super_terminal_get_keyboard_state(
    SuperTerminalClientContext* ctx,
    WinguiKeyboardState* out_state) {
    if (!ctx || !ctx->host || !ctx->host->window || !out_state) {
        wingui_set_last_error_string_internal("super_terminal_get_keyboard_state: invalid arguments");
        return 0;
    }
    return wingui_window_get_keyboard_state(ctx->host->window, out_state);
}

extern "C" WINGUI_API int32_t WINGUI_CALL super_terminal_resolve_pane_id_utf8(
    SuperTerminalClientContext* ctx,
    const char* node_id_utf8,
    SuperTerminalPaneId* out_pane_id) {
    if (!ctx || !ctx->host || !node_id_utf8 || !*node_id_utf8 || !out_pane_id) {
        wingui_set_last_error_string_internal("super_terminal_resolve_pane_id_utf8: invalid arguments");
        return 0;
    }

    SuperTerminalPaneId pane_id{};
    pane_id.value = hashPaneNodeId(node_id_utf8);

    {
        std::lock_guard<std::mutex> lock(ctx->host->pane_mutex);
        if (!assignPaneNodeIdLocked(ctx->host, pane_id, node_id_utf8, "super_terminal_resolve_pane_id_utf8")) {
            return 0;
        }
    }

    *out_pane_id = pane_id;
    wingui_clear_last_error_internal();
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL super_terminal_get_pane_layout(
    SuperTerminalClientContext* ctx,
    SuperTerminalPaneId pane_id,
    SuperTerminalPaneLayout* out_layout) {
    if (!ctx || !ctx->host || !out_layout) {
        wingui_set_last_error_string_internal("super_terminal_get_pane_layout: invalid arguments");
        return 0;
    }

    if (pane_id.value == 0) {
        if (!resolvePaneLayout(ctx->host, pane_id, out_layout)) {
            wingui_set_last_error_string_internal("super_terminal_get_pane_layout: root layout is unavailable");
            return 0;
        }
        wingui_clear_last_error_internal();
        return 1;
    }

    if (!copyPaneLayoutFromCache(ctx->host, pane_id, out_layout)) {
        wingui_set_last_error_string_internal("super_terminal_get_pane_layout: pane layout is unavailable");
        return 0;
    }
    wingui_clear_last_error_internal();
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL super_terminal_get_mouse_state(
    SuperTerminalClientContext* ctx,
    WinguiMouseState* out_state) {
    if (!ctx || !ctx->host || !ctx->host->window) {
        wingui_set_last_error_string_internal("super_terminal_get_mouse_state: invalid arguments");
        return 0;
    }
    return wingui_window_get_mouse_state(ctx->host->window, out_state);
}

extern "C" WINGUI_API int32_t WINGUI_CALL super_terminal_get_native_ui_patch_metrics(
    SuperTerminalClientContext* ctx,
    SuperTerminalNativeUiPatchMetrics* out_metrics) {
    (void)ctx;
    if (!out_metrics) {
        wingui_set_last_error_string_internal("super_terminal_get_native_ui_patch_metrics: invalid arguments");
        return 0;
    }
    WinguiNativePatchMetrics native_metrics{};
    if (!wingui_native_get_patch_metrics(&native_metrics)) {
        return 0;
    }
    out_metrics->publish_count = native_metrics.publish_count;
    out_metrics->patch_request_count = native_metrics.patch_request_count;
    out_metrics->direct_apply_count = native_metrics.direct_apply_count;
    out_metrics->subtree_rebuild_count = native_metrics.subtree_rebuild_count;
    out_metrics->window_rebuild_count = native_metrics.window_rebuild_count;
    out_metrics->resize_reject_count = native_metrics.resize_reject_count;
    out_metrics->failed_patch_count = native_metrics.failed_patch_count;
    wingui_clear_last_error_internal();
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL super_terminal_publish_ui_json(
    SuperTerminalClientContext* ctx,
    const char* json_utf8) {
    if (!json_utf8) {
        wingui_set_last_error_string_internal("super_terminal_publish_ui_json: invalid arguments");
        return 0;
    }
    char* owned = _strdup(json_utf8);
    if (!owned) {
        wingui_set_last_error_string_internal("super_terminal_publish_ui_json: out of memory");
        return 0;
    }
    SuperTerminalCommand command{};
    command.type = SUPERTERMINAL_CMD_NATIVE_UI_PUBLISH;
    command.data.native_ui_publish.json_utf8 = owned;
    const int32_t result = super_terminal_enqueue(ctx, &command);
    if (!result) free(owned);
    return result;
}

extern "C" WINGUI_API int32_t WINGUI_CALL super_terminal_patch_ui_json(
    SuperTerminalClientContext* ctx,
    const char* patch_json_utf8) {
    if (!patch_json_utf8) {
        wingui_set_last_error_string_internal("super_terminal_patch_ui_json: invalid arguments");
        return 0;
    }
    char* owned = _strdup(patch_json_utf8);
    if (!owned) {
        wingui_set_last_error_string_internal("super_terminal_patch_ui_json: out of memory");
        return 0;
    }
    SuperTerminalCommand command{};
    command.type = SUPERTERMINAL_CMD_NATIVE_UI_PATCH;
    command.data.native_ui_patch.patch_json_utf8 = owned;
    const int32_t result = super_terminal_enqueue(ctx, &command);
    if (!result) free(owned);
    return result;
}

extern "C" WINGUI_API int32_t WINGUI_CALL super_terminal_set_title_utf8(
    SuperTerminalClientContext* ctx,
    const char* title_utf8) {
    if (!title_utf8) {
        wingui_set_last_error_string_internal("super_terminal_set_title_utf8: invalid arguments");
        return 0;
    }
    SuperTerminalCommand command{};
    command.type = SUPERTERMINAL_CMD_WINDOW_SET_TITLE;
    copyUtf8Truncate(command.data.set_title.title_utf8, sizeof(command.data.set_title.title_utf8), title_utf8);
    return super_terminal_enqueue(ctx, &command);
}

extern "C" WINGUI_API int32_t WINGUI_CALL super_terminal_text_grid_write_cells(
    SuperTerminalClientContext* ctx,
    SuperTerminalPaneId pane_id,
    const SuperTerminalTextGridCell* cells,
    uint32_t cell_count) {
    if (!cells && cell_count != 0) {
        wingui_set_last_error_string_internal("super_terminal_text_grid_write_cells: invalid arguments");
        return 0;
    }
    SuperTerminalTextGridCell* owned_cells = nullptr;
    if (cell_count != 0) {
        owned_cells = static_cast<SuperTerminalTextGridCell*>(std::malloc(static_cast<size_t>(cell_count) * sizeof(SuperTerminalTextGridCell)));
        if (!owned_cells) {
            wingui_set_last_error_string_internal("super_terminal_text_grid_write_cells: out of memory");
            return 0;
        }
        std::memcpy(owned_cells, cells, static_cast<size_t>(cell_count) * sizeof(SuperTerminalTextGridCell));
    }
    SuperTerminalCommand command{};
    command.type = SUPERTERMINAL_CMD_TEXT_GRID_WRITE_CELLS;
    command.data.text_grid_write_cells.pane_id = pane_id;
    command.data.text_grid_write_cells.cells = owned_cells;
    command.data.text_grid_write_cells.cell_count = cell_count;
    const int32_t result = super_terminal_enqueue(ctx, &command);
    if (!result) {
        free(owned_cells);
    }
    return result;
}

extern "C" WINGUI_API int32_t WINGUI_CALL super_terminal_frame_text_grid_write_cells(
    SuperTerminalClientContext* ctx,
    const SuperTerminalFrameTick* tick,
    SuperTerminalPaneId pane_id,
    const SuperTerminalTextGridCell* cells,
    uint32_t cell_count) {
    if (!ctx || !ctx->host || !tick || !cells) {
        wingui_set_last_error_string_internal("super_terminal_frame_text_grid_write_cells: invalid arguments");
        return 0;
    }
    if ((tick->buffer_index & ~1u) != 0u) {
        wingui_set_last_error_string_internal("super_terminal_frame_text_grid_write_cells: invalid frame buffer index");
        return 0;
    }
    TerminalSurface* surface = textGridSurfaceForPaneBuffer(ctx->host, pane_id, true, tick->buffer_index);
    if (!surface) {
        wingui_set_last_error_string_internal("super_terminal_frame_text_grid_write_cells: unavailable frame buffer");
        return 0;
    }
    SuperTerminalTextGridWriteCells write_cells{};
    write_cells.pane_id = pane_id;
    write_cells.cells = cells;
    write_cells.cell_count = cell_count;
    writeCellsToSurface(surface, write_cells, *ctx->host);
    {
        std::lock_guard<std::mutex> lock(ctx->host->pane_mutex);
        if (RegisteredPane* pane = registeredPaneForIdLocked(ctx->host, pane_id, false)) {
            pane->text_grid_cache_dirty[tick->buffer_index & 1u] = true;
        }
    }
    {
        const std::string node_id = paneNodeIdForTrace(ctx->host, pane_id);
        if (shouldTraceWorkspacePaneNodeId(node_id)) {
            const SuperTerminalTextGridCell& first = cells[0];
            char buffer[512];
            std::snprintf(buffer,
                          sizeof(buffer),
                          "frame-write-cells id=%s pane=%llu buf=%u count=%u first=(r%u,c%u,cp%u)",
                          node_id.c_str(),
                          static_cast<unsigned long long>(pane_id.value),
                          static_cast<unsigned>(tick->buffer_index),
                          static_cast<unsigned>(cell_count),
                          static_cast<unsigned>(first.row),
                          static_cast<unsigned>(first.column),
                          static_cast<unsigned>(first.codepoint));
            traceNativePatchEvent(buffer);
        }
    }
    wingui_clear_last_error_internal();
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL super_terminal_text_grid_clear_region(
    SuperTerminalClientContext* ctx,
    SuperTerminalPaneId pane_id,
    uint32_t row,
    uint32_t column,
    uint32_t width,
    uint32_t height,
    uint32_t fill_codepoint,
    WinguiGraphicsColour foreground,
    WinguiGraphicsColour background) {
    SuperTerminalCommand command{};
    command.type = SUPERTERMINAL_CMD_TEXT_GRID_CLEAR_REGION;
    command.data.text_grid_clear_region.pane_id = pane_id;
    command.data.text_grid_clear_region.row = row;
    command.data.text_grid_clear_region.column = column;
    command.data.text_grid_clear_region.width = width;
    command.data.text_grid_clear_region.height = height;
    command.data.text_grid_clear_region.fill_codepoint = fill_codepoint;
    command.data.text_grid_clear_region.foreground = foreground;
    command.data.text_grid_clear_region.background = background;
    return super_terminal_enqueue(ctx, &command);
}

extern "C" WINGUI_API int32_t WINGUI_CALL super_terminal_frame_text_grid_clear_region(
    SuperTerminalClientContext* ctx,
    const SuperTerminalFrameTick* tick,
    SuperTerminalPaneId pane_id,
    uint32_t row,
    uint32_t column,
    uint32_t width,
    uint32_t height,
    uint32_t fill_codepoint,
    WinguiGraphicsColour foreground,
    WinguiGraphicsColour background) {
    if (!ctx || !ctx->host || !tick) {
        wingui_set_last_error_string_internal("super_terminal_frame_text_grid_clear_region: invalid arguments");
        return 0;
    }
    if ((tick->buffer_index & ~1u) != 0u) {
        wingui_set_last_error_string_internal("super_terminal_frame_text_grid_clear_region: invalid frame buffer index");
        return 0;
    }
    TerminalSurface* surface = textGridSurfaceForPaneBuffer(ctx->host, pane_id, true, tick->buffer_index);
    if (!surface) {
        wingui_set_last_error_string_internal("super_terminal_frame_text_grid_clear_region: unavailable frame buffer");
        return 0;
    }
    SuperTerminalTextGridClearRegion clear_region{};
    clear_region.pane_id = pane_id;
    clear_region.row = row;
    clear_region.column = column;
    clear_region.width = width;
    clear_region.height = height;
    clear_region.fill_codepoint = fill_codepoint;
    clear_region.foreground = foreground;
    clear_region.background = background;
    clearSurfaceRegion(surface, clear_region);
    {
        std::lock_guard<std::mutex> lock(ctx->host->pane_mutex);
        if (RegisteredPane* pane = registeredPaneForIdLocked(ctx->host, pane_id, false)) {
            pane->text_grid_cache_dirty[tick->buffer_index & 1u] = true;
        }
    }
    {
        const std::string node_id = paneNodeIdForTrace(ctx->host, pane_id);
        if (shouldTraceWorkspacePaneNodeId(node_id)) {
            char buffer[512];
            std::snprintf(buffer,
                          sizeof(buffer),
                          "frame-clear-region id=%s pane=%llu buf=%u row=%u col=%u w=%u h=%u fill=%u",
                          node_id.c_str(),
                          static_cast<unsigned long long>(pane_id.value),
                          static_cast<unsigned>(tick->buffer_index),
                          static_cast<unsigned>(row),
                          static_cast<unsigned>(column),
                          static_cast<unsigned>(width),
                          static_cast<unsigned>(height),
                          static_cast<unsigned>(fill_codepoint));
            traceNativePatchEvent(buffer);
        }
    }
    wingui_clear_last_error_internal();
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL super_terminal_frame_indexed_graphics_upload(
    SuperTerminalClientContext* ctx,
    const SuperTerminalFrameTick* tick,
    SuperTerminalPaneId pane_id,
    const SuperTerminalIndexedGraphicsFrame* frame) {
    if (!ctx || !ctx->host || !tick || !frame || !frame->indexed_pixels || !frame->line_palettes || !frame->global_palette) {
        wingui_set_last_error_string_internal("super_terminal_frame_indexed_graphics_upload: invalid arguments");
        return 0;
    }
    if ((tick->buffer_index & ~1u) != 0u) {
        wingui_set_last_error_string_internal("super_terminal_frame_indexed_graphics_upload: invalid frame buffer index");
        return 0;
    }
    if (frame->buffer_width == 0 || frame->buffer_height == 0 || frame->global_palette_count == 0 || frame->global_palette_count > 240u) {
        wingui_set_last_error_string_internal("super_terminal_frame_indexed_graphics_upload: invalid frame dimensions");
        return 0;
    }
    const size_t pixel_bytes = static_cast<size_t>(frame->buffer_width) * frame->buffer_height;
    const size_t line_bytes = static_cast<size_t>(frame->buffer_height) * sizeof(WinguiGraphicsLinePalette);
    const size_t global_bytes = static_cast<size_t>(frame->global_palette_count) * sizeof(WinguiGraphicsColour);
    uint8_t* owned_pixels = new (std::nothrow) uint8_t[pixel_bytes];
    uint8_t* owned_lines = new (std::nothrow) uint8_t[line_bytes];
    uint8_t* owned_global = new (std::nothrow) uint8_t[global_bytes];
    if (!owned_pixels || !owned_lines || !owned_global) {
        delete[] owned_pixels;
        delete[] owned_lines;
        delete[] owned_global;
        wingui_set_last_error_string_internal("super_terminal_frame_indexed_graphics_upload: out of memory");
        return 0;
    }
    std::memcpy(owned_pixels, frame->indexed_pixels, pixel_bytes);
    std::memcpy(owned_lines, frame->line_palettes, line_bytes);
    std::memcpy(owned_global, frame->global_palette, global_bytes);
    SuperTerminalCommand command{};
    command.type = SUPERTERMINAL_CMD_INDEXED_UPLOAD_OWNED;
    SuperTerminalIndexedUploadOwned& up = command.data.indexed_upload_owned;
    up.pane_id = pane_id;
    up.buffer_index = tick->buffer_index & 1u;
    up.buffer_width = frame->buffer_width;
    up.buffer_height = frame->buffer_height;
    up.screen_width = frame->screen_width ? frame->screen_width : frame->buffer_width;
    up.screen_height = frame->screen_height ? frame->screen_height : frame->buffer_height;
    up.scroll_x = frame->scroll_x;
    up.scroll_y = frame->scroll_y;
    up.pixel_aspect_num = frame->pixel_aspect_num ? frame->pixel_aspect_num : 1u;
    up.pixel_aspect_den = frame->pixel_aspect_den ? frame->pixel_aspect_den : 1u;
    up.global_palette_count = frame->global_palette_count;
    up.indexed_pixels = owned_pixels;
    up.line_palettes = owned_lines;
    up.global_palette = owned_global;
    up.free_fn = nullptr;
    up.free_user_data = nullptr;
    if (!super_terminal_enqueue(ctx, &command)) {
        delete[] owned_pixels;
        delete[] owned_lines;
        delete[] owned_global;
        return 0;
    }
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL super_terminal_frame_rgba_upload(
    SuperTerminalClientContext* ctx,
    const SuperTerminalFrameTick* tick,
    SuperTerminalPaneId pane_id,
    const SuperTerminalRgbaFrame* frame) {
    if (!ctx || !ctx->host || !tick || !frame || !frame->bgra8_pixels) {
        wingui_set_last_error_string_internal("super_terminal_frame_rgba_upload: invalid arguments");
        return 0;
    }
    if ((tick->buffer_index & ~1u) != 0u) {
        wingui_set_last_error_string_internal("super_terminal_frame_rgba_upload: invalid frame buffer index");
        return 0;
    }
    const uint32_t pitch = frame->source_pitch ? frame->source_pitch : frame->width * 4u;
    if (frame->width == 0 || frame->height == 0 || pitch < frame->width * 4u) {
        wingui_set_last_error_string_internal("super_terminal_frame_rgba_upload: invalid frame dimensions");
        return 0;
    }
    const size_t total_bytes = static_cast<size_t>(pitch) * frame->height;
    uint8_t* owned = new (std::nothrow) uint8_t[total_bytes];
    if (!owned) {
        wingui_set_last_error_string_internal("super_terminal_frame_rgba_upload: out of memory");
        return 0;
    }
    std::memcpy(owned, frame->bgra8_pixels, total_bytes);
    SuperTerminalCommand command{};
    command.type = SUPERTERMINAL_CMD_RGBA_UPLOAD_OWNED;
    SuperTerminalRgbaUploadOwned& up = command.data.rgba_upload_owned;
    up.pane_id = pane_id;
    up.buffer_index = tick->buffer_index & 1u;
    up.dst_x = 0;
    up.dst_y = 0;
    up.region_width = frame->width;
    up.region_height = frame->height;
    up.source_pitch = pitch;
    up.surface_width = frame->width;
    up.surface_height = frame->height;
    up.screen_width = frame->screen_width ? frame->screen_width : frame->width;
    up.screen_height = frame->screen_height ? frame->screen_height : frame->height;
    up.pixel_aspect_num = frame->pixel_aspect_num ? frame->pixel_aspect_num : 1u;
    up.pixel_aspect_den = frame->pixel_aspect_den ? frame->pixel_aspect_den : 1u;
    up.bgra8_pixels = owned;
    up.free_fn = nullptr;
    up.free_user_data = nullptr;
    if (!super_terminal_enqueue(ctx, &command)) {
        delete[] owned;
        return 0;
    }
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL super_terminal_request_present(
    SuperTerminalClientContext* ctx) {
    SuperTerminalCommand command{};
    command.type = SUPERTERMINAL_CMD_REQUEST_PRESENT;
    return super_terminal_enqueue(ctx, &command);
}

extern "C" WINGUI_API int32_t WINGUI_CALL super_terminal_define_sprite(
    SuperTerminalClientContext* ctx,
    SuperTerminalPaneId pane_id,
    SuperTerminalSpriteId sprite_id,
    uint32_t frame_w,
    uint32_t frame_h,
    uint32_t frame_count,
    uint32_t frames_per_tick,
    void* pixels,
    void* palette,
    SuperTerminalFreeFn free_fn,
    void* free_user_data) {
    if (!ctx || !ctx->host || !pane_id.value || !sprite_id.value ||
        !frame_w || !frame_h || !frame_count || !pixels || !palette) {
        wingui_set_last_error_string_internal("super_terminal_define_sprite: invalid arguments");
        return 0;
    }
    /* Copy pixel strip and palette into owned blobs so caller can free immediately. */
    const size_t pixel_bytes = static_cast<size_t>(frame_w) * frame_count * frame_h;
    uint8_t* owned_pixels = new (std::nothrow) uint8_t[pixel_bytes];
    uint8_t* owned_palette = new (std::nothrow) uint8_t[sizeof(WinguiGraphicsLinePalette)];
    if (!owned_pixels || !owned_palette) {
        delete[] owned_pixels;
        delete[] owned_palette;
        /* Free caller's buffers via provided free_fn */
        if (free_fn) { free_fn(free_user_data, pixels); free_fn(free_user_data, palette); }
        else { delete[] static_cast<uint8_t*>(pixels); delete[] static_cast<uint8_t*>(palette); }
        wingui_set_last_error_string_internal("super_terminal_define_sprite: out of memory");
        return 0;
    }
    std::memcpy(owned_pixels, pixels, pixel_bytes);
    std::memcpy(owned_palette, palette, sizeof(WinguiGraphicsLinePalette));
    /* Free caller's original buffers now that we've copied */
    if (free_fn) { free_fn(free_user_data, pixels); free_fn(free_user_data, palette); }
    else { delete[] static_cast<uint8_t*>(pixels); delete[] static_cast<uint8_t*>(palette); }

    SuperTerminalCommand command{};
    command.type = SUPERTERMINAL_CMD_SPRITE_DEFINE_OWNED;
    SuperTerminalSpriteDefineOwned& def = command.data.sprite_define_owned;
    def.pane_id = pane_id;
    def.sprite_id = sprite_id;
    def.frame_w = frame_w;
    def.frame_h = frame_h;
    def.frame_count = frame_count;
    def.frames_per_tick = frames_per_tick;
    def.pixels = owned_pixels;
    def.palette = owned_palette;
    def.free_fn = nullptr;
    def.free_user_data = nullptr;
    if (!super_terminal_enqueue(ctx, &command)) {
        delete[] owned_pixels;
        delete[] owned_palette;
        return 0;
    }
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL super_terminal_render_sprites(
    SuperTerminalClientContext* ctx,
    SuperTerminalPaneId pane_id,
    uint64_t sprite_tick,
    uint32_t target_width,
    uint32_t target_height,
    const SuperTerminalSpriteInstance* instances,
    uint32_t instance_count) {
    if (!ctx || !ctx->host || !pane_id.value || !instances || !instance_count) {
        wingui_set_last_error_string_internal("super_terminal_render_sprites: invalid arguments");
        return 0;
    }
    const size_t bytes = static_cast<size_t>(instance_count) * sizeof(SuperTerminalSpriteInstance);
    uint8_t* owned = new (std::nothrow) uint8_t[bytes];
    if (!owned) {
        wingui_set_last_error_string_internal("super_terminal_render_sprites: out of memory");
        return 0;
    }
    std::memcpy(owned, instances, bytes);
    SuperTerminalCommand command{};
    command.type = SUPERTERMINAL_CMD_SPRITE_RENDER;
    SuperTerminalSpriteRender& sr = command.data.sprite_render;
    sr.pane_id = pane_id;
    sr.sprite_tick = sprite_tick;
    sr.target_width = target_width;
    sr.target_height = target_height;
    sr.instances = owned;
    sr.instance_count = instance_count;
    sr.free_fn = nullptr;
    sr.free_user_data = nullptr;
    if (!super_terminal_enqueue(ctx, &command)) {
        delete[] owned;
        return 0;
    }
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL super_terminal_get_glyph_atlas_info(
    SuperTerminalClientContext* ctx,
    WinguiGlyphAtlasInfo* out_info) {
    if (!ctx || !ctx->host || !out_info) {
        wingui_set_last_error_string_internal("super_terminal_get_glyph_atlas_info: invalid arguments");
        return 0;
    }
    if (!ctx->host->atlas.pixels_rgba) {
        wingui_set_last_error_string_internal("super_terminal_get_glyph_atlas_info: atlas not built");
        return 0;
    }
    *out_info = ctx->host->atlas.info;
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL super_terminal_vector_draw(
    SuperTerminalClientContext* ctx,
    SuperTerminalPaneId pane_id,
    uint32_t buffer_index,
    uint32_t content_buffer_mode,
    uint32_t blend_mode,
    int32_t clear_before,
    const float clear_color_rgba[4],
    const WinguiVectorPrimitive* primitives,
    uint32_t primitive_count) {
    if (!ctx || !ctx->host || !pane_id.value) {
        wingui_set_last_error_string_internal("super_terminal_vector_draw: invalid arguments");
        return 0;
    }
    if (primitive_count > 0 && !primitives) {
        wingui_set_last_error_string_internal("super_terminal_vector_draw: null primitives");
        return 0;
    }
    uint8_t* owned = nullptr;
    if (primitive_count > 0) {
        const size_t bytes = static_cast<size_t>(primitive_count) * sizeof(WinguiVectorPrimitive);
        owned = new (std::nothrow) uint8_t[bytes];
        if (!owned) {
            wingui_set_last_error_string_internal("super_terminal_vector_draw: out of memory");
            return 0;
        }
        std::memcpy(owned, primitives, bytes);
    }
    SuperTerminalCommand command{};
    command.type = SUPERTERMINAL_CMD_VECTOR_DRAW_OWNED;
    SuperTerminalVectorDrawOwned& vd = command.data.vector_draw_owned;
    vd.pane_id = pane_id;
    vd.buffer_index = buffer_index;
    vd.content_buffer_mode = content_buffer_mode;
    vd.blend_mode = blend_mode;
    vd.clear_before = clear_before;
    if (clear_color_rgba) {
        vd.clear_color[0] = clear_color_rgba[0];
        vd.clear_color[1] = clear_color_rgba[1];
        vd.clear_color[2] = clear_color_rgba[2];
        vd.clear_color[3] = clear_color_rgba[3];
    }
    vd.primitives = owned;
    vd.primitive_count = primitive_count;
    vd.free_fn = nullptr;
    vd.free_user_data = nullptr;
    if (!super_terminal_enqueue(ctx, &command)) {
        delete[] owned;
        return 0;
    }
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL super_terminal_indexed_fill_rect(
    SuperTerminalClientContext* ctx,
    SuperTerminalPaneId pane_id,
    uint32_t buffer_index,
    uint32_t x,
    uint32_t y,
    uint32_t width,
    uint32_t height,
    uint32_t palette_index) {
    if (!ctx || !ctx->host || !pane_id.value) {
        wingui_set_last_error_string_internal("super_terminal_indexed_fill_rect: invalid arguments");
        return 0;
    }
    if (width == 0 || height == 0) return 1;  // nothing to draw
    SuperTerminalCommand command{};
    command.type = SUPERTERMINAL_CMD_INDEXED_FILL_RECT;
    SuperTerminalIndexedFillRect& fr = command.data.indexed_fill_rect;
    fr.pane_id = pane_id;
    fr.buffer_index = buffer_index;
    fr.x = x;
    fr.y = y;
    fr.width = width;
    fr.height = height;
    fr.palette_index = palette_index;
    return super_terminal_enqueue(ctx, &command);
}

extern "C" WINGUI_API int32_t WINGUI_CALL super_terminal_indexed_draw_line(
    SuperTerminalClientContext* ctx,
    SuperTerminalPaneId pane_id,
    uint32_t buffer_index,
    int32_t x0,
    int32_t y0,
    int32_t x1,
    int32_t y1,
    uint32_t palette_index) {
    if (!ctx || !ctx->host || !pane_id.value) {
        wingui_set_last_error_string_internal("super_terminal_indexed_draw_line: invalid arguments");
        return 0;
    }
    SuperTerminalCommand command{};
    command.type = SUPERTERMINAL_CMD_INDEXED_DRAW_LINE;
    SuperTerminalIndexedDrawLine& ln = command.data.indexed_draw_line;
    ln.pane_id = pane_id;
    ln.buffer_index = buffer_index;
    ln.x0 = x0;
    ln.y0 = y0;
    ln.x1 = x1;
    ln.y1 = y1;
    ln.palette_index = palette_index;
    return super_terminal_enqueue(ctx, &command);
}

extern "C" WINGUI_API int32_t WINGUI_CALL super_terminal_rgba_gpu_copy(
    SuperTerminalClientContext* ctx,
    SuperTerminalPaneId dst_pane_id,
    uint32_t dst_buffer_index,
    uint32_t dst_x,
    uint32_t dst_y,
    SuperTerminalPaneId src_pane_id,
    uint32_t src_buffer_index,
    uint32_t src_x,
    uint32_t src_y,
    uint32_t region_width,
    uint32_t region_height) {
    if (!ctx || !ctx->host || dst_pane_id.value == 0 || src_pane_id.value == 0 ||
        region_width == 0 || region_height == 0) {
        wingui_set_last_error_string_internal("super_terminal_rgba_gpu_copy: invalid arguments");
        return 0;
    }
    SuperTerminalCommand command{};
    command.type = SUPERTERMINAL_CMD_RGBA_GPU_COPY;
    SuperTerminalRgbaGpuCopy& cp = command.data.rgba_gpu_copy;
    cp.dst_pane_id = dst_pane_id;
    cp.dst_buffer_index = dst_buffer_index & 1u;
    cp.dst_x = dst_x;
    cp.dst_y = dst_y;
    cp.src_pane_id = src_pane_id;
    cp.src_buffer_index = src_buffer_index & 1u;
    cp.src_x = src_x;
    cp.src_y = src_y;
    cp.region_width = region_width;
    cp.region_height = region_height;
    return super_terminal_enqueue(ctx, &command);
}

extern "C" WINGUI_API int32_t WINGUI_CALL super_terminal_register_rgba_asset_owned(
    SuperTerminalClientContext* ctx,
    uint32_t width,
    uint32_t height,
    void* bgra8_pixels,
    uint32_t source_pitch,
    SuperTerminalFreeFn free_fn,
    void* free_user_data,
    SuperTerminalAssetId* out_asset_id) {
    if (out_asset_id) out_asset_id->value = 0;
    if (!ctx || !ctx->host || !bgra8_pixels || width == 0 || height == 0 || !out_asset_id) {
        wingui_set_last_error_string_internal("super_terminal_register_rgba_asset_owned: invalid arguments");
        return 0;
    }
    const uint32_t pitch = source_pitch ? source_pitch : width * 4u;
    if (pitch < width * 4u) {
        wingui_set_last_error_string_internal("super_terminal_register_rgba_asset_owned: invalid pitch");
        return 0;
    }
    SuperTerminalCommand command{};
    command.type = SUPERTERMINAL_CMD_RGBA_ASSET_REGISTER_OWNED;
    SuperTerminalRgbaAssetRegisterOwned& reg = command.data.rgba_asset_register_owned;
    reg.asset_id.value = ctx->host->next_asset_id.fetch_add(1, std::memory_order_relaxed);
    reg.width = width;
    reg.height = height;
    reg.source_pitch = pitch;
    reg.bgra8_pixels = bgra8_pixels;
    reg.free_fn = free_fn;
    reg.free_user_data = free_user_data;
    if (!super_terminal_enqueue(ctx, &command)) {
        return 0;
    }
    *out_asset_id = reg.asset_id;
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL super_terminal_asset_blit_to_pane(
    SuperTerminalClientContext* ctx,
    SuperTerminalAssetId asset_id,
    uint32_t src_x,
    uint32_t src_y,
    uint32_t region_width,
    uint32_t region_height,
    SuperTerminalPaneId dst_pane_id,
    uint32_t dst_buffer_index,
    uint32_t dst_x,
    uint32_t dst_y) {
    if (!ctx || !ctx->host || asset_id.value == 0 || dst_pane_id.value == 0 ||
        region_width == 0 || region_height == 0) {
        wingui_set_last_error_string_internal("super_terminal_asset_blit_to_pane: invalid arguments");
        return 0;
    }
    SuperTerminalCommand command{};
    command.type = SUPERTERMINAL_CMD_RGBA_ASSET_BLIT_TO_PANE;
    SuperTerminalRgbaAssetBlitToPane& blit = command.data.rgba_asset_blit_to_pane;
    blit.asset_id = asset_id;
    blit.src_x = src_x;
    blit.src_y = src_y;
    blit.region_width = region_width;
    blit.region_height = region_height;
    blit.dst_pane_id = dst_pane_id;
    blit.dst_buffer_index = dst_buffer_index & 1u;
    blit.dst_x = dst_x;
    blit.dst_y = dst_y;
    return super_terminal_enqueue(ctx, &command);
}