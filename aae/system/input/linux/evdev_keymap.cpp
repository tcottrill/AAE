//==============================================================================
// evdev_keymap.cpp -- the KEY_* -> AaeKey table.
//
// A translation table is exactly the kind of thing that compiles perfectly and
// is quietly wrong: a mistyped entry produces a key that does the wrong thing,
// and no build check catches it. Hence the static_asserts below, and hence the
// startup coverage report.
//==============================================================================
#include "evdev_keymap.h"

#include "sys_input.h"
#include "sys_log.h"

#include <linux/input.h>

#include <cstring>

namespace {

//------------------------------------------------------------------------------
// The table has to be constexpr, not const.
//
// An element of a `const` array is not a constant expression, so the
// static_asserts at the bottom would fail to COMPILE against a const table
// (MSVC C2131; g++ agrees). Phase 3b hit precisely this with the TMS5220
// coefficient tables. Building it through a constexpr function is what lets it
// be sparse and constant at the same time.
//------------------------------------------------------------------------------
struct KeyTable { uint8_t v[KEY_MAX + 1]; };

constexpr KeyTable BuildKeyTable()
{
	KeyTable t{};   // value-initialised: every unmapped code is 0

	// --- letters ---------------------------------------------------------
	// Not a loop: evdev letter codes follow the keyboard rows (KEY_Q is 16,
	// KEY_A is 30, KEY_Z is 44), so they are not contiguous alphabetically and
	// there is no arithmetic relationship to AAEKEY_A..Z to exploit.
	t.v[KEY_A] = AAEKEY_A;  t.v[KEY_B] = AAEKEY_B;  t.v[KEY_C] = AAEKEY_C;
	t.v[KEY_D] = AAEKEY_D;  t.v[KEY_E] = AAEKEY_E;  t.v[KEY_F] = AAEKEY_F;
	t.v[KEY_G] = AAEKEY_G;  t.v[KEY_H] = AAEKEY_H;  t.v[KEY_I] = AAEKEY_I;
	t.v[KEY_J] = AAEKEY_J;  t.v[KEY_K] = AAEKEY_K;  t.v[KEY_L] = AAEKEY_L;
	t.v[KEY_M] = AAEKEY_M;  t.v[KEY_N] = AAEKEY_N;  t.v[KEY_O] = AAEKEY_O;
	t.v[KEY_P] = AAEKEY_P;  t.v[KEY_Q] = AAEKEY_Q;  t.v[KEY_R] = AAEKEY_R;
	t.v[KEY_S] = AAEKEY_S;  t.v[KEY_T] = AAEKEY_T;  t.v[KEY_U] = AAEKEY_U;
	t.v[KEY_V] = AAEKEY_V;  t.v[KEY_W] = AAEKEY_W;  t.v[KEY_X] = AAEKEY_X;
	t.v[KEY_Y] = AAEKEY_Y;  t.v[KEY_Z] = AAEKEY_Z;

	// --- digit row -------------------------------------------------------
	// KEY_1..KEY_9 run 2..10 and KEY_0 is 11, i.e. zero comes LAST, unlike
	// AAEKEY_0..AAEKEY_9 where it comes first.
	t.v[KEY_0] = AAEKEY_0;  t.v[KEY_1] = AAEKEY_1;  t.v[KEY_2] = AAEKEY_2;
	t.v[KEY_3] = AAEKEY_3;  t.v[KEY_4] = AAEKEY_4;  t.v[KEY_5] = AAEKEY_5;
	t.v[KEY_6] = AAEKEY_6;  t.v[KEY_7] = AAEKEY_7;  t.v[KEY_8] = AAEKEY_8;
	t.v[KEY_9] = AAEKEY_9;

	// --- function row ----------------------------------------------------
	// F1..F10 are 59..68 but F11/F12 are 87/88, far away.
	t.v[KEY_F1]  = AAEKEY_F1;   t.v[KEY_F2]  = AAEKEY_F2;
	t.v[KEY_F3]  = AAEKEY_F3;   t.v[KEY_F4]  = AAEKEY_F4;
	t.v[KEY_F5]  = AAEKEY_F5;   t.v[KEY_F6]  = AAEKEY_F6;
	t.v[KEY_F7]  = AAEKEY_F7;   t.v[KEY_F8]  = AAEKEY_F8;
	t.v[KEY_F9]  = AAEKEY_F9;   t.v[KEY_F10] = AAEKEY_F10;
	t.v[KEY_F11] = AAEKEY_F11;  t.v[KEY_F12] = AAEKEY_F12;

	// --- numeric keypad ---------------------------------------------------
	// evdev reports the physical keypad keys as KEY_KP* regardless of NumLock;
	// the kernel does no NumLock translation. That happens to match what the
	// Win32 backend produces, which remaps VK_HOME/VK_INSERT/... back to
	// VK_NUMPAD* when the E0 flag is clear (rawinput.cpp). Both platforms
	// therefore yield AAEKEY_7_PAD for the physical numpad 7, NumLock or not.
	t.v[KEY_KP0] = AAEKEY_0_PAD;  t.v[KEY_KP1] = AAEKEY_1_PAD;
	t.v[KEY_KP2] = AAEKEY_2_PAD;  t.v[KEY_KP3] = AAEKEY_3_PAD;
	t.v[KEY_KP4] = AAEKEY_4_PAD;  t.v[KEY_KP5] = AAEKEY_5_PAD;
	t.v[KEY_KP6] = AAEKEY_6_PAD;  t.v[KEY_KP7] = AAEKEY_7_PAD;
	t.v[KEY_KP8] = AAEKEY_8_PAD;  t.v[KEY_KP9] = AAEKEY_9_PAD;
	t.v[KEY_KPSLASH]    = AAEKEY_SLASH_PAD;
	t.v[KEY_KPASTERISK] = AAEKEY_ASTERISK;
	t.v[KEY_KPMINUS]    = AAEKEY_MINUS_PAD;
	t.v[KEY_KPPLUS]     = AAEKEY_PLUS_PAD;
	t.v[KEY_KPDOT]      = AAEKEY_DEL_PAD;     // VK_DECIMAL
	t.v[KEY_KPENTER]    = AAEKEY_ENTER_PAD;   // VK_SEPARATOR
	// KEY_KPEQUAL is deliberately NOT mapped - see the note on
	// AAEKEY_EQUALS_PAD below.

	// --- editing / navigation --------------------------------------------
	t.v[KEY_ESC]       = AAEKEY_ESC;
	t.v[KEY_ENTER]     = AAEKEY_ENTER;
	t.v[KEY_SPACE]     = AAEKEY_SPACE;
	t.v[KEY_TAB]       = AAEKEY_TAB;
	t.v[KEY_BACKSPACE] = AAEKEY_BACKSPACE;
	t.v[KEY_INSERT]    = AAEKEY_INSERT;
	t.v[KEY_DELETE]    = AAEKEY_DEL;
	t.v[KEY_HOME]      = AAEKEY_HOME;
	t.v[KEY_END]       = AAEKEY_END;
	t.v[KEY_PAGEUP]    = AAEKEY_PGUP;
	t.v[KEY_PAGEDOWN]  = AAEKEY_PGDN;
	t.v[KEY_LEFT]      = AAEKEY_LEFT;
	t.v[KEY_RIGHT]     = AAEKEY_RIGHT;
	t.v[KEY_UP]        = AAEKEY_UP;
	t.v[KEY_DOWN]      = AAEKEY_DOWN;
	t.v[KEY_SYSRQ]     = AAEKEY_PRTSCR;   // PrintScreen is KEY_SYSRQ, not KEY_PRINT
	t.v[KEY_PAUSE]     = AAEKEY_PAUSE;

	// --- punctuation ------------------------------------------------------
	// The AAE names are Allegro's; the values are Windows OEM VK codes, so the
	// pairing is by PHYSICAL KEY, not by the character printed on it.
	t.v[KEY_GRAVE]      = AAEKEY_TILDE;        // VK_OEM_3
	t.v[KEY_MINUS]      = AAEKEY_MINUS;
	t.v[KEY_EQUAL]      = AAEKEY_EQUALS;
	t.v[KEY_LEFTBRACE]  = AAEKEY_OPENBRACE;
	t.v[KEY_RIGHTBRACE] = AAEKEY_CLOSEBRACE;
	t.v[KEY_SEMICOLON]  = AAEKEY_COLON;        // VK_OEM_1, the ';' key
	t.v[KEY_APOSTROPHE] = AAEKEY_QUOTE;
	t.v[KEY_BACKSLASH]  = AAEKEY_BACKSLASH;
	t.v[KEY_COMMA]      = AAEKEY_COMMA;
	t.v[KEY_DOT]        = AAEKEY_STOP;
	t.v[KEY_SLASH]      = AAEKEY_SLASH;
	// The extra key ISO keyboards have that ANSI ones do not. Allegro calls it
	// BACKSLASH2, and AAEKEY_BACKSLASH2 shares its VALUE with AAEKEY_BACKSLASH
	// (0xdc, one of the nine documented enumerator collisions), so on this
	// platform the 102nd key is indistinguishable from the main backslash.
	// That is inherited from the key codes, not introduced here.
	t.v[KEY_102ND]      = AAEKEY_BACKSLASH2;

	// --- modifiers and locks ---------------------------------------------
	t.v[KEY_LEFTSHIFT]  = AAEKEY_LSHIFT;
	t.v[KEY_RIGHTSHIFT] = AAEKEY_RSHIFT;
	t.v[KEY_LEFTCTRL]   = AAEKEY_LCONTROL;
	t.v[KEY_RIGHTCTRL]  = AAEKEY_RCONTROL;
	t.v[KEY_LEFTALT]    = AAEKEY_ALT;      // == AAEKEY_LMENU
	t.v[KEY_RIGHTALT]   = AAEKEY_ALTGR;    // == AAEKEY_RMENU
	t.v[KEY_LEFTMETA]   = AAEKEY_LWIN;
	t.v[KEY_RIGHTMETA]  = AAEKEY_RWIN;
	t.v[KEY_CAPSLOCK]   = AAEKEY_CAPSLOCK;
	t.v[KEY_NUMLOCK]    = AAEKEY_NUMLOCK;
	t.v[KEY_SCROLLLOCK] = AAEKEY_SCRLOCK;

	// --- Japanese / Brazilian extras --------------------------------------
	t.v[KEY_YEN] = AAEKEY_YEN;
	t.v[KEY_RO]  = AAEKEY_ABNT_C1;             // VK_ABNT_C1
	t.v[KEY_KATAKANAHIRAGANA] = AAEKEY_KANA;   // VK_KANA
	//
	// KEY_HENKAN and KEY_MUHENKAN are NOT mapped, and that is deliberate.
	// AAEKEY_CONVERT is 0x79 - the same value as AAEKEY_F10 - and
	// AAEKEY_NOCONVERT is 0x7b, the same as AAEKEY_F12 (two of the nine
	// collisions sys_input.h enumerates). Mapping them would make the Henkan
	// key press F10 and Muhenkan press F12: a table entry that compiles,
	// looks correct, and is wrong in exactly the way this file's comments
	// warn about. Leaving them unmapped costs two keys almost nobody binds
	// and keeps the function row honest.
	//
	// KEY_COMPOSE (the "menu"/application key) is likewise unmapped: its
	// Windows code is VK_APPS (0x5d), and AAE has no enumerator for it.
	// AAEKEY_MENU is 0x12, which is VK_MENU - generic Alt, a different key.

	return t;
}

constexpr KeyTable kTable = BuildKeyTable();

//------------------------------------------------------------------------------
// Spot-checks across the ranges most likely to be mistyped. These are cheap
// and they are the only automated defence this file has.
//------------------------------------------------------------------------------
static_assert(kTable.v[KEY_A]     == AAEKEY_A,     "KEY_A must map to AAEKEY_A");
static_assert(kTable.v[KEY_Z]     == AAEKEY_Z,     "KEY_Z must map to AAEKEY_Z");
static_assert(kTable.v[KEY_M]     == AAEKEY_M,     "mid-alphabet, different row");
static_assert(kTable.v[KEY_0]     == AAEKEY_0,     "digit row: zero is last in evdev");
static_assert(kTable.v[KEY_9]     == AAEKEY_9,     "digit row");
static_assert(kTable.v[KEY_ESC]   == AAEKEY_ESC,   "escape");
static_assert(kTable.v[KEY_SPACE] == AAEKEY_SPACE, "space");
static_assert(kTable.v[KEY_LEFT]  == AAEKEY_LEFT,  "arrow cluster");
static_assert(kTable.v[KEY_DOWN]  == AAEKEY_DOWN,  "arrow cluster");
static_assert(kTable.v[KEY_F1]    == AAEKEY_F1,    "function row");
static_assert(kTable.v[KEY_F12]   == AAEKEY_F12,   "F11/F12 are not adjacent to F10");
static_assert(kTable.v[KEY_KP0]   == AAEKEY_0_PAD, "keypad is a separate range");
static_assert(kTable.v[KEY_KP9]   == AAEKEY_9_PAD, "keypad");
static_assert(kTable.v[KEY_LEFTCTRL]  == AAEKEY_LCONTROL, "left/right modifiers differ");
static_assert(kTable.v[KEY_RIGHTCTRL] == AAEKEY_RCONTROL, "left/right modifiers differ");
static_assert(kTable.v[KEY_LEFTCTRL]  != kTable.v[KEY_RIGHTCTRL],
              "the whole point of per-side modifiers is that they are distinct");

// The keypad digits must NOT collide with the main digit row - a single copy
// -paste slip here silently merges them and both rows then fight over one key.
static_assert(kTable.v[KEY_KP1] != kTable.v[KEY_1], "keypad 1 is not digit-row 1");

// Codes that must stay unmapped, for the reasons argued above. Asserting the
// absence keeps a future well-meaning "fill in the gaps" pass from
// reintroducing the F10/F12 collision.
static_assert(kTable.v[KEY_HENKAN]   == 0, "KEY_HENKAN must stay unmapped: AAEKEY_CONVERT aliases F10");
static_assert(kTable.v[KEY_MUHENKAN] == 0, "KEY_MUHENKAN must stay unmapped: AAEKEY_NOCONVERT aliases F12");
static_assert(kTable.v[KEY_KPEQUAL]  == 0, "KEY_KPEQUAL must stay unmapped: AAEKEY_EQUALS_PAD is 0");

//------------------------------------------------------------------------------
// Coverage report data.
//
// AaeKey cannot be enumerated by the language, so the bindable set is listed
// once here. Aliases are listed under a single name: AAEKEY_TILDE and
// AAEKEY_BACKQUOTE are one value and cannot be distinguished, so reporting
// both would report a phantom gap.
//------------------------------------------------------------------------------
struct NamedKey { const char* name; uint8_t code; };

const NamedKey kBindableKeys[] = {
	{"A",AAEKEY_A},{"B",AAEKEY_B},{"C",AAEKEY_C},{"D",AAEKEY_D},{"E",AAEKEY_E},
	{"F",AAEKEY_F},{"G",AAEKEY_G},{"H",AAEKEY_H},{"I",AAEKEY_I},{"J",AAEKEY_J},
	{"K",AAEKEY_K},{"L",AAEKEY_L},{"M",AAEKEY_M},{"N",AAEKEY_N},{"O",AAEKEY_O},
	{"P",AAEKEY_P},{"Q",AAEKEY_Q},{"R",AAEKEY_R},{"S",AAEKEY_S},{"T",AAEKEY_T},
	{"U",AAEKEY_U},{"V",AAEKEY_V},{"W",AAEKEY_W},{"X",AAEKEY_X},{"Y",AAEKEY_Y},
	{"Z",AAEKEY_Z},
	{"0",AAEKEY_0},{"1",AAEKEY_1},{"2",AAEKEY_2},{"3",AAEKEY_3},{"4",AAEKEY_4},
	{"5",AAEKEY_5},{"6",AAEKEY_6},{"7",AAEKEY_7},{"8",AAEKEY_8},{"9",AAEKEY_9},
	{"0_PAD",AAEKEY_0_PAD},{"1_PAD",AAEKEY_1_PAD},{"2_PAD",AAEKEY_2_PAD},
	{"3_PAD",AAEKEY_3_PAD},{"4_PAD",AAEKEY_4_PAD},{"5_PAD",AAEKEY_5_PAD},
	{"6_PAD",AAEKEY_6_PAD},{"7_PAD",AAEKEY_7_PAD},{"8_PAD",AAEKEY_8_PAD},
	{"9_PAD",AAEKEY_9_PAD},
	{"F1",AAEKEY_F1},{"F2",AAEKEY_F2},{"F3",AAEKEY_F3},{"F4",AAEKEY_F4},
	{"F5",AAEKEY_F5},{"F6",AAEKEY_F6},{"F7",AAEKEY_F7},{"F8",AAEKEY_F8},
	{"F9",AAEKEY_F9},{"F10",AAEKEY_F10},{"F11",AAEKEY_F11},{"F12",AAEKEY_F12},
	{"ESC",AAEKEY_ESC},{"TILDE",AAEKEY_TILDE},{"MINUS",AAEKEY_MINUS},
	{"EQUALS",AAEKEY_EQUALS},{"BACKSPACE",AAEKEY_BACKSPACE},{"TAB",AAEKEY_TAB},
	{"OPENBRACE",AAEKEY_OPENBRACE},{"CLOSEBRACE",AAEKEY_CLOSEBRACE},
	{"ENTER",AAEKEY_ENTER},{"COLON",AAEKEY_COLON},{"QUOTE",AAEKEY_QUOTE},
	{"BACKSLASH",AAEKEY_BACKSLASH},{"COMMA",AAEKEY_COMMA},{"STOP",AAEKEY_STOP},
	{"SLASH",AAEKEY_SLASH},{"SPACE",AAEKEY_SPACE},
	{"INSERT",AAEKEY_INSERT},{"DEL",AAEKEY_DEL},{"HOME",AAEKEY_HOME},
	{"END",AAEKEY_END},{"PGUP",AAEKEY_PGUP},{"PGDN",AAEKEY_PGDN},
	{"LEFT",AAEKEY_LEFT},{"RIGHT",AAEKEY_RIGHT},{"UP",AAEKEY_UP},{"DOWN",AAEKEY_DOWN},
	{"SLASH_PAD",AAEKEY_SLASH_PAD},{"ASTERISK",AAEKEY_ASTERISK},
	{"MINUS_PAD",AAEKEY_MINUS_PAD},{"PLUS_PAD",AAEKEY_PLUS_PAD},
	{"DEL_PAD",AAEKEY_DEL_PAD},{"ENTER_PAD",AAEKEY_ENTER_PAD},
	{"PRTSCR",AAEKEY_PRTSCR},{"PAUSE",AAEKEY_PAUSE},
	{"ABNT_C1",AAEKEY_ABNT_C1},{"YEN",AAEKEY_YEN},{"KANA",AAEKEY_KANA},
	{"COLON2",AAEKEY_COLON2},{"KANJI",AAEKEY_KANJI},
	{"LSHIFT",AAEKEY_LSHIFT},{"RSHIFT",AAEKEY_RSHIFT},
	{"LCONTROL",AAEKEY_LCONTROL},{"RCONTROL",AAEKEY_RCONTROL},
	{"ALT",AAEKEY_ALT},{"ALTGR",AAEKEY_ALTGR},
	{"LWIN",AAEKEY_LWIN},{"RWIN",AAEKEY_RWIN},{"MENU",AAEKEY_MENU},
	{"SCRLOCK",AAEKEY_SCRLOCK},{"NUMLOCK",AAEKEY_NUMLOCK},{"CAPSLOCK",AAEKEY_CAPSLOCK},
	// AAEKEY_EQUALS_PAD is 0, which this backend uses as "no key". It is not
	// producible on any platform and is excluded rather than reported missing.
};

} // namespace

//------------------------------------------------------------------------------
uint8_t EvdevKeyToAae(int evdevCode)
{
	if (evdevCode < 0 || evdevCode > KEY_MAX) return 0;
	return kTable.v[evdevCode];
}

//------------------------------------------------------------------------------
void EvdevKeymapLogCoverage()
{
	int mappedCodes = 0;
	for (int i = 0; i <= KEY_MAX; i++)
		if (kTable.v[i]) mappedCodes++;

	// Which AAE keys does at least one evdev code produce?
	bool produced[256] = {false};
	for (int i = 0; i <= KEY_MAX; i++)
		if (kTable.v[i]) produced[kTable.v[i]] = true;

	const int total = (int)(sizeof(kBindableKeys) / sizeof(kBindableKeys[0]));
	int covered = 0;
	char missing[512];
	size_t used = 0;
	missing[0] = 0;

	for (int i = 0; i < total; i++) {
		if (produced[kBindableKeys[i].code]) {
			covered++;
		} else if (used + strlen(kBindableKeys[i].name) + 2 < sizeof(missing)) {
			used += (size_t)snprintf(missing + used, sizeof(missing) - used,
			                         "%s%s", used ? " " : "", kBindableKeys[i].name);
		}
	}

	LOG_INFO("evdev keymap: %d Linux key codes mapped; %d/%d bindable AAE keys "
	         "reachable", mappedCodes, covered, total);
	if (covered < total)
		LOG_WARN("evdev keymap: no evdev source for: %s", missing);
}
