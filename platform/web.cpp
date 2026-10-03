// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius -- platform/web.cpp: see web.hpp.
#include "platform/web.hpp"

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#include <emscripten/html5.h>

#include <cmath>
#endif

namespace gaius::platform::web {

#if defined(__EMSCRIPTEN__)

// web/gaius-web.js defines Module.gaiusFlush: it debounces and runs FS.syncfs(false).
EM_JS(void, gaius_flush_storage_js, (), {
    if (Module.gaiusFlush) Module.gaiusFlush();
});

bool page_canvas_size(int* w, int* h) {
    double css_w = 0, css_h = 0;
    if (emscripten_get_element_css_size("#canvas", &css_w, &css_h) != EMSCRIPTEN_RESULT_SUCCESS) return false;
    const double ratio = emscripten_get_device_pixel_ratio();
    const int pw = static_cast<int>(std::lround(css_w * ratio));
    const int ph = static_cast<int>(std::lround(css_h * ratio));
    if (pw < 16 || ph < 16) return false;  // the page has not laid the canvas out (or it is hidden)
    *w = pw;
    *h = ph;
    return true;
}

void tick() {
    static double last_ms = emscripten_get_now();
    const double now = emscripten_get_now();
    if (now - last_ms < 4000.0) return;
    last_ms = now;
    gaius_flush_storage_js();
}

void flush_storage() { gaius_flush_storage_js(); }

#else

bool page_canvas_size(int*, int*) { return false; }
void tick() {}
void flush_storage() {}

#endif

}  // namespace gaius::platform::web
