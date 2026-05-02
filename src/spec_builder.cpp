#ifndef WINGUI_BUILD_DLL
#define WINGUI_BUILD_DLL
#endif

#include "wingui/spec_builder.h"

#include "wingui/ui_model.h"

#include "wingui_internal.h"
#include "nlohmann/json.hpp"

#include <cstring>
#include <string>
#include <utility>

using Json = nlohmann::ordered_json;

namespace {

const char* jsonTypeName(const Json& value) {
    if (value.is_object()) return "object";
    if (value.is_array()) return "array";
    if (value.is_string()) return "string";
    if (value.is_boolean()) return "boolean";
    if (value.is_number()) return "number";
    if (value.is_null()) return "null";
    return "value";
}

std::string autoId(const std::string& path) {
    return "__auto__:" + path;
}

bool validateObjectArray(const Json& value,
                         const std::string& path,
                         const char* field_name,
                         std::string* out_error) {
    if (!value.is_array()) {
        if (out_error) {
            *out_error = "Invalid native-ui form at " + path + "/" + field_name +
                         ": expected array, got " + jsonTypeName(value) + ".";
        }
        return false;
    }
    for (size_t i = 0; i < value.size(); ++i) {
        const auto& child = value[i];
        if (!child.is_object()) {
            if (out_error) {
                *out_error = "Invalid native-ui form at " + path + "/" + field_name + "/" + std::to_string(i) +
                             ": expected object, got " + jsonTypeName(child) + ".";
            }
            return false;
        }
    }
    return true;
}

bool validateTreeItems(const Json& value, const std::string& path, std::string* out_error) {
    if (!value.is_array()) {
        if (out_error) {
            *out_error = "Invalid native-ui form at " + path + ": expected array, got " + jsonTypeName(value) + ".";
        }
        return false;
    }
    for (size_t i = 0; i < value.size(); ++i) {
        const auto& item = value[i];
        if (!item.is_object()) {
            if (out_error) {
                *out_error = "Invalid native-ui form at " + path + "/" + std::to_string(i) +
                             ": expected object, got " + jsonTypeName(item) + ".";
            }
            return false;
        }
        auto children_it = item.find("children");
        if (children_it != item.end() && !validateTreeItems(*children_it, path + "/" + std::to_string(i) + "/children", out_error)) {
            return false;
        }
    }
    return true;
}

bool normalizeNode(Json* node, const std::string& path, std::string* out_error) {
    if (!node || !node->is_object()) {
        if (out_error) {
            *out_error = "Invalid native-ui form at " + path + ": expected object, got " + (node ? std::string(jsonTypeName(*node)) : std::string("null")) + ".";
        }
        return false;
    }

    auto type_it = node->find("type");
    if (type_it == node->end() || !type_it->is_string() || type_it->get<std::string>().empty()) {
        if (out_error) {
            *out_error = "Invalid native-ui form at " + path + ": missing string 'type'.";
        }
        return false;
    }
    const std::string type = type_it->get<std::string>();

    auto id_it = node->find("id");
    if (id_it != node->end()) {
        if (!id_it->is_string()) {
            if (out_error) {
                *out_error = "Invalid native-ui form at " + path + "/id: expected string, got " + jsonTypeName(*id_it) + ".";
            }
            return false;
        }
        if (id_it->get<std::string>().empty()) {
            (*node)["id"] = autoId(path);
        }
    } else {
        (*node)["id"] = autoId(path);
    }

    auto body_it = node->find("body");
    if (body_it != node->end()) {
        if (!body_it->is_object()) {
            if (out_error) {
                *out_error = "Invalid native-ui form at " + path + "/body: expected object, got " + jsonTypeName(*body_it) + ".";
            }
            return false;
        }
        if (!normalizeNode(&(*body_it), path + "/body", out_error)) {
            return false;
        }
    } else if (type == "window") {
        if (out_error) {
            *out_error = "Invalid native-ui form at " + path + ": window is missing 'body'.";
        }
        return false;
    }

    auto children_it = node->find("children");
    if (children_it != node->end()) {
        if (!children_it->is_array()) {
            if (out_error) {
                *out_error = "Invalid native-ui form at " + path + "/children: expected array, got " + jsonTypeName(*children_it) + ".";
            }
            return false;
        }
        for (size_t i = 0; i < children_it->size(); ++i) {
            auto& child = (*children_it)[i];
            if (!child.is_object()) {
                if (out_error) {
                    *out_error = "Invalid native-ui form at " + path + "/children/" + std::to_string(i) +
                                 ": expected node object, got " + jsonTypeName(child) + ".";
                }
                return false;
            }
            if (!normalizeNode(&child, path + "/children/" + std::to_string(i), out_error)) {
                return false;
            }
        }
    }

    if ((type == "select" || type == "list-box" || type == "radio-group") && node->contains("options")) {
        if (!validateObjectArray((*node)["options"], path, "options", out_error)) return false;
    }

    if (type == "tabs" && node->contains("tabs")) {
        if (!validateObjectArray((*node)["tabs"], path, "tabs", out_error)) return false;
        for (size_t i = 0; i < (*node)["tabs"].size(); ++i) {
            auto& tab = (*node)["tabs"][i];
            auto content_it = tab.find("content");
            if (content_it != tab.end()) {
                if (!content_it->is_object()) {
                    if (out_error) {
                        *out_error = "Invalid native-ui form at " + path + "/tabs/" + std::to_string(i) +
                                     "/content: expected object, got " + jsonTypeName(*content_it) + ".";
                    }
                    return false;
                }
                if (!normalizeNode(&(*content_it), path + "/tabs/" + std::to_string(i) + "/content", out_error)) {
                    return false;
                }
            }
        }
    }

    if (type == "table") {
        if (node->contains("columns") && !validateObjectArray((*node)["columns"], path, "columns", out_error)) return false;
        if (node->contains("rows") && !validateObjectArray((*node)["rows"], path, "rows", out_error)) return false;
    }

    if (type == "tree-view") {
        if (node->contains("items") && !validateTreeItems((*node)["items"], path + "/items", out_error)) return false;
        if (node->contains("expandedIds") && !(*node)["expandedIds"].is_array()) {
            if (out_error) {
                *out_error = "Invalid native-ui form at " + path + "/expandedIds: expected array, got " +
                             jsonTypeName((*node)["expandedIds"]) + ".";
            }
            return false;
        }
    }

    if (type == "split-view") {
        auto split_children_it = node->find("children");
        if (split_children_it == node->end() || !split_children_it->is_array() || split_children_it->size() != 2) {
            if (out_error) {
                *out_error = "Invalid native-ui form at " + path + ": split-view requires exactly two split-pane children.";
            }
            return false;
        }
        for (size_t i = 0; i < split_children_it->size(); ++i) {
            const auto& child = (*split_children_it)[i];
            auto child_type_it = child.find("type");
            if (!child.is_object() || child_type_it == child.end() || !child_type_it->is_string() || child_type_it->get<std::string>() != "split-pane") {
                if (out_error) {
                    *out_error = "Invalid native-ui form at " + path + "/children/" + std::to_string(i) + ": expected split-pane child.";
                }
                return false;
            }
        }
    }

    if (type == "context-menu" && node->contains("items")) {
        if (!validateObjectArray((*node)["items"], path, "items", out_error)) return false;
    }

    if (type == "canvas" && node->contains("commands")) {
        if (!validateObjectArray((*node)["commands"], path, "commands", out_error)) return false;
    }

    return true;
}

bool parseJsonObjectSpec(
    const char* json_utf8,
    const char* caller,
    Json* out_spec) {
    if (!json_utf8 || !json_utf8[0]) {
        wingui_set_last_error_string_internal((std::string(caller) + ": json_utf8 was empty").c_str());
        return false;
    }

    Json parsed = Json::parse(json_utf8, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        wingui_set_last_error_string_internal((std::string(caller) + ": invalid JSON object spec").c_str());
        return false;
    }

    if (out_spec) {
        *out_spec = std::move(parsed);
    }
    wingui_clear_last_error_internal();
    return true;
}

bool parseAndNormalizeSpec(
    const char* json_utf8,
    const char* caller,
    Json* out_spec) {
    Json parsed;
    if (!parseJsonObjectSpec(json_utf8, caller, &parsed)) {
        return false;
    }

    std::string error;
    if (!normalizeNode(&parsed, "window", &error)) {
        wingui_set_last_error_string_internal((std::string(caller) + ": " + error).c_str());
        return false;
    }

    if (parsed.find("title") == parsed.end() || !parsed["title"].is_string()) {
        parsed["title"] = std::string();
    }

    if (out_spec) {
        *out_spec = std::move(parsed);
    }
    wingui_clear_last_error_internal();
    return true;
}

int32_t copyUtf8Result(
    const std::string& text,
    char* buffer_utf8,
    uint32_t buffer_size,
    uint32_t* out_required_size,
    const char* caller) {
    const uint32_t required_size = static_cast<uint32_t>(text.size() + 1);
    if (out_required_size) {
        *out_required_size = required_size;
    }

    if (!buffer_utf8 || buffer_size < required_size) {
        wingui_set_last_error_string_internal((std::string(caller) + ": buffer was null or too small").c_str());
        return 0;
    }

    std::memcpy(buffer_utf8, text.c_str(), required_size);
    wingui_clear_last_error_internal();
    return 1;
}

} // namespace

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_spec_builder_validate_json(
    const char* json_utf8) {
    return parseAndNormalizeSpec(json_utf8, "wingui_spec_builder_validate_json", nullptr) ? 1 : 0;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_spec_builder_copy_canonical_json(
    const char* json_utf8,
    char* buffer_utf8,
    uint32_t buffer_size,
    uint32_t* out_required_size) {
    Json spec;
    if (!parseJsonObjectSpec(json_utf8, "wingui_spec_builder_copy_canonical_json", &spec)) {
        return 0;
    }

    return copyUtf8Result(
        spec.dump(),
        buffer_utf8,
        buffer_size,
        out_required_size,
        "wingui_spec_builder_copy_canonical_json");
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_spec_builder_copy_normalized_json(
    const char* json_utf8,
    char* buffer_utf8,
    uint32_t buffer_size,
    uint32_t* out_required_size) {
    Json spec;
    if (!parseAndNormalizeSpec(json_utf8, "wingui_spec_builder_copy_normalized_json", &spec)) {
        return 0;
    }

    return copyUtf8Result(
        spec.dump(),
        buffer_utf8,
        buffer_size,
        out_required_size,
        "wingui_spec_builder_copy_normalized_json");
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_spec_builder_copy_patch_json(
    const char* old_json_utf8,
    const char* new_json_utf8,
    char* buffer_utf8,
    uint32_t buffer_size,
    uint32_t* out_required_size,
    int32_t* out_requires_full_publish,
    uint32_t* out_patch_op_count) {
    Json old_spec;
    Json new_spec;
    if (!out_requires_full_publish) {
        wingui_set_last_error_string_internal("wingui_spec_builder_copy_patch_json: out_requires_full_publish was null");
        return 0;
    }
    if (!parseAndNormalizeSpec(old_json_utf8, "wingui_spec_builder_copy_patch_json", &old_spec) ||
        !parseAndNormalizeSpec(new_json_utf8, "wingui_spec_builder_copy_patch_json", &new_spec)) {
        return 0;
    }

    *out_requires_full_publish = 0;
    if (out_patch_op_count) {
        *out_patch_op_count = 0;
    }

    const auto patch_ops = wingui::ui_model_diff(old_spec, new_spec);
    if (!patch_ops.has_value()) {
        *out_requires_full_publish = 1;
        if (out_required_size) {
            *out_required_size = 0;
        }
        wingui_clear_last_error_internal();
        return 1;
    }

    if (out_patch_op_count) {
        *out_patch_op_count = static_cast<uint32_t>(patch_ops->size());
    }

    const std::string patch_json = wingui::ui_patch_ops_to_json(*patch_ops).dump();
    return copyUtf8Result(
        patch_json,
        buffer_utf8,
        buffer_size,
        out_required_size,
        "wingui_spec_builder_copy_patch_json");
}