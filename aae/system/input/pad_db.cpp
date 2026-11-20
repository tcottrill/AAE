//==========================================================================
// AAE - Another Arcade Emulator
// Copyright (C) 2026 Tim Cottrill - GNU GPL v3 or later.
//==========================================================================
// See pad_db.h. The rules here are re-expressed from SDL's Linux joystick
// driver (zlib licence) - ConfigJoystick, GuessIfAxesAreDigitalHat and
// LINUX_JoystickGetGamepadMapping - in terms of evdev codes. No SDL code is
// copied.
#include "pad_db.h"

#include <cstdio>
#include <cstring>

namespace {

int hex_val(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

void put16(uint8_t* g, int word, uint16_t v)
{
	g[word * 2]     = (uint8_t)(v & 0xff);
	g[word * 2 + 1] = (uint8_t)(v >> 8);
}

// A line carries a given ",key:" field; returns the value start or nullptr.
const char* field_value(const char* line, const char* keyColon)
{
	char pat[24];
	snprintf(pat, sizeof(pat), ",%s", keyColon);
	const char* p = strstr(line, pat);
	return p ? p + strlen(pat) : nullptr;
}

// The "crc:XXXX" field of a line, if it has one. Some devices share a
// vendor:product with unrelated ones (the Xin-Mo arcade encoder, Nintendo's
// pads); their entries pin the checksum of the device NAME as well.
bool entry_crc(const char* line, uint16_t* crc)
{
	const char* v = field_value(line, "crc:");
	if (!v) return false;
	unsigned x = 0;
	for (int i = 0; i < 4 && hex_val(v[i]) >= 0; i++) x = (x << 4) | (unsigned)hex_val(v[i]);
	*crc = (uint16_t)x;
	return true;
}

bool entry_usable(const char* line, const char* platform)
{
	// The built-in lists have no platform field; user files may. A line with
	// none is usable on every platform.
	if (const char* plat = field_value(line, "platform:")) {
		const size_t n = platform ? strlen(platform) : 0;
		if (!platform || strncmp(plat, platform, n) != 0 || (plat[n] != ',' && plat[n] != '\0'))
			return false;
	}
	return true;
}

bool entry_matches(const char* line, const uint8_t g[16], bool matchVersion)
{
	uint8_t e[16];
	if (!pad_db_parse_guid(line, e)) return false;
	if (line[32] != ',') return false;
	for (int i = 0; i < 16; i++) {
		if (i == 2 || i == 3) continue;                    // CRC field
		if (!matchVersion && (i == 12 || i == 13)) continue;
		if (e[i] != g[i]) return false;
	}
	return true;
}

PadSrc src_key(int code)
{
	PadSrc s; s.kind = PADSRC_KEY; s.code = (uint16_t)code; return s;
}
PadSrc src_axis(int code, int half = 0, bool invert = false)
{
	PadSrc s; s.kind = PADSRC_AXIS; s.code = (uint16_t)code; s.axisHalf = (int8_t)half;
	s.invert = invert; return s;
}
PadSrc src_hat(int pair, int mask)
{
	PadSrc s; s.kind = PADSRC_HAT; s.code = (uint16_t)(PADDB_ABS_HAT0X + 2 * pair);
	s.hatMask = (uint8_t)mask; return s;
}

void set_source(PadLayout* l, const char* text)
{
	snprintf(l->source, sizeof(l->source), "%s", text);
}

// SDL's GuessIfAxesAreDigitalHat, minus the "treat all hats as digital" hint:
// a hat pair is digital when both present axes span exactly -1..1, or when
// neither present axis declares fuzz, flat or resolution.
bool hat_pair_is_digital(const PadDbCaps& c, int pair)
{
	const int cx = PADDB_ABS_HAT0X + 2 * pair, cy = cx + 1;
	const bool hx = c.hasAbs[cx], hy = c.hasAbs[cy];
	if (!hx && !hy) return false;
	const PadDbCaps::HatAbs& ax = c.hat[cx - PADDB_ABS_HAT0X];
	const PadDbCaps::HatAbs& ay = c.hat[cy - PADDB_ABS_HAT0X];

	if ((!hx || (ax.min == -1 && ax.max == 1)) && (!hy || (ay.min == -1 && ay.max == 1)))
		return true;
	if ((!hx || (!ax.fuzz && !ax.flat && !ax.resolution)) &&
	    (!hy || (!ay.fuzz && !ay.flat && !ay.resolution)))
		return true;
	return false;
}

} // namespace

//------------------------------------------------------------------------------
// CRC-16/ARC (reflected polynomial 0xA001, initial value 0): the algorithm
// SDL_crc16 uses, which its mapping "crc:" fields depend on.
uint16_t pad_db_crc16(const char* data, size_t len)
{
	uint16_t crc = 0;
	for (size_t i = 0; i < len; i++) {
		crc ^= (uint8_t)data[i];
		for (int b = 0; b < 8; b++)
			crc = (uint16_t)((crc & 1) ? (crc >> 1) ^ 0xA001 : (crc >> 1));
	}
	return crc;
}

void pad_db_make_guid_ex(uint16_t bus, uint16_t vendor, uint16_t product, uint16_t version,
                         const char* crcString, const char* nameFormString, uint8_t out[16])
{
	memset(out, 0, 16);
	put16(out, 0, bus);
	// Word 1: SDL's checksum of the device name (SDL_CreateJoystickGUID). Mapping
	// lines never carry it in their GUID; it is compared with their "crc:" field
	// in pad_db_find.
	if (crcString) put16(out, 1, pad_db_crc16(crcString, strlen(crcString)));
	if (vendor) {                       // SDL tests the vendor alone
		put16(out, 2, vendor);
		put16(out, 4, product);
		put16(out, 6, version);
	} else if (nameFormString) {
		size_t n = strlen(nameFormString);
		if (n > 11) n = 11;             // bytes 4..14, byte 15 stays NUL
		memcpy(out + 4, nameFormString, n);
	}
}

void pad_db_make_guid(uint16_t bus, uint16_t vendor, uint16_t product, uint16_t version,
                      const char* name, uint8_t out[16])
{
	pad_db_make_guid_ex(bus, vendor, product, version, name, name, out);
}

void pad_db_guid_to_string(const uint8_t guid[16], char out[33])
{
	static const char hex[] = "0123456789abcdef";
	for (int i = 0; i < 16; i++) {
		out[i * 2]     = hex[guid[i] >> 4];
		out[i * 2 + 1] = hex[guid[i] & 15];
	}
	out[32] = 0;
}

bool pad_db_parse_guid(const char* hex32, uint8_t out[16])
{
	if (!hex32) return false;
	for (int i = 0; i < 16; i++) {
		const int hi = hex_val(hex32[i * 2]);
		if (hi < 0) return false;
		const int lo = hex_val(hex32[i * 2 + 1]);
		if (lo < 0) return false;
		out[i] = (uint8_t)((hi << 4) | lo);
	}
	return true;
}

const char* pad_db_find(const uint8_t guid[16], const PadDbList* lists, int nLists,
                        int* listIndex)
{
	return pad_db_find_for(guid, lists, nLists, "Linux", listIndex);
}

const char* pad_db_find_for(const uint8_t guid[16], const PadDbList* lists, int nLists,
                            const char* platform, int* listIndex)
{
	// SDL_PrivateFindGamepadMappingForGUID, re-expressed: for the exact version
	// and then for any version, an entry whose "crc:" equals the device's name
	// checksum wins outright; failing that, the first entry with no "crc:" at
	// all. An entry pinned to a DIFFERENT checksum is another device with the
	// same ids and never matches.
	const uint16_t devCrc = (uint16_t)(guid[2] | (guid[3] << 8));
	for (int pass = 0; pass < 4; pass++) {
		const bool matchVersion = (pass < 2);
		const bool wantCrc      = (pass % 2 == 0);
		for (int li = 0; li < nLists; li++) {
			for (size_t i = 0; i < lists[li].count; i++) {
				const char* line = lists[li].lines[i];
				if (!line || !entry_usable(line, platform)) continue;
				uint16_t crc = 0;
				const bool pinned = entry_crc(line, &crc);
				if (pinned != wantCrc) continue;
				if (pinned && crc != devCrc) continue;
				if (entry_matches(line, guid, matchVersion)) {
					if (listIndex) *listIndex = li;
					return line;
				}
			}
		}
	}
	return nullptr;
}

void pad_db_mapping_name(const char* line, char* out, size_t outSize)
{
	if (!outSize) return;
	out[0] = 0;
	if (!line || strlen(line) < 33 || line[32] != ',') return;
	const char* b = line + 33;
	const char* e = strchr(b, ',');
	size_t n = e ? (size_t)(e - b) : strlen(b);
	if (n >= outSize) n = outSize - 1;
	memcpy(out, b, n);
	out[n] = 0;
}

//------------------------------------------------------------------------------
// ConfigJoystick's numbering. Both loops stop short of KEY_MAX / ABS_MAX
// themselves, exactly as SDL's do.
//------------------------------------------------------------------------------
void pad_db_build_index(const PadDbCaps& caps, PadDbIndex* out)
{
	*out = PadDbIndex();

	for (int i = PADDB_BTN_JOYSTICK; i < PADDB_KEY_MAX; i++)
		if (caps.hasKey[i]) out->btnCode[out->nButtons++] = (uint16_t)i;
	for (int i = 0; i < PADDB_BTN_JOYSTICK; i++)
		if (caps.hasKey[i]) out->btnCode[out->nButtons++] = (uint16_t)i;

	for (int pair = 0; pair < 4; pair++) {
		if (hat_pair_is_digital(caps, pair)) {
			out->hasHat[pair] = true;
			out->hatCode[out->nHats++] = (uint16_t)(PADDB_ABS_HAT0X + 2 * pair);
		}
	}

	for (int i = 0; i < PADDB_ABS_MAX; i++) {
		if (i >= PADDB_ABS_HAT0X && i <= PADDB_ABS_HAT3Y && out->hasHat[(i - PADDB_ABS_HAT0X) / 2])
			continue;
		if (caps.hasAbs[i]) {
			out->hasAxis[i] = true;
			out->axisCode[out->nAxes++] = (uint16_t)i;
		}
	}
}

//------------------------------------------------------------------------------
// Mapping strings.
//------------------------------------------------------------------------------
namespace {

// Slots 0..15 are canonical buttons; 16.. are sticks.
struct KeyDef { const char* name; int slot; };
const KeyDef kKeys[] = {
	{ "a", PAD_BTN_A }, { "b", PAD_BTN_B }, { "x", PAD_BTN_X }, { "y", PAD_BTN_Y },
	{ "back", PAD_BTN_BACK }, { "start", PAD_BTN_START },
	{ "leftstick", PAD_BTN_LTHUMB }, { "rightstick", PAD_BTN_RTHUMB },
	{ "leftshoulder", PAD_BTN_LB }, { "rightshoulder", PAD_BTN_RB },
	{ "dpup", PAD_BTN_DPAD_UP }, { "dpdown", PAD_BTN_DPAD_DOWN },
	{ "dpleft", PAD_BTN_DPAD_LEFT }, { "dpright", PAD_BTN_DPAD_RIGHT },
	{ "lefttrigger", PAD_BTN_LT }, { "righttrigger", PAD_BTN_RT },
	{ "leftx",  PAD_BTN_COUNT + PADDB_STICK_LX }, { "lefty",  PAD_BTN_COUNT + PADDB_STICK_LY },
	{ "rightx", PAD_BTN_COUNT + PADDB_STICK_RX }, { "righty", PAD_BTN_COUNT + PADDB_STICK_RY },
};

bool parse_uint(const char*& p, int* out)
{
	if (*p < '0' || *p > '9') return false;
	int v = 0;
	while (*p >= '0' && *p <= '9') {
		v = v * 10 + (*p - '0');
		if (v > 100000) return false;
		p++;
	}
	*out = v;
	return true;
}

// "b3", "h0.4", "+a2", "-a2", "a1~". A source whose index the device does not
// have comes back PADSRC_NONE (still a successful parse).
bool parse_value(const char* v, const PadDbIndex& idx, PadSrc* out)
{
	*out = PadSrc();
	const char* p = v;
	int half = 0;
	if (*p == '+') { half = 1; p++; }
	else if (*p == '-') { half = -1; p++; }

	if (*p == 'b' && !half) {
		p++;
		int n;
		if (!parse_uint(p, &n) || *p) return false;
		if (n < idx.nButtons) *out = src_key(idx.btnCode[n]);
		return true;
	}
	if (*p == 'a') {
		p++;
		int n;
		if (!parse_uint(p, &n)) return false;
		bool inv = false;
		if (*p == '~') { inv = true; p++; }
		if (*p) return false;
		if (n < idx.nAxes) *out = src_axis(idx.axisCode[n], half, inv);
		return true;
	}
	if (*p == 'h' && !half) {
		p++;
		int h, m;
		if (!parse_uint(p, &h) || *p != '.') return false;
		p++;
		if (!parse_uint(p, &m) || *p) return false;
		if (h < idx.nHats && m > 0 && m < 16) {
			*out = src_hat((idx.hatCode[h] - PADDB_ABS_HAT0X) / 2, m);
		}
		return true;
	}
	return false;
}

// The mapping's "hint:" field (SDL_PrivateAddGamepadMapping, which looks at the
// FIRST hint field only). A NEGATED SDL_GAMECONTROLLER_USE_*_LABELS hint says the
// mapping is already positional; a non-negated one says it is by printed label
// and must be converted to positional: BUTTON_LABELS swaps a<->b and x<->y
// (SDL_ConvertMappingToPositionalBAXY), GAMECUBE_LABELS swaps b<->x
// (SDL_ConvertMappingToPositionalAXBY). Other hints do not concern the layout.
struct HintSwap { bool abxy = false; bool bx = false; };

HintSwap mapping_hint_swap(const char* line)
{
	HintSwap h;
	const char* v = field_value(line, "hint:");
	if (!v) return h;
	bool negate = false;
	if (*v == '!') { negate = true; v++; }
	char hint[128];
	size_t n = 0;
	while (v[n] && v[n] != ',' && v[n] != ':' && n < sizeof(hint) - 1) { hint[n] = v[n]; n++; }
	hint[n] = 0;
	if (negate) return h;
	if (strcmp(hint, "SDL_GAMECONTROLLER_USE_GAMECUBE_LABELS") == 0) h.bx = true;
	else if (strcmp(hint, "SDL_GAMECONTROLLER_USE_BUTTON_LABELS") == 0) h.abxy = true;
	return h;
}

// SDL renames only the first ",a:" / ",b:" / ",x:" / ",y:" it finds; `seen`
// carries which of the four have been renamed already.
const char* hint_renamed(const char* key, const HintSwap& h, bool seen[4])
{
	if (!h.abxy && !h.bx) return key;
	if (key[0] == 0 || key[1] != 0) return key;
	int slot;
	const char* to;
	switch (key[0]) {
	case 'a': slot = 0; to = h.abxy ? "b" : nullptr; break;
	case 'b': slot = 1; to = h.abxy ? "a" : "x"; break;
	case 'x': slot = 2; to = h.abxy ? "y" : "b"; break;
	case 'y': slot = 3; to = h.abxy ? "x" : nullptr; break;
	default:  return key;
	}
	if (!to || seen[slot]) return key;
	seen[slot] = true;
	return to;
}

} // namespace

bool pad_db_parse_mapping(const char* line, const PadDbIndex& idx, PadLayout* out)
{
	PadLayout L;
	if (!line || strlen(line) < 34 || line[32] != ',') return false;

	char name[64];
	pad_db_mapping_name(line, name, sizeof(name));

	const HintSwap hintSwap = mapping_hint_swap(line);
	bool renamed[4] = {};

	const char* p = strchr(line + 33, ',');   // end of the name field
	int mapped = 0;
	while (p && *p == ',') {
		p++;
		const char* end = strchr(p, ',');
		const size_t len = end ? (size_t)(end - p) : strlen(p);

		char field[64];
		if (len > 0 && len < sizeof(field)) {
			memcpy(field, p, len);
			field[len] = 0;
			char* colon = strchr(field, ':');
			// Half-axis OUTPUTS ("+leftx:...") and every key we do not use
			// (guide, misc1, paddle*, touchpad, crc, hint, type, platform, face...)
			// are ignored.
			if (colon && field[0] != '+' && field[0] != '-') {
				*colon = 0;
				const char* keyName = hint_renamed(field, hintSwap, renamed);
				for (const KeyDef& k : kKeys) {
					if (strcmp(keyName, k.name) != 0) continue;
					PadSrc s;
					if (parse_value(colon + 1, idx, &s) && s.kind != PADSRC_NONE) {
						// Sticks only make sense from an axis.
						if (k.slot < PAD_BTN_COUNT)
							L.btn[k.slot] = s;
						else if (s.kind == PADSRC_AXIS)
							L.stick[k.slot - PAD_BTN_COUNT] = s;
						else
							break;
						mapped++;
					}
					break;
				}
			}
		}
		p = end;
	}
	if (!mapped) return false;

	L.gamepad = true;
	char src[96];
	snprintf(src, sizeof(src), "SDL mapping: %s", name);
	set_source(&L, src);
	*out = L;
	return true;
}

//------------------------------------------------------------------------------
// The automapper (SDL: LINUX_JoystickGetGamepadMapping), in evdev codes.
//------------------------------------------------------------------------------
bool pad_db_automap(const PadDbCaps& caps, const PadDbIndex& idx, PadLayout* out)
{
	if (!caps.hasKey[PADDB_BTN_GAMEPAD]) return false;

	PadLayout L;
	L.gamepad = true;
	set_source(&L, "automap");

	auto key = [&](int slot, int code) -> bool {
		if (!caps.hasKey[code]) return false;
		L.btn[slot] = src_key(code);
		return true;
	};

	key(PAD_BTN_A, PADDB_BTN_A);
	key(PAD_BTN_B, PADDB_BTN_B);
	// BTN_X/BTN_NORTH (0x133) and BTN_Y/BTN_WEST (0x134) are one pair of numbers
	// with two sets of names. xpad emits by printed letter (X = 0x133), but
	// hid-playstation by compass point (Square, the west button, = 0x134), so
	// for Sony the two are swapped back to put Square on X and Triangle on Y.
	if (caps.vendor == 0x054c) {
		key(PAD_BTN_X, PADDB_BTN_WEST);
		key(PAD_BTN_Y, PADDB_BTN_NORTH);
	} else {
		key(PAD_BTN_X, PADDB_BTN_X);
		key(PAD_BTN_Y, PADDB_BTN_Y);
	}
	key(PAD_BTN_BACK,   PADDB_BTN_SELECT);
	key(PAD_BTN_START,  PADDB_BTN_START);
	key(PAD_BTN_LTHUMB, PADDB_BTN_THUMBL);
	key(PAD_BTN_RTHUMB, PADDB_BTN_THUMBR);

	// Shoulders: digital buttons, else digital hat 1, else the analogue HAT1 axes.
	bool lb = key(PAD_BTN_LB, PADDB_BTN_TL);
	bool rb = key(PAD_BTN_RB, PADDB_BTN_TR);
	if (!(lb && rb) && idx.hasHat[1]) {
		L.btn[PAD_BTN_LB] = src_hat(1, PADDB_HAT_DOWN);    // left = mask 4
		L.btn[PAD_BTN_RB] = src_hat(1, PADDB_HAT_RIGHT);   // right = mask 2
		lb = rb = true;
	}
	if (!lb && idx.hasAxis[PADDB_ABS_HAT1Y]) { L.btn[PAD_BTN_LB] = src_axis(PADDB_ABS_HAT1Y); lb = true; }
	if (!rb && idx.hasAxis[PADDB_ABS_HAT1X]) { L.btn[PAD_BTN_RB] = src_axis(PADDB_ABS_HAT1X); rb = true; }

	// Is the right stick on Z/RZ? (SDL's right-stick rule, evaluated early
	// because it decides what Z/RZ may still be used for below.)
	const bool rightOnRxRy = idx.hasAxis[PADDB_ABS_RX] && idx.hasAxis[PADDB_ABS_RY];
	const bool rightOnZRz  = !rightOnRxRy && idx.hasAxis[PADDB_ABS_Z] && idx.hasAxis[PADDB_ABS_RZ];

	// Triggers: Linux gamepad spec (HAT2), Android convention (BRAKE/GAS), then
	// the de-facto Xbox 360 / PlayStation Z/RZ.
	// DELIBERATE DEVIATION FROM SDL: when Z/RZ are the right stick, SDL still
	// offers them as the triggers as well, so one physical stick would drive a
	// trigger and a stick axis at once. Here they never do both.
	bool lt = false, rt = false;
	if (idx.hasAxis[PADDB_ABS_HAT2Y])      { L.btn[PAD_BTN_LT] = src_axis(PADDB_ABS_HAT2Y); lt = true; }
	else if (idx.hasAxis[PADDB_ABS_BRAKE]) { L.btn[PAD_BTN_LT] = src_axis(PADDB_ABS_BRAKE); lt = true; }
	else if (idx.hasAxis[PADDB_ABS_Z] && !rightOnZRz) { L.btn[PAD_BTN_LT] = src_axis(PADDB_ABS_Z); lt = true; }

	if (idx.hasAxis[PADDB_ABS_HAT2X])      { L.btn[PAD_BTN_RT] = src_axis(PADDB_ABS_HAT2X); rt = true; }
	else if (idx.hasAxis[PADDB_ABS_GAS])   { L.btn[PAD_BTN_RT] = src_axis(PADDB_ABS_GAS); rt = true; }
	else if (idx.hasAxis[PADDB_ABS_RZ] && !rightOnZRz) { L.btn[PAD_BTN_RT] = src_axis(PADDB_ABS_RZ); rt = true; }

	if (!(lt && rt) && idx.hasHat[2]) {
		L.btn[PAD_BTN_LT] = src_hat(2, PADDB_HAT_DOWN);
		L.btn[PAD_BTN_RT] = src_hat(2, PADDB_HAT_RIGHT);
		lt = rt = true;
	}
	if (!lt) lt = key(PAD_BTN_LT, PADDB_BTN_TL2);
	if (!rt) rt = key(PAD_BTN_RT, PADDB_BTN_TR2);

	// D-pad: the four BTN_DPAD_* keys, else digital hat 0, else the analogue
	// HAT0 axes as halves, else xpad's BTN_TRIGGER_HAPPY1..4.
	int dp = 0;
	if (key(PAD_BTN_DPAD_UP,    PADDB_BTN_DPAD_UP))    dp |= 1;
	if (key(PAD_BTN_DPAD_DOWN,  PADDB_BTN_DPAD_DOWN))  dp |= 2;
	if (key(PAD_BTN_DPAD_LEFT,  PADDB_BTN_DPAD_LEFT))  dp |= 4;
	if (key(PAD_BTN_DPAD_RIGHT, PADDB_BTN_DPAD_RIGHT)) dp |= 8;
	if (dp != 15) {
		if (idx.hasHat[0]) {
			L.btn[PAD_BTN_DPAD_UP]    = src_hat(0, PADDB_HAT_UP);
			L.btn[PAD_BTN_DPAD_DOWN]  = src_hat(0, PADDB_HAT_DOWN);
			L.btn[PAD_BTN_DPAD_LEFT]  = src_hat(0, PADDB_HAT_LEFT);
			L.btn[PAD_BTN_DPAD_RIGHT] = src_hat(0, PADDB_HAT_RIGHT);
		} else if (idx.hasAxis[PADDB_ABS_HAT0X] && idx.hasAxis[PADDB_ABS_HAT0Y]) {
			L.btn[PAD_BTN_DPAD_LEFT]  = src_axis(PADDB_ABS_HAT0X, -1);
			L.btn[PAD_BTN_DPAD_RIGHT] = src_axis(PADDB_ABS_HAT0X, +1);
			L.btn[PAD_BTN_DPAD_UP]    = src_axis(PADDB_ABS_HAT0Y, -1);
			L.btn[PAD_BTN_DPAD_DOWN]  = src_axis(PADDB_ABS_HAT0Y, +1);
		} else if (caps.hasKey[PADDB_BTN_TRIGGER_HAPPY1] && caps.hasKey[PADDB_BTN_TRIGGER_HAPPY2] &&
		           caps.hasKey[PADDB_BTN_TRIGGER_HAPPY3] && caps.hasKey[PADDB_BTN_TRIGGER_HAPPY4]) {
			// SDL only does this for the xpad driver; we cannot see the driver
			// from here, so it applies whenever nothing better exists.
			L.btn[PAD_BTN_DPAD_LEFT]  = src_key(PADDB_BTN_TRIGGER_HAPPY1);
			L.btn[PAD_BTN_DPAD_RIGHT] = src_key(PADDB_BTN_TRIGGER_HAPPY2);
			L.btn[PAD_BTN_DPAD_UP]    = src_key(PADDB_BTN_TRIGGER_HAPPY3);
			L.btn[PAD_BTN_DPAD_DOWN]  = src_key(PADDB_BTN_TRIGGER_HAPPY4);
		}
	}

	if (idx.hasAxis[PADDB_ABS_X] && idx.hasAxis[PADDB_ABS_Y]) {
		L.stick[PADDB_STICK_LX] = src_axis(PADDB_ABS_X);
		L.stick[PADDB_STICK_LY] = src_axis(PADDB_ABS_Y);
	}
	if (rightOnRxRy) {
		L.stick[PADDB_STICK_RX] = src_axis(PADDB_ABS_RX);
		L.stick[PADDB_STICK_RY] = src_axis(PADDB_ABS_RY);
	} else if (rightOnZRz) {
		L.stick[PADDB_STICK_RX] = src_axis(PADDB_ABS_Z);
		L.stick[PADDB_STICK_RY] = src_axis(PADDB_ABS_RZ);
	}

	*out = L;
	return true;
}

//------------------------------------------------------------------------------
void pad_db_raw_joystick(const PadDbCaps& /*caps*/, const PadDbIndex& idx, PadLayout* out)
{
	PadLayout L;
	L.raw = true;
	set_source(&L, "raw joystick");

	for (int n = 0; n < PAD_BTN_COUNT && n < idx.nButtons; n++)
		L.btn[n] = src_key(idx.btnCode[n]);

	if (idx.hasAxis[PADDB_ABS_X] && idx.hasAxis[PADDB_ABS_Y]) {
		L.stick[PADDB_STICK_LX] = src_axis(PADDB_ABS_X);
		L.stick[PADDB_STICK_LY] = src_axis(PADDB_ABS_Y);
	}
	if (idx.hasAxis[PADDB_ABS_RX] && idx.hasAxis[PADDB_ABS_RY]) {
		L.stick[PADDB_STICK_RX] = src_axis(PADDB_ABS_RX);
		L.stick[PADDB_STICK_RY] = src_axis(PADDB_ABS_RY);
	} else if (idx.hasAxis[PADDB_ABS_Z] && idx.hasAxis[PADDB_ABS_RZ]) {
		L.stick[PADDB_STICK_RX] = src_axis(PADDB_ABS_Z);
		L.stick[PADDB_STICK_RY] = src_axis(PADDB_ABS_RZ);
	}

	if (idx.hasHat[0]) {
		L.btn[PAD_BTN_DPAD_UP]    = src_hat(0, PADDB_HAT_UP);
		L.btn[PAD_BTN_DPAD_DOWN]  = src_hat(0, PADDB_HAT_DOWN);
		L.btn[PAD_BTN_DPAD_LEFT]  = src_hat(0, PADDB_HAT_LEFT);
		L.btn[PAD_BTN_DPAD_RIGHT] = src_hat(0, PADDB_HAT_RIGHT);
	}
	*out = L;
}

//------------------------------------------------------------------------------
// SDL's FixupDeviceInfoForMapping: the version it looks mappings up under.
uint16_t pad_db_mapping_version(const PadDbCaps& caps)
{
	// Xbox One Elite Series 2, first firmware: it duplicated every input (it has
	// BTN_TRIGGER_HAPPY1), and SDL files that layout under version 0x0902.
	if (caps.vendor == 0x045e && caps.product == 0x0b05 && caps.version == 0x0903 &&
	    caps.hasKey[PADDB_BTN_TRIGGER_HAPPY1])
		return 0x0902;
	// Atari VCS modern/classic controllers: the version is the firmware's, the
	// layout is stable.
	if (caps.vendor == 0x3250 && (caps.product == 0x1001 || caps.product == 0x1002))
		return 0;
	return caps.version;
}

void pad_db_resolve(const PadDbCaps& caps, const PadDbList* lists, int nLists,
                    PadLayout* out, bool* isGamepadLayout)
{
	PadDbIndex idx;
	pad_db_build_index(caps, &idx);

	uint8_t guid[16];
	pad_db_make_guid(caps.bus, caps.vendor, caps.product, pad_db_mapping_version(caps),
	                 caps.name, guid);

	int li = -1;
	if (const char* line = pad_db_find(guid, lists, nLists, &li)) {
		PadLayout L;
		if (pad_db_parse_mapping(line, idx, &L)) {
			char name[64];
			pad_db_mapping_name(line, name, sizeof(name));
			char src[96];
			snprintf(src, sizeof(src), "%s: %s", (li >= 0 && lists[li].label) ? lists[li].label : "db", name);
			set_source(&L, src);
			*out = L;
			if (isGamepadLayout) *isGamepadLayout = true;
			return;
		}
	}

	PadLayout L;
	if (pad_db_automap(caps, idx, &L)) {
		*out = L;
		if (isGamepadLayout) *isGamepadLayout = true;
		return;
	}

	pad_db_raw_joystick(caps, idx, &L);
	*out = L;
	if (isGamepadLayout) *isGamepadLayout = false;
}

PadCaps pad_db_layout_caps(const PadLayout& l)
{
	PadCaps c;
	c.btnSouth    = l.gamepad && l.btn[PAD_BTN_A].kind != PADSRC_NONE;
	c.btnJoystick = l.raw;
	c.absRx       = l.stick[PADDB_STICK_RX].kind != PADSRC_NONE;
	c.absRy       = l.stick[PADDB_STICK_RY].kind != PADSRC_NONE;
	c.absZ        = l.btn[PAD_BTN_LT].kind != PADSRC_NONE || l.btn[PAD_BTN_RT].kind != PADSRC_NONE;
	c.absHat0X    = l.btn[PAD_BTN_DPAD_UP].kind    != PADSRC_NONE ||
	                l.btn[PAD_BTN_DPAD_DOWN].kind  != PADSRC_NONE ||
	                l.btn[PAD_BTN_DPAD_LEFT].kind  != PADSRC_NONE ||
	                l.btn[PAD_BTN_DPAD_RIGHT].kind != PADSRC_NONE;
	return c;
}

void pad_db_load_text(const char* text, std::vector<std::string>* lines)
{
	if (!text || !lines) return;
	const char* s = text;
	while (*s) {
		const char* e = s;
		while (*e && *e != '\n') ++e;

		const char* b = s;
		const char* t = e;
		while (b < t && (*b == ' ' || *b == '\t' || *b == '\r')) ++b;
		while (t > b && (t[-1] == ' ' || t[-1] == '\t' || t[-1] == '\r')) --t;
		if (b < t && *b != '#')
			lines->emplace_back(b, (size_t)(t - b));

		s = *e ? e + 1 : e;
	}
}

//------------------------------------------------------------------------------
// Windows DirectInput.
//------------------------------------------------------------------------------
void pad_db_build_index_dinput(int nButtons, const bool axisPresent[PADDB_DI_AXIS_COUNT],
                               int nPovs, PadDbIndex* out)
{
	*out = PadDbIndex();

	if (nButtons < 0) nButtons = 0;
	if (nButtons > PADDB_DI_MAX_BUTTONS) nButtons = PADDB_DI_MAX_BUTTONS;
	for (int i = 0; i < nButtons; i++) out->btnCode[out->nButtons++] = (uint16_t)i;

	if (nPovs < 0) nPovs = 0;
	if (nPovs > PADDB_DI_MAX_POVS) nPovs = PADDB_DI_MAX_POVS;
	for (int i = 0; i < nPovs; i++) {
		out->hasHat[i] = true;
		out->hatCode[out->nHats++] = (uint16_t)(PADDB_ABS_HAT0X + 2 * i);
	}

	// SDL (SortDevObjects) numbers the axes in DIJOYSTATE2 offset order, which
	// is X, Y, Z, RX, RY, RZ, slider 0, slider 1, counting only those present.
	for (int a = 0; a < PADDB_DI_AXIS_COUNT; a++) {
		if (axisPresent && axisPresent[a]) {
			out->hasAxis[a] = true;
			out->axisCode[out->nAxes++] = (uint16_t)a;
		}
	}
}

bool pad_db_resolve_dinput(const PadDbDinputInfo& info, const PadDbList* lists, int nLists,
                           PadLayout* out, uint8_t guidOut[16])
{
	PadDbIndex idx;
	pad_db_build_index_dinput(info.nButtons, info.axisPresent, info.nPovs, &idx);

	// SDL (DINPUT_JoystickPrepareDevice): USB bus when the vendor and product ids
	// are both known, Bluetooth otherwise; the version is always 0.
	const uint16_t bus = (info.vendor && info.product) ? 3 : 5;

	const char* cand[2] = { info.crcString, info.crcFallback };
	int nCand = 1;
	if (info.crcFallback && info.crcFallback[0] &&
	    !(info.crcString && strcmp(info.crcString, info.crcFallback) == 0))
		nCand = 2;

	for (int i = 0; i < nCand; i++) {
		uint8_t guid[16];
		pad_db_make_guid_ex(bus, info.vendor, info.product, 0, cand[i], info.nameForm, guid);
		if (i == 0 && guidOut) memcpy(guidOut, guid, 16);

		int li = -1;
		const char* line = pad_db_find_for(guid, lists, nLists, "Windows", &li);
		if (!line) continue;

		PadLayout L;
		if (!pad_db_parse_mapping(line, idx, &L)) continue;

		char name[64];
		pad_db_mapping_name(line, name, sizeof(name));
		char src[96];
		snprintf(src, sizeof(src), "%s: %s", (li >= 0 && lists[li].label) ? lists[li].label : "db", name);
		set_source(&L, src);
		*out = L;
		if (guidOut) memcpy(guidOut, guid, 16);
		return true;
	}
	return false;
}

//------------------------------------------------------------------------------
// Axis math (moved here from the evdev backend unchanged, so both platforms
// scale an axis the same way).
//------------------------------------------------------------------------------
namespace {
int clamp_i(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
}

int pad_db_axis_norm255(int raw, int minV, int maxV, int half, bool invert)
{
	if (maxV <= minV) return 0;
	const int centre = (minV + maxV) / 2;
	int n = 0;
	if (half > 0) {
		if (raw > centre && maxV > centre)
			n = (int)(((long long)(raw - centre) * 255) / (maxV - centre));
	} else if (half < 0) {
		if (raw < centre && centre > minV)
			n = (int)(((long long)(centre - raw) * 255) / (centre - minV));
	} else {
		n = (int)(((long long)(raw - minV) * 255) / (maxV - minV));
	}
	n = clamp_i(n, 0, 255);
	return invert ? 255 - n : n;
}

int pad_db_axis_scale(int raw, int minV, int maxV, int flat)
{
	if (maxV <= minV) return 0;

	const int centre = (minV + maxV) / 2;
	int       offset = raw - centre;

	if (flat > 0) {
		if (offset > flat)       offset -= flat;
		else if (offset < -flat) offset += flat;
		else                     return 0;
	}

	const int halfRange = ((maxV - minV) / 2) - flat;
	if (halfRange <= 0) return 0;

	return clamp_i((offset * 127) / halfRange, -128, 127);
}

int pad_db_stick_value(int raw, int minV, int maxV, int flat, int half, bool invert)
{
	if (half != 0)
		return (pad_db_axis_norm255(raw, minV, maxV, half, invert) * 127) / 255;
	const int v = pad_db_axis_scale(raw, minV, maxV, flat);
	return invert ? clamp_i(-v, -128, 127) : v;
}

//------------------------------------------------------------------------------
// Layout description for the log.
//------------------------------------------------------------------------------
namespace {

std::string describe_src(const PadSrc& s, PadDescribeStyle style)
{
	static const char* const kDiAxis[PADDB_DI_AXIS_COUNT] = { "X", "Y", "Z", "RX", "RY", "RZ", "S0", "S1" };
	char b[40];
	switch (s.kind) {
	case PADSRC_KEY:
		if (style == PADDB_DESCRIBE_WINDOWS) snprintf(b, sizeof(b), "b%d", (int)s.code);
		else                                 snprintf(b, sizeof(b), "key0x%x", (unsigned)s.code);
		break;
	case PADSRC_AXIS:
		if (style == PADDB_DESCRIBE_WINDOWS) {
			snprintf(b, sizeof(b), "%s%s%s",
			         s.axisHalf > 0 ? "+" : (s.axisHalf < 0 ? "-" : ""),
			         s.code < PADDB_DI_AXIS_COUNT ? kDiAxis[s.code] : "?",
			         s.invert ? "~" : "");
		} else {
			snprintf(b, sizeof(b), "%sabs%d%s",
			         s.axisHalf > 0 ? "+" : (s.axisHalf < 0 ? "-" : ""),
			         (int)s.code, s.invert ? "~" : "");
		}
		break;
	case PADSRC_HAT:
		snprintf(b, sizeof(b), "hat%d.%d", ((int)s.code - PADDB_ABS_HAT0X) / 2, (int)s.hatMask);
		break;
	default:
		return "-";
	}
	return b;
}

} // namespace

void pad_db_describe_layout(const PadLayout& l, char* out, size_t outSize, PadDescribeStyle style)
{
	if (!out || !outSize) return;
	auto D = [style](const PadSrc& s) { return describe_src(s, style); };

	std::string s;
	s += "LX=" + D(l.stick[PADDB_STICK_LX]);
	s += " LY=" + D(l.stick[PADDB_STICK_LY]);
	s += " RX=" + D(l.stick[PADDB_STICK_RX]);
	s += " RY=" + D(l.stick[PADDB_STICK_RY]);
	s += " LT=" + D(l.btn[PAD_BTN_LT]);
	s += " RT=" + D(l.btn[PAD_BTN_RT]);

	const PadSrc& u = l.btn[PAD_BTN_DPAD_UP];
	const PadSrc& d = l.btn[PAD_BTN_DPAD_DOWN];
	const PadSrc& lf = l.btn[PAD_BTN_DPAD_LEFT];
	const PadSrc& r = l.btn[PAD_BTN_DPAD_RIGHT];
	if (u.kind == PADSRC_HAT && d.kind == PADSRC_HAT && lf.kind == PADSRC_HAT &&
	    r.kind == PADSRC_HAT && u.code == d.code && u.code == lf.code && u.code == r.code) {
		s += " dpad=hat" + std::to_string(((int)u.code - PADDB_ABS_HAT0X) / 2);
	} else {
		s += " dpad=U:" + D(u) + " D:" + D(d) + " L:" + D(lf) + " R:" + D(r);
	}
	s += " A=" + D(l.btn[PAD_BTN_A]) + " B=" + D(l.btn[PAD_BTN_B]) +
	     " X=" + D(l.btn[PAD_BTN_X]) + " Y=" + D(l.btn[PAD_BTN_Y]);
	if (style == PADDB_DESCRIBE_WINDOWS) {
		s += " LB=" + D(l.btn[PAD_BTN_LB]) + " RB=" + D(l.btn[PAD_BTN_RB]) +
		     " Back=" + D(l.btn[PAD_BTN_BACK]) + " Start=" + D(l.btn[PAD_BTN_START]) +
		     " LS=" + D(l.btn[PAD_BTN_LTHUMB]) + " RS=" + D(l.btn[PAD_BTN_RTHUMB]);
	}
	snprintf(out, outSize, "%s", s.c_str());
}
