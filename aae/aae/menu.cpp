#include "menu.h"
#include "controller_help.h"
#include "sys_window.h"
#include "aae_mame_driver.h"
#include "opengl_renderer.h"
#include "vector_fonts.h"
#include "gl_texturing.h"
#include "inptport.h"
#include "deftypes.h"
#include "osdepend.h"
#include "os_input.h"
#include "config.h"
#include "sys_str.h"   // aae_stricmp
#include "colordefs.h"
#include "mame_layout.h"      // Layout_ComputeGameAspect (GAME ASPECT menu item)
#ifdef _WIN32
#include "windows_util.h"   // Win32 dialog/monitor helpers
#else
// Linux halves, defined in system/window/linux/linux_main.cpp.
void WindowUtil_UpdateAspect(float gameAspect);
void allegro_message(const char* title, const char* message);
#endif     // WindowUtil_UpdateAspect
#include "sys_log.h"
#include "sys_input.h"        // RawInput_GetMouseCount/Name (multi-mouse menu)
#include "joystick.h"         // joystick_device_count/get_display_name (INPUT DEVICES)

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>
#include <functional>

#include <algorithm>

// Regression guard: this file must never see OpenGL headers. If this fires,
// a render header re-leaked glew.h — fix the header, not this guard.
#ifdef __glew_h__
#error "OpenGL headers leaked into a non-render translation unit"
#endif

void AAE_ApplyAudioVolumesFromConfig(int force);
void setup_ambient(int style);
void init_raster_overlay();
void setup_video_config();

// Pull in emulator_is_gui_active() so SaveConfigIfRequired and GetTitleText
// can detect when the menu is being used from the GUI frontend instead of
// during gameplay. In GUI mode all saves go to aae.ini (path 0).
#include "aae_emulator.h"

// ----------------------------------------------------------------------
// Artwork Availability Flag Definitions
// ----------------------------------------------------------------------
// Defaults to 0 (unavailable). Your texture/artwork loading code should
// set these after it attempts to load each art resource.
// See menu.h for full usage instructions.

int g_artworkAvailable = 0;
int g_overlayAvailable = 0;
int g_bezelAvailable   = 0;
int g_artcropAvailable = 0;  // automatically follows g_bezelAvailable in practice

// ----------------------------------------------------------------------
// Internal Configuration, Enums, and Data
// ----------------------------------------------------------------------

namespace {

    enum class MenuID : int {
        None = 0,
        Root = 100,
        GlobalKeys = 200,
        LocalKeys = 300,
        GlobalJoy = 400,
        LocalJoy = 500,
        Analog = 600,
        InputDevices = 650,
        DipSwitch = 700,
        Video = 800,
        Audio = 900,
        MonoMonitor = 1000,
        ColorMonitor = 1050,
        VectorMonitor = 1100
    };

    // Constants for Layout
    constexpr float MENU_X          = 225.0f;
    constexpr float MENU_LINE_HEIGHT = 28.0f;
    constexpr int   VISIBLE_ITEMS   = 16;
    constexpr float VALUE_X_OFFSET  = 350.0f;
    // Maximum width of the value column. Values longer than this (long HID
    // device names) marquee-scroll within the field instead of being
    // truncated, so every page keeps the standard left margin. Sized so
    // MENU_X + VALUE_X_OFFSET + VALUE_W_MAX + ARROW_EXTRA + PAD_RIGHT stays
    // inside the 1024-unit overlay.
    constexpr float VALUE_W_MAX     = 370.0f;

    // Visual Styling Constants
    constexpr float TITLE_Y      = 650.0f;
    constexpr float TITLE_GAP    = 20.0f;
    constexpr float FOOTER_GAP   = 25.0f;
    constexpr float POLL_GAP     = 14.0f;
    constexpr float PAD_TOP      = 25.0f;
    constexpr float PAD_BOTTOM   = 25.0f;
    constexpr float PAD_LEFT     = 30.0f;
    constexpr float PAD_RIGHT    = 30.0f;

    constexpr float FONT_SCALE   = 2.0f;
    constexpr float FOOTER_SCALE = 1.6f;
    constexpr float CHAR_PITCH   = 9.5f * FONT_SCALE;
    constexpr float FOOTER_PITCH = 9.5f * FOOTER_SCALE;

    // Extra width allowance for selected < and > arrows + padding
    constexpr float ARROW_EXTRA  = 2.0f * (9.5f * 2.0f) + 10.0f;

    // Gray color used for disabled (unavailable) menu items.
    // Defined here so it can be tuned without hunting through Draw().
    constexpr unsigned int RGB_DISABLED = MAKE_RGBA(100, 100, 100, 255);

    // Key Names Array (256 entries, 12 across)
    const char* key_names[256] = {
        "NULL","LBUTTON","RBUTTON","CANCEL","MBUTTON","XBUTTON1","XBUTTON2","UNDEF","BACKSPACE","TAB","LF","VT",
        "CLEAR","ENTER","UNDEF","UNDEF","SHIFT","CONTROL","MENU","PAUSE","CAPSLOCK","KANA","IME_ON","JUNJA",
        "CANCEL","KANJI","IME_OFF","ESC","CONVERT","NONCONVERT","ACCEPT","MODECHANGE","SPACE","PGUP","PGDN","END",
        "HOME","LEFT","UP","RIGHT","DOWN","SELECT","PRINT","EXECUTE","PRNTSCRN","INSERT","DEL","HELP",
        "0","1","2","3","4","5","6","7","8","9","UNDEF","UNDEF",
        "UNDEF","UNDEF","UNDEF","UNDEF","UNDEF","A","B","C","D","E","F","G",
        "H","I","J","K","L","M","N","O","P","Q","R","S",
        "T","U","V","W","X","Y","Z","LWIN","RWIN","APPS","UNDEF","SLEEP",
        "NUMPAD0","NUMPAD1","NUMPAD2","NUMPAD3","NUMPAD4","NUMPAD5","NUMPAD6","NUMPAD7","NUMPAD8","NUMPAD9","MULTIPLY","ADD",
        "SEPARATOR","SUBTRACT","DECIMAL","DIVIDE","F1","F2","F3","F4","F5","F6","F7","F8",
        "F9","F10","F11","F12","F13","F14","F15","F16","F17","F18","F19","F20",
        "F21","F22","F23","F24","UNDEF","UNDEF","UNDEF","UNDEF","UNDEF","UNDEF","UNDEF","UNDEF",
        "NUMLOCK","SCROLL","UNDEF","UNDEF","UNDEF","UNDEF","UNDEF","UNDEF","UNDEF","UNDEF","UNDEF","UNDEF",
        "UNDEF","UNDEF","UNDEF","UNDEF","LSHIFT","RSHIFT","LCONTROL","RCONTROL","LMENU","RMENU","BROWSER_BACK","BROWSER_FORWARD",
        "BROWSER_REFRESH","BROWSER_STOP","BROWSER_SEARCH","BROWSER_FAVORITES","BROWSER_HOME","VOLUME_MUTE","VOLUME_DOWN","VOLUME_UP","MEDIA_NEXT","MEDIA_PREV","MEDIA_STOP","MEDIA_PLAY_PAUSE",
        "LAUNCH_MAIL","LAUNCH_MEDIA_SELECT","LAUNCH_APP1","LAUNCH_APP2","UNDEF","SEMICOLON","EQUALS","OEM_COMMA","MINUS","PERIOD","SLASH","OEM_3",
        "UNDEF","UNDEF","UNDEF","UNDEF","UNDEF","UNDEF","UNDEF","UNDEF","UNDEF","UNDEF","UNDEF","UNDEF",
        "UNDEF","UNDEF","UNDEF","UNDEF","UNDEF","UNDEF","UNDEF","UNDEF","UNDEF","LBRACKET","BACKSLASH","RBRACKET",
        "APOSTROPHE","OEM_8","UNDEF","UNDEF","UNDEF","UNDEF","UNDEF","UNDEF","UNDEF","UNDEF","UNDEF","UNDEF",
        "UNDEF","UNDEF","UNDEF","NONE"
    };

    // Safe helper to get key name string from a virtual key code.
    std::string GetKeyName(int keycode) {
        if (keycode >= 0 && keycode < 256) {
            return std::string(key_names[keycode]);
        }
        return "UNKNOWN";
    }

} // end anonymous namespace

// ----------------------------------------------------------------------
// MenuItem Abstraction
// ----------------------------------------------------------------------

struct MenuItem {
    std::string label;

    // Returns the string to display on the right side (value column).
    std::function<std::string()> getValueDisplay;

    // Called when Left (-1) or Right (+1) is pressed.
    std::function<void(int dir)> onAdjust;

    // Called when Enter is pressed.
    std::function<void()> onActivate;

    // Predicates that control whether directional arrows are drawn.
    std::function<bool()> hasLeft;
    std::function<bool()> hasRight;

    // If true, this item opens a submenu or fires a command (renders differently).
    bool isLink = false;

    // If true, this item cannot be adjusted. It is rendered in a dimmed color
    // and no arrows are drawn. Use this for artwork options that did not load.
    bool isDisabled = false;

    // Optional LIVE disabled predicate, re-evaluated every draw/input. Use
    // when the disabled state depends on another setting the user can change
    // without leaving the menu (e.g. COLOR MONITOR SETUP greys out while a
    // RASTER EFFECT texture is selected). Overrides isDisabled when set.
    std::function<bool()> isDisabledFn;

    // Effective disabled state: live predicate if present, else the baked flag.
    bool disabled() const { return isDisabledFn ? isDisabledFn() : isDisabled; }

    // Optional short reason shown after the value when the item is disabled
    // (e.g. "NOT LOADED"). Keep it short -- it has to fit in the value column.
    std::string disabledReason;

    // ------------------------------------------------------------------
    // Factory: Integer with optional string label list
    // ------------------------------------------------------------------
    static MenuItem Integer(const std::string& name, int* target, int min, int max,
        const std::vector<std::string>& labels = {})
    {
        MenuItem item;
        item.label = name;
        item.getValueDisplay = [target, labels]() {
            if (!labels.empty()) {
                int idx = *target;
                if (idx < 0) idx = 0;
                if (idx >= (int)labels.size()) idx = (int)labels.size() - 1;
                return labels[idx];
            }
            return std::to_string(*target);
        };
        item.onAdjust = [target, min, max](int dir) {
            int newVal = *target + dir;
            if (newVal > max) newVal = max;
            if (newVal < min) newVal = min;
            *target = newVal;
        };
        item.onActivate = []() {};
        item.hasLeft  = [target, min]() { return *target > min; };
        item.hasRight = [target, max]() { return *target < max; };
        return item;
    }

    // ------------------------------------------------------------------
    // Factory: Float value with step size and format string
    // ------------------------------------------------------------------
    static MenuItem Float(const std::string& name, float* target, float step,
        float min, float max, const char* fmt = "%2.1f")
    {
        MenuItem item;
        item.label = name;
        item.getValueDisplay = [target, fmt]() {
            char buf[32];
            snprintf(buf, 32, fmt, *target);
            return std::string(buf);
        };
        item.onAdjust = [target, step, min, max](int dir) {
            float newVal = *target + (dir * step);
            if (newVal > max) newVal = max;
            if (newVal < min) newVal = min;
            *target = newVal;
        };
        item.onActivate = []() {};
        item.hasLeft  = [target, min]() { return *target > min; };
        item.hasRight = [target, max]() { return *target < max; };
        return item;
    }

    // ------------------------------------------------------------------
    // Factory: Boolean (int 0/1) shown as NO/YES
    // ------------------------------------------------------------------
    static MenuItem Bool(const std::string& name, int* target) {
        return Integer(name, target, 0, 1, { "NO", "YES" });
    }

    // ------------------------------------------------------------------
    // Factory: Boolean that is conditionally disabled when art is absent.
    // availableFlag  - pointer to the g_xxxAvailable flag from menu.h.
    //                  When *availableFlag == 0 the item is disabled.
    // unavailText    - short string shown in value column when disabled.
    // ------------------------------------------------------------------
    static MenuItem BoolWithAvailability(const std::string& name, int* target,
        const int* availableFlag,
        const std::string& unavailText = "NOT LOADED")
    {
        // Build a normal Bool item first, then decorate it.
        MenuItem item = Bool(name, target);

        // Capture the flag pointer so we can query it at draw/adjust time.
        // We do NOT use a lambda that re-evaluates the flag in getValueDisplay
        // because the disabled state is applied at the top level in Draw().
        // Instead we store the flag and the reason, and BuildVideoMenu will
        // call this at item-build time to bake in the current state.
        if (*availableFlag == 0) {
            item.isDisabled     = true;
            item.disabledReason = unavailText;
        }

        return item;
    }

    // ------------------------------------------------------------------
    // Factory: String cycling through a list of options
    // ------------------------------------------------------------------
    static MenuItem String(const std::string& name, std::string* target,
        const std::vector<std::string>& options,
        std::function<void(const std::string&)> onChange = nullptr)
    {
        MenuItem item;
        item.label = name;
        item.getValueDisplay = [target]() { return *target; };

        item.onAdjust = [target, options, onChange](int dir) {
            if (options.empty()) return;
            int idx = 0;
            for (int i = 0; i < (int)options.size(); ++i) {
                if (options[i] == *target) { idx = i; break; }
            }
            idx += dir;
            if (idx < 0) idx = 0;
            if (idx >= (int)options.size()) idx = (int)options.size() - 1;
            *target = options[idx];
            if (onChange) onChange(*target);
        };

        item.onActivate = []() {};

        item.hasLeft = [target, options]() {
            if (options.empty()) return false;
            for (int i = 0; i < (int)options.size(); ++i) {
                if (options[i] == *target) return i > 0;
            }
            return false;
        };

        item.hasRight = [target, options]() {
            if (options.empty()) return false;
            for (int i = 0; i < (int)options.size(); ++i) {
                if (options[i] == *target) return i < (int)options.size() - 1;
            }
            return false;
        };

        return item;
    }

    // ------------------------------------------------------------------
    // Factory: Link / Command (opens submenu or fires action)
    // ------------------------------------------------------------------
    static MenuItem Link(const std::string& name, std::function<void()> action) {
        MenuItem item;
        item.label = name;
        item.isLink = true;
        item.getValueDisplay = []() { return ""; };
        item.onAdjust    = [](int) {};
        item.onActivate  = action;
        item.hasLeft     = []() { return false; };
        item.hasRight    = []() { return false; };
        return item;
    }

    // ------------------------------------------------------------------
    // Factory: Permanently disabled placeholder item.
    //
    // Use this for settings that exist in the config struct and are saved/
    // loaded correctly, but whose runtime effect is not yet implemented.
    // The item is grayed out and always shows "DISABLED" in the value
    // column. No arrows are drawn and no adjustment is possible.
    // Swap back to Integer/Bool factories once the feature is wired up.
    // ------------------------------------------------------------------
    static MenuItem Disabled(const std::string& name)
    {
        MenuItem item;
        item.label = name;
        item.isDisabled = true;
        item.disabledReason = "DISABLED";
        item.getValueDisplay = []() { return std::string("DISABLED"); };
        item.onAdjust = [](int) {};
        item.onActivate = []() {};
        item.hasLeft = []() { return false; };
        item.hasRight = []() { return false; };
        return item;
    }
};  


// ----------------------------------------------------------------------
// MenuManager (Singleton)
// ----------------------------------------------------------------------

class MenuManager {
public:
    static MenuManager& Instance() {
        static MenuManager instance;
        return instance;
    }

    // Public interface matching the legacy free-function calls
    int  GetStatus() const  { return m_showMenu; }
    void SetStatus(int on);
    int  GetLevel() const   { return static_cast<int>(m_currentMenuId); }
    void SetLevelTop();
    void NavigateBack();

    void Draw();

    void Navigate(int dir); // Up/Down arrow
    void Adjust(int dir);   // Left/Right arrow
    void Select();          // Enter key

    bool IsPolling() const  { return m_isPolling; }
    void PollInput();

private:
    MenuManager() = default;

    // State
    int    m_showMenu      = 0;
    MenuID m_currentMenuId = MenuID::Root;
    std::vector<MenuItem> m_items;
    int    m_selectedIndex = 0;
    int    m_scrollOffset  = 0;

    // Layout cache -- recalculated once per menu build/adjust
    float  m_cachedMaxWidth = 0.0f;

    // Key/joy polling state for input assignment screens
    bool   m_isPolling      = false;
    bool   m_pollingIsJoy   = false;
    std::function<void(int code)> m_inputAssignmentHandler;

    // Menu Builders
    void RebuildCurrentMenu();
    void BuildRootMenu();
    void BuildVideoMenu();
    void BuildVectorMonitorMenu();
    void BuildMonoMonitorMenu();
    void BuildColorMonitorMenu();
    void BuildSoundMenu();
    void BuildDipSwitchMenu();
    void BuildAnalogMenu();
    void BuildInputDevicesMenu();
    void BuildInputMenu(bool isGlobal, bool isJoystick);

    // Helpers
    void TransitionTo(MenuID newId);
    void RecalculateLayout();
    void DrawBackground();
    void DrawFooter();
    void SaveConfigIfRequired(MenuID fromId);

    std::string GetTitleText() const;
    std::string GetFooterText() const;
};

// ----------------------------------------------------------------------
// MenuManager Implementation
// ----------------------------------------------------------------------

void MenuManager::SetStatus(int on) {
    if (m_showMenu != on) {
        if (on == 0) {
            // Save whatever submenu we're leaving, then return to root.
            SaveConfigIfRequired(m_currentMenuId);
            SetLevelTop();
        }
        else {
            SetLevelTop();
        }
    }
    m_showMenu = on;
}

void MenuManager::SetLevelTop() {
    m_isPolling = false;
    m_inputAssignmentHandler = nullptr;
    TransitionTo(MenuID::Root);
}

// Go back ONE level: nested submenus return to their parent menu, top-level
// submenus return to the root. Used by ESC / joypad-Back while the menu is
// open, so leaving e.g. MONO MONITOR SETUP lands in VIDEO SETUP, not the
// main menu. TransitionTo() saves the departing menu's settings.
void MenuManager::NavigateBack() {
    m_isPolling = false;
    m_inputAssignmentHandler = nullptr;

    MenuID parent = MenuID::Root;
    if (m_currentMenuId == MenuID::VectorMonitor ||
        m_currentMenuId == MenuID::MonoMonitor ||
        m_currentMenuId == MenuID::ColorMonitor)
        parent = MenuID::Video;

    TransitionTo(parent);
}

void MenuManager::TransitionTo(MenuID newId) {
    // Save the menu we are leaving (unless we are rebuilding in place).
    if (m_currentMenuId != newId) {
        SaveConfigIfRequired(m_currentMenuId);
    }
    m_currentMenuId = newId;
    m_selectedIndex = 0;
    m_scrollOffset  = 0;
    m_isPolling     = false;
    RebuildCurrentMenu();

    // Stable layout dimensions before the first Draw() call.
    RecalculateLayout();
}

std::string MenuManager::GetTitleText() const {
    if (m_currentMenuId == MenuID::Root)       return "MAIN MENU";
    if (m_currentMenuId == MenuID::Audio) {
        // Show (GLOBAL) label when accessed from the GUI frontend, because in
        // that context saves go to aae.ini and affect all games.
        return emulator_is_gui_active() ? "AUDIO SETUP (GLOBAL)" : "AUDIO SETUP";
    }
    if (m_currentMenuId == MenuID::Video) {
        return emulator_is_gui_active() ? "VIDEO SETUP (GLOBAL)" : "VIDEO SETUP";
    }
    if (m_currentMenuId == MenuID::MonoMonitor) return "MONO MONITOR (B/W RASTER)";
    if (m_currentMenuId == MenuID::ColorMonitor) return "COLOR MONITOR (COLOR RASTER)";
    if (m_currentMenuId == MenuID::VectorMonitor) return "VECTOR MONITOR SETUP";
    if (m_currentMenuId == MenuID::GlobalKeys) return "KEY CONFIG (GLOBAL)";
    if (m_currentMenuId == MenuID::LocalKeys)  return "KEY CONFIG (THIS GAME)";
    if (m_currentMenuId == MenuID::GlobalJoy)  return "JOY CONFIG (GLOBAL)";
    if (m_currentMenuId == MenuID::LocalJoy)   return "JOY CONFIG (THIS GAME)";
    if (m_currentMenuId == MenuID::Analog)     return "ANALOG SETTINGS";
    if (m_currentMenuId == MenuID::InputDevices) return "INPUT DEVICES";
    if (m_currentMenuId == MenuID::DipSwitch)  return "DIPSWITCH MENU";
    return "CONFIGURATION";
}

std::string MenuManager::GetFooterText() const {
    if (m_currentMenuId == MenuID::Root) return "ESC to close menu";
    return "ESC to return to menu root";
}

void MenuManager::RecalculateLayout() {
    // Start with the title and footer widths as minimums.
    std::string title  = GetTitleText();
    float maxW = (float)title.length() * CHAR_PITCH;

    std::string footer = GetFooterText();
    float footerW = (float)footer.length() * FOOTER_PITCH;
    if (footerW > maxW) maxW = footerW;

    // Walk all items (not just the visible window) to find the widest row.
    for (const auto& item : m_items) {
        float labelW = (float)item.label.length() * CHAR_PITCH;
        float totalW = labelW;

        if (!item.isLink && item.getValueDisplay) {
            std::string val = item.disabled()
                ? item.disabledReason   // use the reason string for width calc
                : item.getValueDisplay();
            // Values wider than VALUE_W_MAX marquee within the field, so the
            // box never needs to grow past it.
            float valW = (float)val.length() * CHAR_PITCH;
            if (valW > VALUE_W_MAX) valW = VALUE_W_MAX;
            totalW = VALUE_X_OFFSET + valW + ARROW_EXTRA;
        }

        if (totalW > maxW) maxW = totalW;
    }

    m_cachedMaxWidth = maxW;
}

void MenuManager::SaveConfigIfRequired(MenuID fromId) {
    // When the menu is used from the GUI frontend (no game running), treat all
    // saves as global -- write everything to aae.ini (path 0). This prevents
    // settings from accidentally going to gui.ini and also makes the intent
    // clear: editing video/audio from the GUI sets the global defaults.
    const bool inGui = emulator_is_gui_active();

    if (fromId == MenuID::Video) {
        // Global display options always go to aae.ini (path 0).
        // Window mode saves from the LIVE state (covers ALT+ENTER changes
        // made outside the menu) and always globally -- never per-game.
        my_set_config_int("window", "fullscreen",
            GetWindowSetup().borderlessFullscreen ? 1 : 0, 0);
        my_set_config_string("main", "game_aspect",
            config.game_aspect ? config.game_aspect : "AUTO", inGui ? 0 : gamenum);
        // Renderer is a machine-level choice and always global (path 0).
        // The PENDING value is what gets written - the live one never changes
        // mid-session (see BuildVideoMenu's RENDERER item).
        my_set_config_string("main", "renderer",
            (config.renderer_pending == RENDERER_VULKAN) ? "vulkan" : "opengl", 0);
        my_set_config_int("main", "screenw",     config.screenw,   0);
        my_set_config_int("main", "screenh",     config.screenh,   0);
        my_set_config_int("main", "gamma",       config.gamma,     0);
        my_set_config_int("main", "bright",      config.bright,    0);
        my_set_config_int("main", "contrast",    config.contrast,  0);
        my_set_config_int("main", "force_vsync", config.forcesync, 0);
        my_set_config_int("main", "widescreen",  config.widescreen,0);
        my_set_config_int("main", "priority",    config.priority,  0);
        my_set_config_int("main", "kbleds",      config.kbleds,    0);

        // Game-specific visual overrides.
        // In GUI mode these also go to aae.ini (path 0) so they become the
        // new global defaults rather than being silently written to gui.ini.
        const int vidPath = inGui ? 0 : gamenum;
        my_set_config_int("main", "artwork",     config.artwork,  vidPath);
        my_set_config_int("main", "overlay",     config.overlay,  vidPath);
        my_set_config_int("main", "bezel",       config.bezel,    vidPath);
        my_set_config_int("main", "artcrop",     config.artcrop,  vidPath);
    }
    else if (fromId == MenuID::InputDevices) {
        // Device-to-player assignments are machine-level settings: always
        // global (aae.ini [input]). The device path is the stable identity;
        // the index is a same-session convenience/fallback.
        for (int p = 0; p < 4; p++) {
            const std::string mbase = "mouse_player" + std::to_string(p + 1);
            my_set_config_int("input", mbase.c_str(), config.mouse_player[p], 0);
            my_set_config_string("input", (mbase + "_path").c_str(),
                config.mouse_player_path[p], 0);

            const std::string kbase = "kbd_player" + std::to_string(p + 1);
            my_set_config_int("input", kbase.c_str(), config.kbd_player[p], 0);
            my_set_config_string("input", (kbase + "_path").c_str(),
                config.kbd_player_path[p], 0);

            const std::string jbase = "joy_player" + std::to_string(p + 1);
            my_set_config_int("input", jbase.c_str(), config.joy_player[p], 0);
            my_set_config_string("input", (jbase + "_id").c_str(),
                config.joy_player_id[p], 0);
        }
    }
    else if (fromId == MenuID::VectorMonitor) {
        // Vector monitor / beam settings. Same persistence rules as the old
        // Video-menu placement: per-game overrides during gameplay, global
        // defaults (aae.ini) when adjusted from the GUI frontend.
        const int vidPath = inGui ? 0 : gamenum;
        my_set_config_int("main", "vectortrail", config.vectrail, vidPath);
        my_set_config_int("main", "vectorglow",  config.vecglow,  vidPath);
        my_set_config_int  ("main", "glow_filter",  config.glow_filter,  vidPath);
        my_set_config_float("main", "glow2_gain",   config.glow2_gain,   vidPath);
        my_set_config_float("main", "glow2_spread", config.glow2_spread, vidPath);
        my_set_config_float("main", "glow2_tail",   config.glow2_tail,   vidPath);
        my_set_config_float("main", "glow2_core",   config.glow2_core,   vidPath);
        my_set_config_int("main", "m_line",      config.m_line,   vidPath);
        my_set_config_int("main", "m_point",     config.m_point,  vidPath);
        my_set_config_int("main", "gain",        config.gain,     vidPath);
        my_set_config_float("main", "line_smoothing",  config.line_smoothing,  vidPath);
        my_set_config_float("main", "corner_strength", config.corner_strength, vidPath);
        my_set_config_int  ("main", "shots_textured",  config.shots_textured,  vidPath);
        my_set_config_int("main", "drawzero", config.drawzero, 0);
    }
    else if (fromId == MenuID::MonoMonitor) {
        // Same persistence rules as the Video menu: adjusted from the GUI
        // frontend these save as global defaults (aae.ini [monitormono]);
        // adjusted during gameplay they save to the game's own ini as
        // per-game overrides (setup_config reads them back per game).
        const int monoPath = inGui ? 0 : gamenum;
        my_set_config_int  ("monitormono", "mono_enable",          config.mono_enable,          monoPath);
        my_set_config_float("monitormono", "mono_blur_h",          config.mono_blur_h,          monoPath);
        my_set_config_float("monitormono", "mono_blur_v",          config.mono_blur_v,          monoPath);
        my_set_config_float("monitormono", "mono_halation",        config.mono_halation,        monoPath);
        my_set_config_float("monitormono", "mono_halation_radius", config.mono_halation_radius, monoPath);
        my_set_config_float("monitormono", "mono_scanline",        config.mono_scanline,        monoPath);
        my_set_config_float("monitormono", "mono_contrast",        config.mono_contrast,        monoPath);
        my_set_config_float("monitormono", "mono_brightness",      config.mono_brightness,      monoPath);
        my_set_config_int  ("monitormono", "mono_tint",            config.mono_tint,            monoPath);
    }
    else if (fromId == MenuID::ColorMonitor) {
        // Same persistence rules as the Video menu: global from the GUI
        // frontend, per-game override ini during gameplay.
        const int colPath = inGui ? 0 : gamenum;
        my_set_config_string("main", "raster_effect",
            config.raster_effect ? config.raster_effect : "NONE", colPath);
        my_set_config_int  ("monitorcolor", "color_enable",          config.color_enable,          colPath);
        my_set_config_float("monitorcolor", "color_blur_h",          config.color_blur_h,          colPath);
        my_set_config_float("monitorcolor", "color_blur_v",          config.color_blur_v,          colPath);
        my_set_config_float("monitorcolor", "color_converge",        config.color_converge,        colPath);
        my_set_config_float("monitorcolor", "color_halation",        config.color_halation,        colPath);
        my_set_config_float("monitorcolor", "color_halation_radius", config.color_halation_radius, colPath);
        my_set_config_float("monitorcolor", "color_scanline",        config.color_scanline,        colPath);
        my_set_config_float("monitorcolor", "color_contrast",        config.color_contrast,        colPath);
        my_set_config_float("monitorcolor", "color_brightness",      config.color_brightness,      colPath);
        my_set_config_float("monitorcolor", "color_saturation",      config.color_saturation,      colPath);
        my_set_config_int  ("monitorcolor", "color_mask_type",       config.color_mask_type,       colPath);
        my_set_config_float("monitorcolor", "color_mask_strength",   config.color_mask_strength,   colPath);
        my_set_config_float("monitorcolor", "color_mask_scale",      config.color_mask_scale,      colPath);
    }
    else if (fromId == MenuID::Audio) {
        // In GUI mode audio saves go to aae.ini (path 0) as global defaults.
        // During gameplay they go to the per-game ini as overrides.
        const int audPath = inGui ? 0 : gamenum;

        // Volumes stored as real 0..255 byte values.
        my_set_config_int("main", "mainvol",  config.mainvol,  audPath);
        my_set_config_int("main", "pokeyvol", config.pokeyvol, audPath);
        my_set_config_int("main", "noisevol", config.noisevol, audPath);
        my_set_config_int("main", "hvnoise",  config.hvnoise,  audPath);
        my_set_config_int("main", "psnoise",  config.psnoise,  audPath);
        my_set_config_int("main", "pshiss",   config.pshiss,   audPath);
    }
}

// ----------------------------------------------------------------------
// Menu Builders
// ----------------------------------------------------------------------

void MenuManager::RebuildCurrentMenu() {
    m_items.clear();
    switch (m_currentMenuId) {
    case MenuID::Root:       BuildRootMenu();              break;
    case MenuID::Video:      BuildVideoMenu();             break;
    case MenuID::MonoMonitor: BuildMonoMonitorMenu();      break;
    case MenuID::ColorMonitor: BuildColorMonitorMenu();    break;
    case MenuID::VectorMonitor: BuildVectorMonitorMenu();  break;
    case MenuID::Audio:      BuildSoundMenu();             break;
    case MenuID::DipSwitch:  BuildDipSwitchMenu();         break;
    case MenuID::Analog:       BuildAnalogMenu();          break;
    case MenuID::InputDevices: BuildInputDevicesMenu();    break;
    case MenuID::GlobalKeys: BuildInputMenu(true,  false); break;
    case MenuID::LocalKeys:  BuildInputMenu(false, false); break;
    case MenuID::GlobalJoy:  BuildInputMenu(true,  true);  break;
    case MenuID::LocalJoy:   BuildInputMenu(false, true);  break;
    default:                 BuildRootMenu();              break;
    }
}

void MenuManager::BuildRootMenu() {
    // When running in the GUI frontend, video and audio changes go to aae.ini
    // as global defaults, so label them accordingly so the user knows.
    const bool inGui = emulator_is_gui_active();

    m_items.push_back(MenuItem::Link("KEY CONFIG (GLOBAL)", [this]() { TransitionTo(MenuID::GlobalKeys); }));
    m_items.push_back(MenuItem::Link("KEY CONFIG (THIS GAME)", [this]() { TransitionTo(MenuID::LocalKeys);  }));
    m_items.push_back(MenuItem::Link("JOY CONFIG (GLOBAL)", [this]() { TransitionTo(MenuID::GlobalJoy);  }));
    m_items.push_back(MenuItem::Link("JOY CONFIG (THIS GAME)", [this]() { TransitionTo(MenuID::LocalJoy);   }));
    m_items.push_back(MenuItem::Link("ANALOG CONFIG", [this]() { TransitionTo(MenuID::Analog);     }));
    m_items.push_back(MenuItem::Link("INPUT DEVICES", [this]() { TransitionTo(MenuID::InputDevices); }));
    m_items.push_back(MenuItem::Link("CONTROLLER HELP", []() {
        set_menu_status(0);        // close the menu so the guide has the screen
        controller_help_open();
    }));
    m_items.push_back(MenuItem::Link("DIPSWITCHES", [this]() { TransitionTo(MenuID::DipSwitch);  }));
    m_items.push_back(MenuItem::Link(inGui ? "VIDEO SETUP (GLOBAL)" : "VIDEO SETUP",
        [this]() { TransitionTo(MenuID::Video);      }));
    m_items.push_back(MenuItem::Link(inGui ? "SOUND SETUP (GLOBAL)" : "SOUND SETUP",
        [this]() { TransitionTo(MenuID::Audio);      }));
}

void MenuManager::BuildVideoMenu() {
    // FULLSCREEN reflects and drives the LIVE window state via the same
    // borderless toggle ALT+ENTER uses (the old item only flipped a config
    // int that nothing applied). Displayed from ws.borderlessFullscreen so
    // it can never drift from reality, and saved GLOBALLY on menu exit --
    // window mode is a machine preference, never per-game.
    {
        MenuItem fs;
        fs.label = "FULLSCREEN";
        fs.getValueDisplay = []() {
            return std::string(GetWindowSetup().borderlessFullscreen ? "YES" : "NO");
        };
        fs.onAdjust = [](int) {
            GetSystemWindow().ToggleBorderlessFullscreen();
            config.windowed = GetWindowSetup().borderlessFullscreen ? 1 : 0;
        };
        fs.onActivate = []() {};
        fs.hasLeft  = []() { return GetWindowSetup().borderlessFullscreen; };
        fs.hasRight = []() { return !GetWindowSetup().borderlessFullscreen; };
        m_items.push_back(fs);
    }

    // RENDERER: which graphics backend starts NEXT run.
    //
    // Deliberately edits config.renderer_pending, never config.renderer:
    // init_gl() latches the live value on every game load, so flipping it
    // mid-session would try to switch APIs against a window whose context was
    // created for the other one at startup. The value display says so, and
    // SaveConfigIfRequired writes it to aae.ini [main] renderer globally.
    {
        MenuItem rend;
        rend.label = "RENDERER";
        rend.getValueDisplay = []() {
            const bool vk = (config.renderer_pending == RENDERER_VULKAN);
            // Flag a pending change so the user knows a restart is needed.
            const bool changed = (config.renderer_pending != config.renderer);
            return std::string(vk ? "VULKAN" : "OPENGL") +
                   (changed ? " (NEXT RUN)" : "");
        };
        rend.onAdjust = [](int dir) {
            (void)dir;   // two states: either direction toggles
            config.renderer_pending =
                (config.renderer_pending == RENDERER_VULKAN) ? RENDERER_OPENGL
                                                             : RENDERER_VULKAN;
        };
        rend.onActivate = []() {};
        rend.hasLeft  = []() { return config.renderer_pending == RENDERER_VULKAN; };
        rend.hasRight = []() { return config.renderer_pending == RENDERER_OPENGL; };
        m_items.push_back(rend);
    }

    // Resolution presets. Index 0 is AUTO (screenw/screenh = 0): the startup
    // code sizes the window to the largest aspect-fit on the monitor. The
    // fixed presets are honored literally at next launch - the window is
    // created at exactly that client size, not inflated to fill the screen.
    //
    // Applies on NEXT RUN, same pattern as the RENDERER item: the live window
    // was created at startup, so the value display flags a pending change by
    // comparing against a snapshot of the values the session started with.
    static const int kResW[] = { 0, 1024, 1152, 1280, 1600, 1920 };
    static const int kResH[] = { 0,  768,  864, 1024, 1200, 1080 };
    static constexpr int kResCount = 6;

    // Static so the index survives redraws; snapshot taken once per session.
    static int resIndex = 0;
    static int s_startupW = -1, s_startupH = -1;
    if (s_startupW < 0) { s_startupW = config.screenw; s_startupH = config.screenh; }
    for (int i = 0; i < kResCount; ++i)
        if (config.screenw == kResW[i] && config.screenh == kResH[i]) { resIndex = i; break; }

    MenuItem resItem;
    resItem.label = "RESOLUTION";
    resItem.getValueDisplay = []() {
        const char* names[] = { "AUTO", "1024x768", "1152x864", "1280x1024", "1600x1200", "1920x1080" };
        const bool changed = (config.screenw != s_startupW) || (config.screenh != s_startupH);
        return std::string(names[std::clamp(resIndex, 0, kResCount - 1)]) +
               (changed ? " (NEXT RUN)" : "");
        };
    resItem.onAdjust = [](int dir) {
        resIndex = std::clamp(resIndex + dir, 0, kResCount - 1);
        config.screenw = kResW[resIndex];
        config.screenh = kResH[resIndex];
        };
    resItem.hasLeft = []() { return resIndex > 0; };
    resItem.hasRight = []() { return resIndex < kResCount - 1; };
    resItem.onActivate = []() {};
    m_items.push_back(resItem);

    // ------------------------------------------------------------------
    // The following five settings exist in config and are saved/loaded
    // correctly, but are not yet connected to the rendering pipeline.
    // They show as DISABLED so the user knows they are not active yet.
    // To re-enable any of them once the rendering code is wired up,
    // swap the Disabled() call for the commented-out original below it.
    // ------------------------------------------------------------------
   // m_items.push_back(MenuItem::Disabled("GAMMA"));
    // m_items.push_back(MenuItem::Integer("GAMMA",      &config.gamma,    50, 200));

   // m_items.push_back(MenuItem::Disabled("BRIGHTNESS"));
    // m_items.push_back(MenuItem::Integer("BRIGHTNESS", &config.bright,   50, 200));

   // m_items.push_back(MenuItem::Disabled("CONTRAST"));
    // m_items.push_back(MenuItem::Integer("CONTRAST",   &config.contrast, 50, 200));

    m_items.push_back(MenuItem::Disabled("VSYNC"));
    // m_items.push_back(MenuItem::Bool("VSYNC",         &config.forcesync));

    // GAME ASPECT: display aspect override. AUTO (the default) applies
    // nothing -- every game keeps its natural computed aspect (rotated
    // games portrait, yiear narrow, etc.). The fixed ratios are the common
    // tweaks: 4:3 / 5:4 / 16:9 for horizontal games, 3:4 / 9:16 for
    // rotated ones. Applies live via the same WindowUtil_UpdateAspect path
    // run_game uses at launch; persists per-game during gameplay.
    {
        static std::string s_gameAspect;
        if (config.game_aspect && config.game_aspect[0])
            s_gameAspect = config.game_aspect;
        else
            s_gameAspect = "AUTO";

        MenuItem ga = MenuItem::String(
            "GAME ASPECT",
            &s_gameAspect,
            { "AUTO", "4:3", "3:4", "5:4", "16:9", "9:16" },
            [](const std::string& v) {
                config.game_aspect = (char*)v.c_str();
                // Apply immediately, every flip -- in-game AND in the GUI
                // frontend (there AUTO resolves to the frontend's own
                // natural aspect, so cycling back always restores it).
                float f = aspect_from_string(config.game_aspect);
                if (f <= 0.0f)
                    f = Layout_ComputeGameAspect();   // AUTO: natural aspect
                LOG_INFO("GAME ASPECT menu: %s -> %.3f", v.c_str(), f);
                if (f > 0.0f)
                    WindowUtil_UpdateAspect(f);
            }
        );
        // A command-line -aspect (or ini use_aspect) override outranks ANY
        // setting, including this one -- grey the item out so a live adjust
        // cannot clobber the forced aspect mid-game.
        ga.isDisabledFn = []() { return GetWindowSetup().aspectOverrideActive; };
        ga.disabledReason = "-ASPECT FORCED";
        m_items.push_back(ga);
    }

    // Monitor setup submenus. Each one only applies to its monitor class,
    // so during gameplay the entries for monitor types the running game
    // does NOT use are greyed out (in the GUI frontend all three stay
    // enabled -- there you are editing the global defaults).
    auto gameUsesMonitor = [](int vattrMask) -> bool {
        if (emulator_is_gui_active()) return true;
        return Machine && Machine->drv &&
            (Machine->drv->video_attributes & vattrMask) != 0;
    };

    // Vector monitor / beam settings submenu (affects vector games only).
    {
        MenuItem link = MenuItem::Link("VECTOR MONITOR SETUP",
            [this]() { TransitionTo(MenuID::VectorMonitor); });
        link.isDisabledFn = [gameUsesMonitor]() { return !gameUsesMonitor(VIDEO_TYPE_VECTOR); };
        link.disabledReason = "NOT A VECTOR GAME";
        m_items.push_back(link);
    }

    // Mono monitor CRT simulation submenu (affects B/W raster games only).
    {
        MenuItem link = MenuItem::Link("MONO MONITOR SETUP",
            [this]() { TransitionTo(MenuID::MonoMonitor); });
        link.isDisabledFn = [gameUsesMonitor]() { return !gameUsesMonitor(VIDEO_TYPE_RASTER_BW); };
        link.disabledReason = "NOT A B/W RASTER GAME";
        m_items.push_back(link);
    }

    // Color monitor / screen effect submenu (affects color raster games only).
    {
        MenuItem link = MenuItem::Link("COLOR MONITOR SETUP",
            [this]() { TransitionTo(MenuID::ColorMonitor); });
        link.isDisabledFn = [gameUsesMonitor]() { return !gameUsesMonitor(VIDEO_TYPE_RASTER_COLOR); };
        link.disabledReason = "NOT A COLOR RASTER GAME";
        m_items.push_back(link);
    }

    // ------------------------------------------------------------------
    // Artwork items -- conditionally disabled based on availability flags.
    //
    // BoolWithAvailability checks the flag at build time (this function is
    // called fresh every time the Video submenu is entered). If the flag is
    // 0 the item is marked disabled: grayed out, no arrows, ignores input.
    // Your texture loading code must set these flags after loading art.
    //
    // artcrop depends on bezel, so it inherits g_bezelAvailable.
    // ------------------------------------------------------------------
    m_items.push_back(MenuItem::BoolWithAvailability(
        "ARTWORK", &config.artwork, &g_artworkAvailable, "NOT LOADED"));
    m_items.push_back(MenuItem::BoolWithAvailability(
        "OVERLAY", &config.overlay, &g_overlayAvailable, "NOT LOADED"));
    m_items.push_back(MenuItem::BoolWithAvailability(
        "BEZEL ART", &config.bezel, &g_bezelAvailable, "NOT LOADED"));

    // Crop-bezel requires bezel to be present. Even if g_artcropAvailable
    // were set separately, we guard on g_bezelAvailable as the primary gate.
    {
        const int* cropFlag = g_bezelAvailable ? &g_artcropAvailable : &g_bezelAvailable;
        m_items.push_back(MenuItem::BoolWithAvailability(
            "CROP BEZEL", &config.artcrop, cropFlag, "NEEDS BEZEL"));
    }

    // (The raster/scanlines overlay selector moved into COLOR MONITOR SETUP
    // as part of the combined SCREEN EFFECT control.)

    m_items.push_back(MenuItem::Integer("PRIORITY", &config.priority, 0, 4,
        { "LOW", "NORMAL", "ABOVE NORMAL", "HIGH", "REALTIME" }));

    m_items.push_back(MenuItem::Bool("KB LEDS", &config.kbleds));
}

// ----------------------------------------------------------------------
// Vector Monitor submenu -- beam renderer settings for vector games.
// Moved out of the main Video menu when the Mono Monitor submenu was
// added; the two monitor-setup entries sit side by side there now.
// All values live-adjust while a vector game runs.
// ----------------------------------------------------------------------
void MenuManager::BuildVectorMonitorMenu() {
    m_items.push_back(MenuItem::Integer("PHOSPHOR TRAIL", &config.vectrail, 0, 3,
        { "NONE", "LITTLE", "MORE", "MAX" }));
    m_items.push_back(MenuItem::Integer("VECTOR GLOW", &config.vecglow, 0, 25));

    // Glow blur algorithm + dual-filter pyramid tuning. All read per frame
    // by both renderers, so every adjustment here applies live. The four
    // GLOW2 items only matter when GLOW FILTER is PYRAMID; they stay
    // visible either way so the menu layout is stable.
    m_items.push_back(MenuItem::Integer("GLOW FILTER", &config.glow_filter, 0, 1,
        { "CLASSIC", "PYRAMID" }));

    // Small helper for the float knobs: value +/- step within [lo, hi],
    // displayed with two decimals. Same hand-rolled pattern as BEAM WIDTH.
    auto addFloatItem = [this](const char* label, float* value,
                               float lo, float hi, float step) {
        MenuItem it;
        it.label = label;
        it.getValueDisplay = [value]() {
            char buf[32];
            snprintf(buf, 32, "%.2f", *value);
            return std::string(buf);
            };
        it.onAdjust = [value, lo, hi, step](int dir) {
            *value += step * (float)dir;
            *value = std::clamp(*value, lo, hi);
            };
        it.hasLeft  = [value, lo]() { return *value > lo; };
        it.hasRight = [value, hi]() { return *value < hi; };
        it.onActivate = []() {};
        m_items.push_back(it);
        };

    addFloatItem("GLOW2 GAIN",   &config.glow2_gain,   0.0f, 30.0f, 0.5f);
    addFloatItem("GLOW2 SPREAD", &config.glow2_spread, 0.2f,  3.0f, 0.05f);
    addFloatItem("GLOW2 TAIL",   &config.glow2_tail,   0.0f,  2.0f, 0.05f);
    addFloatItem("GLOW2 CORE",   &config.glow2_core,   0.0f,  2.0f, 0.05f);

    {
        MenuItem lwItem;
        lwItem.label = "BEAM WIDTH";
        lwItem.getValueDisplay = []() {
            char buf[32];
            snprintf(buf, 32, "%2.1f", config.m_line * 0.1f);
            return std::string(buf);
            };
        lwItem.onAdjust = [](int dir) {
            config.m_line += dir;
            if (config.m_line < 10) config.m_line = 10;
            if (config.m_line > 70) config.m_line = 70;
            config.linewidth = config.m_line * 0.1f;
            };
        lwItem.hasLeft = []() { return config.m_line > 10; };
        lwItem.hasRight = []() { return config.m_line < 70; };
        lwItem.onActivate = []() {};
        m_items.push_back(lwItem);
    }
    {
        MenuItem csItem;
        csItem.label = "BEAM CORNERSIZE";
        csItem.getValueDisplay = []() {
            char buf[32];
            snprintf(buf, 32, "%2.2f", config.corner_strength);
            return std::string(buf);
            };
        csItem.onAdjust = [](int dir) {
            config.corner_strength += dir * 0.05f;
            if (config.corner_strength < 0.3f) config.corner_strength = 0.3f;
            if (config.corner_strength > 2.5f) config.corner_strength = 2.5f;
            };
        csItem.hasLeft = []() { return config.corner_strength > 0.3f; };
        csItem.hasRight = []() { return config.corner_strength < 2.5f; };
        csItem.onActivate = []() {};
        m_items.push_back(csItem);
    }
    {
        MenuItem smItem;
        smItem.label = "BEAM SMOOTHING";
        smItem.getValueDisplay = []() {
            char buf[32];
            snprintf(buf, 32, "%2.1f", config.line_smoothing);
            return std::string(buf);
            };
        smItem.onAdjust = [](int dir) {
            config.line_smoothing += dir * 0.1f;
            if (config.line_smoothing < 0.4f) config.line_smoothing = 0.4f;
            if (config.line_smoothing > 2.0f) config.line_smoothing = 2.0f;
            };
        smItem.hasLeft = []() { return config.line_smoothing > 0.4f; };
        smItem.hasRight = []() { return config.line_smoothing < 2.0f; };
        smItem.onActivate = []() {};
        m_items.push_back(smItem);
    }

    m_items.push_back(MenuItem::Integer("VECTOR SHOTS", &config.shots_textured, 0, 1,
        { "PROCEDURAL", "TEXTURED" }));

    m_items.push_back(MenuItem::Integer("MONITOR GAIN", &config.gain, -127, 127));

    // Not yet wired to the renderer -- kept as a grayed-out placeholder
    // (was the same in the old Video menu placement).
    m_items.push_back(MenuItem::Disabled("DRAW 0 LINES"));
    // m_items.push_back(MenuItem::Bool("DRAW 0 LINES",  &config.drawzero));
}

// ----------------------------------------------------------------------
// Mono Monitor submenu -- knobs for the mono CRT shader applied to B/W
// raster games. All values live-adjust: the render pass reads config
// every frame, so changes are visible immediately while the menu is up.
// Ranges match the shader's designed limits (see config.cpp clamps).
// ----------------------------------------------------------------------
void MenuManager::BuildMonoMonitorMenu() {
    m_items.push_back(MenuItem::Bool("MONO EFFECT", &config.mono_enable));

    m_items.push_back(MenuItem::Integer("MONITOR COLOR", &config.mono_tint, 0, 2,
        { "P4 WHITE", "P1 GREEN", "P3 AMBER" }));

    m_items.push_back(MenuItem::Float("BEAM BLUR HORIZ", &config.mono_blur_h,
        0.05f, 0.0f, 2.5f, "%.2f"));
    m_items.push_back(MenuItem::Float("BEAM BLUR VERT", &config.mono_blur_v,
        0.05f, 0.0f, 1.0f, "%.2f"));
    m_items.push_back(MenuItem::Float("HALATION", &config.mono_halation,
        0.02f, 0.0f, 1.0f, "%.2f"));
    m_items.push_back(MenuItem::Float("HALATION RADIUS", &config.mono_halation_radius,
        0.5f, 1.0f, 16.0f, "%.1f"));
    m_items.push_back(MenuItem::Float("SCANLINE RIPPLE", &config.mono_scanline,
        0.02f, 0.0f, 1.0f, "%.2f"));
    m_items.push_back(MenuItem::Float("BEAM CONTRAST", &config.mono_contrast,
        0.05f, 1.0f, 3.0f, "%.2f"));
    m_items.push_back(MenuItem::Float("BLACK LEVEL LIFT", &config.mono_brightness,
        0.01f, 0.0f, 0.25f, "%.2f"));
}

void MenuManager::BuildColorMonitorMenu() {
    // SCREEN EFFECT: single selector for the color raster look. It drives
    // the two underlying config fields so the render path and all existing
    // inis keep working unchanged:
    //   OFF    -> color_enable=0, raster_effect="NONE"  (raw pixels)
    //   SHADER -> color_enable=1, raster_effect="NONE"  (color CRT shader)
    //   *.png  -> color_enable=0, raster_effect=<file>  (texture overlay)
    {
        static std::string s_screenEffect;
        if (config.raster_effect && config.raster_effect[0] &&
            aae_stricmp(config.raster_effect, "NONE") != 0)
            s_screenEffect = config.raster_effect;
        else
            s_screenEffect = config.color_enable ? "SHADER" : "OFF";

        std::vector<std::string> fxOptions = {
            "OFF", "SHADER", "aperture4x6.png", "scanlines.png", "scanrez2.png", "scanrez2r.png"
        };
        // Include any custom filename that came from the ini but is not listed.
        bool alreadyListed = false;
        for (const auto& opt : fxOptions) {
            if (opt == s_screenEffect) { alreadyListed = true; break; }
        }
        if (!alreadyListed)
            fxOptions.push_back(s_screenEffect);

        m_items.push_back(MenuItem::String(
            "SCREEN EFFECT",
            &s_screenEffect,
            fxOptions,
            [](const std::string& v) {
                if (v == "OFF") {
                    config.color_enable = 0;
                    config.raster_effect = (char*)"NONE";
                }
                else if (v == "SHADER") {
                    config.color_enable = 1;
                    config.raster_effect = (char*)"NONE";
                }
                else {
                    // v aliases the static s_screenEffect above, so the
                    // pointer stays valid (same trick the old Video-menu
                    // RASTER EFFECT item used).
                    config.color_enable = 0;
                    config.raster_effect = (char*)v.c_str();
                }
                // Live texture load/unload so the change shows immediately.
                init_raster_overlay();
            }
        ));
    }

    const size_t firstShaderKnob = m_items.size();

    m_items.push_back(MenuItem::Integer("SHADOW MASK TYPE", &config.color_mask_type, 0, 2,
        { "APERTURE GRILLE", "SLOT MASK", "DOT TRIAD" }));
    m_items.push_back(MenuItem::Float("MASK STRENGTH", &config.color_mask_strength,
        0.02f, 0.0f, 1.0f, "%.2f"));
    m_items.push_back(MenuItem::Float("MASK SIZE", &config.color_mask_scale,
        0.5f, 1.0f, 6.0f, "%.1f"));

    m_items.push_back(MenuItem::Float("SCANLINE STRENGTH", &config.color_scanline,
        0.02f, 0.0f, 1.0f, "%.2f"));

    m_items.push_back(MenuItem::Float("BEAM BLUR HORIZ", &config.color_blur_h,
        0.05f, 0.0f, 2.5f, "%.2f"));
    m_items.push_back(MenuItem::Float("BEAM BLUR VERT", &config.color_blur_v,
        0.05f, 0.0f, 1.0f, "%.2f"));
    m_items.push_back(MenuItem::Float("CONVERGENCE ERROR", &config.color_converge,
        0.05f, 0.0f, 2.0f, "%.2f"));

    m_items.push_back(MenuItem::Float("HALATION", &config.color_halation,
        0.02f, 0.0f, 1.0f, "%.2f"));
    m_items.push_back(MenuItem::Float("HALATION RADIUS", &config.color_halation_radius,
        0.5f, 1.0f, 16.0f, "%.1f"));

    m_items.push_back(MenuItem::Float("BEAM CONTRAST", &config.color_contrast,
        0.05f, 1.0f, 3.0f, "%.2f"));
    m_items.push_back(MenuItem::Float("BLACK LEVEL LIFT", &config.color_brightness,
        0.01f, 0.0f, 0.25f, "%.2f"));
    m_items.push_back(MenuItem::Float("SATURATION", &config.color_saturation,
        0.05f, 0.0f, 2.0f, "%.2f"));

    // Everything below SCREEN EFFECT tunes the shader; grey it all out
    // (live) whenever the shader is not the selected effect.
    for (size_t i = firstShaderKnob; i < m_items.size(); ++i) {
        m_items[i].isDisabledFn = []() { return config.color_enable == 0; };
        m_items[i].disabledReason = "SHADER OFF";
    }
}
void MenuManager::BuildSoundMenu() {
    auto clamp_int = [](int v, int lo, int hi) -> int {
        if (v < lo) return lo;
        if (v > hi) return hi;
        return v;
    };

    // Convert a 0..255 byte volume to a 0..100 percentage for display.
    auto vol255_to_percent = [&](int vol255) -> int {
        vol255 = clamp_int(vol255, 0, 255);
        int pct = (int)std::lround((double)vol255 * 100.0 / 255.0);
        return clamp_int(pct, 0, 100);
    };

    // Convert a 0..100 percentage back to a 0..255 byte volume.
    auto percent_to_vol255 = [&](int pct) -> int {
        pct = clamp_int(pct, 0, 100);
        int vol255 = (int)std::lround((double)pct * 255.0 / 100.0);
        return clamp_int(vol255, 0, 255);
    };

    // Helper that builds a volume item displaying as percentage with
    // smart stepping (1% steps below 10%, 5% steps otherwise).
    auto addVolItemPercent = [this, &clamp_int, &vol255_to_percent, &percent_to_vol255]
        (const std::string& title, int* val255)
    {
        MenuItem item;
        item.label = title;

        item.getValueDisplay = [val255, &vol255_to_percent]() {
            int pct = vol255_to_percent(*val255);
            return std::to_string(pct) + "%";
        };

        item.onAdjust = [val255, &clamp_int, &vol255_to_percent, &percent_to_vol255](int dir) {
            int pct  = vol255_to_percent(*val255);
            int step = (pct < 10) ? 1 : 5;
            pct += dir * step;
            pct = clamp_int(pct, 0, 100);
            *val255 = percent_to_vol255(pct);
        };

        item.hasLeft = [val255, &vol255_to_percent]() {
            return vol255_to_percent(*val255) > 0;
        };
        item.hasRight = [val255, &vol255_to_percent]() {
            return vol255_to_percent(*val255) < 100;
        };

        item.onActivate = []() {};
        m_items.push_back(item);
    };

    // Clamp before building so the display starts in a sane state.
    config.mainvol  = clamp_int(config.mainvol,  0, 255);
    config.pokeyvol = clamp_int(config.pokeyvol, 0, 255);
    config.noisevol = clamp_int(config.noisevol, 0, 255);

    addVolItemPercent("MAIN VOLUME",    &config.mainvol);
    addVolItemPercent("POKEY/AY VOLUME",&config.pokeyvol);
    addVolItemPercent("AMBIENT VOLUME", &config.noisevol);

    m_items.push_back(MenuItem::Bool("HV CHATTER", &config.hvnoise));
    m_items.push_back(MenuItem::Bool("PS HISS",    &config.pshiss));
    m_items.push_back(MenuItem::Bool("PS NOISE",   &config.psnoise));
}

void MenuManager::BuildDipSwitchMenu() {
    if (Machine->input_ports == nullptr) {
        m_items.push_back(MenuItem::Link("NO INPUT PORTS", []() {}));
        return;
    }

    InputPort* in = Machine->input_ports;
    while (in->type != IPT_END) {
        if ((in->type & ~IPF_MASK) == IPT_DIPSWITCH_NAME &&
            input_port_name(in) != nullptr &&
            !(in->type & IPF_UNUSED) &&
            !(!options.cheat && (in->type & IPF_CHEAT)))
        {
            MenuItem item;
            item.label = input_port_name(in);
            UINT16 mask = in->mask;

            // Collect all valid settings for this dipswitch.
            auto getSettings = [in, mask]() {
                std::vector<InputPort*> settings;
                InputPort* p = in + 1;
                while ((p->type & ~IPF_MASK) == IPT_DIPSWITCH_SETTING) {
                    if (!(!options.cheat && (p->type & IPF_CHEAT)))
                        settings.push_back(p);
                    p++;
                }
                return settings;
            };

            item.getValueDisplay = [in, mask]() {
                UINT16 currentVal = in->default_value & mask;
                InputPort* setting = in + 1;
                while ((setting->type & ~IPF_MASK) == IPT_DIPSWITCH_SETTING) {
                    if ((setting->default_value & mask) == currentVal) {
                        if (input_port_name(setting))
                            return std::string(input_port_name(setting));
                        else if (setting->name)
                            return std::string(setting->name);
                    }
                    setting++;
                }
                return std::string("INVALID");
            };

            item.onAdjust = [in, mask, getSettings](int dir) {
                auto settings = getSettings();
                if (settings.empty()) return;

                UINT16 currentVal = in->default_value & mask;
                int curIdx = 0;
                for (size_t i = 0; i < settings.size(); ++i) {
                    if ((settings[i]->default_value & mask) == currentVal) {
                        curIdx = (int)i;
                        break;
                    }
                }

                curIdx += dir;
                if (curIdx >= (int)settings.size()) curIdx = (int)settings.size() - 1;
                if (curIdx < 0) curIdx = 0;

                in->default_value = (in->default_value & ~mask) |
                                    (settings[curIdx]->default_value & mask);
            };

            item.hasLeft = [in, mask, getSettings]() {
                auto settings = getSettings();
                if (settings.empty()) return false;
                UINT16 currentVal = in->default_value & mask;
                return (settings[0]->default_value & mask) != currentVal;
            };

            item.hasRight = [in, mask, getSettings]() {
                auto settings = getSettings();
                if (settings.empty()) return false;
                UINT16 currentVal = in->default_value & mask;
                return (settings.back()->default_value & mask) != currentVal;
            };

            item.onActivate = []() {};
            m_items.push_back(item);
        }
        in++;
    }

    if (m_items.empty()) {
        m_items.push_back(MenuItem::Link("NO DIPSWITCHES FOUND", []() {}));
    }
}

// ----------------------------------------------------------------------
// INPUT DEVICES: per-player physical device assignment (multi-mouse /
// multi-keyboard). Machine-level settings, saved globally to aae.ini
// [input] on leaving the page.
// ----------------------------------------------------------------------
void MenuManager::BuildInputDevicesMenu() {
    // ------------------------------------------------------------------
    // Per-player mouse device assignment (multi-mouse).
    // Cycle: NONE (-2) / SYSTEM = all mice merged (-1) / MOUSE 1..N.
    // Specific devices are remembered by PATH (stable identity), so the
    // assignment survives reboots and enumeration-order changes; the index
    // shown is just where the device happens to sit this session.
    // Saved to aae.ini [input] mouse_player1..4(+_path) on leaving.
    // ------------------------------------------------------------------

    // effective selection: -2 none, -1 system, 0.. current index of the
    // path-assigned device; -3 = path assigned but device not attached
    auto effective_mouse = [](int p) -> int {
        if (config.mouse_player_path[p][0]) {
            int dev = RawInput_FindMouseByPath(config.mouse_player_path[p]);
            return (dev >= 0) ? dev : -3;
        }
        int v = config.mouse_player[p];
        return (v < -2) ? -2 : v;
    };
    auto assign_mouse = [](int p, int v) {
        config.mouse_player[p] = v;
        if (v >= 0)
            aae_strncpy(config.mouse_player_path[p], sizeof(config.mouse_player_path[p]),
                      RawInput_GetMousePath(v));
        else
            config.mouse_player_path[p][0] = 0;
    };

    for (int p = 0; p < 4; p++) {
        MenuItem mi;
        mi.label = "Player " + std::to_string(p + 1) + " Mouse";
        mi.getValueDisplay = [p, effective_mouse]() -> std::string {
            int v = effective_mouse(p);
            if (v == -3) return "ASSIGNED DEVICE NOT ATTACHED";
            if (v == -2) return "NONE";
            if (v == -1) return "SYSTEM (ALL MICE)";
            std::string s = "MOUSE " + std::to_string(v + 1) + ": " + RawInput_GetMouseName(v);
            // '*' = this device has actually sent input (wiggle a mouse
            // to identify it; phantom keyboard collections never move)
            if (RawInput_MouseSeenInput(v)) s += " *";
            return s;
        };
        mi.onAdjust = [p, effective_mouse, assign_mouse](int dir) {
            int v = effective_mouse(p);
            if (v == -3) v = -2;            // adjusting a missing device clears it
            v += dir;
            int hi = RawInput_GetMouseCount() - 1;
            if (v < -2) v = -2;
            if (v > hi) v = hi;
            assign_mouse(p, v);
        };
        mi.hasLeft  = [p, effective_mouse]() { return effective_mouse(p) != -2; };
        mi.hasRight = [p, effective_mouse]() {
            int v = effective_mouse(p);
            return v == -3 || v < RawInput_GetMouseCount() - 1;
        };
        mi.onActivate = []() {};
        m_items.push_back(mi);
    }

    // ------------------------------------------------------------------
    // Per-player keyboard device assignment (multi-keyboard), same scheme.
    // Default is SYSTEM for everyone (several players sharing one keyboard,
    // the classic model). Assign specific devices for multi-encoder setups
    // (e.g. two Ultimarc I-PACs); coin/start follow player 1's keyboard,
    // UI/menu keys always work from any keyboard.
    // ------------------------------------------------------------------
    auto effective_kbd = [](int p) -> int {
        if (config.kbd_player_path[p][0]) {
            int dev = RawInput_FindKeyboardByPath(config.kbd_player_path[p]);
            return (dev >= 0) ? dev : -3;
        }
        int v = config.kbd_player[p];
        return (v < -2) ? -2 : v;
    };
    auto assign_kbd = [](int p, int v) {
        config.kbd_player[p] = v;
        if (v >= 0)
            aae_strncpy(config.kbd_player_path[p], sizeof(config.kbd_player_path[p]),
                      RawInput_GetKeyboardPath(v));
        else
            config.kbd_player_path[p][0] = 0;
    };

    for (int p = 0; p < 4; p++) {
        MenuItem mi;
        mi.label = "Player " + std::to_string(p + 1) + " Keyboard";
        mi.getValueDisplay = [p, effective_kbd]() -> std::string {
            int v = effective_kbd(p);
            // routing falls back to ALL KEYBOARDS while the device is away
            if (v == -3) return "NOT ATTACHED (USING ALL KBDS)";
            if (v == -2) return "NONE";
            if (v == -1) return "SYSTEM (ALL KEYBOARDS)";
            std::string s = "KBD " + std::to_string(v + 1) + ": " + RawInput_GetKeyboardName(v);
            // '*' = this keyboard has actually sent a key (press one to identify)
            if (RawInput_KeyboardSeenInput(v)) s += " *";
            return s;
        };
        mi.onAdjust = [p, effective_kbd, assign_kbd](int dir) {
            int v = effective_kbd(p);
            if (v == -3) v = -2;            // adjusting a missing device clears it
            v += dir;
            int hi = RawInput_GetKeyboardCount() - 1;
            if (v < -2) v = -2;
            if (v > hi) v = hi;
            assign_kbd(p, v);
        };
        mi.hasLeft  = [p, effective_kbd]() { return effective_kbd(p) != -2; };
        mi.hasRight = [p, effective_kbd]() {
            int v = effective_kbd(p);
            return v == -3 || v < RawInput_GetKeyboardCount() - 1;
        };
        mi.onActivate = []() {};
        m_items.push_back(mi);
    }

    // ------------------------------------------------------------------
    // Per-player joystick assignment. AUTO = stick N drives player N (the
    // legacy default). Specific devices are remembered by stable id --
    // DirectInput instance GUID for generic sticks (survives reboots and
    // enumeration-order changes; twin Ultimarc sticks stay pinned), XInput
    // slot for pads.
    // ------------------------------------------------------------------
    auto effective_joy = [](int p) -> int {
        if (config.joy_player_id[p][0]) {
            int j = joystick_find_by_id(config.joy_player_id[p]);
            return (j >= 0) ? j : -3;   // -3 = assigned but not attached
        }
        int v = config.joy_player[p];
        return (v < -2) ? -2 : v;
    };
    auto assign_joy = [](int p, int v) {
        config.joy_player[p] = v;
        if (v >= 0)
            aae_strncpy(config.joy_player_id[p], sizeof(config.joy_player_id[p]),
                      joystick_get_id(v));
        else
            config.joy_player_id[p][0] = 0;
    };

    for (int p = 0; p < 4; p++) {
        MenuItem mi;
        mi.label = "Player " + std::to_string(p + 1) + " Joystick";
        mi.getValueDisplay = [p, effective_joy]() -> std::string {
            int v = effective_joy(p);
            if (v == -3) return "ASSIGNED DEVICE NOT ATTACHED";
            if (v == -2) return "NONE";
            if (v == -1) return "AUTO (JOY " + std::to_string(p + 1) + ")";
            std::string s = "JOY " + std::to_string(v + 1) + ": " + joystick_get_display_name(v);
            s += joystick_is_connected(v) ? " *" : " (NOT CONNECTED)";
            return s;
        };
        mi.onAdjust = [p, effective_joy, assign_joy](int dir) {
            int v = effective_joy(p);
            if (v == -3) v = -2;            // adjusting a missing device clears it
            v += dir;
            int hi = joystick_device_count() - 1;
            if (v < -2) v = -2;
            if (v > hi) v = hi;
            assign_joy(p, v);
        };
        mi.hasLeft  = [p, effective_joy]() { return effective_joy(p) != -2; };
        mi.hasRight = [p, effective_joy]() {
            int v = effective_joy(p);
            return v == -3 || v < joystick_device_count() - 1;
        };
        mi.onActivate = []() {};
        m_items.push_back(mi);
    }
}

void MenuManager::BuildAnalogMenu() {
    if (Machine->input_ports == nullptr) {
        m_items.push_back(MenuItem::Link("NO INPUT PORTS", []() {}));
        return;
    }

    InputPort* in = Machine->input_ports;
    while (in->type != IPT_END) {
        int type = in->type & 0xFF;
        if (type > IPT_ANALOG_START && type < IPT_ANALOG_END &&
            !(!options.cheat && (in->type & IPF_CHEAT)))
        {
            // Speed / delta
            MenuItem deltaItem;
            deltaItem.label = std::string(input_port_name(in)) + " Speed";
            deltaItem.getValueDisplay = [in]() { return std::to_string(IP_GET_DELTA(in)); };
            deltaItem.onAdjust = [in](int dir) {
                int val = IP_GET_DELTA(in) + dir;
                if (val < 1)   val = 1;
                if (val > 255) val = 255;
                IP_SET_DELTA(in, val);
            };
            deltaItem.hasLeft  = [in]() { return IP_GET_DELTA(in) > 1;   };
            deltaItem.hasRight = [in]() { return IP_GET_DELTA(in) < 255; };
            deltaItem.onActivate = []() {};
            m_items.push_back(deltaItem);

            // Sensitivity
            MenuItem sensItem;
            sensItem.label = std::string(input_port_name(in)) + " Sensitivity";
            sensItem.getValueDisplay = [in]() { return std::to_string(IP_GET_SENSITIVITY(in)) + "%"; };
            sensItem.onAdjust = [in](int dir) {
                int val = IP_GET_SENSITIVITY(in) + dir;
                if (val < 1)   val = 1;
                if (val > 255) val = 255;
                IP_SET_SENSITIVITY(in, val);
            };
            sensItem.hasLeft  = [in]() { return IP_GET_SENSITIVITY(in) > 1;   };
            sensItem.hasRight = [in]() { return IP_GET_SENSITIVITY(in) < 255; };
            sensItem.onActivate = []() {};
            m_items.push_back(sensItem);

            // Reverse toggle
            MenuItem revItem;
            revItem.label = std::string(input_port_name(in)) + " Reverse";
            revItem.getValueDisplay = [in]() { return (in->type & IPF_REVERSE) ? "ON" : "OFF"; };
            revItem.onAdjust = [in](int dir) {
                int isRev = (in->type & IPF_REVERSE) ? 1 : 0;
                int newVal = isRev + dir;
                if (newVal < 0) newVal = 0;
                if (newVal > 1) newVal = 1;
                if (newVal) in->type |=  IPF_REVERSE;
                else        in->type &= ~IPF_REVERSE;
            };
            revItem.hasLeft  = [in]() { return (in->type & IPF_REVERSE) != 0; };
            revItem.hasRight = [in]() { return (in->type & IPF_REVERSE) == 0; };
            revItem.onActivate = []() {};
            m_items.push_back(revItem);
        }
        in++;
    }

    if (m_items.empty()) {
        m_items.push_back(MenuItem::Link("NO ANALOG CONTROLS", []() {}));
    }
}

void MenuManager::BuildInputMenu(bool isGlobal, bool isJoystick) {
    if (isGlobal) {
        struct ipd* in = inputport_defaults;
        // Visual section dividers (keyboard view only) separating UI hotkeys
        // from game inputs. Joystick view skips this — UI hotkeys have
        // joystick=0 and the filter below excludes them entirely.
        bool emitted_ui_header   = false;
        bool emitted_game_header = false;
        while (in->type != IPT_END) {
            if (!isJoystick) {
                if (!emitted_ui_header && is_ui_input_type(in->type)) {
                    m_items.push_back(MenuItem::Disabled("--- UI HOTKEYS ---"));
                    emitted_ui_header = true;
                }
                else if (emitted_ui_header && !emitted_game_header
                         && !is_ui_input_type(in->type)
                         && in->name && in->keyboard != IP_KEY_NONE) {
                    m_items.push_back(MenuItem::Disabled("--- GAME INPUTS ---"));
                    emitted_game_header = true;
                }
            }
            if (in->name &&
                !(in->type & IPF_UNUSED) &&
                !(!options.cheat && (in->type & IPF_CHEAT)) &&
                ((isJoystick  && in->joystick != IP_JOY_NONE) ||
                 (!isJoystick && in->keyboard  != IP_KEY_NONE)))
            {
                MenuItem item;
                item.label = in->name;
                item.getValueDisplay = [in, isJoystick]() {
                    if (isJoystick) return std::string(osd_joy_name(in->joystick));
                    return GetKeyName(in->keyboard);
                };
                item.onActivate = [this, in, isJoystick]() {
                    m_isPolling    = true;
                    m_pollingIsJoy = isJoystick;
                    m_inputAssignmentHandler = [in, isJoystick](int code) {
                        // No osd_key_invalid() filter — UI hotkeys are now
                        // rebindable via the same menu, so no keys are reserved.
                        // Conflicts (binding the same key to two actions) make
                        // both actions fire.
                        if (isJoystick) in->joystick = code;
                        else            in->keyboard = code;
                    };
                };
                item.onAdjust = [in, isJoystick](int dir) {
                    // Left/Right clears the assignment for this input.
                    if (isJoystick) in->joystick = 0;
                    else            in->keyboard  = 0;
                };
                // Key/joy input rows do not have a left/right range to arrow through.
                item.hasLeft  = []() { return false; };
                item.hasRight = []() { return false; };
                m_items.push_back(item);
            }
            in++;
        }
    }
    else {
        if (Machine->input_ports == nullptr) {
            m_items.push_back(MenuItem::Link("NO INPUT PORTS", []() {}));
            return;
        }

        InputPort* in = Machine->input_ports;
        while (in->type != IPT_END) {
            if (input_port_name(in) &&
                ((isJoystick  && input_port_joy(in) != IP_JOY_NONE) ||
                 (!isJoystick && input_port_key(in) != IP_KEY_NONE)))
            {
                MenuItem item;
                item.label = input_port_name(in);
                item.getValueDisplay = [in, isJoystick]() {
                    if (isJoystick) return std::string(osd_joy_name(input_port_joy(in)));
                    return GetKeyName(input_port_key(in));
                };
                item.onActivate = [this, in, isJoystick]() {
                    m_isPolling    = true;
                    m_pollingIsJoy = isJoystick;
                    m_inputAssignmentHandler = [in, isJoystick](int code) {
                        // No osd_key_invalid() filter — see global menu above.
                        if (isJoystick) in->joystick = code;
                        else            in->keyboard = code;
                    };
                };
                item.onAdjust = [in, isJoystick](int dir) {
                    if (isJoystick) in->joystick = 0;
                    else            in->keyboard  = 0;
                };
                item.hasLeft  = []() { return false; };
                item.hasRight = []() { return false; };
                m_items.push_back(item);
            }
            in++;
        }
    }
}

// ----------------------------------------------------------------------
// Drawing & Navigation
// ----------------------------------------------------------------------

void MenuManager::Draw() {
    if (!m_showMenu) return;

    std::string title = GetTitleText();

    // ---- Compute visible item count and scroll window ----
    int count = (int)m_items.size();
    if (m_selectedIndex < m_scrollOffset)
        m_scrollOffset = m_selectedIndex;
    if (m_selectedIndex >= m_scrollOffset + VISIBLE_ITEMS)
        m_scrollOffset = m_selectedIndex - VISIBLE_ITEMS + 1;

    int visibleCount = (std::min)(count - m_scrollOffset, VISIBLE_ITEMS);
    if (visibleCount < 0) visibleCount = 0;

    // ---- Layout using cached max width ----
    // All pages share the standard left margin; overlong values (HID device
    // names) marquee-scroll within the value field instead of widening it.
    const float menuX = MENU_X;

    float topY        = TITLE_Y + PAD_TOP;
    float firstItemY  = TITLE_Y - TITLE_GAP - MENU_LINE_HEIGHT;
    float lastItemY   = firstItemY - ((visibleCount > 0 ? visibleCount - 1 : 0) * MENU_LINE_HEIGHT);

    float contentBottomY = lastItemY;
    if (m_isPolling) {
        contentBottomY -= (POLL_GAP + MENU_LINE_HEIGHT);
    }
    float footerY  = contentBottomY - FOOTER_GAP;
    float bottomY  = footerY - PAD_BOTTOM;

    float maxTextWidth = m_cachedMaxWidth;
    float leftX        = menuX - PAD_LEFT;
    float rightX       = menuX + maxTextWidth + PAD_RIGHT;
    float bgCenterX    = (leftX + rightX) * 0.5f;
    float bgCenterY    = (topY + bottomY) * 0.5f;
    float bgWidth      = rightX - leftX;
    float bgHeight     = topY - bottomY;

    // ---- Background + Title ----
    VF.DrawQuad(bgCenterX, bgCenterY, bgWidth, bgHeight, MAKE_RGBA(20, 20, 80, 255));
    VF.Print(menuX, (int)TITLE_Y, RGB_WHITE, FONT_SCALE, title.c_str());

    // ---- Scroll indicator positions ----
    float scrollArrowX   = rightX - (PAD_RIGHT * 0.55f) - (CHAR_PITCH * 0.5f);
    float titleSafeBottomY = TITLE_Y - (TITLE_GAP * 0.25f);
    float footerSafeTopY   = footerY + (MENU_LINE_HEIGHT * 0.55f);

    float upArrowY = firstItemY + (MENU_LINE_HEIGHT * 0.85f);
    if (upArrowY > titleSafeBottomY) upArrowY = titleSafeBottomY;

    float downArrowY = lastItemY - (MENU_LINE_HEIGHT * 0.35f);
    if (downArrowY < footerSafeTopY) downArrowY = footerSafeTopY;

    if (m_scrollOffset > 0) {
        VF.Print(scrollArrowX, (int)upArrowY, RGB_YELLOW, FONT_SCALE, "\x1E"); // UP TRI
    }

    constexpr float ARROW_GAP = 5.0f;

    // ---- Draw visible items ----
    float y = firstItemY;
    for (int i = m_scrollOffset; i < m_scrollOffset + visibleCount; ++i) {
        const MenuItem& it = m_items[i];
        bool isSelected = (i == m_selectedIndex);

        // Disabled items always draw gray regardless of selection.
        unsigned int color;
        if (it.disabled()) {
            color = RGB_DISABLED;
        }
        else {
            color = isSelected ? RGB_PINK : RGB_WHITE;
        }

        VF.Print(menuX, (int)y, color, FONT_SCALE, it.label.c_str());

        if (!it.isLink && it.getValueDisplay) {
            if (it.disabled()) {
                // Show the reason string (e.g. "NOT LOADED") without arrows.
                VF.Print(menuX + VALUE_X_OFFSET, (int)y,
                    RGB_DISABLED, FONT_SCALE, it.disabledReason.c_str());
            }
            else {
                std::string val = it.getValueDisplay();

                // Width of the value as drawn; overlong values marquee inside
                // a VALUE_W_MAX field, so nothing downstream exceeds it.
                float valW = (float)val.length() * CHAR_PITCH;
                if (valW > VALUE_W_MAX) valW = VALUE_W_MAX;

                if (isSelected) {
                    // Left arrow
                    if (it.hasLeft && it.hasLeft()) {
                        float arrowLeftX = menuX + VALUE_X_OFFSET - ARROW_GAP - CHAR_PITCH;
                        VF.Print(arrowLeftX, (int)y, RGB_WHITE, FONT_SCALE, "<");
                    }

                    VF.PrintMarquee(menuX + VALUE_X_OFFSET, (int)y, VALUE_W_MAX, color, FONT_SCALE, "%s", val.c_str());

                    // Right arrow sits at the end of the value field
                    if (it.hasRight && it.hasRight()) {
                        float valEndX = menuX + VALUE_X_OFFSET + valW + ARROW_GAP;
                        VF.Print(valEndX, (int)y, RGB_WHITE, FONT_SCALE, ">");
                    }
                }
                else {
                    VF.PrintMarquee(menuX + VALUE_X_OFFSET, (int)y, VALUE_W_MAX, color, FONT_SCALE, "%s", val.c_str());
                }
            }
        }

        y -= MENU_LINE_HEIGHT;
    }

    // Down scroll indicator
    if (m_scrollOffset + visibleCount < count) {
        VF.Print(scrollArrowX, (int)downArrowY, RGB_YELLOW, FONT_SCALE, "\x1F"); // DOWN TRI
    }

    // ---- Input polling hint ----
    if (m_isPolling) {
        VF.Print(menuX, (int)(y - POLL_GAP), RGB_YELLOW, FONT_SCALE,
            "PRESS KEY/BUTTON OR ESC TO CANCEL");
    }

    // ---- Footer ----
    std::string footerText = GetFooterText();
    VF.Print(menuX, (int)footerY, RGB_YELLOW, FOOTER_SCALE, footerText.c_str());
}

void MenuManager::DrawBackground() {}
void MenuManager::DrawFooter()    {}

void MenuManager::Navigate(int dir) {
    if (m_isPolling) return;
    m_selectedIndex += dir;
    if (m_selectedIndex < 0) m_selectedIndex = 0;
    if (m_selectedIndex >= (int)m_items.size())
        m_selectedIndex = (int)m_items.size() - 1;
}

void MenuManager::Adjust(int dir) {
    if (m_isPolling) return;

    if (m_selectedIndex >= 0 && m_selectedIndex < (int)m_items.size()) {
        const MenuItem& it = m_items[m_selectedIndex];

        // Refuse to adjust disabled (unavailable art) items.
        if (it.disabled()) return;

        if (it.onAdjust) {
            it.onAdjust(dir);

            if (m_currentMenuId == MenuID::Video) {
                set_points_lines();
                setup_video_config();
            }
            else if (m_currentMenuId == MenuID::Audio) {
                // Apply master volume and ambient toggles immediately.
                AAE_ApplyAudioVolumesFromConfig(0);
                setup_ambient(0);
            }

            // Recalculate layout in case the value string length changed
            // (e.g. "NO" -> "YES", or "99" -> "100").
            RecalculateLayout();
        }
    }
}

void MenuManager::Select() {
    if (m_isPolling) return;
    if (m_selectedIndex >= 0 && m_selectedIndex < (int)m_items.size()) {
        // Block Enter on disabled items (e.g. a greyed-out submenu link).
        if (m_items[m_selectedIndex].disabled()) return;
        if (m_items[m_selectedIndex].onActivate) {
            m_items[m_selectedIndex].onActivate();
        }
    }
}

void MenuManager::PollInput() {
    if (!m_isPolling) return;

    // ESC / cancel during polling aborts the assignment.
    if (osd_key_pressed_memory(OSD_KEY_FAST_EXIT) ||
        osd_key_pressed_memory(OSD_KEY_CANCEL)    ||
        osd_key_pressed_memory(OSD_KEY_ESC))
    {
        m_isPolling = false;
        m_inputAssignmentHandler = nullptr;
        return;
    }

    int detectedCode = -1;

    if (m_pollingIsJoy) {
        for (int j = 1; j < OSD_MAX_JOY; ++j) {
            if (osd_joy_pressed(j)) {
                detectedCode = j;
                break;
            }
        }
    }
    else {
        int k = osd_read_key_immediate();
        if (k != OSD_KEY_NONE && k != OSD_KEY_PAUSE && k != OSD_KEY_ENTER) {
            detectedCode = k;
        }
    }

    if (detectedCode != -1) {
        if (m_inputAssignmentHandler) {
            m_inputAssignmentHandler(detectedCode);
        }
        m_isPolling = false;
        m_inputAssignmentHandler = nullptr;
        // Re-layout in case the key name string length changed.
        RecalculateLayout();
    }
}

// ----------------------------------------------------------------------
// First-Run Notice
// ----------------------------------------------------------------------
// Drawn from video_loop() on top of everything else, so it lands over the
// GUI frontend or over a game started from the command line without either
// of them needing to know it exists. See menu.h for the contract.

namespace {

    const char* const NOTICE_BODY =
        "A.A.E. is built using M.A.M.E source code and information collected "
        "over the last two decades by the dedicated and hard working people "
        "that sacrificed their time and used their knowledge to create "
        "something truly special. Thank you to everyone who has contributed "
        "over the years especially the vector teams. Please see credit.txt for "
        "more information.";

    const char* const NOTICE_PROMPT = "(press any key)";

    constexpr float NOTICE_SCALE      = 2.0f;
    constexpr float NOTICE_LINE_H     = 30.0f;
    constexpr float NOTICE_TEXT_W     = 720.0f;  // wrap width in VF units
    constexpr float NOTICE_PAD_X      = 40.0f;
    constexpr float NOTICE_PAD_TOP    = 34.0f;
    constexpr float NOTICE_PAD_BOTTOM = 22.0f;
    constexpr float NOTICE_CENTER_X   = 512.0f;  // VF space is always 1024x768
    constexpr float NOTICE_CENTER_Y   = 384.0f;

    // Greedy word wrap measured with the real proportional glyph widths, so
    // the panel is sized from what actually gets drawn rather than from a
    // character count. Built once on first use -- VF's font metrics come from
    // its constructor, so this is safe before the renderer is up.
    const std::vector<std::string>& NoticeLines() {
        static std::vector<std::string> lines;
        if (!lines.empty()) return lines;

        const std::string src(NOTICE_BODY);
        std::string cur;
        size_t i = 0;
        while (i < src.size()) {
            const size_t sp = src.find(' ', i);
            const std::string word = (sp == std::string::npos)
                ? src.substr(i)
                : src.substr(i, sp - i);
            i = (sp == std::string::npos) ? src.size() : sp + 1;
            if (word.empty()) continue;

            const std::string trial = cur.empty() ? word : cur + " " + word;
            if (!cur.empty() &&
                VF.GetStringPitch(trial.c_str(), NOTICE_SCALE, 0) > NOTICE_TEXT_W)
            {
                lines.push_back(cur);
                cur = word;
            }
            else {
                cur = trial;
            }
        }
        if (!cur.empty()) lines.push_back(cur);
        return lines;
    }

}  // namespace

// True while any key or any joystick fire button is physically down.
// key[] is the raw Allegro-style keystate both backends fill; only the
// aggregate JOYn_FIRE codes are polled so a drifting analog stick can
// never dismiss a panel on its own. Shared by the first-run notice and
// the controller guide (see menu.h).
int ui_any_input_down() {
    for (int k = 1; k <= AAEKEY_MAX; ++k) {
        if (key[k]) return 1;
    }
    return (osd_joy_pressed(OSD_JOY_FIRE)  || osd_joy_pressed(OSD_JOY2_FIRE) ||
            osd_joy_pressed(OSD_JOY3_FIRE) || osd_joy_pressed(OSD_JOY4_FIRE)) ? 1 : 0;
}

int first_run_notice_active() { return config.first_run ? 1 : 0; }

void do_the_first_run_notice() {
    if (!config.first_run) return;

    // ---- Dismissal ----
    // Arm only once nothing is held. A game started from a shell still has
    // the launching ENTER down on the first frame, and the GUI can reach
    // here with a button held; without this the panel would vanish before
    // it was ever readable.
    static bool s_armed = false;
    const bool anyDown = ui_any_input_down() != 0;

    if (!s_armed) {
        if (!anyDown) s_armed = true;
    }
    else if (anyDown) {
        config.first_run = 0;
        my_set_config_int("main", "first_run", 0, 0);  // path 0 = aae.ini
        LOG_INFO("First-run notice dismissed");
        return;
    }

    // ---- Layout ----
    const std::vector<std::string>& lines = NoticeLines();

    // Body lines, then a blank spacer, then the prompt.
    const int totalLines = (int)lines.size() + 2;

    const float blockH = (totalLines - 1) * NOTICE_LINE_H;
    const float firstY = NOTICE_CENTER_Y +
        ((blockH + NOTICE_PAD_BOTTOM - NOTICE_PAD_TOP) * 0.5f);

    const float panelTop    = firstY + NOTICE_PAD_TOP;
    const float panelBottom = firstY - blockH - NOTICE_PAD_BOTTOM;
    const float panelW      = NOTICE_TEXT_W + (NOTICE_PAD_X * 2.0f);
    const float panelH      = panelTop - panelBottom;

    const float textLeftX = NOTICE_CENTER_X - (NOTICE_TEXT_W * 0.5f);

    // ---- Draw ----
    VF.DrawQuad(NOTICE_CENTER_X, (panelTop + panelBottom) * 0.5f,
        panelW, panelH, MAKE_RGBA(20, 20, 80, 255));

    float y = firstY;
    for (const std::string& line : lines) {
        VF.Print(textLeftX, (int)y, RGB_WHITE, NOTICE_SCALE, "%s", line.c_str());
        y -= NOTICE_LINE_H;
    }

    y -= NOTICE_LINE_H;  // blank spacer line
    VF.PrintCentered((int)y, RGB_YELLOW, NOTICE_SCALE, NOTICE_PROMPT);
}

// ----------------------------------------------------------------------
// Legacy Free-Function Interface
// ----------------------------------------------------------------------

int  get_menu_status()        { return MenuManager::Instance().GetStatus(); }
void set_menu_status(int on)  { MenuManager::Instance().SetStatus(on);      }
int  get_menu_level()         { return MenuManager::Instance().GetLevel();   }
void set_menu_level_top()     { MenuManager::Instance().SetLevelTop();       }
void menu_navigate_back()     { MenuManager::Instance().NavigateBack();      }

void do_the_menu() {
    // If we are waiting for a key/joy assignment, service that first.
    if (MenuManager::Instance().IsPolling()) {
        MenuManager::Instance().PollInput();
    }
    MenuManager::Instance().Draw();
}

void change_menu_level(int dir) {
    // dir != 0 means "go deeper" (was used to push submenu); 0 means "back".
    MenuManager::Instance().Navigate(dir ? 1 : -1);
}

void change_menu_item(int dir) {
    MenuManager::Instance().Adjust(dir ? 1 : -1);
}

void select_menu_item() {
    MenuManager::Instance().Select();
}

void set_points_lines() {
   // glLineWidth(config.linewidth);
   // glPointSize(config.pointsize);
}
