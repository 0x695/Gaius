// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/province_render.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <vector>

#include "formats/pal256/pal256.hpp"
#include "formats/pl8/pl8.hpp"

namespace gaius::render {

namespace {

std::string find_asset(const std::string& dir, const std::string& upper) {
    namespace fs = std::filesystem;
    std::string lower = upper;
    for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (const std::string& name : {upper, lower}) {
        const fs::path p = fs::path(dir) / name;
        if (fs::exists(p)) return p.string();
    }
    throw formats::FormatError("render: " + upper + " not found in " + dir);
}

void blit(formats::IndexedImage& out, const formats::PL8Frame& f, int dx, int dy, bool transparent) {
    for (int r = 0; r < f.height; ++r) {
        const int oy = dy + r;
        if (oy < 0 || oy >= out.height) continue;
        for (int c = 0; c < f.width; ++c) {
            const int ox = dx + c;
            if (ox < 0 || ox >= out.width) continue;
            const uint8_t p = f.pixels[static_cast<size_t>(r) * f.width + c];
            if (transparent && p == 0) continue;
            out.pixels[static_cast<size_t>(oy) * out.width + ox] = p;
        }
    }
}

const formats::PL8Frame* frame_at(const formats::PL8Sheet& sheet, int index) {
    if (index < 0 || index >= static_cast<int>(sheet.frames.size())) return nullptr;
    const formats::PL8Frame& f = sheet.frames[static_cast<size_t>(index)];
    return f.pixels.empty() ? nullptr : &f;
}

}  // namespace

ProvinceSprites load_province_sprites(const std::string& asset_dir) {
    ProvinceSprites s;
    s.terrain = formats::pl8::load(find_asset(asset_dir, "FIXT3.PL8"));
    s.units = formats::pl8::load(find_asset(asset_dir, "SPRITE2.PL8"));
    s.palette = formats::pal256::load(find_asset(asset_dir, "SHADE.256"));
    return s;
}

void render_province(const formats::empire2::EmpireMap& map, const ProvinceSprites& sprites,
                     const std::array<model::Actor, model::kActorCount>& actors, formats::IndexedImage& out) {
    using formats::empire2::kMapH;
    using formats::empire2::kMapW;
    out.width = kMapW * kProvincePx;
    out.height = kMapH * kProvincePx;
    out.pixels.assign(static_cast<size_t>(out.width) * out.height, 0);
    for (int row = 0; row < kMapH; ++row) {
        for (int col = 0; col < kMapW; ++col) {
            if (const formats::PL8Frame* f = frame_at(sprites.terrain, province_frame(map.at(row, col))))
                blit(out, *f, col * kProvincePx, row * kProvincePx, false);
        }
    }
    std::vector<const model::Actor*> list;
    for (const model::Actor& a : actors)
        if (a.active() != 0 && a.type() >= 11) list.push_back(&a);
    std::stable_sort(list.begin(), list.end(),
                     [](const model::Actor* p, const model::Actor* q) { return p->screen_y() < q->screen_y(); });
    for (const model::Actor* a : list) {
        if (const formats::PL8Frame* f = frame_at(sprites.units, a->frame()))
            blit(out, *f, a->screen_x(), a->screen_y() + 8 - f->height, true);
    }
}

}  // namespace gaius::render
