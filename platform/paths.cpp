// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/paths.hpp"

#include <SDL.h>

#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <utility>

namespace gaius::platform {

namespace fs = std::filesystem;

namespace {

[[maybe_unused]] std::string env(const char* name) {
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string();
}

}  // namespace

PathEnvironment current_environment() {
    PathEnvironment e;
#if defined(__ANDROID__)
    e.os = Os::Android;
    if (const char* p = SDL_AndroidGetInternalStoragePath()) e.android_internal = p;
    if (const char* p = SDL_AndroidGetExternalStoragePath()) e.android_external = p;
#elif defined(_WIN32)
    e.os = Os::Windows;
    e.appdata = env("APPDATA");
#elif defined(__linux__)
    e.os = Os::Linux;
    e.home = env("HOME");
    e.xdg_config_home = env("XDG_CONFIG_HOME");
    e.xdg_data_home = env("XDG_DATA_HOME");
#else
    e.os = Os::Other;
#endif
    if (e.os == Os::Other || (e.os == Os::Windows && e.appdata.empty()) || (e.os == Os::Linux && e.home.empty())) {
        e.os = Os::Other;
        if (char* p = SDL_GetPrefPath("Gaius", "Gaius")) {
            e.sdl_pref = p;
            SDL_free(p);
        }
    }
    return e;
}

std::string pref_path() {
    char* p = SDL_GetPrefPath("Gaius", "Gaius");
    if (!p) throw std::runtime_error(std::string("platform::pref_path: SDL_GetPrefPath failed: ") + SDL_GetError());
    std::string result(p);
    SDL_free(p);
    return result;
}

std::string data_path(const std::string& subdir) {
    fs::path full = fs::path(pref_path()) / subdir;
    fs::create_directories(full);
    std::string s = full.string();
    if (s.empty() || (s.back() != '/' && s.back() != '\\')) s += '/';
    return s;
}

const GaiusPaths& paths() {
    static const GaiusPaths resolved = [] {
        GaiusPaths p = resolve_paths(current_environment());
        for (const std::string* dir : {&p.settings, &p.saves, &p.game}) {
            if (dir->empty()) throw std::runtime_error("platform::paths: no folder for Gaius's files on this system");
            std::error_code ec;
            fs::create_directories(*dir, ec);
            if (ec) throw std::runtime_error("platform::paths: can't create " + *dir + ": " + ec.message());
        }
        // The earlier layout, SDL_GetPrefPath("Gaius", "Gaius")/{settings,saves}:
        // move its files over the first time, never overwriting.
        const PathEnvironment e = current_environment();
        std::string old_base;
        if (e.os == Os::Windows) old_base = (fs::path(e.appdata) / "Gaius" / "Gaius").string();
        if (e.os == Os::Linux)
            old_base = (e.xdg_data_home.empty() ? fs::path(e.home) / ".local" / "share" : fs::path(e.xdg_data_home))
                           .append("Gaius")
                           .append("Gaius")
                           .string();
        if (!old_base.empty()) {
            for (const auto& [sub, target] : {std::pair<const char*, std::string>{"settings", p.settings},
                                              std::pair<const char*, std::string>{"saves", p.saves}}) {
                std::error_code ec;
                const fs::path from = fs::path(old_base) / sub;
                if (!fs::is_directory(from, ec)) continue;
                for (const auto& entry : fs::directory_iterator(from, ec)) {
                    const fs::path to = fs::path(target) / entry.path().filename();
                    if (!fs::exists(to, ec)) fs::rename(entry.path(), to, ec);
                }
            }
        }
        return p;
    }();
    return resolved;
}

}  // namespace gaius::platform
