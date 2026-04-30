// ui_model.cpp
//
// Implementation of the client-side declarative UI model library.
// See include/wingui/ui_model.h for the full API documentation.

#include "wingui/ui_model.h"

#include <algorithm>
#include <cassert>
#include <stdexcept>

namespace wingui {

// ===========================================================================
// Internal helpers
// ===========================================================================

namespace {

// Convert a UiValue variant to a Json scalar.
Json ui_value_to_json(const UiValue& v) {
    return std::visit([](const auto& val) -> Json {
        using T = std::decay_t<decltype(val)>;
        if constexpr (std::is_same_v<T, std::monostate>) return Json(nullptr);
        else return Json(val);
    }, v);
}

// Produces the auto ID string that mirrors Scheme's "__auto__:path" logic.
std::string auto_id(const std::string& path) {
    return "__auto__:" + path;
}

// Returns true if value represents one of the supported scalar types.
bool is_scalar(const Json& v) {
    return v.is_string() || v.is_number() || v.is_boolean() || v.is_null();
}

// These are the structured-array props that the diff engine can patch without
// a full republish (mirrors Scheme user-app-structured-prop-patchable?).
bool is_structured_patchable(const std::string& node_type,
                              const std::string& key) {
    if ((node_type == "select" || node_type == "list-box" || node_type == "radio-group") && key == "options") {
        return true;
    }
    if (node_type == "tabs" && key == "tabs") return true;
    if (node_type == "table" && key == "rows") return true;
    if (node_type == "table" && key == "columns") return true;
    if (node_type == "tree-view" && (key == "items" || key == "expandedIds")) return true;
    if (node_type == "context-menu" && key == "items") return true;
    if (node_type == "canvas" && key == "commands") return true;
    return false;
}

// Returns whether key is a structural prop handled separately by the diff
// engine and should therefore not appear in set-node-props patches.
bool is_structural_key(const std::string& key) {
    return key == "type" || key == "id" || key == "children" || key == "body";
}

// Collect the union of keys from two Json objects, preserving insertion order.
std::vector<std::string> union_keys(const Json& a, const Json& b) {
    std::vector<std::string> result;
    std::vector<std::string> seen;
    auto push = [&](const std::string& k) {
        if (std::find(seen.begin(), seen.end(), k) == seen.end()) {
            seen.push_back(k);
            result.push_back(k);
        }
    };
    for (const auto& [k, _] : a.items()) push(k);
    for (const auto& [k, _] : b.items()) push(k);
    return result;
}

// ---------------------------------------------------------------------------
// Forward declarations for recursive diff helpers
// ---------------------------------------------------------------------------

std::optional<std::vector<UiPatchOp>>
diff_node(const Json& old_node, const Json& new_node);

std::optional<std::vector<UiPatchOp>>
diff_children(const std::string& parent_id,
              const Json& old_children,
              const Json& new_children);

// ---------------------------------------------------------------------------
// Node-prop diff
//
// Returns the changed props as a Json object, or nullopt if any prop changed
// in a way we cannot patch (e.g. structured prop with incompatible shapes).
// Returns an empty Json object if there are no prop changes.
// ---------------------------------------------------------------------------

std::optional<Json> diff_node_props(const Json& old_node, const Json& new_node) {
    const std::string node_type = new_node.contains("type") ?
        new_node["type"].get<std::string>() : "";

    Json changed = Json::object();
    for (const auto& key : union_keys(old_node, new_node)) {
        if (is_structural_key(key)) continue;

        const Json old_val = old_node.contains(key) ? old_node[key] : Json(nullptr);
        const Json new_val = new_node.contains(key) ? new_node[key] : Json(nullptr);

        if (old_val == new_val) continue;

        // Scalar-to-scalar change.
        if (is_scalar(old_val) && is_scalar(new_val)) {
            changed[key] = new_val;
            continue;
        }
        // Structured-array prop that we know how to patch (replace whole array).
        if (is_structured_patchable(node_type, key) &&
            old_val.is_array() && new_val.is_array()) {
            changed[key] = new_val;
            continue;
        }
        // Shape changed in a way we cannot patch incrementally.
        return std::nullopt;
    }
    return changed;
}

// ---------------------------------------------------------------------------
// Children helpers (mirrors Scheme children-diff logic)
// ---------------------------------------------------------------------------

bool children_all_identifiable(const Json& children) {
    if (!children.is_array()) return false;
    for (const auto& child : children) {
        if (!child.is_object()) return false;
        if (!child.contains("id")) return false;
        const auto& id = child["id"];
        if (!id.is_string() || id.get<std::string>().empty()) return false;
    }
    return true;
}

std::vector<std::string> child_id_list(const Json& children) {
    std::vector<std::string> ids;
    for (const auto& child : children) {
        ids.push_back(child.contains("id") ? child["id"].get<std::string>() : "");
    }
    return ids;
}

std::optional<Json> find_child_by_id(const Json& children, const std::string& id) {
    for (const auto& child : children) {
        if (child.contains("id") && child["id"].get<std::string>() == id) {
            return child;
        }
    }
    return std::nullopt;
}

// Filters a vector keeping only elements satisfying predicate.
std::vector<std::string> filter_ids(const std::vector<std::string>& ids,
                                    const std::function<bool(const std::string&)>& pred) {
    std::vector<std::string> out;
    for (const auto& id : ids) { if (pred(id)) out.push_back(id); }
    return out;
}

std::optional<std::vector<UiPatchOp>>
diff_children(const std::string& parent_id,
              const Json& old_children,
              const Json& new_children) {
    if (!old_children.is_array() || !new_children.is_array()) {
        // Both absent or identical non-array – caller handles this.
        return std::vector<UiPatchOp>{};
    }

    // Fast path: same id list and count – recurse into each child.
    const auto old_ids = child_id_list(old_children);
    const auto new_ids = child_id_list(new_children);

    if (old_ids == new_ids) {
        std::vector<UiPatchOp> ops;
        for (size_t i = 0; i < old_children.size(); ++i) {
            auto child_ops = diff_node(old_children[i], new_children[i]);
            if (!child_ops) return std::nullopt;
            for (auto& op : *child_ops) ops.push_back(std::move(op));
        }
        return ops;
    }

    // Structural mutation possible only when parent has an ID and all
    // children are individually identifiable.
    if (parent_id.empty() ||
        !children_all_identifiable(old_children) ||
        !children_all_identifiable(new_children)) {
        return std::nullopt;
    }

    // Determine shared IDs (present in both old and new).
    const auto shared_in_new = filter_ids(new_ids, [&](const std::string& id) {
        return std::find(old_ids.begin(), old_ids.end(), id) != old_ids.end();
    });
    const auto shared_in_old = filter_ids(old_ids, [&](const std::string& id) {
        return std::find(new_ids.begin(), new_ids.end(), id) != new_ids.end();
    });

    std::vector<UiPatchOp> ops;

    // Recurse into shared children first.  Fall back to replace-children if
    // any sub-diff fails.
    std::vector<UiPatchOp> shared_ops;
    for (const auto& id : shared_in_new) {
        auto old_child = find_child_by_id(old_children, id);
        auto new_child = find_child_by_id(new_children, id);
        if (!old_child || !new_child) continue;
        auto child_ops = diff_node(*old_child, *new_child);
        if (!child_ops) {
            // Fall back to a single replace-children op covering all children.
            UiPatchOp replace;
            replace.kind     = UiPatchOp::Kind::ReplaceChildren;
            replace.id       = parent_id;
            replace.children = new_children;
            return std::vector<UiPatchOp>{replace};
        }
        for (auto& op : *child_ops) shared_ops.push_back(std::move(op));
    }

    // Remove ops for children that disappeared.
    for (const auto& id : old_ids) {
        if (std::find(new_ids.begin(), new_ids.end(), id) == new_ids.end()) {
            UiPatchOp op;
            op.kind     = UiPatchOp::Kind::RemoveChild;
            op.id       = parent_id;
            op.child_id = id;
            ops.push_back(std::move(op));
        }
    }

    // Reorder / insert ops, simulating the Scheme reorder-ops logic.
    std::vector<std::string> current_order = shared_in_old;
    for (int32_t index = 0; index < static_cast<int32_t>(new_children.size()); ++index) {
        const auto& child      = new_children[index];
        const std::string& cid = child["id"].get<std::string>();
        const bool was_present = std::find(old_ids.begin(), old_ids.end(), cid) != old_ids.end();

        if (was_present) {
            // Move if not already in the right relative position.
            auto it = std::find(current_order.begin(), current_order.end(), cid);
            const int32_t cur_pos = static_cast<int32_t>(it - current_order.begin());
            if (cur_pos != index) {
                UiPatchOp op;
                op.kind     = UiPatchOp::Kind::MoveChild;
                op.id       = parent_id;
                op.child_id = cid;
                op.index    = index;
                ops.push_back(std::move(op));
                current_order.erase(it);
                current_order.insert(current_order.begin() + index, cid);
            }
        } else {
            // New child – append or insert.
            const bool append = (index >= static_cast<int32_t>(current_order.size()));
            UiPatchOp op;
            op.kind       = append ? UiPatchOp::Kind::AppendChild : UiPatchOp::Kind::InsertChild;
            op.id         = parent_id;
            op.index      = index;
            op.child_node = child;
            ops.push_back(std::move(op));
            current_order.insert(current_order.begin() + index, cid);
        }
    }

    // Append shared child sub-ops after structural ops.
    for (auto& op : shared_ops) ops.push_back(std::move(op));
    return ops;
}

// ---------------------------------------------------------------------------
// Node diff
// ---------------------------------------------------------------------------

std::optional<std::vector<UiPatchOp>>
diff_node(const Json& old_node, const Json& new_node) {
    if (!old_node.is_object() || !new_node.is_object()) return std::nullopt;

    // Type and id must match for incremental diff.
    auto get_str = [](const Json& n, const char* k) -> std::string {
        return n.contains(k) ? n[k].get<std::string>() : "";
    };
    if (get_str(old_node, "type") != get_str(new_node, "type")) return std::nullopt;
    if (get_str(old_node, "id")   != get_str(new_node, "id"))   return std::nullopt;

    auto prop_changes = diff_node_props(old_node, new_node);
    if (!prop_changes) return std::nullopt;

    std::vector<UiPatchOp> ops;

    // Emit a set-node-props op if any scalar/structured props changed.
    if (!prop_changes->empty()) {
        UiPatchOp op;
        op.kind  = UiPatchOp::Kind::SetNodeProps;
        op.id    = get_str(new_node, "id");
        op.props = std::move(*prop_changes);
        ops.push_back(std::move(op));
    }

    // Recurse into body.
    const bool has_old_body = old_node.contains("body") && old_node["body"].is_object();
    const bool has_new_body = new_node.contains("body") && new_node["body"].is_object();
    if (has_old_body && has_new_body) {
        auto body_ops = diff_node(old_node["body"], new_node["body"]);
        if (!body_ops) return std::nullopt;
        for (auto& op : *body_ops) ops.push_back(std::move(op));
    } else if (has_old_body != has_new_body) {
        return std::nullopt;
    }

    // Recurse into children.
    const Json empty_arr = Json::array();
    const Json& old_ch = old_node.contains("children") ? old_node["children"] : empty_arr;
    const Json& new_ch = new_node.contains("children") ? new_node["children"] : empty_arr;
    if (old_ch == new_ch) {
        // Nothing to do.
    } else {
        auto ch_ops = diff_children(get_str(new_node, "id"), old_ch, new_ch);
        if (!ch_ops) return std::nullopt;
        for (auto& op : *ch_ops) ops.push_back(std::move(op));
    }

    return ops;
}

} // anonymous namespace

// ===========================================================================
// UiNode
// ===========================================================================

UiNode::UiNode(std::string type) : type_(std::move(type)) {}

UiNode& UiNode::id(std::string id_str) {
    id_ = std::move(id_str);
    return *this;
}

UiNode& UiNode::prop(const std::string& key, UiValue value) {
    props_[key] = ui_value_to_json(value);
    return *this;
}

UiNode& UiNode::prop(const std::string& key, Json value) {
    props_[key] = std::move(value);
    return *this;
}

UiNode& UiNode::options(Json options_array) {
    return prop("options", std::move(options_array));
}

UiNode& UiNode::columns(Json columns_array) {
    return prop("columns", std::move(columns_array));
}

UiNode& UiNode::rows(Json rows_array) {
    return prop("rows", std::move(rows_array));
}

UiNode& UiNode::items(Json items_array) {
    return prop("items", std::move(items_array));
}

UiNode& UiNode::expanded_ids(Json expanded_ids_array) {
    return prop("expandedIds", std::move(expanded_ids_array));
}

UiNode& UiNode::tabs(Json tabs_array) {
    return prop("tabs", std::move(tabs_array));
}

UiNode& UiNode::commands(Json commands_array) {
    return prop("commands", std::move(commands_array));
}

UiNode& UiNode::child(UiNode child_node) {
    children_.push_back(std::move(child_node));
    return *this;
}

UiNode& UiNode::children(std::vector<UiNode> child_nodes) {
    children_ = std::move(child_nodes);
    return *this;
}

UiNode& UiNode::body(UiNode body_node) {
    body_.clear();
    body_.push_back(std::move(body_node));
    return *this;
}

Json UiNode::to_json(const std::string& path) const {
    Json obj = Json::object();
    obj["type"] = type_;

    // Assign ID: explicit first, then auto-generated from path.
    if (!id_.empty()) {
        obj["id"] = id_;
    } else {
        obj["id"] = auto_id(path);
    }

    // Scalar and structured props.
    for (const auto& [k, v] : props_.items()) {
        obj[k] = v;
    }

    // Body node.
    if (!body_.empty()) {
        obj["body"] = body_.front().to_json(path + "/body");
    }

    // Children.
    if (!children_.empty()) {
        Json children_json = Json::array();
        for (size_t i = 0; i < children_.size(); ++i) {
            const std::string child_path = path + "/children/" + std::to_string(i);
            children_json.push_back(children_[i].to_json(child_path));
        }
        obj["children"] = std::move(children_json);
    }

    return obj;
}

// ===========================================================================
// UiWindow
// ===========================================================================

UiWindow::UiWindow(std::string title, UiNode body_node)
    : title_(std::move(title)), body_(std::move(body_node)) {}

UiWindow& UiWindow::prop(const std::string& key, UiValue value) {
    extra_props_[key] = ui_value_to_json(value);
    return *this;
}

UiWindow& UiWindow::menu_bar(Json menu_json) {
    extra_props_["menuBar"] = std::move(menu_json);
    return *this;
}

Json UiWindow::to_json() const {
    Json obj = Json::object();
    obj["type"]  = "window";
    obj["title"] = title_;
    for (const auto& [k, v] : extra_props_.items()) {
        obj[k] = v;
    }
    obj["body"] = body_.to_json("window/body");
    return obj;
}

// ===========================================================================
// Patch document serialisation
// ===========================================================================

Json ui_patch_ops_to_json(const std::vector<UiPatchOp>& ops) {
    Json ops_array = Json::array();
    for (const auto& op : ops) {
        Json entry = Json::object();
        switch (op.kind) {
            case UiPatchOp::Kind::SetWindowProps:
                entry["op"]    = "set-window-props";
                entry["props"] = op.props;
                break;
            case UiPatchOp::Kind::SetNodeProps:
                entry["op"]    = "set-node-props";
                entry["id"]    = op.id;
                entry["props"] = op.props;
                break;
            case UiPatchOp::Kind::RemoveChild:
                entry["op"]      = "remove-child";
                entry["id"]      = op.id;
                entry["childId"] = op.child_id;
                break;
            case UiPatchOp::Kind::MoveChild:
                entry["op"]      = "move-child";
                entry["id"]      = op.id;
                entry["childId"] = op.child_id;
                entry["index"]   = op.index;
                break;
            case UiPatchOp::Kind::AppendChild:
                entry["op"]    = "append-child";
                entry["id"]    = op.id;
                entry["child"] = op.child_node;
                break;
            case UiPatchOp::Kind::InsertChild:
                entry["op"]    = "insert-child";
                entry["id"]    = op.id;
                entry["index"] = op.index;
                entry["child"] = op.child_node;
                break;
            case UiPatchOp::Kind::ReplaceChildren:
                entry["op"]       = "replace-children";
                entry["id"]       = op.id;
                entry["children"] = op.children;
                break;
        }
        ops_array.push_back(std::move(entry));
    }
    Json doc = Json::object();
    doc["ops"] = std::move(ops_array);
    return doc;
}

// ===========================================================================
// Diff engine (public)
// ===========================================================================

std::optional<std::vector<UiPatchOp>>
ui_model_diff(const Json& old_spec, const Json& new_spec) {
    if (!old_spec.is_object() || !new_spec.is_object()) return std::nullopt;
    const auto get_type = [](const Json& n) -> std::string {
        return n.contains("type") ? n["type"].get<std::string>() : "";
    };
    if (get_type(old_spec) != "window" || get_type(new_spec) != "window") {
        return std::nullopt;
    }

    std::vector<UiPatchOp> ops;

    // Diff window-level props other than body/type. Non-title structured
    // values are still patchable because the native host can accept the
    // patch document and perform a focused rebuild when needed.
    Json window_changed = Json::object();
    for (const auto& key : union_keys(old_spec, new_spec)) {
        if (key == "type" || key == "body") continue;
        const Json old_v = old_spec.contains(key) ? old_spec[key] : Json(nullptr);
        const Json new_v = new_spec.contains(key) ? new_spec[key] : Json(nullptr);
        if (old_v == new_v) continue;
        window_changed[key] = new_v;
    }
    if (!window_changed.empty()) {
        UiPatchOp op;
        op.kind  = UiPatchOp::Kind::SetWindowProps;
        op.props = std::move(window_changed);
        ops.push_back(std::move(op));
    }

    // Recurse into body.
    const bool has_old = old_spec.contains("body") && old_spec["body"].is_object();
    const bool has_new = new_spec.contains("body") && new_spec["body"].is_object();
    if (has_old && has_new) {
        auto body_ops = diff_node(old_spec["body"], new_spec["body"]);
        if (!body_ops) return std::nullopt;
        for (auto& op : *body_ops) ops.push_back(std::move(op));
    } else if (has_old != has_new) {
        return std::nullopt;
    }

    return ops;
}

// ===========================================================================
// UiState
// ===========================================================================

auto UiState::find(const std::string& key)
    -> std::vector<std::pair<std::string, Json>>::iterator {
    return std::find_if(entries_.begin(), entries_.end(),
        [&](const auto& e) { return e.first == key; });
}

auto UiState::find(const std::string& key) const
    -> std::vector<std::pair<std::string, Json>>::const_iterator {
    return std::find_if(entries_.begin(), entries_.end(),
        [&](const auto& e) { return e.first == key; });
}

const Json& UiState::get(const std::string& key) const {
    auto it = find(key);
    if (it == entries_.end()) throw std::out_of_range("UiState: key not found: " + key);
    return it->second;
}

Json UiState::get(const std::string& key, Json default_value) const {
    auto it = find(key);
    return it != entries_.end() ? it->second : std::move(default_value);
}

void UiState::set(const std::string& key, Json value) {
    auto it = find(key);
    if (it != entries_.end()) it->second = std::move(value);
    else entries_.emplace_back(key, std::move(value));
}

void UiState::update(const std::string& key, const std::function<Json(Json)>& fn) {
    set(key, fn(get(key)));
}

void UiState::update(const std::string& key, const std::function<Json(Json)>& fn,
                     Json default_value) {
    set(key, fn(get(key, std::move(default_value))));
}

void UiState::merge(std::initializer_list<std::pair<std::string, Json>> new_entries) {
    for (auto& [k, v] : new_entries) set(k, std::move(v));
}

// ===========================================================================
// UiModel
// ===========================================================================

bool UiModel::render(std::function<UiWindow()> fn) {
    render_fn_ = std::move(fn);
    last_spec_.reset();
    return run_reconcile();
}

bool UiModel::show(const UiWindow& window) {
    if (!publish_fn_) return false;
    const Json spec = window.to_json();
    const bool ok = publish_fn_(spec.dump());
    last_spec_.reset(); // force next rerender to be a full publish
    return ok;
}

bool UiModel::rerender() {
    if (event_depth_ > 0) {
        rerender_pending_ = true;
        return true;
    }
    return run_reconcile();
}

void UiModel::pop_event_depth() {
    if (event_depth_ > 0) --event_depth_;
    if (event_depth_ == 0 && rerender_pending_) {
        rerender_pending_ = false;
        run_reconcile();
    }
}

bool UiModel::run_reconcile() {
    if (!render_fn_ || !publish_fn_) return false;

    const Json new_spec = render_fn_().to_json();

    if (!last_spec_) {
        const bool ok = publish_fn_(new_spec.dump());
        if (ok) last_spec_ = new_spec;
        return ok;
    }

    auto diff = ui_model_diff(*last_spec_, new_spec);

    if (!diff) {
        // Shape diverged – full republish.
        const bool ok = publish_fn_(new_spec.dump());
        if (ok) last_spec_ = new_spec;
        return ok;
    }

    if (diff->empty()) {
        last_spec_ = new_spec;
        return true; // nothing to send
    }

    if (!patch_fn_) {
        // No patch transport – fall back to full publish.
        const bool ok = publish_fn_(new_spec.dump());
        if (ok) last_spec_ = new_spec;
        return ok;
    }

    const Json patch_doc = ui_patch_ops_to_json(*diff);
    const bool ok = patch_fn_(patch_doc.dump());
    if (ok) last_spec_ = new_spec;
    return ok;
}

// ===========================================================================
// Builder free functions
// ===========================================================================

UiWindow ui_window(std::string title, UiNode body) {
    return UiWindow(std::move(title), std::move(body));
}

UiNode ui_stack(std::vector<UiNode> c) {
    UiNode n("stack"); n.children(std::move(c)); return n;
}
UiNode ui_row(std::vector<UiNode> c) {
    UiNode n("row"); n.children(std::move(c)); return n;
}
UiNode ui_toolbar(std::vector<UiNode> c) {
    UiNode n("toolbar"); n.children(std::move(c)); return n;
}
UiNode ui_card(std::string t, std::vector<UiNode> c) {
    UiNode n("card"); n.title(std::move(t)); n.children(std::move(c)); return n;
}
UiNode ui_divider() { return UiNode("divider"); }

UiNode ui_split_pane(std::string id_str, std::vector<UiNode> c) {
    UiNode n("split-pane"); n.id(std::move(id_str)); n.children(std::move(c)); return n;
}
UiNode ui_split_pane(std::string id_str, bool focused, std::vector<UiNode> c) {
    UiNode n("split-pane");
    n.id(std::move(id_str));
    n.focused(focused);
    n.children(std::move(c));
    return n;
}
UiNode ui_split_pane(std::string id_str,
                     double size,
                     int64_t min_size,
                     int64_t max_size,
                     bool collapsed,
                     bool focused,
                     std::vector<UiNode> c) {
    UiNode n("split-pane");
    n.id(std::move(id_str));
    n.size(size);
    n.min_size(min_size);
    if (max_size > 0) n.max_size(max_size);
    n.collapsed(collapsed);
    n.focused(focused);
    n.children(std::move(c));
    return n;
}
UiNode ui_split_view(std::string orientation, std::string event_name,
                     UiNode first_pane, UiNode second_pane) {
    UiNode n("split-view");
    n.orientation(std::move(orientation));
    n.event(std::move(event_name));
    n.children({std::move(first_pane), std::move(second_pane)});
    return n;
}
UiNode ui_split_view(std::string orientation,
                     std::string event_name,
                     UiNode first_pane,
                     UiNode second_pane,
                     int64_t width,
                     int64_t height,
                     int64_t divider_size,
                     bool live_resize,
                     bool disabled) {
    UiNode n("split-view");
    n.orientation(std::move(orientation));
    n.event(std::move(event_name));
    if (width > 0) n.width(width);
    if (height > 0) n.height(height);
    n.divider_size(divider_size);
    n.live_resize(live_resize);
    n.disabled(disabled);
    n.children({std::move(first_pane), std::move(second_pane)});
    return n;
}

UiNode ui_text(std::string t)    { UiNode n("text");    n.text(std::move(t));  return n; }
UiNode ui_heading(std::string t) { UiNode n("heading"); n.text(std::move(t));  return n; }
UiNode ui_badge(std::string t)   { UiNode n("badge");   n.text(std::move(t));  return n; }

UiNode ui_link(std::string text, std::string href, std::string event_name) {
    UiNode n("link");
    n.text(std::move(text));
    n.prop("href", uv(std::move(href)));
    n.event(std::move(event_name));
    return n;
}
UiNode ui_image(std::string s, std::string a) {
    UiNode n("image"); n.src(std::move(s)); n.alt(std::move(a)); return n;
}

UiNode ui_button(std::string text, std::string event_name) {
    UiNode n("button"); n.text(std::move(text)); n.event(std::move(event_name)); return n;
}
UiNode ui_input(std::string l, std::string v, std::string e) {
    UiNode n("input"); n.label(std::move(l)); n.value(std::move(v)); n.event(std::move(e)); return n;
}
UiNode ui_number_input(std::string l, double v, std::string e) {
    UiNode n("number-input"); n.label(std::move(l)); n.value(v); n.event(std::move(e)); return n;
}
UiNode ui_date_picker(std::string l, std::string v, std::string e) {
    UiNode n("date-picker"); n.label(std::move(l)); n.value(std::move(v)); n.event(std::move(e)); return n;
}
UiNode ui_time_picker(std::string l, std::string v, std::string e) {
    UiNode n("time-picker"); n.label(std::move(l)); n.value(std::move(v)); n.event(std::move(e)); return n;
}
UiNode ui_textarea(std::string l, std::string v, std::string e) {
    UiNode n("textarea"); n.label(std::move(l)); n.value(std::move(v)); n.event(std::move(e)); return n;
}
UiNode ui_rich_text(std::string l, std::string v, std::string e) {
    UiNode n("rich-text"); n.label(std::move(l)); n.value(std::move(v)); n.event(std::move(e)); return n;
}
UiNode ui_checkbox(std::string l, bool ch, std::string e) {
    UiNode n("checkbox"); n.label(std::move(l)); n.checked(ch); n.event(std::move(e)); return n;
}
UiNode ui_switch_toggle(std::string l, bool ch, std::string e) {
    UiNode n("switch"); n.label(std::move(l)); n.checked(ch); n.event(std::move(e)); return n;
}
UiNode ui_slider(std::string l, double v, std::string e) {
    UiNode n("slider"); n.label(std::move(l)); n.value(v); n.event(std::move(e)); return n;
}
UiNode ui_progress(std::string l, double v) {
    UiNode n("progress"); n.label(std::move(l)); n.value(v); return n;
}
UiNode ui_canvas(int64_t w, int64_t h, Json cmds) {
    UiNode n("canvas"); n.width(w); n.height(h); n.commands(std::move(cmds)); return n;
}

UiNode ui_text_grid(int64_t cols, int64_t rows, std::string e) {
    UiNode n("text-grid"); n.prop("columns", uv(cols)); n.prop("rows", uv(rows)); if(!e.empty()) n.event(std::move(e)); return n;
}
UiNode ui_text_grid(std::string id_str, int64_t cols, int64_t rows, std::string e, bool focused) {
    UiNode n("text-grid");
    n.id(std::move(id_str));
    n.prop("columns", uv(cols));
    n.prop("rows", uv(rows));
    if (!e.empty()) n.event(std::move(e));
    if (focused) n.focused(true);
    return n;
}
UiNode ui_indexed_graphics(int64_t w, int64_t h, std::string e) {
    UiNode n("indexed-graphics"); n.width(w); n.height(h); if(!e.empty()) n.event(std::move(e)); return n;
}
UiNode ui_indexed_graphics(std::string id_str, int64_t w, int64_t h, std::string e, bool focused) {
    UiNode n("indexed-graphics");
    n.id(std::move(id_str));
    n.width(w);
    n.height(h);
    if (!e.empty()) n.event(std::move(e));
    if (focused) n.focused(true);
    return n;
}
UiNode ui_rgba_pane(int64_t w, int64_t h, std::string e) {
    UiNode n("rgba-pane"); n.width(w); n.height(h); if(!e.empty()) n.event(std::move(e)); return n;
}
UiNode ui_rgba_pane(std::string id_str, int64_t w, int64_t h, std::string e, bool focused) {
    UiNode n("rgba-pane");
    n.id(std::move(id_str));
    n.width(w);
    n.height(h);
    if (!e.empty()) n.event(std::move(e));
    if (focused) n.focused(true);
    return n;
}

UiNode ui_select(std::string l, std::string v, std::string e, Json opts) {
    UiNode n("select"); n.label(std::move(l)); n.value(std::move(v)); n.event(std::move(e)); n.options(std::move(opts)); return n;
}
UiNode ui_list_box(std::string l, std::string v, std::string e, Json opts) {
    UiNode n("list-box"); n.label(std::move(l)); n.value(std::move(v)); n.event(std::move(e)); n.options(std::move(opts)); return n;
}
UiNode ui_radio_group(std::string l, std::string v, std::string e, Json opts) {
    UiNode n("radio-group"); n.label(std::move(l)); n.value(std::move(v)); n.event(std::move(e)); n.options(std::move(opts)); return n;
}
UiNode ui_table(std::string l, Json cols, Json r, std::string e) {
    UiNode n("table"); n.label(std::move(l)); n.columns(std::move(cols)); n.rows(std::move(r)); n.event(std::move(e)); return n;
}
UiNode ui_tree_view(std::string l, Json its, std::string e) {
    UiNode n("tree-view"); n.label(std::move(l)); n.items(std::move(its)); n.event(std::move(e)); return n;
}
UiNode ui_tree_view(std::string l,
                    Json its,
                    std::string selected_id,
                    Json expanded_ids,
                    std::string e,
                    std::string toggle_e,
                    std::string activate_e) {
    UiNode n("tree-view");
    n.label(std::move(l));
    n.items(std::move(its));
    n.selected_id(std::move(selected_id));
    n.expanded_ids(std::move(expanded_ids));
    n.event(std::move(e));
    n.toggle_event(std::move(toggle_e));
    n.activate_event(std::move(activate_e));
    return n;
}
UiNode ui_tabs(std::string l, std::string v, std::string e, Json t) {
    UiNode n("tabs"); n.label(std::move(l)); n.value(std::move(v)); n.event(std::move(e)); n.tabs(std::move(t)); return n;
}
UiNode ui_tabs(std::string l, std::string v, std::string e, Json t, int64_t w, int64_t h) {
    UiNode n("tabs");
    n.label(std::move(l));
    n.value(std::move(v));
    n.event(std::move(e));
    if (w > 0) n.width(w);
    if (h > 0) n.height(h);
    n.tabs(std::move(t));
    return n;
}
UiNode ui_context_menu(Json its, UiNode content) {
    UiNode n("context-menu");
    n.prop("items", std::move(its));
    n.children({std::move(content)});
    return n;
}

// ---------------------------------------------------------------------------
// Data helpers
// ---------------------------------------------------------------------------

Json ui_option(std::string value, std::string text) {
    Json o = Json::object();
    o["value"] = std::move(value);
    o["text"]  = std::move(text);
    return o;
}

Json ui_column(std::string key, std::string title) {
    Json o = Json::object();
    o["key"]   = std::move(key);
    o["title"] = std::move(title);
    return o;
}

Json ui_table_row(std::string id_str, Json props) {
    if (!props.is_object()) props = Json::object();
    props["id"] = std::move(id_str);
    return props;
}

Json ui_tree_item(std::string id_str, std::string text, Json children) {
    Json o = Json::object();
    o["id"]   = std::move(id_str);
    o["text"] = std::move(text);
    if (!children.empty()) o["children"] = std::move(children);
    return o;
}

Json ui_tree_item(std::string id_str, std::string text, Json children, Json tag) {
    Json o = ui_tree_item(std::move(id_str), std::move(text), std::move(children));
    o["tag"] = std::move(tag);
    return o;
}

Json ui_tab(std::string value, std::string text, const UiNode& content) {
    Json o = Json::object();
    o["value"]   = std::move(value);
    o["text"]    = std::move(text);
    o["content"] = content.to_json();
    return o;
}

// ---------------------------------------------------------------------------
// Menu helpers
// ---------------------------------------------------------------------------

Json ui_menu_item(std::string id_str, std::string text) {
    Json o = Json::object();
    o["id"]   = std::move(id_str);
    o["text"] = std::move(text);
    return o;
}

Json ui_menu_item_checked(std::string id_str, std::string text, bool checked) {
    Json o = ui_menu_item(std::move(id_str), std::move(text));
    o["checked"] = checked;
    return o;
}

Json ui_menu_separator() {
    Json o = Json::object();
    o["separator"] = true;
    return o;
}

Json ui_menu_submenu(std::string text, Json items) {
    Json o = Json::object();
    o["text"]  = std::move(text);
    o["items"] = std::move(items);
    return o;
}

Json ui_menu(std::string text, Json items) {
    Json o = Json::object();
    o["text"]  = std::move(text);
    o["items"] = std::move(items);
    return o;
}

Json ui_menu_bar(Json menus) {
    Json o = Json::object();
    o["menus"] = std::move(menus);
    return o;
}

// ===========================================================================
// Event parsing helpers
// ===========================================================================

Json ui_parse_event(const std::string& payload_utf8) {
    return Json::parse(payload_utf8, nullptr, /*allow_exceptions=*/false);
}

Json ui_event_ref(const Json& event, const std::string& key) {
    if (event.is_object() && event.contains(key)) return event[key];
    return Json(nullptr);
}

std::string ui_event_name(const Json& event) {
    const Json v = ui_event_ref(event, "event");
    return v.is_string() ? v.get<std::string>() : "";
}

} // namespace wingui
