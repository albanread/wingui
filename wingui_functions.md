# Wingui Functions Documentation

This document provides an overview and reference for all the functions available in the `wingui` C API (`wingui.h`). The functions are grouped logically by functionality.

All functions use the `WINGUI_CALL` (`__cdecl`) calling convention and are exported via `WINGUI_API`. Standard integer types (`int32_t`, `uint32_t`, etc.) are used heavily. Most functions returning `int32_t` return an error code or success indicator (typically `0` for success, non-zero for failure).

## General & Error Handling

*   **`const char* wingui_last_error_utf8(void)`**
    Returns a null-terminated UTF-8 string describing the last error that occurred in the library. Useful for debugging after a function returns an error code.
*   **`uint32_t wingui_version_major(void)`**
    Returns the major version number of the Wingui library.
*   **`uint32_t wingui_version_minor(void)`**
    Returns the minor version number of the Wingui library.
*   **`uint32_t wingui_version_patch(void)`**
    Returns the patch version number of the Wingui library.

## Window Management

*   **`int32_t wingui_create_window_utf8(const WinguiWindowDesc* desc, WinguiWindow** out_window)`**
    Creates a new native Windows window using the provided description. `desc` encapsulates properties like class name, title, bounds, and the window procedure.
*   **`void wingui_destroy_window(WinguiWindow* window)`**
    Destroys the specified window and frees associated resources.
*   **`int32_t wingui_window_show(WinguiWindow* window, int32_t show_command)`**
    Sets the show state of the window (e.g., SW_SHOW, SW_HIDE).
*   **`int32_t wingui_window_close(WinguiWindow* window)`**
    Requests to close the window.
*   **`int32_t wingui_window_set_title_utf8(WinguiWindow* window, const char* title_utf8)`**
    Changes the title text of the window.
*   **`int32_t wingui_window_set_menu(WinguiWindow* window, WinguiMenu* menu)`**
    Assigns a native menu bar to the window.
*   **`int32_t wingui_window_redraw_menu_bar(WinguiWindow* window)`**
    Forces a redraw of the window's menu bar, useful after modifying an attached menu.
*   **`void* wingui_window_hwnd(WinguiWindow* window)`**
    Returns the underlying raw Win32 `HWND` handle for the window.
*   **`void wingui_window_set_user_data(WinguiWindow* window, void* user_data)`**
    Attaches a custom user data pointer to the window.
*   **`void* wingui_window_user_data(WinguiWindow* window)`**
    Retrieves the custom user data pointer previously attached to the window.
*   **`int32_t wingui_window_client_size(WinguiWindow* window, int32_t* out_width, int32_t* out_height)`**
    Gets the dimensions of the window's client drawing area.

## Menu Management

*   **`int32_t wingui_create_menu_bar(WinguiMenu** out_menu)`**
    Creates a new empty top-level menu bar.
*   **`int32_t wingui_create_popup_menu_handle(WinguiMenu** out_menu)`**
    Creates a new popup/context menu.
*   **`void wingui_destroy_menu(WinguiMenu* menu)`**
    Destroys the menu and frees resources.
*   **`int32_t wingui_menu_append_item_utf8(WinguiMenu* menu, uint32_t flags, uint32_t command_id, const char* text_utf8)`**
    Appends a standard item with a command ID to the specified menu.
*   **`int32_t wingui_menu_append_separator(WinguiMenu* menu)`**
    Appends a separator line to the menu.
*   **`int32_t wingui_menu_append_submenu_utf8(WinguiMenu* menu, WinguiMenu* submenu, uint32_t flags, const char* text_utf8)`**
    Appends a submenu to an existing menu item.
*   **`int32_t wingui_menu_remove_item(WinguiMenu* menu, uint32_t command_id)`**
    Removes a menu item by its command ID.
*   **`int32_t wingui_menu_set_item_enabled(WinguiMenu* menu, uint32_t command_id, int32_t enabled)`**
    Enables (1) or disables (0) a menu item.
*   **`int32_t wingui_menu_set_item_checked(WinguiMenu* menu, uint32_t command_id, int32_t checked)`**
    Checks (1) or unchecks (0) a menu item.
*   **`int32_t wingui_menu_set_item_label_utf8(WinguiMenu* menu, uint32_t command_id, const char* text_utf8)`**
    Changes the display text of a menu item.
*   **`void* wingui_menu_native_handle(WinguiMenu* menu)`**
    Returns the raw Win32 `HMENU` handle.

## Application Lifecycle & Message Loop

*   **`int32_t wingui_pump_message(int32_t wait_for_message, int32_t* out_exit_code)`**
    Pumps the Win32 message loop. If `wait_for_message` is non-zero, it will block until a message arrives. Returns when `WM_QUIT` occurs, outputting the exit code to `out_exit_code`.
*   **`void wingui_post_quit_message(int32_t exit_code)`**
    Signals the application to terminate by posting a `WM_QUIT` message to the queue.

## Glyph Atlas & Text Rendering

*   **`int32_t wingui_build_glyph_atlas_utf8(const WinguiGlyphAtlasDesc* desc, WinguiGlyphAtlasBitmap* out_bitmap)`**
    Generates a rasterized glyph atlas from system fonts.
*   **`void wingui_free_glyph_atlas_bitmap(WinguiGlyphAtlasBitmap* bitmap)`**
    Frees the dynamically allocated memory used by a glyph atlas bitmap.
*   **`int32_t wingui_create_text_grid_renderer(const WinguiTextGridRendererDesc* desc, WinguiTextGridRenderer** out_renderer)`**
    Creates a hardware-accelerated text grid renderer.
*   **`void wingui_destroy_text_grid_renderer(WinguiTextGridRenderer* renderer)`**
    Destroys the text grid renderer.
*   **`int32_t wingui_text_grid_renderer_set_atlas(WinguiTextGridRenderer* renderer, const WinguiGlyphAtlasBitmap* bitmap)`**
    Uploads a glyph atlas bitmap to the GPU for the text renderer to use.
*   **`int32_t wingui_text_grid_renderer_render(WinguiTextGridRenderer* renderer, int32_t viewport_x, int32_t viewport_y, int32_t viewport_width, int32_t viewport_height, const WinguiTextGridFrame* frame)`**
    Issues draw calls to render the text grid instance data into the specified viewport.

## Indexed Graphics Rendering

*   **`int32_t wingui_create_indexed_graphics_renderer(const WinguiIndexedGraphicsRendererDesc* desc, WinguiIndexedGraphicsRenderer** out_renderer)`**
    Creates a retro-style palette-indexed graphics and sprite renderer.
*   **`void wingui_destroy_indexed_graphics_renderer(WinguiIndexedGraphicsRenderer* renderer)`**
    Destroys an indexed graphics renderer.
*   **`int32_t wingui_compute_indexed_pane_layout( ... )`**
    Computes an optimal scaling and positioning layout (`WinguiIndexedPaneLayout`) for displaying retro resolutions within a modern viewport size while avoiding shimmering/stretching.
*   **`int32_t wingui_indexed_graphics_render_pane(WinguiIndexedGraphicsRenderer* renderer, ...)`**
    Renders the background layout / tile pane of an indexed graphics frame to the screen.
*   **`int32_t wingui_indexed_graphics_upload_sprite_atlas_region(...)`**
    Uploads 8-bit indexed pixel data to the sprite atlas texture on the GPU.
*   **`int32_t wingui_indexed_graphics_upload_sprite_palettes(...)`**
    Uploads up to the maximum number of 16-color palettes to the constant buffer for sprites.
*   **`int32_t wingui_indexed_graphics_render_sprites(...)`**
    Renders instances of sprites utilizing the sprite atlas and palettes.

## RGBA Pane Rendering

*   **`int32_t wingui_create_rgba_pane_renderer(const WinguiRgbaPaneRendererDesc* desc, WinguiRgbaPaneRenderer** out_renderer)`**
    Creates a full 32-bit color pane renderer, useful for standard modern image grids or pixel buffers.
*   **`void wingui_destroy_rgba_pane_renderer(WinguiRgbaPaneRenderer* renderer)`**
    Destroys an RGBA pane renderer.
*   **`int32_t wingui_rgba_pane_ensure_buffers(WinguiRgbaPaneRenderer* renderer, uint32_t width, uint32_t height)`**
    Resizes or ensures the internal hardware textures match the requested dimensions.
*   **`int32_t wingui_rgba_pane_upload_bgra8(WinguiRgbaPaneRenderer* renderer, uint32_t buffer_index, const uint8_t* pixels, uint32_t source_pitch)`**
    Uploads raw BGRA pixel data to the specified pane buffer.
*   **`int32_t wingui_rgba_pane_render(WinguiRgbaPaneRenderer* renderer, ... )`**
    Renders the pane buffer to the target viewport.
*   **`int32_t wingui_rgba_pane_save_buffer_utf8(...)`**
    Saves a pane's internal buffer to a file on disk (useful for screenshots or editing).
*   **`int32_t wingui_rgba_pane_save_buffer_resized_utf8(...)`**
    Saves a pane's internal buffer to a file on disk, automatically resizing it (High-Quality filtering).
*   **`int32_t wingui_rgba_pane_load_image_into_buffer_utf8(...)`**
    Loads an image from disk directly onto the GPU into a pane buffer.

## Image Loading & Saving Helpers

*   **`int32_t wingui_load_image_utf8(const char* path_utf8, WinguiImageData* out_image)`**
    Loads an image from disk using WIC and returns raw BGRA8 pixels.
*   **`void wingui_free_image(WinguiImageData* image)`**
    Frees an image buffer allocated by `wingui_load_image_utf8`.
*   **`int32_t wingui_save_bgra8_image_utf8(...)`**
    Saves a raw BGRA8 pixel buffer to a file (PNG natively supported via WIC).
*   **`int32_t wingui_save_bgra8_image_resized_utf8(...)`**
    Resizes and saves a raw BGRA8 pixel buffer to a file.
*   **`int32_t wingui_indexed_graphics_load_image_into_sprite_atlas_utf8(...)`**
    Helper specifically for the indexed graphics renderer which loads a recognized indexed image (e.g., BMP or GIF) straight into the GPU sprite atlas.

## Audio Engine (Synth / Playback)

*   **`int32_t wingui_audio_init(void)`**
    Initializes the Windows sound runtime (waveOut) and synthesizer engine.
*   **`void wingui_audio_shutdown(void)`**
    Stops playback, flushes audio buffers, and frees all synthesizer resources.
*   **`int32_t wingui_audio_is_initialized(void)`**
    Checks if the audio subsystem is successfully initialized.
*   **`void wingui_audio_stop_all(void)`**
    Immediately halts all currently playing sounds.
*   **`uint32_t wingui_audio_create_beep(float frequency, float duration)`**
    Generates a simple sine wave beep sound, returning a `sound_id` handle.
*   **`uint32_t wingui_audio_create_zap(float frequency, float duration)`**
    Generates a retro Zap/Sweep sound.
*   **`uint32_t wingui_audio_create_tone(float frequency, float duration, int32_t waveform)`**
    Generates a continuous tone using a specified primitive waveform (Sine, Square, Sawtooth, Triangle, Pulse).
*   **`uint32_t wingui_audio_create_note(float midi_note, float duration, int32_t waveform, float attack, float decay, float sustain, float release)`**
    Generates a complex note using an ADSR envelope.
*   **`uint32_t wingui_audio_create_noise(int32_t noise_type, float duration)`**
    Generates white/pink noise.
*   **`uint32_t wingui_audio_create_fm(float carrier, float modulator, float index, float duration)`**
    Generates a basic Frequency Modulation synthesis sound.
*   **`uint32_t wingui_audio_create_filtered_tone( ... )`** / **`wingui_audio_create_filtered_note( ... )`**
    Similar to Tone/Note functions but allows the application of low-pass, high-pass, or band-pass filters to shape the tone color over its lifecycle.
*   **`int32_t wingui_audio_play(uint32_t sound_id, float volume, float pan)`**
    Schedules a generated sound ID for playback with volume and stereo panning controls.
*   **`int32_t wingui_audio_play_simple(uint32_t sound_id)`**
    Schedules a sound for playback using default volume (1.0) and center panning (0.0).
*   **`void wingui_audio_stop_sound(uint32_t sound_id)`**
    Stops a specific sound if it is currently playing.
*   **`int32_t wingui_audio_is_sound_playing(uint32_t sound_id)`**
    Checks if a specific sound ID is actively playing back.
*   **`float wingui_audio_sound_duration(uint32_t sound_id)`**
    Gets the total duration of a loaded sound buffer in seconds.
*   **`int32_t wingui_audio_free_sound(uint32_t sound_id)`**
    Unloads and frees a generated sound ID's buffer from memory.
*   **`void wingui_audio_free_all(void)`**
    Stops all playback and drops all generated sound buffers.
*   **`void wingui_audio_set_master_volume(float volume)`**
    Sets the global mixing master volume modifier (0.0 to 1.0+).
*   **`float wingui_audio_get_master_volume(void)`**
    Retrieves the global master volume modifier.
*   **`int32_t wingui_audio_sound_exists(uint32_t sound_id)`**
    Checks if a sound ID is currently stored/valid in the bank.
*   **`uint32_t wingui_audio_sound_count(void)`**
    Returns the total number of sounds currently in memory.
*   **`uint64_t wingui_audio_sound_memory_usage(void)`**
    Returns the total bytes allocated by the generated audio pools in RAM.
*   **`float wingui_audio_note_to_frequency(int32_t midi_note)`** / **`int32_t wingui_audio_frequency_to_note(float frequency)`**
    Mathematical helpers for mapping between MIDI Notes (Middle C == 60) and Frequency in Hertz.
*   **`int32_t wingui_audio_export_wav_utf8(uint32_t sound_id, const char* path_utf8, float volume)`**
    Renders/saves a generated sound buffer directly to a .wav audio file.

## MIDI Out

*   **`int32_t wingui_midi_init(void)`**
    Initializes the Windows MIDI interface (`midiOut`).
*   **`void wingui_midi_shutdown(void)`**
    Closes the Windows MIDI interface.
*   **`int32_t wingui_midi_is_initialized(void)`**
    Determines if MIDI initialization was successful.
*   **`void wingui_midi_reset(void)`**
    Silences all MIDI channels (`midiOutReset`).
*   **`int32_t wingui_midi_short_message(uint32_t message)`**
    Sends an arbitrary 32-bit packed short-message event using standard MIDI bitwise layout.
*   **`int32_t wingui_midi_program_change(uint8_t channel, uint8_t program)`**
    Changes the active instrument (Patch) for a channel.
*   **`int32_t wingui_midi_control_change(uint8_t channel, uint8_t controller, uint8_t value)`**
    Adjusts a CC Controller parameter on a channel (e.g., Modulation Wheel, Sustain Pedal).
*   **`int32_t wingui_midi_note_on(uint8_t channel, uint8_t note, uint8_t velocity)`**
    Sends a Note-On event.
*   **`int32_t wingui_midi_note_off(uint8_t channel, uint8_t note, uint8_t velocity)`**
    Sends a Note-Off event.

## D3D11 Context Management

*   **`int32_t wingui_create_context(const WinguiContextDesc* desc, WinguiContext** out_context)`**
    Initializes a Direct3D 11 device, swapchain, and context wrapping it for rendering into a window `HWND`.
*   **`void wingui_destroy_context(WinguiContext* context)`**
    Releases all Direct3D resources.
*   **`int32_t wingui_resize_context(WinguiContext* context, uint32_t width, uint32_t height)`**
    Adjusts the Swap Chain and Render Targets when the associated Win32 window changes size.
*   **`int32_t wingui_begin_frame(WinguiContext* context, float red, float green, float blue, float alpha)`**
    Clears the back-buffer using the specified normalized color and binds it for rendering.
*   **`int32_t wingui_present(WinguiContext* context, uint32_t sync_interval)`**
    Flips the rendered back-buffer to the screen. Set `sync_interval` to 1 for V-SYNC, or 0 for immediate un-throttled presentation.

## Native DirectX Handles

These methods afford advanced users direct access to the underlying Direct3D APIs when manual rendering or resource allocation is needed.

*   **`void* wingui_d3d11_device(WinguiContext* context)`**
    Returns the `ID3D11Device*`.
*   **`void* wingui_d3d11_context(WinguiContext* context)`**
    Returns the `ID3D11DeviceContext*`.
*   **`void* wingui_dxgi_swap_chain(WinguiContext* context)`**
    Returns the `IDXGISwapChain*`.
*   **`void* wingui_d3d11_render_target_view(WinguiContext* context)`**
    Returns the `ID3D11RenderTargetView*` corresponding to the current window back buffer.

## General Diagnostics & Shaders

*   **`float wingui_current_dpi_scale(void* hwnd)`**
    Returns the DPI scaling coefficient (e.g., `1.0` for 100%, `1.5` for 150%) associated with a specific window.
*   **`int32_t wingui_compile_shader_from_file_utf8(const char* path_utf8, const char* entry_utf8, const char* target_utf8, void** out_blob, size_t* out_size)`**
    Loads and compiles an HLSL `.hlsl` / `.fx` source file dynamically via D3DCompile, returning an `ID3DBlob` handle.
*   **`void wingui_release_blob(void* blob)`**
    Releases a compiled shader blob allocated by `wingui_compile_shader_from_file_utf8`.
