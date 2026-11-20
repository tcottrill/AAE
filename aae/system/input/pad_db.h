//==========================================================================
// AAE - Another Arcade Emulator
// Copyright (C) 2026 Tim Cottrill - GNU GPL v3 or later.
//==========================================================================
#pragma once
//==============================================================================
// pad_db -- "which evdev control is this pad's A button / left stick / ...".
//
// The Linux joystick backend reads raw evdev and has no SDL. SDL maps a Linux
// pad in two steps, and this module reproduces both so a pad behaves here the
// way it behaves in every SDL game:
//   1. look the device up (by GUID) in a gamepad mapping database;
//   2. failing that, apply SDL's Linux "automapper" rules, which guess a
//      layout from the capability bits (src/joystick/linux/SDL_sysjoystick.c,
//      LINUX_JoystickGetGamepadMapping).
// A device that is neither in the database nor BTN_GAMEPAD-class (flight
// sticks, arcade encoders) is mapped "raw": button order in, canonical order out.
//
// The result is a PadLayout: for each canonical control, the evdev source that
// feeds it. The evdev backend drives everything from that table.
//
// No OS headers: unit-tested standalone on Windows (tests/test_pad_db.cpp).
// The handful of Linux input codes needed are restated below with a PADDB_
// prefix because <linux/input.h> is not available here; the values are the
// kernel's own and the evdev file passes real kernel codes unchanged.
//==============================================================================
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "pad_map.h"   // PAD_BTN_* canonical order, PadCaps

enum PadDbCode : int {
	PADDB_KEY_MAX = 0x2ff,
	PADDB_BTN_JOYSTICK = 0x120,
	PADDB_BTN_GAMEPAD = 0x130, PADDB_BTN_SOUTH = 0x130, PADDB_BTN_A = 0x130,
	PADDB_BTN_B = 0x131,
	PADDB_BTN_C = 0x132,
	PADDB_BTN_X = 0x133, PADDB_BTN_NORTH = 0x133,
	PADDB_BTN_Y = 0x134, PADDB_BTN_WEST = 0x134,
	PADDB_BTN_Z = 0x135,
	PADDB_BTN_TL = 0x136, PADDB_BTN_TR = 0x137,
	PADDB_BTN_TL2 = 0x138, PADDB_BTN_TR2 = 0x139,
	PADDB_BTN_SELECT = 0x13a, PADDB_BTN_START = 0x13b, PADDB_BTN_MODE = 0x13c,
	PADDB_BTN_THUMBL = 0x13d, PADDB_BTN_THUMBR = 0x13e,
	PADDB_BTN_DPAD_UP = 0x220, PADDB_BTN_DPAD_DOWN = 0x221,
	PADDB_BTN_DPAD_LEFT = 0x222, PADDB_BTN_DPAD_RIGHT = 0x223,
	PADDB_BTN_TRIGGER_HAPPY1 = 0x2c0, PADDB_BTN_TRIGGER_HAPPY2 = 0x2c1,
	PADDB_BTN_TRIGGER_HAPPY3 = 0x2c2, PADDB_BTN_TRIGGER_HAPPY4 = 0x2c3,
	PADDB_ABS_MAX = 0x3f,
	PADDB_ABS_X = 0, PADDB_ABS_Y = 1, PADDB_ABS_Z = 2,
	PADDB_ABS_RX = 3, PADDB_ABS_RY = 4, PADDB_ABS_RZ = 5,
	PADDB_ABS_GAS = 9, PADDB_ABS_BRAKE = 10,
	PADDB_ABS_HAT0X = 0x10, PADDB_ABS_HAT0Y = 0x11,
	PADDB_ABS_HAT1X = 0x12, PADDB_ABS_HAT1Y = 0x13,
	PADDB_ABS_HAT2X = 0x14, PADDB_ABS_HAT2Y = 0x15,
	PADDB_ABS_HAT3X = 0x16, PADDB_ABS_HAT3Y = 0x17,
};

constexpr int PADDB_NUM_KEYS = 0x300;   // key codes 0..KEY_MAX
constexpr int PADDB_NUM_ABS  = 0x40;    // abs codes 0..ABS_MAX
constexpr int PADDB_NUM_HAT_AXES = 8;   // ABS_HAT0X..ABS_HAT3Y

// SDL hat bit values (the "hH.M" mask in a mapping string).
constexpr int PADDB_HAT_UP = 1, PADDB_HAT_RIGHT = 2, PADDB_HAT_DOWN = 4, PADDB_HAT_LEFT = 8;

//------------------------------------------------------------------------------
// What a device advertises.
//------------------------------------------------------------------------------
struct PadDbCaps {
	bool hasKey[PADDB_NUM_KEYS] = {};
	bool hasAbs[PADDB_NUM_ABS]  = {};
	// absinfo of ABS_HAT0X..ABS_HAT3Y (index = code - ABS_HAT0X); only these
	// are needed, for the digital-hat guess.
	struct HatAbs { int min = 0, max = 0, fuzz = 0, flat = 0, resolution = 0; };
	HatAbs hat[PADDB_NUM_HAT_AXES];

	uint16_t bus = 0, vendor = 0, product = 0, version = 0;
	char     name[128] = {};
};

//------------------------------------------------------------------------------
// SDL's numbering of a device's buttons, hats and axes, which is what the
// "b3" / "a2" / "h0.4" in a mapping string index into.
//------------------------------------------------------------------------------
struct PadDbIndex {
	int nButtons = 0, nHats = 0, nAxes = 0;
	uint16_t btnCode[PADDB_NUM_KEYS] = {};    // button index -> key code
	uint16_t hatCode[4] = {};                 // hat index    -> abs code of its X axis (Y = +1)
	uint16_t axisCode[PADDB_NUM_ABS] = {};    // axis index   -> abs code
	bool     hasHat[4] = {};                  // by hat PAIR (0..3): this pair became a hat
	bool     hasAxis[PADDB_NUM_ABS] = {};     // by abs code: this code became an axis
};

//------------------------------------------------------------------------------
// One evdev source feeding one canonical control.
//------------------------------------------------------------------------------
enum PadSrcKind : uint8_t { PADSRC_NONE = 0, PADSRC_KEY, PADSRC_AXIS, PADSRC_HAT };

struct PadSrc {
	uint8_t  kind     = PADSRC_NONE;
	uint16_t code     = 0;     // key code, abs code, or (hat) abs code of the hat's X axis
	uint8_t  hatMask  = 0;     // PADDB_HAT_* bits (hat only)
	int8_t   axisHalf = 0;     // axis only: 0 full range, +1 positive half, -1 negative half
	bool     invert   = false; // the "~" suffix
};

enum { PADDB_STICK_LX = 0, PADDB_STICK_LY, PADDB_STICK_RX, PADDB_STICK_RY, PADDB_STICK_COUNT };

struct PadLayout {
	PadSrc btn[PAD_BTN_COUNT];            // canonical order; LT/RT are the triggers
	PadSrc stick[PADDB_STICK_COUNT];      // leftx, lefty, rightx, righty
	bool   gamepad = false;               // DB hit or automap
	bool   raw     = false;               // raw joystick fallback
	char   source[96] = {};               // for the log
};

// One searchable list of SDL mapping strings, with the label the log uses.
struct PadDbList {
	const char* const* lines = nullptr;
	size_t             count = 0;
	const char*        label = "";
};

//------------------------------------------------------------------------------
// GUID. SDL's Linux GUID: eight little-endian uint16 - bus, the CRC of the
// kernel device name (0 when name is null), then vendor/0/product/0/version/0,
// or - when the vendor is 0 - the device name (at most 11 characters) from
// byte 4 on.
//------------------------------------------------------------------------------
uint16_t pad_db_crc16(const char* data, size_t len);   // CRC-16/ARC, as SDL_crc16
// The version SDL looks a device's mapping up under (two device-specific
// adjustments; otherwise caps.version).
uint16_t pad_db_mapping_version(const PadDbCaps& caps);
void pad_db_make_guid(uint16_t bus, uint16_t vendor, uint16_t product, uint16_t version,
                      const char* name, uint8_t out[16]);
void pad_db_guid_to_string(const uint8_t guid[16], char out[33]);   // lowercase hex
// 32 hex digits (either case) at hex32. False if any of them is not hex.
bool pad_db_parse_guid(const char* hex32, uint8_t out[16]);

// Same layout as pad_db_make_guid, but the checksum string and the name-form
// string are separate. SDL_CreateJoystickGUID checksums "manufacturer product"
// when it has both, and the product alone otherwise, while the vendor-less name
// form always carries the product name. `crcString` null/empty -> checksum 0.
void pad_db_make_guid_ex(uint16_t bus, uint16_t vendor, uint16_t product, uint16_t version,
                         const char* crcString, const char* nameFormString, uint8_t out[16]);

// Search the lists in the given priority order. A mapping line matches when its
// GUID equals `guid` ignoring bytes 2-3 (the CRC field). As in SDL: first an
// exact match including the version bytes (12-13) over ALL lists, and only if
// nothing matched, the same ignoring the version. Within each, a line whose
// "crc:XXXX" field equals the device's name CRC (bytes 2-3 of `guid`) wins over
// a line with no "crc:"; a line pinned to a different CRC never matches. Lines
// whose "platform:" is not Linux are skipped. Returns the line, or nullptr;
// *listIndex (optional) receives the index of the list it came from.
const char* pad_db_find(const uint8_t guid[16], const PadDbList* lists, int nLists,
                        int* listIndex);
// The same for any platform name ("Linux", "Windows", ...). A line with no
// "platform:" field is usable everywhere.
const char* pad_db_find_for(const uint8_t guid[16], const PadDbList* lists, int nLists,
                            const char* platform, int* listIndex);

// The name field of a mapping line, NUL-terminated into out.
void pad_db_mapping_name(const char* line, char* out, size_t outSize);

// SDL's numbering for this device (ConfigJoystick).
void pad_db_build_index(const PadDbCaps& caps, PadDbIndex* out);

// Parse "guid,name,key:value,...". False when no control could be mapped.
bool pad_db_parse_mapping(const char* line, const PadDbIndex& idx, PadLayout* out);

// SDL's Linux automapper, expressed directly in evdev codes. False unless the
// device has BTN_GAMEPAD.
bool pad_db_automap(const PadDbCaps& caps, const PadDbIndex& idx, PadLayout* out);

// Raw joystick fallback: button index n -> canonical button n, ABS_X/Y left
// stick, RX/RY (else Z/RZ) right stick, digital hat 0 -> the d-pad. Not a
// gamepad layout.
void pad_db_raw_joystick(const PadDbCaps& caps, const PadDbIndex& idx, PadLayout* out);

// Whole pipeline: index, lists in order, automap, raw joystick.
// *isGamepadLayout is true for a DB hit or automap, false for the raw fallback.
void pad_db_resolve(const PadDbCaps& caps, const PadDbList* lists, int nLists,
                    PadLayout* out, bool* isGamepadLayout);

// A PadCaps describing the RESOLVED layout, so pad_rank ranks what the pad will
// actually do rather than what its raw bits suggest.
PadCaps pad_db_layout_caps(const PadLayout& layout);

// Split text into lines, dropping blank lines and '#' comments; appends to
// *lines. Also used for the SDL_GAMECONTROLLERCONFIG variable.
void pad_db_load_text(const char* fileContents, std::vector<std::string>* lines);

//------------------------------------------------------------------------------
// Windows DirectInput. There is no kernel capability bitmap and no automapper:
// a device either has a database entry (and gets that layout) or it stays a raw
// stick, which the caller handles.
//
// PadSrc.code means, for a DirectInput layout:
//   KEY  - the DIJOYSTATE2 rgbButtons index;
//   AXIS - an axis id, PADDB_DI_AXIS_*  (X, Y, Z, RX, RY, RZ, slider 0, slider 1);
//   HAT  - PADDB_ABS_HAT0X + 2 * the rgdwPOV index (the Linux convention, so the
//          parser's hat arithmetic is shared).
// SDL's indexes on DirectInput (SDL_dinputjoystick.c, SortDevObjects) follow the
// DIJOYSTATE2 offsets: button N is rgbButtons[N], hat N is rgdwPOV[N], and axis
// N is the N-th PRESENT axis in the order above.
//------------------------------------------------------------------------------
enum PadDbDiAxis : int {
	PADDB_DI_AXIS_X = 0, PADDB_DI_AXIS_Y, PADDB_DI_AXIS_Z,
	PADDB_DI_AXIS_RX, PADDB_DI_AXIS_RY, PADDB_DI_AXIS_RZ,
	PADDB_DI_AXIS_SLIDER0, PADDB_DI_AXIS_SLIDER1,
	PADDB_DI_AXIS_COUNT
};
constexpr int PADDB_DI_MAX_BUTTONS = 128;   // DIJOYSTATE2::rgbButtons
constexpr int PADDB_DI_MAX_POVS    = 4;     // DIJOYSTATE2::rgdwPOV

void pad_db_build_index_dinput(int nButtons, const bool axisPresent[PADDB_DI_AXIS_COUNT],
                               int nPovs, PadDbIndex* out);

// What a DirectInput device tells us about itself.
struct PadDbDinputInfo {
	uint16_t    vendor = 0, product = 0;
	// Checksum string candidates (SDL_CreateJoystickGUID): "manufacturer product"
	// when the HID strings give both, else the product string. Tried in order;
	// crcFallback (the DirectInput product name) only if the first finds nothing.
	const char* crcString   = nullptr;
	const char* crcFallback = nullptr;
	const char* nameForm    = nullptr;   // the product name, for a vendor-less GUID
	int         nButtons = 0;
	bool        axisPresent[PADDB_DI_AXIS_COUNT] = {};
	int         nPovs = 0;
};

// Look the device up (platform "Windows") and parse the entry. True only on a
// database hit that mapped something; *out then has gamepad = true and source
// "<list label>: <mapping name>". No hit -> false and *out is untouched (the
// caller keeps its raw-stick behaviour). *guidOut (optional) receives the GUID of
// the candidate that hit, or of the first candidate when nothing hit.
bool pad_db_resolve_dinput(const PadDbDinputInfo& info, const PadDbList* lists, int nLists,
                           PadLayout* out, uint8_t guidOut[16]);

//------------------------------------------------------------------------------
// Axis value math shared by the Linux and Windows backends. `raw`, `minV`, `maxV`
// are the device's own units; the centre is (min + max) / 2.
//------------------------------------------------------------------------------
// 0..255 for a trigger or an axis-as-button. half 0: min..max; +1: centre..max;
// -1: centre..min. `invert` flips the result. 0 for an empty range.
int pad_db_axis_norm255(int raw, int minV, int maxV, int half, bool invert);
// -128..127 for a stick, relative to the centre, honouring a declared dead zone
// `flat` (raw units).
int pad_db_axis_scale(int raw, int minV, int maxV, int flat);
// A stick axis as a PadSrc asks for it (half-range sources report their 0..127
// magnitude; `invert` negates a full-range one).
int pad_db_stick_value(int raw, int minV, int maxV, int flat, int half, bool invert);

//------------------------------------------------------------------------------
// One-line description of a resolved layout for the log. The two styles differ
// only in how a KEY and an AXIS source print: Linux "key0x130" / "abs2",
// Windows "b3" / "X","Y","Z","RX","RY","RZ","S0","S1" (halves "+Z", inverted "Y~").
// The Windows style also lists the remaining buttons.
//------------------------------------------------------------------------------
enum PadDescribeStyle : int { PADDB_DESCRIBE_LINUX = 0, PADDB_DESCRIBE_WINDOWS = 1 };
void pad_db_describe_layout(const PadLayout& layout, char* out, size_t outSize,
                            PadDescribeStyle style);
