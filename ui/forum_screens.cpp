// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/forum_screens.hpp"

#include <algorithm>
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

formats::IndexedImage compose_treasurer_screen(const model::CityState& state, const InterfaceArt& art) {
    formats::IndexedImage img = blank_canvas();
    draw_panel(img, art, 0, 0, 20, 12);
    draw_inset(img, art, 0x10, 0x10, 5, 10);

    // 1F6F:2292 and 0x0D361: the funds history (table_72, 17 records of year
    // and value, newest first) as one-pixel columns 6 high from x 0x50 --
    // a loss to the left in colour 8 (a column per 40 Dn, at most 28), a gain
    // to the right in colour 0 (per 80 Dn, at most 12) -- and each record's
    // year in MINIFONT.
    const std::vector<uint8_t>& t = state.table_72;
    int index = g(state, 0x6B38);
    for (int k = 0; k < 17; ++k) {
        const size_t o = static_cast<size_t>(index) * 4;
        const int year = t.size() >= o + 2 ? static_cast<int16_t>(t[o] | (t[o + 1] << 8)) : 0;
        const int value = t.size() >= o + 4 ? static_cast<int16_t>(t[o + 2] | (t[o + 3] << 8)) : 0;
        const int bar_y = 0x1C + 8 * k;
        if (value < 0) {
            const int n = std::min(-value / 40, 28);
            for (int i = 0; i < n; ++i) fill_rect(img, 0x50 - i, bar_y, 1, 6, 8);
        } else {
            const int n = std::min(value / 80, 12);
            for (int i = 0; i < n; ++i) fill_rect(img, 0x50 + i, bar_y, 1, 6, 0);
        }
        const int label_y = 0x1D + 8 * k;
        if (year < 0) draw_text(img, art, Font::Mini, 0x14, label_y, "bc");
        if (year > 0) draw_text(img, art, Font::Mini, 0x14, label_y, "ad");
        if (year != 0) draw_number(img, art, Font::Mini, 0x21, label_y, std::abs(year), 3, 1);
        if (--index < 0) index = 16;
    }

    // The accounts (DS:0x70CA-0x70F0).
    struct Line { int y; const char* text; };
    static constexpr Line kLines[] = {
        {0x06, "            POP.    IND."}, {0x14, "Tax rate"},          {0x26, "City population"},
        {0x32, "Unemployment           %"}, {0x3E, "tax per head     dn   ."}, {0x4E, "previous year"},
        {0x5D, "denarii in  -"},            {0x67, " taxes - population"}, {0x71, " taxes - industry  "},
        {0x80, "denarii out -"},            {0x8A, " construction work"},  {0x94, " operating costs"},
        {0x9E, " tribute to rome"}};
    for (const Line& l : kLines) draw_text(img, art, Font::Font1, 0x64, l.y, l.text);
    draw_text(img, art, Font::Font1, 0x64, 0xAE, g(state, 0x6BB6) >= 0 ? "overall gain of" : "overall loss of");
    draw_number(img, art, Font::Font1, 0x104, 0x26, g(state, 0x6C0E), 5, 1);
    draw_number(img, art, Font::Font1, 0xFC, 0x32, g(state, 0x6BCC), 3, 1);
    draw_number(img, art, Font::Font1, 0x10A, 0x3E, g(state, 0x6BCA), 1, 1);
    draw_number(img, art, Font::Font1, 0x11C, 0x3E, g(state, 0x6BC8), 2, 0);
    draw_number(img, art, Font::Font1, 0x104, 0x67, g(state, 0x6BB2), 4, 1);
    draw_number(img, art, Font::Font1, 0x104, 0x71, g(state, 0x6BB0), 4, 1);
    draw_number(img, art, Font::Font1, 0x104, 0x8A, g(state, 0x6BAE), 4, 1);
    draw_number(img, art, Font::Font1, 0x104, 0x94, g(state, 0x6BAC), 4, 1);
    draw_number(img, art, Font::Font1, 0x104, 0x9E, g(state, 0x6BAA), 4, 1);
    const int balance = g(state, 0x6BB4);
    draw_number(img, art, Font::Font1, 0x104, 0xAE, balance >= 0 ? balance : -balance, 4, 1);

    // 0x0E6E3, each frame: the rates (0x0D623 repaints their stone only for
    // four frames after an arrow is clicked, DS:0x6D8B) ("  %" filled by 100F:17E2 mode 2) and
    // the arrows (DS:0x0404: cells (12, 1) and (13, 1) the population tax's
    // up and down, (16, 1) and (17, 1) the industry tax's).
    draw_text(img, art, Font::Font1, 0xE4, 0x14, number_text(g(state, 0x6C04), 2, 2) + "%");
    draw_text(img, art, Font::Font1, 0x124, 0x14, number_text(g(state, 0x6C02), 2, 2) + "%");
    draw_block(img, art.blocks, 18, 12 * 16, 16);
    draw_block(img, art.blocks, 19, 13 * 16, 16);
    draw_block(img, art.blocks, 18, 16 * 16, 16);
    draw_block(img, art.blocks, 19, 17 * 16, 16);
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
