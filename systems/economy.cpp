// SPDX-License-Identifier: GPL-3.0-or-later
#include "systems/economy.hpp"

#include <algorithm>

namespace gaius::systems::economy {

namespace {

// The engine's words are 16-bit: every sum wraps, and comparisons are signed.
int16_t w16(int32_t v) { return static_cast<int16_t>(static_cast<uint16_t>(v & 0xFFFF)); }

int g(const model::CityState& state, uint16_t ds) { return model::global_word(state, ds); }
void set(model::CityState& state, uint16_t ds, int v) { model::set_global_word(state, ds, w16(v)); }

constexpr size_t kWorkshopRecord = 24;
constexpr int kWorkshops = 30;

int record_word(const std::vector<uint8_t>& t, size_t o) {
    return o + 1 < t.size() ? static_cast<int16_t>(t[o] | (t[o + 1] << 8)) : 0;
}

}  // namespace

int construction_cost(construction::CommandId id, int forum_grade) {
    if (id == construction::CommandId::Forum)
        return construction::kForumGradeCost[static_cast<size_t>(std::clamp(forum_grade, 0, 7))];
    const int i = static_cast<int>(id);
    return i >= 0 && i < static_cast<int>(kConstructionCost.size()) ? kConstructionCost[static_cast<size_t>(i)] : 0;
}

bool can_afford(const model::CityState& state, int cost) { return cost <= g(state, kFunds); }

void charge(model::CityState& state, int cost) {
    set(state, kFunds, g(state, kFunds) - cost);
    set(state, 0x6BC0, g(state, 0x6BC0) + cost);
}

void refund(model::CityState& state, int amount) {
    set(state, kFunds, g(state, kFunds) + amount);
    set(state, 0x6BC0, g(state, 0x6BC0) - amount);
}

bool grant_emergency_funds(model::CityState& state) {
    if (g(state, 0x6C9A) != 0 || g(state, 0x6C30) > 6) return false;
    set(state, 0x6C9A, 1);
    set(state, kFunds, g(state, kFunds) + 500);
    return true;
}

bool donate_savings(model::CityState& state, int amount) {
    if (amount > g(state, 0x6C2E)) return false;
    set(state, 0x6C2E, g(state, 0x6C2E) - amount);
    set(state, kFunds, g(state, kFunds) + w16(static_cast<int32_t>(amount) * 90 / 100));
    return true;
}

int average_workshop_level(const model::CityState& state) {
    int sum = 0, count = 0;
    for (int i = 0; i < kWorkshops; ++i) {
        const size_t r = static_cast<size_t>(i) * kWorkshopRecord;
        if (record_word(state.table_720, r + 8) == 0) continue;
        sum += record_word(state.table_720, r + 0x10) & 7;
        ++count;
    }
    return count > 0 ? sum / count : 0;
}

PopulationTax population_tax(const model::CityMap& city, int rate, int population) {
    PopulationTax t;
    int16_t units = 0;
    for (int y = 0; y < model::kCityH; ++y) {
        for (int x = 0; x < model::kCityW; ++x) {
            const uint8_t tile = city.tile[y][x];
            if (tile < 0xC8 || tile > 0xD7) continue;
            if (city.service_flags[y][x] & 0x20) {
                units = w16(units + kTaxUnitsPerCell[tile - 0xC8]);
            } else {
                t.uncollected = true;
            }
        }
    }
    // The C runtime's 32-bit multiply and divide (0:03C1, 0:03DB, 0:03EA).
    t.tax = w16(static_cast<int32_t>(units) * rate / 20);
    const int32_t hundredths = static_cast<int32_t>(t.tax) * 100 / (static_cast<int32_t>(w16(population)) + 1);
    t.per_head = w16(hundredths / 100);
    t.per_head_cents = w16(hundredths % 100);
    return t;
}

int industrial_tax(const model::CityState& state, int rate) {
    int16_t output = 0;
    for (int i = 0; i < kWorkshops; ++i) {
        const size_t r = static_cast<size_t>(i) * kWorkshopRecord;
        if (record_word(state.table_720, r + 8) == 0) continue;
        output = w16(output + ((record_word(state.table_720, r + 0x10) & 7) << 6));
    }
    return w16(static_cast<int32_t>(output) * rate / 25);
}

int industrial_tax_pressure(int pressure, int rate, int forums) {
    if (forums == 0) pressure = -50;
    const int step = rate - 5;
    if (rate <= 5 || rate > 10) {
        pressure += 2 * step;
    } else {
        if (pressure < 3) pressure += step >> 1;
        pressure += step;
    }
    return std::clamp(pressure, -50, 24);
}

Settlement settle_accounts(model::CityState& state) {
    Settlement s;
    set(state, 0x6BC2, g(state, 0x6BC0));
    set(state, 0x6BC0, 0);

    set(state, 0x6C2E, g(state, 0x6C2E) + g(state, 0x6C2C));
    if (g(state, 0x6C2E) > 25000) {
        set(state, 0x6C2C, 0);
        set(state, 0x6C2E, 25000);
        s.savings_capped = true;
    }
    const int operating = w16(g(state, 0x6C46) + g(state, 0x6C2C) + g(state, 0x6C08));
    set(state, 0x6BBE, operating);

    const int taxes = w16(g(state, 0x6BC6) + g(state, 0x6BC4));
    set(state, kFunds, g(state, kFunds) + taxes);
    set(state, kFunds, g(state, kFunds) - operating);
    if (g(state, kFunds) < 0) set(state, kFunds, 0);

    if (g(state, 0x6BBA) < 100) set(state, 0x6BBA, g(state, 0x6BBA) + 1);
    int paid = 0;
    if (g(state, kFunds) != 0) {
        set(state, 0x6BB8, 0);
        const int due = g(state, 0x6BBA);
        if (due > g(state, kFunds)) {
            paid = g(state, kFunds);
            set(state, kFunds, 0);
        } else {
            set(state, kFunds, g(state, kFunds) - due);
            paid = due;
        }
    }
    const int profit = w16(taxes - g(state, 0x6BC2) - operating);
    if (g(state, kFunds) > 500 && profit > 0) {
        const int share = w16(static_cast<int32_t>(profit) * 60 / 100);
        set(state, kFunds, g(state, kFunds) - share);
        paid = w16(paid + share);
    }
    set(state, 0x6BAA, paid);
    if (paid == 0) {
        set(state, 0x6BB8, g(state, 0x6BB8) + 1);
        s.tribute_missed = true;
        s.dismissed = g(state, 0x6BB8) >= 3;
        set(state, kFunds, 0);
    }

    const int balance = w16(profit - paid);
    set(state, 0x6BB6, balance);
    set(state, 0x6BB4, balance);
    set(state, 0x6BB2, g(state, 0x6BC6));
    set(state, 0x6BB0, g(state, 0x6BC4));
    set(state, 0x6BAE, g(state, 0x6BC2));
    set(state, 0x6BAC, operating);
    if (g(state, 0x6C46) > g(state, kFunds)) set(state, 0x6C46, g(state, kFunds));
    return s;
}

Settlement run_year(model::CityState& state, int& industrial_rate_sum) {
    set(state, 0x6C00, g(state, 0x6C00) / 12);
    const int industrial_rate = w16(industrial_rate_sum) / 12;

    set(state, 0x6BE8, average_workshop_level(state));

    const PopulationTax pop = population_tax(state.city, g(state, 0x6C00), g(state, 0x6C0E));
    set(state, 0x6C7E, pop.uncollected ? 1 : 0);
    set(state, 0x6BC6, pop.tax);
    set(state, 0x6BCA, pop.per_head);
    set(state, 0x6BC8, pop.per_head_cents);

    set(state, 0x6BC4, industrial_tax(state, industrial_rate));
    set(state, 0x6BFC, industrial_tax_pressure(g(state, 0x6BFC), industrial_rate, g(state, 0x6CA0)));

    const Settlement s = settle_accounts(state);

    set(state, 0x6C00, 0);
    industrial_rate_sum = 0;
    set(state, 0x6C54, g(state, 0x6C56));
    return s;
}

}  // namespace gaius::systems::economy
