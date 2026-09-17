// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/strings.hpp"

#include <sstream>
#include <utility>

namespace gaius::ui {

namespace {

Catalog& catalog() {
    static Catalog c{"en", "English", {}};
    return c;
}

std::string trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return std::string();
    return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}

}  // namespace

const char* tr(const char* english) {
    const Catalog& c = catalog();
    if (c.text.empty() || !english) return english;
    const auto it = c.text.find(english);
    return it == c.text.end() ? english : it->second.c_str();
}

std::string tr(const std::string& english) { return tr(english.c_str()); }

Catalog parse_catalog(const std::string& code, const std::string& file_text) {
    Catalog c;
    c.code = code;
    c.name = code;
    std::istringstream in(file_text);
    std::string line;
    bool first = true;
    while (std::getline(in, line)) {
        if (first && line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF &&
            static_cast<unsigned char>(line[1]) == 0xBB && static_cast<unsigned char>(line[2]) == 0xBF)
            line = line.substr(3);  // a UTF-8 byte-order mark
        first = false;
        const std::string t = trim(line);
        if (t.empty()) continue;
        if (t[0] == '#') {
            const std::string body = trim(t.substr(1));
            if (body.rfind("language", 0) == 0) {
                const size_t eq = body.find('=');
                if (eq != std::string::npos && !trim(body.substr(eq + 1)).empty()) c.name = trim(body.substr(eq + 1));
            }
            continue;
        }
        const size_t sep = t.find(" = ");
        if (sep == std::string::npos) continue;
        const std::string english = trim(t.substr(0, sep));
        const std::string translation = trim(t.substr(sep + 3));
        if (!english.empty() && !translation.empty()) c.text[english] = translation;
    }
    return c;
}

void set_catalog(Catalog c) { catalog() = std::move(c); }

const Catalog& current_catalog() { return catalog(); }

}  // namespace gaius::ui
