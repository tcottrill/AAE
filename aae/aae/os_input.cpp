//============================================================================
// AAE is a poorly written M.A.M.E (TM) derivitave based on early MAME
// code, 0.29 through .90 mixed with code of my own. This emulator was
// created solely for my amusement and learning and is provided only
// as an archival experience.
//
// All MAME code used and abused in this emulator remains the copyright
// of the dedicated people who spend countless hours creating it. All
// MAME code should be annotated as belonging to the MAME TEAM.
//
// SOME CODE BELOW IS FROM MAME and COPYRIGHT the MAME TEAM.
//============================================================================

#include "os_input.h"
#include "osdepend.h"
#include "inptport.h"     // input_type_key(), IPT_UI_* enum values
#include "joystick.h"
#include "sys_input.h"
#include "os_basic.h"
#include "sys_log.h"
#include "config.h"       // config.mouse_player[] for osd_trak_read

//----------------------------------------------------------------------------
// Build-time boundary test (the Phase 1 idiom, extended here in Phase 3b).
//
// os_input.cpp is aae_core code: it must build on Linux and, eventually, on a
// freestanding Teensy. Nothing it includes may drag in windows.h.
//
// This is exactly the leak that went unnoticed until Phase 3b. joystick.h
// included <windows.h> unconditionally, and Phase 2's boundary test could not
// catch it - that test works by excluding include DIRECTORIES from aae_core's
// search path, and system/input is legitimately on that path because the core
// needs sys_input.h. Only a guard inside the translation unit closes the gap.
//
// _WINDOWS_ is windows.h's own include guard, so this fires on any path that
// reaches it, however indirect. Verified to actually trigger, rather than
// assumed - three guards shipped in Phase 1 could never fire because their
// macro name was guessed.
//----------------------------------------------------------------------------
#ifdef _WINDOWS_
#error "os_input.cpp is core code and must not see windows.h - check your includes"
#endif

// Phase 4 boundary check (spec sec. 3.5): the emulation core must never see
// Vulkan headers. Mirrors the _WINDOWS_ leak guard idiom from Phase 1.
#ifdef VULKAN_H_
#error "os_input.cpp is core code and must not see vulkan.h - check your includes"
#endif

int joy_type = -1;
int use_mouse = 1;
int joystick = 1;

void os_init_input(void)
{
	//int err;

	osd_set_leds(0);    /* turn off all leds */

	if (joystick != JOY_TYPE_NONE)
	{
		if (install_joystick() != 0)
		{
			LOG_INFO("Joystick not found.\n");
			joystick = JOY_TYPE_NONE;
		}
		//}
		else if (joystick != joy_type)
		{
			if (install_joystick() != 0)
			{
				LOG_INFO("Joystick not found.\n");
				joystick = JOY_TYPE_NONE;
			}
		}

		if (joystick == JOY_TYPE_NONE)
			LOG_INFO("Joystick not found\n");
		else
			LOG_INFO("Installed Joystick");//%s %s\n",	joystick_driver->name, joystick_driver->desc);
	}

	if (use_mouse)
		use_mouse = 1;
	else
		use_mouse = 1;
}

void os_shutdown_input(void)
{
}

// ---------------------------------------------------------------------------
// pseudo_to_key_code
// Translate a logical UI keycode (OSD_KEY_CANCEL, OSD_KEY_RESET_MACHINE,
// OSD_KEY_UI_LEFT, etc.) into the physical key currently bound to it.
//
// AAE (2026-05-29): the original MAME-derived hardcoded switch is replaced
// with a lookup into inputport_defaults[] via input_type_key(). The current
// binding for each pseudo-keycode is whatever the user (or the compiled-in
// default) has assigned to the corresponding IPT_UI_* row. See AAE-vs-MAME
// divergence note at the top of inptport.cpp.
//
// Removed in the same pass: the modifier-dispatch logic for SHOW_FPS /
// SHOW_PROFILE / SHOW_TOTAL_COLORS (the latter two had no consumers), and
// the seven dead pseudo-key cases (SHOW_GFX, CHEAT_TOGGLE, FRAMESKIP_INC,
// FRAMESKIP_DEC, SHOW_PROFILE, SHOW_TOTAL_COLORS, ON_SCREEN_DISPLAY).
//
// OSD_KEY_FAST_EXIT is kept as a source-compatibility alias for OSD_KEY_CANCEL
// — both resolve through IPT_UI_CANCEL.
// ---------------------------------------------------------------------------
static int pseudo_to_key_code(int keycode)
{
	switch (keycode)
	{
	case OSD_KEY_CANCEL:        return input_type_key(IPT_UI_CANCEL);
	case OSD_KEY_FAST_EXIT:     return input_type_key(IPT_UI_CANCEL);   // alias
	case OSD_KEY_RESET_MACHINE: return input_type_key(IPT_UI_RESET_MACHINE);
	case OSD_KEY_THROTTLE:      return input_type_key(IPT_UI_THROTTLE);
	case OSD_KEY_SHOW_FPS:      return input_type_key(IPT_UI_SHOW_FPS);
	case OSD_KEY_SNAPSHOT:      return input_type_key(IPT_UI_SNAPSHOT);
	case OSD_KEY_CONFIGURE:     return input_type_key(IPT_UI_CONFIGURE);
	case OSD_KEY_UI_LEFT:       return input_type_key(IPT_UI_LEFT);
	case OSD_KEY_UI_RIGHT:      return input_type_key(IPT_UI_RIGHT);
	case OSD_KEY_UI_UP:         return input_type_key(IPT_UI_UP);
	case OSD_KEY_UI_DOWN:       return input_type_key(IPT_UI_DOWN);
	case OSD_KEY_UI_SELECT:     return input_type_key(IPT_UI_SELECT);
	}
	return keycode;     // not a pseudo-key, pass through
}

// osd_key_invalid() was removed in the UI-key remap refactor (2026-05-29).
// It used to reserve a set of "system" keys (ESC/F3/F4/F5/F9/F10/F11/TAB/TILDE)
// so they couldn't be assigned to game inputs. The reservation no longer makes
// sense now that those keys are themselves rebindable through KEY CONFIG
// (GLOBAL). If you bind ESC to a game input AND leave ESC bound to "Cancel",
// both actions fire — that's the user's problem to resolve via the menu.

/*
 * Check if a key is pressed. The keycode is the standard PC keyboard
 * code, as defined in osdepend.h. Return 0 if the key is not pressed,
 * nonzero otherwise. Handle pseudo keycodes.
 */
int osd_key_pressed(int keycode)
{
	if (keycode == OSD_KEY_ANY)
		return osd_read_key_immediate();

	keycode = pseudo_to_key_code(keycode);

	if (keycode > OSD_MAX_KEY) return 0;

	if (keycode == OSD_KEY_RCONTROL) keycode = AAEKEY_RCONTROL;
	if (keycode == OSD_KEY_ALTGR) keycode = AAEKEY_ALTGR;
	if (keycode == OSD_KEY_PAUSE)
	{
		static int pressed, counter;
		int res;

		keycode = AAEKEY_PAUSE;
		res = key[keycode] ^ pressed;
		if (res)
		{
			if (counter > 0)
			{
				if (--counter == 0)
					pressed = key[keycode];
			}
			else counter = 4;
		}

		return res;
	}
	//if (key[keycode])LOG_INFO("read key immediate returning %d name: %s",keycode,osd_key_name(keycode));
	return key[keycode];
}

/*
 * Player-aware key check for GAME inputs (multi-keyboard). Routes the check
 * to the keyboard assigned to `player` in the ANALOG CONFIG menu:
 *   -2 = none, -1 = system (merged, the default -- identical to
 *   osd_key_pressed), 0.. = a specific device, resolved by PATH first so
 *   the assignment survives reboots. An assigned-but-unplugged keyboard
 *   yields no input rather than falling back to someone else's.
 * UI/menu keys and anything that isn't a per-player game bit should keep
 * calling plain osd_key_pressed().
 */
int osd_key_pressed_for(int player, int keycode)
{
	int dev = -1;   // merged by default

	if (player >= 0 && player < 4)
	{
		if (config.kbd_player_path[player][0])
		{
			dev = RawInput_FindKeyboardByPath(config.kbd_player_path[player]);
			// Assigned keyboard not attached: fall back to the merged
			// SYSTEM state (all keyboards) rather than locking the player
			// out of their digital controls entirely. Unlike a mouse, a
			// merged keyboard fallback can't feed a WRONG device's motion
			// to the player -- worst case is the classic shared-keyboard
			// behavior. The assignment self-heals when the device returns.
			if (dev < 0) dev = -1;
		}
		else
			dev = config.kbd_player[player];

		if (dev == -2) return 0;                // explicitly no keyboard
	}

	if (dev < 0)
		return osd_key_pressed(keycode);        // merged legacy path

	// specific device: same translation as osd_key_pressed
	if (keycode == OSD_KEY_ANY)
		return osd_read_key_immediate();
	keycode = pseudo_to_key_code(keycode);
	if (keycode > OSD_MAX_KEY) return 0;
	if (keycode == OSD_KEY_RCONTROL) keycode = AAEKEY_RCONTROL;
	if (keycode == OSD_KEY_ALTGR) keycode = AAEKEY_ALTGR;
	if (keycode == OSD_KEY_PAUSE) keycode = AAEKEY_PAUSE;

	return RawInput_IsKeyDownEx(dev, keycode);
}

static char memory[256];

/* Report a key as pressed only when the user hits it, not while it is */
/* being kept pressed. */
int osd_key_pressed_memory(int keycode)
{
	int res = 0;

	keycode = pseudo_to_key_code(keycode);

	if (osd_key_pressed(keycode))
	{
		if (keycode == OSD_KEY_ANY) return 1;

		if (memory[keycode] == 0) res = 1;
		memory[keycode] = 1;
	}
	else
		memory[keycode] = 0;

	return res;
}

/* report key as pulsing while it is pressed */
int osd_key_pressed_memory_repeat(int keycode, int speed)
{
	static int counter, keydelay;
	int res = 0;

	keycode = pseudo_to_key_code(keycode);

	if (osd_key_pressed(keycode))
	{
		if (memory[keycode] == 0)
		{
			keydelay = 3;
			counter = 0;
			res = 1;
		}
		else if (++counter > keydelay * speed * 60 / 60)//Machine->drv->frames_per_second)//Machine->drv->frames_per_second
		{
			keydelay = 1;
			counter = 0;
			res = 1;
		}
		memory[keycode] = 1;
	}
	else
		memory[keycode] = 0;

	return res;
}

/* If the user presses a key return it, otherwise return OSD_KEY_NONE. */
/* DO NOT wait for the user to press a key */
int osd_read_key_immediate(void)
{
	int res;

	/* first of all, record keys which are NOT pressed */
	for (res = OSD_MAX_KEY; res > OSD_KEY_NONE; res--)
	{
		if (!osd_key_pressed(res))
		{
			memory[res] = 0;
		}
	}

	for (res = OSD_MAX_KEY; res > OSD_KEY_NONE; res--)
	{
		if (osd_key_pressed(res))
		{
			if (memory[res] == 0)
			{
				memory[res] = 1;
			}
			else res = OSD_KEY_NONE;
			break;
		}
	}

	return res;
}

/* return the name of a key */ //--ERROR KEY NAMES NEED TO GO TO 200
const char* osd_key_name(int keycode)
{
	static const char* keynames[] =
	{
		"ESC", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "MINUS", "EQUAL", "BKSPACE",
		"TAB", "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "OPBRACE", "CLBRACE", "ENTER",
		"LCTRL", "A", "S", "D", "F", "G", "H", "J", "K", "L", "COLON", "QUOTE", "TILDE",
		"LSHIFT", "Error", "Z", "X", "C", "V", "B", "N", "M", "COMMA", ".", "SLASH", "RSHIFT",
		"*", "ALT", "SPACE", "CAPSLOCK", "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10",
		"NUMLOCK", "SCRLOCK", "HOME", "UP", "PGUP", "MINUS PAD",
		"LEFT", "5 PAD", "RIGHT", "PLUS PAD", "END", "DOWN",
		"PGDN", "INS", "DEL", "PRTSCR", "Error", "Error",
		"F11", "F12", "Error", "Error",
		"LWIN", "RWIN", "MENU", "RCTRL", "ALTGR", "PAUSE",
		"Error", "Error", "Error", "Error",
		"1 PAD", "2 PAD", "3 PAD", "4 PAD", "Error",
		"6 PAD", "7 PAD", "8 PAD", "9 PAD", "0 PAD",
		". PAD", "= PAD", "/ PAD", "* PAD", "ENTER PAD",
	};
	static const char* nonedefined = "None";

	if (keycode && keycode <= OSD_MAX_KEY) return keynames[keycode - 1];
	else return (char*)nonedefined;
}

/* return the name of a joystick button */
const char* osd_joy_name(int joycode)
{
	static const char* joynames[] = {
		"Left", "Right", "Up", "Down", "Button 1",
		"Button 2", "Button 3", "Button 4", "Button 5", "Button 6",
		"Button 7", "Button 8", "Button 9", "Button 10", "Any Button",
		"J2 Left", "J2 Right", "J2 Up", "J2 Down", "J2 Button 1",
		"J2 Button 2", "J2 Button 3", "J2 Button 4", "J2 Button 5", "J2 Button 6",
		"J2 Button 7", "J2 Button 8", "J2 Button 9", "J2 Button 10", "J2 Any Button",
		"J3 Left", "J3 Right", "J3 Up", "J3 Down", "J3 Button 1",
		"J3 Button 2", "J3 Button 3", "J3 Button 4", "J3 Button 5", "J3 Button 6",
		"J3 Button 7", "J3 Button 8", "J3 Button 9", "J3 Button 10", "J3 Any Button",
		"J4 Left", "J4 Right", "J4 Up", "J4 Down", "J4 Button 1",
		"J4 Button 2", "J4 Button 3", "J4 Button 4", "J4 Button 5", "J4 Button 6",
		"J4 Button 7", "J4 Button 8", "J4 Button 9", "J4 Button 10", "J4 Any Button"
	};

	if (joycode == 0) return "None";
	else if (joycode <= OSD_MAX_JOY) return (char*)joynames[joycode - 1];
	else return "Unknown";
}

void osd_poll_joysticks(void)
{
	if (joystick > JOY_TYPE_NONE)
		poll_joystick();
}

static int resolve_joy(int n);   /* defined below, near osd_analogjoy_read */

/* check if the joystick is moved in the specified direction, defined in */
/* osdepend.h. Return 0 if it is not pressed, nonzero otherwise. */
int osd_joy_pressed(int joycode)
{
	int joy_num;

	/* which joystick are we polling? */
	if (joycode < OSD_JOY_LEFT)
		return 0;
	else if (joycode < OSD_JOY2_LEFT)
	{
		joy_num = 0;
	}
	else if (joycode < OSD_JOY3_LEFT)
	{
		joy_num = 1;
		joycode = joycode - OSD_JOY2_LEFT + OSD_JOY_LEFT;
	}
	else if (joycode < OSD_JOY4_LEFT)
	{
		joy_num = 2;
		joycode = joycode - OSD_JOY3_LEFT + OSD_JOY_LEFT;
	}
	else if (joycode < OSD_MAX_JOY)
	{
		joy_num = 3;
		joycode = joycode - OSD_JOY4_LEFT + OSD_JOY_LEFT;
	}
	else
		return 0;

	if (joy_num == 0)
	{
		/* special case for mouse buttons */
		switch (joycode)
		{
		case OSD_JOY_FIRE1:
			if (mouse_b & 1) return 1; break;
		case OSD_JOY_FIRE2:
			if (mouse_b & 2) return 1; break;
		case OSD_JOY_FIRE3:
			if (mouse_b & 4) return 1; break;
		case OSD_JOY_FIRE: /* any mouse button */
			if (mouse_b) return 1; break;
		}
	}

	/* route through the INPUT DEVICES per-player assignment */
	joy_num = resolve_joy(joy_num);
	if (joy_num < 0)
		return 0;

	/* do we have as many sticks? */
	if (joy_num + 1 > num_joysticks)
		return 0;

	switch (joycode)
	{
	case OSD_JOY_LEFT:
		return joy[joy_num].stick[0].axis[0].d1;
		break;
	case OSD_JOY_RIGHT:
		return joy[joy_num].stick[0].axis[0].d2;
		break;
	case OSD_JOY_UP:
		return joy[joy_num].stick[0].axis[1].d1;
		break;
	case OSD_JOY_DOWN:
		return joy[joy_num].stick[0].axis[1].d2;
		break;
	case OSD_JOY_FIRE1:
		return joy[joy_num].button[0].b;
		break;
	case OSD_JOY_FIRE2:
		return joy[joy_num].button[1].b;
		break;
	case OSD_JOY_FIRE3:
		return joy[joy_num].button[2].b;
		break;
	case OSD_JOY_FIRE4:
		return joy[joy_num].button[3].b;
		break;
	case OSD_JOY_FIRE5:
		return joy[joy_num].button[4].b;
		break;
	case OSD_JOY_FIRE6:
		return joy[joy_num].button[5].b;
		break;
	case OSD_JOY_FIRE7:
		return joy[joy_num].button[6].b;
		break;
	case OSD_JOY_FIRE8:
		return joy[joy_num].button[7].b;
		break;
	case OSD_JOY_FIRE9:
		return joy[joy_num].button[8].b;
		break;
	case OSD_JOY_FIRE10:
		return joy[joy_num].button[9].b;
		break;
	case OSD_JOY_FIRE:
	{
		int i;
		for (i = 0; i < 10; i++)
			if (joy[joy_num].button[i].b)
				return 1;
	}
	break;
	}
	return 0;
}

/* Map a player-facing joystick number through the INPUT DEVICES assignment.
   A stable id string ("DI:{guid}" / "XINPUT:n") wins when set: the device is
   found wherever it currently sits, and an unplugged device yields no input.
   Otherwise: -1 = AUTO (stick N drives player N), -2 = none, 0.. = a
   specific slot. Returns -1 for "no stick". */
static int resolve_joy(int n)
{
	if (n < 0 || n >= 4) return n;

	if (config.joy_player_id[n][0])
		return joystick_find_by_id(config.joy_player_id[n]);   /* -1 if absent */

	int v = config.joy_player[n];
	if (v == -1) return n;      /* AUTO */
	if (v < 0)  return -1;      /* NONE */
	return v;
}

/* return a value in the range -128 .. 128 (yes, 128, not 127) */
void osd_analogjoy_read(int player, int* analog_x, int* analog_y)
{
	*analog_x = *analog_y = 0;

	player = resolve_joy(player);

	/* is there an analog joystick at all? */
	if (player < 0 || player + 1 > num_joysticks || joystick == JOY_TYPE_NONE)
		return;

	*analog_x = joy[player].stick[0].axis[0].pos;
	*analog_y = joy[player].stick[0].axis[1].pos;
}

void osd_trak_read(int player, int* deltax, int* deltay)
{
	// Route each player's trackball/paddle to its assigned mouse device
	// (set in the ANALOG CONFIG menu):
	//   -2 = no device, -1 = merged system mouse, 0.. = specific mouse.
	// A specific assignment is resolved by DEVICE PATH (stable identity)
	// first, so it survives reboots and enumeration-order changes; if the
	// device is unplugged the player gets no input rather than someone
	// else's mouse. Note the merged read (-1) resets the shared deltas,
	// so only one player should use it at a time -- the defaults do.
	int dev = -2;

	if (player >= 0 && player < 4)
	{
		if (config.mouse_player_path[player][0])
			dev = RawInput_FindMouseByPath(config.mouse_player_path[player]);  // -1 if absent
		else
			dev = config.mouse_player[player];

		// path set but device absent: FindMouseByPath returned -1, which
		// would fall through to the merged mouse -- force silence instead
		if (config.mouse_player_path[player][0] && dev < 0)
			dev = -2;
	}

	if (dev == -2)
	{
		*deltax = *deltay = 0;
		return;
	}

	get_mouse_mickeys_ex(dev, deltax, deltay);
}