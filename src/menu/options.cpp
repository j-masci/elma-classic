#include "menu/options.h"
#include "eol/settings.h"
#include "game/state.h"
#include "main.h"
#include "menu/ball.h"
#include "menu/controls.h"
#include "menu/dialog.h"
#include "menu/nav.h"
#include "menu/pic.h"
#include "menu/player.h"
#include "pic/surface.h"
#include "physics/init.h"
#include "physics/killer_shot.h"
#include "physics/rain.h"
#include "physics/projectile.h"
#include "platform/implementation.h"
#include "renderer/canvas.h"
#include "util/file_iter.h"
#include <cmath>
#include <cstring>
#include <format>
#include <initializer_list>

void menu_about() {
    BallSpeed = 10.0;

    menu_nav nav("Elasto Mania Online " ELMA_VERSION);

    int i = 1;
    constexpr int DY = 60;
    nav.add_overlay("https://elma.online", 320, nav.y_title + DY * i++, OverlayAlignment::Centered,
                    ScreenAnchor::Center);
    nav.add_overlay("Find Our Community on Discord!", 320, nav.y_title + DY * i++,
                    OverlayAlignment::Centered, ScreenAnchor::Center);
    nav.add_overlay("Thank You to Our Contributors!", 320, nav.y_title + DY * i++,
                    OverlayAlignment::Centered, ScreenAnchor::Center);
    nav.y_entries = nav.y_title + DY * i++;

    // Alphabetical
    nav.add_row("bene", "", NAV_FUNC(){});
    nav.add_row("joey", "", NAV_FUNC(){});
    nav.add_row("Kopaka", "", NAV_FUNC(){});
    nav.add_row("Markku", "", NAV_FUNC(){});
    nav.add_row("sunl", "", NAV_FUNC(){});

    // Alphabetical
    nav.add_row("Hibernatus", "", NAV_FUNC(){});
    nav.add_row("milagros", "", NAV_FUNC(){});

    // Alphabetical
    nav.add_row("amarshalov", "", NAV_FUNC(){});
    nav.add_row("Smibu", "", NAV_FUNC(){});
    nav.add_row("Zweq", "", NAV_FUNC(){});

    nav.navigate();

    BallSpeed = 1.0;
}

void menu_help() {
    menu_pic menu;

    int x1 = 90;
    int x2 = 220;
    int y0 = 80;
    int dy = 32;
    menu.add_line_centered("Default controls:", 320, 20);

    menu.add_line("UP", x1, y0);
    menu.add_line("- Accelerate", x2, y0);

    menu.add_line("DOWN", x1, y0 + dy);
    menu.add_line("- Block Wheels", x2, y0 + dy);

    menu.add_line("LEFT", x1, y0 + dy * 2);
    menu.add_line("- Rotate AntiClockwise", x2, y0 + dy * 2);

    menu.add_line("RIGHT", x1, y0 + dy * 3);
    menu.add_line("- Rotate Clockwise", x2, y0 + dy * 3);

    menu.add_line("SPACE", x1, y0 + dy * 4);
    menu.add_line("- Turn Around", x2, y0 + dy * 4);

    menu.add_line("V", x1, y0 + dy * 5);
    menu.add_line("- View Box Toggle", x2, y0 + dy * 5);

    menu.add_line("T", x1, y0 + dy * 6);
    menu.add_line("- Time Display Toggle", x2, y0 + dy * 6);

    menu.add_line_centered("After you have eaten all the fruits,", 320, y0 + dy * 9);
    menu.add_line_centered("touch the flower!", 320, y0 + dy * 10);

    menu.loop();
}

static void menu_lgr() {
    menu_nav nav("Pick an LGR!");
    nav.search_pattern = SearchPattern::Sorted;

    finame filename;
    bool done = find_first("lgr/*.lgr", filename, MAX_FILENAME_LEN);
    while (!done) {
        constexpr int LGR_EXT_LEN = 4;
        int len = strlen(filename);
        std::string lgrname = std::string(filename, len - LGR_EXT_LEN);

        nav.add_row(lgrname, NAV_FUNC() { EolSettings->persist_default_lgr_name(left); });

        done = find_next(filename);
    }
    find_close();

    nav.sort_rows();
    nav.select_row(EolSettings->default_lgr_name_persisted());

    nav.navigate();
}

struct resolution {
    int width;
    int height;
};

static constexpr resolution RESOLUTIONS[] = {
    {800, 600},  {1024, 768},  {1280, 720},  {1280, 960},  {1280, 1024}, {1366, 768},  {1440, 900},
    {1600, 900}, {1600, 1200}, {1920, 1080}, {1920, 1200}, {2560, 1440}, {3840, 1600}, {3840, 2160},
};

static void menu_resolution() {
    menu_nav nav("Pick a resolution!");

    if (EolSettings->fullscreen_persisted() == FullscreenMode::Fullscreen) {
        auto display_modes = platform_get_display_modes();
        for (const auto& [w, h] : display_modes) {
            std::string label = std::format("{}x{}", w, h);
            nav.add_row(label, NAV_FUNC(w, h) { update_resolution(w, h); });
        }
    } else {
        auto [desktop_w, desktop_h] = platform_get_desktop_resolution();
        for (const auto& res : RESOLUTIONS) {
            if (res.width > desktop_w || res.height > desktop_h) {
                continue;
            }
            std::string label = std::format("{}x{}", res.width, res.height);
            nav.add_row(label, NAV_FUNC(&res) { update_resolution(res.width, res.height); });
        }
    }

    std::string current = std::format("{}x{}", EolSettings->screen_width_persisted(),
                                      EolSettings->screen_height_persisted());
    nav.select_row(current);

    nav.navigate();
}

static const char* fullscreen_mode_label(FullscreenMode mode) {
    switch (mode) {
    case FullscreenMode::Windowed:
        return "Off";
    case FullscreenMode::Fullscreen:
        return "Exclusive";
    case FullscreenMode::FullscreenDesktop:
        return "Fullscreen (Desktop)";
    }
    return "";
}

static void menu_fullscreen() {
    menu_nav nav("Pick a fullscreen mode!");

    constexpr FullscreenMode MODES[] = {
        FullscreenMode::Windowed,
        FullscreenMode::Fullscreen,
        FullscreenMode::FullscreenDesktop,
    };

    for (auto mode : MODES) {
        nav.add_row(
            fullscreen_mode_label(mode), NAV_FUNC(mode) { EolSettings->persist_fullscreen(mode); });
    }

    nav.select_row(fullscreen_mode_label(EolSettings->fullscreen_persisted()));

    nav.navigate();
}

#define BOOL_OPTION(text, setting)                                                                 \
    nav.add_row(                                                                                   \
        text, EolSettings->setting##_persisted() ? "Yes" : "No",                                   \
        NAV_FUNC() { EolSettings->persist_##setting(!EolSettings->setting##_persisted()); });

static void menu_cripples() {
    int choice = 0;
    while (true) {
        menu_nav nav("Cripples");
        nav.select_row(choice);
        nav.x_left = 0;
        nav.x_right = 390;

        BOOL_OPTION("No Brake:", cripple_no_brake);
        BOOL_OPTION("No Throttle:", cripple_no_throttle);
        BOOL_OPTION("Always Throttle:", cripple_always_throttle);
        BOOL_OPTION("No Turn:", cripple_no_turn);
        BOOL_OPTION("One Turn:", cripple_one_turn);
        BOOL_OPTION("No Volt:", cripple_no_volt);
        BOOL_OPTION("Drunk:", cripple_drunk);
        BOOL_OPTION("One Wheel:", cripple_one_wheel);
        BOOL_OPTION("Show One Wheel Status:", show_one_wheel_status);

        choice = nav.navigate();

        if (choice < 0) {
            return;
        }
    }
}

// Lightweight, session-only controls: reuse the existing Enter-to-cycle menu
// rather than adding experimental values to the persisted/network settings API.
static double next_experiment_value(double value, std::initializer_list<double> choices) {
    for (double choice : choices) {
        if (choice > value + 0.0001) {
            return choice;
        }
    }
    return *choices.begin();
}

static void menu_experiments() {
    int selected = 0;
    while (true) {
        menu_nav nav("Experiments (this session)");
        nav.x_left = 0;
        nav.x_right = 390;
        nav.y_entries = 77;
        nav.dy = 28;
        nav.select_row(selected);
        nav.add_row("Rain / metre / second:", std::format("{:.2f}", rain::settings().frequency), NAV_FUNC() {
            auto& rate = rain::settings().frequency;
            rate = next_experiment_value(rate, {0.0, 0.25, 0.5, 1.0, 2.0, 5.0, 10.0});
        });
        nav.add_row("Clear rain", NAV_FUNC() { rain::clear(); });
        nav.add_row("Water resistance:", std::format("{:.0f}%", rain::settings().water_resistance*100), NAV_FUNC() {
            auto& resistance=rain::settings().water_resistance;
            resistance=next_experiment_value(resistance,{0.0,0.15,0.35,0.5,0.75,1.0});
        });
        nav.add_row("Traction (9/0):", std::format("{:.1f}", SurfaceGrip), NAV_FUNC() {
            set_surface_grip(SurfaceGrip >= 1.0 - 1e-9 ? 0.0 : SurfaceGrip + 0.1);
        });
        nav.add_row("Wheel size (O/P):", std::format("{:.1f}x", WheelSizeScale), NAV_FUNC() {
            set_wheel_size_scale(next_experiment_value(WheelSizeScale,
                                                       {0.5, 0.8, 1.0, 1.2, 1.5, 2.0, 2.5, 3.0}));
        });
        nav.add_row("Throttle (Ctrl O/P):", std::format("{:.2f}x", ThrottlePowerScale), NAV_FUNC() {
            set_throttle_power_scale(next_experiment_value(ThrottlePowerScale,
                                                           {0.25, 0.5, 1.0, 1.5, 2.0, 3.0, 4.0, 5.0}));
        });
        nav.add_row("Shot speed:", std::format("{:.1f}", projectile::settings().speed), NAV_FUNC() {
            auto& s = projectile::settings();
            s.speed = next_experiment_value(s.speed, {3.0, 4.5, 6.2, 8.0, 10.0, 15.0});
        });
        nav.add_row("Shot gravity:", std::format("{:.1f}", projectile::settings().gravity), NAV_FUNC() {
            auto& s = projectile::settings();
            s.gravity = next_experiment_value(s.gravity, {0.0, 2.5, 5.0, 10.0, 15.0, 20.0});
        });
        nav.add_row("Shot angle:", std::format("{:.0f} degrees", projectile::settings().elevation_degrees),
                    NAV_FUNC() {
                        auto& s = projectile::settings();
                        s.elevation_degrees = next_experiment_value(s.elevation_degrees,
                                                                    {0.0, 10.0, 15.0, 25.0, 35.0, 45.0});
                    });
        nav.add_row("Shot delay:", std::format("{:.1f}s", projectile::settings().delay_seconds), NAV_FUNC() {
            auto& s = projectile::settings();
            s.delay_seconds = next_experiment_value(s.delay_seconds, {0.1, 0.3, 0.4, 0.5, 0.8, 1.0});
        });
        nav.add_row("Square collisions:", projectile::settings().collide_with_squares ? "Stop" : "Pass through",
                    NAV_FUNC() {
                        auto& s = projectile::settings();
                        s.collide_with_squares = !s.collide_with_squares;
                        projectile::reset(); // Do not enable solidity on already-overlapping shots.
                    });
        nav.add_row("Killer speed:", std::format("{:.0f}", killer_shot::settings().speed), NAV_FUNC() {
            auto& s = killer_shot::settings();
            s.speed = next_experiment_value(s.speed, {12.0, 18.0, 24.0, 32.0, 48.0});
        });
        nav.add_row("Killer delay:", std::format("{:.2f}s", killer_shot::settings().delay_seconds), NAV_FUNC() {
            auto& s = killer_shot::settings();
            s.delay_seconds = next_experiment_value(s.delay_seconds, {0.1, 0.15, 0.25, 0.4, 0.5, 1.0});
        });
        nav.add_row("Reset experiments", NAV_FUNC() {
            set_wheel_size_scale(1.0);
            set_throttle_power_scale(1.0);
            set_surface_grip(1.0);
            rain::settings() = rain::Settings{};
            rain::clear();
            projectile::settings() = projectile::Settings{};
            projectile::reset();
            killer_shot::settings() = killer_shot::Settings{};
            killer_shot::rotate_aim(10.0 - killer_shot::aim_degrees());
            killer_shot::reset();
        });
        selected = nav.navigate();
        if (selected < 0) {
            return;
        }
    }
}

void menu_options() {
    menu_nav nav("Options");
    nav.x_left = 0;
    nav.x_right = 390;
    nav.y_entries = 77;
    nav.dy = 36;
    nav.search_pattern = SearchPattern::Filter;

    int choice = 0;
    while (true) {
        nav.clear_entries();
        nav.select_row(choice);

        nav.add_row(
            "Play mode:", State->single ? "Single Player" : "Multiplayer",
            NAV_FUNC() { State->single = !State->single; });

        if (!State->single) {
            nav.add_row(
                "Flag Tag:", State->flag_tag ? "On" : "Off",
                NAV_FUNC() { State->flag_tag = !State->flag_tag; });
        }

        nav.add_row("Player A:", State->player1, NAV_FUNC() { menu_player_choose(true, true); });

        nav.add_row("Player B:", State->player2, NAV_FUNC() { menu_player_choose(false, true); });

        nav.add_row(
            "Sound:", State->sound_on ? "Enabled" : "Disabled", NAV_FUNC() {
                if (State->sound_on) {
                    close_sound_device();
                    State->sound_on = 0;
                } else {
                    open_sound_device();
                    State->sound_on = 1;
                }
            });

        nav.add_row(
            "Animated Menus:", State->animated_menus ? "Yes" : "No",
            NAV_FUNC() { State->animated_menus = !State->animated_menus; });

        nav.add_row(
            "Video Detail:", State->high_quality ? "High" : "Low", NAV_FUNC() {
                State->high_quality = !State->high_quality;
                canvas::invalidate_canvases();
            });

        nav.add_row(
            "Animated Objects:", State->animated_objects ? "Yes" : "No",
            NAV_FUNC() { State->animated_objects = !State->animated_objects; });

        BOOL_OPTION("Still Objects:", still_objects);

        BOOL_OPTION("Access all internals:", all_internals_accessible);

        nav.add_row(
            "Swap Bikes:", State->player1_bike1 ? "No" : "Yes",
            NAV_FUNC() { State->player1_bike1 = !State->player1_bike1; });

        nav.add_row("Customize Controls ...", NAV_FUNC() { menu_customize_controls(); });

        nav.add_row("Cripples ...", NAV_FUNC() { menu_cripples(); });
        nav.add_row("Experiments ...", NAV_FUNC() { menu_experiments(); });

        BOOL_OPTION("Pics In Background:", pictures_in_background);

        BOOL_OPTION("Override Ground:", default_ground);
        BOOL_OPTION("Override Sky:", default_sky);

        BOOL_OPTION("Centered Camera:", center_camera);
        BOOL_OPTION("Centered Minimap:", center_map);

        nav.add_row(
            "Minimap Alignment:",
            [] {
                switch (EolSettings->map_alignment_persisted()) {
                case MapAlignment::None:
                    return "None";
                case MapAlignment::Left:
                    return "Left";
                case MapAlignment::Middle:
                    return "Middle";
                case MapAlignment::Right:
                    return "Right";
                }
                return "";
            }(),
            NAV_FUNC() {
                switch (EolSettings->map_alignment_persisted()) {
                case MapAlignment::None:
                    EolSettings->persist_map_alignment(MapAlignment::Left);
                    return;
                case MapAlignment::Left:
                    EolSettings->persist_map_alignment(MapAlignment::Middle);
                    return;
                case MapAlignment::Middle:
                    EolSettings->persist_map_alignment(MapAlignment::Right);
                    return;
                case MapAlignment::Right:
                    EolSettings->persist_map_alignment(MapAlignment::None);
                    return;
                }
            });

        nav.add_row(
            "Minimap Size:",
            std::format("{}x{}", EolSettings->minimap_width_persisted(),
                        EolSettings->minimap_height_persisted()),
            NAV_FUNC() {
                int w = EolSettings->minimap_width_persisted();
                if (w == 140) {
                    EolSettings->persist_minimap_width(180);
                    EolSettings->persist_minimap_height(90);
                } else if (w == 180) {
                    EolSettings->persist_minimap_width(220);
                    EolSettings->persist_minimap_height(110);
                } else if (w == 220) {
                    EolSettings->persist_minimap_width(280);
                    EolSettings->persist_minimap_height(140);
                } else if (w == 280) {
                    EolSettings->persist_minimap_width(350);
                    EolSettings->persist_minimap_height(175);
                } else if (w == 350) {
                    EolSettings->persist_minimap_width(420);
                    EolSettings->persist_minimap_height(210);
                } else {
                    EolSettings->persist_minimap_width(140);
                    EolSettings->persist_minimap_height(70);
                }
            });

        nav.add_row(
            "Minimap Opacity:", std::format("{}%", EolSettings->minimap_opacity_persisted()),
            NAV_FUNC() {
                int opacity = EolSettings->minimap_opacity_persisted();
                if (opacity == 25) {
                    EolSettings->persist_minimap_opacity(50);
                } else if (opacity == 50) {
                    EolSettings->persist_minimap_opacity(75);
                } else if (opacity == 75) {
                    EolSettings->persist_minimap_opacity(100);
                } else {
                    EolSettings->persist_minimap_opacity(25);
                }
            });

        nav.add_row(
            "Resolution:",
            std::format("{}x{}", EolSettings->screen_width_persisted(),
                        EolSettings->screen_height_persisted()),
            NAV_FUNC() {
                if (EolSettings->fullscreen_persisted() == FullscreenMode::FullscreenDesktop) {
                    menu_dialog("Resolution is locked to desktop", "in Fullscreen (Desktop) mode.");
                    return;
                }
                menu_resolution();
            });

        nav.add_row(
            "Zoom:", std::format("{:.2f}", EolSettings->zoom_persisted()), NAV_FUNC() {
                double old_zoom = EolSettings->zoom_persisted();
                EolSettings->persist_zoom(old_zoom + 0.25);
                if (old_zoom == EolSettings->zoom_persisted()) {
                    EolSettings->persist_zoom(0.25);
                }
            });

        nav.add_row(
            "Minimap Zoom:", std::format("{:.2f}", EolSettings->minimap_zoom_persisted()),
            NAV_FUNC() {
                double old_zoom = EolSettings->minimap_zoom_persisted();
                EolSettings->persist_minimap_zoom(old_zoom + 0.25);
                if (old_zoom == EolSettings->minimap_zoom_persisted()) {
                    EolSettings->persist_minimap_zoom(0.25);
                }
            });

        BOOL_OPTION("Zoom Textures:", zoom_textures);

        BOOL_OPTION("Zoom Grass:", zoom_grass);

        nav.add_row(
            "Renderer:",
            [] {
                switch (EolSettings->renderer_persisted()) {
                case RendererType::Software:
                    return "Software";
                case RendererType::OpenGL:
                    return "OpenGL";
                }
                return "";
            }(),
            NAV_FUNC() {
                switch (EolSettings->renderer_persisted()) {
                case RendererType::Software:
                    EolSettings->persist_renderer(RendererType::OpenGL);
                    return;
                case RendererType::OpenGL:
                    EolSettings->persist_renderer(RendererType::Software);
                    return;
                }
            });

        nav.add_row(
            "Fullscreen:", fullscreen_mode_label(EolSettings->fullscreen_persisted()),
            NAV_FUNC() { menu_fullscreen(); });

        nav.add_row(
            "Turn Time:",
            [] {
                if (EolSettings->turn_time_persisted() == 0.0) {
                    return std::string("Instant");
                }
                return std::format("{:.2f}s", EolSettings->turn_time_persisted());
            }(),
            NAV_FUNC() {
                double old_turn_time = EolSettings->turn_time_persisted();
                double new_turn_time = std::round((old_turn_time - 0.10) * 100.0) / 100.0;
                EolSettings->persist_turn_time(new_turn_time);
                if (old_turn_time == EolSettings->turn_time_persisted()) {
                    EolSettings->persist_turn_time(0.35);
                }
            });

        BOOL_OPTION("LCtrl search:", lctrl_search);

        nav.add_row(
            "Default LGR:", EolSettings->default_lgr_name_persisted(), NAV_FUNC() { menu_lgr(); });

        BOOL_OPTION("Fancyboost LGRs:", fancyboost);

        BOOL_OPTION("Show Apple Time:", show_last_apple_time);
        BOOL_OPTION("Show Lev Name In Rec:", show_lev_name_in_rec);
        BOOL_OPTION("Gravity Arrows:", show_gravity_arrows);

        BOOL_OPTION("Show FPS:", show_fps);
        BOOL_OPTION("Show UPS:", show_ups);

        nav.add_row(
            "FPS Limit:",
            EolSettings->fps_limit_enabled_persisted()
                ? std::to_string(EolSettings->fps_limit_persisted())
                : std::string("Off"),
            NAV_FUNC() {
                static constexpr int steps[] = {60, 100, 144, 240, 500};
                if (!EolSettings->fps_limit_enabled_persisted()) {
                    EolSettings->persist_fps_limit_enabled(true);
                    EolSettings->persist_fps_limit(steps[0]);
                    return;
                }
                int cur = EolSettings->fps_limit_persisted();
                for (size_t i = 0; i < std::size(steps); ++i) {
                    if (steps[i] == cur && i + 1 < std::size(steps)) {
                        EolSettings->persist_fps_limit(steps[i + 1]);
                        return;
                    }
                }
                EolSettings->persist_fps_limit_enabled(false);
                EolSettings->persist_fps_limit(steps[0]);
            });

        nav.add_row(
            "Record Replay FPS:", std::to_string(EolSettings->recording_fps_persisted()),
            NAV_FUNC() {
                int old_fps = EolSettings->recording_fps_persisted();
                int new_fps;
                if (old_fps == 30) {
                    new_fps = 60;
                } else if (old_fps == 60) {
                    new_fps = 120;
                } else {
                    new_fps = 30;
                }
                EolSettings->persist_recording_fps(new_fps);
            });

        BOOL_OPTION("Show Total Time:", show_total_time);
        BOOL_OPTION("Demo menu:", show_demo_menu);
        BOOL_OPTION("Help menu:", show_help_menu);
        BOOL_OPTION("About menu:", show_about_menu);
        BOOL_OPTION("Best Times menu:", show_best_times_menu);
        BOOL_OPTION("Skip Intro:", skip_intro);

        nav.add_row(
            "Show chat:",
            [] {
                switch (EolSettings->chat_visibility_persisted()) {
                case ChatVisibility::Shown:
                    return "Yes";
                case ChatVisibility::PublicHidden:
                    return "Public hidden";
                case ChatVisibility::Hidden:
                    return "No";
                }
                return "";
            }(),
            NAV_FUNC() {
                switch (EolSettings->chat_visibility_persisted()) {
                case ChatVisibility::Shown:
                    EolSettings->persist_chat_visibility(ChatVisibility::PublicHidden);
                    return;
                case ChatVisibility::PublicHidden:
                    EolSettings->persist_chat_visibility(ChatVisibility::Hidden);
                    return;
                case ChatVisibility::Hidden:
                    EolSettings->persist_chat_visibility(ChatVisibility::Shown);
                    return;
                }
            });

        nav.add_row(
            "Num Chat Lines:", std::format("{}", EolSettings->chat_lines_persisted()), NAV_FUNC() {
                int old_chat_lines = EolSettings->chat_lines_persisted();
                EolSettings->persist_chat_lines(old_chat_lines + 1);
                if (old_chat_lines == EolSettings->chat_lines_persisted()) {
                    EolSettings->persist_chat_lines(1);
                }
            });

        BOOL_OPTION("Show others:", show_others);
        BOOL_OPTION("Show battle status:", show_battle_status);
        BOOL_OPTION("Show battle leader:", show_battle_leader);
        BOOL_OPTION("Show speedometer:", show_speedometer);

        nav.add_row(
            "Table Alignment:",
            [] {
                switch (EolSettings->table_alignment_persisted()) {
                case eol_table::Align::Left:
                    return "Left";
                case eol_table::Align::Center:
                    return "Center";
                case eol_table::Align::Right:
                    return "Right";
                }
                return "";
            }(),
            NAV_FUNC() {
                switch (EolSettings->table_alignment_persisted()) {
                case eol_table::Align::Left:
                    EolSettings->persist_table_alignment(eol_table::Align::Center);
                    return;
                case eol_table::Align::Center:
                    EolSettings->persist_table_alignment(eol_table::Align::Right);
                    return;
                case eol_table::Align::Right:
                    EolSettings->persist_table_alignment(eol_table::Align::Left);
                    return;
                }
            });

        choice = nav.navigate();

        if (choice < 0) {
            eol_settings::write_settings();
            State->save();
            return;
        }
    }
}

#undef BOOL_OPTION
