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

// 0x0A7C3 + 0x0E6E3, the Treasurer: last year's accounts, the funds history
// and the two tax rates with their arrows (DS:0x0404).
formats::IndexedImage compose_treasurer_screen(const model::CityState& state, const InterfaceArt& art);

// 0x0B151 + 0x0E167, the Military Advisor: the Legion's Centuries, the
// Cohort on display (DS:0x6C0A) with its standard from SPRITE2.PL8
// (`cohort_sprites`, loaded at 630D:0000), the wages and conscription, and
// the buttons (DS:0x0094). `frame_counter` is DS:0x6D3A, which animates the
// standard.
formats::IndexedImage compose_legion_screen(const model::CityState& state, const InterfaceArt& art,
                                            const formats::PL8Sheet& cohort_sprites, int frame_counter);

// 0x09D22, the man in green: the industry report.
formats::IndexedImage compose_industry_screen(const model::CityState& state, const InterfaceArt& art);

}  // namespace gaius::ui
