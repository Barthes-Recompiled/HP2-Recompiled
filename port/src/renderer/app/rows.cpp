// xboxrecomp: every setting as one table of rows, after SCDA-XBERecomp's app::rows (TGP482, src/app/menu.cpp, MIT:
// src/renderer/LICENSE). The F10 panel (menu.cpp) draws each page's rows as a label with its value stepped left / right,
// and the game's own Settings pages (hle_frontend.c, through app_PageOption*) list the rows marked for them, so both
// show the same names and values and step them the same way. See app.h.
#include "app/app.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace app {

namespace {

// a row's setting
enum Id {
    kDisplayMode, kResolution, kAspect, kVsync, kFov, kInternal, kRenderScale, kMaxHeight, kMsaa, kSmaa, kAniso,
    kBrightness, kContrast, kFpsCounter, kDrawAll, kBloom, kBloomStrength, kSharpen, kSaturation, kVibrance, kWarmth,
    kVignette, kGrain, kSpeakers, kKbm, kMouseSensitivity, kMouseInvertY, kMoveMode, kController, kDeadzoneLeft,
    kSensitivityLeft, kDeadzoneRight, kCameraSpeed, kInvertX, kInvertY, kVibration, kVibrationStrength, kCutsceneSkip,
    kBeanVoice,
    kIntroSkip, kColorBlind, kColorBlindStrength, kExploreGrounds, kBroomLandFlat, kDebugKeys, kAudioTrace, kIds
};

// how a row's value reads and steps: an on / off flag, a list of choices, or a range; the display mode, aspect ratio,
// resolution, warmth and grounds rows are worked out in Value / Step
struct Def {
    bool Settings::*flag = nullptr;
    uint32_t Settings::*value = nullptr;
    const uint32_t* choices = nullptr;
    const char* const* names = nullptr;
    int count = 0;
    uint32_t lo = 0, hi = 0, step = 0;
    const char* unit = "";
    const char* zero = nullptr;  // a range's 0 by name
};

constexpr uint32_t kMsaa_[] = {0, 2, 4, 8};
const char* const kMsaaNames[] = {"Off", "2x", "4x", "8x"};
constexpr uint32_t kAniso_[] = {0, 2, 4, 8, 16};
const char* const kAnisoNames[] = {"Game's", "2x", "4x", "8x", "16x"};
// internal resolution: render lines, 0 follows the window
constexpr uint32_t kInternal_[] = {0, 480, 720, 960, 1080, 1440, 1920, 2160};
const char* const kInternalNames[] = {"Follow window", "480 (Xbox, 1x)", "720 (1.5x)", "960 (2x)",
                                      "1080 (2.25x)",  "1440 (3x)",      "1920 (4x)",  "2160 (4.5x, 4K)"};
constexpr uint32_t kSpeakers_[] = {2, 6};
const char* const kSpeakerNames[] = {"Stereo", "5.1 surround"};
constexpr uint32_t kColorBlind_[] = {kCbOff, kCbProtanopia, kCbDeuteranopia, kCbTritanopia};
const char* const kColorBlindNames[] = {"Off", "Protanopia (red-weak)", "Deuteranopia (green-weak)",
                                        "Tritanopia (blue-weak)"};
constexpr uint32_t kMove_[] = {kMoveSlowWalk, kMoveWalk, kMoveRun};
const char* const kDisplayNames[] = {"Windowed", "Borderless", "Fullscreen"};
const char* const kAspectNames[] = {"4:3", "16:9", "Stretch"};

template <size_t N>
constexpr Def List(uint32_t Settings::*value, const uint32_t (&choices)[N], const char* const* names) {
    return {.value = value, .choices = choices, .names = names, .count = int(N)};
}
constexpr Def Range(uint32_t Settings::*value, uint32_t lo, uint32_t hi, uint32_t step, const char* unit = "",
                    const char* zero = nullptr) {
    return {.value = value, .lo = lo, .hi = hi, .step = step, .unit = unit, .zero = zero};
}
constexpr Def Flag(bool Settings::*flag) { return {.flag = flag}; }

using S = Settings;
const Def kDefs[kIds] = {
    /* kDisplayMode */ {},
    /* kResolution */ {},
    /* kAspect */ {},
    /* kVsync */ Flag(&S::vsync),
    /* kFov */ Range(&S::fov, 80, 130, 5, "%"),
    /* kInternal */ List(&S::internal_height, kInternal_, kInternalNames),
    /* kRenderScale */ Range(&S::render_percent, 25, 200, 5, "%"),
    /* kMaxHeight */ Range(&S::render_max_height, 0, 4320, 120, " lines", "No limit"),
    /* kMsaa */ List(&S::msaa, kMsaa_, kMsaaNames),
    /* kSmaa */ Flag(&S::smaa),
    /* kAniso */ List(&S::aniso, kAniso_, kAnisoNames),
    /* kBrightness */ Range(&S::brightness, 0, 100, 5),
    /* kContrast */ Range(&S::contrast, 0, 100, 5),
    /* kFpsCounter */ Flag(&S::fps_counter),
    /* kDrawAll */ Flag(&S::draw_all),
    /* kBloom */ Flag(&S::bloom),
    /* kBloomStrength */ Range(&S::bloom_strength, 0, 100, 5),
    /* kSharpen */ Range(&S::sharpen, 0, 100, 10, "", "Off"),
    /* kSaturation */ Range(&S::saturation, 0, 200, 10, "%"),
    /* kVibrance */ Range(&S::vibrance, 0, 100, 10, "", "Off"),
    /* kWarmth */ Range(&S::warmth, 0, 100, 5),
    /* kVignette */ Range(&S::vignette, 0, 100, 10, "", "Off"),
    /* kGrain */ Range(&S::grain, 0, 100, 10, "", "Off"),
    /* kSpeakers */ List(&S::speakers, kSpeakers_, kSpeakerNames),
    /* kKbm */ Flag(&S::kbm),
    /* kMouseSensitivity */ Range(&S::mouse_sensitivity, 1, 100, 5),
    /* kMouseInvertY */ Flag(&S::mouse_invert_y),
    /* kMoveMode */ List(&S::move_mode, kMove_, kMoveModeNames),
    /* kController */ Flag(&S::controller),
    /* kDeadzoneLeft */ Range(&S::deadzone_left, 0, 50, 5, "%"),
    /* kSensitivityLeft */ Range(&S::sensitivity_left, 25, 200, 25, "%"),
    /* kDeadzoneRight */ Range(&S::deadzone_right, 0, 50, 5, "%"),
    /* kCameraSpeed */ Range(&S::sensitivity_right, 25, 200, 25, "%"),
    /* kInvertX */ Flag(&S::invert_x),
    /* kInvertY */ Flag(&S::invert_y),
    /* kVibration */ Flag(&S::vibration),
    /* kVibrationStrength */ Range(&S::vibration_strength, 0, 100, 10, "%"),
    /* kCutsceneSkip */ Flag(&S::cutscene_skip),
    /* kIntroSkip */ Flag(&S::intro_skip),
    /* kBeanVoice */ Flag(&S::bean_voice),
    /* kColorBlind */ List(&S::colorblind, kColorBlind_, kColorBlindNames),
    /* kColorBlindStrength */ Range(&S::colorblind_strength, 0, 100, 10, "%"),
    /* kExploreGrounds */ Flag(&S::grounds_warps),  // shown inverted: exploring is the path warps off
    /* kBroomLandFlat */ Flag(&S::broom_land_flat),
    /* kDebugKeys */ Flag(&S::debug_keys),
    /* kAudioTrace */ Flag(&S::audio_trace),
};

using rows::Kind;
using rows::Row;
constexpr Row Head(const char* label) { return {Kind::Heading, -1, label}; }
constexpr Row Val(Id id, const char* label, bool game = false) { return {Kind::Value, id, label, game}; }

// the pages; a row marked true is on the game's own page too (hle_frontend.c), in this order
const Row kGraphicsRows[] = {
    Head("Display"),
    Val(kDisplayMode, "Display mode", true),
    Val(kResolution, "Resolution", true),
    Val(kAspect, "Aspect ratio", true),
    Val(kVsync, "Vsync", true),
    Val(kFov, "Field of view", true),
    Head("Resolution"),
    Val(kInternal, "Internal resolution"),
    Val(kRenderScale, "Render scale", true),
    Val(kMaxHeight, "Max render height"),
    Head("Anti-aliasing and filtering"),
    Val(kMsaa, "MSAA", true),
    Val(kSmaa, "SMAA", true),
    Val(kAniso, "Anisotropic filtering", true),
    Head("Picture"),
    Val(kBrightness, "Brightness", true),
    Val(kContrast, "Contrast"),
    Val(kFpsCounter, "FPS counter", true),
    Val(kDrawAll, "Draw everything in view"),
    Head("Post-processing"),
    Val(kBloom, "Bloom", true),
    Val(kBloomStrength, "Bloom strength"),
    Val(kSharpen, "Sharpen", true),
    Val(kSaturation, "Saturation"),
    Val(kVibrance, "Vibrance"),
    Val(kWarmth, "Warmth"),
    Val(kVignette, "Vignette"),
    Val(kGrain, "Film grain"),
};
const Row kAudioRows[] = {
    Head("Output"),
    Val(kSpeakers, "Speakers"),
};
const Row kControlsRows[] = {
    Head("Controller"),
    Val(kController, "Use controller", true),
    Val(kVibration, "Vibration", true),
    Val(kVibrationStrength, "Vibration strength", true),
    Val(kInvertX, "Invert camera X", true),
    Val(kInvertY, "Invert camera Y", true),
    Val(kDeadzoneLeft, "Left stick deadzone", true),
    Val(kSensitivityLeft, "Left stick sensitivity"),
    Val(kDeadzoneRight, "Right stick deadzone", true),
    Val(kCameraSpeed, "Camera speed", true),
    {Kind::PadMap, -1, "Button remapping"},
    Head("Keyboard and mouse"),
    Val(kKbm, "Keyboard and mouse", true),
    Val(kMouseSensitivity, "Mouse sensitivity", true),
    Val(kMouseInvertY, "Invert mouse Y", true),
    Val(kMoveMode, "Movement keys pace", true),
    {Kind::Keys, -1, "Key bindings"},
};
const Row kGameplayRows[] = {
    Head("Cutscenes"),
    Val(kCutsceneSkip, "Cutscene fast-forward", true),
    Val(kIntroSkip, "Skip start-up logos", true),
    Head("Sound"),
    Val(kBeanVoice, "Bean pickup voice", true),
};
const Row kAccessibilityRows[] = {
    Head("Colour blindness"),
    Val(kColorBlind, "Correction filter"),
    Val(kColorBlindStrength, "Strength"),
};
const Row kEnhancementsRows[] = {
    Head("Hogwarts grounds"),
    Val(kExploreGrounds, "Explore Hogwarts Grounds (experimental)"),
    Head("Broom"),
    Val(kBroomLandFlat, "Broom lands anywhere flat", true),
};
const Row kDebugRows[] = {
    {Kind::DebugTools, -1, "Tools"},
    Head("Debug settings"),
    Val(kDebugKeys, "Debug keys"),
    Val(kAudioTrace, "Audio trace"),
};

struct PageDef {
    const char* name;
    const Row* rows;
    size_t count;
};
template <size_t N>
constexpr PageDef P(const char* name, const Row (&r)[N]) { return {name, r, N}; }
const PageDef kPageDefs[rows::kPages] = {
    P("Graphics", kGraphicsRows), P("Audio", kAudioRows), P("Controls", kControlsRows),
    P("Gameplay", kGameplayRows), P("Accessibility", kAccessibilityRows), P("Enhancements", kEnhancementsRows),
    P("Debug", kDebugRows),
};

// xboxrecomp: the sizes for an aspect ratio, biggest first: the monitor's own modes of that shape and the common ones
// that fit the desktop; stretch takes every mode
bool Shaped(uint32_t w, uint32_t h, AspectMode a) {
    if (a == AspectMode::Stretch) return true;
    float r = float(w) / float(h), want = a == AspectMode::Wide ? 16.0f / 9.0f : 4.0f / 3.0f;
    return r > want - 0.02f && r < want + 0.02f;
}

std::vector<std::pair<uint32_t, uint32_t>> ModesFor(AspectMode a) {
    static const std::vector<std::pair<uint32_t, uint32_t>> monitor = [] {
        std::vector<std::pair<uint32_t, uint32_t>> m;
        DEVMODEW dm{};
        dm.dmSize = sizeof dm;
        for (DWORD i = 0; EnumDisplaySettingsW(nullptr, i, &dm); i++)
            if (dm.dmPelsWidth >= 640 && dm.dmPelsHeight >= 480) m.emplace_back(dm.dmPelsWidth, dm.dmPelsHeight);
        return m;
    }();
    static const std::pair<uint32_t, uint32_t> kCommon[] = {
        {640, 480},   {800, 600},   {1024, 768},  {1280, 960},  {1440, 1080}, {1600, 1200}, {1920, 1440},
        {2048, 1536}, {2880, 2160}, {1280, 720},  {1600, 900},  {1920, 1080}, {2560, 1440}, {3200, 1800},
        {3840, 2160}};
    uint32_t dw = GetSystemMetrics(SM_CXSCREEN), dh = GetSystemMetrics(SM_CYSCREEN);
    std::vector<std::pair<uint32_t, uint32_t>> m;
    for (auto [w, h] : monitor)
        if (Shaped(w, h, a)) m.emplace_back(w, h);
    for (auto [w, h] : kCommon)
        if (Shaped(w, h, a) && w <= dw && h <= dh) m.emplace_back(w, h);
    std::sort(m.begin(), m.end(), [](auto x, auto y) { return x.first * x.second > y.first * y.second; });
    m.erase(std::unique(m.begin(), m.end()), m.end());
    return m;
}

int Wrap(int i, int n) { return ((i % n) + n) % n; }

// a range's next value a step away, on the step's grid (a value hpcos.toml set off it lands on it)
uint32_t StepRange(const Def& d, uint32_t v, int dir) {
    int s = int(d.step), n = dir > 0 ? (int(v) / s + 1) * s : ((int(v) + s - 1) / s - 1) * s;
    return uint32_t(std::clamp(n, int(d.lo), int(d.hi)));
}

int ChoiceIndex(const Def& d, uint32_t v) {
    for (int k = 0; k < d.count; k++)
        if (d.choices[k] == v) return k;
    return -1;
}

std::string ValueOf(const Settings& s, Id id) {
    const Def& d = kDefs[id];
    switch (id) {
        case kDisplayMode: return kDisplayNames[std::min<uint32_t>(uint32_t(s.display_mode), 2)];
        case kAspect: {
            std::string v = kAspectNames[std::min<uint32_t>(uint32_t(s.aspect), 2)];
            return s.aspect != BootAspect() && !StartMenuOpen() ? v + " (next start)" : v;
        }
        case kResolution:
            return s.width && s.height ? std::to_string(s.width) + " x " + std::to_string(s.height) : "Desktop";
        case kWarmth:
            return s.warmth == 50 ? "As the game"
                                  : (s.warmth < 50 ? "Cooler " : "Warmer ") + std::to_string(s.warmth);
        case kExploreGrounds: return s.grounds_warps ? "Off" : "On";
        default: break;
    }
    if (d.flag) return s.*d.flag ? "On" : "Off";
    uint32_t v = s.*d.value;
    if (d.choices) {
        int k = ChoiceIndex(d, v);
        return k >= 0 ? d.names[k] : std::to_string(v);  // a value hpcos.toml set that the list lacks
    }
    if (v == 0 && d.zero) return d.zero;
    return std::to_string(v) + d.unit;
}

void StepOf(Settings& s, Id id, int dir) {
    const Def& d = kDefs[id];
    bool display = false;
    switch (id) {
        case kDisplayMode:
            s.display_mode = DisplayMode(Wrap(int(s.display_mode) + dir, 3));
            display = true;
            break;
        case kAspect:
            s.aspect = AspectMode(Wrap(int(s.aspect) + dir, 3));
            // a window or mode of the old shape: the biggest of the new one
            if (s.width && s.height && !Shaped(s.width, s.height, s.aspect)) {
                auto m = ModesFor(s.aspect);
                if (!m.empty()) s.width = m.front().first, s.height = m.front().second;
            }
            if (StartMenuOpen()) SetBootAspect(s.aspect);
            display = true;
            break;
        case kResolution: {  // "Desktop" (0 x 0) then the sizes of the aspect ratio
            auto m = ModesFor(s.aspect);
            m.insert(m.begin(), {0, 0});
            int c = 0;
            for (int k = 0; k < int(m.size()); k++)
                if (m[size_t(k)].first == s.width && m[size_t(k)].second == s.height) c = k;
            c = Wrap(c + dir, int(m.size()));
            s.width = m[size_t(c)].first, s.height = m[size_t(c)].second;
            display = true;
            break;
        }
        default:
            if (d.flag) {
                s.*d.flag = !(s.*d.flag);
            } else if (d.choices) {
                int k = ChoiceIndex(d, s.*d.value);
                s.*d.value = d.choices[Wrap(k < 0 ? 0 : k + dir, d.count)];
            } else {
                s.*d.value = StepRange(d, s.*d.value, dir);
            }
    }
    Save();
    if (display) ApplyDisplay();
}

// a row back to a new hpcos.toml's value; the aspect ratio, read at boot, stays
void DefaultOf(Settings& s, const Row& r) {
    const Settings d;
    if (r.kind == Kind::Keys) {
        std::copy(std::begin(d.keys), std::end(d.keys), std::begin(s.keys));
        return;
    }
    if (r.kind == Kind::PadMap) {
        std::copy(std::begin(d.pad_map), std::end(d.pad_map), std::begin(s.pad_map));
        return;
    }
    if (r.kind != Kind::Value) return;
    const Def& def = kDefs[r.id];
    if (r.id == kDisplayMode) s.display_mode = d.display_mode;
    else if (r.id == kResolution) s.width = d.width, s.height = d.height;
    else if (def.flag) s.*def.flag = d.*def.flag;
    else if (def.value) s.*def.value = d.*def.value;
}

}  // namespace

namespace rows {

const Row* Page(int page, size_t* count) {
    if (page < 0 || page >= kPages) return *count = 0, nullptr;
    *count = kPageDefs[page].count;
    return kPageDefs[page].rows;
}

const char* PageName(int page) { return page >= 0 && page < kPages ? kPageDefs[page].name : ""; }

std::string Value(const Row& row) {
    std::lock_guard lock(UiLock());
    return row.kind == Kind::Value ? ValueOf(Get(), Id(row.id)) : std::string();
}

void Step(const Row& row, int step) {
    std::lock_guard lock(UiLock());
    if (row.kind == Kind::Value && step) StepOf(Get(), Id(row.id), step < 0 ? -1 : 1);
}

void Defaults(int page) {
    std::lock_guard lock(UiLock());
    size_t n = 0;
    const Row* list = Page(page, &n);
    for (size_t i = 0; i < n; i++) DefaultOf(Get(), list[i]);
    Save();
    ApplyDisplay();
}

bool Shown(const Row& row) {
    std::lock_guard lock(UiLock());
    const Settings& s = Get();
    switch (row.id) {
        case kRenderScale: case kMaxHeight: return !s.internal_height;  // the window's height sets the render's
        default: return true;
    }
}

bool Enabled(const Row& row) {
    std::lock_guard lock(UiLock());
    const Settings& s = Get();
    if (row.kind == Kind::Keys) return s.kbm;
    switch (row.id) {
        case kBloomStrength: return s.bloom;
        case kVibrationStrength: return s.vibration;
        case kColorBlindStrength: return s.colorblind != kCbOff;
        default: return true;
    }
}

const char* Note(const Row& row) {
    std::lock_guard lock(UiLock());
    const Settings& s = Get();
    switch (row.id) {
        case kResolution:
            return s.display_mode == DisplayMode::Borderless
                       ? "Borderless covers the monitor; the size is for windowed / fullscreen." : nullptr;
        case kFov: return "Experimental.";
        case kDrawAll: return "No doorway culling: objects a room's doorway view leaves out are drawn when in view.";
        case kBloom: return "A glow round bright lights, not the HUD.";
        case kSpeakers:
            return "Takes effect the next time the game starts. 5.1 needs Windows' speaker setup set to 5.1\n"
                   "(or a 5.1 HDMI receiver).";
        case kController:
            return s.controller ? nullptr : "The game ignores the controller; keyboard and mouse still play.";
        case kMoveMode: return "Mouse wheel in game:  down  Slow walk  <  Walk  <  Run  up";
        case kCutsceneSkip:
            return "Hold Start or Back: runs it to its own end, muted and with the picture held, so nothing it sets\n"
                   "up is missed.";
        case kBeanVoice: return "Harry's line each time he picks up beans. Off keeps the pickup sound only.";
        case kIntroSkip: return "The EA and Warner Bros. logo movies; takes effect at the next start.";
        case kColorBlind: return "Shifts the colours a colour-blind player confuses toward ones they can tell apart.";
        case kExploreGrounds:
            return "On: walk the whole grounds, every area shown. Off: a path's end fades out to its other end, as\n"
                   "the game does. While on, the story's cutscenes and lessons on the grounds play normally; flying\n"
                   "practice can only be repeated later by disabling this option.";
        case kBroomLandFlat:
            return "Experimental. On: over a flat roof or a wall's top, land as in a landing zone. Off: only the\n"
                   "game's landing zones.";
        case kDebugKeys: return "F7 depth test always passes, F8 log depth for all draws, F9 alpha test off";
        case kAudioTrace: return "Logs every sound started and every DirectSound call; on only while reproducing.";
        default: return nullptr;
    }
}

}  // namespace rows

}  // namespace app

// the game's own Settings pages (hle_frontend.c): 0 Gameplay (with the Enhancements rows marked for it), 1 Graphics,
// 2 Controls, each the rows of those F10 tabs marked for the game
namespace {
const app::rows::Row* GameRow(int page, int i) {
    using namespace app::rows;
    static const int kTabs[][2] = {{kGameplay, kEnhancements}, {kGraphics, -1}, {kControls, -1}};
    if (page < 0 || page >= int(std::size(kTabs))) return nullptr;
    for (int tab : kTabs[page]) {
        size_t n = 0;
        const Row* list = Page(tab, &n);
        for (size_t k = 0; k < n; k++)
            if (list[k].game && i-- == 0) return &list[k];
    }
    return nullptr;
}
}  // namespace

extern "C" int app_PageOptionCount(int page) {
    int n = 0;
    while (GameRow(page, n)) n++;
    return n;
}
extern "C" const char* app_PageOptionLabel(int page, int i) {
    const app::rows::Row* r = GameRow(page, i);
    return r ? r->label : "";
}
extern "C" const char* app_PageOptionValue(int page, int i) {
    static char buf[48];
    const app::rows::Row* r = GameRow(page, i);
    std::snprintf(buf, sizeof buf, "%s", r ? app::rows::Value(*r).c_str() : "");
    return buf;
}
extern "C" void app_PageOptionStep(int page, int i, int dir) {
    if (const app::rows::Row* r = GameRow(page, i)) app::rows::Step(*r, dir);
}
