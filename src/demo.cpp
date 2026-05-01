// demo.cpp — bouncing ball whose color and speed are controlled by live UI sliders.
//
// Layout (declared once):
//   ┌─────────────────────────┬─────────────────┐
//   │                         │  ○ Hue    [===] │
//   │   RGBA canvas pane      │  ○ Speed  [===] │
//   │   (animated GPU draw)   │                 │
//   │                         │  [  Clear  ]    │
//   └─────────────────────────┴─────────────────┘
//
// Frame callback draws into the canvas pane every tick.
// Event callback mutates state and calls rerender() — the layout engine
// diffs and sends a targeted JSON patch, not a full republish.

#include "wingui/app.hpp"
#include <numbers>
#include <cmath>
#include <random>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static wg::Color hue_to_color(double hue_deg, float alpha = 1.f) {
    // Simple HSV→RGB with S=V=1
    const float h = static_cast<float>(std::fmod(hue_deg, 360.0));
    const float s = 1.f, v = 1.f;
    const float c = v * s;
    const float x = c * (1.f - std::fabs(std::fmod(h / 60.f, 2.f) - 1.f));
    const float m = v - c;
    float r, g, b;
    if      (h <  60) { r = c; g = x; b = 0; }
    else if (h < 120) { r = x; g = c; b = 0; }
    else if (h < 180) { r = 0; g = c; b = x; }
    else if (h < 240) { r = 0; g = x; b = c; }
    else if (h < 300) { r = x; g = 0; b = c; }
    else              { r = c; g = 0; b = x; }
    return { r + m, g + m, b + m, alpha };
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main() {
    wg::Layout layout;

    // Mutable rendering state (accessed only from the client thread).
    double  ball_x      = 200.f;
    double  ball_y      = 150.f;
    double  vel_x       = 1.8;
    double  vel_y       = 1.3;
    bool    do_clear    = false;
    std::mt19937 rng{std::random_device{}()};
    std::uniform_real_distribution<double> bounce_jitter{-0.18, 0.18};

    wg::App{}
        .title("Demo — bouncing ball")
        .frame_rate(16)       // ~60 fps
        .auto_present()
        .layout(layout)

        // ---- Setup: publish the initial UI tree -------------------------
        .on_setup([&](SuperTerminalClientContext*) {
            layout.state().merge({
                { "hue",   45.0  },
                { "speed",  1.0  },
            });

            return layout.render([&] {
                const double hue   = layout.state().get("hue",   45.0 ).get<double>();
                const double speed = layout.state().get("speed",  1.0 ).get<double>();
                char hue_label[32];
                char speed_label[32];
                std::snprintf(hue_label, sizeof(hue_label), "Hue %.0f\xC2\xB0", hue);
                std::snprintf(speed_label, sizeof(speed_label), "Speed %.1f", speed);

                return wg::ui_window("Demo",
                    wg::ui_split_view("horizontal", "main-split",
                        wg::ui_split_pane("left-pane", {
                            wg::ui_rgba_pane("canvas", 640, 480, "canvas-input"),
                        }).size(0.75),
                        wg::ui_split_pane("right-pane", {
                            wg::ui_stack({
                                wg::ui_slider(hue_label,   hue,   "hue")
                                    .min_val(0).max_val(360).step(1),
                                wg::ui_slider(speed_label, speed, "speed")
                                    .min_val(0.1).max_val(5.0).step(0.1),
                                wg::ui_divider(),
                                wg::ui_button("Clear", "btn-clear"),
                            }),
                        }).size(0.25)
                    )
                );
            });
        })

        // ---- Events: mutate state and rerender --------------------------
        // App automatically calls push/pop_event_depth so rerenders
        // triggered here are deferred and coalesced to one patch.
        .on_event([&](const wg::Event& e) {
            if (e.is_close_requested()) {
                wg::request_stop(nullptr);
                return;
            }
            if (!e.is_native_ui()) return;

            const auto ev   = wg::ui_parse_event(e.native_ui().payload_json_utf8);
            const auto name = wg::ui_event_name(ev);

            if (name == "hue") {
                layout.state().set("hue", ev["value"]);
                layout.rerender();   // sends a targeted patch for just the slider
            } else if (name == "speed") {
                layout.state().set("speed", ev["value"]);
                layout.rerender();
            } else if (name == "btn-clear") {
                do_clear = true;     // consumed on next frame
            }
        })

        // ---- Frame: GPU drawing into the canvas pane --------------------
        .on_frame([&](wg::Frame& f) {
            auto pane = f.pane("canvas");
            if (!pane.valid()) return;

            const double hue   = layout.state().get("hue",   45.0).get<double>();
            const double speed = layout.state().get("speed",  1.0).get<double>();
            const auto   layout_px = pane.layout();
            const float  w  = static_cast<float>(layout_px.width);
            const float  h  = static_cast<float>(layout_px.height);
            const float  r  = 28.f;

            // Advance physics
            ball_x += vel_x * speed;
            ball_y += vel_y * speed;
            bool bounced_x = false;
            bool bounced_y = false;
            if (ball_x - r < 0)   { ball_x = r;     vel_x =  std::fabs(vel_x); bounced_x = true; }
            if (ball_x + r > w)   { ball_x = w - r; vel_x = -std::fabs(vel_x); bounced_x = true; }
            if (ball_y - r < 0)   { ball_y = r;     vel_y =  std::fabs(vel_y); bounced_y = true; }
            if (ball_y + r > h)   { ball_y = h - r; vel_y = -std::fabs(vel_y); bounced_y = true; }
            if (bounced_x || bounced_y) {
                const double velocity = std::hypot(vel_x, vel_y);
                if (velocity > 0.0001) {
                    const double jittered_angle = std::atan2(vel_y, vel_x) + bounce_jitter(rng);
                    vel_x = std::cos(jittered_angle) * velocity;
                    vel_y = std::sin(jittered_angle) * velocity;

                    const double min_axis_velocity = velocity * 0.18;
                    if (bounced_x) vel_x = std::copysign(std::max(std::fabs(vel_x), min_axis_velocity), vel_x);
                    if (bounced_y) vel_y = std::copysign(std::max(std::fabs(vel_y), min_axis_velocity), vel_y);

                    const double adjusted_velocity = std::hypot(vel_x, vel_y);
                    if (adjusted_velocity > 0.0001) {
                        vel_x *= velocity / adjusted_velocity;
                        vel_y *= velocity / adjusted_velocity;
                    }
                }
            }

            const wg::Color ball_color = hue_to_color(hue);
            const wg::Color trail_color = hue_to_color(hue + 30.0, 0.35f);
            const wg::Color bg = wg::Color::grey(0.07f);

            const bool clear_this_frame = do_clear;
            do_clear = false;

            wg::PrimitiveList prims;

            // Trail circle (slightly offset, faded)
            prims.add_circle_filled(
                static_cast<float>(ball_x) - static_cast<float>(vel_x * speed) * 3.f,
                static_cast<float>(ball_y) - static_cast<float>(vel_y * speed) * 3.f,
                r * 0.85f, trail_color);

            // Main ball
            prims.add_circle_filled(
                static_cast<float>(ball_x),
                static_cast<float>(ball_y),
                r, ball_color);

            // Specular highlight
            prims.add_circle_filled(
                static_cast<float>(ball_x) - r * 0.28f,
                static_cast<float>(ball_y) - r * 0.32f,
                r * 0.28f, wg::Color{1,1,1,0.45f});

            // Speed label in top-left
            const auto atlas = f.glyph_atlas_info();
            char label[64];
            std::snprintf(label, sizeof(label), "speed %.1f  hue %.0f\xc2\xb0",
                          speed, hue);
            prims.add_text(atlas, label, 8.f, 8.f, wg::Color::grey(0.7f));

            // Clear to dark background on first frame or when button pressed,
            // otherwise alpha-over so the trail accumulates slightly.
            const bool is_first = (f.index() == 0);
            pane.vector_draw(prims,
                wg::RgbaContentBufferMode::Persistent,
                WINGUI_RGBA_BLIT_ALPHA_OVER,
                /*clear_first=*/ is_first || clear_this_frame,
                bg);
        })

        .run();

    return 0;
}
