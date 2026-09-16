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

// 0x0A57A + 0x0E822, the Tribune of the Plebs: the plebs, welfare and each
// duty's plebs and need, with the arrows (DS:0x0494).
formats::IndexedImage compose_tribune_screen(const model::CityState& state, const InterfaceArt& art);

// The ratings screen's own art: TEMPLE.VPX, TEMPLE.256 and the column pieces
// TEMPLBIT.PL8 (loaded at 43A5:5694).
struct RatingsArt {
    formats::IndexedImage picture;
    formats::Palette palette;
    formats::PL8Sheet columns;
};
RatingsArt load_ratings_art(const std::string& asset_dir);

// 0x096ED + 0x0CC21, the ratings: a column for each of Peace, Culture,
// Prosperity and Empire, a piece per 10 points (0x0D069), the percentages,
// and along the bottom the average or the advice `hint`
// (systems::forum::kRatingHints; 0 for the average). The screen keeps
// TEMPLE.256 as its palette.
formats::IndexedImage compose_ratings_screen(const model::CityState& state, const InterfaceArt& art,
                                             const RatingsArt& ratings, int hint);

// 0x098AB + 0x0BDD3, the governor's own affairs: C_VITAE.VPX (`picture`,
// load_governor_picture) and a panel with the governor's name, rank,
// province, savings, Imperial favour and salary, and the five buttons
// (DS:0x0444, cells (18, 1), (18, 2), (18, 4), (18, 8), (18, 10)).
formats::IndexedImage load_governor_picture(const std::string& asset_dir);
formats::IndexedImage compose_governor_screen(const model::CityState& state, const InterfaceArt& art,
                                              const formats::IndexedImage& picture);

// The governor screen's three dialogs, each drawn over it: the promotion
// requirements (0x0BE7C, button (18, 2)), the salary (0x0C06A, (18, 8)) and
// the donation (0x0C17D, (18, 10)). A right click (DS:0x6D4C) ends each; a
// donation is paid when its dialog ends (0x0C26E, economy::donate_savings).
enum class GovernorDialog { None, Requirements, Salary, Donation };
formats::IndexedImage compose_governor_dialog(const model::CityState& state, const InterfaceArt& art,
                                              const formats::IndexedImage& picture, GovernorDialog dialog);
// The salary and donation dialogs' two buttons (DS:0x0144, DS:0x0124): the up
// arrow at cell (12, 6) gives +1, the down arrow at (13, 6) -1, elsewhere 0.
int governor_dialog_arrow(GovernorDialog dialog, int x, int y);

// 0x084B1: the funds warning -- a 20 x 12 panel and the six lines at
// DS:0x085F in FONT1 from x 0.
formats::IndexedImage compose_funds_warning_screen(const InterfaceArt& art);

// 0x09D22, the man in green: the industry report.
formats::IndexedImage compose_industry_screen(const model::CityState& state, const InterfaceArt& art);

}  // namespace gaius::ui
