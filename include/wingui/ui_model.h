#pragma once

// ui_model.h
//
// Client-side declarative UI model library for the Wingui terminal framework.
//
// This header provides a pure C++ virtual-DOM-style layer that the client
// thread uses to build, mutate, diff, and serialise native-UI specs without
// ever touching Win32 or any renderer object directly.
//
// Workflow
// --------
//  1. Build a tree of UiNode objects using the builder helpers.
//  2. Call UiModel::render(render_fn) to register a render callback and push
//     the first full publish (equivalent to Scheme's user-app-start!).
//  3. When application state changes, mutate state objects and call
//     UiModel::rerender().  The library diffs against the last published tree
//     and calls its delegate with either a full publish or a patch document.
//  4. The host-side terminal framework receives the JSON over the command
//     queue and reconciles the Win32 control tree.
//
// The library has no dependencies on Win32, D3D, or native_ui.h.  It relies
// only on nlohmann/json.hpp which is already required by the host.

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "nlohmann/json.hpp"

namespace wingui {

using Json = nlohmann::ordered_json;

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------

class UiNode;
class UiWindow;

// ---------------------------------------------------------------------------
// UiValue – a leaf property value accepted by all builder helpers
// ---------------------------------------------------------------------------

using UiValue = std::variant<std::monostate, bool, int64_t, double, std::string>;

// Convenience conversions so callers can pass plain literals.
inline UiValue uv(bool v)               { return v; }
inline UiValue uv(int v)                { return static_cast<int64_t>(v); }
inline UiValue uv(int64_t v)            { return v; }
inline UiValue uv(double v)             { return v; }
inline UiValue uv(const char* v)        { return std::string(v); }
inline UiValue uv(std::string v)        { return v; }

// ---------------------------------------------------------------------------
// UiNode – a node in the declarative UI tree
// ---------------------------------------------------------------------------

class UiNode {
public:
    // ----- construction -----------------------------------------------------

    // Creates a node with the given type string.
    explicit UiNode(std::string type);

    // Node type is immutable after construction.
    const std::string& type() const { return type_; }

    // ----- identity ---------------------------------------------------------

    UiNode& id(std::string id);
    const std::string& id() const { return id_; }

    // ----- scalar props -----------------------------------------------------

    UiNode& prop(const std::string& key, UiValue value);
    UiNode& prop(const std::string& key, Json value);

    // Common named-prop shorthands.
    UiNode& text(std::string v)      { return prop("text",  uv(std::move(v))); }
    UiNode& label(std::string v)     { return prop("label", uv(std::move(v))); }
    UiNode& value(std::string v)     { return prop("value", uv(std::move(v))); }
    UiNode& value(int64_t v)         { return prop("value", uv(v)); }
    UiNode& value(double v)          { return prop("value", uv(v)); }
    UiNode& event(std::string name)  { return prop("event", uv(std::move(name))); }
    UiNode& checked(bool v)          { return prop("checked", uv(v)); }
    UiNode& enabled(bool v)          { return prop("enabled", uv(v)); }
    UiNode& hidden(bool v)           { return prop("hidden", uv(v)); }
    UiNode& focused(bool v)          { return prop("focused", uv(v)); }
    UiNode& disabled(bool v)         { return prop("disabled", uv(v)); }
    UiNode& collapsed(bool v)        { return prop("collapsed", uv(v)); }
    UiNode& src(std::string v)       { return prop("src",   uv(std::move(v))); }
    UiNode& alt(std::string v)       { return prop("alt",   uv(std::move(v))); }
    UiNode& title(std::string v)     { return prop("title", uv(std::move(v))); }
    UiNode& href(std::string v)      { return prop("href", uv(std::move(v))); }
    UiNode& fit(std::string v)       { return prop("fit", uv(std::move(v))); }
    UiNode& orientation(std::string v){ return prop("orientation", uv(std::move(v))); }
    UiNode& width(int64_t v)         { return prop("width",  uv(v)); }
    UiNode& height(int64_t v)        { return prop("height", uv(v)); }
    UiNode& padding(int64_t v)       { return prop("padding", uv(v)); }
    UiNode& gap(int64_t v)           { return prop("gap", uv(v)); }
    UiNode& divider_size(int64_t v)  { return prop("dividerSize", uv(v)); }
    UiNode& live_resize(bool v)      { return prop("liveResize", uv(v)); }
    UiNode& size(double v)           { return prop("size", uv(v)); }
    UiNode& rows(int64_t v)          { return prop("rows", uv(v)); }
    UiNode& columns(int64_t v)       { return prop("columns", uv(v)); }
    UiNode& step(double v)           { return prop("step", uv(v)); }
    UiNode& min_height(int64_t v)    { return prop("minHeight", uv(v)); }
    UiNode& min_size(int64_t v)      { return prop("minSize", uv(v)); }
    UiNode& max_size(int64_t v)      { return prop("maxSize", uv(v)); }
    UiNode& selected_id(std::string v){ return prop("selectedId", uv(std::move(v))); }
    UiNode& toggle_event(std::string v){ return prop("toggleEvent", uv(std::move(v))); }
    UiNode& activate_event(std::string v){ return prop("activateEvent", uv(std::move(v))); }
    UiNode& show_buttons(bool v)     { return prop("showButtons", uv(v)); }
    UiNode& show_root_lines(bool v)  { return prop("showRootLines", uv(v)); }
    UiNode& min_val(double v)        { return prop("min", uv(v)); }
    UiNode& max_val(double v)        { return prop("max", uv(v)); }

    // Options list (select, list-box, radio-group).
    UiNode& options(Json options_array);

    // Table columns/rows.
    UiNode& columns(Json columns_array);
    UiNode& rows(Json rows_array);

    // Tree-view items.
    UiNode& items(Json items_array);
    UiNode& expanded_ids(Json expanded_ids_array);

    // Tabs list for a tabs node.
    UiNode& tabs(Json tabs_array);

    // Canvas draw commands.
    UiNode& commands(Json commands_array);

    // ----- children ---------------------------------------------------------

    UiNode& child(UiNode child_node);
    UiNode& children(std::vector<UiNode> child_nodes);
    UiNode& body(UiNode body_node);

    // ----- serialisation ----------------------------------------------------

    // Returns the normalised JSON representation of this node.
    // Auto-assigns IDs of the form "__auto__:path" for nodes that have a type
    // but no explicit id, matching the Scheme user-app-normalized-tree logic.
    Json to_json(const std::string& path = "window") const;

    // ----- internal access (used by diff engine) ----------------------------

    const Json& props() const { return props_; }
    const std::vector<UiNode>& child_list() const { return children_; }
    const UiNode* body_node() const { return body_.empty() ? nullptr : &body_.front(); }

private:
    std::string type_;
    std::string id_;
    Json props_ = Json::object();
    std::vector<UiNode> children_;
    std::vector<UiNode> body_;
};

// ---------------------------------------------------------------------------
// UiWindow – top-level window spec (wraps a body UiNode)
// ---------------------------------------------------------------------------

class UiWindow {
public:
    explicit UiWindow(std::string title, UiNode body_node);

    UiWindow& title(std::string t)   { title_ = std::move(t); return *this; }
    UiWindow& prop(const std::string& key, UiValue value);
    UiWindow& focused_pane_id(std::string id) { return prop("focusedPaneId", uv(std::move(id))); }
    UiWindow& menu_bar(Json menu_json);

    Json to_json() const;

    const std::string& title_str() const { return title_; }
    const UiNode& body() const { return body_; }

private:
    std::string title_;
    UiNode body_;
    Json extra_props_ = Json::object();
};

// ---------------------------------------------------------------------------
// Patch ops produced by the diff engine
// ---------------------------------------------------------------------------

struct UiPatchOp {
    enum class Kind {
        SetWindowProps,
        SetNodeProps,
        RemoveChild,
        MoveChild,
        AppendChild,
        InsertChild,
        ReplaceChildren,
    };

    Kind kind = Kind::SetNodeProps;
    std::string id;
    std::string child_id;
    int32_t index = -1;
    Json props;         // set-*-props payload
    Json child_node;    // append/insert-child payload
    Json children;      // replace-children payload
};

// Produces the patch document JSON accepted by wingui_native_patch_json.
Json ui_patch_ops_to_json(const std::vector<UiPatchOp>& ops);

// ---------------------------------------------------------------------------
// Diff engine
// ---------------------------------------------------------------------------

// Computes the minimal patch ops between old_spec and new_spec.
// Returns nullopt when a structural change is beyond the reconciler's scope
// (full republish is required).  Returns an empty vector when there are no
// changes.
std::optional<std::vector<UiPatchOp>> ui_model_diff(
    const Json& old_spec,
    const Json& new_spec);

// ---------------------------------------------------------------------------
// UiState – lightweight reactive key/value store (mirrors Scheme user-app-state)
// ---------------------------------------------------------------------------

class UiState {
public:
    // Get a value; throws std::out_of_range if key absent and no default.
    const Json& get(const std::string& key) const;
    Json get(const std::string& key, Json default_value) const;

    // Set or insert.
    void set(const std::string& key, Json value);

    // Read-modify-write helper.
    void update(const std::string& key, const std::function<Json(Json)>& fn);
    void update(const std::string& key, const std::function<Json(Json)>& fn, Json default_value);

    // Bulk set from an initialiser list.
    void merge(std::initializer_list<std::pair<std::string, Json>> entries);

    // Iterate over all entries.
    const std::vector<std::pair<std::string, Json>>& entries() const { return entries_; }

private:
    std::vector<std::pair<std::string, Json>> entries_;
    std::vector<std::pair<std::string, Json>>::iterator find(const std::string& key);
    std::vector<std::pair<std::string, Json>>::const_iterator find(const std::string& key) const;
};

// ---------------------------------------------------------------------------
// UiModel – the reactive model / reconciler
//
// The client thread holds one of these.  It stores the render callback and
// the last serialised tree so it can emit targeted patches instead of always
// sending a full publish.
// ---------------------------------------------------------------------------

// Delegate supplied by the terminal framework glue code.
// Called by UiModel on the client thread; the delegate is responsible for
// enqueuing the payload as a WINGUI_TERMINAL_CMD_NATIVE_UI_PUBLISH or
// WINGUI_TERMINAL_CMD_NATIVE_UI_PATCH command.
using UiModelPublishFn = std::function<bool(const std::string& json_utf8)>;
using UiModelPatchFn   = std::function<bool(const std::string& json_utf8)>;

class UiModel {
public:
    UiModel() = default;

    enum class ReconcileMode {
        None,
        FullPublishInitial,
        FullPublishDiffFallback,
        FullPublishNoPatchTransport,
        Patch,
        NoChange,
        PublishFailed,
        PatchFailed,
    };

    // Attach transport delegates.  Must be called before render().
    void set_publish_fn(UiModelPublishFn fn) { publish_fn_ = std::move(fn); }
    void set_patch_fn(UiModelPatchFn fn)     { patch_fn_   = std::move(fn); }

    // Register a render callback and immediately trigger an initial full
    // publish.  Equivalent to Scheme user-app-native-start!.
    bool render(std::function<UiWindow()> render_fn);

    // Trigger a reconcile.  If called during event processing
    // (event_depth > 0), defers to the next rerender() at depth 0.
    // Equivalent to Scheme user-app-native-rerender!.
    bool rerender();

    // Publish a static UiWindow immediately, bypassing the reconciler cache.
    // Equivalent to Scheme user-app-native-show!.
    bool show(const UiWindow& window);

    // Begin event scope (called by the framework before dispatching a native
    // UI event to the client handler).
    void push_event_depth()  { ++event_depth_; }

    // End event scope and flush any deferred rerender.
    void pop_event_depth();

    // True when a rerender has been deferred.
    bool rerender_pending() const { return rerender_pending_; }

    // Forget the cached spec (forces next rerender to be a full publish).
    void reset_cache() { last_spec_.reset(); }

    ReconcileMode last_reconcile_mode() const { return last_reconcile_mode_; }
    uint64_t full_publish_count() const { return full_publish_count_; }
    uint64_t patch_send_count() const { return patch_send_count_; }
    uint64_t diff_fallback_count() const { return diff_fallback_count_; }
    uint64_t no_change_count() const { return no_change_count_; }
    uint64_t publish_failure_count() const { return publish_failure_count_; }
    uint64_t patch_failure_count() const { return patch_failure_count_; }
    size_t last_patch_op_count() const { return last_patch_op_count_; }

private:
    std::function<UiWindow()> render_fn_;
    UiModelPublishFn publish_fn_;
    UiModelPatchFn   patch_fn_;
    std::optional<Json> last_spec_;
    int event_depth_     = 0;
    bool rerender_pending_ = false;
    ReconcileMode last_reconcile_mode_ = ReconcileMode::None;
    uint64_t full_publish_count_ = 0;
    uint64_t patch_send_count_ = 0;
    uint64_t diff_fallback_count_ = 0;
    uint64_t no_change_count_ = 0;
    uint64_t publish_failure_count_ = 0;
    uint64_t patch_failure_count_ = 0;
    size_t last_patch_op_count_ = 0;

    bool run_reconcile();
};

// ---------------------------------------------------------------------------
// Builder free functions (mirror every Scheme user-app-* constructor)
// ---------------------------------------------------------------------------

// Layouts
UiNode ui_stack(std::vector<UiNode> children = {});
UiNode ui_row(std::vector<UiNode> children = {});
UiNode ui_toolbar(std::vector<UiNode> children = {});
UiNode ui_card(std::string title, std::vector<UiNode> children = {});
UiNode ui_scroll_view(std::vector<UiNode> children = {});
UiNode ui_grid(int64_t columns, std::vector<UiNode> children = {});
UiNode ui_form(std::vector<UiNode> children = {});
UiNode ui_divider();

// Splits
UiNode ui_split_pane(std::string id, std::vector<UiNode> children = {});
UiNode ui_split_pane(std::string id, bool focused, std::vector<UiNode> children);
UiNode ui_split_pane(std::string id,
                     double size,
                     int64_t min_size,
                     int64_t max_size,
                     bool collapsed,
                     bool focused,
                     std::vector<UiNode> children = {});
UiNode ui_split_view(std::string orientation, std::string event_name,
                     UiNode first_pane, UiNode second_pane);
UiNode ui_split_view(std::string orientation,
                     std::string event_name,
                     UiNode first_pane,
                     UiNode second_pane,
                     int64_t width,
                     int64_t height,
                     int64_t divider_size,
                     bool live_resize,
                     bool disabled = false);

// Text / media
UiNode ui_text(std::string text);
UiNode ui_heading(std::string text);
UiNode ui_link(std::string text, std::string href, std::string event_name);
UiNode ui_image(std::string src, std::string alt = {});
UiNode ui_badge(std::string text);

// Inputs
UiNode ui_button(std::string text, std::string event_name);
UiNode ui_input(std::string label, std::string value, std::string event_name);
UiNode ui_number_input(std::string label, double value, std::string event_name);
UiNode ui_date_picker(std::string label, std::string value, std::string event_name);
UiNode ui_time_picker(std::string label, std::string value, std::string event_name);
UiNode ui_textarea(std::string label, std::string value, std::string event_name);
UiNode ui_rich_text(std::string label, std::string value, std::string event_name);
UiNode ui_checkbox(std::string label, bool checked, std::string event_name);
UiNode ui_switch_toggle(std::string label, bool checked, std::string event_name);
UiNode ui_slider(std::string label, double value, std::string event_name);
UiNode ui_progress(std::string label, double value);
UiNode ui_canvas(int64_t width, int64_t height, Json commands = Json::array());

// Custom D3D surfaces
UiNode ui_text_grid(int64_t columns, int64_t rows, std::string event_name = "");
UiNode ui_text_grid(std::string id, int64_t columns, int64_t rows, std::string event_name = "", bool focused = false);
UiNode ui_indexed_graphics(int64_t width, int64_t height, std::string event_name = "");
UiNode ui_indexed_graphics(std::string id, int64_t width, int64_t height, std::string event_name = "", bool focused = false);
UiNode ui_rgba_pane(int64_t width, int64_t height, std::string event_name = "");
UiNode ui_rgba_pane(std::string id, int64_t width, int64_t height, std::string event_name = "", bool focused = false);

// Collections
UiNode ui_select(std::string label, std::string value,
                 std::string event_name, Json options);
UiNode ui_list_box(std::string label, std::string value,
                   std::string event_name, Json options);
UiNode ui_radio_group(std::string label, std::string value,
                      std::string event_name, Json options);
UiNode ui_table(std::string label, Json columns, Json rows,
                std::string event_name);
UiNode ui_tree_view(std::string label, Json items, std::string event_name);
UiNode ui_tree_view(std::string label,
                    Json items,
                    std::string selected_id,
                    Json expanded_ids,
                    std::string event_name,
                    std::string toggle_event = "tree-toggle",
                    std::string activate_event = "tree-activate");
UiNode ui_tabs(std::string label, std::string value,
               std::string event_name, Json tabs);
UiNode ui_tabs(std::string label,
               std::string value,
               std::string event_name,
               Json tabs,
               int64_t width,
               int64_t height = 0);
UiNode ui_context_menu(Json items, UiNode content);

// Data helpers (produce plain Json objects, not UiNodes)
Json ui_option(std::string value, std::string text);
Json ui_column(std::string key, std::string title);
Json ui_table_row(std::string id, Json props);
Json ui_tree_item(std::string id, std::string text, Json children = Json::array());
Json ui_tree_item(std::string id, std::string text, Json children, Json tag);
Json ui_tab(std::string value, std::string text, const UiNode& content);

// Menu helpers (produce plain Json objects for the window menu-bar prop)
Json ui_menu_item(std::string id, std::string text);
Json ui_menu_item_checked(std::string id, std::string text, bool checked);
Json ui_menu_separator();
Json ui_menu_submenu(std::string text, Json items);
Json ui_menu(std::string text, Json items);
Json ui_menu_bar(Json menus);

// Window
UiWindow ui_window(std::string title, UiNode body);

// ---------------------------------------------------------------------------
// Event parsing helpers
// ---------------------------------------------------------------------------

// Extracts a named field from an event JSON object (parsed from the
// WINGUI_NATIVE_EVENT_DISPATCH_JSON payload).
Json ui_event_ref(const Json& event, const std::string& key);
std::string ui_event_name(const Json& event);

// Parses a raw UTF-8 event payload string into a Json object.
Json ui_parse_event(const std::string& payload_utf8);

} // namespace wingui
