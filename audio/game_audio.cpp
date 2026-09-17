// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio/game_audio.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>

#include "formats/gtl/gtl.hpp"
#include "formats/voc/voc.hpp"
#include "ymfm_opl.h"

namespace gaius::audio {

namespace {

std::vector<uint8_t> read_file(const std::filesystem::path& path) {
    std::vector<uint8_t> data;
    std::FILE* f = std::fopen(path.string().c_str(), "rb");
    if (!f) return data;
    uint8_t buf[4096];
    size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) data.insert(data.end(), buf, buf + n);
    std::fclose(f);
    return data;
}

// The game names its files in lower case; the folder may hold either.
std::filesystem::path find_file(const std::string& dir, const std::string& name) {
    std::filesystem::path p = std::filesystem::path(dir) / name;
    if (std::filesystem::exists(p)) return p;
    std::string upper = name, lower = name;
    for (char& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (const std::string& n : {upper, lower}) {
        p = std::filesystem::path(dir) / n;
        if (std::filesystem::exists(p)) return p;
    }
    return {};
}

}  // namespace

struct GameAudio::Chip : ymfm::ymfm_interface {
    ymfm::ym3812 opl{*this};
    Chip() { opl.reset(); }
    void write(uint8_t reg, uint8_t value) {
        opl.write_address(reg);
        opl.write_data(value);
    }
    int32_t next() {
        ymfm::ym3812::output_data out;
        opl.generate(&out, 1);
        return out.data[0];
    }
};

uint8_t halve_sample(uint8_t s) {
    if (s < 0x80) return static_cast<uint8_t>(0x80 - ((0x80 - s) >> 1));
    if (s > 0x80) return static_cast<uint8_t>((((s + 0x80) & 0xFF) >> 1) + 0x80);
    return s;
}

GameAudio::GameAudio()
    : chip_(std::make_unique<Chip>()), driver_([this](uint8_t reg, uint8_t value) { chip_->write(reg, value); }) {}

GameAudio::~GameAudio() = default;

bool GameAudio::load(const std::string& game_dir) {
    dir_ = game_dir;
    library_.clear();
    driver_loaded_ = false;
    const std::filesystem::path gtl = find_file(game_dir, "SAMPLE.AD");
    if (gtl.empty()) return false;
    try {
        for (const formats::gtl::Timbre& t : formats::gtl::load(gtl.string())) {
            std::vector<uint8_t> entry{t.patch, t.bank};
            entry.insert(entry.end(), t.data.begin(), t.data.end());
            library_.push_back(std::move(entry));
        }
    } catch (const formats::FormatError&) {
        return false;
    }
    driver_.init();
    driver_loaded_ = true;
    return true;
}

bool GameAudio::music_playing() const { return driver_.registered() && driver_.status() == AilXmidi::kPlaying; }

void GameAudio::stop_music() {
    // 0x3219D: a sequence whose status isn't "stopped" is stopped and released.
    if (driver_.registered() && driver_.status() != AilXmidi::kStopped) {
        driver_.stop();
        driver_.release();
        music_name_.clear();
    }
}

void GameAudio::stop_effect() {
    effect_.clear();
    effect_pos_ = 0;
}

void GameAudio::stop_all() {
    stop_music();
    stop_effect();
}

void GameAudio::play_music(const std::string& name, bool tunes_on) {
    if (!driver_loaded_ || !tunes_on) return;
    stop_music();
    stop_effect();
    const std::filesystem::path path = find_file(dir_, name);
    if (path.empty()) return;
    const std::vector<uint8_t> xmi = read_file(path);
    if (xmi.empty()) return;
    if (driver_.registered()) driver_.release();
    if (!driver_.register_sequence(xmi, 0)) return;
    for (int request = driver_.request(); request != -1; request = driver_.request()) {
        const uint8_t bank = static_cast<uint8_t>(request >> 8), patch = static_cast<uint8_t>(request & 0xFF);
        const auto it = std::find_if(library_.begin(), library_.end(),
                                     [&](const std::vector<uint8_t>& e) { return e[0] == patch && e[1] == bank; });
        if (it == library_.end()) return;  // 0x3234E: not in the library, the tune never starts
        driver_.install_timbre(bank, patch, std::vector<uint8_t>(it->begin() + 2, it->end()));
    }
    driver_.start();
    music_name_ = name;
}

void GameAudio::play_effect(int effect, bool effects_on) {
    if (!effects_on || effect < 0 || effect >= kEffectCount) return;
    const std::filesystem::path path = find_file(dir_, kEffectFiles[effect]);
    if (path.empty()) return;
    formats::voc::Sound sound;
    try {
        sound = formats::voc::load(path.string());
    } catch (const formats::FormatError&) {
        return;
    }
    stop_music();
    stop_effect();
    for (uint8_t& s : sound.samples) s = halve_sample(s);
    effect_ = std::move(sound.samples);
    effect_rate_ = sound.sample_rate;
    effect_pos_ = 0;
}

void GameAudio::set_volumes(int music, int effects) {
    music_gain_ = std::clamp(music, 0, 100) / 100.0;
    effects_gain_ = std::clamp(effects, 0, 100) / 100.0;
}

void GameAudio::render(int16_t* out, size_t frames, int rate) {
    if (rate <= 0) return;
    const double chip_rate = static_cast<double>(kChipClock) / 72.0;
    const double chip_step = chip_rate / rate;
    const double service_period = chip_rate / AilXmidi::kServiceRate;
    for (size_t i = 0; i < frames; ++i) {
        chip_phase_ += chip_step;
        while (chip_phase_ >= 1.0) {
            chip_phase_ -= 1.0;
            service_accum_ += 1.0;
            if (service_accum_ >= service_period) {
                service_accum_ -= service_period;
                driver_.serve();
            }
            chip_prev_ = chip_next_;
            chip_next_ = chip_->next();
        }
        double mix = (chip_prev_ + (chip_next_ - chip_prev_) * chip_phase_) * music_gain_;
        if (effect_pos_ < effect_.size()) {
            const size_t k = static_cast<size_t>(effect_pos_);
            const double frac = effect_pos_ - static_cast<double>(k);
            const int a = effect_[k] - 128;
            const int b = k + 1 < effect_.size() ? effect_[k + 1] - 128 : 0;
            mix += (a + (b - a) * frac) * 256.0 * effects_gain_;
            effect_pos_ += static_cast<double>(effect_rate_) / rate;
            if (effect_pos_ >= effect_.size()) stop_effect();
        }
        out[i] = static_cast<int16_t>(std::clamp(mix, -32768.0, 32767.0));
    }
}

}  // namespace gaius::audio
