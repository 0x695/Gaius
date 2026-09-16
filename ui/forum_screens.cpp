// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/forum_screens.hpp"

#include <array>
#include <cstdlib>

#include "systems/forum.hpp"

namespace gaius::ui {

namespace {

int g(const model::CityState& s, uint16_t ds) { return model::global_word(s, ds); }

}  // namespace

formats::IndexedImage compose_history_screen(const model::CityState& state, const InterfaceArt& art) {
    namespace forum = systems::forum;
    formats::IndexedImage img = blank_canvas();
    draw_panel(img, art, 0, 0, 20, 12);
    draw_inset(img, art, 0x10, 0x20, 8, 3);
    draw_inset(img, art, 0x10, 0x60, 8, 5);
    draw_inset(img, art, 0xB0, 0x60, 8, 5);
    draw_inset(img, art, 0xB0, 0x20, 8, 3);

    const std::array<const std::vector<uint8_t>*, 4> tables = {&state.table_60_a, &state.table_60_b,
                                                               &state.table_60_c, &state.table_60_d};
    for (size_t i = 0; i < forum::kHistoryGraphs.size(); ++i) {
        const forum::HistoryGraph& graph = forum::kHistoryGraphs[i];
        const forum::HistoryBars bars =
            forum::history_bars(*tables[i], g(state, graph.index_word), graph.start_scale, graph.max_height);
        // 1F6F:236C: newest on the right, 8 px apart; frame 0x37 where x is a
        // multiple of 16, 0x36 elsewhere.
        int x = graph.right_x;
        for (int h : bars.height) {
            if (h > 0) draw_sprite_rows(img, art.pointers, (x & 0xF) ? 0x36 : 0x37, x, graph.base_y, h);
            x -= 8;
        }
        if (bars.doublings < 3) {
            const int cx = graph.right_x == 0x80 ? 0x14 : 0xB4;
            const int cy = graph.base_y == 0x4C ? 0x24 : 0x64;
            draw_text(img, art, Font::Mini, cx, cy, graph.ranges[static_cast<size_t>(bars.doublings)]);
        }
    }
    // The labels (DS:0x702A-0x7030), padded as stored.
    draw_text(img, art, Font::Font1, 0x20, 0xB2, " city funds  ");
    draw_text(img, art, Font::Font1, 0x18, 0x54, "population tax");
    draw_text(img, art, Font::Font1, 0x20, 0x54, "                    industry tax");
    draw_text(img, art, Font::Font1, 0x20, 0xB2, "                    population ");
    // 0x0B04C: the years.
    const int year = g(state, 0x6C32);
    draw_text(img, art, Font::Font1, 0x60, 0x0C, year - 15 >= 0 ? "A.D.     -" : "B.C.     -");
    draw_number(img, art, Font::Font1, 0x8A, 0x0C, std::labs(year - 15), 2, 1);
    draw_number(img, art, Font::Font1, 0xB8, 0x0C, std::labs(year - 1), 2, 1);
    return img;
}

formats::IndexedImage compose_industry_screen(const model::CityState& state, const InterfaceArt& art) {
    namespace forum = systems::forum;
    formats::IndexedImage img = blank_canvas();
    draw_panel(img, art, 0, 0, 20, 11);
    draw_inset(img, art, 0x10, 0x10, 18, 8);
    const forum::IndustryReport r = forum::industry_report(state);
    // DS:0x3050 onwards, and the province's 16-character name (DS:0x2CAE).
    draw_text(img, art, Font::Font1, 0x18, 0x06, "Industry Report on");
    static constexpr const char* kProvinceFields[] = {
        "    Sicilia     ", "    Campania    ", "    Latium      ", " Cisalpine Gaul ",
        "    Corsica     ", "    Sardinia    ", " Alpes Maritimae", "   Narbonensis  ",
        "  Hispania Inf. ", "     Baetica    ", " Lusitania Inf. ", " Lusitania Sup. ",
        " Tarraconensis  ", " Aquitania Inf. ", "  Hispania Sup. ", " Aquitania Sup. ",
        "  Lugdunensis   ", "    Belgica     ", "  Gallia  Sup.  ", "  Gallia  Inf.  ",
        "  W. Britannia  ", "  E. Britannia  ", " Britannia Sup. ", "   Caledonia    ",
        "  Germania Inf. ", "  Germania Sup. ", "    Pannonia    ", "     Dacia      ",
        "   Illyricum    ", "    Dalmatia    ", "   Macedonia    ", "     Achaea     ",
        "     Creta      ", "    Thracia     ", "      Asia      ", "   Pamphylia    ",
        "   Cappadocia   ", "    Assyria     ", "     Syria      ", "  Mesopotamia   ",
        "     Judea      ", "     Arabia     ", "    Aegyptus    ", "   Cyrenaica    ",
        "     Africa     ", "    Numidia     ", "   Mauretania   ", "  Caeariensis   ",
        "   Tingitania   ", "     Moesia     "};
    if (r.province >= 0 && r.province < 50) draw_text(img, art, Font::Font1, 0xB0, 0x06, kProvinceFields[r.province]);
    draw_text(img, art, Font::Mini, 0x2C, 0x18, "Industry type      suitability  factories");
    static constexpr const char* kGrades[] = {"Terrible", "  Poor", " Average ", "  Good", "Excellent"};
    draw_text(img, art, Font::Font1, 0x18, 0x92, "Overall Industry Rating - ");
    draw_text(img, art, Font::Font1, 0xE8, 0x92, kGrades[r.overall]);
    draw_text(img, art, Font::Font1, 0x18, 0xA0, "Prospects for Expansion -");
    draw_text(img, art, Font::Font1, 0xE8, 0xA0, kGrades[r.prospects]);
    static constexpr const char* kGoods[] = {"     Glass      ", "      Tin       ", "    Pottery     ",
                                             "    Copper      ", "      Wine      ", "     Ivory      ",
                                             "     Wheat      ", "     Spices     "};
    for (int i = 0; i < 8; ++i) {
        const forum::IndustryRow& row = r.rows[static_cast<size_t>(i)];
        draw_text(img, art, Font::Mini, 0x18, 12 * i + 0x28, kGoods[i]);
        draw_sprite(img, art.pointers, 0x38 + i, 0x78, 12 * i + 0x26);
        if (row.grade >= 0) draw_text(img, art, Font::Mini, 0xA0, 12 * i + 0x28, kGrades[row.grade]);
        draw_number(img, art, Font::Mini, 0xFE, 12 * i + 0x28, row.factories, 2, 1);
    }
    return img;
}

}  // namespace gaius::ui
