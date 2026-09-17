// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius Phase 1 deliverable: gaius_viewer
//
// "Look at your data" tool per GAIUS_ROADMAP.md Phase 1 -- proves the
// resolution-independence and input-abstraction architecture
// (GAIUS_MASTERPLAN.md section 5a) end-to-end with a real SDL2 window,
// not just static PNG dumps like Phase 0's tools produced.
//
// Accepts either an EMPIRE2.0xx scenario (rendered with the same
// terrain-family color classification as Phase 0's empire_view tool) or a
// CAESARxx.SAV save file (rendered as the 100x100 city tile grid or one of
// the four service-layer heatmaps -- see apps/viewer/save_view.hpp). Which
// one is picked is decided by the file's exact size, not its extension --
// same "sniff structure, don't trust extensions" lesson as
// docs/CAESAR_GOG_BUILD_FINDINGS.md's MINIFONT.PL1 finding.
//
// Controls:
//   left-drag with middle mouse / single-finger touch drag / left gamepad
//     stick  -> pan
//   scroll wheel / gamepad triggers                        -> zoom
//   right-click / two-finger tap / gamepad B                -> cycle save
//     layer (save-file mode only; no-op for an EMPIRE2 scenario)
//   Tab / gamepad X                                         -> cycle build tool
//   V / gamepad right shoulder / tap the selected button    -> next Forum grade or Workshop goods
//   Space / gamepad Y                                       -> pause / resume time (a month about every 2 s;
//                                                              save-file mode)
//   left-click / tap / gamepad A                            -> place current
//     build tool at the clicked cell (save-file mode only)
//   M / gamepad Back / the City, Province, Forum buttons    -> switch screen
//   on the province: the command bar, then click the map    -> build, place a fort, or order a Cohort
//                                                              (click the Cohort, then a point or an army)
//   F11                                                     -> cycle window mode
//   Escape / window close                                   -> quit
//
// Build mode (Phase 5) places through systems::construction, which carries
// the real seed tiles, footprints and drag auto-tiling recovered from the
// executable; a mouse left-drag lays roads and walls cell by cell. Time runs
// through systems::month (walkers, fire and all), and the city view animates
// on the engine's frame counters. See systems/construction.hpp.
//
// Usage:
//   gaius_viewer <EMPIRE2.0xx | CAESARxx.SAV>
//   gaius_viewer <path> --screenshot out.png --frames N   (headless smoke test)
//   gaius_viewer <CAESARxx.SAV> --assets <game dir>       (draw the city with the game's sprites; by default
//                                                          they're looked for beside the save and one folder up)
//   gaius_viewer <CAESARxx.SAV> --test-layer 0..4         (headless: pick a data layer directly)
//   gaius_viewer <CAESARxx.SAV> --months N [--paused]     (run N months before the first frame; start paused)
//   gaius_viewer <CAESARxx.SAV> --test-build T X Y        (headless: place tool T at cell X,Y)

#include <SDL.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <stdexcept>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "apps/viewer/save_view.hpp"
#include "audio/game_audio.hpp"
#include "apps/viewer/screens.hpp"
#include "apps/viewer/settings_page.hpp"
#include "apps/viewer/setup_screen.hpp"
#include "formats/pal256/pal256.hpp"
#include "formats/pl8/pl8.hpp"
#include "formats/p32/p32.hpp"
#include "formats/screen_data/screen_data.hpp"
#include "formats/vpx/vpx.hpp"
#include "render/city_render.hpp"
#include "render/empire_map.hpp"
#include "render/province_render.hpp"
#include "systems/campaign.hpp"
#include "systems/month.hpp"
#include "systems/province.hpp"
#include "systems/sounds.hpp"
#include "ui/panel.hpp"
#include "ui/battle_screen.hpp"
#include "ui/buttons.hpp"
#include "ui/font.hpp"
#include "ui/forum_screens.hpp"
#include "ui/interface.hpp"
#include "ui/maps_screen.hpp"
#include "ui/name_entry.hpp"
#include "ui/options_screen.hpp"
#include "ui/game_font.hpp"
#include "formats/empire2/empire2.hpp"
#include "formats/save/save.hpp"
#include "model/city_state.hpp"
#include "platform/game_import.hpp"
#include "platform/input.hpp"
#include "platform/paths.hpp"
#include "platform/window.hpp"
#include "stb_image_write.h"
#include "systems/construction.hpp"
#include "systems/economy.hpp"
#include "ui/metrics.hpp"
#include "ui/settings.hpp"
#include "ui/strings.hpp"
#include "ui/toolbar.hpp"

using namespace gaius;
using gaius::formats::empire2::EmpireMap;
using gaius::formats::empire2::kMapH;
using gaius::formats::empire2::kMapW;
namespace fs = std::filesystem;

namespace {

constexpr int kLogicalW = 320;  // matches the original Caesar screen resolution -- a deliberate nod, not a constraint
constexpr int kLogicalH = 200;
constexpr int kCellPx = 16;  // world pixels per EMPIRE2 map cell at zoom == 1
constexpr int kWorldW = kMapW * kCellPx;
constexpr int kWorldH = kMapH * kCellPx;

// World pixels per city-grid cell at zoom == 1 for the data-layer heatmaps.
// With the game's sprites available the city uses its real 16 px cells
// (render::kCellPx) for every layer, so camera and clicks share one scale.
constexpr int kCityCellPx = 6;

// Build-mode tool ring: the placeable construction commands, in toolbar
// order. The drag-built ones (Road/Wall/Plaza/Clear Area) place one cell per
// click, which is what the engine's handler does for each cell of a drag.
// Forum and Workshop place the grade / goods chosen with CycleVariant (V,
// gamepad right shoulder, or tapping the selected button again).
constexpr systems::construction::CommandId kBuildTools[] = {
    systems::construction::CommandId::Road,      systems::construction::CommandId::Wall,
    systems::construction::CommandId::Plaza,     systems::construction::CommandId::ClearArea,
    systems::construction::CommandId::Forum,     systems::construction::CommandId::Workshop,
    systems::construction::CommandId::Housing,   systems::construction::CommandId::Well,
    systems::construction::CommandId::Fountain,  systems::construction::CommandId::ReservoirPipe,
    systems::construction::CommandId::Temple,    systems::construction::CommandId::BathHouses,
    systems::construction::CommandId::Hospital,  systems::construction::CommandId::School,
    systems::construction::CommandId::Oracle,    systems::construction::CommandId::Theater,
    systems::construction::CommandId::Coliseum,  systems::construction::CommandId::Hippodrome,
    systems::construction::CommandId::Barracks,  systems::construction::CommandId::Prefecture,
    systems::construction::CommandId::Market,    systems::construction::CommandId::HeavyIndustry,
};
constexpr int kBuildToolCount = sizeof(kBuildTools) / sizeof(kBuildTools[0]);

// Same classification as tools/empire_view.cpp -- kept in sync deliberately;
// this is exactly the kind of small duplication that should collapse into
// a shared formats::empire2 rendering helper once a second consumer shows
// up for real (not yet worth abstracting for one).
formats::RGB classify_color(uint8_t v) {
    if (v == 0x00) return {20, 20, 20};
    if (v == 0x41) return {255, 255, 0};
    if (v == 0x4A) return {255, 0, 0};
    if (v == 0x4B) return {255, 128, 128};
    if (v == 0x61) return {255, 165, 0};
    if (v >= 0x4E && v <= 0x50) return {40, 90, 220};
    if (v >= 0x1D && v <= 0x35) return {90, 160, 60};
    if (v >= 0x51 && v <= 0x60) return {140, 120, 90};
    if (v == 0x4C || v == 0x79 || v == 0x7A || v == 0x78) return {200, 100, 200};
    return {100, 100, 100};
}

// Generalized over world size so the same pan/zoom/input-handling code
// drives either the EMPIRE2 world or the (differently-sized) city-save
// world -- the two view modes differ only in what they sample and draw,
// never in how the camera behaves.
struct Camera {
    double world_w, world_h;
    double x, y;  // top-left world pixel visible at viewport (0,0)
    double zoom = 1.0;

    // Height of the map area actually VISIBLE to the player, in logical
    // pixels. This is less than kLogicalH whenever the toolbar panel
    // occludes the bottom of the frame. Clamping against the full buffer
    // height instead would leave the world's last panel_h/zoom pixels
    // permanently behind the panel -- reachable by neither eye nor click,
    // which for a 100x100 city at desktop scale hides its bottom ~5 rows.
    double visible_h = kLogicalH;

    explicit Camera(double ww, double wh)
        : world_w(ww), world_h(wh), x(ww / 2.0 - kLogicalW / 2.0), y(wh / 2.0 - kLogicalH / 2.0) {}

    void clamp() {
        zoom = std::clamp(zoom, 0.5, 8.0);
        double view_w = kLogicalW / zoom, view_h = visible_h / zoom;
        x = std::clamp(x, 0.0, std::max(0.0, world_w - view_w));
        y = std::clamp(y, 0.0, std::max(0.0, world_h - view_h));
    }
};

void render_empire_frame(const EmpireMap& map, const Camera& cam, std::vector<uint8_t>& rgb_out) {
    rgb_out.resize(static_cast<size_t>(kLogicalW) * kLogicalH * 3);
    for (int vy = 0; vy < kLogicalH; ++vy) {
        for (int vx = 0; vx < kLogicalW; ++vx) {
            double wx = cam.x + vx / cam.zoom;
            double wy = cam.y + vy / cam.zoom;
            int cell_x = static_cast<int>(wx) / kCellPx;
            int cell_y = static_cast<int>(wy) / kCellPx;
            formats::RGB c{0, 0, 0};
            if (cell_x >= 0 && cell_x < kMapW && cell_y >= 0 && cell_y < kMapH) {
                c = classify_color(map.at(cell_y, cell_x));
            }
            size_t idx = (static_cast<size_t>(vy) * kLogicalW + vx) * 3;
            rgb_out[idx + 0] = c.r;
            rgb_out[idx + 1] = c.g;
            rgb_out[idx + 2] = c.b;
        }
    }
}

// Samples a rendered city image (render::render_city, 16 px per cell)
// through the camera and converts it with the city palette.
void render_sprite_view(const formats::IndexedImage& img, const formats::Palette& pal, const Camera& cam,
                        std::vector<uint8_t>& rgb_out) {
    rgb_out.resize(static_cast<size_t>(kLogicalW) * kLogicalH * 3);
    for (int vy = 0; vy < kLogicalH; ++vy) {
        for (int vx = 0; vx < kLogicalW; ++vx) {
            const int ix = static_cast<int>(cam.x + vx / cam.zoom);
            const int iy = static_cast<int>(cam.y + vy / cam.zoom);
            formats::RGB c{0, 0, 0};
            if (ix >= 0 && ix < img.width && iy >= 0 && iy < img.height) {
                c = pal.colors[img.pixels[static_cast<size_t>(iy) * img.width + ix]];
            }
            size_t idx = (static_cast<size_t>(vy) * kLogicalW + vx) * 3;
            rgb_out[idx + 0] = c.r;
            rgb_out[idx + 1] = c.g;
            rgb_out[idx + 2] = c.b;
        }
    }
}

}  // namespace

namespace {

// The languages lang/languages.txt lists, read through SDL so Android finds
// them among the APK's assets: English first.
std::vector<ui::Catalog> load_languages() {
    std::vector<ui::Catalog> out{ui::Catalog{"en", "English", {}}};
    std::string base;
#if !defined(__ANDROID__)
    if (char* b = SDL_GetBasePath()) {
        base = b;
        SDL_free(b);
    }
#endif
    const auto read = [&](const std::string& name) {
        std::string text;
        SDL_RWops* rw = SDL_RWFromFile((base + "lang/" + name).c_str(), "rb");
        if (!rw) return text;
        char buf[4096];
        size_t n = 0;
        while ((n = SDL_RWread(rw, buf, 1, sizeof buf)) > 0) text.append(buf, n);
        SDL_RWclose(rw);
        return text;
    };
    std::string index = read("languages.txt");
    size_t at = 0;
    while (at < index.size()) {
        size_t end = index.find('\n', at);
        if (end == std::string::npos) end = index.size();
        std::string code = index.substr(at, end - at);
        at = end + 1;
        while (!code.empty() && (code.back() == '\r' || code.back() == ' ')) code.pop_back();
        if (code.empty() || code[0] == '#' || code == "en") continue;
        const std::string text = read(code + ".txt");
        if (!text.empty()) out.push_back(ui::parse_catalog(code, text));
    }
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    // Gaius's settings (ui/settings.hpp), before anything else: they may name
    // the game's folder.
    ui::Settings settings;
    std::string settings_path;
    bool settings_existed = false;
    try {
        settings_path = (fs::path(platform::paths().settings) / "gaius.cfg").string();
        settings_existed = ui::load_settings(settings_path, settings);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "note: no settings folder: %s\n", e.what());
    }
    // The Steam Deck (Steam sets SteamDeck=1): fullscreen on a first start.
    const char* deck = std::getenv("SteamDeck");
    const bool steam_deck = deck && std::strcmp(deck, "1") == 0;
    if (steam_deck && !settings_existed) settings.window_mode = ui::WindowModeSetting::Fullscreen;
#if defined(__ANDROID__)
    settings.window_mode = ui::WindowModeSetting::Fullscreen;
#endif
    const std::vector<ui::Catalog> languages = load_languages();
    for (const ui::Catalog& c : languages)
        if (c.code == settings.language) ui::set_catalog(c);
    viewer::apply_bindings(settings);

    std::string in_path = argc >= 2 ? argv[1] : std::string();
    if (in_path.empty() || in_path.rfind("--", 0) == 0) {
        // No folder or file given: a career from the game's files, wherever
        // they are, or the setup screen that explains where they go.
        in_path = viewer::find_game_folder(settings);
        if (in_path.empty()) {
            if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
                std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
                return 3;
            }
            try {
                in_path = viewer::run_setup_screen(settings, settings_path, kLogicalW, kLogicalH);
            } catch (const std::exception& e) {
                std::fprintf(stderr, "fatal: %s\n", e.what());
            }
            if (in_path.empty()) {
                SDL_Quit();
                return 0;
            }
        }
    }
    std::string screenshot_path;
    int screenshot_frames = 0;
    double test_pan_x = 0, test_pan_y = 0, test_zoom = 1.0;
    int test_layer = -1;
    struct TestBuild { int tool, x, y; };
    std::vector<TestBuild> test_builds;  // repeatable: --test-build may appear many times
    struct TestClick { int x, y; };
    std::vector<TestClick> test_clicks;  // logical-space clicks, for headless UI tests
    int ui_scale_override = -1;          // -1 = use the size heuristic
    std::string assets_dir;
    int run_months = 0;
    bool start_paused = false;
    bool mute = false;  // --mute: no audio device (headless runs are always mute)
    std::string start_screen;       // --screen city|province|forum
    int start_forum_tab = 0;        // --forum-tab 0..6, or 8 for the statue
    bool cheats = false;            // --cheats: the statue opens (the original's key gate, 0x0DF9B)
    std::vector<int> test_actions;  // --test-action N: a page action applied before the first frame
    bool test_battle = false;       // --test-battle: the first Cohort meets a new army
    bool test_promotion = false;    // --test-promotion: a promotion is offered now
    std::string save_dir_option;    // --save-dir: where the save slots live (default: the per-user data folder)
    std::string cohort_command;     // --cohort-command: runs Cohort 2 for a battle (findings section 44)
    int start_speed = -1;           // --speed 0-100: DS:0x5292 (default: the options' CAESAR.INF)
    int test_message = -1;          // --test-message N: post systems::messages::Id N before the first frame
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--assets") == 0 && i + 1 < argc) assets_dir = argv[++i];
        if (std::strcmp(argv[i], "--months") == 0 && i + 1 < argc) run_months = std::atoi(argv[++i]);
        if (std::strcmp(argv[i], "--paused") == 0) start_paused = true;
        if (std::strcmp(argv[i], "--mute") == 0) mute = true;
        if (std::strcmp(argv[i], "--screen") == 0 && i + 1 < argc) start_screen = argv[++i];
        if (std::strcmp(argv[i], "--forum-tab") == 0 && i + 1 < argc) start_forum_tab = std::atoi(argv[++i]);
        if (std::strcmp(argv[i], "--test-action") == 0 && i + 1 < argc) test_actions.push_back(std::atoi(argv[++i]));
        if (std::strcmp(argv[i], "--test-battle") == 0) test_battle = true;
        if (std::strcmp(argv[i], "--cheats") == 0) cheats = true;
        if (std::strcmp(argv[i], "--test-promotion") == 0) test_promotion = true;
        if (std::strcmp(argv[i], "--save-dir") == 0 && i + 1 < argc) save_dir_option = argv[++i];
        if (std::strcmp(argv[i], "--cohort-command") == 0 && i + 1 < argc) cohort_command = argv[++i];
        if (std::strcmp(argv[i], "--speed") == 0 && i + 1 < argc) start_speed = std::atoi(argv[++i]);
        if (std::strcmp(argv[i], "--test-message") == 0 && i + 1 < argc) test_message = std::atoi(argv[++i]);
        if (std::strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) screenshot_path = argv[++i];
        if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) screenshot_frames = std::atoi(argv[++i]);
        // Headless verification hooks -- there's no real mouse/touch/gamepad
        // in a CI/sandboxed environment, so these let automated smoke tests
        // exercise the same Camera math the real input path drives.
        if (std::strcmp(argv[i], "--test-pan") == 0 && i + 2 < argc) {
            test_pan_x = std::atof(argv[++i]);
            test_pan_y = std::atof(argv[++i]);
        }
        if (std::strcmp(argv[i], "--test-zoom") == 0 && i + 1 < argc) test_zoom = std::atof(argv[++i]);
        if (std::strcmp(argv[i], "--test-layer") == 0 && i + 1 < argc) test_layer = std::atoi(argv[++i]);
        // Headless build-mode hook: place tool #N at grid cell (x,y) before
        // the first frame, so the placement path can be smoke-tested without
        // a real mouse -- same spirit as --test-pan/--test-zoom. Repeatable,
        // so a whole test city can be laid down in one invocation.
        if (std::strcmp(argv[i], "--test-build") == 0 && i + 3 < argc) {
            TestBuild b;
            b.tool = std::atoi(argv[++i]);
            b.x = std::atoi(argv[++i]);
            b.y = std::atoi(argv[++i]);
            test_builds.push_back(b);
        }
        // Headless UI hook: a click in LOGICAL framebuffer coordinates,
        // fed through the same handler the real Select command uses, so
        // toolbar hit-testing is exercised rather than bypassed.
        if (std::strcmp(argv[i], "--test-click") == 0 && i + 2 < argc) {
            TestClick c;
            c.x = std::atoi(argv[++i]);
            c.y = std::atoi(argv[++i]);
            test_clicks.push_back(c);
        }
        if (std::strcmp(argv[i], "--ui-scale") == 0 && i + 1 < argc) {
            std::string v = argv[++i];
            if (v == "desktop") ui_scale_override = static_cast<int>(ui::Breakpoint::Desktop);
            else if (v == "handheld") ui_scale_override = static_cast<int>(ui::Breakpoint::Handheld);
            else if (v == "phone") ui_scale_override = static_cast<int>(ui::Breakpoint::Phone);
            else if (v == "tv") ui_scale_override = static_cast<int>(ui::Breakpoint::Tv);
            else std::fprintf(stderr, "unknown --ui-scale '%s' (desktop|handheld|phone|tv)\n", v.c_str());
        }
    }

    // Dispatch by exact file size, not extension -- EMPIRE2.0xx is always
    // exactly 1602 bytes, CAESARxx.SAV is always exactly 57126
    // (formats::save::kSaveSize); no other size is valid input here. Same
    // "sniff structure, don't trust extensions" lesson as
    // docs/CAESAR_GOG_BUILD_FINDINGS.md's MINIFONT.PL1 finding.
    // A folder is the game's own: a new career, from the start screen.
    const bool new_career = fs::is_directory(in_path);
    std::error_code ec;
    uintmax_t file_size = new_career ? 0 : fs::file_size(in_path, ec);
    if (ec) {
        std::fprintf(stderr, "failed to stat %s: %s\n", in_path.c_str(), ec.message().c_str());
        return 2;
    }
    bool save_mode = new_career || (file_size == formats::save::kSaveSize);
    if (new_career && assets_dir.empty()) assets_dir = in_path;

    EmpireMap map;
    formats::save::SaveFile save;
    model::CityState state;  // save mode works on the Layer 2 model so build mode can mutate it
    viewer::SaveLayer layer = viewer::SaveLayer::Tiles;
    if (test_layer >= 0 && test_layer < viewer::kSaveLayerCount) layer = viewer::kSaveLayerOrder[test_layer];
    int tool_index = 0;

    try {
        if (new_career) {
            state = model::blank_state();
        } else if (save_mode) {
            save = formats::save::load(in_path);
            state = model::load(save);
        } else {
            map = formats::empire2::load(in_path);
        }
    } catch (const formats::FormatError& e) {
        std::fprintf(stderr, "failed to load %s: %s\n", in_path.c_str(), e.what());
        return 2;
    }

    // The game's own city sprites, for save mode: from --assets, else the
    // save's folder (a game install keeps its saves beside the sheets) or the
    // folder above it. Without them the viewer shows the data layers only.
    render::CitySprites sprites;
    bool have_sprites = false;
    ui::GameFont game_font;  // FONT1.PL8, drawn with the city palette
    bool have_font = false;
    formats::PL8Sheet toolbar_icons;  // POINTERS.PL8
    bool have_icons = false;
    render::ProvinceSprites province_sprites;  // FIXT3.PL8, SPRITE2.PL8
    bool have_province_sprites = false;
    // The Forum's hall: NEWFORUM.VPX in NEWFORUM.256, and CONTFRM.GD8, the map
    // of which figure is under a click (formats/screen_data).
    formats::IndexedImage forum_picture;
    formats::Palette forum_palette;
    formats::screen_data::ClickMap forum_clicks;
    bool have_forum_picture = false;
    formats::IndexedImage empire_picture;  // EMAP2.VPX
    formats::Palette empire_palette;       // EMAP2.P32
    std::array<formats::screen_data::Marker, 50> empire_markers{};  // EDATA.CSR
    bool have_empire_map = false;
    ui::BattleArt battle_art;  // WAR2.VPX and the rest of the original battle screen
    bool have_battle_art = false;
    ui::MapsArt maps_art;  // the original maps screen
    bool have_maps_art = false;
    ui::NameArt name_art;  // the name dialog
    bool have_name_art = false;
    ui::InterfaceArt interface_art;  // the original's advisor screens
    bool have_interface_art = false;
    ui::RatingsArt ratings_art;      // TEMPLE.VPX and its columns
    bool have_ratings_art = false;
    formats::IndexedImage governor_picture;  // C_VITAE.VPX
    bool have_governor_picture = false;
    ui::GovernorDialog governor_dialog = ui::GovernorDialog::None;  // open over the governor's screen
    bool pause_until_click = false;  // "Pause the game" (0x0F01E): a click or T resumes
    char key_last = 0, key_prev = 0;  // 2EF9:002F and 2EF9:0031: the last key typed and the one before
    // The original screens' buttons, run once a frame (0x0D41D / 0x0D521).
    std::vector<ui::Button> active_buttons;
    ui::ButtonTracker button_tracker;
    int active_buttons_key = -1;
    ui::Pointer pointer;
    bool pointer_was_held = false;
    // Ending a governor dialog; the donation's pays out (0x0C26E).
    const auto close_governor_dialog = [&] {
        if (governor_dialog == ui::GovernorDialog::Donation)
            systems::economy::donate_savings(state, model::global_word(state, 0x6C28));
        governor_dialog = ui::GovernorDialog::None;
    };
    std::string game_dir;  // where the game's files are: a new province's EMPIRE2.0NN is read from here
    if (save_mode) {
        std::vector<std::string> candidates;
        if (!assets_dir.empty()) candidates.push_back(assets_dir);
        const fs::path save_dir = fs::absolute(in_path).parent_path();
        candidates.push_back(save_dir.string());
        candidates.push_back(save_dir.parent_path().string());
        for (const std::string& dir : candidates) {
            try {
                sprites = render::load_city_sprites(dir);
                have_sprites = true;
                std::printf("city sprites: %s\n", dir.c_str());
                game_dir = dir;
                try {
                    province_sprites = render::load_province_sprites(dir);
                    have_province_sprites = true;
                } catch (const formats::FormatError&) {
                }
                const auto asset = [&](std::string name) {
                    fs::path p = fs::path(dir) / name;
                    if (fs::exists(p)) return p.string();
                    for (char& ch : name) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                    return (fs::path(dir) / name).string();
                };
                try {
                    forum_picture = formats::vpx::decode(asset("NEWFORUM.VPX")).image;
                    forum_palette = formats::pal256::load(asset("NEWFORUM.256"));
                    forum_clicks = formats::screen_data::load_click_map(asset("CONTFRM.GD8"));
                    have_forum_picture = true;
                } catch (const formats::FormatError&) {
                }
                try {
                    interface_art = ui::load_interface_art(dir);
                    have_interface_art = true;
                } catch (const formats::FormatError&) {
                }
                try {
                    governor_picture = ui::load_governor_picture(dir);
                    have_governor_picture = true;
                } catch (const formats::FormatError&) {
                }
                try {
                    ratings_art = ui::load_ratings_art(dir);
                    have_ratings_art = true;
                } catch (const formats::FormatError&) {
                }
                try {
                    name_art = ui::load_name_art(dir);
                    have_name_art = true;
                } catch (const formats::FormatError&) {
                }
                try {
                    maps_art = ui::load_maps_art(dir);
                    have_maps_art = true;
                } catch (const formats::FormatError&) {
                }
                try {
                    battle_art = ui::load_battle_art(dir);
                    have_battle_art = true;
                } catch (const formats::FormatError&) {
                }
                try {
                    empire_picture = formats::vpx::decode(asset("EMAP2.VPX")).image;
                    empire_palette = formats::p32::load(asset("EMAP2.P32"));
                    empire_markers = formats::screen_data::load_province_markers(asset("EDATA.CSR"));
                    have_empire_map = true;
                } catch (const formats::FormatError&) {
                }
                try {
                    game_font = ui::load_game_font(dir, sprites.palette);
                    have_font = true;
                } catch (const formats::FormatError&) {
                }
                for (const char* name : {"POINTERS.PL8", "pointers.pl8"}) {
                    const fs::path p = fs::path(dir) / name;
                    if (!fs::exists(p)) continue;
                    try {
                        toolbar_icons = formats::pl8::load(p.string());
                        have_icons = true;
                    } catch (const formats::FormatError&) {
                    }
                    break;
                }
                break;
            } catch (const formats::FormatError&) {
            }
        }
        if (!have_sprites) std::printf("city sprites not found (pass --assets <game dir>); showing data layers only\n");
        if (game_dir.empty()) game_dir = fs::absolute(in_path).parent_path().string();
    }
    const int city_cell_px = have_sprites ? render::kCellPx : kCityCellPx;
    bool show_sprites = have_sprites && test_layer < 0;
    formats::IndexedImage city_image;
    bool city_image_dirty = true;  // re-rendered whenever build mode or time changes the grid
    formats::IndexedImage province_image;
    bool province_image_dirty = true;
    render::RenderPhase render_phase;

    // The simulation clock (systems::month), seeded from the save.
    systems::month::SimState sim;
    if (save_mode) sim = systems::month::sim_state_from_save(state);
    // The options: CAESAR.INF (DS:0x5288) -- Gaius's own copy in the per-user
    // data folder, first read from the game's, so the original's is never
    // written.
    ui::GameOptions game_options = ui::default_options();
    std::string options_path;
    try {
        options_path = (fs::path(platform::paths().settings) / "caesar.inf").string();
    } catch (const std::exception&) {
    }
    if (options_path.empty() || !ui::load_options(options_path, game_options)) {
        if (!ui::load_options((fs::path(game_dir) / "CAESAR.INF").string(), game_options))
            ui::load_options((fs::path(game_dir) / "caesar.inf").string(), game_options);
    }
    if (start_speed >= 0) game_options.set(ui::kOptSpeed, std::clamp(start_speed / 10 * 10, 0, 100));
    sim.speed = std::clamp(game_options.speed() / 10 * 10, 0, 100);
    bool time_running = save_mode && !start_paused;
    // One step every 19 ms: a month (106 steps) in about 2 s, and the walkers
    // move a pixel each step.
    constexpr Uint32 kStepMs = 19;
    auto report_month = [&]() {
        std::printf("month %d, year %d: population %d, funds %d Dn\n", sim.month + 1, sim.year,
                    4 * sim.population_units, model::global_word(state, systems::economy::kFunds));
        if (sim.month == 0) {
            // The year just settled (systems::economy::run_year): the Treasurer's report.
            auto g = [&](uint16_t ds) { return model::global_word(state, ds); };
            std::printf("  last year: population tax %d, industrial tax %d, construction %d, operating costs %d, "
                        "tribute %d, profit %d\n",
                        g(0x6BB2), g(0x6BB0), g(0x6BAE), g(0x6BAC), g(0x6BAA), g(0x6BB4));
        }
    };
    auto advance_month = [&]() {
        systems::month::run_month(state, sim);
        city_image_dirty = true;
        province_image_dirty = true;
        report_month();
    };
    // One frame of the main loop (systems::month::run_frame): at the game speed
    // not every frame runs a step, but every frame draws and counts the message.
    auto advance_step = [&]() {
        const int month_before = sim.month;
        if (!systems::month::run_frame(state, sim)) return false;
        city_image_dirty = true;
        province_image_dirty = true;
        if (sim.month != month_before) report_month();
        return true;
    };

    // One placement for the click handler and --test-build: the drag-built
    // commands keep their neighbour snapshot in `drag`, and Clear Area draws
    // rubble from the simulation's random number generator.
    systems::construction::DragState drag;
    int drag_last_x = -1, drag_last_y = -1;  // the last cell a click or drag placed on
    // The active drag's undo trail, for the original's cancel gesture (right
    // button while the left is still down -- manual, Building Roads): the
    // pre-placement tile and 7BB4 byte of every cell that might have changed
    // this drag, and the funds it cost. Cleared at the start of each new
    // gesture (handle_select_logical) and consumed by CancelDrag below.
    struct DragUndoCell {
        int x, y;
        uint8_t tile, op_state;
    };
    std::vector<DragUndoCell> drag_undo;
    int drag_refund = 0;
    // The variant choices the original makes in sub-menus: DS:0x6D89 (Forum
    // grade) and DS:0x6D87 (Workshop goods).
    int forum_grade = 0, workshop_goods = 0;
    auto tool_label = [&]() -> std::string {
        namespace construction = systems::construction;
        const auto tool = kBuildTools[tool_index];
        if (tool == construction::CommandId::Forum)
            return "Forum grade " + std::to_string(forum_grade + 1) + ", " +
                   std::to_string(construction::kForumGradeCost[static_cast<size_t>(forum_grade)]) + " Dn";
        if (tool == construction::CommandId::Workshop)
            return std::string("Workshop: ") + construction::kWorkshopGoodsNames[static_cast<size_t>(workshop_goods)] +
                   ", " + std::to_string(systems::economy::construction_cost(tool)) + " Dn";
        const int cost = systems::economy::construction_cost(tool);
        return std::string(construction::command_name(tool)) + (cost > 0 ? ", " + std::to_string(cost) + " Dn" : "");
    };
    auto cycle_variant = [&]() {
        const auto tool = kBuildTools[tool_index];
        if (tool == systems::construction::CommandId::Forum) {
            forum_grade = (forum_grade + 1) % 8;
        } else if (tool == systems::construction::CommandId::Workshop) {
            workshop_goods = (workshop_goods + 1) % 8;
        } else {
            return false;
        }
        std::printf("tool: %s\n", tool_label().c_str());
        return true;
    };
    auto place_tool = [&](systems::construction::CommandId tool, int x, int y) {
        namespace construction = systems::construction;
        namespace economy = systems::economy;
        // The engine checks the cost before calling the handler and charges it
        // only when the handler succeeds; a drag pays per cell (0x11DAC-0x120B0).
        namespace msg = systems::messages;
        // 0x11C72: fewer than 50 pleb groups refuses every construction command.
        if (!economy::enough_plebs(state, static_cast<int>(tool))) {
            sim.messages.post(msg::plain(msg::Id::ConstructionPlebs));
            return false;
        }
        // 0x14E1D / 0x15252: at 30 forums or 30 workshops.
        if (tool == construction::CommandId::Forum && model::global_word(state, 0x6CA0) >= 30) {
            sim.messages.post(msg::plain(msg::Id::NoForum));
            return false;
        }
        if (tool == construction::CommandId::Workshop && model::global_word(state, 0x6C9E) >= 30) {
            sim.messages.post(msg::plain(msg::Id::NoFactory));
            return false;
        }
        const int cost = economy::construction_cost(tool, forum_grade);
        if (!economy::can_afford(state, cost)) {
            if (economy::grant_emergency_funds(state))
                std::printf("Rome sends 500 Dn in emergency funds\n");
            else
                std::printf("not enough funds: %s costs %d Dn\n", construction::command_name(tool), cost);
            return false;
        }
        const bool draggable = construction::placement_spec(tool).kind == construction::PlacementKind::DragAutoTiled;
        if (draggable) {
            // Snapshot a 7x7 box around the target before placing: Road, Wall
            // and Plaza only ever retile a direct neighbour (1-cell radius),
            // but Clear Area can wreck a whole building anchored up to 3 cells
            // away (construction::wreck_cells walks to the anchor first), so
            // that's the box that covers every drag command's worst case.
            // Only the first snapshot of a given cell in this drag is kept, so
            // a later overlapping placement can't clobber the pre-drag state
            // CancelDrag needs to restore.
            for (int dy = -3; dy <= 3; ++dy) {
                for (int dx = -3; dx <= 3; ++dx) {
                    const int sx = x + dx, sy = y + dy;
                    if (sx < 0 || sy < 0 || sx >= model::kCityW || sy >= model::kCityH) continue;
                    bool seen = false;
                    for (const auto& u : drag_undo) {
                        if (u.x == sx && u.y == sy) {
                            seen = true;
                            break;
                        }
                    }
                    if (!seen)
                        drag_undo.push_back({sx, sy, state.city.tile[sy][sx], state.city.operational_state[sy][sx]});
                }
            }
        }
        bool placed = false;
        switch (tool) {
            case construction::CommandId::Road: placed = construction::place_road(state.city, drag, x, y); break;
            case construction::CommandId::Wall: placed = construction::place_wall(state.city, drag, x, y); break;
            case construction::CommandId::Plaza: placed = construction::place_plaza(state.city, x, y); break;
            case construction::CommandId::ClearArea: placed = construction::clear_area(state, sim.random, x, y); break;
            case construction::CommandId::Forum: placed = construction::place_forum(state, forum_grade, x, y); break;
            case construction::CommandId::Workshop:
                placed = construction::place_workshop(state, workshop_goods, x, y);
                break;
            default: placed = construction::place(state.city, tool, x, y); break;
        }
        if (placed) {
            economy::charge(state, cost);
            if (draggable) drag_refund += cost;
        }
        return placed;
    };

    // ---- The screens: the city, the province, the Forum, a promotion offer and
    // a battle. The last two open themselves when the simulation asks and stop
    // time until they're answered.
    enum class Screen { City, Province, Maps, ForumHall, Forum, Promotion, Battle, Ending, Start, Files, Notice, EmpireMap,
                        NameEntry, Settings };
    Screen settings_return = Screen::City;  // where Resume goes back to
    viewer::SettingsView settings_view;
    settings_view.languages = languages;
    settings_view.game_dir = game_dir;
#if defined(__ANDROID__)
    settings_view.windowed_platform = false;
#endif
    settings_view.can_import = platform::can_import_game_folder();
    // DS:0x0DD0: the governor's name, kept across new games and saved with each.
    std::string governor_name = ui::governor_name(state);
    ui::NameEntry name_entry;
    bool name_return_to_forum = false;  // opened from the governor's screen rather than the start screen
    Screen notice_return = Screen::City;  // where the funds warning's Continue goes back to
    int hall_hover = 0;  // the CONTFRM.GD8 region under the pointer
    Screen screen = Screen::City;
    Screen files_return = Screen::Forum;  // where the save / load page goes back to
    bool files_saving = false;
    int funding_level = 0, start_difficulty = 0;  // the start screen's DS:0x6CBA and DS:0x6CB8
    std::vector<ui::PanelButton> slot_buttons;
    if (new_career) {
        screen = Screen::Start;
        time_running = false;
    }
    int overlay = 0;             // viewer::Overlay on the maps panel
    ui::MapMode map_mode = ui::MapMode::None;  // DS:0x6D26 on the original maps screen
    bool map_shows_city = false;               // DS:0x6D28
    bool ending_caesar = false;  // the ending page: Caesar, or dismissed
    bool quit_requested = false;
    if (start_screen == "maps") screen = Screen::Maps;
    if (start_screen == "settings") screen = Screen::Settings;
    if (start_screen == "province") screen = Screen::Province;
    if (start_screen == "forum") screen = have_forum_picture ? Screen::ForumHall : Screen::Forum;
    if (start_screen == "forum-page") screen = Screen::Forum;
    if (start_screen == "empire" && have_empire_map && have_icons) screen = Screen::EmpireMap;
    Screen battle_return = Screen::Province;
    viewer::ForumTab forum_tab =
        start_forum_tab == viewer::kStatue
            ? viewer::kStatue
            : static_cast<viewer::ForumTab>(std::clamp(start_forum_tab, 0, static_cast<int>(viewer::kForumTabCount) - 1));
    if (forum_tab == viewer::kIndustry) systems::forum::open_industry_report(state);
    int rating_hint = 0;  // forum::kRatingHints on show
    int hint_cycle = 0;   // DS:0x6D2A
    int hint_frames = 0;  // the advice shows 90 frames on the original screen
    bool promotion_to_caesar = false;
    viewer::BattleView battle_view;
    ui::BattleScreen battle_screen;  // the original's screen, when its files are there
    bool battle_clicked = false;     // a click this frame that wasn't a button (DS:0x6D4C / 0x6D4E)
    int shore_variant = 0;  // DS:0x079C, kept from one generated city to the next

    auto install_hooks = [&]() {
        // The yearly routine offers a promotion. The player answers on the
        // promotion screen, so the hook leaves it unanswered and stops time;
        // the answer is applied from there (administration::accept_promotion,
        // defer_promotion), which is all 0x29023 would have done with it.
        sim.on_promotion = [&](model::CityState&, bool to_caesar) {
            promotion_to_caesar = to_caesar;
            screen = Screen::Promotion;
            time_running = false;
            return 0;
        };
        // A Cohort reaches the army it attacks: the battle screen, with time stopped.
        sim.on_battle = [&](model::CityState&, int cohort, int army) {
            if (screen == Screen::Battle) return;
            battle_return = screen == Screen::City ? Screen::City : Screen::Province;
            battle_view = viewer::BattleView{};
            battle_view.cohort = cohort;
            battle_view.army = army;
            if (have_battle_art) {
                battle_screen = ui::begin_battle(state, battle_art, cohort, army);
                // 0x222CE: the original offers Cohort 2 when cohort.exe is
                // there; Gaius offers it when it has a command to run it.
                if (!cohort_command.empty()) battle_screen.cohort2 = ui::Cohort2Offer::Offer;
            }
            battle_clicked = false;
            screen = Screen::Battle;
            time_running = false;
            std::printf("battle: the Cohort in slot %d meets the army in slot %d\n", cohort, army);
        };
    };
    if (save_mode) install_hooks();

    // 0x0F81B: an accepted promotion (DS:0x6C26 = 1) moves the governor to the
    // new province: its map EMPIRE2.0NN (0x0FF1C writes the province number in
    // three digits) and a new city (systems::campaign::start_province).
    auto start_new_province = [&]() {
        const int province = model::global_word(state, 0x6CA6);
        char name[16];
        std::snprintf(name, sizeof name, "EMPIRE2.%03d", province);
        fs::path path = fs::path(game_dir) / name;
        if (!fs::exists(path)) {
            std::string lower = name;
            for (char& ch : lower) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            path = fs::path(game_dir) / lower;
        }
        formats::empire2::EmpireMap province_map;
        try {
            province_map = formats::empire2::load(path.string());
        } catch (const std::exception& e) {
            std::printf("can't start the new province, %s: %s\n", name, e.what());
            return false;
        }
        systems::month::Random random = sim.random;
        const int difficulty = sim.difficulty;
        systems::campaign::start_province(state, province_map, random, difficulty, shore_variant);
        model::set_global_word(state, 0x6CB8, difficulty);  // a promotion may have raised it from Easy
        const int speed = sim.speed;
        sim = systems::month::sim_state_from_save(state);
        sim.speed = speed;
        sim.random = random;
        install_hooks();
        city_image_dirty = province_image_dirty = true;
        std::printf("new province: %s (%s), funds %d Dn\n", viewer::province_name(province), name,
                    model::global_word(state, systems::economy::kFunds));
        return true;
    };

    // The save slots: CAESAR01.SAV-CAESAR08.SAV in --save-dir, or the per-user
    // data folder. The files are the original's format (formats::save::write).
    auto slot_path = [&](int slot) {
        std::string dir = save_dir_option;
        if (dir.empty()) {
            try {
                dir = platform::paths().saves;
            } catch (const std::exception&) {
                dir = game_dir;
            }
        }
        std::error_code mkdir_error;
        fs::create_directories(dir, mkdir_error);
        char name[16];
        std::snprintf(name, sizeof name, "CAESAR%02d.SAV", slot + 1);
        return (fs::path(dir) / name).string();
    };
    auto refresh_slots = [&]() {
        slot_buttons.clear();
        for (int i = 0; i < viewer::kSaveSlots; ++i) {
            const std::string path = slot_path(i);
            std::string label = std::to_string(i + 1) + "  empty";
            bool exists = false;
            if (fs::exists(path)) {
                try {
                    const auto saved = std::make_unique<model::CityState>(model::load(formats::save::load(path)));
                    label = std::to_string(i + 1) + "  " + viewer::province_name(model::global_word(*saved, 0x6CA6)) +
                            ", " + viewer::year_text(model::global_word(*saved, 0x6C32));
                    exists = true;
                } catch (const formats::FormatError&) {
                    label = std::to_string(i + 1) + "  unreadable";
                }
            }
            slot_buttons.push_back({label, viewer::kActionSlot + i, files_saving || exists});
        }
        slot_buttons.push_back({"Back", viewer::kActionBack});
    };
    // After a load: the simulation restarts from the state's words, as the
    // engine's loader leaves them (DS:0x6BFE = rate x month, 0x0568F).
    auto adopt_state = [&]() {
        const systems::month::Random random = sim.random;
        const int speed = sim.speed;
        sim = systems::month::sim_state_from_save(state);
        sim.speed = speed;
        sim.random = random;
        install_hooks();
        city_image_dirty = province_image_dirty = true;
        governor_name = ui::governor_name(state);  // 0x054CA: the loader copies it back
    };

    auto current_page = [&]() -> ui::Page {
        switch (screen) {
            case Screen::Forum:
                return viewer::forum_page(state, forum_tab, sim.speed, rating_hint, have_empire_map && have_icons);
            case Screen::Notice:
                return viewer::notice_page(systems::messages::kFundsWarning.data(),
                                           systems::messages::kFundsWarning.size());
            case Screen::Promotion: return viewer::promotion_page(state, promotion_to_caesar);
            case Screen::Battle: return viewer::battle_page(state, battle_view);
            case Screen::Ending: return viewer::ending_page(state, ending_caesar);
            case Screen::Start:
            case Screen::NameEntry:
                return name_return_to_forum ? viewer::forum_page(state, forum_tab, sim.speed, rating_hint)
                                            : viewer::start_page(funding_level, start_difficulty, governor_name);
            case Screen::Files: return viewer::files_page(files_saving, slot_buttons);
            case Screen::Settings: {
                viewer::SettingsView v = settings_view;
                v.messages_on = model::global_word(state, 0x6C78) != 0;
                v.in_game = settings_return != Screen::Start;
                v.game_found = platform::looks_like_game_folder(game_dir);
                v.game_dir = viewer::tail(game_dir, 30);
                return viewer::settings_page(settings, game_options, v);
            }
            default: return ui::Page{};
        }
    };
    // The Forum pages Gaius draws in the original's art, when its files are there.
    const auto original_forum_screen = [&]() {
        return screen == Screen::Forum && have_interface_art &&
               (forum_tab == viewer::kHistory || forum_tab == viewer::kIndustry || forum_tab == viewer::kTreasurer ||
                forum_tab == viewer::kTribune || (forum_tab == viewer::kRatings && have_ratings_art) ||
                (forum_tab == viewer::kGovernor && have_governor_picture) ||
                (forum_tab == viewer::kLegion && have_province_sprites));
    };
    const auto page_screen = [&]() {
        return screen == Screen::Forum || screen == Screen::Promotion ||
               (screen == Screen::Battle && !have_battle_art) ||
               screen == Screen::Ending || screen == Screen::Start || screen == Screen::Files ||
               screen == Screen::Notice || screen == Screen::Settings;
    };
    auto apply_page_action = [&](int action) {
        namespace admin = systems::administration;
        namespace battle = systems::battle;
        if (action == viewer::kActionSpeedDown || action == viewer::kActionSpeedUp) {
            // 0x0F204 / 0x0F217: tens, 0-100.
            sim.speed = std::clamp(sim.speed + (action == viewer::kActionSpeedUp ? 10 : -10), 0, 100);
            return;
        }
        if (screen == Screen::Notice) {
            if (action == viewer::kActionContinue) {
                screen = notice_return;
                time_running = true;
            }
            return;
        }
        if (action == viewer::kActionOpenSave || action == viewer::kActionOpenLoad) {
            files_return = screen;
            files_saving = action == viewer::kActionOpenSave;
            refresh_slots();
            screen = Screen::Files;
            return;
        }
        if (screen == Screen::Files) {
            if (action == viewer::kActionBack) {
                screen = files_return;
                return;
            }
            const int slot = action - viewer::kActionSlot;
            if (slot < 0 || slot >= viewer::kSaveSlots) return;
            const std::string path = slot_path(slot);
            try {
                if (files_saving) {
                    formats::save::write(model::serialize(state), path);
                    std::printf("saved %s\n", path.c_str());
                    screen = files_return;
                } else {
                    state = model::load(formats::save::load(path));
                    adopt_state();
                    std::printf("loaded %s\n", path.c_str());
                    screen = Screen::City;
                    time_running = true;
                }
            } catch (const std::exception& e) {
                std::printf("%s: %s\n", path.c_str(), e.what());
            }
            return;
        }
        if (screen == Screen::Start) {
            // 0x27F54-0x27F83: funding 0-9, difficulty 0-2.
            if (action == viewer::kActionFundingDown && funding_level > 0) --funding_level;
            if (action == viewer::kActionFundingUp && funding_level < 9) ++funding_level;
            if (action == viewer::kActionDifficultyDown && start_difficulty > 0) --start_difficulty;
            if (action == viewer::kActionDifficultyUp && start_difficulty < 2) ++start_difficulty;
            if (action == viewer::kActionChooseName && have_name_art) {
                // 0x27F84: the name dialog.
                name_entry = ui::begin_name_entry(governor_name);
                screen = Screen::NameEntry;
                platform::set_text_entry(true);
                return;
            }
            if (action == viewer::kActionBegin) {
                state = model::blank_state();
                sim.difficulty = start_difficulty;
                const int province =
                    systems::campaign::begin_new_game(state, sim.random, funding_level, start_difficulty);
                ui::set_governor_name(state, governor_name);
                if (province >= 0 && start_new_province()) {
                    // 0x0F7D3 / 0x0F7DA: the first message, shown 78 frames.
                    sim.messages.post(systems::messages::plain(systems::messages::Id::NoCity));
                    sim.messages.timer = 0x4E;
                    screen = Screen::City;
                    time_running = true;
                }
            }
            return;
        }
        if (screen == Screen::Forum) {
            if (action >= viewer::kActionHint && action < viewer::kActionHint + 4) {
                // 0x0CF12: each click advances the cycle, then picks the text.
                hint_cycle = systems::forum::next_hint_cycle(hint_cycle);
                rating_hint = systems::forum::rating_hint(
                    state, systems::forum::rating_column_x(action - viewer::kActionHint), hint_cycle,
                    sim.linked_towns, sim.random.walk);
                return;
            }
            if (action == viewer::kActionEmpireMap) {
                screen = Screen::EmpireMap;  // 0x0C060 -> 0x0D174
                return;
            }
            const viewer::ForumTab tab_before = forum_tab;
            if (!viewer::apply_forum_action(state, action, forum_tab))
                screen = have_forum_picture ? Screen::ForumHall : Screen::City;
            if (forum_tab != tab_before) {
                rating_hint = 0;
                governor_dialog = ui::GovernorDialog::None;
            }
            if (action == viewer::kActionRankUp || action == viewer::kActionRankDown)
                sim.difficulty = model::global_word(state, systems::forum::kDifficulty);
        } else if (screen == Screen::Promotion) {
            if (action == viewer::kActionAccept) {
                if (promotion_to_caesar) {
                    admin::become_caesar(state);
                    std::printf("you are Caesar\n");
                    ending_caesar = true;
                    screen = Screen::Ending;
                    return;
                } else {
                    admin::accept_promotion(state, sim.difficulty);
                    std::printf("promotion accepted: %s\n", viewer::rank_name(model::global_word(state, admin::kRank)));
                    if (model::global_word(state, 0x6C26) == 1) start_new_province();
                }
            } else if (action == viewer::kActionWait9 || action == viewer::kActionWait24) {
                admin::defer_promotion(state, action == viewer::kActionWait9 ? 9 : 24);
            } else {
                return;
            }
            screen = Screen::City;
            time_running = true;
        } else if (screen == Screen::Battle) {
            if (action >= viewer::kActionTactic && action < viewer::kActionTactic + 4) {
                // The screen draws the generator once a frame while it waits (systems/battle.hpp).
                sim.random.advance();
                battle_view.last = battle::fight_round(state, battle_view.cohort, battle_view.army,
                                                       static_cast<battle::Tactic>(action - viewer::kActionTactic),
                                                       sim.random);
                battle_view.has_round = true;
            } else if (action == viewer::kActionRetreat) {
                battle::retreat(state, battle_view.cohort);
                battle_view.retreated = true;
            } else if (action == viewer::kActionContinue) {
                screen = battle_return;
                time_running = true;
            }
        } else if (screen == Screen::Ending) {
            if (action == viewer::kActionQuit) quit_requested = true;
            if (action == viewer::kActionContinue) screen = Screen::City;  // governing on, time still stopped
        }
        city_image_dirty = province_image_dirty = true;
    };

    // The province toolbar (DS:0x123E): construction ids with the manual's names.
    struct ProvinceCommand {
        const char* label;
        int id;
    };
    static constexpr ProvinceCommand kProvinceCommands[] = {
        {"Clear", 35}, {"Road", 36}, {"Wall", 37},   {"Tower", 41},  {"Highway", 42},
        {"Fort", 29},  {"Halt", 30}, {"Patrol", 31}, {"Attack", 32}, {"Home", 33}};
    constexpr int kProvinceCommandCount = sizeof(kProvinceCommands) / sizeof(kProvinceCommands[0]);
    int province_command = 1;
    int order_cohort = -1;             // the Cohort a Patrol or Attack order is being given to
    int patrol_x = -1, patrol_y = -1;  // the patrol's first point
    systems::construction::DragState province_drag;
    int province_drag_x = -1, province_drag_y = -1;

    // The province actor of a type range standing on a cell (+0x12/+0x13), or -1.
    auto province_actor_at = [&](int x, int y, int type_lo, int type_hi) {
        for (int i = 0; i < model::kActorCount; ++i) {
            const model::Actor& a = state.objects[static_cast<size_t>(i)];
            if (a.active() != 0 && a.type() >= type_lo && a.type() <= type_hi && a.raw_x() == x && a.raw_y() == y)
                return i;
        }
        return -1;
    };
    // One province construction command on a cell, charged the way the build
    // routine charges it: the terrain's cost shift, the pleb gate and the funds.
    auto province_place = [&](int id, int x, int y) {
        namespace province = systems::province;
        namespace economy = systems::economy;
        if (x < 0 || y < 0 || x >= province::kMapW || y >= province::kMapW) return false;
        if (!economy::enough_plebs(state, id)) {
            sim.messages.post(systems::messages::plain(systems::messages::Id::ConstructionPlebs));  // 0x11C8F
            return false;
        }
        if (id == 29 && model::global_word(state, 0x6C12) >= 10) {
            sim.messages.post(systems::messages::plain(systems::messages::Id::NoFort));  // 0x15482
            return false;
        }
        const uint8_t tile = state.empire.cells[static_cast<size_t>(y) * province::kMapW + x] & 0x7F;
        const int cost = economy::kConstructionCost[static_cast<size_t>(id)] << economy::province_cost_shift(id, tile);
        if (!economy::can_afford(state, cost)) {
            if (economy::grant_emergency_funds(state))
                std::printf("Rome sends 500 Dn in emergency funds\n");
            else
                std::printf("not enough funds: %d Dn\n", cost);
            return false;
        }
        province::Built built = province::Built::Refused;
        switch (id) {
            case 35: built = province::clear_province(state, x, y); break;
            case 36: built = province::place_province_road(state, province_drag, x, y); break;
            case 37: built = province::place_great_wall(state, province_drag, x, y); break;
            case 41: built = province::place_great_tower(state, x, y); break;
            case 42: built = province::place_highway(state, province_drag, x, y); break;
            case 29:
                built = province::place_fort(state, x, y) >= 0 ? province::Built::Charged : province::Built::Refused;
                break;
            default: break;
        }
        if (built == province::Built::Charged) economy::charge(state, cost);
        if (built != province::Built::Refused) province_image_dirty = true;
        return built != province::Built::Refused;
    };
    // A click on the province map with the selected command.
    auto province_click = [&](int x, int y) {
        namespace province = systems::province;
        const ProvinceCommand& command = kProvinceCommands[province_command];
        if (command.id < 30 || command.id > 33) {
            const bool ok = province_place(command.id, x, y);
            province_drag_x = x;
            province_drag_y = y;
            std::printf("%s at (%d,%d): %s\n", command.label, x, y, ok ? "OK" : "refused");
            return;
        }
        if (order_cohort < 0) {
            const int cohort = province_actor_at(x, y, province::kCohortType, province::kCohortType);
            if (cohort < 0) {
                std::printf("%s: click a Cohort\n", command.label);
                return;
            }
            if (command.id == 30 || command.id == 33) {
                const bool ok = command.id == 30 ? province::order_halt(state, cohort)
                                                 : province::order_go_home(state, cohort);
                std::printf("%s: %s\n", command.label, ok ? "OK" : "refused");
                return;
            }
            order_cohort = cohort;
            patrol_x = patrol_y = -1;
            std::printf(command.id == 31 ? "Patrol: click the first point\n" : "Attack: click an army\n");
            return;
        }
        bool ok = false;
        if (command.id == 31) {
            if (patrol_x < 0) {
                patrol_x = x;
                patrol_y = y;
                std::printf("Patrol: click the second point\n");
                return;
            }
            ok = province::order_patrol(state, order_cohort, patrol_x, patrol_y, x, y);
        } else {
            const int army = province_actor_at(x, y, province::kArmyType, province::kSeaArmyType);
            if (army < 0) {
                std::printf("Attack: click an army\n");
                return;
            }
            ok = province::order_attack(state, order_cohort, army);
        }
        std::printf("%s: %s\n", command.label, ok ? "OK" : "refused");
        order_cohort = -1;
        patrol_x = patrol_y = -1;
    };
    auto province_label = [&]() -> std::string {
        const ProvinceCommand& command = kProvinceCommands[province_command];
        std::string s = ui::tr(command.label);
        const int cost = systems::economy::kConstructionCost[static_cast<size_t>(command.id)];
        if (cost > 0) s += ", " + std::to_string(cost) + " Dn";
        if (command.id >= 30 && command.id <= 33) {
            // FONT1 has no colon (DS:0F64 draws it as '0').
            if (order_cohort < 0)
                s += ", pick a Cohort";
            else if (command.id == 31)
                s += std::string(", ") + ui::tr(patrol_x < 0 ? "first point" : "second point");
            else
                s += std::string(", ") + ui::tr("pick an army");
        }
        return s + " - Funds " + std::to_string(model::global_word(state, systems::economy::kFunds)) + " Dn";
    };

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 3;
    }

    try {
        const platform::GaiusPaths& gaius_paths = platform::paths();
        std::printf("settings: %s\nsaves: %s\n", gaius_paths.settings.c_str(), gaius_paths.saves.c_str());
    } catch (const std::exception& e) {
        std::fprintf(stderr, "note: Gaius's folders are unavailable: %s\n", e.what());
    }
    platform::open_gamepads();

    if (save_mode) {
        std::printf("save mode -- layer: %s (right-click / two-finger-tap / gamepad B to cycle)\n",
                    show_sprites ? "city view (the game's sprites)" : viewer::layer_label(layer));
        std::printf("build mode -- tool: %s (Tab / gamepad X to cycle, left-click / tap / gamepad A to place)\n",
                    systems::construction::command_name(kBuildTools[tool_index]));
        for (const auto& b : test_builds) {
            if (b.tool < 0 || b.tool >= kBuildToolCount) {
                std::printf("--test-build: tool index %d out of range (0..%d)\n", b.tool, kBuildToolCount - 1);
                continue;
            }
            auto tool = kBuildTools[b.tool];
            bool ok = place_tool(tool, b.x, b.y);
            std::printf("--test-build: place %s at (%d,%d): %s\n", systems::construction::command_name(tool), b.x, b.y,
                        ok ? "OK" : "rejected");
        }
        for (int m = 0; m < run_months; ++m) advance_month();
        std::printf("time: %s (Space / gamepad Y)\n", time_running ? "running, about 2 s a month" : "paused");
    }

    try {
        const auto window_mode_of = [](ui::WindowModeSetting m) {
            return m == ui::WindowModeSetting::Fullscreen   ? platform::WindowMode::Fullscreen
                   : m == ui::WindowModeSetting::Borderless ? platform::WindowMode::Borderless
                                                            : platform::WindowMode::Windowed;
        };
        platform::Window window("Gaius", kLogicalW, kLogicalH, 960, 600,
                                screenshot_path.empty() ? window_mode_of(settings.window_mode)
                                                        : platform::WindowMode::Windowed);
        window.set_vsync(settings.frame_cap == 0);

        // Toolbar. Sized from the physical window (or an explicit
        // --ui-scale), but laid out in LOGICAL coordinates, which is what
        // makes one hit-test correct for mouse, touch and gamepad alike:
        // every device's position reaches it through
        // window.window_to_logical().
        int pw0 = 0, ph0 = 0;
        window.physical_size(&pw0, &ph0);
        // Touch presence is a fact SDL reports; screen size alone is not
        // enough to tell a small window from a small device.
        bool has_touch = SDL_GetNumTouchDevices() > 0;
        ui::Breakpoint bp = ui_scale_override >= 0 ? static_cast<ui::Breakpoint>(ui_scale_override)
                                                   : ui::breakpoint_for(pw0, ph0, has_touch);
        // The Settings screen's UI scale, unless --ui-scale names one.
        const auto toolbar_metrics = [&]() {
            if (ui_scale_override < 0 && settings.ui_scale > 0) return ui::metrics_for_scale(settings.ui_scale);
            return ui::metrics_for(bp);
        };
        ui::Metrics metrics = toolbar_metrics();
        ui::Toolbar toolbar(kBuildTools, kBuildToolCount, metrics, kLogicalW, kLogicalH);
        int hovered = -1;
        if (save_mode) {
            std::printf("toolbar: %s scale=%dx  %d buttons (%dx%d px) in %d row(s), panel %d px tall\n",
                        ui::breakpoint_name(bp), metrics.scale, kBuildToolCount, metrics.button_px(),
                        metrics.button_px(), toolbar.rows(), toolbar.panel().h);
        }

        Camera cam = save_mode ? Camera(viewer::kCityW * city_cell_px, viewer::kCityH * city_cell_px)
                               : Camera(kWorldW, kWorldH);
        if (save_mode) cam.visible_h = kLogicalH - toolbar.panel().h;
        cam.x += test_pan_x;
        cam.y += test_pan_y;
        cam.zoom *= test_zoom;
        cam.clamp();
        std::vector<uint8_t> frame;
        bool running = true;
        int frame_count = 0;

        // Pages, the screen strip and the province bar are laid out at 1x: the
        // same 320 x 200 constraint ui/toolbar.hpp describes -- at 2x a Forum
        // page's dozen rows would not fit the logical screen.
        const ui::Metrics page_metrics = ui::metrics_for(ui::Breakpoint::Desktop);
        const std::vector<ui::PanelButton> screen_tabs = {
            {ui::tr("City"), 900}, {ui::tr("Province"), 901}, {ui::tr("Maps"), 902}, {ui::tr("Forum"), 903}, {ui::tr("Settings"), 904}};
        std::vector<ui::PanelButton> overlay_buttons;
        for (int i = 0; i < viewer::kOverlayCount; ++i) overlay_buttons.push_back({ui::tr(viewer::kOverlayNames[i]), 1100 + i});
        const ui::PanelLayout overlay_bar = ui::bar_layout(overlay_buttons, page_metrics, kLogicalW, kLogicalH);
        const ui::PanelLayout hall_bar = ui::bar_layout({}, page_metrics, kLogicalW, kLogicalH);
        const ui::PanelLayout strip = ui::strip_layout(screen_tabs, page_metrics, kLogicalW);
        std::vector<ui::PanelButton> province_buttons;
        for (int i = 0; i < kProvinceCommandCount; ++i) province_buttons.push_back({ui::tr(kProvinceCommands[i].label), 1000 + i});
        const ui::PanelLayout province_bar = ui::bar_layout(province_buttons, page_metrics, kLogicalW, kLogicalH);
        Camera pcam(40.0 * render::kProvincePx, 40.0 * render::kProvincePx);
        pcam.visible_h = kLogicalH - province_bar.frame.h;
        pcam.clamp();
        int page_hovered = -1;
        const auto strip_hit = [&](int lx, int ly) {
            for (size_t i = 0; i < strip.tabs.size(); ++i)
                if (strip.tabs[i].contains(lx, ly)) return static_cast<int>(i);
            return -1;
        };
        // The message box, and whether it shows on this screen (the city,
        // province and maps views; the messages option DS:0x6C78 on).
        static constexpr ui::Rect kMessageBox{12, 16, 240, 28};
        // Sound (Phase 9, findings section 45): the game's own driver and files
        // on an emulated Sound Blaster, mixed on SDL's audio thread. Without
        // the game's files, a device or with --mute / --screenshot, silence.
        audio::GameAudio game_audio;
        SDL_AudioDeviceID audio_device = 0;
        constexpr int kAudioRate = 44100;
        if (!mute && screenshot_path.empty() && !game_dir.empty() && SDL_InitSubSystem(SDL_INIT_AUDIO) == 0) {
            if (!game_audio.load(game_dir)) std::printf("audio: no SAMPLE.AD in %s, effects only\n", game_dir.c_str());
            SDL_AudioSpec want{};
            want.freq = kAudioRate;
            want.format = AUDIO_S16SYS;
            want.channels = 1;
            want.samples = 1024;
            want.userdata = &game_audio;
            want.callback = [](void* user, Uint8* stream, int len) {
                static_cast<audio::GameAudio*>(user)->render(reinterpret_cast<int16_t*>(stream),
                                                            static_cast<size_t>(len) / 2, 44100);
            };
            game_audio.set_volumes(settings.music_volume, settings.effects_volume);
            audio_device = SDL_OpenAudioDevice(nullptr, 0, &want, nullptr, 0);
            if (audio_device == 0) std::printf("audio: %s\n", SDL_GetError());
            else SDL_PauseAudioDevice(audio_device, 0);
        }
        // The device is closed before game_audio goes: its thread renders from it.
        struct AudioDeviceCloser {
            SDL_AudioDeviceID& id;
            ~AudioDeviceCloser() {
                if (id != 0) SDL_CloseAudioDevice(id);
            }
        } audio_device_closer{audio_device};
        const auto play_effect = [&](int effect) {
            if (audio_device == 0) return;
            SDL_LockAudioDevice(audio_device);
            game_audio.play_effect(effect, game_options.effects());
            SDL_UnlockAudioDevice(audio_device);
        };
        const auto play_music = [&](const char* name) {
            if (audio_device == 0) return;
            SDL_LockAudioDevice(audio_device);
            game_audio.play_music(name, game_options.tunes());
            const bool started = game_audio.music_playing();
            SDL_UnlockAudioDevice(audio_device);
            if (started) std::printf("music: %s\n", name);
        };
        // The tune each screen starts as it opens: the title (0x0F6DC), the
        // game's start (0x0F7F9), the Forum (0x0A475), each advisor (findings
        // section 34.2), the options (0x0ECA3), promotion (0x291C3), Caesar's
        // offer (0x290DF) and dismissal (0x29310).
        const auto advisor_tune = [](viewer::ForumTab tab) -> const char* {
            switch (tab) {
                case viewer::kTreasurer: return "czarjin9.xmi";
                case viewer::kTribune: return "czarjin7.xmi";
                case viewer::kLegion: return "czarjin6.xmi";
                case viewer::kRatings: return "czarjinb.xmi";
                case viewer::kGovernor: return "czarjina.xmi";
                case viewer::kIndustry: return "czarjin5.xmi";
                case viewer::kHistory: return "czarjin4.xmi";
                default: return nullptr;
            }
        };
        Screen tune_screen = screen;
        viewer::ForumTab tune_tab = forum_tab;
        if (screen == Screen::Start) play_music("czartit.xmi");
        const auto screen_tunes = [&]() {
            if (screen == tune_screen && forum_tab == tune_tab) return;
            const Screen from = tune_screen;
            tune_screen = screen;
            const bool tab_changed = forum_tab != tune_tab;
            tune_tab = forum_tab;
            switch (screen) {
                case Screen::City:
                case Screen::Province:
                    if (from == Screen::Start) play_music("starter.xmi");
                    break;
                case Screen::ForumHall: play_music("czarjin8.xmi"); break;
                case Screen::Forum:
                    if (tab_changed || from == Screen::ForumHall || from == Screen::City || from == Screen::Province ||
                        from == Screen::Maps) {
                        if (const char* tune = advisor_tune(forum_tab)) play_music(tune);
                    }
                    break;
                case Screen::Settings:
                    // The original's Options screen tune (0x0ECA3).
                    if (from != Screen::Settings && from != Screen::Files) play_music("czarjin5.xmi");
                    break;
                case Screen::Promotion: play_music(promotion_to_caesar ? "emptune.xmi" : "czarjin1.xmi"); break;
                case Screen::Ending:
                    if (!ending_caesar) play_music("czarjin2.xmi");
                    break;
                default: break;
            }
        };

        const auto message_visible = [&]() {
            return (screen == Screen::City || screen == Screen::Province || screen == Screen::Maps) &&
                   sim.messages.showing() && model::global_word(state, 0x6C78) != 0;
        };
        const auto switch_to = [&](int i) {
            if (i == 4) {
                // The control panel's "Game Options" (command 40, 0x1739D ->
                // 0x0ECA3): Gaius's Settings screen, which holds the original's
                // options (apps/viewer/settings_page.hpp).
                if (screen != Screen::Settings) settings_return = screen;
                settings_view.tab = viewer::kSettingsGame;
                settings_view.confirm = viewer::SettingsView::Confirm::None;
                settings_view.capturing = -1;
                screen = Screen::Settings;
                return;
            }
            screen = i == 0   ? Screen::City
                     : i == 1 ? Screen::Province
                     : i == 2 ? Screen::Maps
                              : (have_forum_picture ? Screen::ForumHall : Screen::Forum);
            order_cohort = patrol_x = patrol_y = -1;
            province_image_dirty = true;
        };

        // The original screens' button tables: which one is running, and
        // what each button does (its handler).
        const auto buttons_key = [&]() -> int {
            if (!save_mode) return -1;
            if (!original_forum_screen()) return -1;
            if (forum_tab == viewer::kGovernor) return 200 + static_cast<int>(governor_dialog);
            if (forum_tab == viewer::kTreasurer) return 1;
            if (forum_tab == viewer::kTribune) return 2;
            if (forum_tab == viewer::kLegion) return 3;
            return -1;
        };
        const auto buttons_for = [&](int key) -> std::vector<ui::Button> {
            if (key == 1) return ui::treasurer_buttons();
            if (key == 2) return ui::tribune_buttons();
            if (key == 3) return ui::legion_buttons();
            if (key == 200) return ui::governor_buttons();
            if (key > 200) return ui::governor_dialog_buttons(static_cast<ui::GovernorDialog>(key - 200));
            if (key >= 100 && key < 200)
                return ui::options_buttons(static_cast<ui::OptionsDialog>(key - 100), game_options,
                                           model::global_word(state, 0x6C78) != 0);
            return {};
        };
        const auto options_changed = [&]() {
            sim.speed = std::clamp(game_options.speed() / 10 * 10, 0, 100);
            if (!game_options.tunes() && audio_device != 0) {  // 0x0F420
                SDL_LockAudioDevice(audio_device);
                game_audio.stop_music();
                SDL_UnlockAudioDevice(audio_device);
            }
            if (!options_path.empty() && !ui::save_options(options_path, game_options))
                std::printf("options: can't write %s\n", options_path.c_str());
        };
        const auto save_settings_now = [&]() {
            // Headless runs (--screenshot) never write the player's settings.
            if (!settings_path.empty() && screenshot_path.empty() && !ui::save_settings(settings_path, settings))
                std::printf("settings: can't write %s\n", settings_path.c_str());
        };
        // Gaius's settings take effect at once.
        const auto apply_settings = [&]() {
            if (screenshot_path.empty()) window.set_mode(window_mode_of(settings.window_mode));
            window.set_vsync(settings.frame_cap == 0);
            metrics = toolbar_metrics();
            toolbar = ui::Toolbar(kBuildTools, kBuildToolCount, metrics, kLogicalW, kLogicalH);
            toolbar.show_tool(tool_index);
            cam.visible_h = kLogicalH - toolbar.panel().h;
            cam.clamp();
            for (const ui::Catalog& c : languages)
                if (c.code == settings.language) ui::set_catalog(c);
            if (audio_device != 0) {
                SDL_LockAudioDevice(audio_device);
                game_audio.set_volumes(settings.music_volume, settings.effects_volume);
                SDL_UnlockAudioDevice(audio_device);
            }
            save_settings_now();
        };
        // The Settings screen's buttons and arrows (apps/viewer/settings_page.hpp).
        const auto on_settings_action = [&](int action) {
            using Confirm = viewer::SettingsView::Confirm;
            if (action >= viewer::kActionSettingsTab && action < viewer::kActionSettingsTab + viewer::kSettingsTabCount) {
                settings_view.tab = static_cast<viewer::SettingsTab>(action - viewer::kActionSettingsTab);
                settings_view.capturing = -1;
                platform::cancel_capture();
                return;
            }
            switch (action) {
                case viewer::kActionResume:  // 0x0F0CC
                    screen = settings_return;
                    return;
                case viewer::kActionPause:  // 0x0F01E: the view without steps until a click or T
                    screen = settings_return;
                    time_running = false;
                    pause_until_click = true;
                    return;
                case viewer::kActionSettingsLoad:
                case viewer::kActionSettingsSave:
                    // 0x0EE07 / 0x0EE93: the original's file dialog (0x0C558);
                    // Gaius's slot page stands in for it.
                    files_return = Screen::Settings;
                    files_saving = action == viewer::kActionSettingsSave;
                    refresh_slots();
                    screen = Screen::Files;
                    return;
                case viewer::kActionRestart: settings_view.confirm = Confirm::Restart; return;  // 0x0F43F
                case viewer::kActionExit: settings_view.confirm = Confirm::Exit; return;        // 0x0ED2C
                case viewer::kActionYes:
                    if (settings_view.confirm == Confirm::Restart) {
                        screen = Screen::Start;
                        time_running = false;
                    } else if (settings_view.confirm == Confirm::Exit) {
                        running = false;
                    }
                    settings_view.confirm = Confirm::None;
                    return;
                case viewer::kActionNo: settings_view.confirm = Confirm::None; return;
                case viewer::kActionResetControls:
                    platform::reset_bindings();
                    viewer::store_bindings(settings);
                    save_settings_now();
                    return;
                case viewer::kActionImportGame: platform::import_game_folder(); return;
                case viewer::kActionRescanGame: return;  // the page looks each time it's drawn
                default: break;
            }
            bool messages_on = model::global_word(state, 0x6C78) != 0;
            int capture = -1;
            const ui::GameOptions before = game_options;
            if (viewer::adjust_setting(action, settings, game_options, messages_on, languages, &capture)) {
                model::set_global_word(state, 0x6C78, messages_on ? 1 : 0);
                if (game_options.words != before.words) options_changed();
                apply_settings();
            } else if (capture >= 0) {
                settings_view.capturing = capture;
                platform::capture_binding(platform::kBindable[capture / 2], capture % 2 != 0);
            }
        };
        const auto on_button = [&](int key, int index) {
            namespace forum = systems::forum;
            if (key == 1) {
                // DS:0x0404 (0x0E7AE-0x0E7E4).
                forum::adjust(state, index < 2 ? forum::Control::PopulationTax : forum::Control::IndustrialTax,
                              index % 2 == 0 ? 1 : -1);
            } else if (key == 2) {
                // DS:0x0494: welfare, a block whose handler returns, the duties.
                if (index < 2) forum::adjust(state, forum::Control::Welfare, index == 0 ? 1 : -1);
                if (index >= 3) {
                    const auto duty = static_cast<forum::Duty>((index - 3) / 2);
                    if ((index - 3) % 2 == 0)
                        forum::raise_duty(state, duty);
                    else
                        forum::lower_duty(state, duty);
                }
            } else if (key == 3) {
                // DS:0x0094.
                if (index == 0) forum::next_cohort(state);
                if (index == 1) forum::toggle_mobilized(state);
                if (index == 2) forum::previous_cohort(state);
                if (index == 3 || index == 4) forum::adjust(state, forum::Control::ArmyWages, index == 3 ? 1 : -1);
                if (index == 5 || index == 6) forum::adjust(state, forum::Control::Conscription, index == 5 ? 1 : -1);
            } else if (key == 200) {
                // DS:0x0444: the name, the requirements, the map, the salary, the donation.
                if (index == 0 && have_name_art) {
                    name_entry = ui::begin_name_entry(governor_name);
                    name_return_to_forum = true;
                    screen = Screen::NameEntry;
                    platform::set_text_entry(true);
                }
                if (index == 1) governor_dialog = ui::GovernorDialog::Requirements;
                if (index == 2 && have_empire_map && have_icons) screen = Screen::EmpireMap;
                if (index == 3) governor_dialog = ui::GovernorDialog::Salary;
                if (index == 4) governor_dialog = ui::GovernorDialog::Donation;
            } else if (key > 200) {
                // DS:0x0144 / 0x0124.
                forum::adjust(state,
                              governor_dialog == ui::GovernorDialog::Salary ? forum::Control::Salary
                                                                            : forum::Control::Donation,
                              index == 0 ? 1 : -1);
            }
        };
        // A headless click on a button: held for a frame, then let go.
        const auto press_button_now = [&](int lx, int ly) {
            const int key = buttons_key();
            active_buttons = buttons_for(key);
            active_buttons_key = key;
            ui::ButtonTracker tracker;
            ui::Pointer p{lx, ly, true, false};
            int fired = ui::process_buttons(active_buttons, tracker, p);
            if (fired >= 0) on_button(key, fired);
            if (buttons_key() == key) {
                p.left_held = false;
                p.left_released = true;
                fired = ui::process_buttons(active_buttons, tracker, p);
                if (fired >= 0) on_button(key, fired);
            }
            active_buttons_key = -1;  // rebuilt on the next frame
        };

        // The Cohort 2 hand-over (0x2236D-0x223DD, then "csr.exe cohort",
        // findings section 44): the battle into its words, the game saved as
        // csr0.dat and cohort.csr naming it, both in the game's folder where
        // cohort.exe looks; the command runs; then the save cohort.csr names
        // is loaded and the result taken back (0x23272).
        const auto run_cohort2 = [&]() {
            namespace battle = systems::battle;
            battle::hand_over(state, battle_screen.cohort, battle_screen.army);
            const fs::path dir = game_dir;
            try {
                formats::save::write(model::serialize(state), (dir / battle::kHandoverSaveName).string());
                if (!battle::write_handover_file((dir / "cohort.csr").string()))
                    throw std::runtime_error("can't write cohort.csr");
                std::printf("Cohort 2: running %s\n", cohort_command.c_str());
                std::fflush(stdout);
                const int status = std::system(cohort_command.c_str());
                std::printf("Cohort 2: finished (%d)\n", status);
                std::string name = battle::read_handover_file((dir / "cohort.csr").string());
                if (name.empty()) name = battle::kHandoverSaveName;
                state = model::load(formats::save::load((dir / name).string()));
                adopt_state();
            } catch (const std::exception& e) {
                std::printf("Cohort 2: %s -- taking the battle back as it was\n", e.what());
            }
            battle::take_back(state);
            screen = battle_return;
            time_running = true;
            city_image_dirty = province_image_dirty = true;
        };

        // One handler for "the primary action happened at this logical
        // point", shared by the real input path and --test-click. The
        // toolbar gets first refusal: a click on the panel selects a tool
        // and must NOT also fall through to the map underneath it.
        auto handle_select_logical = [&](int lx, int ly, bool synthetic) {
            if (!save_mode) return;
            if (pause_until_click) {
                pause_until_click = false;  // 0x0F070: a click ends the pause
                time_running = true;
                return;
            }
            drag_last_x = drag_last_y = -1;
            province_drag_x = province_drag_y = -1;
            drag_undo.clear();
            drag_refund = 0;
            if (buttons_key() >= 0 && ui::button_at(buttons_for(buttons_key()), lx, ly) >= 0) {
                // The screen's buttons answer the mouse once a frame (the
                // per-frame pass below); a headless --test-click presses and
                // lets go at once.
                if (synthetic) press_button_now(lx, ly);
                return;
            }
            if (original_forum_screen() && governor_dialog != ui::GovernorDialog::None) {
                // Any other click ends a governor dialog (the original ends
                // it on a right-click).
                close_governor_dialog();
                return;
            }
            if (original_forum_screen()) {
                // The original's advisor screens: a click that isn't a button
                // goes back to the Forum picture (the original leaves on a
                // right-click).
                if (forum_tab == viewer::kRatings) {
                    // 0x0CF12: a click among the columns asks for advice.
                    if (ly >= systems::forum::kRatingHintTop && ly < systems::forum::kRatingHintBottom) {
                        hint_cycle = systems::forum::next_hint_cycle(hint_cycle);
                        rating_hint = systems::forum::rating_hint(state, lx, hint_cycle, sim.linked_towns,
                                                                  sim.random.walk);
                        hint_frames = systems::forum::kRatingHintFrames;
                        return;
                    }
                }
                screen = have_forum_picture ? Screen::ForumHall : Screen::City;
                return;
            }
            if (screen == Screen::Notice && have_interface_art) {
                apply_page_action(viewer::kActionContinue);  // 0x084B1 waits for a click
                return;
            }
            if (page_screen()) {
                const ui::Page page = current_page();
                const int action = ui::hit_test(page, ui::layout(page, page_metrics, kLogicalW, kLogicalH), lx, ly);
                if (action >= 0) {
                    if (screen == Screen::Settings) {
                        on_settings_action(action);
                    } else {
                        apply_page_action(action);
                    }
                }
                return;
            }
            if (screen == Screen::EmpireMap) {
                screen = Screen::Forum;  // 0x0D174 waits for a click
                return;
            }
            if (screen == Screen::NameEntry) {
                ui::name_click(name_entry, lx, ly);
                return;
            }
            if (screen == Screen::Battle && have_battle_art && battle_screen.cohort2 != ui::Cohort2Offer::None) {
                if (ui::answer_cohort2(battle_screen, lx, ly, false) == ui::Cohort2Answer::Accepted)
                    run_cohort2();
                return;
            }
            if (screen == Screen::Battle && have_battle_art) {
                // 0x2250F's buttons, or the retreat dialog's; anything else
                // cuts a message short.
                if (battle_screen.confirming_retreat) {
                    ui::answer_retreat(battle_screen, state, battle_art, lx, ly);
                } else if (const int button = ui::button_at(battle_screen, lx, ly); button == ui::kRetreatButton) {
                    battle_screen.confirming_retreat = true;
                } else if (button >= 0) {
                    ui::play_round(battle_screen, state, battle_art, static_cast<systems::battle::Tactic>(button),
                                   sim.random);
                } else {
                    battle_clicked = true;
                }
                city_image_dirty = province_image_dirty = true;
                return;
            }
            if (const int tab = strip_hit(lx, ly); tab >= 0) {
                switch_to(tab);
                return;
            }
            if (message_visible() && kMessageBox.contains(lx, ly)) {
                // 0x0F982: a message with a place takes the view there (its
                // cell less 10 and 5), and the message goes.
                const systems::messages::Message m = sim.messages.current;
                if (m.place != systems::messages::Place::None) {
                    sim.messages.timer = 0;
                    if (m.place == systems::messages::Place::City) {
                        screen = Screen::City;
                        cam.x = (m.x - 10) * city_cell_px;
                        cam.y = (m.y - 5) * city_cell_px;
                        cam.clamp();
                    } else {
                        screen = Screen::Province;
                        pcam.x = (m.x - 10) * render::kProvincePx;
                        pcam.y = (m.y - 5) * render::kProvincePx;
                        pcam.clamp();
                        province_image_dirty = true;
                    }
                }
                return;
            }
            if (screen == Screen::ForumHall) {
                // 0x0DF57: the figure under the click opens its advisor
                // (findings section 34.2). Those Gaius has no page for say so.
                const int region = forum_clicks.region_at(lx, ly);
                // The statue opens after "c" then "B" (0x0DF9B reads the key
                // words 2EF9:0031 and 2EF9:002F), or with --cheats.
                const bool statue_keys = key_prev == 'c' && key_last == 'B';
                const int tab = region == 1   ? (cheats || statue_keys ? viewer::kStatue : -1)
                                : region == 2 ? viewer::kGovernor
                                : region == 3 ? viewer::kLegion
                                : region == 4 ? viewer::kHistory
                                : region == 5 ? viewer::kTreasurer
                                : region == 6 ? viewer::kRatings
                                : region == 7 ? viewer::kTribune
                                : region == 8 ? viewer::kIndustry
                                              : -1;
                if (tab >= 0) {
                    forum_tab = static_cast<viewer::ForumTab>(tab);
                    rating_hint = 0;
                    governor_dialog = ui::GovernorDialog::None;
                    if (forum_tab == viewer::kIndustry) systems::forum::open_industry_report(state);
                    screen = Screen::Forum;
                }
                return;
            }
            if (screen == Screen::Maps && have_maps_art) {
                // DS:0x0704's buttons, or a click on the map (0x0DCB4), which
                // takes the city view there.
                bool toggle_city = false;
                int map_x = 0, map_y = 0;
                if (ui::maps_button_at(lx, ly, toggle_city, map_mode)) {
                    if (toggle_city) map_shows_city = !map_shows_city;
                } else if (ui::maps_cell_at(lx, ly, map_x, map_y)) {
                    screen = Screen::City;
                    cam.x = (map_x - 10) * city_cell_px;
                    cam.y = (map_y - 5) * city_cell_px;
                    cam.clamp();
                }
                return;
            }
            if (screen == Screen::Maps) {
                for (size_t i = 0; i < overlay_bar.buttons.size(); ++i) {
                    if (overlay_bar.buttons[i].contains(lx, ly)) overlay = static_cast<int>(i);
                }
                return;
            }
            if (screen == Screen::Province) {
                for (size_t i = 0; i < province_bar.buttons.size(); ++i) {
                    if (province_bar.buttons[i].contains(lx, ly)) {
                        province_command = static_cast<int>(i);
                        order_cohort = patrol_x = patrol_y = -1;
                        std::printf("province command: %s\n", kProvinceCommands[i].label);
                        play_effect(0);  // 0x0FFA5
                        return;
                    }
                }
                if (province_bar.frame.contains(lx, ly)) return;
                province_click(static_cast<int>(pcam.x + lx / pcam.zoom) / render::kProvincePx,
                               static_cast<int>(pcam.y + ly / pcam.zoom) / render::kProvincePx);
                return;
            }
            int hit = toolbar.hit_test(lx, ly);
            if (hit == ui::kPreviousPage || hit == ui::kNextPage) {
                toolbar.turn_page(hit == ui::kNextPage ? 1 : -1);
                return;
            }
            if (hit >= 0) {
                if (hit == tool_index && cycle_variant()) return;  // tapping the selected button again
                tool_index = hit;
                std::printf("tool: %s\n", systems::construction::command_name(kBuildTools[tool_index]));
                play_effect(0);  // 0x0FFA5
                return;
            }
            if (toolbar.contains(lx, ly)) return;  // panel background, not a button

            int cell_x = static_cast<int>(cam.x + lx / cam.zoom) / city_cell_px;
            int cell_y = static_cast<int>(cam.y + ly / cam.zoom) / city_cell_px;
            auto tool = kBuildTools[tool_index];
            bool ok = place_tool(tool, cell_x, cell_y);
            if (ok) city_image_dirty = true;
            drag_last_x = cell_x;
            drag_last_y = cell_y;
            std::printf("place %s at (%d,%d): %s\n", systems::construction::command_name(tool), cell_x, cell_y,
                        ok ? "OK" : "rejected (terrain not buildable / off grid)");
        };

        if (save_mode && test_message > 0) {
            const auto id = static_cast<systems::messages::Id>(test_message);
            sim.messages.post(systems::messages::at(id, systems::messages::Place::City, 50, 50));
            if (model::global_word(state, 0x6C78) == 0) model::set_global_word(state, 0x6C78, 1);
        }
        if (save_mode && test_promotion) {
            model::set_global_word(state, 0x6CA4, (model::global_word(state, 0x6CA6) + 1) % 50);
            sim.on_promotion(state, false);
        }
        if (save_mode && test_battle) {
            // A five-strong army on the first Cohort's cell, in the first free slot.
            int cohort = -1, army = -1;
            for (int i = 0; i < model::kActorCount; ++i) {
                const model::Actor& a = state.objects[static_cast<size_t>(i)];
                if (a.active() != 0 && a.type() == systems::province::kCohortType && cohort < 0) cohort = i;
                if (a.active() == 0 && army < 0) army = i;
            }
            if (cohort >= 0 && army >= 0) {
                model::Actor& a = state.objects[static_cast<size_t>(army)];
                a.raw = state.objects[static_cast<size_t>(cohort)].raw;
                a.raw[0x07] = systems::province::kArmyType;
                a.raw[0x08] = static_cast<uint8_t>(army);
                a.raw[0x09] = 0;
                a.raw[systems::battle::kArmySize] = 5;
                a.raw[0x31] = systems::province::kLinger;
                sim.on_battle(state, cohort, army);
            } else {
                std::printf("--test-battle: no Cohort or no free slot\n");
            }
        }
        for (const auto& c : test_clicks) {
            std::printf("--test-click (%d,%d): ", c.x, c.y);
            handle_select_logical(c.x, c.y, true);
        }
        for (int action : test_actions) {
            std::printf("--test-action %d\n", action);
            if (save_mode && screen == Screen::Settings) {
                on_settings_action(action);
            } else if (save_mode && page_screen()) {
                apply_page_action(action);
            }
        }

        Uint32 last_step_ms = SDL_GetTicks();
        Uint32 last_pad_ms = SDL_GetTicks();
        Uint32 last_frame_ms = SDL_GetTicks();
        double pad_pointer_x = 0, pad_pointer_y = 0;
        bool pad_pointer_moved = false;
        while (running) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_KEYDOWN) {
                    // 0x3029B: each key read moves the last one to 2EF9:0031.
                    const SDL_Keycode sym = event.key.keysym.sym;
                    char c = 0;
                    if (sym >= SDLK_a && sym <= SDLK_z) {
                        c = static_cast<char>('a' + (sym - SDLK_a));
                        if ((event.key.keysym.mod & KMOD_SHIFT) != 0) c = static_cast<char>(c - 'a' + 'A');
                    } else if (sym >= 32 && sym < 127) {
                        c = static_cast<char>(sym);
                    }
                    key_prev = key_last;
                    key_last = c;
                    if (pause_until_click && (c == 't' || c == 'T')) {
                        pause_until_click = false;  // 0x0F07E: T ends the pause too
                        time_running = true;
                    }
                }
                int pw, ph;
                window.physical_size(&pw, &ph);
                auto cmd = platform::translate_event(event, pw, ph);
                if (!cmd) continue;

                switch (cmd->type) {
                    case platform::CommandType::TextKey:
                        if (save_mode && screen == Screen::NameEntry) {
                            const ui::NameKey key = static_cast<ui::NameKey>(static_cast<int>(cmd->text_key));
                            if (ui::name_key(name_entry, key, cmd->ch) != 0) {
                                // Escape or Enter: the dialog edits in place, so both keep it.
                                governor_name = ui::name_text(name_entry);
                                if (name_return_to_forum) ui::set_governor_name(state, governor_name);
                                screen = name_return_to_forum ? Screen::Forum : Screen::Start;
                                name_return_to_forum = false;
                                platform::set_text_entry(false);
                            }
                        }
                        break;
                    case platform::CommandType::Quit:
                        running = false;
                        break;
                    case platform::CommandType::Menu:
                        // Escape, gamepad Start, Android's Back: the Settings
                        // screen, or back out of it.
                        if (!save_mode) {
                            running = false;
                        } else if (screen == Screen::Settings) {
                            if (settings_view.confirm != viewer::SettingsView::Confirm::None)
                                settings_view.confirm = viewer::SettingsView::Confirm::None;
                            else if (settings_return != Screen::Start)
                                screen = settings_return;
                        } else if (screen != Screen::NameEntry) {
                            switch_to(4);
                        }
                        break;
                    case platform::CommandType::Rebound:
                        settings_view.capturing = -1;
                        viewer::store_bindings(settings);
                        save_settings_now();
                        break;
                    case platform::CommandType::PreviousTool:
                        if (save_mode && screen == Screen::Province) {
                            province_command = (province_command + kProvinceCommandCount - 1) % kProvinceCommandCount;
                            order_cohort = patrol_x = patrol_y = -1;
                            play_effect(0);
                        } else if (save_mode && screen == Screen::City) {
                            tool_index = (tool_index + kBuildToolCount - 1) % kBuildToolCount;
                            toolbar.show_tool(tool_index);
                            play_effect(0);
                        }
                        break;
                    case platform::CommandType::ToggleWindowMode: {
                        auto next = window.mode() == platform::WindowMode::Windowed ? platform::WindowMode::Borderless
                                    : window.mode() == platform::WindowMode::Borderless
                                        ? platform::WindowMode::Fullscreen
                                        : platform::WindowMode::Windowed;
                        window.set_mode(next);
                        break;
                    }
                    case platform::CommandType::CycleScreen:
                        if (save_mode && (!page_screen() || screen == Screen::Forum))
                            switch_to(screen == Screen::City ? 1 : screen == Screen::Province ? 2
                                                              : screen == Screen::Maps    ? 3
                                                                                          : 0);
                        break;
                    case platform::CommandType::Secondary:
                        if (save_mode && pause_until_click) {
                            pause_until_click = false;
                            time_running = true;
                            break;
                        }
                        if (save_mode && screen == Screen::Settings) {
                            // DS:0x6D4C: a right click leaves the menu, or a
                            // question back to it.
                            if (settings_view.capturing >= 0) {
                                settings_view.capturing = -1;
                                platform::cancel_capture();
                            } else if (settings_view.confirm != viewer::SettingsView::Confirm::None) {
                                settings_view.confirm = viewer::SettingsView::Confirm::None;
                            } else if (settings_return != Screen::Start) {
                                screen = settings_return;
                            }
                            break;
                        }
                        if (save_mode && message_visible()) {
                            // 0x0F93A: a right-click on the message dismisses it.
                            int mx = 0, my = 0;
                            if (window.window_to_logical(cmd->x, cmd->y, &mx, &my) && kMessageBox.contains(mx, my)) {
                                sim.messages.timer = 1;
                                sim.messages.current.place = systems::messages::Place::None;
                                break;
                            }
                        }
                        if (save_mode && screen == Screen::ForumHall) {
                            screen = Screen::City;  // right-click leaves the Forum
                            break;
                        }
                        if (save_mode && screen == Screen::EmpireMap) {
                            screen = Screen::Forum;
                            break;
                        }
                        if (save_mode && original_forum_screen() && governor_dialog != ui::GovernorDialog::None) {
                            close_governor_dialog();  // DS:0x6D4C
                            break;
                        }
                        if (save_mode && original_forum_screen()) {
                            screen = have_forum_picture ? Screen::ForumHall : Screen::City;
                            break;
                        }
                        if (save_mode && screen == Screen::NameEntry) {
                            // 0x0C544: a right-click ends the dialog, keeping the name.
                            governor_name = ui::name_text(name_entry);
                            if (name_return_to_forum) ui::set_governor_name(state, governor_name);
                            screen = name_return_to_forum ? Screen::Forum : Screen::Start;
                            name_return_to_forum = false;
                            platform::set_text_entry(false);
                            break;
                        }
                        if (save_mode && screen == Screen::Battle && have_battle_art &&
                            battle_screen.cohort2 != ui::Cohort2Offer::None) {
                            ui::answer_cohort2(battle_screen, 0, 0, true);
                            break;
                        }
                        if (save_mode && screen == Screen::Battle && have_battle_art) {
                            battle_clicked = true;
                            break;
                        }
                        if (save_mode && screen == Screen::Province) {
                            // Right-click forgets an order being given.
                            order_cohort = patrol_x = patrol_y = -1;
                            break;
                        }
                        if (save_mode && screen == Screen::City) {
                            // With sprites: city view, then each data layer, then back.
                            if (show_sprites) {
                                show_sprites = false;
                                layer = viewer::kSaveLayerOrder[0];
                            } else if (have_sprites && viewer::next_layer(layer) == viewer::kSaveLayerOrder[0]) {
                                show_sprites = true;
                            } else {
                                layer = viewer::next_layer(layer);
                            }
                            std::printf("layer: %s\n",
                                        show_sprites ? "city view (the game's sprites)" : viewer::layer_label(layer));
                        }
                        break;
                    case platform::CommandType::ToggleTime:
                        if (save_mode) {
                            time_running = !time_running;
                            std::printf("time: %s\n", time_running ? "running" : "paused");
                        }
                        break;
                    case platform::CommandType::CycleVariant:
                        if (save_mode) cycle_variant();
                        break;
                    case platform::CommandType::CycleTool:
                        if (save_mode && screen == Screen::Province) {
                            province_command = (province_command + 1) % kProvinceCommandCount;
                            order_cohort = patrol_x = patrol_y = -1;
                            std::printf("province command: %s\n", kProvinceCommands[province_command].label);
                            play_effect(0);
                        } else if (save_mode && screen == Screen::City) {
                            tool_index = (tool_index + 1) % kBuildToolCount;
                            toolbar.show_tool(tool_index);
                            std::printf("build tool: %s\n",
                                        systems::construction::command_name(kBuildTools[tool_index]));
                            play_effect(0);
                        }
                        break;
                    case platform::CommandType::Select: {
                        if (!save_mode) break;
                        // Physical click -> logical framebuffer -> toolbar or
                        // world -> grid cell, reusing the same camera math the
                        // renderer samples through, so mouse, touch tap and
                        // gamepad A all land on the same button or cell.
                        int lx = 0, ly = 0;
                        if (!window.window_to_logical(cmd->x, cmd->y, &lx, &ly)) break;
                        handle_select_logical(lx, ly, false);
                        break;
                    }
                    case platform::CommandType::SelectMove: {
                        // Dragging a drag-built command: the engine calls its
                        // handler for each cell the cursor passes over, so step
                        // one cell at a time from the last placed cell.
                        if (save_mode && screen == Screen::Province) {
                            // Clear, Road, Wall and Highway follow the pointer cell by cell.
                            const int id = kProvinceCommands[province_command].id;
                            if (province_drag_x < 0 || !(id == 35 || id == 36 || id == 37 || id == 42)) break;
                            int lx = 0, ly = 0;
                            if (!window.window_to_logical(cmd->x, cmd->y, &lx, &ly) || province_bar.frame.contains(lx, ly))
                                break;
                            const int tx = static_cast<int>(pcam.x + lx / pcam.zoom) / render::kProvincePx;
                            const int ty = static_cast<int>(pcam.y + ly / pcam.zoom) / render::kProvincePx;
                            while (province_drag_x != tx || province_drag_y != ty) {
                                const int ddx = tx - province_drag_x, ddy = ty - province_drag_y;
                                if (std::abs(ddx) >= std::abs(ddy))
                                    province_drag_x += ddx > 0 ? 1 : -1;
                                else
                                    province_drag_y += ddy > 0 ? 1 : -1;
                                province_place(id, province_drag_x, province_drag_y);
                            }
                            break;
                        }
                        if (!save_mode || screen != Screen::City || drag_last_x < 0) break;
                        const auto tool = kBuildTools[tool_index];
                        if (systems::construction::placement_spec(tool).kind !=
                            systems::construction::PlacementKind::DragAutoTiled) {
                            break;
                        }
                        int lx = 0, ly = 0;
                        if (!window.window_to_logical(cmd->x, cmd->y, &lx, &ly) || toolbar.contains(lx, ly)) break;
                        const int tx = static_cast<int>(cam.x + lx / cam.zoom) / city_cell_px;
                        const int ty = static_cast<int>(cam.y + ly / cam.zoom) / city_cell_px;
                        while (drag_last_x != tx || drag_last_y != ty) {
                            const int ddx = tx - drag_last_x, ddy = ty - drag_last_y;
                            if (std::abs(ddx) >= std::abs(ddy)) {
                                drag_last_x += ddx > 0 ? 1 : -1;
                            } else {
                                drag_last_y += ddy > 0 ? 1 : -1;
                            }
                            if (place_tool(tool, drag_last_x, drag_last_y)) city_image_dirty = true;
                        }
                        break;
                    }
                    case platform::CommandType::CancelDrag: {
                        // The original's drag-cancel gesture (see DragUndoCell's
                        // comment): put back every cell this drag touched and
                        // refund what it cost. Does nothing outside an active
                        // drag-built placement.
                        if (!save_mode || drag_undo.empty()) break;
                        for (const auto& u : drag_undo) {
                            state.city.tile[u.y][u.x] = u.tile;
                            state.city.operational_state[u.y][u.x] = u.op_state;
                        }
                        systems::economy::refund(state, drag_refund);
                        std::printf("drag cancelled: refunded %d Dn\n", drag_refund);
                        drag_undo.clear();
                        drag_refund = 0;
                        drag_last_x = drag_last_y = -1;
                        city_image_dirty = true;
                        break;
                    }
                    case platform::CommandType::Hover: {
                        if (!save_mode) break;
                        int lx = 0, ly = 0;
                        const bool inside = window.window_to_logical(cmd->x, cmd->y, &lx, &ly);
                        hovered = inside && screen == Screen::City ? toolbar.hit_test(lx, ly) : -1;
                        page_hovered = -1;
                        hall_hover = inside && screen == Screen::ForumHall ? forum_clicks.region_at(lx, ly) : 0;
                        if (inside && page_screen()) {
                            const ui::Page page = current_page();
                            page_hovered =
                                ui::hit_test(page, ui::layout(page, page_metrics, kLogicalW, kLogicalH), lx, ly);
                        }
                        break;
                    }
                    case platform::CommandType::PanBegin:
                        break;
                    case platform::CommandType::PanEnd:
                        break;
                    case platform::CommandType::PanMove: {
                        Camera& c = screen == Screen::Province ? pcam : cam;
                        c.x -= cmd->dx / c.zoom;
                        c.y -= cmd->dy / c.zoom;
                        c.clamp();
                        break;
                    }
                    case platform::CommandType::Zoom: {
                        Camera& c = screen == Screen::Province ? pcam : cam;
                        c.zoom *= std::pow(1.1, cmd->zoom_delta);
                        c.clamp();
                        break;
                    }
                    default:
                        break;
                }
            }

            if (save_mode) {
                // The original screens' buttons, once a frame (0x0D41D).
                const int key = buttons_key();
                if (key != active_buttons_key) {
                    active_buttons = buttons_for(key);
                    button_tracker = ui::ButtonTracker{};
                    active_buttons_key = key;
                }
                int wx = 0, wy = 0, lx = -1, ly = -1;
                const Uint32 mouse = SDL_GetMouseState(&wx, &wy);
                if (!window.window_to_logical(wx, wy, &lx, &ly)) lx = ly = -1;
                pointer.x = lx;
                pointer.y = ly;
                pointer.left_held = (mouse & SDL_BUTTON_LMASK) != 0 ||
                                    platform::gamepad_button_down(platform::button_for(platform::CommandType::Select));
                pointer.left_released = pointer_was_held && !pointer.left_held;
                pointer_was_held = pointer.left_held;
                if (key >= 0 && !active_buttons.empty()) {
                    const int fired = ui::process_buttons(active_buttons, button_tracker, pointer);
                    if (fired >= 0) on_button(key, fired);
                }
            }

            // Gamepads (Phase 9): the left stick moves the pointer -- the
            // mouse, so every screen answers it as it answers a mouse -- and
            // holding A drags; the right stick and the d-pad pan the map, the
            // triggers zoom. With the pointer off, the left stick pans too.
            if (platform::gamepad_connected()) {
                const Uint32 pad_now = SDL_GetTicks();
                const double dt = std::min(0.1, (pad_now - last_pad_ms) / 1000.0);
                last_pad_ms = pad_now;
                const platform::GamepadAxes axes = platform::gamepad_axes();
                double pan_x = axes.right_x, pan_y = axes.right_y;
                if (platform::gamepad_button_down(SDL_CONTROLLER_BUTTON_DPAD_LEFT)) pan_x = -1;
                if (platform::gamepad_button_down(SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) pan_x = 1;
                if (platform::gamepad_button_down(SDL_CONTROLLER_BUTTON_DPAD_UP)) pan_y = -1;
                if (platform::gamepad_button_down(SDL_CONTROLLER_BUTTON_DPAD_DOWN)) pan_y = 1;
                if (!settings.gamepad_cursor) {
                    pan_x += axes.left_x;
                    pan_y += axes.left_y;
                }
                const bool map_screen = screen == Screen::City || screen == Screen::Province;
                if (map_screen && (pan_x != 0 || pan_y != 0 || axes.left_trigger > 0 || axes.right_trigger > 0)) {
                    Camera& c = screen == Screen::Province ? pcam : cam;
                    constexpr double kPanPerSecond = 240.0;  // logical pixels at full tilt
                    c.x += pan_x * kPanPerSecond * dt / c.zoom;
                    c.y += pan_y * kPanPerSecond * dt / c.zoom;
                    c.zoom *= std::pow(2.0, (axes.right_trigger - axes.left_trigger) * dt);
                    c.clamp();
                }
                if (settings.gamepad_cursor && (axes.left_x != 0 || axes.left_y != 0)) {
                    int pw = 0, ph = 0;
                    window.physical_size(&pw, &ph);
                    int mx = 0, my = 0;
                    SDL_GetMouseState(&mx, &my);
                    // Half the screen's width a second at full tilt.
                    const double speed = pw * 0.5;
                    pad_pointer_x = std::clamp(pad_pointer_x + axes.left_x * speed * dt, 0.0, pw - 1.0);
                    pad_pointer_y = std::clamp(pad_pointer_y + axes.left_y * speed * dt, 0.0, ph - 1.0);
                    if (std::abs(static_cast<int>(pad_pointer_x) - mx) > 2 || std::abs(static_cast<int>(pad_pointer_y) - my) > 2) {
                        // Not where the mouse is: someone moved the mouse; start from it.
                        if (!pad_pointer_moved) {
                            pad_pointer_x = mx + axes.left_x * speed * dt;
                            pad_pointer_y = my + axes.left_y * speed * dt;
                        }
                    }
                    pad_pointer_moved = true;
                    const int nx = static_cast<int>(pad_pointer_x), ny = static_cast<int>(pad_pointer_y);
                    SDL_WarpMouseInWindow(window.sdl_window(), nx, ny);
                    // Holding the Select button drags, as a held mouse button does.
                    if (platform::gamepad_button_down(platform::button_for(platform::CommandType::Select))) {
                        SDL_Event motion{};
                        motion.type = SDL_MOUSEMOTION;
                        motion.motion.windowID = SDL_GetWindowID(window.sdl_window());
                        motion.motion.x = nx;
                        motion.motion.y = ny;
                        motion.motion.state = SDL_BUTTON_LMASK;
                        SDL_PushEvent(&motion);
                    }
                } else {
                    pad_pointer_moved = false;
                    int mx = 0, my = 0;
                    SDL_GetMouseState(&mx, &my);
                    pad_pointer_x = mx;
                    pad_pointer_y = my;
                }
            }

            if (time_running) {
                const Uint32 now = SDL_GetTicks();
                // A battle or a promotion offer stops time mid-step.
                for (int budget = 8; budget > 0 && time_running && now - last_step_ms >= kStepMs; --budget) {
                    const bool stepped = advance_step();
                    last_step_ms += kStepMs;
                    // 0x0FFD4: the city sounds, from what the city view drew.
                    if (stepped && screen == Screen::City && !game_options.city_sounds_off()) {
                        const int col0 = static_cast<int>(cam.x) / city_cell_px;
                        const int row0 = static_cast<int>(cam.y) / city_cell_px;
                        const int cols = static_cast<int>(kLogicalW / cam.zoom) / city_cell_px + 1;
                        const int rows = static_cast<int>(cam.visible_h / cam.zoom) / city_cell_px + 1;
                        const int effect = systems::sounds::city_sound_effect(
                            sim.ticks % 512, render::city_sound_flags(state, col0, row0, cols, rows));
                        if (effect >= 0) systems::sounds::request(effect);
                    }
                    if (sim.funds_warning) {
                        // 0x0FAD3: the one full-screen warning, which waits for a click.
                        sim.funds_warning = false;
                        notice_return = screen;
                        screen = Screen::Notice;
                        time_running = false;
                    }
                    if (sim.dismissed) {
                        sim.dismissed = false;
                        ending_caesar = false;
                        screen = Screen::Ending;
                        time_running = false;
                        std::printf("dismissed: three tributes missed\n");
                    }
                }
                if (now - last_step_ms >= kStepMs) last_step_ms = now;  // behind: drop the backlog
            }
            // The effects this frame asked for, in order: one sound at a time,
            // so the last is the one heard.
            for (const int effect : systems::sounds::take()) play_effect(effect);
            screen_tunes();

            const std::string tool_text = save_mode ? tool_label() : std::string();
            const std::string funds_text =
                save_mode ? std::string(ui::tr("Funds")) + " " + std::to_string(model::global_word(state, systems::economy::kFunds)) + " Dn"
                          : std::string();
            const ui::GameFont* font = have_font ? &game_font : nullptr;
            if (save_mode && (page_screen() || screen == Screen::NameEntry)) {
                const ui::Page page = current_page();
                frame.assign(static_cast<size_t>(kLogicalW) * kLogicalH * 3, 0);
                ui::render(page, ui::layout(page, page_metrics, kLogicalW, kLogicalH), frame, kLogicalW, kLogicalH,
                           page_metrics, font, page_hovered);
                if (screen == Screen::NameEntry) ui::compose_name_entry(name_entry, name_art, frame);
                if (screen == Screen::Notice && have_interface_art)
                    ui::canvas_to_rgb(ui::compose_funds_warning_screen(interface_art), interface_art.palette, frame);
                if (original_forum_screen()) {
                    // The advisors in the original's art.
                    formats::IndexedImage advisor =
                        forum_tab == viewer::kHistory     ? ui::compose_history_screen(state, interface_art)
                        : forum_tab == viewer::kIndustry  ? ui::compose_industry_screen(state, interface_art)
                        : forum_tab == viewer::kTribune   ? ui::compose_tribune_screen(state, interface_art)
                        : forum_tab == viewer::kRatings
                            ? ui::compose_ratings_screen(state, interface_art, ratings_art, rating_hint)
                        : forum_tab == viewer::kGovernor
                            ? ui::compose_governor_dialog(state, interface_art, governor_picture, governor_dialog)
                        : forum_tab == viewer::kLegion
                            ? ui::compose_legion_screen(state, interface_art, province_sprites.units,
                                                        static_cast<int>(sim.ticks))
                            : ui::compose_treasurer_screen(state, interface_art);
                    if (buttons_key() >= 0 && buttons_key() == active_buttons_key)
                        ui::draw_buttons(advisor, interface_art.blocks, active_buttons, button_tracker, pointer);
                    ui::canvas_to_rgb(advisor,
                                      forum_tab == viewer::kRatings ? ratings_art.palette : interface_art.palette, frame);
                    if (forum_tab == viewer::kRatings && hint_frames > 0 && --hint_frames == 0) rating_hint = 0;
                }
            } else if (save_mode && screen == Screen::Battle && have_battle_art &&
                       battle_screen.cohort2 != ui::Cohort2Offer::None) {
                // The offer and its question wait outside the battle's loop.
                ui::compose_battle(battle_screen, state, battle_art, frame);
            } else if (save_mode && screen == Screen::Battle && have_battle_art) {
                // 0x2244B, once a frame: the generator draws, then the screen.
                sim.random.advance();
                ui::battle_frame(battle_screen, state, battle_art, battle_clicked);
                battle_clicked = false;
                if (battle_screen.finished() ||
                    (battle_screen.ended() && battle_screen.closing == 0 && battle_screen.message_timer == 0)) {
                    screen = battle_return;
                    time_running = true;
                    city_image_dirty = province_image_dirty = true;
                }
                ui::compose_battle(battle_screen, state, battle_art, frame);
            } else if (save_mode && screen == Screen::EmpireMap) {
                // 0x09276 + 0x0D21E: the map of the Empire and the provinces given.
                formats::IndexedImage empire;
                render::render_empire_map(empire_picture, toolbar_icons, empire_markers, state.table_50,
                                          model::global_word(state, 0x6CA6), empire);
                frame.resize(static_cast<size_t>(kLogicalW) * kLogicalH * 3);
                for (size_t i = 0; i < empire.pixels.size() && i * 3 + 2 < frame.size(); ++i) {
                    const formats::RGB c = empire_palette.colors[empire.pixels[i]];
                    frame[i * 3] = c.r;
                    frame[i * 3 + 1] = c.g;
                    frame[i * 3 + 2] = c.b;
                }
            } else if (save_mode && screen == Screen::ForumHall) {
                // The original's Forum picture, clicked through its own click map.
                frame.resize(static_cast<size_t>(kLogicalW) * kLogicalH * 3);
                for (size_t i = 0; i < forum_picture.pixels.size() && i * 3 + 2 < frame.size(); ++i) {
                    const formats::RGB c = forum_palette.colors[forum_picture.pixels[i]];
                    frame[i * 3] = c.r;
                    frame[i * 3 + 1] = c.g;
                    frame[i * 3 + 2] = c.b;
                }
                static constexpr const char* kFigures[] = {"Choose an advisor",       "The statue",
                                                            "The governor's affairs",  "The Military Advisor",
                                                            "The histories",           "The Treasurer",
                                                            "The ratings",             "The Tribune of the Plebs",
                                                            "Industry"};
                ui::render_bar({}, -1, hall_bar, frame, kLogicalW, kLogicalH, page_metrics, font,
                               kFigures[std::clamp(hall_hover, 0, 8)]);
                ui::render_strip(screen_tabs, 3, strip, frame, kLogicalW, kLogicalH, page_metrics, font);
            } else if (save_mode && screen == Screen::Province) {
                if (have_province_sprites) {
                    if (province_image_dirty) {
                        render::render_province(state.empire, province_sprites, state.objects, province_image);
                        province_image_dirty = false;
                    }
                    render_sprite_view(province_image, province_sprites.palette, pcam, frame);
                } else {
                    render_empire_frame(state.empire, pcam, frame);
                }
                const std::string label = province_label();
                ui::render_bar(province_buttons, province_command, province_bar, frame, kLogicalW, kLogicalH,
                               page_metrics, font, label.c_str());
                ui::render_strip(screen_tabs, 1, strip, frame, kLogicalW, kLogicalH, page_metrics, font);
            } else if (save_mode && screen == Screen::Maps && have_maps_art) {
                // 0x0B717: the original maps screen.
                ui::compose_maps_screen(state.city, map_mode, map_shows_city, maps_art, frame);
                ui::render_strip(screen_tabs, 2, strip, frame, kLogicalW, kLogicalH, page_metrics, font);
            } else if (save_mode && screen == Screen::Maps) {
                // The city under the overlay: cells it marks take its colour
                // over a third of the city's, the rest are dimmed.
                if (have_sprites) {
                    if (city_image_dirty) {
                        render::render_city(state.city, sprites, 0, 0, viewer::kCityW, viewer::kCityH, city_image,
                                            render_phase, &state.objects);
                        city_image_dirty = false;
                    }
                    render_sprite_view(city_image, sprites.palette, cam, frame);
                } else {
                    viewer::render_city_map_layer(state.city, viewer::SaveLayer::Tiles, city_cell_px, cam.x, cam.y,
                                                  cam.zoom, kLogicalW, kLogicalH, frame);
                }
                for (int vy = 0; vy < kLogicalH; ++vy) {
                    const int cy = static_cast<int>(cam.y + vy / cam.zoom) / city_cell_px;
                    for (int vx = 0; vx < kLogicalW; ++vx) {
                        const int cx = static_cast<int>(cam.x + vx / cam.zoom) / city_cell_px;
                        if (cx < 0 || cy < 0 || cx >= viewer::kCityW || cy >= viewer::kCityH) continue;
                        const size_t i = (static_cast<size_t>(vy) * kLogicalW + vx) * 3;
                        formats::RGB c;
                        if (viewer::overlay_color(state.city, static_cast<viewer::Overlay>(overlay), cx, cy, c)) {
                            frame[i] = static_cast<uint8_t>((frame[i] + 2 * c.r) / 3);
                            frame[i + 1] = static_cast<uint8_t>((frame[i + 1] + 2 * c.g) / 3);
                            frame[i + 2] = static_cast<uint8_t>((frame[i + 2] + 2 * c.b) / 3);
                        } else {
                            frame[i] = static_cast<uint8_t>(frame[i] / 3);
                            frame[i + 1] = static_cast<uint8_t>(frame[i + 1] / 3);
                            frame[i + 2] = static_cast<uint8_t>(frame[i + 2] / 3);
                        }
                    }
                }
                const std::string label = std::string("Maps - ") + viewer::kOverlayNames[overlay];
                ui::render_bar(overlay_buttons, overlay, overlay_bar, frame, kLogicalW, kLogicalH, page_metrics, font,
                               label.c_str());
                ui::render_strip(screen_tabs, 2, strip, frame, kLogicalW, kLogicalH, page_metrics, font);
            } else if (save_mode && show_sprites) {
                // The draw loop's animation (renderer findings section 6): the
                // water phase advances on each drawn frame while the 32-step
                // counter DS:0x6D3E is odd, and burning tiles blink on bit 2
                // of the 128-step counter DS:0x6D3A.
                if ((sim.ticks % 32) & 1) {
                    render_phase.water = (render_phase.water + 1) % 3;
                    city_image_dirty = true;
                }
                const int blink = ((sim.ticks % 128) >> 2) & 1;
                if (blink != render_phase.blink) {
                    render_phase.blink = blink;
                    city_image_dirty = true;
                }
                if (render_phase.ticks != sim.ticks || !render_phase.workshop_records) {
                    render_phase.ticks = sim.ticks;
                    render_phase.population_units = model::global_word(state, 0x6C10);
                    render_phase.coverage_base = model::global_word(state, 0x6BF8);
                    render_phase.workshop_records = &state.table_720;
                    render_phase.barracks_records = &state.table_120;
                    city_image_dirty = true;
                }
                if (city_image_dirty) {
                    render::render_city(state.city, sprites, 0, 0, viewer::kCityW, viewer::kCityH, city_image,
                                        render_phase, &state.objects);
                    city_image_dirty = false;
                }
                render_sprite_view(city_image, sprites.palette, cam, frame);
                ui::render(toolbar, tool_index, hovered, viewer::heat_color, frame, kLogicalW, kLogicalH,
                           have_font ? &game_font : nullptr, have_icons ? &toolbar_icons : nullptr,
                           have_sprites ? &sprites.palette : nullptr, tool_text.c_str(), funds_text.c_str());
            } else if (save_mode) {
                viewer::render_city_map_layer(state.city, layer, city_cell_px, cam.x, cam.y, cam.zoom, kLogicalW,
                                               kLogicalH, frame);
                // Toolbar draws over the map, as the original's panel does.
                // The map is still rendered full-frame so the click ->
                // world math stays a single uniform mapping; the panel
                // simply occludes the bottom, and handle_select_logical
                // keeps clicks there from reaching the occluded cells.
                ui::render(toolbar, tool_index, hovered, viewer::heat_color, frame, kLogicalW, kLogicalH,
                           have_font ? &game_font : nullptr, have_icons ? &toolbar_icons : nullptr,
                           have_sprites ? &sprites.palette : nullptr, tool_text.c_str(), funds_text.c_str());
            } else {
                render_empire_frame(map, cam, frame);
            }
            if (save_mode && screen == Screen::City)
                ui::render_strip(screen_tabs, 0, strip, frame, kLogicalW, kLogicalH, page_metrics, font);
            if (save_mode && message_visible()) {
                // 0x279AC: the message's two 28-character lines. The original
                // draws them at (16, 14) and (16, 30); here they sit below the
                // screen strip.
                const std::string& t = sim.messages.current.text;
                const std::string line1 = t.substr(0, std::min<size_t>(28, t.size()));
                const std::string line2 = t.size() > 28 ? t.substr(28) : std::string();
                for (int y = kMessageBox.y; y < kMessageBox.y + kMessageBox.h; ++y) {
                    for (int x = kMessageBox.x; x < kMessageBox.x + kMessageBox.w; ++x) {
                        const size_t i = (static_cast<size_t>(y) * kLogicalW + x) * 3;
                        const bool edge = y == kMessageBox.y || x == kMessageBox.x ||
                                          y == kMessageBox.y + kMessageBox.h - 1 || x == kMessageBox.x + kMessageBox.w - 1;
                        frame[i] = edge ? 140 : 58;
                        frame[i + 1] = edge ? 132 : 55;
                        frame[i + 2] = edge ? 100 : 40;
                    }
                }
                for (const auto& [line, y] : {std::pair<const std::string&, int>{line1, kMessageBox.y + 3},
                                              std::pair<const std::string&, int>{line2, kMessageBox.y + 15}}) {
                    if (font)
                        ui::draw_game_text(frame, kLogicalW, kLogicalH, kMessageBox.x + 4, y, line.c_str(), 1, *font);
                    else
                        ui::draw_text(frame, kLogicalW, kLogicalH, kMessageBox.x + 4, y, line.c_str(), 1,
                                      formats::RGB{232, 226, 200});
                }
            }
            window.present_rgb24(frame);
            // The Settings screen's frame rate: vsync, or a cap (SDL_Delay).
            if (settings.frame_cap > 0 && screenshot_path.empty()) {
                const Uint32 target = 1000 / static_cast<Uint32>(settings.frame_cap);
                const Uint32 spent = SDL_GetTicks() - last_frame_ms;
                if (spent < target) SDL_Delay(target - spent);
            }
            last_frame_ms = SDL_GetTicks();
            if (quit_requested) running = false;
            ++frame_count;

            if (screenshot_frames > 0 && frame_count >= screenshot_frames) {
                if (!screenshot_path.empty()) {
                    stbi_write_png(screenshot_path.c_str(), kLogicalW, kLogicalH, 3, frame.data(), kLogicalW * 3);
                    std::printf("wrote screenshot to %s after %d frame(s)\n", screenshot_path.c_str(), frame_count);
                }
                running = false;
            }
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "fatal: %s\n", e.what());
        SDL_Quit();
        return 4;
    }

    SDL_Quit();
    return 0;
}
