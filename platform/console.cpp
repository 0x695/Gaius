// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/console.hpp"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdio>
#endif

namespace gaius::platform {

#if defined(_WIN32)

namespace {

bool usable(HANDLE h) { return h != nullptr && h != INVALID_HANDLE_VALUE; }

}  // namespace

void attach_parent_console() {
    // A GUI-subsystem process starts with no standard handles unless its parent
    // redirected them; those redirections are kept as they are.
    const bool out_ok = usable(GetStdHandle(STD_OUTPUT_HANDLE));
    const bool err_ok = usable(GetStdHandle(STD_ERROR_HANDLE));
    if (out_ok && err_ok) return;
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) return;  // no parent console: started from the desktop
    if (!out_ok) {
        FILE* f = nullptr;
        freopen_s(&f, "CONOUT$", "w", stdout);
    }
    if (!err_ok) {
        FILE* f = nullptr;
        freopen_s(&f, "CONOUT$", "w", stderr);
    }
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);
}

#else

void attach_parent_console() {}

#endif

}  // namespace gaius::platform
