// SPDX-License-Identifier: GPL-3.0-or-later
#include "systems/messages.hpp"

namespace gaius::systems::messages {

namespace {

int g(const model::CityState& s, uint16_t ds) { return model::global_word(s, ds); }
void set(model::CityState& s, uint16_t ds, int v) { model::set_global_word(s, ds, v); }

}  // namespace

// DS:0x2CAE, 16 characters each, as 0x27B3C copies them.
const std::array<const char*, 50> kProvinceNameFields = {
    "    Sicilia     ", "    Campania    ", "    Latium      ", " Cisalpine Gaul ", "    Corsica     ",
    "    Sardinia    ", " Alpes Maritimae", "   Narbonensis  ", "  Hispania Inf. ", "     Baetica    ",
    " Lusitania Inf. ", " Lusitania Sup. ", " Tarraconensis  ", " Aquitania Inf. ", "  Hispania Sup. ",
    " Aquitania Sup. ", "  Lugdunensis   ", "    Belgica     ", "  Gallia  Sup.  ", "  Gallia  Inf.  ",
    "  W. Britannia  ", "  E. Britannia  ", " Britannia Sup. ", "   Caledonia    ", "  Germania Inf. ",
    "  Germania Sup. ", "    Pannonia    ", "     Dacia      ", "   Illyricum    ", "    Dalmatia    ",
    "   Macedonia    ", "     Achaea     ", "     Creta      ", "    Thracia     ", "      Asia      ",
    "   Pamphylia    ", "   Cappadocia   ", "    Assyria     ", "     Syria      ", "  Mesopotamia   ",
    "     Judea      ", "     Arabia     ", "    Aegyptus    ", "   Cyrenaica    ", "     Africa     ",
    "    Numidia     ", "   Mauretania   ", "  Caeariensis   ", "   Tingitania   ", "     Moesia     "};

// DS:0x085F, drawn by 0x084B1.
const std::array<const char*, 6> kFundsWarning = {
    "                WARNING !               ", "   The province's funds are now at or   ",
    "     below 1,000 Denarii. Excessive     ", "   spending may bankrupt you, so watch  ",
    "          your funds carefully.         ", "       THIS IS YOUR ONLY WARNING !!     "};

std::string text(Id id) {
    switch (id) {
        case Id::None: return "";
        case Id::RoadMaintenance: return "  Allocate more Plebs to           road maintenance";
        case Id::BuildingMaintenance: return "  Allocate more Plebs to         building maintenance";
        case Id::FirePrevention: return "  You need to allocate more    Plebs to fire prevention";
        case Id::BarbariansEntering: return "  Barbarians are entering               the city.";
        case Id::BarbariansSighted: return "                                   Barbarians sighted.";
        case Id::ArrestOrdered: return "Furious over lack of payment  Rome has ordered your arrest";
        case Id::TributeAgain: return "  Rome is angry that, again     no Tribute has been made.";
        case Id::TributeUnpaid: return "   You have not paid your         annual Tribute to Rome.";
        case Id::Population20000: return "     City population has              passed 20000.";
        case Id::Population16000: return "     City population has              passed 16000.";
        case Id::Population12000: return "     City population has              passed 12000.";
        case Id::Population8000: return "     City population has              passed 8000.";
        case Id::Population4000: return "     City population has              passed 4000.";
        case Id::Population2000: return "     City population has              passed 2000.";
        case Id::Population1000: return "     City population has              passed 1000.";
        case Id::Population200: return "     City population has              passed 200.";
        case Id::Unrest: return "   There is unrest in parts         of the city.";
        case Id::ConstructionPlebs: return "    You must allocate more    Plebs to construction work.";
        case Id::SalaryStopped: return "Your salary has been stopped  you have too much money.";
        case Id::NoFort: return "   You have no need for            another  fort.";
        case Id::NoForum: return "   You have no need for            another  forum.";
        case Id::NoFactory: return "   You have no need for            another  factory.";
        case Id::ProvinceWorkers: return "  The provinces need more       workers for maintenance.";
        case Id::ApproachingHighway: return "  Barbarians are approaching        your highway.";
        case Id::ApproachingRoads: return "  Barbarians are approaching         your roads.";
        case Id::ApproachingTowns: return "  Barbarians are approaching         your towns.";
        case Id::NoCity: return "13 BC - and as yet you have       no city to govern !!.";
    }
    return "";
}

Message plain(Id id) {
    Message m;
    m.id = id;
    m.text = text(id);
    return m;
}

Message at(Id id, Place place, int x, int y) {
    Message m = plain(id);
    m.place = place;
    m.x = x;
    m.y = y;
    return m;
}

Message sighted(int province, int x, int y) {
    Message m = at(Id::BarbariansSighted, Place::Province, x, y);
    if (province >= 0 && province < 50) {
        const char* field = kProvinceNameFields[static_cast<size_t>(province)];
        for (size_t k = 0; k < 16 && 7 + k < m.text.size(); ++k) m.text[7 + k] = field[k];
    }
    return m;
}

bool Board::post(const Message& message, int frames) {
    if (timer != 0) return false;
    current = message;
    timer = frames;
    return true;
}

void Board::tick() {
    if (timer <= 0) return;
    if (--timer == 0) current.place = Place::None;  // 0x279DC: DS:0x6C72 = 0
}

bool post_limited(model::CityState& state, Board& board, uint16_t counter, const Message& message) {
    const int left = g(state, counter) - 1;
    set(state, counter, left);
    if (left > 0 || board.timer != 0) return false;
    set(state, counter, 5);
    return board.post(message);
}

void check_milestones(model::CityState& state, Board& board) {
    struct Milestone {
        int population;
        Id id;
    };
    static constexpr Milestone kMilestones[] = {
        {200, Id::Population200},     {1000, Id::Population1000},   {2000, Id::Population2000},
        {4000, Id::Population4000},   {8000, Id::Population8000},   {12000, Id::Population12000},
        {16000, Id::Population16000}, {20000, Id::Population20000}};
    if (state.table_10.size() < 10) state.table_10.resize(10, 0);
    const int population = g(state, 0x6C0E);
    for (size_t i = 0; i < 8; ++i) {
        if (population > kMilestones[i].population && state.table_10[i] == 0) {
            board.post(plain(kMilestones[i].id));
            state.table_10[i] = 1;
        }
    }
}

void tribute_missed(const model::CityState& state, Board& board) {
    if (board.timer != 0) return;
    switch (g(state, 0x6BB8)) {
        case 1: board.post(plain(Id::TributeUnpaid)); break;
        case 2: board.post(plain(Id::TributeAgain)); break;
        case 3: board.post(plain(Id::ArrestOrdered)); break;
        default: break;
    }
}

bool funds_warning(model::CityState& state) {
    if (g(state, 0x6CA2) > 1000 || g(state, 0x6BE6) != 0) return false;
    set(state, 0x6BE6, 1);
    return true;
}

}  // namespace gaius::systems::messages
