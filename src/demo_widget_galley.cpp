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

constexpr std::array<EventLabel, 18> kTrackedEvents{{
    {"main_tab", "Tabs"},
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
}};

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
        {"notes", "Select, list box, radio, tabs."},
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
        rows.push_back(
            wg::ui_row({
                wg::ui_badge(count > 0 ? "seen" : "idle"),
                wg::ui_text(std::string(tracked.second) + " " + count_text),
            }).gap(8)
        );
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
    char numeric[128];
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

    const int exit_code = app
        .title("Demo - widget galley")
        .layout(layout)
        .on_setup([&](SuperTerminalClientContext* ctx) {
            app_ctx = ctx;
            layout.state().merge({
                {"main_tab", "inputs"},
                {"name", "Ada Lovelace"},
                {"age", 37.0},
                {"meeting_date", "2026-05-01"},
                {"meeting_time", "13:45"},
                {"notes", "Try each control and watch the coverage panel."},
                {"rich_notes", "<p><b>Rich text</b> should emit HTML.</p>"},
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
                {"hits", Json::object()},
            });

            return layout.render([&] {
                const std::string main_tab = layout.state().get("main_tab", std::string("inputs")).get<std::string>();
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
                const Json hits = layout.state().get("hits", Json::object());
                SuperTerminalNativeUiPatchMetrics native_metrics{};
                if (app_ctx) {
                    super_terminal_get_native_ui_patch_metrics(app_ctx, &native_metrics);
                }

                int seen = 0;
                for (const auto& tracked : kTrackedEvents) {
                    if (hit_count(hits, tracked.first) > 0) ++seen;
                }

                char coverage_text[64];
                char total_text[64];
                char click_text[64];
                std::snprintf(coverage_text, sizeof(coverage_text), "Coverage %d/%zu", seen, kTrackedEvents.size());
                std::snprintf(total_text, sizeof(total_text), "Events %d", total_hits(hits));
                std::snprintf(click_text, sizeof(click_text), "Clicks %d", clicks);

                std::vector<wg::UiNode> input_nodes = {
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
                };

                std::vector<wg::UiNode> choice_nodes = {
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
                };

                std::vector<wg::UiNode> data_nodes = {
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

                Json tabs = Json::array();
                tabs.push_back(wg::ui_tab("inputs", "Inputs", wg::ui_scroll_view(input_nodes).id("inputs-scroll").padding(10).gap(10)));
                tabs.push_back(wg::ui_tab("choices", "Choices", wg::ui_scroll_view(choice_nodes).id("choices-scroll").padding(10).gap(10)));
                tabs.push_back(wg::ui_tab("data", "Data", wg::ui_scroll_view(data_nodes).id("data-scroll").padding(10).gap(10)));

                std::vector<wg::UiNode> summary_nodes = {
                    wg::ui_card("Coverage", {
                        wg::ui_row({
                            wg::ui_badge(coverage_text),
                            wg::ui_badge(total_text),
                            wg::ui_badge(click_text),
                        }).gap(8),
                        wg::ui_text("Try interacting with each widget. The panel counts which event names actually fired."),
                    }),
                    wg::ui_card("Last event", {
                        wg::ui_text(last_event),
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
                    "Widget Galley",
                    wg::ui_split_view(
                        "horizontal",
                        "widget-galley-split",
                        wg::ui_split_pane("left", {
                            wg::ui_stack({
                                wg::ui_heading("Interactive widget galley"),
                                wg::ui_text("This screen is intentionally broad: edit values, switch tabs, select rows, and expand the tree."),
                                wg::ui_tabs("Sections", main_tab, "main_tab", std::move(tabs), 840, 680),
                            }).gap(10).padding(12),
                        }).size(0.68),
                        wg::ui_split_pane("right", {
                            wg::ui_scroll_view(summary_nodes).id("summary-scroll").padding(10).gap(10),
                        }).size(0.32)
                    )
                );
            });
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

            Json hits = layout.state().get("hits", Json::object());
            if (!hits.is_object()) hits = Json::object();
            hits[event_name] = hit_count(hits, event_name.c_str()) + 1;
            layout.state().set("hits", hits);

            if (event_name == "counter_up") {
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

    if (result.host_error_code != SUPERTERMINAL_HOST_ERROR_NONE) {
        std::fprintf(stderr,
                     "wingui_widget_galley failed: [%d] %s\n",
                     result.host_error_code,
                     result.message_utf8[0] ? result.message_utf8 : "unknown error");
        MessageBoxA(nullptr,
                    result.message_utf8[0] ? result.message_utf8 : "unknown error",
                    "wingui_widget_galley failed",
                    MB_ICONERROR | MB_OK);
        return result.host_error_code;
    }

    return exit_code;
}