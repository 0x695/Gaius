// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius -- platform/path_rules.cpp: platform::resolve_paths, kept free of SDL
// so the tests can link it (platform/paths.hpp).
#include <filesystem>

#include "platform/paths.hpp"

namespace gaius::platform {

namespace fs = std::filesystem;

namespace {

std::string join(const std::string& a, const std::string& b) {
    if (a.empty()) return std::string();
    return (fs::path(a) / b).string();
}

std::string trim_separator(std::string s) {
    while (s.size() > 1 && (s.back() == '/' || s.back() == '\\')) s.pop_back();
    return s;
}

}  // namespace

GaiusPaths resolve_paths(const PathEnvironment& e) {
    GaiusPaths p;
    switch (e.os) {
        case Os::Windows: {
            const std::string base = join(trim_separator(e.appdata), "Gaius");
            p.settings = join(base, "settings");
            p.saves = join(base, "saves");
            p.game = join(base, "game");
            break;
        }
        case Os::Linux: {
            const std::string home = trim_separator(e.home);
            const std::string config =
                !e.xdg_config_home.empty() ? trim_separator(e.xdg_config_home) : join(home, ".config");
            const std::string data =
                !e.xdg_data_home.empty() ? trim_separator(e.xdg_data_home) : join(join(home, ".local"), "share");
            p.settings = join(config, "gaius");
            p.saves = join(join(data, "gaius"), "saves");
            p.game = join(join(data, "gaius"), "game");
            break;
        }
        case Os::Android: {
            p.settings = join(trim_separator(e.android_internal), "settings");
            p.saves = join(trim_separator(e.android_internal), "saves");
            p.game = join(trim_separator(e.android_external), "game");
            break;
        }
        case Os::Other: {
            const std::string base = trim_separator(e.sdl_pref);
            p.settings = join(base, "settings");
            p.saves = join(base, "saves");
            p.game = join(base, "game");
            break;
        }
    }
    return p;
}

}  // namespace gaius::platform
