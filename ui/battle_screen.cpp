// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/battle_screen.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>

#include "formats/pal256/pal256.hpp"
#include "formats/pl8/pl8.hpp"
#include "formats/vpx/vpx.hpp"
#include "systems/military.hpp"
#include "systems/sounds.hpp"

namespace gaius::ui {

namespace {

namespace battle = systems::battle;
namespace military = systems::military;

std::string asset(const std::string& dir, std::string name) {
    namespace fs = std::filesystem;
    fs::path p = fs::path(dir) / name;
    if (fs::exists(p)) return p.string();
    for (char& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    p = fs::path(dir) / name;
    if (fs::exists(p)) return p.string();
    throw formats::FormatError("battle screen: " + name + " not found in " + dir);
}

int byte_of(const model::CityState& s, int slot, size_t field) {
    if (slot < 0 || slot >= model::kActorCount) return 0;
    return static_cast<int8_t>(s.objects[static_cast<size_t>(slot)].raw[field]);
}

int men(const model::CityState& s, int cohort) {
    return byte_of(s, cohort, military::kCohortRegulars) + byte_of(s, cohort, military::kCohortIrregulars) +
           byte_of(s, cohort, military::kCohortAuxiliaries);
}

// 100F:0EF9: a filled rectangle.
void fill(formats::IndexedImage& img, int x, int y, int w, int h, uint8_t colour) {
    for (int yy = std::max(0, y); yy < std::min(img.height, y + h); ++yy)
        for (int xx = std::max(0, x); xx < std::min(img.width, x + w); ++xx)
            img.pixels[static_cast<size_t>(yy) * img.width + xx] = colour;
}

void blit(formats::IndexedImage& img, const formats::PL8Sheet& sheet, int index, int x, int y) {
    if (index < 0 || index >= static_cast<int>(sheet.frames.size())) return;
    const formats::PL8Frame& f = sheet.frames[static_cast<size_t>(index)];
    for (int r = 0; r < f.height; ++r) {
        const int yy = y + r;
        if (yy < 0 || yy >= img.height) continue;
        for (int c = 0; c < f.width; ++c) {
            const int xx = x + c;
            if (xx < 0 || xx >= img.width) continue;
            const uint8_t p = f.pixels[static_cast<size_t>(r) * f.width + c];
            if (p != 0) img.pixels[static_cast<size_t>(yy) * img.width + xx] = p;
        }
    }
}

// 3496:0718: the stone pattern 1F6F:1EC1 scatters over a panel's middle.
constexpr std::array<uint8_t, 50> kPanelPattern = {0, 2, 5, 4, 3, 1, 5, 4, 3, 4, 1, 3, 2, 5, 2, 3, 4,
                                                   6, 6, 2, 3, 1, 5, 1, 3, 1, 0, 4, 3, 1, 2, 2, 0, 3,
                                                   6, 5, 4, 6, 4, 2, 4, 3, 4, 5, 1, 2, 4, 0, 2, 0};

// 1F6F:1EC1: a panel of P_BLOCKS.PL8 frames 0-8 (corners, edges, middle),
// `cols` x `rows` 16 px cells, the middle cells taking 0x13 + the pattern
// where it isn't 0.
void panel(formats::IndexedImage& img, const formats::PL8Sheet& blocks, int x, int y, int cols, int rows) {
    size_t k = 0;
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            int frame = r == 0 ? 0 : r == rows - 1 ? 6 : 3;
            frame += c == 0 ? 0 : c == cols - 1 ? 2 : 1;
            if (frame == 4) {
                k = k + 1 > 49 ? 0 : k + 1;
                if (kPanelPattern[k] != 0) frame = kPanelPattern[k] + 0x13;
            }
            blit(img, blocks, frame, x + c * 16, y + r * 16);
        }
    }
}

// 0x0954F: the background again, then a strip over the buttons tiled from the
// picture's own stone at (4, 0xB8) (1F6F:1E73 grabs it, 303E:0A9F puts it at
// x 8-247, y 0xAA-0xC7 -- the 8 x 8 block size is INFERENCE).
void message_strip(BattleScreen& b, const model::CityState& state, const BattleArt& art) {
    draw_background(b, state, art);
    const formats::IndexedImage& bg = art.background;
    for (int y = 0xAA; y < 0xC8; ++y) {
        for (int x = 8; x < 8 + 30 * 8; ++x) {
            const int sx = 4 + (x - 8) % 8, sy = 0xB8 + (y - 0xAA) % 8;
            if (y < b.screen.height && sy < bg.height)
                b.screen.pixels[static_cast<size_t>(y) * b.screen.width + x] =
                    bg.pixels[static_cast<size_t>(sy) * bg.width + sx];
        }
    }
}

void show_message(BattleScreen& b, const model::CityState& state, const BattleArt& art, const char* first,
                  const char* second, int frames) {
    message_strip(b, state, art);
    b.message = {first, second};
    b.message_timer = frames;
}

void play(const formats::vas::Animation& animation, int& counter, int last, formats::IndexedImage& screen) {
    if (counter < 2) return;
    if (counter > last) {
        counter = 0;
        return;
    }
    const size_t frame = static_cast<size_t>(counter - 2);
    if (frame < animation.frame_offsets.size()) {
        formats::vas::Planes planes = formats::vas::to_planes(screen);
        formats::vas::apply_frame(animation, frame, planes);
        screen = formats::vas::to_image(planes);
    }
    ++counter;
}

void number(std::vector<uint8_t>& rgb, int x, int y, int value, int digits, const GameFont& font) {
    std::string text = std::to_string(value);
    if (static_cast<int>(text.size()) < digits) text.insert(0, static_cast<size_t>(digits) - text.size(), ' ');
    draw_game_text(rgb, 320, 200, x, y, text.c_str(), 1, font);
}

}  // namespace

BattleArt load_battle_art(const std::string& dir) {
    BattleArt art;
    art.background = formats::vpx::decode(asset(dir, "WAR2.VPX")).image;
    art.palette = formats::pal256::load(asset(dir, "WAR2.256"));
    art.banners = formats::pl8::load(asset(dir, "SPRITE2X.PL8"));
    art.blocks = formats::pl8::load(asset(dir, "P_BLOCKS.PL8"));
    art.lose = formats::vas::load(asset(dir, "LOSE0001.VAS"));
    art.wins = formats::vas::load(asset(dir, "WINS0001.VAS"));
    art.font = GameFont{formats::pl8::load(asset(dir, "FONT2.PL8")), art.palette};
    art.interface_palette = formats::pal256::load(asset(dir, "SHADE.256"));
    art.dialog_font = GameFont{formats::pl8::load(asset(dir, "FONT1.PL8")), art.interface_palette};
    // 1F6F:292B plots every MINIFONT bit in colour 0 (2EF9:0039 = 0).
    art.mini = load_mini_font(dir, art.palette.colors[0]);
    try {
        art.offer = formats::vpx::decode(asset(dir, "WARMESS.VPX")).image;  // only the offer uses it
    } catch (const formats::FormatError&) {
    }
    return art;
}

BattleScreen begin_battle(const model::CityState& state, const BattleArt& art, int cohort, int army) {
    BattleScreen b;
    b.cohort = cohort;
    b.army = army;
    b.barbarian_banner = model::global_word(state, 0x6BD8);
    b.roman_banner = byte_of(state, cohort, 0x2A) * 4;
    b.start_army = byte_of(state, army, battle::kArmySize);
    b.start_men = men(state, cohort) * 2;
    b.start_regulars = byte_of(state, cohort, military::kCohortRegulars);
    b.start_irregulars = byte_of(state, cohort, military::kCohortIrregulars);
    b.start_auxiliaries = byte_of(state, cohort, military::kCohortAuxiliaries);
    draw_background(b, state, art);
    return b;
}

void draw_background(BattleScreen& b, const model::CityState& state, const BattleArt& art) {
    b.screen = art.background;
    int h = std::min(b.start_men * 2, 64);
    fill(b.screen, 0x10E, 0x62 - h - 1, 16, h + 2, 0);
    h = std::min(b.start_army * 4, 64);
    fill(b.screen, 0x124, 0x62 - h - 1, 16, h + 2, 0);
    h = std::min(men(state, b.cohort) * 4, 64);
    fill(b.screen, 0x10F, 0x62 - h, 14, h, 12);
    h = std::min(byte_of(state, b.army, battle::kArmySize) * 4, 64);
    fill(b.screen, 0x125, 0x62 - h, 14, h, 12);
}

int button_at(const BattleScreen& b, int x, int y) {
    if (b.closing != 0 || b.message_timer != 0 || b.confirming_retreat) return -1;
    if (x > 0xF0 || y < 0xA8 || x < 0x10) return -1;
    if (x < 0x30) return static_cast<int>(battle::Tactic::Tortoise);
    if (x < 0x40) return -1;
    if (x < 0x60) return static_cast<int>(battle::Tactic::Assault);
    if (x < 0x70) return -1;
    if (x < 0x90) return static_cast<int>(battle::Tactic::Flank);
    if (x < 0xA0) return -1;
    if (x < 0xC0) return static_cast<int>(battle::Tactic::Charge);
    if (x < 0xD0) return -1;
    return kRetreatButton;
}

void play_round(BattleScreen& b, model::CityState& state, const BattleArt& art, battle::Tactic tactic,
                const systems::month::Random& random) {
    b.last = battle::fight_round(state, b.cohort, b.army, tactic, random);
    b.has_round = true;
    if (b.last.victory) {
        b.closing = 0xA9;
        show_message(b, state, art, "Victory is yours. The enemy", "scatters in disarray.", 0xAA);
        b.wins_frame = 2;
        systems::sounds::request(systems::sounds::kCheer);  // 0x22ECA
    } else if (b.last.defeat) {
        b.closing = 0xA9;
        show_message(b, state, art, "Defeat and dishonor as the", "barbarians sweep over you.", 0xAA);
        b.lose_frame = 2;
        systems::sounds::request(systems::sounds::kCheer);  // 0x2307D: the barbarians cheer
    } else if (b.last.outcome == battle::Outcome::Even) {
        show_message(b, state, art, "It is a tough battle and", "still could go either way.", 0x82);
    } else if (b.last.outcome == battle::Outcome::RomansWon) {
        show_message(b, state, art, "The battle goes well and", "the enemy is weakening.", 0x82);
    } else {
        show_message(b, state, art, "Your soldiers are faltering.", "You should try a new tactic.", 0x82);
        b.hit_frame = 2;
    }
}

Cohort2Answer answer_cohort2(BattleScreen& b, int x, int y, bool right_click) {
    if (b.cohort2 == Cohort2Offer::Offer) {
        if (!right_click && y > 0x49 && y < 0x74) {
            b.cohort2 = Cohort2Offer::Question;
            return Cohort2Answer::Waiting;
        }
        b.cohort2 = Cohort2Offer::None;
        return Cohort2Answer::Declined;
    }
    if (b.cohort2 == Cohort2Offer::Question && !right_click && x / 16 == 12) {
        // DS:0x132C's buttons: Yes 0x236EE, No 0x236FB (DS:0x5808).
        if (y / 16 == 6) {
            b.cohort2 = Cohort2Offer::None;
            return Cohort2Answer::Accepted;
        }
        if (y / 16 == 7) {
            b.cohort2 = Cohort2Offer::None;
            return Cohort2Answer::Declined;
        }
    }
    return Cohort2Answer::Waiting;
}

bool answer_retreat(BattleScreen& b, model::CityState& state, const BattleArt& art, int x, int y) {
    if (!b.confirming_retreat || x / 16 != 12) return false;
    if (y / 16 == 6) {
        b.confirming_retreat = false;
        battle::retreat(state, b.cohort);
        b.retreated = true;
        b.closing = 0x78;
        show_message(b, state, art, "The barbarians press on .", "Your troops are demoralized.", 0x82);
        b.hit_frame = 2;
        return true;
    }
    if (y / 16 == 7) {
        b.confirming_retreat = false;
        return true;
    }
    return false;
}

void battle_frame(BattleScreen& b, const model::CityState& state, const BattleArt& art, bool clicked) {
    if (b.message_timer != 0) {
        if (clicked) {
            b.lose_frame = b.wins_frame = b.hit_frame = 0;
            if (b.closing > 2) {
                b.closing = 2;
            } else {
                b.message_timer = 1;
            }
        }
        --b.message_timer;
        // 0x22497: swords and war cries at fixed counts of the message's timer.
        switch (b.message_timer) {
            case 0x73:
            case 0x5A:
            case 0x4B:
            case 0x1E:
            case 0x0F: systems::sounds::request(systems::sounds::kSword); break;
            case 0x32:
            case 0x02: systems::sounds::request(systems::sounds::kWarCry); break;
            default: break;
        }
        play(art.lose, b.lose_frame, 22, b.screen);
        play(art.wins, b.wins_frame, 21, b.screen);
        play(art.lose, b.hit_frame, 14, b.screen);
        if (b.message_timer == 0) {
            draw_background(b, state, art);
            b.message = {};
        }
    }
    if (b.closing > 0) --b.closing;
}

void compose_battle(const BattleScreen& b, const model::CityState& state, const BattleArt& art,
                    std::vector<uint8_t>& rgb) {
    if (b.cohort2 != Cohort2Offer::None) {
        // 0x09611: WARMESS.VPX in WAR2.256, and the two lines in the battle's
        // FONT2 (loaded at 0x22297, before the offer). 0x2357E: FONT1 back,
        // the interface palette, the pages cleared, a 14 x 5 panel and the
        // Yes / No buttons (DS:0x132C).
        formats::IndexedImage img;
        const bool question = b.cohort2 == Cohort2Offer::Question;
        if (question || art.offer.pixels.empty()) {
            img = b.screen;
            std::fill(img.pixels.begin(), img.pixels.end(), uint8_t{0});
        } else {
            img = art.offer;
        }
        if (question) {
            panel(img, art.blocks, 0x30, 0x40, 14, 5);
            blit(img, art.blocks, 29, 12 * 16, 6 * 16);
            blit(img, art.blocks, 29, 12 * 16, 7 * 16);
        }
        const formats::Palette& palette = question ? art.interface_palette : art.palette;
        rgb.resize(static_cast<size_t>(320) * 200 * 3);
        for (size_t i = 0; i < img.pixels.size() && i < 64000; ++i) {
            const formats::RGB c = palette.colors[img.pixels[i]];
            rgb[i * 3] = c.r;
            rgb[i * 3 + 1] = c.g;
            rgb[i * 3 + 2] = c.b;
        }
        if (question) {
            draw_game_text(rgb, 320, 200, 0x5A, 0x4A, "Cohort ?", 1, art.dialog_font);
            draw_game_text(rgb, 320, 200, 0x32, 0x62, "     Yes", 1, art.dialog_font);
            draw_game_text(rgb, 320, 200, 0x32, 0x72, "      No", 1, art.dialog_font);
        } else {
            draw_game_text(rgb, 320, 200, 0x0A, 0x52, " Left click here for Cohort. Click", 1, art.font);
            draw_game_text(rgb, 320, 200, 0x0A, 0x62, "elsewhere or right-click to continue.", 1, art.font);
        }
        return;
    }
    formats::IndexedImage img = b.screen;
    // 0x23878: the Cohort's standard and the barbarians' banner.
    blit(img, art.banners, b.roman_banner, 0x10E, 0x12);
    blit(img, art.banners, b.barbarian_banner, 0x124, 0x10);
    if (b.confirming_retreat) {
        // 0x23708: 100F:00EF runs 2EF9:0C50 on both display pages, which
        // clears them to colour 0 (findings section 42.2), then a 14 x 5 panel and the Yes / No buttons
        // (DS:0x132C).
        std::fill(img.pixels.begin(), img.pixels.end(), 0);
        panel(img, art.blocks, 0x30, 0x40, 14, 5);
        blit(img, art.blocks, 29, 12 * 16, 6 * 16);
        blit(img, art.blocks, 29, 12 * 16, 7 * 16);
    }
    rgb.resize(static_cast<size_t>(320) * 200 * 3);
    // The dialog sets the palette saved at 68F6:B6A8 for the whole screen and
    // puts WAR2's back after (0x237EE); that the saved one is SHADE.256 is
    // INFERENCE.
    const formats::Palette& palette = b.confirming_retreat ? art.interface_palette : art.palette;
    for (size_t i = 0; i < img.pixels.size() && i < 64000; ++i) {
        const formats::RGB c = palette.colors[img.pixels[i]];
        rgb[i * 3] = c.r;
        rgb[i * 3 + 1] = c.g;
        rgb[i * 3 + 2] = c.b;
    }
    const auto text = [&](int x, int y, const std::string& s, const GameFont& font) {
        draw_game_text(rgb, 320, 200, x, y, s.c_str(), 1, font);
    };
    if (b.confirming_retreat) {
        // The dialog's own loop draws nothing else.
        text(0x5A, 0x4A, "Retreat ?", art.dialog_font);
        text(0x32, 0x62, "     Yes", art.dialog_font);
        text(0x32, 0x72, "      No", art.dialog_font);
        return;
    }
    if (b.message_timer != 0) {
        text(8, 0xAA, b.message[0], art.font);
        text(8, 0xBC, b.message[1], art.font);
    } else {
        // 0x22651: the Cohort's emblem (DS:0x45BE via DS:0x7116, 12-character
        // fields) and the race (DS:0x2BAD via DS:0x70F6, 16), padded as stored.
        static constexpr const char* kEmblems[] = {"   Eagle    ", "  Rabbit    ", "   Snake    ", "   Fish     ",
                                                   "   Horse    ", "    Pig     ", "   Wolf     ", "   Hero     ",
                                                   " Explorer   ", " Protector  "};
        static constexpr const char* kRaceFields[] = {
            " Carthaginians  ", "     Mauri      ", "    Blemmyes    ", "   Sassanids    ",
            "      Huns      ", "   Ostrogoths   ", "    Visigoths   ", "    Alamanni    ",
            "     Saxons     ", "     Picts      ", "     Celts      ", "  Celtiberians  ",
            "    Helvetii    ", "    Ligurians   ", "    Illyrians   ", "     Volcae     "};
        const int n = byte_of(state, b.cohort, 0x2A);
        if (n >= 0 && n < 10) text(8, 0xA2, kEmblems[n], art.font);
        const int race = model::global_word(state, 0x6BD6);
        if (race >= 0 && race < 16) text(0x7E, 0xA2, kRaceFields[race], art.font);
    }
    // 0x226BC: the figures, in MINIFONT.
    text(0x118, 0x68, "Men", art.mini);
    number(rgb, 0x128, 0x70, byte_of(state, b.army, battle::kArmySize), 2, art.mini);
    number(rgb, 0x110, 0x70, men(state, b.cohort), 2, art.mini);
    text(0x110, 0x8C, "Morale", art.mini);
    number(rgb, 0x11E, 0x96, byte_of(state, b.cohort, military::kCohortMorale), 1, art.mini);
    text(0x10C, 0xA0, "r   -", art.mini);
    text(0x10C, 0xAA, "i   -", art.mini);
    text(0x10C, 0xB4, "a   -", art.mini);
    number(rgb, 0x118, 0xA0, byte_of(state, b.cohort, military::kCohortRegulars), 2, art.mini);
    number(rgb, 0x118, 0xAA, byte_of(state, b.cohort, military::kCohortIrregulars), 2, art.mini);
    number(rgb, 0x118, 0xB4, byte_of(state, b.cohort, military::kCohortAuxiliaries), 2, art.mini);
    number(rgb, 0x128, 0xA0, b.start_regulars, 2, art.mini);
    number(rgb, 0x128, 0xAA, b.start_irregulars, 2, art.mini);
    number(rgb, 0x128, 0xB4, b.start_auxiliaries, 2, art.mini);
}

}  // namespace gaius::ui
