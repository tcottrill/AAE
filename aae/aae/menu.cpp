#include "menu.h"
#include "framework.h"
#include "aae_mame_driver.h"
#include "opengl_renderer.h"
#include "vector_fonts.h"
#include "gl_texturing.h"
#include "inptport.h"
#include "deftypes.h"
#include "osdepend.h"
#include "os_input.h"
#include "config.h"
#include "colordefs.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>
#include <functional>
#include <algorithm>

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
        DipSwitch = 700,
        Video = 800,
        Audio = 900
    };

    // Constants for Layout
    constexpr float MENU_X          = 225.0f;
    constexpr float MENU_LINE_HEIGHT = 28.0f;
    constexpr int   VISIBLE_ITEMS   = 16;
    constexpr float VALUE_X_OFFSET  = 350.0f;

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
    void BuildSoundMenu();
    void BuildDipSwitchMenu();
    void BuildAnalogMenu();
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
    if (m_currentMenuId == MenuID::GlobalKeys) return "KEY CONFIG (GLOBAL)";
    if (m_currentMenuId == MenuID::LocalKeys)  return "KEY CONFIG (THIS GAME)";
    if (m_currentMenuId == MenuID::GlobalJoy)  return "JOY CONFIG (GLOBAL)";
    if (m_currentMenuId == MenuID::LocalJoy)   return "JOY CONFIG (THIS GAME)";
    if (m_currentMenuId == MenuID::Analog)     return "ANALOG SETTINGS";
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
            std::string val = item.isDisabled
                ? item.disabledReason   // use the reason string for width calc
                : item.getValueDisplay();
            totalW = VALUE_X_OFFSET + (float)val.length() * CHAR_PITCH + ARROW_EXTRA;
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
        my_set_config_int("window", "fullscreen", config.windowed, 0);
        my_set_config_string("window", "aspect_ratio",
            config.aspect ? config.aspect : "4:3", 0);
        my_set_config_int("main", "screenw",     config.screenw,   0);
        my_set_config_int("main", "screenh",     config.screenh,   0);
        my_set_config_int("main", "gamma",       config.gamma,     0);
        my_set_config_int("main", "bright",      config.bright,    0);
        my_set_config_int("main", "contrast",    config.contrast,  0);
        my_set_config_int("main", "force_vsync", config.forcesync, 0);
        my_set_config_int("main", "drawzero",    config.drawzero,  0);
        my_set_config_int("main", "widescreen",  config.widescreen,0);
        my_set_config_int("main", "priority",    config.priority,  0);
        my_set_config_int("main", "kbleds",      config.kbleds,    0);

        // Game-specific visual overrides.
        // In GUI mode these also go to aae.ini (path 0) so they become the
        // new global defaults rather than being silently written to gui.ini.
        const int vidPath = inGui ? 0 : gamenum;
        my_set_config_int("main", "vectortrail", config.vectrail, vidPath);
        my_set_config_int("main", "vectorglow",  config.vecglow,  vidPath);
        my_set_config_int("main", "m_line",      config.m_line,   vidPath);
        my_set_config_int("main", "m_point",     config.m_point,  vidPath);
        my_set_config_int("main", "gain",        config.gain,     vidPath);
        my_set_config_int("main", "artwork",     config.artwork,  vidPath);
        my_set_config_int("main", "overlay",     config.overlay,  vidPath);
        my_set_config_int("main", "bezel",       config.bezel,    vidPath);
        my_set_config_int("main", "artcrop",     config.artcrop,  vidPath);
        my_set_config_string("main", "raster_effect",
            config.raster_effect ? config.raster_effect : "NONE", vidPath);
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
    case MenuID::Audio:      BuildSoundMenu();             break;
    case MenuID::DipSwitch:  BuildDipSwitchMenu();         break;
    case MenuID::Analog:     BuildAnalogMenu();            break;
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
    m_items.push_back(MenuItem::Link("DIPSWITCHES", [this]() { TransitionTo(MenuID::DipSwitch);  }));
    m_items.push_back(MenuItem::Link(inGui ? "VIDEO SETUP (GLOBAL)" : "VIDEO SETUP",
        [this]() { TransitionTo(MenuID::Video);      }));
    m_items.push_back(MenuItem::Link(inGui ? "SOUND SETUP (GLOBAL)" : "SOUND SETUP",
        [this]() { TransitionTo(MenuID::Audio);      }));
}

void MenuManager::BuildVideoMenu() {
    m_items.push_back(MenuItem::Bool("FULLSCREEN", &config.windowed));

    // Window aspect ratio stored as a stable static string so config.aspect
    // always points to valid memory while the menu is alive.
    static std::string s_windowAspect;
    if (config.aspect && config.aspect[0])
        s_windowAspect = config.aspect;
    else
        s_windowAspect = "4:3";

    m_items.push_back(MenuItem::String(
        "WINDOW ASPECT",
        &s_windowAspect,
        { "4:3", "5:4", "6:5", "16:10", "16:9" },
        [](const std::string& v) { config.aspect = (char*)v.c_str(); }
    ));

    // Resolution preset binding. Static so the index survives redraws.
    static int resIndex = 0;
    if (config.screenw == 1024) resIndex = 0;
    else if (config.screenw == 1152) resIndex = 1;
    else if (config.screenw == 1280) resIndex = 2;
    else if (config.screenw == 1600) resIndex = 3;
    else if (config.screenw == 1920) resIndex = 4;

    MenuItem resItem;
    resItem.label = "RESOLUTION";
    resItem.getValueDisplay = []() {
        const char* names[] = { "1024x768", "1152x864", "1280x1024", "1600x1200", "1920x1080" };
        return std::string(names[std::clamp(resIndex, 0, 4)]);
        };
    resItem.onAdjust = [](int dir) {
        resIndex += dir;
        if (resIndex > 4) resIndex = 4;
        if (resIndex < 0) resIndex = 0;
        switch (resIndex) {
        case 0: config.screenw = 1024; config.screenh = 768;  break;
        case 1: config.screenw = 1152; config.screenh = 864;  break;
        case 2: config.screenw = 1280; config.screenh = 1024; break;
        case 3: config.screenw = 1600; config.screenh = 1200; break;
        case 4: config.screenw = 1920; config.screenh = 1080; break;
        }
        };
    resItem.hasLeft = []() { return resIndex > 0; };
    resItem.hasRight = []() { return resIndex < 4; };
    resItem.onActivate = []() {};
    m_items.push_back(resItem);

    // ------------------------------------------------------------------
    // The following five settings exist in config and are saved/loaded
    // correctly, but are not yet connected to the rendering pipeline.
    // They show as DISABLED so the user knows they are not active yet.
    // To re-enable any of them once the rendering code is wired up,
    // swap the Disabled() call for the commented-out original below it.
    // ------------------------------------------------------------------
    m_items.push_back(MenuItem::Disabled("GAMMA"));
    // m_items.push_back(MenuItem::Integer("GAMMA",      &config.gamma,    50, 200));

    m_items.push_back(MenuItem::Disabled("BRIGHTNESS"));
    // m_items.push_back(MenuItem::Integer("BRIGHTNESS", &config.bright,   50, 200));

    m_items.push_back(MenuItem::Disabled("CONTRAST"));
    // m_items.push_back(MenuItem::Integer("CONTRAST",   &config.contrast, 50, 200));

    m_items.push_back(MenuItem::Disabled("VSYNC"));
    // m_items.push_back(MenuItem::Bool("VSYNC",         &config.forcesync));

    m_items.push_back(MenuItem::Disabled("DRAW 0 LINES"));
    // m_items.push_back(MenuItem::Bool("DRAW 0 LINES",  &config.drawzero));

    m_items.push_back(MenuItem::Integer("GAME ASPECT", &config.widescreen, 0, 3,
        { "4:3", "5:4", "14:10", "16:9" }));
    m_items.push_back(MenuItem::Integer("PHOSPHOR TRAIL", &config.vectrail, 0, 3,
        { "NONE", "LITTLE", "MORE", "MAX" }));
    m_items.push_back(MenuItem::Integer("VECTOR GLOW", &config.vecglow, 0, 25));

    {
        MenuItem lwItem;
        lwItem.label = "LINEWIDTH";
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
        MenuItem psItem;
        psItem.label = "POINTSIZE";
        psItem.getValueDisplay = []() {
            char buf[32];
            snprintf(buf, 32, "%2.1f", config.m_point * 0.1f);
            return std::string(buf);
            };
        psItem.onAdjust = [](int dir) {
            config.m_point += dir;
            if (config.m_point < 10) config.m_point = 10;
            if (config.m_point > 70) config.m_point = 70;
            config.pointsize = config.m_point * 0.1f;
            };
        psItem.hasLeft = []() { return config.m_point > 10; };
        psItem.hasRight = []() { return config.m_point < 70; };
        psItem.onActivate = []() {};
        m_items.push_back(psItem);
    }

    m_items.push_back(MenuItem::Integer("MONITOR GAIN", &config.gain, -127, 127));

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

    // ------------------------------------------------------------------
    // Raster / scanlines overlay effect.
    // Cycles between "NONE" and known effect filenames stored under artwork\.
    // onChange live-reloads the texture so the change is visible immediately.
    // ------------------------------------------------------------------
    {
        static std::string s_rasterEffect;
        if (config.raster_effect && config.raster_effect[0])
            s_rasterEffect = config.raster_effect;
        else
            s_rasterEffect = "NONE";

        std::vector<std::string> rasterOptions = {
            "NONE", "aperture4x6.png", "scanlines.png", "scanrez2.png", "scanrez2r.png"
        };
        // Include any custom filename that came from the ini but is not in our list.
        bool alreadyListed = false;
        for (const auto& opt : rasterOptions) {
            if (opt == s_rasterEffect) { alreadyListed = true; break; }
        }
        if (!alreadyListed)
            rasterOptions.push_back(s_rasterEffect);

        m_items.push_back(MenuItem::String(
            "RASTER EFFECT",
            &s_rasterEffect,
            rasterOptions,
            [](const std::string& v) {
                config.raster_effect = (char*)v.c_str();
                // Live reload so the change is visible without restarting.
                init_raster_overlay();
            }
        ));
    }

    m_items.push_back(MenuItem::Integer("PRIORITY", &config.priority, 0, 4,
        { "LOW", "NORMAL", "ABOVE NORMAL", "HIGH", "REALTIME" }));

    m_items.push_back(MenuItem::Bool("KB LEDS", &config.kbleds));
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
        while (in->type != IPT_END) {
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
                        if (isJoystick) in->joystick = code;
                        else {
                            if (osd_key_invalid(code)) code = IP_KEY_DEFAULT;
                            in->keyboard = code;
                        }
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
                        if (isJoystick) in->joystick = code;
                        else {
                            if (osd_key_invalid(code)) code = IP_KEY_DEFAULT;
                            in->keyboard = code;
                        }
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
    float leftX        = MENU_X - PAD_LEFT;
    float rightX       = MENU_X + maxTextWidth + PAD_RIGHT;
    float bgCenterX    = (leftX + rightX) * 0.5f;
    float bgCenterY    = (topY + bottomY) * 0.5f;
    float bgWidth      = rightX - leftX;
    float bgHeight     = topY - bottomY;

    // ---- Background + Title ----
    VF.DrawQuad(bgCenterX, bgCenterY, bgWidth, bgHeight, MAKE_RGBA(20, 20, 80, 255));
    VF.Print(MENU_X, (int)TITLE_Y, RGB_WHITE, FONT_SCALE, title.c_str());

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
        if (it.isDisabled) {
            color = RGB_DISABLED;
        }
        else {
            color = isSelected ? RGB_PINK : RGB_WHITE;
        }

        VF.Print(MENU_X, (int)y, color, FONT_SCALE, it.label.c_str());

        if (!it.isLink && it.getValueDisplay) {
            if (it.isDisabled) {
                // Show the reason string (e.g. "NOT LOADED") without arrows.
                VF.Print(MENU_X + VALUE_X_OFFSET, (int)y,
                    RGB_DISABLED, FONT_SCALE, it.disabledReason.c_str());
            }
            else {
                std::string val = it.getValueDisplay();

                if (isSelected) {
                    // Left arrow
                    if (it.hasLeft && it.hasLeft()) {
                        float arrowLeftX = MENU_X + VALUE_X_OFFSET - ARROW_GAP - CHAR_PITCH;
                        VF.Print(arrowLeftX, (int)y, RGB_WHITE, FONT_SCALE, "<");
                    }

                    VF.Print(MENU_X + VALUE_X_OFFSET, (int)y, color, FONT_SCALE, val.c_str());

                    // Right arrow
                    if (it.hasRight && it.hasRight()) {
                        float valEndX = MENU_X + VALUE_X_OFFSET + (float)val.length() * CHAR_PITCH + ARROW_GAP;
                        VF.Print(valEndX, (int)y, RGB_WHITE, FONT_SCALE, ">");
                    }
                }
                else {
                    VF.Print(MENU_X + VALUE_X_OFFSET, (int)y, color, FONT_SCALE, val.c_str());
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
        VF.Print(MENU_X, (int)(y - POLL_GAP), RGB_YELLOW, FONT_SCALE,
            "PRESS KEY/BUTTON OR ESC TO CANCEL");
    }

    // ---- Footer ----
    std::string footerText = GetFooterText();
    VF.Print(MENU_X, (int)footerY, RGB_YELLOW, FOOTER_SCALE, footerText.c_str());
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
        if (it.isDisabled) return;

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
        // Allow activating disabled items as a no-op (onActivate is [](){} anyway).
        // If you want to block Enter on disabled items, add: if (m_items[...].isDisabled) return;
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
// Legacy Free-Function Interface
// ----------------------------------------------------------------------

int  get_menu_status()        { return MenuManager::Instance().GetStatus(); }
void set_menu_status(int on)  { MenuManager::Instance().SetStatus(on);      }
int  get_menu_level()         { return MenuManager::Instance().GetLevel();   }
void set_menu_level_top()     { MenuManager::Instance().SetLevelTop();       }

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
    glLineWidth(config.linewidth);
    glPointSize(config.pointsize);
}
