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

const char* const kEssentialGameFiles[] = {"EMPIRE2.001", "HOUSES.PL8", "HOUSES2.PL8", "FIXTS.PL8",
                                           "MOREMEN.PL8",  "SHADE.256",  "FONT1.PL8"};
const int kEssentialGameFileCount = static_cast<int>(sizeof(kEssentialGameFiles) / sizeof(kEssentialGameFiles[0]));

GameFolderReport inspect_game_folder(const std::string& dir) {
    GameFolderReport report;
    if (dir.empty()) return report;
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) return report;
    std::vector<std::string> names;  // upper case
    for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end;
         it.increment(ec)) {
        std::string name = it->path().filename().string();
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        names.push_back(std::move(name));
    }
    const auto has = [&](const std::string& name) { return std::find(names.begin(), names.end(), name) != names.end(); };
    const auto has_extension = [&](const std::string& ext) {
        return std::any_of(names.begin(), names.end(), [&](const std::string& n) {
            return n.size() > ext.size() && n.compare(n.size() - ext.size(), ext.size(), ext) == 0;
        });
    };
    for (int i = 0; i < kEssentialGameFileCount; ++i)
        if (!has(kEssentialGameFiles[i])) report.missing.push_back(kEssentialGameFiles[i]);
    const bool caesar = has("EMPIRE2.001") || has("HOUSES.PL8") || has("CSR.EXE");
    if (!caesar) {
        report.missing.clear();
        report.status = GameFolderStatus::NotCaesar;
    } else if (report.missing.empty()) {
        report.status = GameFolderStatus::Usable;
    } else if (has_extension(".MDI") || has("MUSIC.MOD")) {
        report.status = GameFolderStatus::International;  // its music is .MDI files, not the US release's .XMI
    } else {
        report.status = GameFolderStatus::Incomplete;
    }
    return report;
}

bool looks_like_game_folder(const std::string& dir) {
    return inspect_game_folder(dir).status == GameFolderStatus::Usable;
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
