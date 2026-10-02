// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius — platform/console.hpp
//
// On Windows the viewer is a GUI program: double-clicked, it opens no console
// window behind the game. Started from a terminal, it borrows that terminal so
// its log lines (the month, the toolbar, "wrote screenshot ...") still show;
// when its output is redirected to a file or a pipe it leaves that alone.
// Everywhere else this does nothing.

#pragma once

namespace gaius::platform {

void attach_parent_console();

}  // namespace gaius::platform
