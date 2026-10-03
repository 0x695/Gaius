// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius -- platform/web.hpp
//
// The few things the browser build needs from its page (web/): how big the canvas is, and persisting the player's
// files. Everywhere else these do nothing, so callers need no #if.
//
// The page mounts the browser's IndexedDB at /persist (Emscripten's IDBFS): the game files the player imported, the
// settings and the saves live there. IDBFS keeps them in memory and writes them back only when asked, so the game asks
// every few seconds (tick) and the page also asks when it is hidden or closed.

#pragma once

namespace gaius::platform::web {

// True in the browser build.
constexpr bool enabled() {
#if defined(__EMSCRIPTEN__)
    return true;
#else
    return false;
#endif
}

// The size the page gives the canvas, in device pixels (CSS size times devicePixelRatio). False when there is no page.
bool page_canvas_size(int* w, int* h);

// Called once a frame: every few seconds asks the page to write /persist back to IndexedDB.
void tick();

// Asks for that write now (after a save, before leaving).
void flush_storage();

}  // namespace gaius::platform::web
