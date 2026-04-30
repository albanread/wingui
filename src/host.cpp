#ifndef WINGUI_BUILD_DLL
#define WINGUI_BUILD_DLL
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "wingui/host.h"

#include "wingui_internal.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr uint32_t kTerminalWakeMessage = WM_APP + 0x51;
constexpr uint32_t kDefaultQueueCapacity = 1024;
constexpr uint32_t kDefaultColumns = 80;
constexpr uint32_t kDefaultRows = 25;

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

struct WinguiHost;

struct WinguiHostClientContext {
    WinguiHost* host = nullptr;
};

struct WinguiHost {
    WinguiHostAppDesc desc{};
    WinguiWindow* window = nullptr;
    WinguiContext* context = nullptr;
    WinguiTextGridRenderer* text_renderer = nullptr;
    WinguiGlyphAtlasBitmap atlas{};
    RingQueue<WinguiHostCommand> command_queue;
    RingQueue<WinguiHostEvent> event_queue;
    WinguiHostClientContext client_ctx{};
    TerminalSurface surface;
    std::vector<WinguiGlyphInstance> glyph_instances;
    std::thread client_thread;
    std::atomic<int32_t> stop_requested{0};
    std::atomic<int32_t> render_dirty{0};
    std::atomic<int32_t> client_finished{0};
    std::atomic<int32_t> close_event_sent{0};
    std::atomic<uint32_t> command_sequence{0};
    std::atomic<uint32_t> event_sequence{0};
    int32_t exit_code = 0;
    int32_t host_error_code = WINGUI_HOST_HOST_ERROR_NONE;
    std::string message;
};

void setHostError(WinguiHost* host, int32_t code, const char* message) {
    if (host) {
        host->host_error_code = code;
        host->message = message ? message : "";
    }
    wingui_set_last_error_string_internal(message ? message : "WINGUI_HOST failure");
}

void copyUtf8Truncate(char* dst, size_t dst_size, const char* src) {
    if (!dst || dst_size == 0) return;
    dst[0] = '\0';
    if (!src) return;
    std::strncpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';
}

WinguiGraphicsColour defaultForeground() {
    return WinguiGraphicsColour{255, 255, 255, 255};
}

WinguiGraphicsColour defaultBackground() {
    return WinguiGraphicsColour{0, 0, 0, 255};
}

uint32_t clampCodepoint(const WinguiHost& host, uint32_t codepoint) {
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

bool decodeNextUtf8(const char* text, size_t length, size_t* index, uint32_t* out_codepoint) {
    if (!text || !index || !out_codepoint || *index >= length) return false;

    const uint8_t byte0 = static_cast<uint8_t>(text[*index]);
    if (byte0 < 0x80u) {
        *out_codepoint = byte0;
        ++(*index);
        return true;
    }

    uint32_t codepoint = 0;
    size_t needed = 0;
    if ((byte0 & 0xe0u) == 0xc0u) {
        codepoint = byte0 & 0x1fu;
        needed = 1;
    } else if ((byte0 & 0xf0u) == 0xe0u) {
        codepoint = byte0 & 0x0fu;
        needed = 2;
    } else if ((byte0 & 0xf8u) == 0xf0u) {
        codepoint = byte0 & 0x07u;
        needed = 3;
    } else {
        *out_codepoint = '?';
        ++(*index);
        return true;
    }

    if (*index + needed >= length) {
        *out_codepoint = '?';
        *index = length;
        return true;
    }

    for (size_t i = 0; i < needed; ++i) {
        const uint8_t cont = static_cast<uint8_t>(text[*index + 1 + i]);
        if ((cont & 0xc0u) != 0x80u) {
            *out_codepoint = '?';
            ++(*index);
            return true;
        }
        codepoint = (codepoint << 6) | (cont & 0x3fu);
    }

    *index += needed + 1;
    *out_codepoint = codepoint;
    return true;
}

bool initSurface(TerminalSurface* surface, uint32_t columns, uint32_t rows) {
    if (!surface || columns == 0 || rows == 0) return false;
    surface->columns = columns;
    surface->rows = rows;
    surface->cells.assign(static_cast<size_t>(columns) * rows, TerminalCell{});
    return true;
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
            new_cells[static_cast<size_t>(row) * columns + col] = surface->cells[static_cast<size_t>(row) * surface->columns + col];
        }
    }
    surface->columns = columns;
    surface->rows = rows;
    surface->cells.swap(new_cells);
    return true;
}

void clearSurfaceRegion(TerminalSurface* surface, const WinguiHostClearRegion& clear_region) {
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

void writeTextToSurface(WinguiHost* host, const WinguiHostWriteTextUtf8& write_text) {
    if (!host) return;
    const size_t length = std::strlen(write_text.text_utf8);
    size_t index = 0;
    uint32_t row = write_text.row;
    uint32_t column = write_text.column;
    const uint32_t start_column = write_text.column;
    while (row < host->surface.rows && index < length) {
        uint32_t codepoint = ' ';
        if (!decodeNextUtf8(write_text.text_utf8, length, &index, &codepoint)) {
            break;
        }
        if (codepoint == '\r') {
            continue;
        }
        if (codepoint == '\n') {
            ++row;
            column = start_column;
            continue;
        }
        if (column >= host->surface.columns) {
            ++row;
            column = start_column;
            if (row >= host->surface.rows) break;
        }
        TerminalCell* cell = surfaceCell(&host->surface, row, column);
        if (!cell) break;
        cell->codepoint = codepoint;
        cell->foreground = write_text.foreground;
        cell->background = write_text.background;
        ++column;
    }
}

uint32_t currentModifiers() {
    uint32_t modifiers = 0;
    if ((GetKeyState(VK_SHIFT) & 0x8000) != 0) modifiers |= 1u << 0;
    if ((GetKeyState(VK_CONTROL) & 0x8000) != 0) modifiers |= 1u << 1;
    if ((GetKeyState(VK_MENU) & 0x8000) != 0) modifiers |= 1u << 2;
    return modifiers;
}

bool pushEvent(WinguiHost* host, const WinguiHostEvent& event) {
    if (!host) return false;
    return host->event_queue.push(event);
}

void sendHostStoppingEvent(WinguiHost* host, int32_t exit_code) {
    if (!host) return;
    WinguiHostEvent event{};
    event.type = WINGUI_HOST_EVENT_HOST_STOPPING;
    event.sequence = host->event_sequence.fetch_add(1, std::memory_order_relaxed) + 1;
    event.data.host_stopping.exit_code = exit_code;
    pushEvent(host, event);
}

void requestStopInternal(WinguiHost* host, int32_t exit_code, bool close_window) {
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

bool enqueueResizeEvent(WinguiHost* host, uint32_t pixel_width, uint32_t pixel_height) {
    if (!host) return false;
    WinguiHostEvent event{};
    event.type = WINGUI_HOST_EVENT_RESIZE;
    event.sequence = host->event_sequence.fetch_add(1, std::memory_order_relaxed) + 1;
    event.data.resize.pixel_width = pixel_width;
    event.data.resize.pixel_height = pixel_height;
    event.data.resize.columns = host->surface.columns;
    event.data.resize.rows = host->surface.rows;
    event.data.resize.dpi_scale = host->desc.dpi_scale > 0.0f ? host->desc.dpi_scale : 1.0f;
    event.data.resize.cell_width = host->atlas.info.cell_width;
    event.data.resize.cell_height = host->atlas.info.cell_height;
    return pushEvent(host, event);
}

bool updateSurfaceForClientSize(WinguiHost* host) {
    if (!host || !host->window) return false;
    int32_t client_width = 0;
    int32_t client_height = 0;
    if (!wingui_window_client_size(host->window, &client_width, &client_height)) {
        return false;
    }
    const uint32_t new_columns = std::max<uint32_t>(1, static_cast<uint32_t>(client_width / std::max(1.0f, host->atlas.info.cell_width)));
    const uint32_t new_rows = std::max<uint32_t>(1, static_cast<uint32_t>(client_height / std::max(1.0f, host->atlas.info.cell_height)));
    const bool changed = new_columns != host->surface.columns || new_rows != host->surface.rows;
    if (changed) {
        if (!resizeSurfacePreserve(&host->surface, new_columns, new_rows)) {
            return false;
        }
    }
    enqueueResizeEvent(host, static_cast<uint32_t>(std::max(client_width, 0)), static_cast<uint32_t>(std::max(client_height, 0)));
    host->render_dirty.store(1, std::memory_order_release);
    return true;
}

bool renderSurface(WinguiHost* host) {
    if (!host || !host->context || !host->text_renderer) return false;

    int32_t client_width = 0;
    int32_t client_height = 0;
    if (!wingui_window_client_size(host->window, &client_width, &client_height)) {
        return false;
    }

    host->glyph_instances.resize(host->surface.cells.size());
    const uint32_t atlas_cols = std::max<uint32_t>(1, host->atlas.info.cols);
    for (uint32_t row = 0; row < host->surface.rows; ++row) {
        for (uint32_t col = 0; col < host->surface.columns; ++col) {
            const TerminalCell& cell = host->surface.cells[static_cast<size_t>(row) * host->surface.columns + col];
            const uint32_t codepoint = clampCodepoint(*host, cell.codepoint);
            const uint32_t glyph_index = codepoint - host->atlas.info.first_codepoint;
            WinguiGlyphInstance& instance = host->glyph_instances[static_cast<size_t>(row) * host->surface.columns + col];
            instance.pos_x = static_cast<float>(col);
            instance.pos_y = static_cast<float>(row);
            instance.uv_x = static_cast<float>((glyph_index % atlas_cols)) * host->atlas.info.cell_width;
            instance.uv_y = static_cast<float>((glyph_index / atlas_cols)) * host->atlas.info.cell_height;
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

    WinguiTextGridFrame frame{};
    frame.instances = host->glyph_instances.data();
    frame.instance_count = static_cast<uint32_t>(host->glyph_instances.size());
    frame.uniforms.viewport_width = static_cast<float>(std::max(client_width, 1));
    frame.uniforms.viewport_height = static_cast<float>(std::max(client_height, 1));
    frame.uniforms.cell_width = host->atlas.info.cell_width;
    frame.uniforms.cell_height = host->atlas.info.cell_height;
    frame.uniforms.atlas_width = host->atlas.info.atlas_width;
    frame.uniforms.atlas_height = host->atlas.info.atlas_height;
    frame.uniforms.row_origin = 0.0f;
    frame.uniforms.effects_mode = 0.0f;

    if (!wingui_begin_frame(host->context, 0.0f, 0.0f, 0.0f, 1.0f)) {
        return false;
    }
    if (!wingui_text_grid_renderer_render(host->text_renderer, 0, 0, client_width, client_height, &frame)) {
        return false;
    }
    if (!wingui_present(host->context, 1)) {
        return false;
    }
    host->render_dirty.store(0, std::memory_order_release);
    return true;
}

void applyCommand(WinguiHost* host, const WinguiHostCommand& command) {
    if (!host) return;
    switch (command.type) {
        case WINGUI_HOST_CMD_UI_PUBLISH_JSON:
        case WINGUI_HOST_CMD_UI_PATCH_JSON:
        case WINGUI_HOST_CMD_UPDATE_TEXT_GRID:
        case WINGUI_HOST_CMD_UPDATE_INDEXED_GRAPHICS:
        case WINGUI_HOST_CMD_UPDATE_RGBA_PANE:
            // TODO: dispatch to UI framework model
            host->render_dirty.store(1, std::memory_order_release);
            break;
        case WINGUI_HOST_CMD_SET_TITLE:
            wingui_window_set_title_utf8(host->window, command.data.set_title.title_utf8);
            break;
        case WINGUI_HOST_CMD_REQUEST_PRESENT:
            host->render_dirty.store(1, std::memory_order_release);
            break;
        case WINGUI_HOST_CMD_REQUEST_CLOSE:
            requestStopInternal(host, host->exit_code, true);
            break;
        case WINGUI_HOST_CMD_NOP:
        default:
            break;
    }
}

void drainCommands(WinguiHost* host) {
    if (!host) return;
    WinguiHostCommand command{};
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
    auto* host = static_cast<WinguiHost*>(user_data);
    if (!host || !handled) {
        return 0;
    }

    switch (message) {
        case WM_ERASEBKGND:
            *handled = 1;
            return 1;
        case WM_CLOSE: {
            if (host->close_event_sent.exchange(1, std::memory_order_acq_rel) == 0) {
                WinguiHostEvent event{};
                event.type = WINGUI_HOST_EVENT_CLOSE_REQUESTED;
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
                updateSurfaceForClientSize(host);
            }
            break;
        case WM_SETFOCUS:
        case WM_KILLFOCUS: {
            WinguiHostEvent event{};
            event.type = WINGUI_HOST_EVENT_FOCUS;
            event.sequence = host->event_sequence.fetch_add(1, std::memory_order_relaxed) + 1;
            event.data.focus.focused = message == WM_SETFOCUS ? 1 : 0;
            pushEvent(host, event);
            break;
        }
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYUP: {
            WinguiHostEvent event{};
            event.type = WINGUI_HOST_EVENT_KEY;
            event.sequence = host->event_sequence.fetch_add(1, std::memory_order_relaxed) + 1;
            event.data.key.virtual_key = static_cast<uint32_t>(wparam);
            event.data.key.repeat_count = static_cast<uint32_t>(lparam & 0xffffu);
            event.data.key.is_down = (message == WM_KEYDOWN || message == WM_SYSKEYDOWN) ? 1 : 0;
            event.data.key.modifiers = currentModifiers();
            pushEvent(host, event);
            break;
        }
        case WM_CHAR:
        case WM_SYSCHAR: {
            WinguiHostEvent event{};
            event.type = WINGUI_HOST_EVENT_CHAR;
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
                WinguiHostEvent event{};
                event.type = WINGUI_HOST_EVENT_MOUSE;
                event.sequence = host->event_sequence.fetch_add(1, std::memory_order_relaxed) + 1;
                event.data.mouse.x = mouse_state.x;
                event.data.mouse.y = mouse_state.y;
                event.data.mouse.buttons = mouse_state.buttons;
                event.data.mouse.wheel_delta = message == WM_MOUSEWHEEL ? GET_WHEEL_DELTA_WPARAM(wparam) : 0;
                event.data.mouse.button_mask = 0;
                event.data.mouse.kind = WINGUI_HOST_MOUSE_MOVE;
                if (message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN || message == WM_MBUTTONDOWN || message == WM_XBUTTONDOWN) {
                    event.data.mouse.kind = WINGUI_HOST_MOUSE_BUTTON_DOWN;
                } else if (message == WM_LBUTTONUP || message == WM_RBUTTONUP || message == WM_MBUTTONUP || message == WM_XBUTTONUP) {
                    event.data.mouse.kind = WINGUI_HOST_MOUSE_BUTTON_UP;
                } else if (message == WM_MOUSEWHEEL) {
                    event.data.mouse.kind = WINGUI_HOST_MOUSE_WHEEL;
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
            }
            break;
        }
        default:
            if (message == kTerminalWakeMessage) {
                *handled = 1;
                return 0;
            }
            break;
    }

    *handled = 0;
    return 0;
}

bool initWindowAndRenderer(WinguiHost* host) {
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
        setHostError(host, WINGUI_HOST_HOST_ERROR_GLYPH_ATLAS_CREATE, wingui_last_error_utf8());
        return false;
    }

    RECT rect{0, 0,
        static_cast<LONG>(std::max<uint32_t>(1, host->desc.columns) * static_cast<uint32_t>(host->atlas.info.cell_width)),
        static_cast<LONG>(std::max<uint32_t>(1, host->desc.rows) * static_cast<uint32_t>(host->atlas.info.cell_height))};
    AdjustWindowRectEx(&rect, WS_OVERLAPPEDWINDOW, FALSE, 0);

    WinguiWindowDesc window_desc{};
    window_desc.class_name_utf8 = "WinguiHostWindow";
    window_desc.title_utf8 = host->desc.title_utf8 && *host->desc.title_utf8 ? host->desc.title_utf8 : "Wingui Terminal";
    window_desc.width = rect.right - rect.left;
    window_desc.height = rect.bottom - rect.top;
    window_desc.style = WS_OVERLAPPEDWINDOW;
    window_desc.window_proc = hostWindowProc;
    window_desc.user_data = host;
    if (!wingui_create_window_utf8(&window_desc, &host->window)) {
        setHostError(host, WINGUI_HOST_HOST_ERROR_WINDOW_CREATE, wingui_last_error_utf8());
        return false;
    }

    int32_t client_width = 0;
    int32_t client_height = 0;
    if (!wingui_window_client_size(host->window, &client_width, &client_height)) {
        setHostError(host, WINGUI_HOST_HOST_ERROR_WINDOW_CREATE, wingui_last_error_utf8());
        return false;
    }

    WinguiContextDesc context_desc{};
    context_desc.hwnd = wingui_window_hwnd(host->window);
    context_desc.width = static_cast<uint32_t>(std::max(client_width, 1));
    context_desc.height = static_cast<uint32_t>(std::max(client_height, 1));
    context_desc.buffer_count = 2;
    context_desc.vsync_interval = 1;
    if (!wingui_create_context(&context_desc, &host->context)) {
        setHostError(host, WINGUI_HOST_HOST_ERROR_CONTEXT_CREATE, wingui_last_error_utf8());
        return false;
    }

    WinguiTextGridRendererDesc renderer_desc{};
    renderer_desc.context = host->context;
    renderer_desc.shader_path_utf8 = host->desc.text_shader_path_utf8;
    if (!wingui_create_text_grid_renderer(&renderer_desc, &host->text_renderer)) {
        setHostError(host, WINGUI_HOST_HOST_ERROR_RENDERER_CREATE, wingui_last_error_utf8());
        return false;
    }
    if (!wingui_text_grid_renderer_set_atlas(host->text_renderer, &host->atlas)) {
        setHostError(host, WINGUI_HOST_HOST_ERROR_RENDERER_CREATE, wingui_last_error_utf8());
        return false;
    }
    return true;
}

void shutdownHost(WinguiHost* host) {
    if (!host) return;
    if (host->client_thread.joinable()) {
        host->client_thread.join();
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

void clientThreadMain(WinguiHost* host) {
    int32_t client_exit_code = 0;
    if (host && host->desc.startup) {
        try {
            client_exit_code = host->desc.startup(&host->client_ctx, host->desc.user_data);
        } catch (...) {
            client_exit_code = -1;
            setHostError(host, WINGUI_HOST_HOST_ERROR_CLIENT_START, "WINGUI_HOST_run: client startup threw an exception");
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

bool pumpMessages(WinguiHost* host) {
    if (!host) return false;

    const bool should_wait = host->render_dirty.load(std::memory_order_acquire) == 0 && host->command_queue.empty();
    MSG msg{};
    if (should_wait) {
        const BOOL result = GetMessageW(&msg, nullptr, 0, 0);
        if (result == -1) {
            setHostError(host, WINGUI_HOST_HOST_ERROR_MESSAGE_LOOP, "WINGUI_HOST_run: GetMessageW failed");
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

void fillRunResult(const WinguiHost& host, WinguiHostRunResult* out_result) {
    if (!out_result) return;
    out_result->exit_code = host.exit_code;
    out_result->host_error_code = host.host_error_code;
    copyUtf8Truncate(out_result->message_utf8, sizeof(out_result->message_utf8), host.message.c_str());
}

} // namespace

extern "C" WINGUI_API int32_t WINGUI_CALL WINGUI_HOST_run(
    const WinguiHostAppDesc* desc,
    WinguiHostRunResult* out_result) {
    if (out_result) {
        std::memset(out_result, 0, sizeof(*out_result));
    }
    if (!desc || !desc->startup) {
        wingui_set_last_error_string_internal("WINGUI_HOST_run: invalid arguments");
        return 0;
    }

    WinguiHost host{};
    host.desc = *desc;
    host.client_ctx.host = &host;
    host.desc.columns = host.desc.columns ? host.desc.columns : kDefaultColumns;
    host.desc.rows = host.desc.rows ? host.desc.rows : kDefaultRows;
    host.message.clear();

    if (!host.command_queue.init(host.desc.command_queue_capacity ? host.desc.command_queue_capacity : kDefaultQueueCapacity, false)) {
        setHostError(&host, WINGUI_HOST_HOST_ERROR_INVALID_ARGUMENT, "WINGUI_HOST_run: failed to initialize command queue");
        fillRunResult(host, out_result);
        return 0;
    }
    if (!host.event_queue.init(host.desc.event_queue_capacity ? host.desc.event_queue_capacity : kDefaultQueueCapacity, true)) {
        setHostError(&host, WINGUI_HOST_HOST_ERROR_INVALID_ARGUMENT, "WINGUI_HOST_run: failed to initialize event queue");
        shutdownHost(&host);
        fillRunResult(host, out_result);
        return 0;
    }
    if (!initSurface(&host.surface, host.desc.columns, host.desc.rows)) {
        setHostError(&host, WINGUI_HOST_HOST_ERROR_INVALID_ARGUMENT, "WINGUI_HOST_run: failed to initialize terminal surface");
        shutdownHost(&host);
        fillRunResult(host, out_result);
        return 0;
    }
    if (!initWindowAndRenderer(&host)) {
        shutdownHost(&host);
        fillRunResult(host, out_result);
        return 0;
    }
    if ((host.desc.flags & WINGUI_HOST_APP_ENABLE_AUDIO) != 0) {
        wingui_audio_init();
    }

    host.render_dirty.store(1, std::memory_order_release);
    updateSurfaceForClientSize(&host);

    host.client_thread = std::thread(clientThreadMain, &host);
    if (!wingui_window_show(host.window, SW_SHOWDEFAULT)) {
        setHostError(&host, WINGUI_HOST_HOST_ERROR_WINDOW_CREATE, wingui_last_error_utf8());
        requestStopInternal(&host, -1, true);
    }

    while (true) {
        drainCommands(&host);
        if (host.render_dirty.load(std::memory_order_acquire) != 0) {
            if (!renderSurface(&host)) {
                setHostError(&host, WINGUI_HOST_HOST_ERROR_RENDERER_CREATE, wingui_last_error_utf8());
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
    if ((host.desc.flags & WINGUI_HOST_APP_ENABLE_AUDIO) != 0) {
        wingui_audio_shutdown();
    }
    shutdownHost(&host);
    fillRunResult(host, out_result);
    wingui_clear_last_error_internal();
    return host.host_error_code == WINGUI_HOST_HOST_ERROR_NONE ? 1 : 0;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_host_enqueue(
    WinguiHostClientContext* ctx,
    const WinguiHostCommand* command) {
    if (!ctx || !ctx->host || !command) {
        wingui_set_last_error_string_internal("wingui_host_enqueue: invalid arguments");
        return 0;
    }
    WinguiHostCommand command_copy = *command;
    command_copy.sequence = ctx->host->command_sequence.fetch_add(1, std::memory_order_relaxed) + 1;
    if (!ctx->host->command_queue.push(command_copy)) {
        wingui_set_last_error_string_internal("wingui_host_enqueue: queue is full");
        return 0;
    }
    HWND hwnd = static_cast<HWND>(wingui_window_hwnd(ctx->host->window));
    if (hwnd) {
        PostMessageW(hwnd, kTerminalWakeMessage, 0, 0);
    }
    wingui_clear_last_error_internal();
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL WINGUI_HOST_wait_event(
    WinguiHostClientContext* ctx,
    uint32_t timeout_ms,
    WinguiHostEvent* out_event) {
    if (!ctx || !ctx->host || !out_event) {
        wingui_set_last_error_string_internal("WINGUI_HOST_wait_event: invalid arguments");
        return 0;
    }
    const DWORD wait_ms = timeout_ms == WINGUI_HOST_WAIT_INFINITE ? INFINITE : timeout_ms;
    const DWORD wait_result = WaitForSingleObject(ctx->host->event_queue.event_handle, wait_ms);
    if (wait_result == WAIT_TIMEOUT) {
        wingui_clear_last_error_internal();
        return 0;
    }
    if (wait_result != WAIT_OBJECT_0) {
        wingui_set_last_error_string_internal("WINGUI_HOST_wait_event: wait failed");
        return 0;
    }
    if (!ctx->host->event_queue.pop(out_event)) {
        wingui_clear_last_error_internal();
        return 0;
    }
    wingui_clear_last_error_internal();
    return 1;
}

extern "C" WINGUI_API void* WINGUI_CALL WINGUI_HOST_event_handle(
    WinguiHostClientContext* ctx) {
    if (!ctx || !ctx->host) {
        wingui_set_last_error_string_internal("WINGUI_HOST_event_handle: invalid arguments");
        return nullptr;
    }
    wingui_clear_last_error_internal();
    return ctx->host->event_queue.event_handle;
}

extern "C" WINGUI_API int32_t WINGUI_CALL WINGUI_HOST_request_stop(
    WinguiHostClientContext* ctx,
    int32_t exit_code) {
    if (!ctx || !ctx->host) {
        wingui_set_last_error_string_internal("WINGUI_HOST_request_stop: invalid arguments");
        return 0;
    }
    requestStopInternal(ctx->host, exit_code, true);
    wingui_clear_last_error_internal();
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL WINGUI_HOST_get_key_state(
    WinguiHostClientContext* ctx,
    uint32_t virtual_key) {
    if (!ctx || !ctx->host || !ctx->host->window) {
        wingui_set_last_error_string_internal("WINGUI_HOST_get_key_state: invalid arguments");
        return 0;
    }
    return wingui_window_get_key_state(ctx->host->window, virtual_key);
}

extern "C" WINGUI_API int32_t WINGUI_CALL WINGUI_HOST_get_mouse_state(
    WinguiHostClientContext* ctx,
    WinguiMouseState* out_state) {
    if (!ctx || !ctx->host || !ctx->host->window) {
        wingui_set_last_error_string_internal("WINGUI_HOST_get_mouse_state: invalid arguments");
        return 0;
    }
    return wingui_window_get_mouse_state(ctx->host->window, out_state);
}

extern "C" WINGUI_API int32_t WINGUI_CALL WINGUI_HOST_write_text_utf8(
    WinguiHostClientContext* ctx,
    uint32_t row,
    uint32_t column,
    const char* text_utf8,
    WinguiGraphicsColour foreground,
    WinguiGraphicsColour background) {
    if (!text_utf8) {
        wingui_set_last_error_string_internal("WINGUI_HOST_write_text_utf8: invalid arguments");
        return 0;
    }
    WinguiHostCommand command{};
    command.type = WINGUI_HOST_CMD_WRITE_TEXT_UTF8;
    command.data.write_text_utf8.row = row;
    command.data.write_text_utf8.column = column;
    command.data.write_text_utf8.foreground = foreground;
    command.data.write_text_utf8.background = background;
    copyUtf8Truncate(command.data.write_text_utf8.text_utf8, sizeof(command.data.write_text_utf8.text_utf8), text_utf8);
    return wingui_host_enqueue(ctx, &command);
}

extern "C" WINGUI_API int32_t WINGUI_CALL WINGUI_HOST_clear_region(
    WinguiHostClientContext* ctx,
    uint32_t row,
    uint32_t column,
    uint32_t width,
    uint32_t height,
    uint32_t fill_codepoint,
    WinguiGraphicsColour foreground,
    WinguiGraphicsColour background) {
    WinguiHostCommand command{};
    command.type = WINGUI_HOST_CMD_CLEAR_REGION;
    command.data.clear_region.row = row;
    command.data.clear_region.column = column;
    command.data.clear_region.width = width;
    command.data.clear_region.height = height;
    command.data.clear_region.fill_codepoint = fill_codepoint;
    command.data.clear_region.foreground = foreground;
    command.data.clear_region.background = background;
    return wingui_host_enqueue(ctx, &command);
}

extern "C" WINGUI_API int32_t WINGUI_CALL WINGUI_HOST_set_title_utf8(
    WinguiHostClientContext* ctx,
    const char* title_utf8) {
    if (!title_utf8) {
        wingui_set_last_error_string_internal("WINGUI_HOST_set_title_utf8: invalid arguments");
        return 0;
    }
    WinguiHostCommand command{};
    command.type = WINGUI_HOST_CMD_SET_TITLE;
    copyUtf8Truncate(command.data.set_title.title_utf8, sizeof(command.data.set_title.title_utf8), title_utf8);
    return wingui_host_enqueue(ctx, &command);
}

extern "C" WINGUI_API int32_t WINGUI_CALL WINGUI_HOST_present(
    WinguiHostClientContext* ctx) {
    WinguiHostCommand command{};
    command.type = WINGUI_HOST_CMD_REQUEST_PRESENT;
    return wingui_host_enqueue(ctx, &command);
}
