// SPDX-License-Identifier: GPL-3.0-or-later
#include "formats/common/game_files.hpp"

#include <cctype>
#include <filesystem>
#include <system_error>

namespace gaius::formats {

namespace {

bool same_ignoring_case(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    return true;
}

}  // namespace

std::string find_name_ignoring_case(const std::string& dir, const std::string& name) {
    namespace fs = std::filesystem;
    std::error_code ec;
    for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end;
         it.increment(ec)) {
        const std::string entry = it->path().filename().string();
        if (same_ignoring_case(entry, name)) return entry;
    }
    return std::string();
}

std::string game_file_path(const std::string& dir, const std::string& name) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path exact = fs::path(dir) / name;
    if (fs::exists(exact, ec)) return exact.string();
    const std::string found = find_name_ignoring_case(dir, name);
    return found.empty() ? exact.string() : (fs::path(dir) / found).string();
}

bool game_file_exists(const std::string& dir, const std::string& name) {
    std::error_code ec;
    return std::filesystem::exists(game_file_path(dir, name), ec);
}

}  // namespace gaius::formats
