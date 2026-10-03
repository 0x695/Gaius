// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius -- platform/path_rules.cpp: platform::resolve_paths, kept free of SDL
// so the tests can link it (platform/paths.hpp).
#include <algorithm>
#include <cctype>
#include <filesystem>

#include "platform/game_import.hpp"
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

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
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
        case Os::Web: {
            const std::string base = trim_separator(e.web_root);
            p.settings = join(base, "settings");
            p.saves = join(base, "saves");
            p.game = join(base, "game");
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

bool looks_like_game_folder(const std::string& dir) {
    if (dir.empty()) return false;
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) return false;
    bool scenario = false, sprites = false;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        std::string name = entry.path().filename().string();
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        if (name == "EMPIRE2.001") scenario = true;
        if (name == "HOUSES.PL8") sprites = true;
    }
    return scenario && sprites;
}

std::vector<std::string> game_folders_under(const std::string& root, int depth) {
    std::vector<std::string> found;
    std::error_code ec;
    if (root.empty() || !fs::is_directory(root, ec)) return found;
    // Breadth first, so a folder near the root comes before one deep below it.
    std::vector<std::string> level{root};
    for (int d = 0; d <= depth && !level.empty(); ++d) {
        std::vector<std::string> next;
        for (const std::string& dir : level) {
            if (looks_like_game_folder(dir)) found.push_back(dir);
            if (d == depth) continue;
            for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end;
                 it.increment(ec)) {
                std::error_code is_ec;
                if (it->is_directory(is_ec)) next.push_back(it->path().string());
            }
        }
        level = std::move(next);
    }
    // The US build is the one Gaius's findings were made against: it goes first when a folder holds both.
    std::stable_sort(found.begin(), found.end(), [](const std::string& a, const std::string& b) {
        const auto is_us = [](const std::string& p) { return lower(fs::path(p).filename().string()) == "us"; };
        return is_us(a) && !is_us(b);
    });
    return found;
}

}  // namespace gaius::platform
