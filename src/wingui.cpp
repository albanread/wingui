#ifndef WINGUI_BUILD_DLL
#define WINGUI_BUILD_DLL
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "wingui/wingui.h"

#include "wingui_internal.h"

#include <windows.h>
#include <windowsx.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <wincodec.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <new>
#include <string>
#include <vector>

struct WinguiMenu {
    HMENU handle = nullptr;
    bool attached_to_parent = false;
};

struct WinguiWindow {
    HWND hwnd = nullptr;
    std::wstring class_name;
    WinguiWindowProc window_proc = nullptr;
    void* user_data = nullptr;
    std::atomic<uint8_t> key_states[256]{};
    std::atomic<int32_t> mouse_x{0};
    std::atomic<int32_t> mouse_y{0};
    std::atomic<uint32_t> mouse_buttons{0};
    std::atomic<int32_t> mouse_inside_client{0};
    bool mouse_tracking_active = false;
};

struct WinguiContext {
    HWND hwnd = nullptr;
    UINT width = 0;
    UINT height = 0;
    UINT buffer_count = 2;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* device_context = nullptr;
    IDXGISwapChain* swap_chain = nullptr;
    ID3D11RenderTargetView* render_target_view = nullptr;
};

struct WinguiTextGridRenderer {
    WinguiContext* context = nullptr;
    ID3D11VertexShader* vertex_shader = nullptr;
    ID3D11PixelShader* pixel_shader = nullptr;
    ID3D11InputLayout* input_layout = nullptr;
    ID3D11Buffer* constant_buffer = nullptr;
    ID3D11Buffer* instance_buffer = nullptr;
    ID3D11SamplerState* sampler = nullptr;
    ID3D11RasterizerState* rasterizer = nullptr;
    ID3D11Texture2D* atlas_texture = nullptr;
    ID3D11ShaderResourceView* atlas_srv = nullptr;
    size_t instance_capacity = 0;
    WinguiGlyphAtlasInfo atlas_info{};
};

struct WinguiSpriteVertex {
    float pos[2];
    float atlas_px[2];
    float palette_slot;
    float alpha;
    float effect_type;
    float effect_param1;
    float effect_param2;
    float effect_colour[4];
};

struct WinguiGraphicsDisplayUniforms {
    float uv_scale_x;
    float uv_scale_y;
    float uv_offset_x;
    float uv_offset_y;
    uint32_t screen_width;
    uint32_t screen_height;
    uint32_t buffer_width;
    uint32_t buffer_height;
    int32_t scroll_x;
    int32_t scroll_y;
    uint32_t has_texture;
    uint32_t reserved0;
};

struct WinguiIndexedGraphicsRenderer {
    WinguiContext* context = nullptr;
    ID3D11VertexShader* graphics_vertex_shader = nullptr;
    ID3D11PixelShader* graphics_pixel_shader = nullptr;
    ID3D11Buffer* graphics_constant_buffer = nullptr;
    ID3D11SamplerState* point_sampler = nullptr;
    ID3D11RasterizerState* rasterizer = nullptr;
    ID3D11Texture2D* graphics_indexed_texture = nullptr;
    ID3D11ShaderResourceView* graphics_indexed_srv = nullptr;
    ID3D11Texture2D* graphics_line_palette_texture = nullptr;
    ID3D11ShaderResourceView* graphics_line_palette_srv = nullptr;
    ID3D11Texture2D* graphics_global_palette_texture = nullptr;
    ID3D11ShaderResourceView* graphics_global_palette_srv = nullptr;
    uint32_t graphics_buffer_width = 0;
    uint32_t graphics_buffer_height = 0;

    ID3D11VertexShader* sprite_vertex_shader = nullptr;
    ID3D11PixelShader* sprite_pixel_shader = nullptr;
    ID3D11InputLayout* sprite_input_layout = nullptr;
    ID3D11Buffer* sprite_constant_buffer = nullptr;
    ID3D11Buffer* sprite_vertex_buffer = nullptr;
    ID3D11Texture2D* sprite_atlas_texture = nullptr;
    ID3D11ShaderResourceView* sprite_atlas_srv = nullptr;
    ID3D11Texture2D* sprite_palette_texture = nullptr;
    ID3D11ShaderResourceView* sprite_palette_srv = nullptr;
    ID3D11BlendState* sprite_blend_state = nullptr;
    size_t sprite_vertex_capacity = 0;
    uint32_t sprite_atlas_size = 2048;
    uint32_t sprite_max_palettes = 1024;
    std::vector<WinguiSpriteVertex> sprite_vertices;
};

struct WinguiRgbaBufferResources {
    ID3D11Texture2D* texture = nullptr;
    ID3D11ShaderResourceView* srv = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
};

struct WinguiRgbaPaneRenderer {
    WinguiContext* context = nullptr;
    ID3D11VertexShader* vertex_shader = nullptr;
    ID3D11PixelShader* pixel_shader = nullptr;
    ID3D11Buffer* constant_buffer = nullptr;
    ID3D11SamplerState* linear_sampler = nullptr;
    ID3D11RasterizerState* rasterizer = nullptr;
    std::vector<WinguiRgbaBufferResources> buffers;
    uint32_t buffer_width = 0;
    uint32_t buffer_height = 0;
};

struct WinguiRgbaSurface {
    WinguiContext* context = nullptr;
    std::vector<WinguiRgbaBufferResources> buffers;
    uint32_t buffer_width = 0;
    uint32_t buffer_height = 0;
};

struct WinguiRgbaBlitter {
    WinguiContext* context = nullptr;
    ID3D11VertexShader* vertex_shader = nullptr;
    ID3D11PixelShader* pixel_shader = nullptr;
    ID3D11Buffer* constant_buffer = nullptr;
    ID3D11SamplerState* linear_sampler = nullptr;
    ID3D11RasterizerState* rasterizer = nullptr;
    ID3D11BlendState* blend_opaque = nullptr;
    ID3D11BlendState* blend_alpha_over = nullptr;
};

struct WinguiRgbaBlitUniforms {
    float dst_pos_min[2];
    float dst_pos_max[2];
    float src_uv_min[2];
    float src_uv_max[2];
    float tint[4];
    uint32_t pad0;
    uint32_t pad1;
    uint32_t pad2;
    uint32_t pad3;
};

struct WinguiIndexedBufferResources {
    ID3D11Texture2D* pixels_texture = nullptr;
    ID3D11ShaderResourceView* pixels_srv = nullptr;
    ID3D11Texture2D* line_palette_texture = nullptr;
    ID3D11ShaderResourceView* line_palette_srv = nullptr;
    ID3D11Texture2D* global_palette_texture = nullptr;
    ID3D11ShaderResourceView* global_palette_srv = nullptr;
};

struct WinguiIndexedSurface {
    WinguiContext* context = nullptr;
    std::vector<WinguiIndexedBufferResources> buffers;
    uint32_t buffer_width = 0;
    uint32_t buffer_height = 0;
};

thread_local std::string g_last_error;

void wingui_set_last_error_string_internal(const char* text) {
    g_last_error = text ? text : "";
}

void wingui_set_last_error_hresult_internal(const char* prefix, long hr) {
    char buffer[256];
    std::snprintf(buffer, sizeof(buffer), "%s (HRESULT=0x%08lx)", prefix ? prefix : "wingui failure", static_cast<unsigned long>(hr));
    g_last_error = buffer;
}

void wingui_clear_last_error_internal() {
    g_last_error.clear();
}

namespace {

void setMouseButtonState(WinguiWindow* window, uint32_t mask, bool pressed) {
    if (!window) return;
    if (pressed) {
        window->mouse_buttons.fetch_or(mask, std::memory_order_release);
    } else {
        window->mouse_buttons.fetch_and(~mask, std::memory_order_release);
    }
}

void clearAllInputState(WinguiWindow* window) {
    if (!window) return;
    for (auto& key_state : window->key_states) {
        key_state.store(0, std::memory_order_release);
    }
    window->mouse_buttons.store(0, std::memory_order_release);
}

void updatePolledInputState(WinguiWindow* window, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (!window) return;

    auto store_key = [&](WPARAM key, uint8_t value) {
        if (key <= 0xffu) {
            window->key_states[static_cast<uint8_t>(key)].store(value, std::memory_order_release);
        }
    };

    switch (msg) {
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            store_key(wparam, 1);
            break;
        case WM_KEYUP:
        case WM_SYSKEYUP:
            store_key(wparam, 0);
            break;
        case WM_KILLFOCUS:
        case WM_CAPTURECHANGED:
            clearAllInputState(window);
            break;
        case WM_MOUSEMOVE: {
            window->mouse_x.store(GET_X_LPARAM(lparam), std::memory_order_release);
            window->mouse_y.store(GET_Y_LPARAM(lparam), std::memory_order_release);
            window->mouse_inside_client.store(1, std::memory_order_release);
            if (!window->mouse_tracking_active) {
                TRACKMOUSEEVENT track{};
                track.cbSize = sizeof(track);
                track.dwFlags = TME_LEAVE;
                track.hwndTrack = window->hwnd;
                if (TrackMouseEvent(&track)) {
                    window->mouse_tracking_active = true;
                }
            }
            break;
        }
        case WM_MOUSELEAVE:
            window->mouse_inside_client.store(0, std::memory_order_release);
            window->mouse_tracking_active = false;
            break;
        case WM_LBUTTONDOWN:
            setMouseButtonState(window, WINGUI_MOUSE_BUTTON_LEFT, true);
            break;
        case WM_LBUTTONUP:
            setMouseButtonState(window, WINGUI_MOUSE_BUTTON_LEFT, false);
            break;
        case WM_RBUTTONDOWN:
            setMouseButtonState(window, WINGUI_MOUSE_BUTTON_RIGHT, true);
            break;
        case WM_RBUTTONUP:
            setMouseButtonState(window, WINGUI_MOUSE_BUTTON_RIGHT, false);
            break;
        case WM_MBUTTONDOWN:
            setMouseButtonState(window, WINGUI_MOUSE_BUTTON_MIDDLE, true);
            break;
        case WM_MBUTTONUP:
            setMouseButtonState(window, WINGUI_MOUSE_BUTTON_MIDDLE, false);
            break;
        case WM_XBUTTONDOWN:
            setMouseButtonState(
                window,
                GET_XBUTTON_WPARAM(wparam) == XBUTTON1 ? WINGUI_MOUSE_BUTTON_X1 : WINGUI_MOUSE_BUTTON_X2,
                true);
            break;
        case WM_XBUTTONUP:
            setMouseButtonState(
                window,
                GET_XBUTTON_WPARAM(wparam) == XBUTTON1 ? WINGUI_MOUSE_BUTTON_X1 : WINGUI_MOUSE_BUTTON_X2,
                false);
            break;
        default:
            break;
    }
}

template <typename T>
void safeRelease(T*& ptr) {
    if (ptr) {
        ptr->Release();
        ptr = nullptr;
    }
}

void setLastErrorString(const char* text) {
    wingui_set_last_error_string_internal(text);
}

void setLastErrorHresult(const char* prefix, HRESULT hr) {
    wingui_set_last_error_hresult_internal(prefix, hr);
}

std::wstring utf8ToWide(const char* input) {
    if (!input || !*input) return {};
    const int needed = MultiByteToWideChar(CP_UTF8, 0, input, -1, nullptr, 0);
    if (needed <= 1) return {};
    std::wstring out(static_cast<size_t>(needed - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, input, -1, out.data(), needed);
    return out;
}

std::wstring fullPathWide(const std::wstring& path) {
    if (path.empty()) return {};
    const DWORD needed = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (needed == 0) return path;
    std::wstring result(static_cast<size_t>(needed), L'\0');
    const DWORD written = GetFullPathNameW(path.c_str(), needed, result.data(), nullptr);
    if (written == 0 || written >= needed) return path;
    result.resize(static_cast<size_t>(written));
    return result;
}

const GUID& containerFormatForPath(const std::wstring& path) {
    const size_t dot = path.find_last_of(L'.');
    if (dot != std::wstring::npos) {
        const std::wstring ext = path.substr(dot);
        if (_wcsicmp(ext.c_str(), L".bmp") == 0) return GUID_ContainerFormatBmp;
        if (_wcsicmp(ext.c_str(), L".jpg") == 0 || _wcsicmp(ext.c_str(), L".jpeg") == 0) return GUID_ContainerFormatJpeg;
        if (_wcsicmp(ext.c_str(), L".tif") == 0 || _wcsicmp(ext.c_str(), L".tiff") == 0) return GUID_ContainerFormatTiff;
    }
    return GUID_ContainerFormatPng;
}

bool use24BitOutputForPath(const std::wstring& path) {
    const size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos) return false;
    const std::wstring ext = path.substr(dot);
    return _wcsicmp(ext.c_str(), L".jpg") == 0 || _wcsicmp(ext.c_str(), L".jpeg") == 0;
}

HRESULT createWicFactory(IWICImagingFactory** out_factory) {
    if (!out_factory) return E_POINTER;
    *out_factory = nullptr;
    return CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        __uuidof(IWICImagingFactory),
        reinterpret_cast<void**>(out_factory));
}

HRESULT readBackTextureBgra(
    ID3D11Device* device,
    ID3D11DeviceContext* context,
    ID3D11Texture2D* source,
    std::vector<uint8_t>& pixels_out,
    uint32_t& width_out,
    uint32_t& height_out,
    uint32_t& stride_out) {
    if (!device || !context || !source) return E_INVALIDARG;

    D3D11_TEXTURE2D_DESC desc{};
    source->GetDesc(&desc);
    if (desc.Width == 0 || desc.Height == 0) return E_FAIL;

    D3D11_TEXTURE2D_DESC staging_desc = desc;
    staging_desc.BindFlags = 0;
    staging_desc.MiscFlags = 0;
    staging_desc.Usage = D3D11_USAGE_STAGING;
    staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    ID3D11Texture2D* staging = nullptr;
    HRESULT hr = device->CreateTexture2D(&staging_desc, nullptr, &staging);
    if (FAILED(hr) || !staging) return FAILED(hr) ? hr : E_FAIL;

    context->CopyResource(staging, source);
    context->Flush();

    D3D11_MAPPED_SUBRESOURCE mapped{};
    hr = context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        safeRelease(staging);
        return hr;
    }

    width_out = desc.Width;
    height_out = desc.Height;
    stride_out = desc.Width * 4u;
    pixels_out.resize(static_cast<size_t>(stride_out) * desc.Height);
    for (uint32_t y = 0; y < desc.Height; ++y) {
        const uint8_t* src = static_cast<const uint8_t*>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch;
        uint8_t* dst = pixels_out.data() + static_cast<size_t>(y) * stride_out;
        std::memcpy(dst, src, stride_out);
    }

    context->Unmap(staging, 0);
    safeRelease(staging);
    return S_OK;
}

HMODULE currentModuleHandle() {
    HMODULE module = nullptr;
    GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&currentModuleHandle),
        &module);
    return module;
}

LRESULT CALLBACK winguiWindowProcThunk(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    WinguiWindow* window = reinterpret_cast<WinguiWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        const auto* create_struct = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        window = static_cast<WinguiWindow*>(create_struct->lpCreateParams);
        if (window) {
            window->hwnd = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
        }
    }

    updatePolledInputState(window, msg, wparam, lparam);

    if (window && window->window_proc) {
        int32_t handled = 0;
        const intptr_t result = window->window_proc(window, window->user_data, msg, static_cast<uintptr_t>(wparam), static_cast<intptr_t>(lparam), &handled);
        if (msg == WM_NCDESTROY) {
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            window->hwnd = nullptr;
        }
        if (handled) return static_cast<LRESULT>(result);
    } else if (msg == WM_NCDESTROY && window) {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        window->hwnd = nullptr;
    }

    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

bool ensureWindowClassRegistered(const WinguiWindowDesc& desc, std::wstring& class_name_out) {
    class_name_out = utf8ToWide(desc.class_name_utf8 && *desc.class_name_utf8 ? desc.class_name_utf8 : "WinguiWindow");
    if (class_name_out.empty()) class_name_out = L"WinguiWindow";

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = desc.class_style;
    wc.lpfnWndProc = winguiWindowProcThunk;
    wc.hInstance = currentModuleHandle();
    wc.hIcon = static_cast<HICON>(desc.icon);
    wc.hCursor = desc.cursor ? static_cast<HCURSOR>(desc.cursor) : LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = static_cast<HBRUSH>(desc.background_brush);
    wc.lpszClassName = class_name_out.c_str();
    wc.hIconSm = static_cast<HICON>(desc.small_icon);
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }
    return true;
}

bool setMenuItemState(HMENU menu, UINT command_id, UINT state_mask, UINT state_value) {
    MENUITEMINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = MIIM_STATE;
    info.fState = state_value;
    return SetMenuItemInfoW(menu, command_id, FALSE, &info) != FALSE;
}

constexpr D3D11_INPUT_ELEMENT_DESC kWinguiTextGridInstanceLayout[] = {
    { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(WinguiGlyphInstance, pos_x), D3D11_INPUT_PER_INSTANCE_DATA, 1 },
    { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(WinguiGlyphInstance, uv_x), D3D11_INPUT_PER_INSTANCE_DATA, 1 },
    { "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, offsetof(WinguiGlyphInstance, fg), D3D11_INPUT_PER_INSTANCE_DATA, 1 },
    { "COLOR", 1, DXGI_FORMAT_R8G8B8A8_UNORM, 0, offsetof(WinguiGlyphInstance, bg), D3D11_INPUT_PER_INSTANCE_DATA, 1 },
    { "BLENDINDICES", 0, DXGI_FORMAT_R32_UINT, 0, offsetof(WinguiGlyphInstance, flags), D3D11_INPUT_PER_INSTANCE_DATA, 1 },
};

constexpr D3D11_INPUT_ELEMENT_DESC kWinguiSpriteLayout[] = {
    { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    { "TEXCOORD", 1, DXGI_FORMAT_R32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    { "TEXCOORD", 2, DXGI_FORMAT_R32_FLOAT, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    { "TEXCOORD", 3, DXGI_FORMAT_R32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    { "TEXCOORD", 4, DXGI_FORMAT_R32_FLOAT, 0, 28, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    { "TEXCOORD", 5, DXGI_FORMAT_R32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0 },
};

uint32_t packGraphicsColour(const WinguiGraphicsColour& colour) {
    return static_cast<uint32_t>(colour.r) |
        (static_cast<uint32_t>(colour.g) << 8u) |
        (static_cast<uint32_t>(colour.b) << 16u) |
        (static_cast<uint32_t>(colour.a) << 24u);
}

bool computeIndexedPaneLayoutInternal(
    int32_t viewport_x,
    int32_t viewport_y,
    int32_t viewport_width,
    int32_t viewport_height,
    uint32_t screen_width,
    uint32_t screen_height,
    uint32_t pixel_aspect_num,
    uint32_t pixel_aspect_den,
    WinguiIndexedPaneLayout& out_layout) {
    if (viewport_width <= 0 || viewport_height <= 0 || screen_width == 0 || screen_height == 0) return false;
    const float par = static_cast<float>(pixel_aspect_num ? pixel_aspect_num : 1u) /
        std::max(1.0f, static_cast<float>(pixel_aspect_den ? pixel_aspect_den : 1u));
    const float display_w = static_cast<float>(screen_width);
    const float display_h = static_cast<float>(screen_height) * std::max(par, 0.0001f);
    const float pane_w = std::max(1.0f, static_cast<float>(viewport_width));
    const float pane_h = std::max(1.0f, static_cast<float>(viewport_height));
    const float scale = std::min(pane_w / display_w, pane_h / display_h);
    out_layout.shown_width = display_w * scale;
    out_layout.shown_height = display_h * scale;
    out_layout.origin_x = static_cast<float>(viewport_x) + (pane_w - out_layout.shown_width) * 0.5f;
    out_layout.origin_y = static_cast<float>(viewport_y) + (pane_h - out_layout.shown_height) * 0.5f;
    out_layout.scale_x = out_layout.shown_width / display_w;
    out_layout.scale_y = out_layout.shown_height / static_cast<float>(screen_height);
    return true;
}

HRESULT ensureIndexedGraphicsTextures(WinguiIndexedGraphicsRenderer& renderer, uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) return E_INVALIDARG;
    if (renderer.graphics_indexed_texture &&
        renderer.graphics_line_palette_texture &&
        renderer.graphics_global_palette_texture &&
        renderer.graphics_buffer_width == width &&
        renderer.graphics_buffer_height == height) {
        return S_OK;
    }

    safeRelease(renderer.graphics_indexed_srv);
    safeRelease(renderer.graphics_indexed_texture);
    safeRelease(renderer.graphics_line_palette_srv);
    safeRelease(renderer.graphics_line_palette_texture);
    safeRelease(renderer.graphics_global_palette_srv);
    safeRelease(renderer.graphics_global_palette_texture);
    renderer.graphics_buffer_width = 0;
    renderer.graphics_buffer_height = 0;

    auto create_dynamic_texture = [&](UINT tex_width, UINT tex_height, DXGI_FORMAT format, ID3D11Texture2D** texture_out, ID3D11ShaderResourceView** srv_out) -> HRESULT {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = tex_width;
        desc.Height = tex_height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = format;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        HRESULT hr = renderer.context->device->CreateTexture2D(&desc, nullptr, texture_out);
        if (FAILED(hr)) return hr;

        D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc{};
        srv_desc.Format = format;
        srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srv_desc.Texture2D.MipLevels = 1;
        hr = renderer.context->device->CreateShaderResourceView(*texture_out, &srv_desc, srv_out);
        if (FAILED(hr)) {
            safeRelease(*texture_out);
            return hr;
        }
        return S_OK;
    };

    ID3D11Texture2D* indexed_texture = nullptr;
    ID3D11ShaderResourceView* indexed_srv = nullptr;
    ID3D11Texture2D* line_palette_texture = nullptr;
    ID3D11ShaderResourceView* line_palette_srv = nullptr;
    ID3D11Texture2D* global_palette_texture = nullptr;
    ID3D11ShaderResourceView* global_palette_srv = nullptr;

    HRESULT hr = create_dynamic_texture(width, height, DXGI_FORMAT_R8_UINT, &indexed_texture, &indexed_srv);
    if (FAILED(hr)) goto cleanup;
    hr = create_dynamic_texture(16, height, DXGI_FORMAT_R32_UINT, &line_palette_texture, &line_palette_srv);
    if (FAILED(hr)) goto cleanup;
    hr = create_dynamic_texture(240, 1, DXGI_FORMAT_R32_UINT, &global_palette_texture, &global_palette_srv);
    if (FAILED(hr)) goto cleanup;

    renderer.graphics_indexed_texture = indexed_texture;
    renderer.graphics_indexed_srv = indexed_srv;
    renderer.graphics_line_palette_texture = line_palette_texture;
    renderer.graphics_line_palette_srv = line_palette_srv;
    renderer.graphics_global_palette_texture = global_palette_texture;
    renderer.graphics_global_palette_srv = global_palette_srv;
    renderer.graphics_buffer_width = width;
    renderer.graphics_buffer_height = height;
    return S_OK;

cleanup:
    safeRelease(indexed_srv);
    safeRelease(indexed_texture);
    safeRelease(line_palette_srv);
    safeRelease(line_palette_texture);
    safeRelease(global_palette_srv);
    safeRelease(global_palette_texture);
    return hr;
}

HRESULT ensureSpriteVertexBuffer(WinguiIndexedGraphicsRenderer& renderer, size_t needed_vertices) {
    if (needed_vertices <= renderer.sprite_vertex_capacity) return S_OK;

    size_t new_capacity = std::max<size_t>(3072, renderer.sprite_vertex_capacity ? renderer.sprite_vertex_capacity : 3072);
    while (new_capacity < needed_vertices) new_capacity *= 2;

    safeRelease(renderer.sprite_vertex_buffer);
    D3D11_BUFFER_DESC desc{};
    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    desc.ByteWidth = static_cast<UINT>(new_capacity * sizeof(WinguiSpriteVertex));
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    const HRESULT hr = renderer.context->device->CreateBuffer(&desc, nullptr, &renderer.sprite_vertex_buffer);
    if (FAILED(hr)) return hr;
    renderer.sprite_vertex_capacity = new_capacity;
    return S_OK;
}

HRESULT createSpriteResources(WinguiIndexedGraphicsRenderer& renderer) {
    std::vector<uint8_t> zero_atlas(static_cast<size_t>(renderer.sprite_atlas_size) * renderer.sprite_atlas_size, 0);

    D3D11_TEXTURE2D_DESC atlas_desc{};
    atlas_desc.Width = renderer.sprite_atlas_size;
    atlas_desc.Height = renderer.sprite_atlas_size;
    atlas_desc.MipLevels = 1;
    atlas_desc.ArraySize = 1;
    atlas_desc.Format = DXGI_FORMAT_R8_UINT;
    atlas_desc.SampleDesc.Count = 1;
    atlas_desc.Usage = D3D11_USAGE_DEFAULT;
    atlas_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA atlas_init{};
    atlas_init.pSysMem = zero_atlas.data();
    atlas_init.SysMemPitch = renderer.sprite_atlas_size;

    HRESULT hr = renderer.context->device->CreateTexture2D(&atlas_desc, &atlas_init, &renderer.sprite_atlas_texture);
    if (FAILED(hr)) return hr;

    D3D11_SHADER_RESOURCE_VIEW_DESC atlas_srv_desc{};
    atlas_srv_desc.Format = atlas_desc.Format;
    atlas_srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    atlas_srv_desc.Texture2D.MipLevels = 1;
    hr = renderer.context->device->CreateShaderResourceView(renderer.sprite_atlas_texture, &atlas_srv_desc, &renderer.sprite_atlas_srv);
    if (FAILED(hr)) return hr;

    D3D11_TEXTURE2D_DESC palette_desc{};
    palette_desc.Width = 16;
    palette_desc.Height = renderer.sprite_max_palettes;
    palette_desc.MipLevels = 1;
    palette_desc.ArraySize = 1;
    palette_desc.Format = DXGI_FORMAT_R32_UINT;
    palette_desc.SampleDesc.Count = 1;
    palette_desc.Usage = D3D11_USAGE_DYNAMIC;
    palette_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    palette_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = renderer.context->device->CreateTexture2D(&palette_desc, nullptr, &renderer.sprite_palette_texture);
    if (FAILED(hr)) return hr;

    D3D11_SHADER_RESOURCE_VIEW_DESC palette_srv_desc{};
    palette_srv_desc.Format = palette_desc.Format;
    palette_srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    palette_srv_desc.Texture2D.MipLevels = 1;
    hr = renderer.context->device->CreateShaderResourceView(renderer.sprite_palette_texture, &palette_srv_desc, &renderer.sprite_palette_srv);
    if (FAILED(hr)) return hr;

    D3D11_BLEND_DESC blend_desc{};
    blend_desc.RenderTarget[0].BlendEnable = TRUE;
    blend_desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blend_desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blend_desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blend_desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blend_desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    blend_desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blend_desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    hr = renderer.context->device->CreateBlendState(&blend_desc, &renderer.sprite_blend_state);
    return hr;
}

void releaseRgbaBuffers(std::vector<WinguiRgbaBufferResources>& buffers, uint32_t& buffer_width, uint32_t& buffer_height) {
    for (auto& buffer : buffers) {
        safeRelease(buffer.rtv);
        safeRelease(buffer.srv);
        safeRelease(buffer.texture);
    }
    buffer_width = 0;
    buffer_height = 0;
}

HRESULT createRgbaPaneBufferResource(WinguiContext* context, WinguiRgbaBufferResources& buffer, UINT width, UINT height) {
    if (!context || !context->device) return E_POINTER;
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;

    HRESULT hr = context->device->CreateTexture2D(&desc, nullptr, &buffer.texture);
    if (FAILED(hr)) return hr;

    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc{};
    srv_desc.Format = desc.Format;
    srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MipLevels = 1;
    hr = context->device->CreateShaderResourceView(buffer.texture, &srv_desc, &buffer.srv);
    if (FAILED(hr)) {
        safeRelease(buffer.texture);
        return hr;
    }

    D3D11_RENDER_TARGET_VIEW_DESC rtv_desc{};
    rtv_desc.Format = desc.Format;
    rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    hr = context->device->CreateRenderTargetView(buffer.texture, &rtv_desc, &buffer.rtv);
    if (FAILED(hr)) {
        safeRelease(buffer.srv);
        safeRelease(buffer.texture);
        return hr;
    }
    return S_OK;
}

int32_t rgbaSurfaceEnsureBuffers(WinguiContext* context,
                                 std::vector<WinguiRgbaBufferResources>& buffers,
                                 uint32_t& buffer_width,
                                 uint32_t& buffer_height,
                                 uint32_t width,
                                 uint32_t height,
                                 const char* error_prefix) {
    if (!context) {
        setLastErrorString(error_prefix ? error_prefix : "rgba surface: invalid context");
        return 0;
    }
    if (width == 0 || height == 0) {
        setLastErrorString(error_prefix ? error_prefix : "rgba surface: invalid dimensions");
        return 0;
    }
    if (buffer_width == width && buffer_height == height && !buffers.empty() && buffers[0].texture) {
        g_last_error.clear();
        return 1;
    }

    releaseRgbaBuffers(buffers, buffer_width, buffer_height);
    for (auto& buffer : buffers) {
        const HRESULT hr = createRgbaPaneBufferResource(context, buffer, width, height);
        if (FAILED(hr)) {
            setLastErrorHresult(error_prefix ? error_prefix : "rgba surface: texture creation failed", hr);
            releaseRgbaBuffers(buffers, buffer_width, buffer_height);
            return 0;
        }
    }

    buffer_width = width;
    buffer_height = height;
    g_last_error.clear();
    return 1;
}

int32_t rgbaSurfaceGetBufferInfo(const std::vector<WinguiRgbaBufferResources>& buffers,
                                uint32_t buffer_width,
                                uint32_t buffer_height,
                                uint32_t* out_width,
                                uint32_t* out_height,
                                uint32_t* out_buffer_count,
                                const char* invalid_message) {
    if (buffers.empty()) {
        setLastErrorString(invalid_message ? invalid_message : "rgba surface: invalid arguments");
        return 0;
    }
    if (out_width) *out_width = buffer_width;
    if (out_height) *out_height = buffer_height;
    if (out_buffer_count) *out_buffer_count = static_cast<uint32_t>(buffers.size());
    g_last_error.clear();
    return 1;
}

int32_t rgbaSurfaceUploadBgraRegion(WinguiContext* context,
                                    std::vector<WinguiRgbaBufferResources>& buffers,
                                    uint32_t buffer_width,
                                    uint32_t buffer_height,
                                    uint32_t buffer_index,
                                    WinguiRectU32 destination_region,
                                    const uint8_t* pixels,
                                    uint32_t source_pitch,
                                    const char* invalid_message,
                                    const char* uninitialized_message,
                                    const char* bounds_message) {
    if (!context || !context->device_context || !pixels || buffer_index >= buffers.size()) {
        setLastErrorString(invalid_message ? invalid_message : "rgba surface upload: invalid arguments");
        return 0;
    }
    if (!destination_region.width || !destination_region.height) {
        setLastErrorString(invalid_message ? invalid_message : "rgba surface upload: invalid region");
        return 0;
    }
    if (!buffer_width || !buffer_height || !buffers[buffer_index].texture) {
        setLastErrorString(uninitialized_message ? uninitialized_message : "rgba surface upload: buffers are not initialized");
        return 0;
    }
    if (destination_region.x + destination_region.width > buffer_width ||
        destination_region.y + destination_region.height > buffer_height) {
        setLastErrorString(bounds_message ? bounds_message : "rgba surface upload: region out of bounds");
        return 0;
    }
    const D3D11_BOX box{
        destination_region.x,
        destination_region.y,
        0,
        destination_region.x + destination_region.width,
        destination_region.y + destination_region.height,
        1,
    };
    context->device_context->UpdateSubresource(
        buffers[buffer_index].texture,
        0,
        &box,
        pixels,
        source_pitch ? source_pitch : destination_region.width * 4u,
        0);
    g_last_error.clear();
    return 1;
}

int32_t rgbaSurfaceCopyRegion(WinguiContext* context,
                              std::vector<WinguiRgbaBufferResources>& buffers,
                              uint32_t buffer_width,
                              uint32_t buffer_height,
                              uint32_t dst_buffer_index,
                              uint32_t dst_x,
                              uint32_t dst_y,
                              uint32_t src_buffer_index,
                              WinguiRectU32 source_region,
                              const char* invalid_message,
                              const char* uninitialized_message,
                              const char* bounds_message) {
    if (!context || !context->device_context || dst_buffer_index >= buffers.size() || src_buffer_index >= buffers.size()) {
        setLastErrorString(invalid_message ? invalid_message : "rgba surface copy: invalid arguments");
        return 0;
    }
    if (!source_region.width || !source_region.height) {
        setLastErrorString(invalid_message ? invalid_message : "rgba surface copy: invalid region");
        return 0;
    }
    if (!buffer_width || !buffer_height || !buffers[dst_buffer_index].texture || !buffers[src_buffer_index].texture) {
        setLastErrorString(uninitialized_message ? uninitialized_message : "rgba surface copy: buffers are not initialized");
        return 0;
    }
    if (source_region.x + source_region.width > buffer_width ||
        source_region.y + source_region.height > buffer_height ||
        dst_x + source_region.width > buffer_width ||
        dst_y + source_region.height > buffer_height) {
        setLastErrorString(bounds_message ? bounds_message : "rgba surface copy: region out of bounds");
        return 0;
    }
    const D3D11_BOX source_box{
        source_region.x,
        source_region.y,
        0,
        source_region.x + source_region.width,
        source_region.y + source_region.height,
        1,
    };
    context->device_context->CopySubresourceRegion(
        buffers[dst_buffer_index].texture,
        0,
        dst_x,
        dst_y,
        0,
        buffers[src_buffer_index].texture,
        0,
        &source_box);
    g_last_error.clear();
    return 1;
}

int32_t renderRgbaBufferSet(WinguiRgbaPaneRenderer* renderer,
                            const std::vector<WinguiRgbaBufferResources>& buffers,
                            uint32_t buffer_width,
                            uint32_t buffer_height,
                            int32_t viewport_x,
                            int32_t viewport_y,
                            int32_t viewport_width,
                            int32_t viewport_height,
                            uint32_t screen_width,
                            uint32_t screen_height,
                            uint32_t pixel_aspect_num,
                            uint32_t pixel_aspect_den,
                            uint32_t buffer_index,
                            WinguiIndexedPaneLayout* out_layout,
                            const char* invalid_message,
                            const char* ready_message,
                            const char* layout_message) {
    if (!renderer || !renderer->context || !renderer->context->device_context) {
        setLastErrorString(invalid_message ? invalid_message : "rgba render: invalid arguments");
        return 0;
    }
    if (buffer_index >= buffers.size() || !buffers[buffer_index].srv || !buffer_width || !buffer_height) {
        setLastErrorString(ready_message ? ready_message : "rgba render: requested buffer is not ready");
        return 0;
    }

    WinguiIndexedPaneLayout layout{};
    if (!computeIndexedPaneLayoutInternal(
            viewport_x,
            viewport_y,
            viewport_width,
            viewport_height,
            screen_width ? screen_width : buffer_width,
            screen_height ? screen_height : buffer_height,
            pixel_aspect_num,
            pixel_aspect_den,
            layout)) {
        setLastErrorString(layout_message ? layout_message : "rgba render: invalid pane layout inputs");
        return 0;
    }
    if (out_layout) *out_layout = layout;

    WinguiGraphicsDisplayUniforms uniforms{};
    uniforms.has_texture = 1;
    uniforms.screen_width = screen_width ? screen_width : buffer_width;
    uniforms.screen_height = screen_height ? screen_height : buffer_height;
    uniforms.buffer_width = buffer_width;
    uniforms.buffer_height = buffer_height;
    uniforms.uv_scale_x = layout.shown_width / std::max(1.0f, static_cast<float>(viewport_width));
    uniforms.uv_scale_y = layout.shown_height / std::max(1.0f, static_cast<float>(viewport_height));
    uniforms.uv_offset_x = (1.0f - uniforms.uv_scale_x) * 0.5f;
    uniforms.uv_offset_y = (1.0f - uniforms.uv_scale_y) * 0.5f;

    D3D11_MAPPED_SUBRESOURCE mapped{};
    HRESULT hr = renderer->context->device_context->Map(renderer->constant_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) {
        setLastErrorHresult(invalid_message ? invalid_message : "rgba render: constant buffer map failed", hr);
        return 0;
    }
    std::memcpy(mapped.pData, &uniforms, sizeof(uniforms));
    renderer->context->device_context->Unmap(renderer->constant_buffer, 0);

    const D3D11_VIEWPORT viewport{ static_cast<float>(viewport_x), static_cast<float>(viewport_y), static_cast<float>(viewport_width), static_cast<float>(viewport_height), 0.0f, 1.0f };
    const D3D11_RECT scissor{ viewport_x, viewport_y, viewport_x + viewport_width, viewport_y + viewport_height };
    ID3D11Buffer* no_vertex_buffer = nullptr;
    const UINT stride = 0;
    const UINT offset = 0;
    ID3D11ShaderResourceView* srvs[] = { buffers[buffer_index].srv };

    renderer->context->device_context->OMSetRenderTargets(1, &renderer->context->render_target_view, nullptr);
    renderer->context->device_context->RSSetState(renderer->rasterizer);
    renderer->context->device_context->RSSetViewports(1, &viewport);
    renderer->context->device_context->RSSetScissorRects(1, &scissor);
    renderer->context->device_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    renderer->context->device_context->IASetInputLayout(nullptr);
    renderer->context->device_context->IASetVertexBuffers(0, 1, &no_vertex_buffer, &stride, &offset);
    renderer->context->device_context->VSSetShader(renderer->vertex_shader, nullptr, 0);
    renderer->context->device_context->PSSetShader(renderer->pixel_shader, nullptr, 0);
    renderer->context->device_context->VSSetConstantBuffers(0, 1, &renderer->constant_buffer);
    renderer->context->device_context->PSSetConstantBuffers(0, 1, &renderer->constant_buffer);
    renderer->context->device_context->PSSetSamplers(0, 1, &renderer->linear_sampler);
    renderer->context->device_context->PSSetShaderResources(0, 1, srvs);
    renderer->context->device_context->Draw(6, 0);

    g_last_error.clear();
    return 1;
}

bool buildGlyphAtlasBitmap(const WinguiGlyphAtlasDesc* desc, WinguiGlyphAtlasBitmap& out_bitmap) {
    const uint32_t first_codepoint = desc && desc->first_codepoint ? desc->first_codepoint : 0x20u;
    const uint32_t glyph_count = desc && desc->glyph_count ? desc->glyph_count : 95u;
    const uint32_t cols = desc && desc->cols ? desc->cols : 16u;
    const uint32_t rows = desc && desc->rows ? desc->rows : static_cast<uint32_t>((glyph_count + cols - 1u) / cols);
    if (glyph_count == 0 || cols == 0 || rows == 0 || rows * cols < glyph_count) return false;

    const float dpi_scale = desc && desc->dpi_scale > 0.0f ? desc->dpi_scale : 1.0f;
    const int requested_px = desc && desc->font_pixel_height > 0 ? desc->font_pixel_height : std::max(13, static_cast<int>(dpi_scale * 13.0f));
    const std::wstring font_family = utf8ToWide(desc && desc->font_family_utf8 && *desc->font_family_utf8 ? desc->font_family_utf8 : "Consolas");

    HDC screen_dc = GetDC(nullptr);
    if (!screen_dc) return false;
    HDC mem_dc = CreateCompatibleDC(screen_dc);
    ReleaseDC(nullptr, screen_dc);
    if (!mem_dc) return false;

    HFONT font = CreateFontW(
        -requested_px,
        0,
        0,
        0,
        FW_NORMAL,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_OUTLINE_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        FIXED_PITCH | FF_MODERN,
        font_family.empty() ? L"Consolas" : font_family.c_str());
    if (!font) {
        DeleteDC(mem_dc);
        return false;
    }

    HFONT old_font = static_cast<HFONT>(SelectObject(mem_dc, font));
    TEXTMETRICW metrics{};
    GetTextMetricsW(mem_dc, &metrics);

    SIZE space_size{};
    GetTextExtentPoint32W(mem_dc, L" ", 1, &space_size);

    const UINT cell_w = std::max<UINT>(8, static_cast<UINT>(space_size.cx > 0 ? space_size.cx : metrics.tmAveCharWidth));
    const UINT cell_h = std::max<UINT>(16, static_cast<UINT>(metrics.tmHeight));
    const UINT atlas_w = (cell_w * cols + 3u) & ~3u;
    const UINT atlas_h = (cell_h * rows + 3u) & ~3u;

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = static_cast<LONG>(atlas_w);
    bmi.bmiHeader.biHeight = -static_cast<LONG>(atlas_h);
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* dib_bits = nullptr;
    HBITMAP dib = CreateDIBSection(mem_dc, &bmi, DIB_RGB_COLORS, &dib_bits, nullptr, 0);
    if (!dib || !dib_bits) {
        SelectObject(mem_dc, old_font);
        DeleteObject(font);
        DeleteDC(mem_dc);
        return false;
    }

    HGDIOBJ old_bitmap = SelectObject(mem_dc, dib);
    std::memset(dib_bits, 0, static_cast<size_t>(atlas_w) * atlas_h * 4u);
    SetBkMode(mem_dc, TRANSPARENT);
    SetTextColor(mem_dc, RGB(255, 255, 255));

    for (uint32_t index = 0; index < glyph_count; ++index) {
        const uint32_t cp = first_codepoint + index;
        const uint32_t col = index % cols;
        const uint32_t row = index / cols;
        const int x = static_cast<int>(col * cell_w);
        const int y = static_cast<int>(row * cell_h);
        const wchar_t ch = static_cast<wchar_t>(cp);
        TextOutW(mem_dc, x, y, &ch, 1);
    }

    const size_t byte_count = static_cast<size_t>(atlas_w) * static_cast<size_t>(atlas_h) * 4u;
    uint8_t* pixels = static_cast<uint8_t*>(std::malloc(byte_count));
    if (!pixels) {
        SelectObject(mem_dc, old_bitmap);
        DeleteObject(dib);
        SelectObject(mem_dc, old_font);
        DeleteObject(font);
        DeleteDC(mem_dc);
        return false;
    }
    std::memcpy(pixels, dib_bits, byte_count);

    for (size_t i = 0; i < byte_count; i += 4) {
        const uint8_t b = pixels[i + 0];
        const uint8_t g = pixels[i + 1];
        const uint8_t r = pixels[i + 2];
        pixels[i + 0] = r;
        pixels[i + 1] = g;
        pixels[i + 2] = b;
        pixels[i + 3] = std::max({ r, g, b });
    }

    out_bitmap.pixels_rgba = pixels;
    out_bitmap.width = atlas_w;
    out_bitmap.height = atlas_h;
    out_bitmap.info = {
        static_cast<float>(atlas_w),
        static_cast<float>(atlas_h),
        static_cast<float>(cell_w),
        static_cast<float>(cell_h),
        cols,
        rows,
        first_codepoint,
        glyph_count,
        static_cast<float>(metrics.tmAscent),
        static_cast<float>(metrics.tmDescent),
        static_cast<float>(metrics.tmExternalLeading),
        0,
    };

    SelectObject(mem_dc, old_bitmap);
    DeleteObject(dib);
    SelectObject(mem_dc, old_font);
    DeleteObject(font);
    DeleteDC(mem_dc);
    return true;
}

HRESULT ensureTextGridInstanceBuffer(WinguiTextGridRenderer& renderer, size_t needed) {
    if (needed <= renderer.instance_capacity) return S_OK;

    size_t new_capacity = std::max<size_t>(4096, renderer.instance_capacity ? renderer.instance_capacity : 4096);
    while (new_capacity < needed) new_capacity *= 2;

    safeRelease(renderer.instance_buffer);

    D3D11_BUFFER_DESC desc{};
    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    desc.ByteWidth = static_cast<UINT>(new_capacity * sizeof(WinguiGlyphInstance));
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    const HRESULT hr = renderer.context->device->CreateBuffer(&desc, nullptr, &renderer.instance_buffer);
    if (FAILED(hr)) return hr;

    renderer.instance_capacity = new_capacity;
    return S_OK;
}

HRESULT createTextGridAtlasTexture(WinguiTextGridRenderer& renderer, const WinguiGlyphAtlasBitmap& bitmap) {
    if (!renderer.context || !renderer.context->device || !bitmap.pixels_rgba || bitmap.width == 0 || bitmap.height == 0) {
        return E_INVALIDARG;
    }

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = bitmap.width;
    desc.Height = bitmap.height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA init{};
    init.pSysMem = bitmap.pixels_rgba;
    init.SysMemPitch = bitmap.width * 4;

    ID3D11Texture2D* texture = nullptr;
    ID3D11ShaderResourceView* srv = nullptr;
    HRESULT hr = renderer.context->device->CreateTexture2D(&desc, &init, &texture);
    if (FAILED(hr)) return hr;

    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc{};
    srv_desc.Format = desc.Format;
    srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MipLevels = 1;
    hr = renderer.context->device->CreateShaderResourceView(texture, &srv_desc, &srv);
    if (FAILED(hr)) {
        safeRelease(texture);
        return hr;
    }

    safeRelease(renderer.atlas_srv);
    safeRelease(renderer.atlas_texture);
    renderer.atlas_texture = texture;
    renderer.atlas_srv = srv;
    renderer.atlas_info = bitmap.info;
    return S_OK;
}

HRESULT createRenderTargetView(WinguiContext& context) {
    ID3D11Texture2D* backbuffer = nullptr;
    HRESULT hr = context.swap_chain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backbuffer));
    if (FAILED(hr)) return hr;
    hr = context.device->CreateRenderTargetView(backbuffer, nullptr, &context.render_target_view);
    safeRelease(backbuffer);
    return hr;
}

void destroyRenderTargetView(WinguiContext& context) {
    safeRelease(context.render_target_view);
}

HRESULT resizeSwapChain(WinguiContext& context, UINT width, UINT height) {
    destroyRenderTargetView(context);
    HRESULT hr = context.swap_chain->ResizeBuffers(context.buffer_count, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
    if (FAILED(hr)) return hr;
    context.width = width;
    context.height = height;
    return createRenderTargetView(context);
}

} // namespace

extern "C" WINGUI_API const char* WINGUI_CALL wingui_last_error_utf8(void) {
    return g_last_error.c_str();
}

extern "C" WINGUI_API uint32_t WINGUI_CALL wingui_version_major(void) { return 0; }
extern "C" WINGUI_API uint32_t WINGUI_CALL wingui_version_minor(void) { return 1; }
extern "C" WINGUI_API uint32_t WINGUI_CALL wingui_version_patch(void) { return 0; }

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_create_window_utf8(const WinguiWindowDesc* desc, WinguiWindow** out_window) {
    if (!desc || !out_window) {
        setLastErrorString("wingui_create_window_utf8: invalid arguments");
        return 0;
    }

    auto* window = new (std::nothrow) WinguiWindow();
    if (!window) {
        setLastErrorString("wingui_create_window_utf8: allocation failed");
        return 0;
    }

    if (!ensureWindowClassRegistered(*desc, window->class_name)) {
        setLastErrorString("wingui_create_window_utf8: RegisterClassExW failed");
        delete window;
        return 0;
    }

    const std::wstring title = utf8ToWide(desc->title_utf8 ? desc->title_utf8 : "");
    window->window_proc = desc->window_proc;
    window->user_data = desc->user_data;

    const DWORD style = desc->style ? desc->style : static_cast<uint32_t>(WS_OVERLAPPEDWINDOW);
    const HWND hwnd = CreateWindowExW(
        desc->ex_style,
        window->class_name.c_str(),
        title.c_str(),
        style,
        desc->x,
        desc->y,
        desc->width > 0 ? desc->width : CW_USEDEFAULT,
        desc->height > 0 ? desc->height : CW_USEDEFAULT,
        static_cast<HWND>(desc->parent),
        desc->menu ? static_cast<HMENU>(desc->menu) : nullptr,
        currentModuleHandle(),
        window);
    if (!hwnd) {
        setLastErrorString("wingui_create_window_utf8: CreateWindowExW failed");
        delete window;
        return 0;
    }

    window->hwnd = hwnd;
    *out_window = window;
    g_last_error.clear();
    return 1;
}

extern "C" WINGUI_API void WINGUI_CALL wingui_destroy_window(WinguiWindow* window) {
    if (!window) return;
    if (window->hwnd) {
        DestroyWindow(window->hwnd);
        window->hwnd = nullptr;
    }
    delete window;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_window_show(WinguiWindow* window, int32_t show_command) {
    if (!window || !window->hwnd) {
        setLastErrorString("wingui_window_show: window is not initialized");
        return 0;
    }
    ShowWindow(window->hwnd, show_command);
    UpdateWindow(window->hwnd);
    g_last_error.clear();
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_window_close(WinguiWindow* window) {
    if (!window || !window->hwnd) {
        setLastErrorString("wingui_window_close: window is not initialized");
        return 0;
    }
    PostMessageW(window->hwnd, WM_CLOSE, 0, 0);
    g_last_error.clear();
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_window_set_title_utf8(WinguiWindow* window, const char* title_utf8) {
    if (!window || !window->hwnd) {
        setLastErrorString("wingui_window_set_title_utf8: window is not initialized");
        return 0;
    }
    const std::wstring title = utf8ToWide(title_utf8 ? title_utf8 : "");
    if (!SetWindowTextW(window->hwnd, title.c_str())) {
        setLastErrorString("wingui_window_set_title_utf8: SetWindowTextW failed");
        return 0;
    }
    g_last_error.clear();
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_window_set_menu(WinguiWindow* window, WinguiMenu* menu) {
    if (!window || !window->hwnd) {
        setLastErrorString("wingui_window_set_menu: window is not initialized");
        return 0;
    }
    if (!SetMenu(window->hwnd, menu ? menu->handle : nullptr)) {
        setLastErrorString("wingui_window_set_menu: SetMenu failed");
        return 0;
    }
    if (menu) menu->attached_to_parent = true;
    DrawMenuBar(window->hwnd);
    g_last_error.clear();
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_window_redraw_menu_bar(WinguiWindow* window) {
    if (!window || !window->hwnd) {
        setLastErrorString("wingui_window_redraw_menu_bar: window is not initialized");
        return 0;
    }
    if (!DrawMenuBar(window->hwnd)) {
        setLastErrorString("wingui_window_redraw_menu_bar: DrawMenuBar failed");
        return 0;
    }
    g_last_error.clear();
    return 1;
}

extern "C" WINGUI_API void* WINGUI_CALL wingui_window_hwnd(WinguiWindow* window) {
    return window ? window->hwnd : nullptr;
}

extern "C" WINGUI_API void WINGUI_CALL wingui_window_set_user_data(WinguiWindow* window, void* user_data) {
    if (!window) return;
    window->user_data = user_data;
}

extern "C" WINGUI_API void* WINGUI_CALL wingui_window_user_data(WinguiWindow* window) {
    return window ? window->user_data : nullptr;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_window_client_size(WinguiWindow* window, int32_t* out_width, int32_t* out_height) {
    if (!window || !window->hwnd || !out_width || !out_height) {
        setLastErrorString("wingui_window_client_size: invalid arguments");
        return 0;
    }
    RECT rect{};
    if (!GetClientRect(window->hwnd, &rect)) {
        setLastErrorString("wingui_window_client_size: GetClientRect failed");
        return 0;
    }
    *out_width = rect.right - rect.left;
    *out_height = rect.bottom - rect.top;
    g_last_error.clear();
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_window_get_key_state(WinguiWindow* window, uint32_t virtual_key) {
    if (!window || virtual_key > 0xffu) {
        setLastErrorString("wingui_window_get_key_state: invalid arguments");
        return 0;
    }
    g_last_error.clear();
    return window->key_states[virtual_key].load(std::memory_order_acquire) ? 1 : 0;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_window_get_keyboard_state(WinguiWindow* window, WinguiKeyboardState* out_state) {
    if (!window || !out_state) {
        setLastErrorString("wingui_window_get_keyboard_state: invalid arguments");
        return 0;
    }
    for (uint32_t index = 0; index < 256; ++index) {
        out_state->pressed[index] = window->key_states[index].load(std::memory_order_acquire);
    }
    g_last_error.clear();
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_window_get_mouse_state(WinguiWindow* window, WinguiMouseState* out_state) {
    if (!window || !out_state) {
        setLastErrorString("wingui_window_get_mouse_state: invalid arguments");
        return 0;
    }
    out_state->x = window->mouse_x.load(std::memory_order_acquire);
    out_state->y = window->mouse_y.load(std::memory_order_acquire);
    out_state->buttons = window->mouse_buttons.load(std::memory_order_acquire);
    out_state->inside_client = window->mouse_inside_client.load(std::memory_order_acquire);
    g_last_error.clear();
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_create_menu_bar(WinguiMenu** out_menu) {
    if (!out_menu) {
        setLastErrorString("wingui_create_menu_bar: invalid arguments");
        return 0;
    }
    auto* menu = new (std::nothrow) WinguiMenu();
    if (!menu) {
        setLastErrorString("wingui_create_menu_bar: allocation failed");
        return 0;
    }
    menu->handle = CreateMenu();
    if (!menu->handle) {
        setLastErrorString("wingui_create_menu_bar: CreateMenu failed");
        delete menu;
        return 0;
    }
    *out_menu = menu;
    g_last_error.clear();
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_create_popup_menu_handle(WinguiMenu** out_menu) {
    if (!out_menu) {
        setLastErrorString("wingui_create_popup_menu_handle: invalid arguments");
        return 0;
    }
    auto* menu = new (std::nothrow) WinguiMenu();
    if (!menu) {
        setLastErrorString("wingui_create_popup_menu_handle: allocation failed");
        return 0;
    }
    menu->handle = CreatePopupMenu();
    if (!menu->handle) {
        setLastErrorString("wingui_create_popup_menu_handle: CreatePopupMenu failed");
        delete menu;
        return 0;
    }
    *out_menu = menu;
    g_last_error.clear();
    return 1;
}

extern "C" WINGUI_API void WINGUI_CALL wingui_destroy_menu(WinguiMenu* menu) {
    if (!menu) return;
    if (menu->handle && !menu->attached_to_parent) {
        DestroyMenu(menu->handle);
    }
    menu->handle = nullptr;
    delete menu;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_menu_append_item_utf8(WinguiMenu* menu, uint32_t flags, uint32_t command_id, const char* text_utf8) {
    if (!menu || !menu->handle || !text_utf8) {
        setLastErrorString("wingui_menu_append_item_utf8: invalid arguments");
        return 0;
    }
    const std::wstring label = utf8ToWide(text_utf8);
    if (!AppendMenuW(menu->handle, flags, command_id, label.c_str())) {
        setLastErrorString("wingui_menu_append_item_utf8: AppendMenuW failed");
        return 0;
    }
    g_last_error.clear();
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_menu_append_separator(WinguiMenu* menu) {
    if (!menu || !menu->handle) {
        setLastErrorString("wingui_menu_append_separator: menu is not initialized");
        return 0;
    }
    if (!AppendMenuW(menu->handle, MF_SEPARATOR, 0, nullptr)) {
        setLastErrorString("wingui_menu_append_separator: AppendMenuW failed");
        return 0;
    }
    g_last_error.clear();
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_menu_append_submenu_utf8(WinguiMenu* menu, WinguiMenu* submenu, uint32_t flags, const char* text_utf8) {
    if (!menu || !menu->handle || !submenu || !submenu->handle || !text_utf8) {
        setLastErrorString("wingui_menu_append_submenu_utf8: invalid arguments");
        return 0;
    }
    const std::wstring label = utf8ToWide(text_utf8);
    if (!AppendMenuW(menu->handle, flags | MF_POPUP, reinterpret_cast<UINT_PTR>(submenu->handle), label.c_str())) {
        setLastErrorString("wingui_menu_append_submenu_utf8: AppendMenuW failed");
        return 0;
    }
    submenu->attached_to_parent = true;
    g_last_error.clear();
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_menu_remove_item(WinguiMenu* menu, uint32_t command_id) {
    if (!menu || !menu->handle) {
        setLastErrorString("wingui_menu_remove_item: menu is not initialized");
        return 0;
    }
    if (!DeleteMenu(menu->handle, command_id, MF_BYCOMMAND)) {
        setLastErrorString("wingui_menu_remove_item: DeleteMenu failed");
        return 0;
    }
    g_last_error.clear();
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_menu_set_item_enabled(WinguiMenu* menu, uint32_t command_id, int32_t enabled) {
    if (!menu || !menu->handle) {
        setLastErrorString("wingui_menu_set_item_enabled: menu is not initialized");
        return 0;
    }
    if (!EnableMenuItem(menu->handle, command_id, MF_BYCOMMAND | (enabled ? MF_ENABLED : MF_GRAYED))) {
        // EnableMenuItem returns previous state, where -1 signals failure. Preserve that semantics.
        if (GetLastError() != ERROR_SUCCESS) {
            setLastErrorString("wingui_menu_set_item_enabled: EnableMenuItem failed");
            return 0;
        }
    }
    g_last_error.clear();
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_menu_set_item_checked(WinguiMenu* menu, uint32_t command_id, int32_t checked) {
    if (!menu || !menu->handle) {
        setLastErrorString("wingui_menu_set_item_checked: menu is not initialized");
        return 0;
    }
    if (!CheckMenuItem(menu->handle, command_id, MF_BYCOMMAND | (checked ? MF_CHECKED : MF_UNCHECKED))) {
        if (GetLastError() != ERROR_SUCCESS) {
            setLastErrorString("wingui_menu_set_item_checked: CheckMenuItem failed");
            return 0;
        }
    }
    g_last_error.clear();
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_menu_set_item_label_utf8(WinguiMenu* menu, uint32_t command_id, const char* text_utf8) {
    if (!menu || !menu->handle || !text_utf8) {
        setLastErrorString("wingui_menu_set_item_label_utf8: invalid arguments");
        return 0;
    }
    const std::wstring label = utf8ToWide(text_utf8);
    MENUITEMINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = MIIM_STRING;
    info.dwTypeData = const_cast<wchar_t*>(label.c_str());
    if (!SetMenuItemInfoW(menu->handle, command_id, FALSE, &info)) {
        setLastErrorString("wingui_menu_set_item_label_utf8: SetMenuItemInfoW failed");
        return 0;
    }
    g_last_error.clear();
    return 1;
}

extern "C" WINGUI_API void* WINGUI_CALL wingui_menu_native_handle(WinguiMenu* menu) {
    return menu ? menu->handle : nullptr;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_pump_message(int32_t wait_for_message, int32_t* out_exit_code) {
    MSG msg{};
    BOOL result = wait_for_message ? GetMessageW(&msg, nullptr, 0, 0) : PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE);
    if (!wait_for_message && !result) {
        if (out_exit_code) *out_exit_code = 0;
        g_last_error.clear();
        return 0;
    }
    if (result == -1) {
        setLastErrorString("wingui_pump_message: message retrieval failed");
        return -1;
    }
    if (result == 0) {
        if (out_exit_code) *out_exit_code = static_cast<int32_t>(msg.wParam);
        g_last_error.clear();
        return 0;
    }
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
    if (out_exit_code) *out_exit_code = 0;
    g_last_error.clear();
    return 1;
}

extern "C" WINGUI_API void WINGUI_CALL wingui_post_quit_message(int32_t exit_code) {
    PostQuitMessage(exit_code);
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_build_glyph_atlas_utf8(const WinguiGlyphAtlasDesc* desc, WinguiGlyphAtlasBitmap* out_bitmap) {
    if (!out_bitmap) {
        setLastErrorString("wingui_build_glyph_atlas_utf8: invalid arguments");
        return 0;
    }
    std::memset(out_bitmap, 0, sizeof(*out_bitmap));
    if (!buildGlyphAtlasBitmap(desc, *out_bitmap)) {
        setLastErrorString("wingui_build_glyph_atlas_utf8: atlas generation failed");
        return 0;
    }
    g_last_error.clear();
    return 1;
}

extern "C" WINGUI_API void WINGUI_CALL wingui_free_glyph_atlas_bitmap(WinguiGlyphAtlasBitmap* bitmap) {
    if (!bitmap) return;
    if (bitmap->pixels_rgba) {
        std::free(bitmap->pixels_rgba);
        bitmap->pixels_rgba = nullptr;
    }
    bitmap->width = 0;
    bitmap->height = 0;
    std::memset(&bitmap->info, 0, sizeof(bitmap->info));
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_create_text_grid_renderer(const WinguiTextGridRendererDesc* desc, WinguiTextGridRenderer** out_renderer) {
    if (!desc || !desc->context || !out_renderer) {
        setLastErrorString("wingui_create_text_grid_renderer: invalid arguments");
        return 0;
    }

    auto* renderer = new (std::nothrow) WinguiTextGridRenderer();
    if (!renderer) {
        setLastErrorString("wingui_create_text_grid_renderer: allocation failed");
        return 0;
    }
    renderer->context = desc->context;

    const char* shader_path = desc->shader_path_utf8 && *desc->shader_path_utf8 ? desc->shader_path_utf8 : "wingui/shaders/text_grid.hlsl";
    void* vs_blob_raw = nullptr;
    void* ps_blob_raw = nullptr;
    size_t vs_blob_size = 0;
    size_t ps_blob_size = 0;
    if (!wingui_compile_shader_from_file_utf8(shader_path, "glyph_vertex", "vs_4_0", &vs_blob_raw, &vs_blob_size)) {
        delete renderer;
        return 0;
    }
    if (!wingui_compile_shader_from_file_utf8(shader_path, "glyph_fragment", "ps_4_0", &ps_blob_raw, &ps_blob_size)) {
        wingui_release_blob(vs_blob_raw);
        delete renderer;
        return 0;
    }

    auto* vs_blob = static_cast<ID3DBlob*>(vs_blob_raw);
    auto* ps_blob = static_cast<ID3DBlob*>(ps_blob_raw);
    HRESULT hr = renderer->context->device->CreateVertexShader(vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), nullptr, &renderer->vertex_shader);
    if (FAILED(hr)) {
        setLastErrorHresult("wingui_create_text_grid_renderer: CreateVertexShader failed", hr);
        wingui_release_blob(ps_blob_raw);
        wingui_release_blob(vs_blob_raw);
        delete renderer;
        return 0;
    }
    hr = renderer->context->device->CreatePixelShader(ps_blob->GetBufferPointer(), ps_blob->GetBufferSize(), nullptr, &renderer->pixel_shader);
    if (FAILED(hr)) {
        setLastErrorHresult("wingui_create_text_grid_renderer: CreatePixelShader failed", hr);
        wingui_release_blob(ps_blob_raw);
        wingui_release_blob(vs_blob_raw);
        wingui_destroy_text_grid_renderer(renderer);
        return 0;
    }
    hr = renderer->context->device->CreateInputLayout(
        kWinguiTextGridInstanceLayout,
        static_cast<UINT>(std::size(kWinguiTextGridInstanceLayout)),
        vs_blob->GetBufferPointer(),
        vs_blob->GetBufferSize(),
        &renderer->input_layout);
    wingui_release_blob(ps_blob_raw);
    wingui_release_blob(vs_blob_raw);
    if (FAILED(hr)) {
        setLastErrorHresult("wingui_create_text_grid_renderer: CreateInputLayout failed", hr);
        wingui_destroy_text_grid_renderer(renderer);
        return 0;
    }

    D3D11_BUFFER_DESC cb_desc{};
    cb_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cb_desc.ByteWidth = sizeof(WinguiTextGridUniforms);
    cb_desc.Usage = D3D11_USAGE_DYNAMIC;
    cb_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = renderer->context->device->CreateBuffer(&cb_desc, nullptr, &renderer->constant_buffer);
    if (FAILED(hr)) {
        setLastErrorHresult("wingui_create_text_grid_renderer: CreateBuffer failed", hr);
        wingui_destroy_text_grid_renderer(renderer);
        return 0;
    }

    D3D11_SAMPLER_DESC sampler_desc{};
    sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;
    hr = renderer->context->device->CreateSamplerState(&sampler_desc, &renderer->sampler);
    if (FAILED(hr)) {
        setLastErrorHresult("wingui_create_text_grid_renderer: CreateSamplerState failed", hr);
        wingui_destroy_text_grid_renderer(renderer);
        return 0;
    }

    D3D11_RASTERIZER_DESC raster_desc{};
    raster_desc.FillMode = D3D11_FILL_SOLID;
    raster_desc.CullMode = D3D11_CULL_NONE;
    raster_desc.ScissorEnable = TRUE;
    raster_desc.DepthClipEnable = TRUE;
    hr = renderer->context->device->CreateRasterizerState(&raster_desc, &renderer->rasterizer);
    if (FAILED(hr)) {
        setLastErrorHresult("wingui_create_text_grid_renderer: CreateRasterizerState failed", hr);
        wingui_destroy_text_grid_renderer(renderer);
        return 0;
    }

    *out_renderer = renderer;
    g_last_error.clear();
    return 1;
}

extern "C" WINGUI_API void WINGUI_CALL wingui_destroy_text_grid_renderer(WinguiTextGridRenderer* renderer) {
    if (!renderer) return;
    safeRelease(renderer->atlas_srv);
    safeRelease(renderer->atlas_texture);
    safeRelease(renderer->rasterizer);
    safeRelease(renderer->sampler);
    safeRelease(renderer->instance_buffer);
    safeRelease(renderer->constant_buffer);
    safeRelease(renderer->input_layout);
    safeRelease(renderer->pixel_shader);
    safeRelease(renderer->vertex_shader);
    delete renderer;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_text_grid_renderer_set_atlas(WinguiTextGridRenderer* renderer, const WinguiGlyphAtlasBitmap* bitmap) {
    if (!renderer || !bitmap || !bitmap->pixels_rgba) {
        setLastErrorString("wingui_text_grid_renderer_set_atlas: invalid arguments");
        return 0;
    }
    const HRESULT hr = createTextGridAtlasTexture(*renderer, *bitmap);
    if (FAILED(hr)) {
        setLastErrorHresult("wingui_text_grid_renderer_set_atlas: texture upload failed", hr);
        return 0;
    }
    g_last_error.clear();
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_text_grid_renderer_render(
    WinguiTextGridRenderer* renderer,
    int32_t viewport_x,
    int32_t viewport_y,
    int32_t viewport_width,
    int32_t viewport_height,
    const WinguiTextGridFrame* frame) {
    if (!renderer || !renderer->context || !renderer->context->device_context || !renderer->context->render_target_view || !frame) {
        setLastErrorString("wingui_text_grid_renderer_render: invalid arguments");
        return 0;
    }
    if (!renderer->atlas_srv || !frame->instances || frame->instance_count == 0 || viewport_width <= 0 || viewport_height <= 0) {
        setLastErrorString("wingui_text_grid_renderer_render: renderer is not ready");
        return 0;
    }

    HRESULT hr = ensureTextGridInstanceBuffer(*renderer, frame->instance_count);
    if (FAILED(hr)) {
        setLastErrorHresult("wingui_text_grid_renderer_render: instance buffer allocation failed", hr);
        return 0;
    }

    D3D11_MAPPED_SUBRESOURCE mapped{};
    hr = renderer->context->device_context->Map(renderer->instance_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) {
        setLastErrorHresult("wingui_text_grid_renderer_render: instance buffer map failed", hr);
        return 0;
    }
    std::memcpy(mapped.pData, frame->instances, static_cast<size_t>(frame->instance_count) * sizeof(WinguiGlyphInstance));
    renderer->context->device_context->Unmap(renderer->instance_buffer, 0);

    hr = renderer->context->device_context->Map(renderer->constant_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) {
        setLastErrorHresult("wingui_text_grid_renderer_render: constant buffer map failed", hr);
        return 0;
    }
    std::memcpy(mapped.pData, &frame->uniforms, sizeof(frame->uniforms));
    renderer->context->device_context->Unmap(renderer->constant_buffer, 0);

    const D3D11_VIEWPORT viewport{
        static_cast<float>(viewport_x),
        static_cast<float>(viewport_y),
        static_cast<float>(viewport_width),
        static_cast<float>(viewport_height),
        0.0f,
        1.0f
    };
    const D3D11_RECT scissor{ viewport_x, viewport_y, viewport_x + viewport_width, viewport_y + viewport_height };
    const UINT stride = sizeof(WinguiGlyphInstance);
    const UINT offset = 0;

    renderer->context->device_context->OMSetRenderTargets(1, &renderer->context->render_target_view, nullptr);
    renderer->context->device_context->RSSetState(renderer->rasterizer);
    renderer->context->device_context->RSSetViewports(1, &viewport);
    renderer->context->device_context->RSSetScissorRects(1, &scissor);
    renderer->context->device_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    renderer->context->device_context->IASetInputLayout(renderer->input_layout);
    renderer->context->device_context->IASetVertexBuffers(0, 1, &renderer->instance_buffer, &stride, &offset);
    renderer->context->device_context->VSSetShader(renderer->vertex_shader, nullptr, 0);
    renderer->context->device_context->PSSetShader(renderer->pixel_shader, nullptr, 0);
    renderer->context->device_context->VSSetConstantBuffers(0, 1, &renderer->constant_buffer);
    renderer->context->device_context->PSSetShaderResources(0, 1, &renderer->atlas_srv);
    renderer->context->device_context->PSSetSamplers(0, 1, &renderer->sampler);
    renderer->context->device_context->DrawInstanced(6, frame->instance_count, 0, 0);

    g_last_error.clear();
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_create_indexed_graphics_renderer(
    const WinguiIndexedGraphicsRendererDesc* desc,
    WinguiIndexedGraphicsRenderer** out_renderer) {
    if (!desc || !desc->context || !out_renderer) {
        setLastErrorString("wingui_create_indexed_graphics_renderer: invalid arguments");
        return 0;
    }

    auto* renderer = new (std::nothrow) WinguiIndexedGraphicsRenderer();
    if (!renderer) {
        setLastErrorString("wingui_create_indexed_graphics_renderer: allocation failed");
        return 0;
    }
    renderer->context = desc->context;
    renderer->sprite_atlas_size = desc->sprite_atlas_size ? desc->sprite_atlas_size : 2048u;
    renderer->sprite_max_palettes = desc->sprite_max_palettes ? desc->sprite_max_palettes : 1024u;

    const char* graphics_shader = desc->graphics_shader_path_utf8 && *desc->graphics_shader_path_utf8 ? desc->graphics_shader_path_utf8 : "wingui/shaders/graphics.hlsl";
    const char* sprite_shader = desc->sprite_shader_path_utf8 && *desc->sprite_shader_path_utf8 ? desc->sprite_shader_path_utf8 : "wingui/shaders/sprite.hlsl";

    void* graphics_vs_raw = nullptr;
    void* graphics_ps_raw = nullptr;
    void* sprite_vs_raw = nullptr;
    void* sprite_ps_raw = nullptr;
    size_t blob_size = 0;

    if (!wingui_compile_shader_from_file_utf8(graphics_shader, "graphics_vertex", "vs_4_0", &graphics_vs_raw, &blob_size) ||
        !wingui_compile_shader_from_file_utf8(graphics_shader, "graphics_fragment", "ps_4_0", &graphics_ps_raw, &blob_size) ||
        !wingui_compile_shader_from_file_utf8(sprite_shader, "sprite_vertex", "vs_4_0", &sprite_vs_raw, &blob_size) ||
        !wingui_compile_shader_from_file_utf8(sprite_shader, "sprite_fragment", "ps_4_0", &sprite_ps_raw, &blob_size)) {
        if (graphics_vs_raw) wingui_release_blob(graphics_vs_raw);
        if (graphics_ps_raw) wingui_release_blob(graphics_ps_raw);
        if (sprite_vs_raw) wingui_release_blob(sprite_vs_raw);
        if (sprite_ps_raw) wingui_release_blob(sprite_ps_raw);
        delete renderer;
        return 0;
    }

    auto* graphics_vs = static_cast<ID3DBlob*>(graphics_vs_raw);
    auto* graphics_ps = static_cast<ID3DBlob*>(graphics_ps_raw);
    auto* sprite_vs = static_cast<ID3DBlob*>(sprite_vs_raw);
    auto* sprite_ps = static_cast<ID3DBlob*>(sprite_ps_raw);

    HRESULT hr = renderer->context->device->CreateVertexShader(graphics_vs->GetBufferPointer(), graphics_vs->GetBufferSize(), nullptr, &renderer->graphics_vertex_shader);
    if (FAILED(hr)) goto failure;
    hr = renderer->context->device->CreatePixelShader(graphics_ps->GetBufferPointer(), graphics_ps->GetBufferSize(), nullptr, &renderer->graphics_pixel_shader);
    if (FAILED(hr)) goto failure;
    hr = renderer->context->device->CreateVertexShader(sprite_vs->GetBufferPointer(), sprite_vs->GetBufferSize(), nullptr, &renderer->sprite_vertex_shader);
    if (FAILED(hr)) goto failure;
    hr = renderer->context->device->CreatePixelShader(sprite_ps->GetBufferPointer(), sprite_ps->GetBufferSize(), nullptr, &renderer->sprite_pixel_shader);
    if (FAILED(hr)) goto failure;
    hr = renderer->context->device->CreateInputLayout(
        kWinguiSpriteLayout,
        static_cast<UINT>(std::size(kWinguiSpriteLayout)),
        sprite_vs->GetBufferPointer(),
        sprite_vs->GetBufferSize(),
        &renderer->sprite_input_layout);
    if (FAILED(hr)) goto failure;

    {
        D3D11_BUFFER_DESC desc_cb{};
        desc_cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        desc_cb.ByteWidth = sizeof(WinguiGraphicsDisplayUniforms);
        desc_cb.Usage = D3D11_USAGE_DYNAMIC;
        desc_cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        hr = renderer->context->device->CreateBuffer(&desc_cb, nullptr, &renderer->graphics_constant_buffer);
        if (FAILED(hr)) goto failure;
    }

    {
        D3D11_BUFFER_DESC desc_cb{};
        desc_cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        desc_cb.ByteWidth = sizeof(WinguiTextGridUniforms);
        desc_cb.Usage = D3D11_USAGE_DYNAMIC;
        desc_cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        hr = renderer->context->device->CreateBuffer(&desc_cb, nullptr, &renderer->sprite_constant_buffer);
        if (FAILED(hr)) goto failure;
    }

    {
        D3D11_SAMPLER_DESC desc_sampler{};
        desc_sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        desc_sampler.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc_sampler.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc_sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc_sampler.MaxLOD = D3D11_FLOAT32_MAX;
        hr = renderer->context->device->CreateSamplerState(&desc_sampler, &renderer->point_sampler);
        if (FAILED(hr)) goto failure;
    }

    {
        D3D11_RASTERIZER_DESC desc_raster{};
        desc_raster.FillMode = D3D11_FILL_SOLID;
        desc_raster.CullMode = D3D11_CULL_NONE;
        desc_raster.ScissorEnable = TRUE;
        desc_raster.DepthClipEnable = TRUE;
        hr = renderer->context->device->CreateRasterizerState(&desc_raster, &renderer->rasterizer);
        if (FAILED(hr)) goto failure;
    }

    hr = createSpriteResources(*renderer);
    if (FAILED(hr)) goto failure;

    wingui_release_blob(sprite_ps_raw);
    wingui_release_blob(sprite_vs_raw);
    wingui_release_blob(graphics_ps_raw);
    wingui_release_blob(graphics_vs_raw);
    *out_renderer = renderer;
    g_last_error.clear();
    return 1;

failure:
    setLastErrorHresult("wingui_create_indexed_graphics_renderer: D3D resource creation failed", hr);
    wingui_release_blob(sprite_ps_raw);
    wingui_release_blob(sprite_vs_raw);
    wingui_release_blob(graphics_ps_raw);
    wingui_release_blob(graphics_vs_raw);
    wingui_destroy_indexed_graphics_renderer(renderer);
    return 0;
}

extern "C" WINGUI_API void WINGUI_CALL wingui_destroy_indexed_graphics_renderer(WinguiIndexedGraphicsRenderer* renderer) {
    if (!renderer) return;
    safeRelease(renderer->sprite_blend_state);
    safeRelease(renderer->sprite_palette_srv);
    safeRelease(renderer->sprite_palette_texture);
    safeRelease(renderer->sprite_atlas_srv);
    safeRelease(renderer->sprite_atlas_texture);
    safeRelease(renderer->sprite_vertex_buffer);
    safeRelease(renderer->sprite_constant_buffer);
    safeRelease(renderer->sprite_input_layout);
    safeRelease(renderer->sprite_vertex_shader);
    safeRelease(renderer->sprite_pixel_shader);
    safeRelease(renderer->graphics_global_palette_srv);
    safeRelease(renderer->graphics_global_palette_texture);
    safeRelease(renderer->graphics_line_palette_srv);
    safeRelease(renderer->graphics_line_palette_texture);
    safeRelease(renderer->graphics_indexed_srv);
    safeRelease(renderer->graphics_indexed_texture);
    safeRelease(renderer->graphics_constant_buffer);
    safeRelease(renderer->point_sampler);
    safeRelease(renderer->rasterizer);
    safeRelease(renderer->graphics_vertex_shader);
    safeRelease(renderer->graphics_pixel_shader);
    delete renderer;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_compute_indexed_pane_layout(
    int32_t viewport_x,
    int32_t viewport_y,
    int32_t viewport_width,
    int32_t viewport_height,
    uint32_t screen_width,
    uint32_t screen_height,
    uint32_t pixel_aspect_num,
    uint32_t pixel_aspect_den,
    WinguiIndexedPaneLayout* out_layout) {
    if (!out_layout) {
        setLastErrorString("wingui_compute_indexed_pane_layout: invalid arguments");
        return 0;
    }
    if (!computeIndexedPaneLayoutInternal(viewport_x, viewport_y, viewport_width, viewport_height, screen_width, screen_height, pixel_aspect_num, pixel_aspect_den, *out_layout)) {
        setLastErrorString("wingui_compute_indexed_pane_layout: invalid dimensions");
        return 0;
    }
    g_last_error.clear();
    return 1;
}

void releaseIndexedBufferResources(WinguiIndexedBufferResources& buffer) {
    safeRelease(buffer.global_palette_srv);
    safeRelease(buffer.global_palette_texture);
    safeRelease(buffer.line_palette_srv);
    safeRelease(buffer.line_palette_texture);
    safeRelease(buffer.pixels_srv);
    safeRelease(buffer.pixels_texture);
}

HRESULT createIndexedSurfaceBuffer(WinguiContext* context, WinguiIndexedBufferResources& buffer, UINT width, UINT height) {
    if (!context || !context->device) return E_POINTER;
    auto make_default_texture = [&](UINT w, UINT h, DXGI_FORMAT fmt, ID3D11Texture2D** tex_out, ID3D11ShaderResourceView** srv_out) -> HRESULT {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = w;
        desc.Height = h;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = fmt;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        HRESULT hr = context->device->CreateTexture2D(&desc, nullptr, tex_out);
        if (FAILED(hr)) return hr;
        D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc{};
        srv_desc.Format = fmt;
        srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srv_desc.Texture2D.MipLevels = 1;
        hr = context->device->CreateShaderResourceView(*tex_out, &srv_desc, srv_out);
        if (FAILED(hr)) {
            safeRelease(*tex_out);
            return hr;
        }
        return S_OK;
    };

    HRESULT hr = make_default_texture(width, height, DXGI_FORMAT_R8_UINT, &buffer.pixels_texture, &buffer.pixels_srv);
    if (FAILED(hr)) return hr;
    hr = make_default_texture(16, height, DXGI_FORMAT_R32_UINT, &buffer.line_palette_texture, &buffer.line_palette_srv);
    if (FAILED(hr)) {
        releaseIndexedBufferResources(buffer);
        return hr;
    }
    hr = make_default_texture(240, 1, DXGI_FORMAT_R32_UINT, &buffer.global_palette_texture, &buffer.global_palette_srv);
    if (FAILED(hr)) {
        releaseIndexedBufferResources(buffer);
        return hr;
    }
    return S_OK;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_create_indexed_surface(
    WinguiContext* context,
    uint32_t buffer_count,
    WinguiIndexedSurface** out_surface) {
    if (!context || !out_surface) {
        setLastErrorString("wingui_create_indexed_surface: invalid arguments");
        return 0;
    }
    auto* surface = new (std::nothrow) WinguiIndexedSurface();
    if (!surface) {
        setLastErrorString("wingui_create_indexed_surface: allocation failed");
        return 0;
    }
    surface->context = context;
    surface->buffers.resize(buffer_count == 0 ? 1u : buffer_count);
    *out_surface = surface;
    g_last_error.clear();
    return 1;
}

extern "C" WINGUI_API void WINGUI_CALL wingui_destroy_indexed_surface(WinguiIndexedSurface* surface) {
    if (!surface) return;
    for (auto& buffer : surface->buffers) {
        releaseIndexedBufferResources(buffer);
    }
    delete surface;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_indexed_surface_ensure_buffers(
    WinguiIndexedSurface* surface,
    uint32_t width,
    uint32_t height) {
    if (!surface || !surface->context || !width || !height) {
        setLastErrorString("wingui_indexed_surface_ensure_buffers: invalid arguments");
        return 0;
    }
    if (surface->buffer_width == width && surface->buffer_height == height &&
        !surface->buffers.empty() && surface->buffers[0].pixels_texture) {
        return 1;
    }
    for (auto& buffer : surface->buffers) {
        releaseIndexedBufferResources(buffer);
    }
    surface->buffer_width = 0;
    surface->buffer_height = 0;
    for (auto& buffer : surface->buffers) {
        HRESULT hr = createIndexedSurfaceBuffer(surface->context, buffer, width, height);
        if (FAILED(hr)) {
            for (auto& cleanup : surface->buffers) releaseIndexedBufferResources(cleanup);
            setLastErrorHresult("wingui_indexed_surface_ensure_buffers: texture creation failed", hr);
            return 0;
        }
    }
    surface->buffer_width = width;
    surface->buffer_height = height;
    g_last_error.clear();
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_indexed_surface_get_buffer_info(
    WinguiIndexedSurface* surface,
    uint32_t* out_width,
    uint32_t* out_height,
    uint32_t* out_buffer_count) {
    if (!surface) {
        setLastErrorString("wingui_indexed_surface_get_buffer_info: invalid arguments");
        return 0;
    }
    if (out_width) *out_width = surface->buffer_width;
    if (out_height) *out_height = surface->buffer_height;
    if (out_buffer_count) *out_buffer_count = static_cast<uint32_t>(surface->buffers.size());
    g_last_error.clear();
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_indexed_surface_upload_pixels_region(
    WinguiIndexedSurface* surface,
    uint32_t buffer_index,
    WinguiRectU32 destination_region,
    const uint8_t* indexed_pixels,
    uint32_t source_pitch) {
    if (!surface || !surface->context || !indexed_pixels || buffer_index >= surface->buffers.size()) {
        setLastErrorString("wingui_indexed_surface_upload_pixels_region: invalid arguments");
        return 0;
    }
    if (!destination_region.width || !destination_region.height) {
        setLastErrorString("wingui_indexed_surface_upload_pixels_region: invalid region");
        return 0;
    }
    if (!surface->buffer_width || !surface->buffer_height || !surface->buffers[buffer_index].pixels_texture) {
        setLastErrorString("wingui_indexed_surface_upload_pixels_region: buffers not initialized");
        return 0;
    }
    if (destination_region.x + destination_region.width > surface->buffer_width ||
        destination_region.y + destination_region.height > surface->buffer_height) {
        setLastErrorString("wingui_indexed_surface_upload_pixels_region: region out of bounds");
        return 0;
    }
    const D3D11_BOX box{
        destination_region.x,
        destination_region.y,
        0,
        destination_region.x + destination_region.width,
        destination_region.y + destination_region.height,
        1,
    };
    surface->context->device_context->UpdateSubresource(
        surface->buffers[buffer_index].pixels_texture,
        0,
        &box,
        indexed_pixels,
        source_pitch ? source_pitch : destination_region.width,
        0);
    g_last_error.clear();
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_indexed_surface_upload_line_palettes(
    WinguiIndexedSurface* surface,
    uint32_t buffer_index,
    uint32_t start_row,
    uint32_t row_count,
    const WinguiGraphicsLinePalette* palettes) {
    if (!surface || !surface->context || !palettes || buffer_index >= surface->buffers.size()) {
        setLastErrorString("wingui_indexed_surface_upload_line_palettes: invalid arguments");
        return 0;
    }
    if (!row_count || !surface->buffer_height || !surface->buffers[buffer_index].line_palette_texture) {
        setLastErrorString("wingui_indexed_surface_upload_line_palettes: invalid request");
        return 0;
    }
    if (start_row + row_count > surface->buffer_height) {
        setLastErrorString("wingui_indexed_surface_upload_line_palettes: row range out of bounds");
        return 0;
    }
    std::vector<uint32_t> packed(static_cast<size_t>(row_count) * 16u);
    for (uint32_t y = 0; y < row_count; ++y) {
        for (uint32_t i = 0; i < 16; ++i) {
            packed[static_cast<size_t>(y) * 16u + i] = packGraphicsColour(palettes[y].colours[i]);
        }
    }
    const D3D11_BOX box{ 0u, start_row, 0u, 16u, start_row + row_count, 1u };
    surface->context->device_context->UpdateSubresource(
        surface->buffers[buffer_index].line_palette_texture,
        0,
        &box,
        packed.data(),
        16u * sizeof(uint32_t),
        0);
    g_last_error.clear();
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_indexed_surface_upload_global_palette(
    WinguiIndexedSurface* surface,
    uint32_t buffer_index,
    uint32_t start_index,
    uint32_t colour_count,
    const WinguiGraphicsColour* colours) {
    if (!surface || !surface->context || !colours || buffer_index >= surface->buffers.size()) {
        setLastErrorString("wingui_indexed_surface_upload_global_palette: invalid arguments");
        return 0;
    }
    if (!colour_count || !surface->buffers[buffer_index].global_palette_texture) {
        setLastErrorString("wingui_indexed_surface_upload_global_palette: invalid request");
        return 0;
    }
    if (start_index + colour_count > 240u) {
        setLastErrorString("wingui_indexed_surface_upload_global_palette: range out of bounds");
        return 0;
    }
    std::vector<uint32_t> packed(colour_count);
    for (uint32_t i = 0; i < colour_count; ++i) {
        packed[i] = packGraphicsColour(colours[i]);
    }
    const D3D11_BOX box{ start_index, 0u, 0u, start_index + colour_count, 1u, 1u };
    surface->context->device_context->UpdateSubresource(
        surface->buffers[buffer_index].global_palette_texture,
        0,
        &box,
        packed.data(),
        colour_count * sizeof(uint32_t),
        0);
    g_last_error.clear();
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_indexed_surface_render(
    WinguiIndexedGraphicsRenderer* renderer,
    WinguiIndexedSurface* surface,
    int32_t viewport_x,
    int32_t viewport_y,
    int32_t viewport_width,
    int32_t viewport_height,
    uint32_t screen_width,
    uint32_t screen_height,
    int32_t scroll_x,
    int32_t scroll_y,
    uint32_t pixel_aspect_num,
    uint32_t pixel_aspect_den,
    uint32_t buffer_index,
    WinguiIndexedPaneLayout* out_layout) {
    if (!renderer || !renderer->context || !renderer->context->device_context || !surface ||
        renderer->context != surface->context) {
        setLastErrorString("wingui_indexed_surface_render: invalid arguments");
        return 0;
    }
    if (buffer_index >= surface->buffers.size() || !surface->buffer_width || !surface->buffer_height ||
        !surface->buffers[buffer_index].pixels_srv ||
        !surface->buffers[buffer_index].line_palette_srv ||
        !surface->buffers[buffer_index].global_palette_srv) {
        setLastErrorString("wingui_indexed_surface_render: buffer not ready");
        return 0;
    }
    WinguiIndexedPaneLayout layout{};
    if (!computeIndexedPaneLayoutInternal(
            viewport_x, viewport_y, viewport_width, viewport_height,
            screen_width ? screen_width : surface->buffer_width,
            screen_height ? screen_height : surface->buffer_height,
            pixel_aspect_num, pixel_aspect_den, layout)) {
        setLastErrorString("wingui_indexed_surface_render: invalid pane layout inputs");
        return 0;
    }
    if (out_layout) *out_layout = layout;

    WinguiGraphicsDisplayUniforms uniforms{};
    uniforms.uv_scale_x = layout.shown_width / std::max(1.0f, static_cast<float>(viewport_width));
    uniforms.uv_scale_y = layout.shown_height / std::max(1.0f, static_cast<float>(viewport_height));
    uniforms.uv_offset_x = (1.0f - uniforms.uv_scale_x) * 0.5f;
    uniforms.uv_offset_y = (1.0f - uniforms.uv_scale_y) * 0.5f;
    uniforms.screen_width = screen_width ? screen_width : surface->buffer_width;
    uniforms.screen_height = screen_height ? screen_height : surface->buffer_height;
    uniforms.buffer_width = surface->buffer_width;
    uniforms.buffer_height = surface->buffer_height;
    uniforms.scroll_x = scroll_x;
    uniforms.scroll_y = scroll_y;
    uniforms.has_texture = 1;

    auto* ctx = renderer->context->device_context;
    D3D11_MAPPED_SUBRESOURCE constant_map{};
    HRESULT hr = ctx->Map(renderer->graphics_constant_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &constant_map);
    if (FAILED(hr)) {
        setLastErrorHresult("wingui_indexed_surface_render: cbuffer map failed", hr);
        return 0;
    }
    std::memcpy(constant_map.pData, &uniforms, sizeof(uniforms));
    ctx->Unmap(renderer->graphics_constant_buffer, 0);

    const D3D11_VIEWPORT viewport{ static_cast<float>(viewport_x), static_cast<float>(viewport_y), static_cast<float>(viewport_width), static_cast<float>(viewport_height), 0.0f, 1.0f };
    const D3D11_RECT scissor{ viewport_x, viewport_y, viewport_x + viewport_width, viewport_y + viewport_height };
    ID3D11Buffer* no_vertex_buffer = nullptr;
    const UINT stride = 0;
    const UINT offset = 0;
    ID3D11ShaderResourceView* srvs[] = {
        surface->buffers[buffer_index].pixels_srv,
        surface->buffers[buffer_index].line_palette_srv,
        surface->buffers[buffer_index].global_palette_srv,
    };

    ctx->OMSetRenderTargets(1, &renderer->context->render_target_view, nullptr);
    ctx->RSSetState(renderer->rasterizer);
    ctx->RSSetViewports(1, &viewport);
    ctx->RSSetScissorRects(1, &scissor);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->IASetInputLayout(nullptr);
    ctx->IASetVertexBuffers(0, 1, &no_vertex_buffer, &stride, &offset);
    ctx->VSSetShader(renderer->graphics_vertex_shader, nullptr, 0);
    ctx->PSSetShader(renderer->graphics_pixel_shader, nullptr, 0);
    ctx->VSSetConstantBuffers(0, 1, &renderer->graphics_constant_buffer);
    ctx->PSSetConstantBuffers(0, 1, &renderer->graphics_constant_buffer);
    ctx->PSSetSamplers(0, 1, &renderer->point_sampler);
    ctx->PSSetShaderResources(0, 3, srvs);
    ctx->Draw(6, 0);
    ID3D11ShaderResourceView* null_srvs[3] = { nullptr, nullptr, nullptr };
    ctx->PSSetShaderResources(0, 3, null_srvs);
    g_last_error.clear();
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_indexed_graphics_render_pane(
    WinguiIndexedGraphicsRenderer* renderer,
    int32_t viewport_x,
    int32_t viewport_y,
    int32_t viewport_width,
    int32_t viewport_height,
    const WinguiIndexedGraphicsFrame* frame,
    WinguiIndexedPaneLayout* out_layout) {
    if (!renderer || !renderer->context || !renderer->context->device_context || !frame) {
        setLastErrorString("wingui_indexed_graphics_render_pane: invalid arguments");
        return 0;
    }
    if (!frame->indexed_pixels || !frame->line_palettes || !frame->global_palette || frame->buffer_width == 0 || frame->buffer_height == 0) {
        setLastErrorString("wingui_indexed_graphics_render_pane: frame is incomplete");
        return 0;
    }

    WinguiIndexedPaneLayout computed_layout{};
    if (!computeIndexedPaneLayoutInternal(
            viewport_x,
            viewport_y,
            viewport_width,
            viewport_height,
            frame->screen_width,
            frame->screen_height,
            frame->pixel_aspect_num,
            frame->pixel_aspect_den,
            computed_layout)) {
        setLastErrorString("wingui_indexed_graphics_render_pane: invalid pane layout inputs");
        return 0;
    }
    if (out_layout) *out_layout = computed_layout;

    HRESULT hr = ensureIndexedGraphicsTextures(*renderer, frame->buffer_width, frame->buffer_height);
    if (FAILED(hr)) {
        setLastErrorHresult("wingui_indexed_graphics_render_pane: texture allocation failed", hr);
        return 0;
    }

    D3D11_MAPPED_SUBRESOURCE texture_map{};
    hr = renderer->context->device_context->Map(renderer->graphics_line_palette_texture, 0, D3D11_MAP_WRITE_DISCARD, 0, &texture_map);
    if (FAILED(hr)) {
        setLastErrorHresult("wingui_indexed_graphics_render_pane: line palette map failed", hr);
        return 0;
    }
    for (uint32_t y = 0; y < frame->buffer_height; ++y) {
        uint32_t* dst = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(texture_map.pData) + static_cast<size_t>(y) * texture_map.RowPitch);
        for (uint32_t i = 0; i < 16; ++i) dst[i] = packGraphicsColour(frame->line_palettes[y].colours[i]);
    }
    renderer->context->device_context->Unmap(renderer->graphics_line_palette_texture, 0);

    hr = renderer->context->device_context->Map(renderer->graphics_global_palette_texture, 0, D3D11_MAP_WRITE_DISCARD, 0, &texture_map);
    if (FAILED(hr)) {
        setLastErrorHresult("wingui_indexed_graphics_render_pane: global palette map failed", hr);
        return 0;
    }
    uint32_t* global_dst = reinterpret_cast<uint32_t*>(texture_map.pData);
    for (uint32_t i = 0; i < 240; ++i) global_dst[i] = packGraphicsColour(frame->global_palette[i]);
    renderer->context->device_context->Unmap(renderer->graphics_global_palette_texture, 0);

    hr = renderer->context->device_context->Map(renderer->graphics_indexed_texture, 0, D3D11_MAP_WRITE_DISCARD, 0, &texture_map);
    if (FAILED(hr)) {
        setLastErrorHresult("wingui_indexed_graphics_render_pane: pixel texture map failed", hr);
        return 0;
    }
    if (texture_map.RowPitch == frame->buffer_width) {
        std::memcpy(texture_map.pData, frame->indexed_pixels, static_cast<size_t>(frame->buffer_width) * frame->buffer_height);
    } else {
        for (uint32_t y = 0; y < frame->buffer_height; ++y) {
            const uint8_t* src = frame->indexed_pixels + static_cast<size_t>(y) * frame->buffer_width;
            uint8_t* dst = static_cast<uint8_t*>(texture_map.pData) + static_cast<size_t>(y) * texture_map.RowPitch;
            std::memcpy(dst, src, frame->buffer_width);
        }
    }
    renderer->context->device_context->Unmap(renderer->graphics_indexed_texture, 0);

    WinguiGraphicsDisplayUniforms uniforms{};
    uniforms.uv_scale_x = computed_layout.shown_width / std::max(1.0f, static_cast<float>(viewport_width));
    uniforms.uv_scale_y = computed_layout.shown_height / std::max(1.0f, static_cast<float>(viewport_height));
    uniforms.uv_offset_x = (1.0f - uniforms.uv_scale_x) * 0.5f;
    uniforms.uv_offset_y = (1.0f - uniforms.uv_scale_y) * 0.5f;
    uniforms.screen_width = frame->screen_width;
    uniforms.screen_height = frame->screen_height;
    uniforms.buffer_width = frame->buffer_width;
    uniforms.buffer_height = frame->buffer_height;
    uniforms.scroll_x = frame->scroll_x;
    uniforms.scroll_y = frame->scroll_y;
    uniforms.has_texture = 1;

    D3D11_MAPPED_SUBRESOURCE constant_map{};
    hr = renderer->context->device_context->Map(renderer->graphics_constant_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &constant_map);
    if (FAILED(hr)) {
        setLastErrorHresult("wingui_indexed_graphics_render_pane: constant buffer map failed", hr);
        return 0;
    }
    std::memcpy(constant_map.pData, &uniforms, sizeof(uniforms));
    renderer->context->device_context->Unmap(renderer->graphics_constant_buffer, 0);

    const D3D11_VIEWPORT viewport{ static_cast<float>(viewport_x), static_cast<float>(viewport_y), static_cast<float>(viewport_width), static_cast<float>(viewport_height), 0.0f, 1.0f };
    const D3D11_RECT scissor{ viewport_x, viewport_y, viewport_x + viewport_width, viewport_y + viewport_height };
    ID3D11Buffer* no_vertex_buffer = nullptr;
    const UINT stride = 0;
    const UINT offset = 0;
    ID3D11ShaderResourceView* srvs[] = {
        renderer->graphics_indexed_srv,
        renderer->graphics_line_palette_srv,
        renderer->graphics_global_palette_srv,
    };

    renderer->context->device_context->OMSetRenderTargets(1, &renderer->context->render_target_view, nullptr);
    renderer->context->device_context->RSSetState(renderer->rasterizer);
    renderer->context->device_context->RSSetViewports(1, &viewport);
    renderer->context->device_context->RSSetScissorRects(1, &scissor);
    renderer->context->device_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    renderer->context->device_context->IASetInputLayout(nullptr);
    renderer->context->device_context->IASetVertexBuffers(0, 1, &no_vertex_buffer, &stride, &offset);
    renderer->context->device_context->VSSetShader(renderer->graphics_vertex_shader, nullptr, 0);
    renderer->context->device_context->PSSetShader(renderer->graphics_pixel_shader, nullptr, 0);
    renderer->context->device_context->VSSetConstantBuffers(0, 1, &renderer->graphics_constant_buffer);
    renderer->context->device_context->PSSetConstantBuffers(0, 1, &renderer->graphics_constant_buffer);
    renderer->context->device_context->PSSetSamplers(0, 1, &renderer->point_sampler);
    renderer->context->device_context->PSSetShaderResources(0, 3, srvs);
    renderer->context->device_context->Draw(6, 0);

    g_last_error.clear();
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_indexed_graphics_upload_sprite_atlas_region(
    WinguiIndexedGraphicsRenderer* renderer,
    uint32_t atlas_x,
    uint32_t atlas_y,
    uint32_t width,
    uint32_t height,
    const uint8_t* pixels,
    uint32_t source_pitch) {
    if (!renderer || !renderer->sprite_atlas_texture || !pixels || width == 0 || height == 0) {
        setLastErrorString("wingui_indexed_graphics_upload_sprite_atlas_region: invalid arguments");
        return 0;
    }
    if (atlas_x + width > renderer->sprite_atlas_size || atlas_y + height > renderer->sprite_atlas_size) {
        setLastErrorString("wingui_indexed_graphics_upload_sprite_atlas_region: region out of bounds");
        return 0;
    }
    D3D11_BOX box{ atlas_x, atlas_y, 0, atlas_x + width, atlas_y + height, 1 };
    renderer->context->device_context->UpdateSubresource(renderer->sprite_atlas_texture, 0, &box, pixels, source_pitch ? source_pitch : width, 0);
    g_last_error.clear();
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_indexed_graphics_upload_sprite_palettes(
    WinguiIndexedGraphicsRenderer* renderer,
    const WinguiGraphicsLinePalette* palettes,
    uint32_t palette_count) {
    if (!renderer || !renderer->sprite_palette_texture || !palettes || palette_count == 0) {
        setLastErrorString("wingui_indexed_graphics_upload_sprite_palettes: invalid arguments");
        return 0;
    }
    const uint32_t clamped_count = std::min<uint32_t>(palette_count, renderer->sprite_max_palettes);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    HRESULT hr = renderer->context->device_context->Map(renderer->sprite_palette_texture, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) {
        setLastErrorHresult("wingui_indexed_graphics_upload_sprite_palettes: palette texture map failed", hr);
        return 0;
    }
    for (uint32_t row = 0; row < clamped_count; ++row) {
        uint32_t* dst = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(mapped.pData) + static_cast<size_t>(row) * mapped.RowPitch);
        for (uint32_t i = 0; i < 16; ++i) dst[i] = packGraphicsColour(palettes[row].colours[i]);
    }
    renderer->context->device_context->Unmap(renderer->sprite_palette_texture, 0);
    g_last_error.clear();
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_indexed_graphics_render_sprites(
    WinguiIndexedGraphicsRenderer* renderer,
    uint32_t target_width,
    uint32_t target_height,
    const WinguiIndexedPaneLayout* layout,
    const WinguiSpriteAtlasEntry* atlas_entries,
    uint32_t atlas_entry_count,
    const WinguiSpriteInstance* instances,
    uint32_t instance_count) {
    if (!renderer || !renderer->context || !renderer->context->device_context || !layout || !atlas_entries || !instances) {
        setLastErrorString("wingui_indexed_graphics_render_sprites: invalid arguments");
        return 0;
    }
    if (!renderer->sprite_vertex_shader || !renderer->sprite_pixel_shader || !renderer->sprite_input_layout || !renderer->sprite_atlas_srv || !renderer->sprite_palette_srv) {
        setLastErrorString("wingui_indexed_graphics_render_sprites: renderer is not initialized");
        return 0;
    }
    if (instance_count == 0 || atlas_entry_count == 0) {
        g_last_error.clear();
        return 1;
    }

    renderer->sprite_vertices.clear();
    renderer->sprite_vertices.reserve(static_cast<size_t>(instance_count) * 6);

    const auto push_vertex = [&](float px, float py, float atlas_x, float atlas_y, float palette_slot, const WinguiSpriteInstance& inst) {
        WinguiSpriteVertex vertex{};
        vertex.pos[0] = px;
        vertex.pos[1] = py;
        vertex.atlas_px[0] = atlas_x;
        vertex.atlas_px[1] = atlas_y;
        vertex.palette_slot = palette_slot;
        vertex.alpha = inst.alpha;
        vertex.effect_type = static_cast<float>(inst.effect_type);
        vertex.effect_param1 = inst.effect_param1;
        vertex.effect_param2 = inst.effect_param2;
        vertex.effect_colour[0] = inst.effect_colour[0] / 255.0f;
        vertex.effect_colour[1] = inst.effect_colour[1] / 255.0f;
        vertex.effect_colour[2] = inst.effect_colour[2] / 255.0f;
        vertex.effect_colour[3] = inst.effect_colour[3] / 255.0f;
        renderer->sprite_vertices.push_back(vertex);
    };

    const uint32_t clamped_instances = std::min<uint32_t>(instance_count, 512u);
    for (uint32_t i = 0; i < clamped_instances; ++i) {
        const WinguiSpriteInstance& inst = instances[i];
        if ((inst.flags & WINGUI_SPRITE_FLAG_VISIBLE) == 0) continue;
        if (inst.atlas_entry_id >= atlas_entry_count) continue;

        const WinguiSpriteAtlasEntry& entry = atlas_entries[inst.atlas_entry_id];
        const float frame_w = static_cast<float>(entry.frame_w ? entry.frame_w : entry.width);
        const float frame_h = static_cast<float>(entry.frame_h ? entry.frame_h : entry.height);
        if (frame_w <= 0.0f || frame_h <= 0.0f) continue;

        const uint32_t frame_count = std::max<uint32_t>(entry.frame_count, 1u);
        const uint32_t frame_index = inst.frame % frame_count;
        const float src_x0 = static_cast<float>(entry.atlas_x) + frame_w * static_cast<float>(frame_index);
        const float src_y0 = static_cast<float>(entry.atlas_y);
        float atlas_left = src_x0;
        float atlas_right = src_x0 + frame_w - 1.0f;
        float atlas_top = src_y0;
        float atlas_bottom = src_y0 + frame_h - 1.0f;
        if (inst.flags & WINGUI_SPRITE_FLAG_FLIP_H) std::swap(atlas_left, atlas_right);
        if (inst.flags & WINGUI_SPRITE_FLAG_FLIP_V) std::swap(atlas_top, atlas_bottom);

        const float scaled_w = frame_w * inst.scale_x;
        const float scaled_h = frame_h * inst.scale_y;
        const float anchor_x = scaled_w * inst.anchor_x;
        const float anchor_y = scaled_h * inst.anchor_y;
        const float pivot_x = inst.x + anchor_x;
        const float pivot_y = inst.y + anchor_y;
        const float cos_a = std::cos(inst.rotation);
        const float sin_a = std::sin(inst.rotation);

        struct Corner { float x; float y; float atlas_x; float atlas_y; };
        Corner corners[4] = {
            { -anchor_x, -anchor_y, atlas_left, atlas_top },
            { scaled_w - anchor_x, -anchor_y, atlas_right, atlas_top },
            { -anchor_x, scaled_h - anchor_y, atlas_left, atlas_bottom },
            { scaled_w - anchor_x, scaled_h - anchor_y, atlas_right, atlas_bottom },
        };

        for (Corner& corner : corners) {
            const float rotated_x = corner.x * cos_a - corner.y * sin_a;
            const float rotated_y = corner.x * sin_a + corner.y * cos_a;
            const float screen_x = pivot_x + rotated_x;
            const float screen_y = pivot_y + rotated_y;
            corner.x = layout->origin_x + screen_x * layout->scale_x;
            corner.y = layout->origin_y + screen_y * layout->scale_y;
        }

        const float palette_slot = static_cast<float>(inst.palette_override > 0 ? (inst.palette_override - 1) : entry.palette_offset);
        push_vertex(corners[0].x, corners[0].y, corners[0].atlas_x, corners[0].atlas_y, palette_slot, inst);
        push_vertex(corners[1].x, corners[1].y, corners[1].atlas_x, corners[1].atlas_y, palette_slot, inst);
        push_vertex(corners[2].x, corners[2].y, corners[2].atlas_x, corners[2].atlas_y, palette_slot, inst);
        push_vertex(corners[2].x, corners[2].y, corners[2].atlas_x, corners[2].atlas_y, palette_slot, inst);
        push_vertex(corners[1].x, corners[1].y, corners[1].atlas_x, corners[1].atlas_y, palette_slot, inst);
        push_vertex(corners[3].x, corners[3].y, corners[3].atlas_x, corners[3].atlas_y, palette_slot, inst);
    }

    if (renderer->sprite_vertices.empty()) {
        g_last_error.clear();
        return 1;
    }

    HRESULT hr = ensureSpriteVertexBuffer(*renderer, renderer->sprite_vertices.size());
    if (FAILED(hr)) {
        setLastErrorHresult("wingui_indexed_graphics_render_sprites: vertex buffer allocation failed", hr);
        return 0;
    }

    D3D11_MAPPED_SUBRESOURCE mapped{};
    hr = renderer->context->device_context->Map(renderer->sprite_vertex_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) {
        setLastErrorHresult("wingui_indexed_graphics_render_sprites: vertex buffer map failed", hr);
        return 0;
    }
    std::memcpy(mapped.pData, renderer->sprite_vertices.data(), renderer->sprite_vertices.size() * sizeof(WinguiSpriteVertex));
    renderer->context->device_context->Unmap(renderer->sprite_vertex_buffer, 0);

    WinguiTextGridUniforms uniforms_cb{};
    uniforms_cb.viewport_width = static_cast<float>(std::max<uint32_t>(target_width, 1u));
    uniforms_cb.viewport_height = static_cast<float>(std::max<uint32_t>(target_height, 1u));
    uniforms_cb.row_origin = 0.0f;

    hr = renderer->context->device_context->Map(renderer->sprite_constant_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) {
        setLastErrorHresult("wingui_indexed_graphics_render_sprites: constant buffer map failed", hr);
        return 0;
    }
    std::memcpy(mapped.pData, &uniforms_cb, sizeof(uniforms_cb));
    renderer->context->device_context->Unmap(renderer->sprite_constant_buffer, 0);

    const D3D11_VIEWPORT viewport{ 0.0f, 0.0f, static_cast<float>(std::max<uint32_t>(target_width, 1u)), static_cast<float>(std::max<uint32_t>(target_height, 1u)), 0.0f, 1.0f };
    const D3D11_RECT scissor{
        static_cast<LONG>(std::floor(layout->origin_x)),
        static_cast<LONG>(std::floor(layout->origin_y)),
        static_cast<LONG>(std::ceil(layout->origin_x + layout->shown_width)),
        static_cast<LONG>(std::ceil(layout->origin_y + layout->shown_height))
    };
    const UINT stride = sizeof(WinguiSpriteVertex);
    const UINT offset = 0;
    const float blend_factor[4] = { 0, 0, 0, 0 };
    ID3D11ShaderResourceView* sprite_srvs[] = { renderer->sprite_atlas_srv, renderer->sprite_palette_srv };

    renderer->context->device_context->OMSetRenderTargets(1, &renderer->context->render_target_view, nullptr);
    renderer->context->device_context->RSSetState(renderer->rasterizer);
    renderer->context->device_context->RSSetViewports(1, &viewport);
    renderer->context->device_context->RSSetScissorRects(1, &scissor);
    renderer->context->device_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    renderer->context->device_context->IASetInputLayout(renderer->sprite_input_layout);
    renderer->context->device_context->IASetVertexBuffers(0, 1, &renderer->sprite_vertex_buffer, &stride, &offset);
    renderer->context->device_context->VSSetShader(renderer->sprite_vertex_shader, nullptr, 0);
    renderer->context->device_context->PSSetShader(renderer->sprite_pixel_shader, nullptr, 0);
    renderer->context->device_context->VSSetConstantBuffers(0, 1, &renderer->sprite_constant_buffer);
    renderer->context->device_context->PSSetConstantBuffers(0, 1, &renderer->sprite_constant_buffer);
    renderer->context->device_context->PSSetShaderResources(0, 2, sprite_srvs);
    renderer->context->device_context->OMSetBlendState(renderer->sprite_blend_state, blend_factor, 0xffffffffu);
    renderer->context->device_context->Draw(static_cast<UINT>(renderer->sprite_vertices.size()), 0);

    ID3D11ShaderResourceView* null_srvs[2] = { nullptr, nullptr };
    renderer->context->device_context->PSSetShaderResources(0, 2, null_srvs);
    renderer->context->device_context->OMSetBlendState(nullptr, blend_factor, 0xffffffffu);

    g_last_error.clear();
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_create_rgba_pane_renderer(
    const WinguiRgbaPaneRendererDesc* desc,
    WinguiRgbaPaneRenderer** out_renderer) {
    if (!desc || !desc->context || !out_renderer) {
        setLastErrorString("wingui_create_rgba_pane_renderer: invalid arguments");
        return 0;
    }

    auto* renderer = new (std::nothrow) WinguiRgbaPaneRenderer();
    if (!renderer) {
        setLastErrorString("wingui_create_rgba_pane_renderer: allocation failed");
        return 0;
    }
    renderer->context = desc->context;
    renderer->buffers.resize(std::max<uint32_t>(1, desc->buffer_count ? desc->buffer_count : 8u));

    const char* shader_path = desc->shader_path_utf8 && *desc->shader_path_utf8 ? desc->shader_path_utf8 : "wingui/shaders/graphics.hlsl";
    void* vs_blob_raw = nullptr;
    void* ps_blob_raw = nullptr;
    size_t blob_size = 0;
    if (!wingui_compile_shader_from_file_utf8(shader_path, "graphics_vertex", "vs_4_0", &vs_blob_raw, &blob_size) ||
        !wingui_compile_shader_from_file_utf8(shader_path, "rgba_fragment", "ps_4_0", &ps_blob_raw, &blob_size)) {
        if (vs_blob_raw) wingui_release_blob(vs_blob_raw);
        if (ps_blob_raw) wingui_release_blob(ps_blob_raw);
        delete renderer;
        return 0;
    }

    auto* vs_blob = static_cast<ID3DBlob*>(vs_blob_raw);
    auto* ps_blob = static_cast<ID3DBlob*>(ps_blob_raw);
    HRESULT hr = renderer->context->device->CreateVertexShader(vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), nullptr, &renderer->vertex_shader);
    if (FAILED(hr)) goto failure;
    hr = renderer->context->device->CreatePixelShader(ps_blob->GetBufferPointer(), ps_blob->GetBufferSize(), nullptr, &renderer->pixel_shader);
    if (FAILED(hr)) goto failure;

    {
        D3D11_BUFFER_DESC desc_cb{};
        desc_cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        desc_cb.ByteWidth = sizeof(WinguiGraphicsDisplayUniforms);
        desc_cb.Usage = D3D11_USAGE_DYNAMIC;
        desc_cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        hr = renderer->context->device->CreateBuffer(&desc_cb, nullptr, &renderer->constant_buffer);
        if (FAILED(hr)) goto failure;
    }

    {
        D3D11_SAMPLER_DESC sampler_desc{};
        sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;
        hr = renderer->context->device->CreateSamplerState(&sampler_desc, &renderer->linear_sampler);
        if (FAILED(hr)) goto failure;
    }

    {
        D3D11_RASTERIZER_DESC raster_desc{};
        raster_desc.FillMode = D3D11_FILL_SOLID;
        raster_desc.CullMode = D3D11_CULL_NONE;
        raster_desc.ScissorEnable = TRUE;
        raster_desc.DepthClipEnable = TRUE;
        hr = renderer->context->device->CreateRasterizerState(&raster_desc, &renderer->rasterizer);
        if (FAILED(hr)) goto failure;
    }

    wingui_release_blob(ps_blob_raw);
    wingui_release_blob(vs_blob_raw);
    *out_renderer = renderer;
    g_last_error.clear();
    return 1;

failure:
    setLastErrorHresult("wingui_create_rgba_pane_renderer: D3D resource creation failed", hr);
    wingui_release_blob(ps_blob_raw);
    wingui_release_blob(vs_blob_raw);
    wingui_destroy_rgba_pane_renderer(renderer);
    return 0;
}

extern "C" WINGUI_API void WINGUI_CALL wingui_destroy_rgba_pane_renderer(WinguiRgbaPaneRenderer* renderer) {
    if (!renderer) return;
    releaseRgbaBuffers(renderer->buffers, renderer->buffer_width, renderer->buffer_height);
    safeRelease(renderer->rasterizer);
    safeRelease(renderer->linear_sampler);
    safeRelease(renderer->constant_buffer);
    safeRelease(renderer->vertex_shader);
    safeRelease(renderer->pixel_shader);
    delete renderer;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_create_rgba_surface(
    WinguiContext* context,
    uint32_t buffer_count,
    WinguiRgbaSurface** out_surface) {
    if (!context || !out_surface) {
        setLastErrorString("wingui_create_rgba_surface: invalid arguments");
        return 0;
    }
    auto* surface = new (std::nothrow) WinguiRgbaSurface();
    if (!surface) {
        setLastErrorString("wingui_create_rgba_surface: allocation failed");
        return 0;
    }
    surface->context = context;
    surface->buffers.resize(std::max<uint32_t>(1, buffer_count ? buffer_count : 2u));
    *out_surface = surface;
    g_last_error.clear();
    return 1;
}

extern "C" WINGUI_API void WINGUI_CALL wingui_destroy_rgba_surface(WinguiRgbaSurface* surface) {
    if (!surface) return;
    releaseRgbaBuffers(surface->buffers, surface->buffer_width, surface->buffer_height);
    delete surface;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_rgba_surface_ensure_buffers(
    WinguiRgbaSurface* surface,
    uint32_t width,
    uint32_t height) {
    if (!surface) {
        setLastErrorString("wingui_rgba_surface_ensure_buffers: invalid arguments");
        return 0;
    }
    return rgbaSurfaceEnsureBuffers(
        surface->context,
        surface->buffers,
        surface->buffer_width,
        surface->buffer_height,
        width,
        height,
        "wingui_rgba_surface_ensure_buffers: texture creation failed");
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_rgba_surface_get_buffer_info(
    WinguiRgbaSurface* surface,
    uint32_t* out_width,
    uint32_t* out_height,
    uint32_t* out_buffer_count) {
    if (!surface) {
        setLastErrorString("wingui_rgba_surface_get_buffer_info: invalid arguments");
        return 0;
    }
    return rgbaSurfaceGetBufferInfo(
        surface->buffers,
        surface->buffer_width,
        surface->buffer_height,
        out_width,
        out_height,
        out_buffer_count,
        "wingui_rgba_surface_get_buffer_info: invalid arguments");
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_rgba_surface_upload_bgra8(
    WinguiRgbaSurface* surface,
    uint32_t buffer_index,
    const uint8_t* pixels,
    uint32_t source_pitch) {
    if (!surface) {
        setLastErrorString("wingui_rgba_surface_upload_bgra8: invalid arguments");
        return 0;
    }
    WinguiRectU32 full_region{0u, 0u, surface->buffer_width, surface->buffer_height};
    return rgbaSurfaceUploadBgraRegion(
        surface->context,
        surface->buffers,
        surface->buffer_width,
        surface->buffer_height,
        buffer_index,
        full_region,
        pixels,
        source_pitch,
        "wingui_rgba_surface_upload_bgra8: invalid arguments",
        "wingui_rgba_surface_upload_bgra8: buffers are not initialized",
        "wingui_rgba_surface_upload_bgra8: region out of bounds");
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_rgba_surface_upload_bgra8_region(
    WinguiRgbaSurface* surface,
    uint32_t buffer_index,
    WinguiRectU32 destination_region,
    const uint8_t* pixels,
    uint32_t source_pitch) {
    if (!surface) {
        setLastErrorString("wingui_rgba_surface_upload_bgra8_region: invalid arguments");
        return 0;
    }
    return rgbaSurfaceUploadBgraRegion(
        surface->context,
        surface->buffers,
        surface->buffer_width,
        surface->buffer_height,
        buffer_index,
        destination_region,
        pixels,
        source_pitch,
        "wingui_rgba_surface_upload_bgra8_region: invalid arguments",
        "wingui_rgba_surface_upload_bgra8_region: buffers are not initialized",
        "wingui_rgba_surface_upload_bgra8_region: region out of bounds");
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_rgba_surface_copy_region(
    WinguiRgbaSurface* surface,
    uint32_t dst_buffer_index,
    uint32_t dst_x,
    uint32_t dst_y,
    uint32_t src_buffer_index,
    WinguiRectU32 source_region) {
    if (!surface) {
        setLastErrorString("wingui_rgba_surface_copy_region: invalid arguments");
        return 0;
    }
    return rgbaSurfaceCopyRegion(
        surface->context,
        surface->buffers,
        surface->buffer_width,
        surface->buffer_height,
        dst_buffer_index,
        dst_x,
        dst_y,
        src_buffer_index,
        source_region,
        "wingui_rgba_surface_copy_region: invalid arguments",
        "wingui_rgba_surface_copy_region: buffers are not initialized",
        "wingui_rgba_surface_copy_region: region out of bounds");
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_rgba_surface_copy_from_surface(
    WinguiRgbaSurface* dst_surface,
    uint32_t dst_buffer_index,
    uint32_t dst_x,
    uint32_t dst_y,
    WinguiRgbaSurface* src_surface,
    uint32_t src_buffer_index,
    WinguiRectU32 source_region) {
    if (!dst_surface || !src_surface || !dst_surface->context || dst_surface->context != src_surface->context) {
        setLastErrorString("wingui_rgba_surface_copy_from_surface: invalid arguments");
        return 0;
    }
    if (dst_buffer_index >= dst_surface->buffers.size() || src_buffer_index >= src_surface->buffers.size()) {
        setLastErrorString("wingui_rgba_surface_copy_from_surface: invalid arguments");
        return 0;
    }
    if (!source_region.width || !source_region.height) {
        setLastErrorString("wingui_rgba_surface_copy_from_surface: invalid region");
        return 0;
    }
    if (!dst_surface->buffer_width || !dst_surface->buffer_height ||
        !src_surface->buffer_width || !src_surface->buffer_height ||
        !dst_surface->buffers[dst_buffer_index].texture ||
        !src_surface->buffers[src_buffer_index].texture) {
        setLastErrorString("wingui_rgba_surface_copy_from_surface: buffers are not initialized");
        return 0;
    }
    if (source_region.x + source_region.width > src_surface->buffer_width ||
        source_region.y + source_region.height > src_surface->buffer_height ||
        dst_x + source_region.width > dst_surface->buffer_width ||
        dst_y + source_region.height > dst_surface->buffer_height) {
        setLastErrorString("wingui_rgba_surface_copy_from_surface: region out of bounds");
        return 0;
    }
    const D3D11_BOX source_box{
        source_region.x,
        source_region.y,
        0,
        source_region.x + source_region.width,
        source_region.y + source_region.height,
        1,
    };
    dst_surface->context->device_context->CopySubresourceRegion(
        dst_surface->buffers[dst_buffer_index].texture,
        0,
        dst_x,
        dst_y,
        0,
        src_surface->buffers[src_buffer_index].texture,
        0,
        &source_box);
    g_last_error.clear();
    return 1;
}

extern "C" WINGUI_API void WINGUI_CALL wingui_destroy_rgba_blitter(WinguiRgbaBlitter* blitter) {
    if (!blitter) return;
    safeRelease(blitter->blend_alpha_over);
    safeRelease(blitter->blend_opaque);
    safeRelease(blitter->rasterizer);
    safeRelease(blitter->linear_sampler);
    safeRelease(blitter->constant_buffer);
    safeRelease(blitter->pixel_shader);
    safeRelease(blitter->vertex_shader);
    delete blitter;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_create_rgba_blitter(
    WinguiContext* context,
    const char* shader_path_utf8,
    WinguiRgbaBlitter** out_blitter) {
    if (!context || !context->device || !out_blitter) {
        setLastErrorString("wingui_create_rgba_blitter: invalid arguments");
        return 0;
    }
    auto* blitter = new (std::nothrow) WinguiRgbaBlitter();
    if (!blitter) {
        setLastErrorString("wingui_create_rgba_blitter: allocation failed");
        return 0;
    }
    blitter->context = context;

    const char* shader_path = shader_path_utf8 && *shader_path_utf8 ? shader_path_utf8 : "wingui/shaders/rgba_blit.hlsl";
    void* vs_blob_raw = nullptr;
    void* ps_blob_raw = nullptr;
    size_t blob_size = 0;
    if (!wingui_compile_shader_from_file_utf8(shader_path, "rgba_blit_vertex", "vs_4_0", &vs_blob_raw, &blob_size) ||
        !wingui_compile_shader_from_file_utf8(shader_path, "rgba_blit_fragment", "ps_4_0", &ps_blob_raw, &blob_size)) {
        if (vs_blob_raw) wingui_release_blob(vs_blob_raw);
        if (ps_blob_raw) wingui_release_blob(ps_blob_raw);
        wingui_destroy_rgba_blitter(blitter);
        return 0;
    }
    auto* vs_blob = static_cast<ID3DBlob*>(vs_blob_raw);
    auto* ps_blob = static_cast<ID3DBlob*>(ps_blob_raw);
    HRESULT hr = context->device->CreateVertexShader(vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), nullptr, &blitter->vertex_shader);
    if (SUCCEEDED(hr)) {
        hr = context->device->CreatePixelShader(ps_blob->GetBufferPointer(), ps_blob->GetBufferSize(), nullptr, &blitter->pixel_shader);
    }
    wingui_release_blob(ps_blob_raw);
    wingui_release_blob(vs_blob_raw);
    if (FAILED(hr)) {
        setLastErrorHresult("wingui_create_rgba_blitter: shader creation failed", hr);
        wingui_destroy_rgba_blitter(blitter);
        return 0;
    }

    D3D11_BUFFER_DESC cb_desc{};
    cb_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cb_desc.ByteWidth = sizeof(WinguiRgbaBlitUniforms);
    cb_desc.Usage = D3D11_USAGE_DYNAMIC;
    cb_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = context->device->CreateBuffer(&cb_desc, nullptr, &blitter->constant_buffer);
    if (FAILED(hr)) {
        setLastErrorHresult("wingui_create_rgba_blitter: cbuffer creation failed", hr);
        wingui_destroy_rgba_blitter(blitter);
        return 0;
    }

    D3D11_SAMPLER_DESC sampler_desc{};
    sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;
    hr = context->device->CreateSamplerState(&sampler_desc, &blitter->linear_sampler);
    if (FAILED(hr)) {
        setLastErrorHresult("wingui_create_rgba_blitter: sampler creation failed", hr);
        wingui_destroy_rgba_blitter(blitter);
        return 0;
    }

    D3D11_RASTERIZER_DESC raster_desc{};
    raster_desc.FillMode = D3D11_FILL_SOLID;
    raster_desc.CullMode = D3D11_CULL_NONE;
    raster_desc.ScissorEnable = TRUE;
    raster_desc.DepthClipEnable = TRUE;
    hr = context->device->CreateRasterizerState(&raster_desc, &blitter->rasterizer);
    if (FAILED(hr)) {
        setLastErrorHresult("wingui_create_rgba_blitter: rasterizer creation failed", hr);
        wingui_destroy_rgba_blitter(blitter);
        return 0;
    }

    D3D11_BLEND_DESC blend_desc{};
    blend_desc.RenderTarget[0].BlendEnable = FALSE;
    blend_desc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    blend_desc.RenderTarget[0].DestBlend = D3D11_BLEND_ZERO;
    blend_desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blend_desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blend_desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    blend_desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blend_desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    hr = context->device->CreateBlendState(&blend_desc, &blitter->blend_opaque);
    if (FAILED(hr)) {
        setLastErrorHresult("wingui_create_rgba_blitter: opaque blend state failed", hr);
        wingui_destroy_rgba_blitter(blitter);
        return 0;
    }

    blend_desc.RenderTarget[0].BlendEnable = TRUE;
    blend_desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blend_desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blend_desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blend_desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    hr = context->device->CreateBlendState(&blend_desc, &blitter->blend_alpha_over);
    if (FAILED(hr)) {
        setLastErrorHresult("wingui_create_rgba_blitter: alpha blend state failed", hr);
        wingui_destroy_rgba_blitter(blitter);
        return 0;
    }

    *out_blitter = blitter;
    g_last_error.clear();
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_rgba_surface_shader_blit(
    WinguiRgbaBlitter* blitter,
    WinguiRgbaSurface* dst_surface,
    uint32_t dst_buffer_index,
    WinguiRectU32 dst_rect,
    WinguiRgbaSurface* src_surface,
    uint32_t src_buffer_index,
    WinguiRectU32 src_rect,
    float tint_r,
    float tint_g,
    float tint_b,
    float tint_a,
    uint32_t blend_mode) {
    if (!blitter || !blitter->context || !blitter->context->device_context ||
        !dst_surface || !src_surface || dst_surface->context != src_surface->context ||
        dst_surface->context != blitter->context) {
        setLastErrorString("wingui_rgba_surface_shader_blit: invalid arguments");
        return 0;
    }
    if (dst_buffer_index >= dst_surface->buffers.size() || src_buffer_index >= src_surface->buffers.size()) {
        setLastErrorString("wingui_rgba_surface_shader_blit: invalid buffer index");
        return 0;
    }
    if (!dst_rect.width || !dst_rect.height || !src_rect.width || !src_rect.height) {
        setLastErrorString("wingui_rgba_surface_shader_blit: invalid rect");
        return 0;
    }
    if (!dst_surface->buffer_width || !dst_surface->buffer_height ||
        !src_surface->buffer_width || !src_surface->buffer_height ||
        !dst_surface->buffers[dst_buffer_index].rtv ||
        !src_surface->buffers[src_buffer_index].srv) {
        setLastErrorString("wingui_rgba_surface_shader_blit: buffers not initialized");
        return 0;
    }
    if (dst_rect.x + dst_rect.width > dst_surface->buffer_width ||
        dst_rect.y + dst_rect.height > dst_surface->buffer_height ||
        src_rect.x + src_rect.width > src_surface->buffer_width ||
        src_rect.y + src_rect.height > src_surface->buffer_height) {
        setLastErrorString("wingui_rgba_surface_shader_blit: rect out of bounds");
        return 0;
    }

    auto* ctx = blitter->context->device_context;

    WinguiRgbaBlitUniforms uniforms{};
    const float dw = static_cast<float>(dst_surface->buffer_width);
    const float dh = static_cast<float>(dst_surface->buffer_height);
    uniforms.dst_pos_min[0] = (static_cast<float>(dst_rect.x) / dw) * 2.0f - 1.0f;
    uniforms.dst_pos_max[0] = (static_cast<float>(dst_rect.x + dst_rect.width) / dw) * 2.0f - 1.0f;
    uniforms.dst_pos_min[1] = 1.0f - (static_cast<float>(dst_rect.y) / dh) * 2.0f;
    uniforms.dst_pos_max[1] = 1.0f - (static_cast<float>(dst_rect.y + dst_rect.height) / dh) * 2.0f;
    const float sw = static_cast<float>(src_surface->buffer_width);
    const float sh = static_cast<float>(src_surface->buffer_height);
    uniforms.src_uv_min[0] = static_cast<float>(src_rect.x) / sw;
    uniforms.src_uv_max[0] = static_cast<float>(src_rect.x + src_rect.width) / sw;
    uniforms.src_uv_min[1] = static_cast<float>(src_rect.y) / sh;
    uniforms.src_uv_max[1] = static_cast<float>(src_rect.y + src_rect.height) / sh;
    uniforms.tint[0] = tint_r;
    uniforms.tint[1] = tint_g;
    uniforms.tint[2] = tint_b;
    uniforms.tint[3] = tint_a;

    D3D11_MAPPED_SUBRESOURCE mapped{};
    HRESULT hr = ctx->Map(blitter->constant_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) {
        setLastErrorHresult("wingui_rgba_surface_shader_blit: cbuffer map failed", hr);
        return 0;
    }
    std::memcpy(mapped.pData, &uniforms, sizeof(uniforms));
    ctx->Unmap(blitter->constant_buffer, 0);

    ID3D11RenderTargetView* rtv = dst_surface->buffers[dst_buffer_index].rtv;
    ID3D11ShaderResourceView* srv = src_surface->buffers[src_buffer_index].srv;
    ID3D11ShaderResourceView* null_srv = nullptr;

    const D3D11_VIEWPORT viewport{ 0.0f, 0.0f, dw, dh, 0.0f, 1.0f };
    const D3D11_RECT scissor{
        static_cast<LONG>(dst_rect.x),
        static_cast<LONG>(dst_rect.y),
        static_cast<LONG>(dst_rect.x + dst_rect.width),
        static_cast<LONG>(dst_rect.y + dst_rect.height) };

    ctx->OMSetRenderTargets(1, &rtv, nullptr);
    ID3D11BlendState* blend_state = (blend_mode == WINGUI_RGBA_BLIT_ALPHA_OVER) ? blitter->blend_alpha_over : blitter->blend_opaque;
    const FLOAT blend_factor[4] = { 0, 0, 0, 0 };
    ctx->OMSetBlendState(blend_state, blend_factor, 0xffffffffu);
    ctx->RSSetState(blitter->rasterizer);
    ctx->RSSetViewports(1, &viewport);
    ctx->RSSetScissorRects(1, &scissor);
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(blitter->vertex_shader, nullptr, 0);
    ctx->VSSetConstantBuffers(0, 1, &blitter->constant_buffer);
    ctx->PSSetShader(blitter->pixel_shader, nullptr, 0);
    ctx->PSSetConstantBuffers(0, 1, &blitter->constant_buffer);
    ctx->PSSetSamplers(0, 1, &blitter->linear_sampler);
    ctx->PSSetShaderResources(0, 1, &srv);
    ctx->Draw(6, 0);
    ctx->PSSetShaderResources(0, 1, &null_srv);
    ID3D11RenderTargetView* null_rtv = nullptr;
    ctx->OMSetRenderTargets(1, &null_rtv, nullptr);
    g_last_error.clear();
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_rgba_surface_render(
    WinguiRgbaPaneRenderer* renderer,
    WinguiRgbaSurface* surface,
    int32_t viewport_x,
    int32_t viewport_y,
    int32_t viewport_width,
    int32_t viewport_height,
    uint32_t screen_width,
    uint32_t screen_height,
    uint32_t pixel_aspect_num,
    uint32_t pixel_aspect_den,
    uint32_t buffer_index,
    WinguiIndexedPaneLayout* out_layout) {
    if (!surface) {
        setLastErrorString("wingui_rgba_surface_render: invalid arguments");
        return 0;
    }
    return renderRgbaBufferSet(
        renderer,
        surface->buffers,
        surface->buffer_width,
        surface->buffer_height,
        viewport_x,
        viewport_y,
        viewport_width,
        viewport_height,
        screen_width,
        screen_height,
        pixel_aspect_num,
        pixel_aspect_den,
        buffer_index,
        out_layout,
        "wingui_rgba_surface_render: invalid arguments",
        "wingui_rgba_surface_render: requested buffer is not ready",
        "wingui_rgba_surface_render: invalid pane layout inputs");
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_rgba_pane_ensure_buffers(
    WinguiRgbaPaneRenderer* renderer,
    uint32_t width,
    uint32_t height) {
    if (!renderer || !renderer->context) {
        setLastErrorString("wingui_rgba_pane_ensure_buffers: invalid arguments");
        return 0;
    }
    return rgbaSurfaceEnsureBuffers(
        renderer->context,
        renderer->buffers,
        renderer->buffer_width,
        renderer->buffer_height,
        width,
        height,
        "wingui_rgba_pane_ensure_buffers: texture creation failed");
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_rgba_pane_get_buffer_info(
    WinguiRgbaPaneRenderer* renderer,
    uint32_t* out_width,
    uint32_t* out_height,
    uint32_t* out_buffer_count) {
    if (!renderer) {
        setLastErrorString("wingui_rgba_pane_get_buffer_info: invalid arguments");
        return 0;
    }
    return rgbaSurfaceGetBufferInfo(
        renderer->buffers,
        renderer->buffer_width,
        renderer->buffer_height,
        out_width,
        out_height,
        out_buffer_count,
        "wingui_rgba_pane_get_buffer_info: invalid arguments");
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_rgba_pane_upload_bgra8(
    WinguiRgbaPaneRenderer* renderer,
    uint32_t buffer_index,
    const uint8_t* pixels,
    uint32_t source_pitch) {
    if (!renderer || !pixels || buffer_index >= renderer->buffers.size()) {
        setLastErrorString("wingui_rgba_pane_upload_bgra8: invalid arguments");
        return 0;
    }
    if (!renderer->buffer_width || !renderer->buffer_height || !renderer->buffers[buffer_index].texture) {
        setLastErrorString("wingui_rgba_pane_upload_bgra8: buffers are not initialized");
        return 0;
    }
    renderer->context->device_context->UpdateSubresource(
        renderer->buffers[buffer_index].texture,
        0,
        nullptr,
        pixels,
        source_pitch ? source_pitch : renderer->buffer_width * 4u,
        0);
    g_last_error.clear();
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_rgba_pane_upload_bgra8_region(
    WinguiRgbaPaneRenderer* renderer,
    uint32_t buffer_index,
    WinguiRectU32 destination_region,
    const uint8_t* pixels,
    uint32_t source_pitch) {
    if (!renderer) {
        setLastErrorString("wingui_rgba_pane_upload_bgra8_region: invalid arguments");
        return 0;
    }
    return rgbaSurfaceUploadBgraRegion(
        renderer->context,
        renderer->buffers,
        renderer->buffer_width,
        renderer->buffer_height,
        buffer_index,
        destination_region,
        pixels,
        source_pitch,
        "wingui_rgba_pane_upload_bgra8_region: invalid arguments",
        "wingui_rgba_pane_upload_bgra8_region: buffers are not initialized",
        "wingui_rgba_pane_upload_bgra8_region: region out of bounds");
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_rgba_pane_copy_region(
    WinguiRgbaPaneRenderer* renderer,
    uint32_t dst_buffer_index,
    uint32_t dst_x,
    uint32_t dst_y,
    uint32_t src_buffer_index,
    WinguiRectU32 source_region) {
    if (!renderer) {
        setLastErrorString("wingui_rgba_pane_copy_region: invalid arguments");
        return 0;
    }
    return rgbaSurfaceCopyRegion(
        renderer->context,
        renderer->buffers,
        renderer->buffer_width,
        renderer->buffer_height,
        dst_buffer_index,
        dst_x,
        dst_y,
        src_buffer_index,
        source_region,
        "wingui_rgba_pane_copy_region: invalid arguments",
        "wingui_rgba_pane_copy_region: buffers are not initialized",
        "wingui_rgba_pane_copy_region: region out of bounds");
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_rgba_pane_render(
    WinguiRgbaPaneRenderer* renderer,
    int32_t viewport_x,
    int32_t viewport_y,
    int32_t viewport_width,
    int32_t viewport_height,
    uint32_t screen_width,
    uint32_t screen_height,
    uint32_t pixel_aspect_num,
    uint32_t pixel_aspect_den,
    uint32_t buffer_index,
    WinguiIndexedPaneLayout* out_layout) {
    return renderRgbaBufferSet(
        renderer,
        renderer->buffers,
        renderer->buffer_width,
        renderer->buffer_height,
        viewport_x,
        viewport_y,
        viewport_width,
        viewport_height,
        screen_width,
        screen_height,
        pixel_aspect_num,
        pixel_aspect_den,
        buffer_index,
        out_layout,
        "wingui_rgba_pane_render: invalid arguments",
        "wingui_rgba_pane_render: requested buffer is not ready",
        "wingui_rgba_pane_render: invalid pane layout inputs");
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_rgba_pane_save_buffer_utf8(
    WinguiRgbaPaneRenderer* renderer,
    uint32_t buffer_index,
    const char* path_utf8) {
    return wingui_rgba_pane_save_buffer_resized_utf8(renderer, buffer_index, path_utf8, 0, 0);
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_rgba_pane_save_buffer_resized_utf8(
    WinguiRgbaPaneRenderer* renderer,
    uint32_t buffer_index,
    const char* path_utf8,
    uint32_t output_width,
    uint32_t output_height) {
    if (!renderer || !renderer->context || !path_utf8 || buffer_index >= renderer->buffers.size()) {
        setLastErrorString("wingui_rgba_pane_save_buffer_resized_utf8: invalid arguments");
        return 0;
    }
    if (!renderer->buffers[buffer_index].texture) {
        setLastErrorString("wingui_rgba_pane_save_buffer_resized_utf8: requested buffer is not ready");
        return 0;
    }

    std::vector<uint8_t> pixels;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t stride = 0;
    const HRESULT hr = readBackTextureBgra(
        renderer->context->device,
        renderer->context->device_context,
        renderer->buffers[buffer_index].texture,
        pixels,
        width,
        height,
        stride);
    if (FAILED(hr)) {
        setLastErrorHresult("wingui_rgba_pane_save_buffer_resized_utf8: texture readback failed", hr);
        return 0;
    }

    return wingui_save_bgra8_image_resized_utf8(
        path_utf8,
        pixels.data(),
        width,
        height,
        stride,
        output_width,
        output_height);
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_rgba_pane_load_image_into_buffer_utf8(
    WinguiRgbaPaneRenderer* renderer,
    uint32_t buffer_index,
    const char* path_utf8) {
    if (!renderer || buffer_index >= renderer->buffers.size()) {
        setLastErrorString("wingui_rgba_pane_load_image_into_buffer_utf8: invalid arguments");
        return 0;
    }

    WinguiImageData image{};
    if (!wingui_load_image_utf8(path_utf8, &image)) {
        return 0;
    }

    const int32_t ensured = wingui_rgba_pane_ensure_buffers(renderer, image.width, image.height);
    if (ensured) {
        wingui_rgba_pane_upload_bgra8(renderer, buffer_index, image.pixels, image.stride);
    }
    wingui_free_image(&image);
    return ensured;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_load_image_utf8(const char* path_utf8, WinguiImageData* out_image) {
    if (!path_utf8 || !out_image) {
        setLastErrorString("wingui_load_image_utf8: invalid arguments");
        return 0;
    }

    std::memset(out_image, 0, sizeof(*out_image));
    const std::wstring path = fullPathWide(utf8ToWide(path_utf8));
    if (path.empty()) {
        setLastErrorString("wingui_load_image_utf8: invalid UTF-8 path");
        return 0;
    }
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        setLastErrorString("wingui_load_image_utf8: source file was not found");
        return 0;
    }

    IWICImagingFactory* factory = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;

    HRESULT hr = createWicFactory(&factory);
    if (SUCCEEDED(hr)) hr = factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder);
    if (SUCCEEDED(hr)) hr = decoder->GetFrame(0, &frame);
    if (SUCCEEDED(hr)) hr = factory->CreateFormatConverter(&converter);
    if (SUCCEEDED(hr)) {
        hr = converter->Initialize(
            frame,
            GUID_WICPixelFormat32bppBGRA,
            WICBitmapDitherTypeNone,
            nullptr,
            0.0,
            WICBitmapPaletteTypeCustom);
    }

    UINT width = 0;
    UINT height = 0;
    if (SUCCEEDED(hr)) hr = converter->GetSize(&width, &height);
    if (SUCCEEDED(hr) && (width == 0 || height == 0)) hr = E_FAIL;

    const uint32_t stride = width * 4u;
    const size_t byte_count = static_cast<size_t>(stride) * height;
    uint8_t* pixels = nullptr;
    if (SUCCEEDED(hr)) {
        pixels = static_cast<uint8_t*>(std::malloc(byte_count));
        if (!pixels) hr = E_OUTOFMEMORY;
    }
    if (SUCCEEDED(hr)) {
        hr = converter->CopyPixels(nullptr, stride, static_cast<UINT>(byte_count), pixels);
    }

    safeRelease(converter);
    safeRelease(frame);
    safeRelease(decoder);
    safeRelease(factory);

    if (FAILED(hr)) {
        if (pixels) std::free(pixels);
        setLastErrorHresult("wingui_load_image_utf8: image decode failed", hr);
        return 0;
    }

    out_image->pixels = pixels;
    out_image->width = width;
    out_image->height = height;
    out_image->stride = stride;
    g_last_error.clear();
    return 1;
}

extern "C" WINGUI_API void WINGUI_CALL wingui_free_image(WinguiImageData* image) {
    if (!image) return;
    if (image->pixels) std::free(image->pixels);
    image->pixels = nullptr;
    image->width = 0;
    image->height = 0;
    image->stride = 0;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_save_bgra8_image_utf8(
    const char* path_utf8,
    const uint8_t* pixels,
    uint32_t width,
    uint32_t height,
    uint32_t stride) {
    return wingui_save_bgra8_image_resized_utf8(path_utf8, pixels, width, height, stride, 0, 0);
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_save_bgra8_image_resized_utf8(
    const char* path_utf8,
    const uint8_t* pixels,
    uint32_t width,
    uint32_t height,
    uint32_t stride,
    uint32_t output_width,
    uint32_t output_height) {
    if (!path_utf8 || !pixels || width == 0 || height == 0 || stride < width * 4u) {
        setLastErrorString("wingui_save_bgra8_image_resized_utf8: invalid arguments");
        return 0;
    }

    const std::wstring path = utf8ToWide(path_utf8);
    if (path.empty()) {
        setLastErrorString("wingui_save_bgra8_image_resized_utf8: invalid UTF-8 path");
        return 0;
    }

    const UINT target_width = output_width == 0 ? width : output_width;
    const UINT target_height = output_height == 0 ? height : output_height;
    if (target_width == 0 || target_height == 0) {
        setLastErrorString("wingui_save_bgra8_image_resized_utf8: invalid output size");
        return 0;
    }

    IWICImagingFactory* factory = nullptr;
    IWICStream* stream = nullptr;
    IWICBitmapEncoder* encoder = nullptr;
    IWICBitmapFrameEncode* frame = nullptr;
    IPropertyBag2* frame_props = nullptr;
    IWICBitmap* source_bitmap = nullptr;
    IWICBitmapScaler* scaler = nullptr;
    IWICFormatConverter* format_converter = nullptr;

    const bool use_24bpp = use24BitOutputForPath(path);
    GUID pixel_format = use_24bpp ? GUID_WICPixelFormat24bppBGR : GUID_WICPixelFormat32bppBGRA;
    const GUID& container_format = containerFormatForPath(path);

    HRESULT hr = createWicFactory(&factory);
    if (SUCCEEDED(hr)) hr = factory->CreateStream(&stream);
    if (SUCCEEDED(hr)) hr = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
    if (SUCCEEDED(hr)) hr = factory->CreateEncoder(container_format, nullptr, &encoder);
    if (SUCCEEDED(hr)) hr = encoder->Initialize(stream, WICBitmapEncoderNoCache);
    if (SUCCEEDED(hr)) hr = encoder->CreateNewFrame(&frame, &frame_props);
    if (SUCCEEDED(hr)) hr = frame->Initialize(frame_props);
    if (SUCCEEDED(hr)) hr = frame->SetSize(target_width, target_height);
    if (SUCCEEDED(hr)) hr = frame->SetPixelFormat(&pixel_format);

    if (SUCCEEDED(hr)) {
        hr = factory->CreateBitmapFromMemory(
            width,
            height,
            GUID_WICPixelFormat32bppBGRA,
            stride,
            stride * height,
            const_cast<BYTE*>(pixels),
            &source_bitmap);
    }

    IWICBitmapSource* encode_source = source_bitmap;
    if (SUCCEEDED(hr) && (target_width != width || target_height != height)) {
        hr = factory->CreateBitmapScaler(&scaler);
        if (SUCCEEDED(hr)) {
            hr = scaler->Initialize(encode_source, target_width, target_height, WICBitmapInterpolationModeFant);
        }
        if (SUCCEEDED(hr)) encode_source = scaler;
    }

    if (SUCCEEDED(hr) && use_24bpp) {
        hr = factory->CreateFormatConverter(&format_converter);
        if (SUCCEEDED(hr)) {
            hr = format_converter->Initialize(
                encode_source,
                GUID_WICPixelFormat24bppBGR,
                WICBitmapDitherTypeNone,
                nullptr,
                0.0,
                WICBitmapPaletteTypeCustom);
        }
        if (SUCCEEDED(hr)) encode_source = format_converter;
    }

    if (SUCCEEDED(hr)) hr = frame->WriteSource(encode_source, nullptr);
    if (SUCCEEDED(hr)) hr = frame->Commit();
    if (SUCCEEDED(hr)) hr = encoder->Commit();

    safeRelease(format_converter);
    safeRelease(scaler);
    safeRelease(source_bitmap);
    safeRelease(frame_props);
    safeRelease(frame);
    safeRelease(encoder);
    safeRelease(stream);
    safeRelease(factory);

    if (FAILED(hr)) {
        setLastErrorHresult("wingui_save_bgra8_image_resized_utf8: image encode failed", hr);
        return 0;
    }

    g_last_error.clear();
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_indexed_graphics_load_image_into_sprite_atlas_utf8(
    WinguiIndexedGraphicsRenderer* renderer,
    uint32_t atlas_x,
    uint32_t atlas_y,
    const char* path_utf8) {
    if (!renderer || !path_utf8) {
        setLastErrorString("wingui_indexed_graphics_load_image_into_sprite_atlas_utf8: invalid arguments");
        return 0;
    }

    WinguiImageData image{};
    if (!wingui_load_image_utf8(path_utf8, &image)) {
        return 0;
    }

    std::vector<uint8_t> indexed(static_cast<size_t>(image.width) * image.height, 0);
    for (uint32_t y = 0; y < image.height; ++y) {
        const uint8_t* src = image.pixels + static_cast<size_t>(y) * image.stride;
        uint8_t* dst = indexed.data() + static_cast<size_t>(y) * image.width;
        for (uint32_t x = 0; x < image.width; ++x) {
            const uint8_t b = src[x * 4u + 0];
            const uint8_t g = src[x * 4u + 1];
            const uint8_t r = src[x * 4u + 2];
            const uint8_t a = src[x * 4u + 3];
            dst[x] = a == 0 ? 0 : std::max({ r, g, b });
        }
    }

    const int32_t result = wingui_indexed_graphics_upload_sprite_atlas_region(
        renderer,
        atlas_x,
        atlas_y,
        image.width,
        image.height,
        indexed.data(),
        image.width);
    wingui_free_image(&image);
    return result;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_create_context(const WinguiContextDesc* desc, WinguiContext** out_context) {
    if (!desc || !out_context) {
        setLastErrorString("wingui_create_context: invalid arguments");
        return 0;
    }
    if (!desc->hwnd) {
        setLastErrorString("wingui_create_context: hwnd is required");
        return 0;
    }

    auto* context = new (std::nothrow) WinguiContext();
    if (!context) {
        setLastErrorString("wingui_create_context: allocation failed");
        return 0;
    }

    context->hwnd = static_cast<HWND>(desc->hwnd);
    context->width = std::max<uint32_t>(1, desc->width);
    context->height = std::max<uint32_t>(1, desc->height);
    context->buffer_count = std::max<uint32_t>(2, desc->buffer_count);

    DXGI_SWAP_CHAIN_DESC swap_desc{};
    swap_desc.BufferDesc.Width = context->width;
    swap_desc.BufferDesc.Height = context->height;
    swap_desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swap_desc.SampleDesc.Count = 1;
    swap_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swap_desc.BufferCount = context->buffer_count;
    swap_desc.OutputWindow = context->hwnd;
    swap_desc.Windowed = TRUE;
    swap_desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT flags = 0;
#if defined(_DEBUG)
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    const D3D_FEATURE_LEVEL requested_levels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };
    D3D_FEATURE_LEVEL actual_level = D3D_FEATURE_LEVEL_11_0;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        flags,
        requested_levels,
        static_cast<UINT>(std::size(requested_levels)),
        D3D11_SDK_VERSION,
        &swap_desc,
        &context->swap_chain,
        &context->device,
        &actual_level,
        &context->device_context);
    if (FAILED(hr)) {
        setLastErrorHresult("wingui_create_context: D3D11CreateDeviceAndSwapChain failed", hr);
        wingui_destroy_context(context);
        return 0;
    }

    hr = createRenderTargetView(*context);
    if (FAILED(hr)) {
        setLastErrorHresult("wingui_create_context: CreateRenderTargetView failed", hr);
        wingui_destroy_context(context);
        return 0;
    }

    *out_context = context;
    g_last_error.clear();
    return 1;
}

extern "C" WINGUI_API void WINGUI_CALL wingui_destroy_context(WinguiContext* context) {
    if (!context) return;
    destroyRenderTargetView(*context);
    safeRelease(context->swap_chain);
    safeRelease(context->device_context);
    safeRelease(context->device);
    delete context;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_resize_context(WinguiContext* context, uint32_t width, uint32_t height) {
    if (!context) {
        setLastErrorString("wingui_resize_context: context is null");
        return 0;
    }
    HRESULT hr = resizeSwapChain(*context, std::max<uint32_t>(1, width), std::max<uint32_t>(1, height));
    if (FAILED(hr)) {
        setLastErrorHresult("wingui_resize_context: ResizeBuffers failed", hr);
        return 0;
    }
    g_last_error.clear();
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_begin_frame(WinguiContext* context, float red, float green, float blue, float alpha) {
    if (!context || !context->device_context || !context->render_target_view) {
        setLastErrorString("wingui_begin_frame: context is not initialized");
        return 0;
    }
    const float clear[4] = { red, green, blue, alpha };
    context->device_context->OMSetRenderTargets(1, &context->render_target_view, nullptr);
    context->device_context->ClearRenderTargetView(context->render_target_view, clear);
    g_last_error.clear();
    return 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_present(WinguiContext* context, uint32_t sync_interval) {
    if (!context || !context->swap_chain) {
        setLastErrorString("wingui_present: context is not initialized");
        return 0;
    }
    HRESULT hr = context->swap_chain->Present(sync_interval, 0);
    if (FAILED(hr)) {
        setLastErrorHresult("wingui_present: Present failed", hr);
        return 0;
    }
    g_last_error.clear();
    return 1;
}

extern "C" WINGUI_API void* WINGUI_CALL wingui_d3d11_device(WinguiContext* context) { return context ? context->device : nullptr; }
extern "C" WINGUI_API void* WINGUI_CALL wingui_d3d11_context(WinguiContext* context) { return context ? context->device_context : nullptr; }
extern "C" WINGUI_API void* WINGUI_CALL wingui_dxgi_swap_chain(WinguiContext* context) { return context ? context->swap_chain : nullptr; }
extern "C" WINGUI_API void* WINGUI_CALL wingui_d3d11_render_target_view(WinguiContext* context) { return context ? context->render_target_view : nullptr; }

extern "C" WINGUI_API float WINGUI_CALL wingui_current_dpi_scale(void* hwnd_value) {
    HWND hwnd = static_cast<HWND>(hwnd_value);
    if (!hwnd) return 1.0f;
    HDC dc = GetDC(hwnd);
    if (!dc) return 1.0f;
    int dpi = GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(hwnd, dc);
    return dpi > 0 ? static_cast<float>(dpi) / 96.0f : 1.0f;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_compile_shader_from_file_utf8(
    const char* path_utf8,
    const char* entry_utf8,
    const char* target_utf8,
    void** out_blob,
    size_t* out_size) {
    if (!path_utf8 || !entry_utf8 || !target_utf8 || !out_blob || !out_size) {
        setLastErrorString("wingui_compile_shader_from_file_utf8: invalid arguments");
        return 0;
    }
    std::wstring path = utf8ToWide(path_utf8);
    if (path.empty()) {
        setLastErrorString("wingui_compile_shader_from_file_utf8: invalid UTF-8 path");
        return 0;
    }

    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    ID3DBlob* blob = nullptr;
    ID3DBlob* errors = nullptr;
    HRESULT hr = D3DCompileFromFile(
        path.c_str(),
        nullptr,
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        entry_utf8,
        target_utf8,
        flags,
        0,
        &blob,
        &errors);
    if (FAILED(hr)) {
        if (errors && errors->GetBufferPointer()) {
            setLastErrorString(static_cast<const char*>(errors->GetBufferPointer()));
        } else {
            setLastErrorHresult("wingui_compile_shader_from_file_utf8: D3DCompileFromFile failed", hr);
        }
        safeRelease(errors);
        safeRelease(blob);
        return 0;
    }

    safeRelease(errors);
    *out_blob = blob;
    *out_size = blob->GetBufferSize();
    g_last_error.clear();
    return 1;
}

extern "C" WINGUI_API void WINGUI_CALL wingui_release_blob(void* blob) {
    auto* d3d_blob = static_cast<ID3DBlob*>(blob);
    safeRelease(d3d_blob);
}