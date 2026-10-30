// Host settings and window for the ported renderer. See app.h.
//
// hpcos.toml beside the exe holds the settings, one `key = value` line each, as
// the original's settings file; it is written with every key on first run and by the F10
// panel, and re-read while the game runs when it changes on disk. A RECOMP_*
// variable in the environment at launch overrides its line for that run
// (run_win.sh, debugging) and is not written back.
#include "app/app.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "core/log.h"

extern "C" uint32_t hpcos_FovPercent(void) { return std::clamp<uint32_t>(app::Get().fov, 80, 130); }
extern "C" void* xbox_FramebufferWindowHwnd(void);
extern "C" void xbox_FramebufferWindowStart(void);
extern "C" void xbox_FramebufferWindowSetDisplay(int mode, int w, int h);

namespace app {

namespace {

enum class Kind { U32, Bool, Display, Aspect, Key, Pad, ColorBlind, Fps };

struct Entry {
    const char* key;  // in hpcos.toml
    const char* env;  // RECOMP_* that overrides it, or null
    Kind kind;
    size_t off;       // into Settings
    uint32_t lo, hi;  // U32 range
    const char* note; // the comment line above it in the file
};

#define FIELD(m) offsetof(Settings, m)
const Entry kEntries[] = {
    // xboxrecomp: first in the file, where a player looks for how to get the launcher's page back
    {"show_launcher", nullptr, Kind::Bool, FIELD(show_launcher), 0, 0,
     "the launcher's settings page at startup: true shows it, false starts the game at once (also: hold Shift while "
     "starting, or F10 > Show the launcher at startup)"},
    {"display_mode", "RECOMP_DISPLAY_MODE", Kind::Display, FIELD(display_mode), 0, 0,
     "'windowed', 'borderless' (covers the monitor) or 'fullscreen' (switches the monitor to width x height)"},
    {"width", nullptr, Kind::U32, FIELD(width), 0, 16384,
     "window size, or the fullscreen mode; 0 0 is the desktop's (RECOMP_WINDOW_SIZE=WxH overrides both)"},
    {"height", nullptr, Kind::U32, FIELD(height), 0, 16384, nullptr},
    {"aspect", "RECOMP_ASPECT", Kind::Aspect, FIELD(aspect), 0, 0,
     "'4:3' (black bars), '16:9' (the game's widescreen camera) or 'stretch'; takes a restart"},
    {"internal_height", "RECOMP_INTERNAL_HEIGHT", Kind::U32, FIELD(internal_height), 0, 8640,
     "internal resolution: lines the 3D is drawn at (480 is the Xbox's, 960 2x, 1440 3x, 2160 4.5x); 0 follows the window"},
    {"render_percent", "RECOMP_RENDER_PERCENT", Kind::U32, FIELD(render_percent), 25, 400,
     "with internal_height 0: the render height as % of the window's"},
    {"render_max_height", "RECOMP_RENDER_MAX_HEIGHT", Kind::U32, FIELD(render_max_height), 0, 8640,
     "with internal_height 0: the most lines it follows the window to, 0 no limit"},
    {"msaa", "RECOMP_MSAA", Kind::U32, FIELD(msaa), 0, 8, "multisample anti-aliasing: 0 off, 2, 4 or 8 samples"},
    {"smaa", "RECOMP_SMAA", Kind::Bool, FIELD(smaa), 0, 0,
     "SMAA anti-aliasing over the finished frame; combines with msaa"},
    {"anisotropic_filtering", "RECOMP_ANISO", Kind::U32, FIELD(aniso), 0, 16, "0 as the game asks, 2, 4, 8 or 16"},
    {"brightness", nullptr, Kind::U32, FIELD(brightness), 0, 100, "0 to 100, 50 leaves the picture alone"},
    {"contrast", nullptr, Kind::U32, FIELD(contrast), 0, 100, nullptr},
    {"vsync", "RECOMP_VSYNC", Kind::Bool, FIELD(vsync), 0, 0, nullptr},
    {"fps_counter", nullptr, Kind::Bool, FIELD(fps_counter), 0, 0, nullptr},
    {"bloom", "RECOMP_BLOOM", Kind::Bool, FIELD(bloom), 0, 0,
     "a glow round bright parts of the 3D scene (not the HUD)"},
    {"bloom_strength", nullptr, Kind::U32, FIELD(bloom_strength), 0, 100, nullptr},
    {"sharpen", "RECOMP_SHARPEN", Kind::U32, FIELD(sharpen), 0, 100, "contrast adaptive sharpening, 0 off"},
    {"saturation", nullptr, Kind::U32, FIELD(saturation), 0, 200, "colour grading: saturation %, 100 as the game"},
    {"vibrance", nullptr, Kind::U32, FIELD(vibrance), 0, 100, "colour grading: dull colours up more than vivid ones, 0 off"},
    {"warmth", nullptr, Kind::U32, FIELD(warmth), 0, 100, "colour grading: 0 cool, 50 as the game, 100 warm"},
    {"vignette", nullptr, Kind::U32, FIELD(vignette), 0, 100, "the picture's corners darkened, 0 off"},
    {"grain", nullptr, Kind::U32, FIELD(grain), 0, 100, "film grain, 0 off"},
    {"fov", "RECOMP_FOV", Kind::U32, FIELD(fov), 80, 130, "field of view, % of the game's"},
    {"controller", nullptr, Kind::Bool, FIELD(controller), 0, 0,
     "false: the game ignores the PC's controller (keyboard and mouse still play)"},
    {"async_shaders", "RECOMP_ASYNC_SHADERS", Kind::Bool, FIELD(async_shaders), 0, 0,
     "a draw whose pipeline is still compiling is skipped instead of waited for"},
    {"start_menu", "RECOMP_START_MENU", Kind::Bool, FIELD(start_menu), 0, 0,
     "the settings page before the game starts (aspect ratio, resolution, quality)"},
    {"fps_limit", "RECOMP_FPS_LIMIT", Kind::Fps, FIELD(fps_limit), 0, 0,
     "frame rate: 0, the game's own (59.94; it advances a fixed step a frame, so no other rate)"},
    {"debug_keys", nullptr, Kind::Bool, FIELD(debug_keys), 0, 0,
     "renderer debug keys: F7 depth test always passes, F8 log depth for all draws, F9 alpha test off"},
    {"grounds_warps", "RECOMP_GROUNDS_WARPS", Kind::Bool, FIELD(grounds_warps), 0, 0,
     "the Hogwarts grounds' path warps (walking onto a path's end fades out and puts Harry at its other end, and only that area of the grounds is shown); false: F10's Explore Hogwarts Grounds, he walks on and every area is shown; the story's cutscenes and lessons on the grounds play normally, and flying practice can only be repeated later by turning it off"},
    {"broom_land_flat", "RECOMP_BROOM_LAND_FLAT", Kind::Bool, FIELD(broom_land_flat), 0, 0,
     "the broom lands on any flat surface up to 3 under Harry (roofs, walls' tops), not only in the game's landing zones"},
    {"draw_all_objects", "RECOMP_DRAW_ALL", Kind::Bool, FIELD(draw_all), 0, 0,
     "everything in the camera's view drawn (the field of view slider's width), not only what the doorways show; false: culled as the game does"},
    {"intro_skip", "RECOMP_INTRO_SKIP", Kind::Bool, FIELD(intro_skip), 0, 0,
     "the EA and Warner Bros. logo movies at start-up are left out; false as the Xbox"},
    {"cutscene_skip", "RECOMP_CUTSCENE_SKIP", Kind::Bool, FIELD(cutscene_skip), 0, 0,
     "holding Start or Back for half a second fast-forwards a cutscene to its own end (muted, the picture held); false as the Xbox"},
    {"speakers", "RECOMP_SPEAKERS", Kind::U32, FIELD(speakers), 2, 6,
     "2 stereo, 6 5.1 surround (the console's speaker mix, each channel to its own speaker); takes a restart"},
    {"colorblind", nullptr, Kind::ColorBlind, FIELD(colorblind), 0, 0,
     "colour-blind correction over the finished frame: 'off', 'protanopia', 'deuteranopia' or 'tritanopia'"},
    {"colorblind_strength", nullptr, Kind::U32, FIELD(colorblind_strength), 0, 100, "0 to 100 %"},
    {"keyboard_mouse", nullptr, Kind::Bool, FIELD(kbm), 0, 0,
     "keyboard and mouse play as controller 1, alongside a pad (key names below; Mouse1-5 the mouse buttons)"},
    {"mouse_sensitivity", nullptr, Kind::U32, FIELD(mouse_sensitivity), 1, 100, "1 to 100, the camera's speed with the mouse"},
    {"mouse_invert_y", nullptr, Kind::Bool, FIELD(mouse_invert_y), 0, 0, nullptr},
    {"move_mode", nullptr, Kind::U32, FIELD(move_mode), 0, 2,
     "the movement keys' pace: 0 slow walk, 1 walk, 2 run; the mouse wheel steps it"},
    {"deadzone_left", nullptr, Kind::U32, FIELD(deadzone_left), 0, 50,
     "controller: % of full tilt read as centred, the stick's response past it in %, the camera's axes inverted"},
    {"deadzone_right", nullptr, Kind::U32, FIELD(deadzone_right), 0, 50, nullptr},
    {"sensitivity_left", nullptr, Kind::U32, FIELD(sensitivity_left), 25, 200, nullptr},
    {"sensitivity_right", nullptr, Kind::U32, FIELD(sensitivity_right), 25, 200, nullptr},
    {"invert_camera_x", nullptr, Kind::Bool, FIELD(invert_x), 0, 0, nullptr},
    {"invert_camera_y", nullptr, Kind::Bool, FIELD(invert_y), 0, 0, nullptr},
    {"vibration", nullptr, Kind::Bool, FIELD(vibration), 0, 0, nullptr},
    {"vibration_strength", nullptr, Kind::U32, FIELD(vibration_strength), 0, 100, "%"},
    {"pad_a", nullptr, Kind::Pad, FIELD(pad_map[0]), 0, 0, "controller remapping: the pad button each game button reads (A B X Y Black White LT RT Up Down Left Right Start Back LThumb RThumb; on a 360/One pad LB is White, RB Black)"},
    {"pad_b", nullptr, Kind::Pad, FIELD(pad_map[1]), 0, 0, nullptr},
    {"pad_x", nullptr, Kind::Pad, FIELD(pad_map[2]), 0, 0, nullptr},
    {"pad_y", nullptr, Kind::Pad, FIELD(pad_map[3]), 0, 0, nullptr},
    {"pad_black", nullptr, Kind::Pad, FIELD(pad_map[4]), 0, 0, nullptr},
    {"pad_white", nullptr, Kind::Pad, FIELD(pad_map[5]), 0, 0, nullptr},
    {"pad_lt", nullptr, Kind::Pad, FIELD(pad_map[6]), 0, 0, nullptr},
    {"pad_rt", nullptr, Kind::Pad, FIELD(pad_map[7]), 0, 0, nullptr},
    {"pad_up", nullptr, Kind::Pad, FIELD(pad_map[8]), 0, 0, nullptr},
    {"pad_down", nullptr, Kind::Pad, FIELD(pad_map[9]), 0, 0, nullptr},
    {"pad_left", nullptr, Kind::Pad, FIELD(pad_map[10]), 0, 0, nullptr},
    {"pad_right", nullptr, Kind::Pad, FIELD(pad_map[11]), 0, 0, nullptr},
    {"pad_start", nullptr, Kind::Pad, FIELD(pad_map[12]), 0, 0, nullptr},
    {"pad_back", nullptr, Kind::Pad, FIELD(pad_map[13]), 0, 0, nullptr},
    {"pad_lthumb", nullptr, Kind::Pad, FIELD(pad_map[14]), 0, 0, nullptr},
    {"pad_rthumb", nullptr, Kind::Pad, FIELD(pad_map[15]), 0, 0, nullptr},
    {"key_a", nullptr, Kind::Key, FIELD(keys[0]), 0, 0, "keyboard and mouse bindings: a key name (A-Z, 0-9, F1-F12, Space, Enter, Escape, Tab, Shift, Ctrl, Alt, Up, Down, Left, Right, Num0-Num9, ...), Mouse1-Mouse5 or none"},
    {"key_b", nullptr, Kind::Key, FIELD(keys[1]), 0, 0, nullptr},
    {"key_x", nullptr, Kind::Key, FIELD(keys[2]), 0, 0, nullptr},
    {"key_y", nullptr, Kind::Key, FIELD(keys[3]), 0, 0, nullptr},
    {"key_black", nullptr, Kind::Key, FIELD(keys[4]), 0, 0, nullptr},
    {"key_white", nullptr, Kind::Key, FIELD(keys[5]), 0, 0, nullptr},
    {"key_lt", nullptr, Kind::Key, FIELD(keys[6]), 0, 0, nullptr},
    {"key_rt", nullptr, Kind::Key, FIELD(keys[7]), 0, 0, nullptr},
    {"key_up", nullptr, Kind::Key, FIELD(keys[8]), 0, 0, nullptr},
    {"key_down", nullptr, Kind::Key, FIELD(keys[9]), 0, 0, nullptr},
    {"key_left", nullptr, Kind::Key, FIELD(keys[10]), 0, 0, nullptr},
    {"key_right", nullptr, Kind::Key, FIELD(keys[11]), 0, 0, nullptr},
    {"key_start", nullptr, Kind::Key, FIELD(keys[12]), 0, 0, nullptr},
    {"key_back", nullptr, Kind::Key, FIELD(keys[13]), 0, 0, nullptr},
    {"key_lthumb", nullptr, Kind::Key, FIELD(keys[14]), 0, 0, nullptr},
    {"key_rthumb", nullptr, Kind::Key, FIELD(keys[15]), 0, 0, nullptr},
    {"key_move_forward", nullptr, Kind::Key, FIELD(keys[16]), 0, 0, nullptr},
    {"key_move_back", nullptr, Kind::Key, FIELD(keys[17]), 0, 0, nullptr},
    {"key_move_left", nullptr, Kind::Key, FIELD(keys[18]), 0, 0, nullptr},
    {"key_move_right", nullptr, Kind::Key, FIELD(keys[19]), 0, 0, nullptr},
    {"key_camera_up", nullptr, Kind::Key, FIELD(keys[20]), 0, 0, nullptr},
    {"key_camera_down", nullptr, Kind::Key, FIELD(keys[21]), 0, 0, nullptr},
    {"key_camera_left", nullptr, Kind::Key, FIELD(keys[22]), 0, 0, nullptr},
    {"key_camera_right", nullptr, Kind::Key, FIELD(keys[23]), 0, 0, nullptr},
};
#undef FIELD

constexpr const char* kDisplayNames[] = {"windowed", "borderless", "fullscreen"};
constexpr const char* kAspectNames[] = {"4:3", "16:9", "stretch"};
constexpr const char* kColorBlindNames[] = {"off", "protanopia", "deuteranopia", "tritanopia"};

uint32_t& U(Settings& s, const Entry& e) { return *reinterpret_cast<uint32_t*>(reinterpret_cast<char*>(&s) + e.off); }
bool& B(Settings& s, const Entry& e) { return *reinterpret_cast<bool*>(reinterpret_cast<char*>(&s) + e.off); }

std::string Trim(std::string v) {
    size_t a = v.find_first_not_of(" \t\r"), b = v.find_last_not_of(" \t\r");
    return a == std::string::npos ? std::string() : v.substr(a, b - a + 1);
}

// one value as the file or the environment spells it; false leaves the setting
bool Parse(Settings& s, const Entry& e, std::string v) {
    v = Trim(v);
    if (v.size() >= 2 && (v[0] == '\'' || v[0] == '"') && v.back() == v[0]) v = v.substr(1, v.size() - 2);
    switch (e.kind) {
        case Kind::U32: {
            char* end = nullptr;
            unsigned long n = std::strtoul(v.c_str(), &end, 0);
            if (end == v.c_str()) return false;
            U(s, e) = std::clamp<uint32_t>(uint32_t(n), e.lo, e.hi);
            return true;
        }
        case Kind::Bool:
            if (v == "true" || v == "1") B(s, e) = true;
            else if (v == "false" || v == "0") B(s, e) = false;
            else return false;
            return true;
        case Kind::Display:
            for (uint32_t i = 0; i < 3; i++) {
                if (!_stricmp(v.c_str(), kDisplayNames[i])) return U(s, e) = i, true;
            }
            return false;
        case Kind::Aspect:
            for (uint32_t i = 0; i < 3; i++) {
                if (!_stricmp(v.c_str(), kAspectNames[i])) return U(s, e) = i, true;
            }
            return false;
        case Kind::Fps:
            if (!_stricmp(v.c_str(), "unlocked")) return U(s, e) = 0, true;  // the old experimental mode, removed
            if (v == "0" || v == "30") return U(s, e) = 0, true;  // 30 fps cap removed: an old '30' reads as the game's rate
            return false;
        case Kind::ColorBlind:
            for (uint32_t i = 0; i < 4; i++) {
                if (!_stricmp(v.c_str(), kColorBlindNames[i])) return U(s, e) = i, true;
            }
            return false;
        case Kind::Pad:
            for (uint32_t i = 0; i < kPadInputs; i++) {
                if (!_stricmp(v.c_str(), kPadInputNames[i])) return U(s, e) = i, true;
            }
            return false;
        case Kind::Key: {
            int vk = KeyFromName(v.c_str());
            if (vk < 0) return false;
            U(s, e) = uint32_t(vk);
            return true;
        }
    }
    return false;
}

std::string Format(Settings& s, const Entry& e) {
    switch (e.kind) {
        case Kind::U32: return std::to_string(U(s, e));
        case Kind::Bool: return B(s, e) ? "true" : "false";
        case Kind::Display: return std::string("'") + kDisplayNames[std::min(U(s, e), 2u)] + "'";
        case Kind::Aspect: return std::string("'") + kAspectNames[std::min(U(s, e), 2u)] + "'";
        case Kind::Fps: return std::to_string(U(s, e));
        case Kind::ColorBlind: return std::string("'") + kColorBlindNames[std::min(U(s, e), 3u)] + "'";
        case Kind::Pad: return std::string("'") + kPadInputNames[std::min(U(s, e), kPadInputs - 1)] + "'";
        case Kind::Key: return std::string("'") + KeyName(U(s, e)) + "'";
    }
    return {};
}

struct State {
    std::mutex lock;
    Settings cur;          // what the game uses
    Settings file;         // what hpcos.toml holds
    bool locked[std::size(kEntries)] = {};  // the environment set it at launch
    bool size_locked = false;               // RECOMP_WINDOW_SIZE
    AspectMode boot_aspect = AspectMode::Standard;
    std::filesystem::file_time_type stamp{};
    ULONGLONG checked = 0;
    bool ready = false;
};

State& St() {
    static State st;
    return st;
}

std::filesystem::path FilePath() { return ExeDir() / "hpcos.toml"; }

// false when there is no file
bool ReadFile(Settings& s) {
    std::ifstream f(FilePath());
    if (!f) return false;
    std::string line;
    while (std::getline(f, line)) {
        if (size_t c = line.find('#'); c != std::string::npos && line.find_first_of("'\"") > c) line.resize(c);
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = Trim(line.substr(0, eq));
        if (key == "broom_free_flight") key = "broom_land_flat";  // the fork's own broom option, now upstream's (2026-10-30)
        for (const Entry& e : kEntries) {
            if (key == e.key && !Parse(s, e, line.substr(eq + 1)))
                LOGW("hpcos.toml: %s = %s not understood, kept %s", e.key, Trim(line.substr(eq + 1)).c_str(),
                     Format(s, e).c_str());
        }
    }
    return true;
}

void WriteFile(Settings& s) {
    std::ostringstream o;
    o << "# Harry Potter and the Chamber of Secrets (xboxrecomp) settings.\n"
         "# F10 in game changes them too. Saved while the game runs, this file applies at once,\n"
         "# except aspect. A RECOMP_* environment variable of the same setting wins over its line.\n";
    for (const Entry& e : kEntries) {
        if (e.note) o << "\n# " << e.note << "\n";
        o << e.key << " = " << Format(s, e) << "\n";
    }
    std::error_code ec;
    std::filesystem::path p = FilePath(), tmp = p;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::trunc);
        f << o.str();
        if (!f) {
            LOGW("hpcos.toml: can't write %s", p.string().c_str());
            return;
        }
    }
    std::filesystem::rename(tmp, p, ec);
    if (ec) LOGW("hpcos.toml: can't write %s: %s", p.string().c_str(), ec.message().c_str());
}

// file values into cur, but not over what the environment set
void Merge(State& st) {
    for (size_t i = 0; i < std::size(kEntries); i++) {
        const Entry& e = kEntries[i];
        if (st.locked[i] || (st.size_locked && (!std::strcmp(e.key, "width") || !std::strcmp(e.key, "height")))) continue;
        if (e.kind == Kind::Bool) B(st.cur, e) = B(st.file, e);
        else U(st.cur, e) = U(st.file, e);  // aspect too, for the next boot: Aspect() keeps the boot value
    }
}

void Init(State& st) {
    if (st.ready) return;
    st.ready = true;
    bool had = ReadFile(st.file);
    st.cur = st.file;
    for (size_t i = 0; i < std::size(kEntries); i++) {
        const Entry& e = kEntries[i];
        const char* v = e.env ? std::getenv(e.env) : nullptr;
        if (v && *v && Parse(st.cur, e, v)) st.locked[i] = true;
    }
    if (const char* sz = std::getenv("RECOMP_WINDOW_SIZE")) {
        unsigned w = 0, h = 0;
        if (std::sscanf(sz, "%ux%u", &w, &h) == 2) {
            st.cur.width = w;
            st.cur.height = h;
            st.size_locked = true;
        }
    }
    st.boot_aspect = st.cur.aspect;
    if (!had) WriteFile(st.file);
    std::error_code ec;
    st.stamp = std::filesystem::last_write_time(FilePath(), ec);
    LOGI("settings: %s%s", FilePath().string().c_str(), had ? "" : " (new, defaults)");
}

void SetEnvDefault(const char* name, const std::string& v) {
    _putenv_s(name, v.c_str());
}

// the size the window opens at: 0 0 is the desktop in borderless / fullscreen, half of it windowed
void WindowSize(const Settings& s, uint32_t& w, uint32_t& h) {
    w = s.width;
    h = s.height;
    if (w && h) return;
    w = GetSystemMetrics(SM_CXSCREEN);
    h = GetSystemMetrics(SM_CYSCREEN);
    if (s.display_mode == DisplayMode::Windowed) {
        w = w * 3 / 4;
        h = h * 3 / 4;
    }
}

}  // namespace

Settings& Get() {
    State& st = St();
    if (!st.ready) {
        std::lock_guard lock(st.lock);
        Init(st);
    }
    return st.cur;
}

void Save() {
    State& st = St();
    std::lock_guard lock(st.lock);
    Init(st);
    // a setting the environment holds keeps the file's own value
    Settings out = st.file;
    for (size_t i = 0; i < std::size(kEntries); i++) {
        const Entry& e = kEntries[i];
        if (st.locked[i] || (st.size_locked && (!std::strcmp(e.key, "width") || !std::strcmp(e.key, "height")))) continue;
        if (e.kind == Kind::Bool) B(out, e) = B(st.cur, e);
        else U(out, e) = U(st.cur, e);
    }
    st.file = out;
    WriteFile(out);
    std::error_code ec;
    st.stamp = std::filesystem::last_write_time(FilePath(), ec);
}

void Poll() {
    State& st = St();
    ULONGLONG now = GetTickCount64();
    if (now - st.checked < 500) return;
    st.checked = now;
    std::error_code ec;
    auto t = std::filesystem::last_write_time(FilePath(), ec);
    if (ec || t == st.stamp) return;
    bool display;
    {
        std::lock_guard lock(st.lock);
        st.stamp = t;
        Settings before = st.cur;
        if (!ReadFile(st.file)) return;
        Merge(st);
        display = before.display_mode != st.cur.display_mode || before.width != st.cur.width ||
                  before.height != st.cur.height;
    }
    LOGI("settings: hpcos.toml changed, applied");
    if (display) ApplyDisplay();
}

void ApplyDisplay() {
    const Settings& s = Get();
    uint32_t w, h;
    WindowSize(s, w, h);
    if (s.display_mode == DisplayMode::Fullscreen && !(s.width && s.height)) w = h = 0;  // the desktop mode
    xbox_FramebufferWindowSetDisplay(int(s.display_mode), int(w), int(h));
}

void* Window() {
    // the window thread opens it. The XDK's device init used to start that thread
    // (via the NV2A/AvSetDisplayMode paths); CreateDevice is HLE now, so start it here.
    xbox_FramebufferWindowStart();
    for (int i = 0; i < 500 && !xbox_FramebufferWindowHwnd(); i++) Sleep(10);
    return xbox_FramebufferWindowHwnd();
}

float RenderScale(uint32_t guest_height) {
    const Settings& s = Get();
    float lines;
    if (s.internal_height) {
        lines = float(s.internal_height);
    } else {
        RECT rc{};
        GetClientRect(static_cast<HWND>(Window()), &rc);
        uint32_t h = std::max<LONG>(rc.bottom - rc.top, 1);
        lines = float(h) * s.render_percent / 100.0f;
        // a maximised window on a 4K screen would otherwise render ~2100 lines; past the cap the frame is upscaled
        if (s.render_max_height) lines = std::min(lines, float(s.render_max_height));
    }
    return std::max(1.0f, lines / float(std::max<uint32_t>(guest_height, 1)));
}


AspectMode BootAspect() {
    Get();
    return St().boot_aspect;
}

// xboxrecomp: chosen on the start menu before the title has asked anything that depends on it (its widescreen check
// reads RECOMP_ASPECT once, later)
void SetBootAspect(AspectMode a) {
    Get();
    St().boot_aspect = a;
    SetEnvDefault("RECOMP_ASPECT", kAspectNames[uint32_t(a)]);
}

float Aspect() {
    AspectMode a = BootAspect();
    // 16:9 is the title's widescreen camera (hle_video.c): exactly that, or a window that is not 16:9 distorts it
    if (a == AspectMode::Wide) return 16.0f / 9.0f;
    if (a == AspectMode::Stretch) {
        RECT rc{};
        GetClientRect(static_cast<HWND>(Window()), &rc);
        float w = float(std::max<LONG>(rc.right - rc.left, 1)), h = float(std::max<LONG>(rc.bottom - rc.top, 1));
        return std::max(4.0f / 3.0f, w / h);
    }
    return 4.0f / 3.0f;
}

std::filesystem::path ExeDir() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return std::filesystem::path(std::wstring(buf, n)).parent_path();
}

std::recursive_mutex& UiLock() {
    static std::recursive_mutex m;
    return m;
}

}  // namespace app

// hle_dsound.c: F10 Debug > Audio trace
extern "C" int app_AudioTrace(void) { return app::Get().audio_trace; }

// hle_dsound.c, when the mixer starts: output channels
extern "C" int app_Speakers(void) { return app::Get().speakers == 6 ? 6 : 2; }

// main.c, first thing: the settings read, and handed to the C side (the window, the title's widescreen camera, the
// pushbuffer path) as the RECOMP_* variables it reads, before any thread starts
extern "C" int app_DrawAll(void) { return app::Get().draw_all; }
extern "C" int app_CutsceneSkip(void) { return app::Get().cutscene_skip; }
extern "C" int app_IntroSkip(void) { return app::Get().intro_skip; }
extern "C" int app_GroundsWarps(void) { return app::Get().grounds_warps; }
extern "C" int app_BroomLandFlat(void) { return app::Get().broom_land_flat; }

extern "C" void hpcos_SettingsInit(void) {
    const app::Settings& s = app::Get();
    uint32_t w, h;
    app::WindowSize(s, w, h);
    app::SetEnvDefault("RECOMP_WINDOW_SIZE", std::to_string(w) + "x" + std::to_string(h));
    app::SetEnvDefault("RECOMP_DISPLAY_MODE", app::kDisplayNames[uint32_t(s.display_mode)]);
    app::SetEnvDefault("RECOMP_ASPECT", app::kAspectNames[uint32_t(s.aspect)]);
}

// hle_video.c: the gameplay cameras' field of view

// xboxrecomp: the in-game settings pages (hle_frontend.c draws them in the game's own menus): Gameplay, Graphics and
// Controls, the same settings the F10 panel changes, stepped left / right and saved to hpcos.toml as F10 does
namespace {
struct InGameOption {
    const char* label;
    bool app::Settings::*flag = nullptr;      // a toggle, or
    uint32_t app::Settings::*value = nullptr; // a number: a list of choices, or a range
    const uint32_t* choices = nullptr;
    const char* const* names = nullptr;
    int count = 0;
    int lo = 0, hi = 0, step = 0;            // a range (choices null)
    const char* unit = "";
    int special = 0;                          // 1 display mode, 2 resolution, 3 aspect
};
constexpr uint32_t kMsaa[] = {0, 2, 4, 8};
const char* const kMsaaNames[] = {"Off", "2x", "4x", "8x"};
constexpr uint32_t kAniso[] = {0, 2, 4, 8, 16};
const char* const kAnisoNames[] = {"Game's", "2x", "4x", "8x", "16x"};
const char* const kDisplayNames[] = {"Windowed", "Borderless", "Fullscreen"};
const char* const kAspectNames[] = {"4:3", "16:9", "Stretch"};
const char* const kMoveNames[] = {"Slow walk", "Walk", "Run"};
constexpr uint32_t kMove[] = {0, 1, 2};

const InGameOption kGameplay[] = {
    {"Cutscene fast-forward", &app::Settings::cutscene_skip},
    {"Skip start-up logos", &app::Settings::intro_skip},
    {"Broom lands anywhere flat", &app::Settings::broom_land_flat},
};
const InGameOption kGraphics[] = {
    {"Display", nullptr, nullptr, nullptr, kDisplayNames, 3, 0, 0, 0, "", 1},
    {"Resolution", nullptr, nullptr, nullptr, nullptr, 0, 0, 0, 0, "", 2},
    {"Render scale", nullptr, &app::Settings::render_percent, nullptr, nullptr, 0, 20, 200, 10, "%"},
    {"Vsync", &app::Settings::vsync},
    {"Aspect (next start)", nullptr, nullptr, nullptr, kAspectNames, 3, 0, 0, 0, "", 3},
    {"Field of view", nullptr, &app::Settings::fov, nullptr, nullptr, 0, 80, 130, 5, "%"},
    {"MSAA", nullptr, &app::Settings::msaa, kMsaa, kMsaaNames, 4},
    {"SMAA", &app::Settings::smaa},
    {"Anisotropic filtering", nullptr, &app::Settings::aniso, kAniso, kAnisoNames, 5},
    {"Brightness", nullptr, &app::Settings::brightness, nullptr, nullptr, 0, 0, 100, 5},
    {"Bloom", &app::Settings::bloom},
    {"Sharpen", nullptr, &app::Settings::sharpen, nullptr, nullptr, 0, 0, 100, 10},
    {"FPS counter", &app::Settings::fps_counter},
};
const InGameOption kControls[] = {
    {"Vibration", &app::Settings::vibration},
    {"Vibration strength", nullptr, &app::Settings::vibration_strength, nullptr, nullptr, 0, 0, 100, 10, "%"},
    {"Invert camera X", &app::Settings::invert_x},
    {"Invert camera Y", &app::Settings::invert_y},
    {"Left stick deadzone", nullptr, &app::Settings::deadzone_left, nullptr, nullptr, 0, 0, 50, 5, "%"},
    {"Right stick deadzone", nullptr, &app::Settings::deadzone_right, nullptr, nullptr, 0, 0, 50, 5, "%"},
    {"Camera speed", nullptr, &app::Settings::sensitivity_right, nullptr, nullptr, 0, 25, 200, 25, "%"},
    {"Keyboard and mouse", &app::Settings::kbm},
    {"Mouse sensitivity", nullptr, &app::Settings::mouse_sensitivity, nullptr, nullptr, 0, 5, 100, 5},
    {"Invert mouse Y", &app::Settings::mouse_invert_y},
    {"Movement keys pace", nullptr, &app::Settings::move_mode, kMove, kMoveNames, 3},
    {"Use controller", &app::Settings::controller},
};
struct PageDef { const InGameOption* opts; int n; };
const PageDef kPages[] = {{kGameplay, int(std::size(kGameplay))}, {kGraphics, int(std::size(kGraphics))},
                          {kControls, int(std::size(kControls))}};

// the monitor's own sizes, biggest first (the launcher's list): 0x0 "Desktop" first
std::vector<std::pair<uint32_t, uint32_t>>& Modes() {
    static std::vector<std::pair<uint32_t, uint32_t>> m;
    if (m.empty()) {
        m.emplace_back(0, 0);
        DEVMODEW dm{};
        dm.dmSize = sizeof dm;
        for (DWORD i = 0; EnumDisplaySettingsW(nullptr, i, &dm); i++) {
            if (dm.dmPelsWidth < 640) continue;
            std::pair<uint32_t, uint32_t> p(dm.dmPelsWidth, dm.dmPelsHeight);
            if (std::find(m.begin(), m.end(), p) == m.end()) m.push_back(p);
        }
        std::sort(m.begin() + 1, m.end(), [](auto a, auto b) { return a.first * a.second > b.first * b.second; });
    }
    return m;
}

const InGameOption* Opt(int page, int i) {
    if (page < 0 || page >= int(std::size(kPages)) || i < 0 || i >= kPages[page].n) return nullptr;
    return &kPages[page].opts[i];
}
}  // namespace

extern "C" int app_PageOptionCount(int page) {
    return page >= 0 && page < int(std::size(kPages)) ? kPages[page].n : 0;
}
extern "C" const char* app_PageOptionLabel(int page, int i) {
    const InGameOption* o = Opt(page, i);
    return o ? o->label : "";
}
extern "C" const char* app_PageOptionValue(int page, int i) {
    static char buf[48];
    const InGameOption* o = Opt(page, i);
    if (!o) return "";
    const app::Settings& s = app::Get();
    if (o->special == 1) return kDisplayNames[std::min<uint32_t>(uint32_t(s.display_mode), 2)];
    if (o->special == 3) return kAspectNames[std::min<uint32_t>(uint32_t(s.aspect), 2)];
    if (o->special == 2) {
        if (!s.width || !s.height) return "Desktop";
        std::snprintf(buf, sizeof buf, "%u x %u", unsigned(s.width), unsigned(s.height));
        return buf;
    }
    if (o->flag) return s.*(o->flag) ? "On" : "Off";
    uint32_t v = s.*(o->value);
    if (o->choices) {
        for (int k = 0; k < o->count; k++)
            if (o->choices[k] == v) return o->names[k];
        return o->names[0];
    }
    std::snprintf(buf, sizeof buf, "%u%s", unsigned(v), o->unit);
    return buf;
}
extern "C" void app_PageOptionStep(int page, int i, int dir) {
    const InGameOption* o = Opt(page, i);
    if (!o || !dir) return;
    app::Settings& s = app::Get();
    bool display = false;
    if (o->special == 1) {
        s.display_mode = app::DisplayMode((uint32_t(s.display_mode) + 3 + uint32_t(dir)) % 3);
        display = true;
    } else if (o->special == 3) {
        s.aspect = app::AspectMode((uint32_t(s.aspect) + 3 + uint32_t(dir)) % 3);
    } else if (o->special == 2) {
        auto& m = Modes();
        int c = 0;
        for (int k = 0; k < int(m.size()); k++)
            if (m[size_t(k)].first == s.width && m[size_t(k)].second == s.height) c = k;
        c = (c + dir + int(m.size())) % int(m.size());
        s.width = m[size_t(c)].first, s.height = m[size_t(c)].second;
        display = true;
    } else if (o->flag) {
        s.*(o->flag) = !(s.*(o->flag));
    } else if (o->choices) {
        int c = 0;
        for (int k = 0; k < o->count; k++)
            if (o->choices[k] == s.*(o->value)) c = k;
        s.*(o->value) = o->choices[(c + dir + o->count) % o->count];
    } else {
        int v = int(s.*(o->value)) + o->step * dir;
        s.*(o->value) = uint32_t(v < o->lo ? o->lo : v > o->hi ? o->hi : v);
    }
    app::Save();
    if (display) app::ApplyDisplay();
}
