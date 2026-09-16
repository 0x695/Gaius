// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius — ui/forum_screens.hpp
//
// The Forum's advisor screens in the original's art, drawn on the interface
// canvas (ui/interface.hpp). Transcribed from the US-build CSR.EXE
// (2026-09-16); findings sections 36 and 41. What the screens show comes from
// systems::forum; this is only where and how.

#pragma once

#include "formats/common/types.hpp"
#include "model/city_state.hpp"
#include "ui/interface.hpp"

namespace gaius::ui {

// 0x0ACA7, the man in the blue robe: the four history graphs.
formats::IndexedImage compose_history_screen(const model::CityState& state, const InterfaceArt& art);

// 0x09D22, the man in green: the industry report.
formats::IndexedImage compose_industry_screen(const model::CityState& state, const InterfaceArt& art);

}  // namespace gaius::ui
