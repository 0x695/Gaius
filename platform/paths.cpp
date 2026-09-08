#include "platform/paths.hpp"

#include <SDL.h>

#include <filesystem>
#include <stdexcept>

namespace gaius::platform {

std::string pref_path() {
    // "Gaius" org, "Gaius" app -- matches SDL_GetPrefPath(org, app). SDL
    // resolves this to the correct convention per OS (see header comment).
    char* p = SDL_GetPrefPath("Gaius", "Gaius");
    if (!p) throw std::runtime_error(std::string("platform::pref_path: SDL_GetPrefPath failed: ") + SDL_GetError());
    std::string result(p);
    SDL_free(p);
    return result;
}

std::string data_path(const std::string& subdir) {
    std::filesystem::path base = pref_path();
    std::filesystem::path full = base / subdir;
    std::filesystem::create_directories(full);
    std::string s = full.string();
    if (s.empty() || (s.back() != '/' && s.back() != '\\')) s += '/';
    return s;
}

}  // namespace gaius::platform
