// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius — platform/game_detect.cpp
//
// Finding the player's Caesar files without being told: the folders an
// installer or a launcher puts the game in (GOG, Steam, the usual game
// folders), and a folder picker for when it isn't any of them.
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "platform/game_import.hpp"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <shobjidl.h>
#endif

namespace gaius::platform {

namespace fs = std::filesystem;

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool mentions_caesar(const std::string& name) { return lower(name).find("caesar") != std::string::npos; }

void add_unique(std::vector<std::string>& out, const std::string& dir) {
    for (const std::string& have : out)
        if (have == dir) return;
    out.push_back(dir);
}

// The sub-folders of `dir` whose name mentions Caesar.
std::vector<std::string> caesar_children(const std::string& dir) {
    std::vector<std::string> out;
    std::error_code ec;
    for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end;
         it.increment(ec)) {
        std::error_code is_ec;
        if (it->is_directory(is_ec) && mentions_caesar(it->path().filename().string())) out.push_back(it->path().string());
    }
    return out;
}

// Where a launcher or an installer might have put the game. `direct` are folders the game may be in or just below (a
// registry entry names the game's own folder); `libraries` are folders of games, searched for a Caesar folder in them.
void add_roots(std::vector<std::string>& direct, std::vector<std::string>& libraries);

}  // namespace

std::vector<std::string> detect_game_folders() {
    std::vector<std::string> found;
#if !defined(__ANDROID__)
    // A folder named outright, and the one the tests and tools use.
    for (const char* name : {"GAIUS_GAME_DIR", "GAIUS_TEST_ASSETS"}) {
        if (const char* v = std::getenv(name))
            for (const std::string& d : game_folders_under(v, 2)) add_unique(found, d);
    }
    std::vector<std::string> direct, libraries;
    add_roots(direct, libraries);
    for (const std::string& root : direct)
        for (const std::string& d : game_folders_under(root, 3)) add_unique(found, d);
    for (const std::string& root : libraries) {
        std::error_code ec;
        if (!fs::is_directory(root, ec)) continue;
        for (const std::string& child : caesar_children(root))
            for (const std::string& d : game_folders_under(child, 3)) add_unique(found, d);
        // A games folder that is itself the game's, files and all.
        if (looks_like_game_folder(root)) add_unique(found, root);
    }
#endif
    return found;
}

#if defined(_WIN32)

namespace {

std::string narrow(const std::wstring& w) { return fs::path(w).string(); }

bool read_registry_string(HKEY key, const wchar_t* name, std::wstring& out) {
    DWORD size = 0;
    if (RegGetValueW(key, nullptr, name, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, nullptr, nullptr, &size) != ERROR_SUCCESS ||
        size < sizeof(wchar_t))
        return false;
    std::wstring value(size / sizeof(wchar_t), L'\0');
    if (RegGetValueW(key, nullptr, name, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, nullptr, value.data(), &size) != ERROR_SUCCESS)
        return false;
    while (!value.empty() && value.back() == L'\0') value.pop_back();
    out = value;
    return true;
}

// GOG's installer records each game under HKLM\SOFTWARE\WOW6432Node\GOG.com\Games\<id>: gameName and path.
void add_gog_registry_roots(std::vector<std::string>& direct) {
    HKEY games = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\GOG.com\\Games", 0, KEY_READ | KEY_WOW64_64KEY, &games) !=
        ERROR_SUCCESS)
        return;
    wchar_t sub[256];
    for (DWORD i = 0;; ++i) {
        DWORD len = 256;
        if (RegEnumKeyExW(games, i, sub, &len, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
        HKEY game = nullptr;
        if (RegOpenKeyExW(games, sub, 0, KEY_READ | KEY_WOW64_64KEY, &game) != ERROR_SUCCESS) continue;
        std::wstring name, path;
        if (read_registry_string(game, L"gameName", name) && read_registry_string(game, L"path", path) &&
            mentions_caesar(narrow(name)))
            add_unique(direct, narrow(path));
        RegCloseKey(game);
    }
    RegCloseKey(games);
}

// Steam's libraries: its own folder and the "path" entries of steamapps/libraryfolders.vdf.
void add_steam_roots(std::vector<std::string>& libraries) {
    HKEY steam = nullptr;
    std::wstring steam_path;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", 0, KEY_READ, &steam) == ERROR_SUCCESS) {
        read_registry_string(steam, L"SteamPath", steam_path);
        RegCloseKey(steam);
    }
    if (steam_path.empty()) return;
    std::vector<std::string> steam_libraries{narrow(steam_path)};
    std::ifstream vdf(fs::path(steam_path) / "steamapps" / "libraryfolders.vdf");
    std::string line;
    while (std::getline(vdf, line)) {
        const size_t key = line.find("\"path\"");
        if (key == std::string::npos) continue;
        const size_t open = line.find('"', key + 6);
        const size_t close = line.find_last_of('"');
        if (open == std::string::npos || close == std::string::npos || close <= open) continue;
        std::string value;
        for (size_t i = open + 1; i < close; ++i) {
            if (line[i] == '\\' && i + 1 < close && line[i + 1] == '\\') ++i;  // the file doubles its backslashes
            value += line[i];
        }
        steam_libraries.push_back(value);
    }
    for (const std::string& lib : steam_libraries) add_unique(libraries, (fs::path(lib) / "steamapps" / "common").string());
}

void add_roots(std::vector<std::string>& direct, std::vector<std::string>& libraries) {
    add_gog_registry_roots(direct);
    add_steam_roots(libraries);
    // The usual places, on every fixed drive.
    const DWORD drives = GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if (!(drives & (1u << i))) continue;
        const std::string drive = std::string(1, static_cast<char>('A' + i)) + ":\\";
        if (GetDriveTypeA(drive.c_str()) != DRIVE_FIXED) continue;
        for (const char* sub : {"GOG Games", "GOG Galaxy\\Games", "Program Files (x86)\\GOG Galaxy\\Games",
                                "Program Files\\GOG Galaxy\\Games", "Program Files (x86)\\Steam\\steamapps\\common",
                                "SteamLibrary\\steamapps\\common", "Steam\\steamapps\\common", "Games", "Program Files (x86)",
                                "Program Files", "DOS", "Abandonware", ""})
            add_unique(libraries, drive + sub);
    }
}

}  // namespace

bool can_pick_folder() { return true; }

std::string pick_folder(const std::string& title) {
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    std::string chosen;
    IFileOpenDialog* dialog = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
        DWORD options = 0;
        dialog->GetOptions(&options);
        dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        const int wide = MultiByteToWideChar(CP_UTF8, 0, title.c_str(), -1, nullptr, 0);
        if (wide > 1) {
            std::wstring w(static_cast<size_t>(wide), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, title.c_str(), -1, w.data(), wide);
            dialog->SetTitle(w.c_str());
        }
        if (SUCCEEDED(dialog->Show(nullptr))) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dialog->GetResult(&item))) {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                    chosen = narrow(path);
                    CoTaskMemFree(path);
                }
                item->Release();
            }
        }
        dialog->Release();
    }
    if (SUCCEEDED(init)) CoUninitialize();
    return chosen;
}

#else  // not Windows

namespace {

void add_roots(std::vector<std::string>& direct, std::vector<std::string>& libraries) {
    (void)direct;
#if !defined(__ANDROID__)
    const char* home = std::getenv("HOME");
    if (!home) return;
    const fs::path h(home);
    for (const char* sub : {"GOG Games", "Games", ".steam/steam/steamapps/common", ".local/share/Steam/steamapps/common",
                            ".var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/common", ".wine/drive_c/GOG Games",
                            ".wine/drive_c/Games", "dosbox", "DOS"})
        add_unique(libraries, (h / sub).string());
    add_unique(libraries, "/opt");
#else
    (void)libraries;
#endif
}

}  // namespace

bool can_pick_folder() { return false; }
std::string pick_folder(const std::string&) { return std::string(); }

#endif

}  // namespace gaius::platform
