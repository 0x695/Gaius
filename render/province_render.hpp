// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius — render/province_render.hpp
//
// The province view: the 40 x 40 province map and its armies and Cohorts,
// drawn from the game's own sheets into an indexed image (no SDL).
//
// With DS:0x6CAE set (the province view) the engine loads FIXT3.PL8 and
// SPRITE2.PL8 where the city view keeps FIXTS.PL8 and MOREMEN.PL8 (renderer
// findings section 1), scrolls by whole 16 px cells (DS:0x6CB2, DS:0x6CB0,
// 0x065D9, clamped to 0-20 and 0-29 by 0x066FE) and draws the actors of types
// 11 and up through the same routine as the city's walkers (0x6834 ->
// 0x6931 -> 0x6946): the frame +0x00 with its bottom edge at y + 8.
//
// The tile -> frame rule is STRONG INFERENCE: FIXT3.PL8 has exactly 125
// frames, one for each province tile 0x00-0x7C, and its frames run water,
// shores, grass, roads, walls, the city and towns in the order the province
// tiles do (systems/province.hpp). Bit 0x80 (a province actor stands there)
// is masked off. No capture of the province view exists to check it against
// pixel by pixel; the palette is the city's SHADE.256 on the same inference.

#pragma once

#include <array>
#include <string>

#include "formats/common/types.hpp"
#include "formats/empire2/empire2.hpp"
#include "model/city_state.hpp"

namespace gaius::render {

struct ProvinceSprites {
    formats::PL8Sheet terrain;  // FIXT3.PL8
    formats::PL8Sheet units;    // SPRITE2.PL8
    formats::Palette palette;   // SHADE.256
};

ProvinceSprites load_province_sprites(const std::string& asset_dir);

inline constexpr int kProvincePx = 16;

// The FIXT3.PL8 frame of a province map byte.
inline int province_frame(uint8_t cell) { return cell & 0x7F; }

// Draws the whole map (640 x 640) with the province actors over it, in order
// of y as the draw list sorts them.
void render_province(const formats::empire2::EmpireMap& map, const ProvinceSprites& sprites,
                     const std::array<model::Actor, model::kActorCount>& actors, formats::IndexedImage& out);

}  // namespace gaius::render
