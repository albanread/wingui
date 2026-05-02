#include "wingui/app.hpp"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace {

using Json = wingui::Json;

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
    static const std::vector<std::pair<const char*, const char*>> tracked{{
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
        {"counter_up", "Button"},
    }};

    std::vector<wg::UiNode> rows;
    rows.reserve(tracked.size());
    for (const auto& entry : tracked) {
        char count_text[16];
        std::snprintf(count_text, sizeof(count_text), "x%d", hit_count(hits, entry.first));
        rows.push_back(wg::ui_row({
            wg::ui_badge(hit_count(hits, entry.first) > 0 ? "seen" : "idle"),
            wg::ui_text(std::string(entry.second) + " " + count_text),
        }).gap(8));
    }
    return rows;
}

std::string state_summary(const Json& state) {
    char numeric[128];
    std::snprintf(numeric,
                  sizeof(numeric),
                  "Age: %.0f\nIntensity: %.0f\nClicks: %d\nEnabled: %s\nTurbo: %s\n",
                  state.value("age", 37.0),
                  state.value("intensity", 42.0),
                  state.value("clicks", 0),
                  state.value("enabled", true) ? "true" : "false",
                  state.value("turbo", false) ? "true" : "false");

    std::string summary;
    summary += "Name: " + state.value("name", std::string("Ada Lovelace")) + "\n";
    summary += numeric;
    summary += "Date: " + state.value("meeting_date", std::string("2026-05-01")) + "\n";
    summary += "Time: " + state.value("meeting_time", std::string("13:45")) + "\n";
    summary += "Flavor: " + state.value("flavor", std::string("mint")) + "\n";
    summary += "Size: " + state.value("size", std::string("m")) + "\n";
    summary += "Plan: " + state.value("plan", std::string("pro")) + "\n";
    summary += "Table row: " + state.value("table_selected", std::string("row-choice")) + "\n";
    summary += "Tree node: " + state.value("tree_selected", std::string("inputs")) + "\n";
    summary += "Expanded: " + state.value("tree_expanded", Json::array()).dump();
    return summary;
}

std::string reconcile_mode_text(wingui::UiModel::ReconcileMode mode) {
    switch (mode) {
    case wingui::UiModel::ReconcileMode::None: return "none";
    case wingui::UiModel::ReconcileMode::FullPublishInitial: return "full-initial";
    case wingui::UiModel::ReconcileMode::FullPublishDiffFallback: return "full-diff-fallback";
    case wingui::UiModel::ReconcileMode::FullPublishNoPatchTransport: return "full-no-patch-transport";
    case wingui::UiModel::ReconcileMode::Patch: return "patch";
    case wingui::UiModel::ReconcileMode::NoChange: return "no-change";
    case wingui::UiModel::ReconcileMode::PublishFailed: return "publish-failed";
    case wingui::UiModel::ReconcileMode::PatchFailed: return "patch-failed";
    }
    return "unknown";
}

wg::UiWindow build_probe_window(const Json& state) {
    const std::string main_tab = state.value("main_tab", std::string("inputs"));
    const std::string name = state.value("name", std::string("Ada Lovelace"));
    const double age = state.value("age", 37.0);
    const std::string meeting_date = state.value("meeting_date", std::string("2026-05-01"));
    const std::string meeting_time = state.value("meeting_time", std::string("13:45"));
    const std::string notes = state.value("notes", std::string());
    const std::string rich_notes = state.value("rich_notes", std::string());
    const bool enabled = state.value("enabled", true);
    const bool turbo = state.value("turbo", false);
    const double intensity = state.value("intensity", 42.0);
    const std::string flavor = state.value("flavor", std::string("mint"));
    const std::string size = state.value("size", std::string("m"));
    const std::string plan = state.value("plan", std::string("pro"));
    const std::string table_selected = state.value("table_selected", std::string("row-choice"));
    const std::string tree_selected = state.value("tree_selected", std::string("inputs"));
    const Json tree_expanded = state.value("tree_expanded", Json::array());
    const int clicks = state.value("clicks", 0);
    const std::string last_event = state.value("last_event", std::string("No widget event yet."));
    const Json hits = state.value("hits", Json::object());

    int seen = 0;
    for (const char* key : {"main_tab", "name", "age", "meeting_date", "meeting_time", "notes", "rich_notes", "enabled", "turbo", "intensity", "counter_up"}) {
        if (hit_count(hits, key) > 0) ++seen;
    }

    char coverage_text[64];
    char total_text[64];
    char click_text[64];
    std::snprintf(coverage_text, sizeof(coverage_text), "Coverage %d/11", seen);
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
                wg::ui_select("Flavor", flavor, "flavor", make_options({{"mint", "Mint"}, {"citrus", "Citrus"}, {"cocoa", "Cocoa"}})),
                wg::ui_list_box("Size", size, "size", make_options({{"s", "Small"}, {"m", "Medium"}, {"l", "Large"}})),
                wg::ui_radio_group("Plan", plan, "plan", make_options({{"free", "Free"}, {"pro", "Pro"}, {"studio", "Studio"}})),
            }).gap(10),
        }),
    };

    std::vector<wg::UiNode> data_nodes = {
        wg::ui_card("Table", {
            wg::ui_table("Widget status", make_table_columns(), make_table_rows(), "table_selected").selected_id(table_selected),
        }),
        wg::ui_card("Tree", {
            wg::ui_tree_view("Widget map", make_tree_items(), tree_selected, tree_expanded, "tree_selected", "tree_toggle", "tree_activate"),
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
            wg::ui_text("Probe summary panel"),
        }),
        wg::ui_card("Last event", { wg::ui_text(last_event) }),
        wg::ui_card("Current state", { wg::ui_text(state_summary(state)) }),
        wg::ui_card("Event hits", build_hit_rows(hits)),
    };

    return wg::ui_window(
        "Widget Patch Probe",
        wg::ui_split_view(
            "horizontal",
            "widget-galley-split",
            wg::ui_split_pane("left", {
                wg::ui_stack({
                    wg::ui_heading("Interactive widget galley"),
                    wg::ui_text("Probe layout for patch reconciliation."),
                    wg::ui_tabs("Sections", main_tab, "main_tab", std::move(tabs), 840, 680),
                }).gap(10).padding(12),
            }).size(0.68),
            wg::ui_split_pane("right", {
                wg::ui_scroll_view(summary_nodes).id("summary-scroll").padding(10).gap(10),
            }).size(0.32)));
}

void initialise_state(wingui::UiState& state) {
    state.merge({
        {"main_tab", "inputs"},
        {"name", "Ada Lovelace"},
        {"age", 37.0},
        {"meeting_date", "2026-05-01"},
        {"meeting_time", "13:45"},
        {"notes", "Try each control and watch the coverage panel."},
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
        {"hits", Json::object()},
    });
}

void apply_event(wingui::UiState& state,
                 const std::string& event_name,
                 Json payload = Json(nullptr)) {
    Json event = Json::object();
    event["type"] = "ui-event";
    event["event"] = event_name;
    if (!payload.is_null()) {
        event["value"] = payload;
    }
    state.set("last_event", event.dump(2));

    Json hits = state.get("hits", Json::object());
    if (!hits.is_object()) hits = Json::object();
    hits[event_name] = hit_count(hits, event_name.c_str()) + 1;
    state.set("hits", hits);

    if (event_name == "counter_up") {
        state.set("clicks", state.get("clicks", 0).get<int>() + 1);
    } else if (event_name == "counter_reset") {
        state.set("clicks", 0);
        state.set("hits", Json::object());
    } else if (event_name == "tree_toggle") {
        if (payload.is_array()) {
            state.set("tree_expanded", payload);
        }
    } else if (event_name == "tree_selected") {
        if (payload.is_string()) {
            state.set("tree_selected", payload);
        }
    } else if (!payload.is_null()) {
        state.set(event_name, payload);
    }
}

std::string summarise_patch_doc(const std::string& patch_json) {
    Json patch = Json::parse(patch_json, nullptr, false);
    if (!patch.is_object() || !patch.contains("ops") || !patch["ops"].is_array()) {
        return "<invalid patch doc>";
    }

    std::string out;
    for (const auto& op : patch["ops"]) {
        if (!op.is_object()) continue;
        const std::string kind = op.value("op", std::string("?"));
        const std::string id = op.value("id", std::string());
        out += kind;
        if (!id.empty()) out += " id=" + id;
        if (op.contains("props") && op["props"].is_object()) {
            out += " props=";
            bool first = true;
            for (auto it = op["props"].begin(); it != op["props"].end(); ++it) {
                if (!first) out += ",";
                out += it.key();
                first = false;
            }
        }
        out += "\n";
    }
    return out;
}

struct Scenario {
    std::string name;
    std::function<void(wingui::UiState&)> mutate;
};

std::vector<Scenario> build_scenarios() {
    return {
        {"text-input:name", [](wingui::UiState& s) { apply_event(s, "name", "Grace Hopper"); }},
        {"number-input:age", [](wingui::UiState& s) { apply_event(s, "age", 52.0); }},
        {"date-picker:meeting_date", [](wingui::UiState& s) { apply_event(s, "meeting_date", "2026-05-17"); }},
        {"time-picker:meeting_time", [](wingui::UiState& s) { apply_event(s, "meeting_time", "15:20"); }},
        {"textarea:notes", [](wingui::UiState& s) { apply_event(s, "notes", "Textarea mutation from probe."); }},
        {"rich-text:rich_notes", [](wingui::UiState& s) { apply_event(s, "rich_notes", wingui::ui_rtf_from_html("<p><i>Probe</i> rich text mutation.</p>")); }},
        {"checkbox:enabled", [](wingui::UiState& s) { apply_event(s, "enabled", false); }},
        {"switch:turbo", [](wingui::UiState& s) { apply_event(s, "turbo", true); }},
        {"slider:intensity", [](wingui::UiState& s) { apply_event(s, "intensity", 77.0); }},
        {"button:counter_up", [](wingui::UiState& s) { apply_event(s, "counter_up"); }},
        {"tabs:main_tab", [](wingui::UiState& s) { apply_event(s, "main_tab", "choices"); }},
    };
}

std::filesystem::path executable_directory() {
    wchar_t path_buffer[MAX_PATH];
    const DWORD length = GetModuleFileNameW(nullptr, path_buffer, MAX_PATH);
    if (length == 0 || length == MAX_PATH) {
        return std::filesystem::current_path();
    }
    return std::filesystem::path(path_buffer).parent_path();
}

SuperTerminalNativeUiPatchMetrics read_native_metrics(SuperTerminalClientContext* ctx) {
    SuperTerminalNativeUiPatchMetrics metrics{};
    if (ctx) {
        super_terminal_get_native_ui_patch_metrics(ctx, &metrics);
    }
    return metrics;
}

uint64_t metric_delta(uint64_t current, uint64_t previous) {
    return current >= previous ? (current - previous) : 0;
}

struct PendingScenarioResult {
    std::string name;
    bool ok = false;
    wingui::UiModel::ReconcileMode mode = wingui::UiModel::ReconcileMode::None;
    size_t patch_ops = 0;
    uint64_t full_publish_count = 0;
    uint64_t patch_send_count = 0;
    uint64_t diff_fallback_count = 0;
    uint64_t publish_failure_count = 0;
    uint64_t patch_failure_count = 0;
    std::string publish_json;
    std::string patch_json;
    SuperTerminalNativeUiPatchMetrics native_before{};
    uint64_t applied_ms = 0;
};

struct ProbeRunner {
    wg::Layout layout;
    SuperTerminalClientContext* ctx = nullptr;
    std::ofstream log;
    std::vector<Scenario> scenarios = build_scenarios();
    std::string last_publish_json;
    std::string last_patch_json;
    size_t next_scenario_index = 0;
    bool initial_native_logged = false;
    bool waiting_for_native = false;
    uint64_t next_apply_ms = 250;
    uint64_t stop_after_ms = 0;
    PendingScenarioResult pending;

    Json snapshot_state() const {
        Json state = Json::object();
        for (const auto& [key, value] : layout.state().entries()) {
            state[key] = value;
        }
        return state;
    }

    bool setup(SuperTerminalClientContext* setup_ctx) {
        ctx = setup_ctx;
        initialise_state(layout.state());

        const std::filesystem::path log_path = executable_directory() / "patch_probe.log";
        log.open(log_path, std::ios::trunc);
        if (!log.is_open()) {
            return false;
        }

        layout.model().set_publish_fn([this, setup_ctx](const std::string& json_utf8) {
            last_publish_json = json_utf8;
            return super_terminal_publish_ui_json(setup_ctx, json_utf8.c_str()) != 0;
        });
        layout.model().set_patch_fn([this, setup_ctx](const std::string& json_utf8) {
            last_patch_json = json_utf8;
            return super_terminal_patch_ui_json(setup_ctx, json_utf8.c_str()) != 0;
        });

        const bool ok = layout.render([this]() {
            return build_probe_window(snapshot_state());
        });

        log << "Widget patch probe (native host)\n";
        log << "Initial reconcile mode: " << reconcile_mode_text(layout.model().last_reconcile_mode()) << "\n";
        log << "Initial publish bytes: " << last_publish_json.size() << "\n";
        log << "Scenario count: " << scenarios.size() << "\n\n";
        log.flush();
        return ok;
    }

    void write_initial_native_metrics() {
        if (initial_native_logged || !ctx || !log.is_open()) return;
        const SuperTerminalNativeUiPatchMetrics metrics = read_native_metrics(ctx);
        log << "Initial native metrics\n";
        log << "publish_count: " << metrics.publish_count << "\n";
        log << "patch_request_count: " << metrics.patch_request_count << "\n";
        log << "direct_apply_count: " << metrics.direct_apply_count << "\n";
        log << "subtree_rebuild_count: " << metrics.subtree_rebuild_count << "\n";
        log << "window_rebuild_count: " << metrics.window_rebuild_count << "\n";
        log << "resize_reject_count: " << metrics.resize_reject_count << "\n";
        log << "failed_patch_count: " << metrics.failed_patch_count << "\n\n";
        log.flush();
        initial_native_logged = true;
    }

    void apply_next_scenario(uint64_t elapsed_ms) {
        if (waiting_for_native || next_scenario_index >= scenarios.size()) return;

        const Scenario& scenario = scenarios[next_scenario_index++];
        last_publish_json.clear();
        last_patch_json.clear();

        pending = {};
        pending.name = scenario.name;
        pending.native_before = read_native_metrics(ctx);
        scenario.mutate(layout.state());
        pending.ok = layout.rerender();
        pending.mode = layout.model().last_reconcile_mode();
        pending.patch_ops = layout.model().last_patch_op_count();
        pending.full_publish_count = layout.model().full_publish_count();
        pending.patch_send_count = layout.model().patch_send_count();
        pending.diff_fallback_count = layout.model().diff_fallback_count();
        pending.publish_failure_count = layout.model().publish_failure_count();
        pending.patch_failure_count = layout.model().patch_failure_count();
        pending.publish_json = last_publish_json;
        pending.patch_json = last_patch_json;
        pending.applied_ms = elapsed_ms;
        waiting_for_native = true;
        next_apply_ms = elapsed_ms + 300;
        super_terminal_request_present(ctx);
    }

    void finalize_pending_scenario(uint64_t elapsed_ms) {
        if (!waiting_for_native || !log.is_open()) return;
        const SuperTerminalNativeUiPatchMetrics native_after = read_native_metrics(ctx);

        log << "=== " << pending.name << " ===\n";
        log << "ok: " << (pending.ok ? "true" : "false") << "\n";
        log << "model_mode: " << reconcile_mode_text(pending.mode) << "\n";
        log << "model_patch_ops: " << pending.patch_ops << "\n";
        log << "full_publish_count: " << pending.full_publish_count << "\n";
        log << "patch_send_count: " << pending.patch_send_count << "\n";
        log << "diff_fallback_count: " << pending.diff_fallback_count << "\n";
        log << "publish_failure_count: " << pending.publish_failure_count << "\n";
        log << "patch_failure_count: " << pending.patch_failure_count << "\n";
        log << "publish_bytes: " << pending.publish_json.size() << "\n";
        log << "patch_bytes: " << pending.patch_json.size() << "\n";
        log << "native_delta.publish_count: " << metric_delta(native_after.publish_count, pending.native_before.publish_count) << "\n";
        log << "native_delta.patch_request_count: " << metric_delta(native_after.patch_request_count, pending.native_before.patch_request_count) << "\n";
        log << "native_delta.direct_apply_count: " << metric_delta(native_after.direct_apply_count, pending.native_before.direct_apply_count) << "\n";
        log << "native_delta.subtree_rebuild_count: " << metric_delta(native_after.subtree_rebuild_count, pending.native_before.subtree_rebuild_count) << "\n";
        log << "native_delta.window_rebuild_count: " << metric_delta(native_after.window_rebuild_count, pending.native_before.window_rebuild_count) << "\n";
        log << "native_delta.resize_reject_count: " << metric_delta(native_after.resize_reject_count, pending.native_before.resize_reject_count) << "\n";
        log << "native_delta.failed_patch_count: " << metric_delta(native_after.failed_patch_count, pending.native_before.failed_patch_count) << "\n";
        log << "elapsed_ms: " << elapsed_ms << "\n";
        if (!pending.patch_json.empty()) {
            log << "patch_summary:\n" << summarise_patch_doc(pending.patch_json);
        }
        log << "\n";
        log.flush();

        waiting_for_native = false;
        if (next_scenario_index >= scenarios.size()) {
            stop_after_ms = elapsed_ms + 1500;
        }
    }

    void on_frame(wg::Frame& frame) {
        write_initial_native_metrics();

        const uint64_t elapsed_ms = frame.elapsed_ms();
        if (waiting_for_native && elapsed_ms >= pending.applied_ms + 150) {
            finalize_pending_scenario(elapsed_ms);
        }
        if (!waiting_for_native && next_scenario_index < scenarios.size() && elapsed_ms >= next_apply_ms) {
            apply_next_scenario(elapsed_ms);
        }
        if (stop_after_ms != 0 && elapsed_ms >= stop_after_ms) {
            wg::request_stop(ctx, 0);
        }
    }

    void shutdown() {
        if (log.is_open()) {
            log << "Probe shutdown\n";
            log.flush();
        }
    }
};

} // namespace

int main() {
    ProbeRunner runner;
    wg::App app;
    SuperTerminalRunResult result{};

    return app
        .title("Widget Patch Probe")
        .layout(runner.layout)
        .frame_rate(16)
        .auto_present(true)
        .on_setup([&](SuperTerminalClientContext* ctx) {
            return runner.setup(ctx);
        })
        .on_frame([&](wg::Frame& frame) {
            runner.on_frame(frame);
        })
        .on_event([&](const wg::Event& event) {
            if (event.is_close_requested()) {
                wg::request_stop(runner.ctx, 0);
            }
        })
        .on_shutdown([&]() {
            runner.shutdown();
        })
        .run(&result);
}