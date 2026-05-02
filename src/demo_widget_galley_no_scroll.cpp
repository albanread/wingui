#include "wingui/app.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <array>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace {

using Json = wingui::Json;
using EventLabel = std::pair<const char*, const char*>;

constexpr std::array<EventLabel, 24> kTrackedEvents{{
    {"name", "Input"},
    {"age", "Number input"},
    {"meeting_date", "Date picker"},
    {"meeting_time", "Time picker"},
    {"notes", "Textarea"},
    {"rich_notes", "Rich text"},
    {"enabled", "Checkbox"},
    {"turbo", "Switch"},
    {"intensity", "Slider"},
    {"flavor", "Select"},
    {"size", "List box"},
    {"plan", "Radio group"},
    {"table_selected", "Table"},
    {"tree_selected", "Tree select"},
    {"tree_toggle", "Tree toggle"},
    {"counter_up", "Button"},
    {"link_ping", "Link"},
    {"menu_reset_demo", "Menu reset demo"},
    {"menu_seed_notes", "Menu seed notes"},
    {"menu_clear_notes", "Menu clear notes"},
    {"menu_reset_coverage", "Menu reset coverage"},
    {"menu_about", "Menu about"},
    {"menu_toggle_enabled", "Menu toggle enabled"},
    {"menu_toggle_turbo", "Menu toggle turbo"},
}};

void seed_demo_state(wingui::UiState& state) {
    state.merge({
        {"name", "Ada Lovelace"},
        {"age", 37.0},
        {"meeting_date", "2026-05-01"},
        {"meeting_time", "13:45"},
        {"notes", "This variant removes nested scroll views so the full gallery participates in the window's own vertical layout."},
        {"rich_notes", wg::ui_rtf_from_html("<p><b>Rich text</b> should emit RTF.</p>")},
        {"enabled", true},
        {"turbo", false},
        {"intensity", 42.0},
        {"flavor", "mint"},
        {"size", "m"},
        {"plan", "pro"},
        {"table_selected", "row-choice"},
        {"tree_selected", "inputs"},
        {"tree_expanded", Json::array({"root", "inputs", "choices"})},
        {"clicks", 0},
        {"last_event", "No widget event yet."},
        {"rich_text_focused", false},
        {"menu_message", "Use the main menu to reset the demo, seed content, or inspect menu events."},
        {"hits", Json::object()},
    });
}

Json rich_menu_command_item(std::string id,
                            std::string text,
                            std::string command,
                            bool disabled) {
    Json item = wg::ui_menu_item_disabled(std::move(id), std::move(text), disabled);
    item["value"] = std::string("format-selection:") + command;
    return item;
}

Json make_options(std::initializer_list<std::pair<const char*, const char*>> items) {
    Json out = Json::array();
    for (const auto& item : items) {
        out.push_back(wg::ui_option(item.first, item.second));
    }
    return out;
}

Json make_table_columns() {
    Json columns = Json::array();
    columns.push_back(wg::ui_column("widget", "Widget"));
    columns.push_back(wg::ui_column("status", "Status"));
    columns.push_back(wg::ui_column("notes", "Notes"));
    return columns;
}

Json make_table_rows() {
    Json rows = Json::array();
    rows.push_back(wg::ui_table_row("row-input", {
        {"widget", "Text inputs"},
        {"status", "Expect change events"},
        {"notes", "Typing should update the summary."},
    }));
    rows.push_back(wg::ui_table_row("row-choice", {
        {"widget", "Selectors"},
        {"status", "Expect selection events"},
        {"notes", "Select, list box, radio, and tree."},
    }));
    rows.push_back(wg::ui_table_row("row-data", {
        {"widget", "Tree and table"},
        {"status", "Expect id events"},
        {"notes", "Selection and expansion should appear at right."},
    }));
    return rows;
}

Json make_tree_items() {
    Json roots = Json::array();
    roots.push_back(wg::ui_tree_item("root", "Widget surfaces", Json::array({
        wg::ui_tree_item("inputs", "Inputs", Json::array({
            wg::ui_tree_item("text", "Text entry"),
            wg::ui_tree_item("rich", "Rich text"),
            wg::ui_tree_item("sliders", "Slider and progress"),
        })),
        wg::ui_tree_item("choices", "Choices", Json::array({
            wg::ui_tree_item("select", "Select"),
            wg::ui_tree_item("list", "List box"),
            wg::ui_tree_item("radio", "Radio group"),
        })),
        wg::ui_tree_item("collections", "Collections", Json::array({
            wg::ui_tree_item("table", "Table"),
            wg::ui_tree_item("tree", "Tree view"),
        })),
    })));
    return roots;
}

int hit_count(const Json& hits, const char* key) {
    if (!hits.is_object()) return 0;
    const auto it = hits.find(key);
    return (it != hits.end() && it->is_number_integer()) ? it->get<int>() : 0;
}

int total_hits(const Json& hits) {
    int total = 0;
    if (!hits.is_object()) return total;
    for (const auto& item : hits.items()) {
        if (item.value().is_number_integer()) total += item.value().get<int>();
    }
    return total;
}

std::vector<wg::UiNode> build_hit_rows(const Json& hits) {
    std::vector<wg::UiNode> rows;
    rows.reserve(kTrackedEvents.size());
    for (const auto& tracked : kTrackedEvents) {
        const int count = hit_count(hits, tracked.first);
        char count_text[16];
        std::snprintf(count_text, sizeof(count_text), "x%d", count);
        rows.push_back(wg::ui_row({
            wg::ui_badge(count > 0 ? "seen" : "idle"),
            wg::ui_text(std::string(tracked.second) + " " + count_text),
        }).gap(8));
    }
    return rows;
}

std::string state_summary(const std::string& name,
                          double age,
                          const std::string& meeting_date,
                          const std::string& meeting_time,
                          bool enabled,
                          bool turbo,
                          double intensity,
                          const std::string& flavor,
                          const std::string& size,
                          const std::string& plan,
                          int clicks,
                          const std::string& table_selected,
                          const std::string& tree_selected,
                          const Json& tree_expanded) {
    char numeric[160];
    std::snprintf(numeric,
                  sizeof(numeric),
                  "Age: %.0f\nIntensity: %.0f\nClicks: %d\nEnabled: %s\nTurbo: %s\n",
                  age,
                  intensity,
                  clicks,
                  enabled ? "true" : "false",
                  turbo ? "true" : "false");

    std::string summary;
    summary += "Name: " + name + "\n";
    summary += numeric;
    summary += "Date: " + meeting_date + "\n";
    summary += "Time: " + meeting_time + "\n";
    summary += "Flavor: " + flavor + "\n";
    summary += "Size: " + size + "\n";
    summary += "Plan: " + plan + "\n";
    summary += "Table row: " + table_selected + "\n";
    summary += "Tree node: " + tree_selected + "\n";
    summary += "Expanded: " + tree_expanded.dump();
    return summary;
}

std::string reconcile_mode_text(wingui::UiModel::ReconcileMode mode) {
    switch (mode) {
    case wingui::UiModel::ReconcileMode::None:
        return "none";
    case wingui::UiModel::ReconcileMode::FullPublishInitial:
        return "full-initial";
    case wingui::UiModel::ReconcileMode::FullPublishDiffFallback:
        return "full-diff-fallback";
    case wingui::UiModel::ReconcileMode::FullPublishNoPatchTransport:
        return "full-no-patch-transport";
    case wingui::UiModel::ReconcileMode::Patch:
        return "patch";
    case wingui::UiModel::ReconcileMode::NoChange:
        return "no-change";
    case wingui::UiModel::ReconcileMode::PublishFailed:
        return "publish-failed";
    case wingui::UiModel::ReconcileMode::PatchFailed:
        return "patch-failed";
    }
    return "unknown";
}

std::string patch_diagnostics_summary(const wingui::UiModel& model,
                                      const SuperTerminalNativeUiPatchMetrics& native_metrics) {
    char buffer[768];
    std::snprintf(buffer,
                  sizeof(buffer),
                  "Model mode: %s\n"
                  "Model patch ops: %zu\n"
                  "Model full publishes: %llu\n"
                  "Model patch sends: %llu\n"
                  "Model diff fallbacks: %llu\n"
                  "Model no-change: %llu\n"
                  "Model publish failures: %llu\n"
                  "Model patch failures: %llu\n"
                  "Native publish count: %llu\n"
                  "Native patch requests: %llu\n"
                  "Native direct apply: %llu\n"
                  "Native subtree rebuilds: %llu\n"
                  "Native window rebuilds: %llu\n"
                  "Native resize rejects: %llu\n"
                  "Native failed patches: %llu",
                  reconcile_mode_text(model.last_reconcile_mode()).c_str(),
                  model.last_patch_op_count(),
                  static_cast<unsigned long long>(model.full_publish_count()),
                  static_cast<unsigned long long>(model.patch_send_count()),
                  static_cast<unsigned long long>(model.diff_fallback_count()),
                  static_cast<unsigned long long>(model.no_change_count()),
                  static_cast<unsigned long long>(model.publish_failure_count()),
                  static_cast<unsigned long long>(model.patch_failure_count()),
                  static_cast<unsigned long long>(native_metrics.publish_count),
                  static_cast<unsigned long long>(native_metrics.patch_request_count),
                  static_cast<unsigned long long>(native_metrics.direct_apply_count),
                  static_cast<unsigned long long>(native_metrics.subtree_rebuild_count),
                  static_cast<unsigned long long>(native_metrics.window_rebuild_count),
                  static_cast<unsigned long long>(native_metrics.resize_reject_count),
                  static_cast<unsigned long long>(native_metrics.failed_patch_count));
    return buffer;
}

} // namespace

int main() {
    wg::Layout layout;
    wg::App app;
    SuperTerminalRunResult result{};
    SuperTerminalClientContext* app_ctx = nullptr;

    auto render_window = [&]() {
        const std::string name = layout.state().get("name", std::string("Ada Lovelace")).get<std::string>();
        const double age = layout.state().get("age", 37.0).get<double>();
        const std::string meeting_date = layout.state().get("meeting_date", std::string("2026-05-01")).get<std::string>();
        const std::string meeting_time = layout.state().get("meeting_time", std::string("13:45")).get<std::string>();
        const std::string notes = layout.state().get("notes", std::string()).get<std::string>();
        const std::string rich_notes = layout.state().get("rich_notes", std::string()).get<std::string>();
        const bool enabled = layout.state().get("enabled", true).get<bool>();
        const bool turbo = layout.state().get("turbo", false).get<bool>();
        const double intensity = layout.state().get("intensity", 42.0).get<double>();
        const std::string flavor = layout.state().get("flavor", std::string("mint")).get<std::string>();
        const std::string size = layout.state().get("size", std::string("m")).get<std::string>();
        const std::string plan = layout.state().get("plan", std::string("pro")).get<std::string>();
        const std::string table_selected = layout.state().get("table_selected", std::string("row-choice")).get<std::string>();
        const std::string tree_selected = layout.state().get("tree_selected", std::string("inputs")).get<std::string>();
        const Json tree_expanded = layout.state().get("tree_expanded", Json::array());
        const int clicks = layout.state().get("clicks", 0).get<int>();
        const std::string last_event = layout.state().get("last_event", std::string()).get<std::string>();
        const std::string menu_message = layout.state().get("menu_message", std::string()).get<std::string>();
        const bool rich_text_focused = layout.state().get("rich_text_focused", false).get<bool>();
        const Json hits = layout.state().get("hits", Json::object());
        const std::string empty_rtf = wg::ui_rtf_from_plain_text("");
        const bool has_notes = !notes.empty() || rich_notes != empty_rtf;
        const bool has_coverage = total_hits(hits) > 0 || clicks > 0;

        SuperTerminalNativeUiPatchMetrics native_metrics{};
        if (app_ctx) {
            super_terminal_get_native_ui_patch_metrics(app_ctx, &native_metrics);
        }

        int seen = 0;
        for (const auto& tracked : kTrackedEvents) {
            if (hit_count(hits, tracked.first) > 0) {
                ++seen;
            }
        }

        char coverage_text[64];
        char total_text[64];
        char click_text[64];
        std::snprintf(coverage_text, sizeof(coverage_text), "Coverage %d/%zu", seen, kTrackedEvents.size());
        std::snprintf(total_text, sizeof(total_text), "Events %d", total_hits(hits));
        std::snprintf(click_text, sizeof(click_text), "Clicks %d", clicks);

        std::vector<wg::UiNode> left_nodes = {
            wg::ui_heading("Interactive widget galley"),
            wg::ui_text("This version intentionally avoids nested scroll areas. If the content is taller than the window, the host window should own the vertical overflow rather than an inner scroll-view."),
            wg::ui_card("Editable", {
                wg::ui_form({
                    wg::ui_input("Name", name, "name"),
                    wg::ui_number_input("Age", age, "age"),
                    wg::ui_date_picker("Meeting date", meeting_date, "meeting_date"),
                    wg::ui_time_picker("Meeting time", meeting_time, "meeting_time"),
                    wg::ui_textarea("Notes", notes, "notes"),
                    wg::ui_rich_text("Rich notes", rich_notes, "rich_notes"),
                }).gap(10),
            }),
            wg::ui_card("Toggles", {
                wg::ui_form({
                    wg::ui_checkbox("Enabled", enabled, "enabled"),
                    wg::ui_switch_toggle("Turbo mode", turbo, "turbo"),
                    wg::ui_slider("Intensity", intensity, "intensity").min_val(0).max_val(100).step(1),
                    wg::ui_progress("Intensity progress", intensity),
                }).gap(10),
            }),
            wg::ui_card("Actions", {
                wg::ui_row({
                    wg::ui_button("Count click", "counter_up"),
                    wg::ui_button("Reset coverage", "counter_reset"),
                }).gap(10),
                wg::ui_link("Ping link event", "https://example.com", "link_ping"),
            }),
            wg::ui_card("Choice widgets", {
                wg::ui_form({
                    wg::ui_select("Flavor", flavor, "flavor", make_options({
                        {"mint", "Mint"},
                        {"citrus", "Citrus"},
                        {"cocoa", "Cocoa"},
                    })),
                    wg::ui_list_box("Size", size, "size", make_options({
                        {"s", "Small"},
                        {"m", "Medium"},
                        {"l", "Large"},
                    })),
                    wg::ui_radio_group("Plan", plan, "plan", make_options({
                        {"free", "Free"},
                        {"pro", "Pro"},
                        {"studio", "Studio"},
                    })),
                }).gap(10),
            }),
            wg::ui_card("Current picks", {
                wg::ui_text("Flavor, list-box, and radio-group should all update the summary panel immediately."),
                wg::ui_badge(std::string("Flavor: ") + flavor),
                wg::ui_badge(std::string("Size: ") + size),
                wg::ui_badge(std::string("Plan: ") + plan),
            }),
            wg::ui_card("Table", {
                wg::ui_table("Widget status", make_table_columns(), make_table_rows(), "table_selected")
                    .selected_id(table_selected),
            }),
            wg::ui_card("Tree", {
                wg::ui_tree_view("Widget map",
                                 make_tree_items(),
                                 tree_selected,
                                 tree_expanded,
                                 "tree_selected",
                                 "tree_toggle",
                                 "tree_activate"),
            }),
        };

        std::vector<wg::UiNode> summary_nodes = {
            wg::ui_card("Coverage", {
                wg::ui_row({
                    wg::ui_badge(coverage_text),
                    wg::ui_badge(total_text),
                    wg::ui_badge(click_text),
                }).gap(8),
                wg::ui_text("This summary column also avoids an inner scroll-view so the whole window owns the overflow behavior."),
            }),
            wg::ui_card("Last event", {
                wg::ui_text(last_event),
            }),
            wg::ui_card("Menu response", {
                wg::ui_text(menu_message),
            }),
            wg::ui_card("Current state", {
                wg::ui_text(state_summary(name,
                                          age,
                                          meeting_date,
                                          meeting_time,
                                          enabled,
                                          turbo,
                                          intensity,
                                          flavor,
                                          size,
                                          plan,
                                          clicks,
                                          table_selected,
                                          tree_selected,
                                          tree_expanded)),
            }),
            wg::ui_card("Patch diagnostics", {
                wg::ui_text(patch_diagnostics_summary(layout.model(), native_metrics)),
            }),
            wg::ui_card("Event hits", build_hit_rows(hits)),
        };

        return wg::ui_window(
            "Widget Galley (No Scroll Areas)",
            wg::ui_stack({
                wg::ui_heading("Interactive widget galley"),
                wg::ui_text("This version relies on the window's root scrollbars rather than inner scroll views. The page keeps a bounded two-column layout and the host window owns vertical overflow."),
                wg::ui_row({
                    wg::ui_stack(left_nodes).gap(10),
                    wg::ui_stack(summary_nodes).gap(10),
                }).gap(16),
            }).gap(12).padding(12)
        )
            .menu_bar(wg::ui_menu_bar(Json::array({
                wg::ui_menu("File", Json::array({
                    wg::ui_menu_item("menu_reset_demo", "Reset demo"),
                    wg::ui_menu_separator(),
                    wg::ui_menu_item("menu_exit", "Exit"),
                })),
                wg::ui_menu("Content", Json::array({
                    wg::ui_menu_item("menu_seed_notes", "Seed sample notes"),
                    wg::ui_menu_item_disabled("menu_clear_notes", "Clear notes", !has_notes),
                })),
                wg::ui_menu("View", Json::array({
                    wg::ui_menu_item_checked("menu_toggle_enabled", "Enabled", enabled),
                    wg::ui_menu_item_checked("menu_toggle_turbo", "Turbo mode", turbo),
                    wg::ui_menu_separator(),
                    wg::ui_menu_item_disabled("menu_reset_coverage", "Reset coverage", !has_coverage),
                })),
                wg::ui_menu("Rich text", Json::array({
                    rich_menu_command_item("menu_rich_bold", "Bold\tCtrl+B", "bold", !rich_text_focused),
                    rich_menu_command_item("menu_rich_italic", "Italic\tCtrl+I", "italic", !rich_text_focused),
                    rich_menu_command_item("menu_rich_underline", "Underline\tCtrl+U", "underline", !rich_text_focused),
                    rich_menu_command_item("menu_rich_clear", "Clear formatting\tCtrl+Space", "clear", !rich_text_focused),
                    wg::ui_menu_separator(),
                    rich_menu_command_item("menu_rich_left", "Align left\tCtrl+L", "align-left", !rich_text_focused),
                    rich_menu_command_item("menu_rich_center", "Align center\tCtrl+E", "align-center", !rich_text_focused),
                    rich_menu_command_item("menu_rich_right", "Align right\tCtrl+R", "align-right", !rich_text_focused),
                    rich_menu_command_item("menu_rich_justify", "Justify\tCtrl+J", "align-justify", !rich_text_focused),
                    rich_menu_command_item("menu_rich_bullets", "Bullets\tCtrl+Shift+L", "bullets", !rich_text_focused),
                    rich_menu_command_item("menu_rich_numbering", "Numbering\tCtrl+Shift+7", "numbering", !rich_text_focused),
                    rich_menu_command_item("menu_rich_indent", "Indent\tTab", "indent", !rich_text_focused),
                    rich_menu_command_item("menu_rich_outdent", "Outdent\tShift+Tab", "outdent", !rich_text_focused),
                })),
                wg::ui_menu("Help", Json::array({
                    wg::ui_menu_separator(),
                    wg::ui_menu_item("menu_about", "About this galley"),
                })),
            })))
            .status_bar(wg::ui_status_bar(Json::array({
                wg::ui_status_part(std::string("Mode ") + reconcile_mode_text(layout.model().last_reconcile_mode())),
                wg::ui_status_part(coverage_text, 110),
                wg::ui_status_part(total_text, 90),
                wg::ui_status_part(click_text, 80),
            })));
    };

    const int exit_code = app
        .title("Demo - widget galley (no scroll areas)")
        .layout(layout)
        .on_setup([&](SuperTerminalClientContext* ctx) {
            app_ctx = ctx;
            seed_demo_state(layout.state());
            return layout.render(render_window);
        })
        .on_event([&](const wg::Event& e) {
            if (e.is_close_requested()) {
                wg::request_stop(nullptr);
                return;
            }
            if (!e.is_native_ui()) return;

            const Json ev = wg::ui_parse_event(e.native_ui().payload_json_utf8);
            if (!ev.is_object()) return;

            const std::string event_name = wg::ui_event_name(ev);
            if (event_name.empty()) return;

            layout.state().set("last_event", ev.dump(2));

            if (ev.contains("focused") && ev.contains("controlType")) {
                const bool focused = ev["focused"].is_boolean() ? ev["focused"].get<bool>() : false;
                const std::string control_type = ev["controlType"].is_string() ? ev["controlType"].get<std::string>() : std::string();
                if (control_type == "rich-text") {
                    layout.state().set("rich_text_focused", focused);
                    layout.state().set("menu_message", focused
                        ? "Rich text menu is active while the rich text control has focus."
                        : "Rich text menu is inactive because focus left the rich text control.");
                }
                layout.rerender();
                return;
            }

            Json hits = layout.state().get("hits", Json::object());
            if (!hits.is_object()) hits = Json::object();
            hits[event_name] = hit_count(hits, event_name.c_str()) + 1;
            layout.state().set("hits", hits);

            if (event_name == "menu_exit") {
                wg::request_stop(app_ctx);
                return;
            } else if (event_name == "menu_reset_demo") {
                seed_demo_state(layout.state());
                layout.state().set("menu_message", "Menu: reset the full demo state to its seeded values.");
            } else if (event_name == "menu_seed_notes") {
                layout.state().set("notes", "Seeded from the main menu. This proves menu commands can mutate normal widget state and rely on the same rerender path.");
                layout.state().set("rich_notes", wg::ui_rtf_from_html("<p><b>Seeded from the menu</b> with <i>native RichEdit</i> content.</p>"));
                layout.state().set("menu_message", "Menu: seeded both plain and rich notes.");
            } else if (event_name == "menu_clear_notes") {
                layout.state().set("notes", "");
                layout.state().set("rich_notes", wg::ui_rtf_from_plain_text(""));
                layout.state().set("menu_message", "Menu: cleared both note fields.");
            } else if (event_name == "menu_toggle_enabled") {
                const bool current = layout.state().get("enabled", true).get<bool>();
                layout.state().set("enabled", !current);
                layout.state().set("menu_message", std::string("Menu: ") + (!current ? "enabled" : "disabled") + " the checkbox state.");
            } else if (event_name == "menu_toggle_turbo") {
                const bool current = layout.state().get("turbo", false).get<bool>();
                layout.state().set("turbo", !current);
                layout.state().set("menu_message", std::string("Menu: turbo mode is now ") + (!current ? "on." : "off."));
            } else if (event_name == "menu_reset_coverage") {
                layout.state().set("clicks", 0);
                layout.state().set("hits", Json::object());
                layout.state().set("menu_message", "Menu: cleared click count and event coverage history.");
            } else if (event_name == "menu_about") {
                layout.state().set("menu_message", "This galley now demonstrates a native Win32 main menu feeding standard UI events back into the declarative app model.");
            } else if (event_name == "counter_up") {
                const int clicks = layout.state().get("clicks", 0).get<int>();
                layout.state().set("clicks", clicks + 1);
            } else if (event_name == "counter_reset") {
                layout.state().set("clicks", 0);
                layout.state().set("hits", Json::object());
            } else if (event_name == "tree_toggle") {
                if (ev.contains("expandedIds")) layout.state().set("tree_expanded", ev["expandedIds"]);
            } else if (event_name == "tree_selected") {
                if (ev.contains("selectedId")) {
                    layout.state().set("tree_selected", ev["selectedId"]);
                } else if (ev.contains("value")) {
                    layout.state().set("tree_selected", ev["value"]);
                }
            } else if (ev.contains("value")) {
                layout.state().set(event_name, ev["value"]);
            }

            layout.rerender();
        })
        .run(&result);

    return exit_code;
}