//==========================================================================
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
//==========================================================================

#include "framework.h"
#include "opengl_renderer.h"
#include "menu.h"
#include "iniFile.h"
#include "os_input.h"
#include "led_service_handler.h"
#include "aae_mame_driver.h"
#include "mixer.h"
#include "vector_fonts.h"
#include "gl_texturing.h"
#include "colordefs.h"
#include "sys_input.h"

// Boundary guard: nothing acommon.cpp includes may drag in the Win32 API.
// MUST stay below every #include above - see the comment on the glew guard.
//
// Currently commented out: it fires, but not because acommon.cpp needs
// Win32. Task 5 closed the aae_mame_driver.h -> framework.h -> <Windows.h>
// leak this comment used to blame, so that path is gone. Two paths remain,
// both direct rather than transitive: this file's own line 14
// `#include "framework.h"`, and line 15's `#include "opengl_renderer.h"`,
// which includes framework.h directly and is staying that way (render
// headers are legitimately Win32-coupled for now - see Task 5 notes).
// A grep of this file found zero calls to any Win32-touching symbol from
// framework.h (win_get_window, allegro_message, osMessage, GetClientWidth/
// Height, ClipAndHideCursor, ...) - acommon.cpp does not itself need Win32,
// framework.h just leaks it to every one of its includers, itself included.
// Re-enable this guard once framework.h stops dragging in windows.h, or once
// acommon.cpp's own dependency on it and on opengl_renderer.h are removed.
//
// #ifdef _WINDOWS_
// #error "windows.h leaked into acommon.cpp"
// #endif

// Regression guard: this file must never see OpenGL headers. If this fires,
// a render header re-leaked glew.h — fix the header, not this guard.
#ifdef __glew_h__
#error "OpenGL headers leaked into a non-render translation unit"
#endif

extern int errorsound;
extern int show_fps;
extern int menulevel;
extern int gamenum;
extern char g_videoIniPath[];

// Pause Handling
extern int paused;
void pause_audio();
void restore_audio();

int leds_status = 0;
static int last_led_status = 0;

#pragma warning( disable : 4244 )

//TODO: None of below belongs in here


void video_loop(void)
{
	// Handle automatic pausing when the menu is active
	static int last_menu_status = 0;
	static int was_paused_before_menu = 0;

	int current_menu_status = get_menu_status();

	if (current_menu_status != last_menu_status)
	{
		if (current_menu_status)
		{
			// Menu opened: capture current state and pause
			reset_custom_input_state(); // Reset the custom menu key handling. 
			// TODO: HACK to stop multiple keypresses, refactor.
			was_paused_before_menu = paused;
			if (!paused)
			{
				paused = 1;
				pause_audio();
			}
		}
		else
		{
			// Menu closed: unpause only if it wasn't already paused manually
			reset_custom_input_state(); // Reset the custom menu key handling.
			if (!was_paused_before_menu)
			{
				paused = 0;
				restore_audio();
			}
		}
		last_menu_status = current_menu_status;
	}

	//If were displaying the menu, go ahead and show it.
	VF.Begin();
		
	int err = glcode_get_gl_error();
	if (err != 0)
	{
		LOG_INFO("openglerror in video loop 1: %d", err);
	}
	if (get_menu_status())
	{
		do_the_menu();
	}
	err = glcode_get_gl_error();
	if (err != 0)
	{
		LOG_INFO("openglerror in video loop 2: %d", err);
	}
	if (show_fps)
	{
		VF.Print(400.00, 750.0, RGB_WHITE, 2.0, " Speed: %2.0f%% %2.0f out of %d FPS", ((fps_count / frameavg) / Machine->gamedrv->fps) * 100, fps_count / frameavg, Machine->gamedrv->fps);
	}

	if (leds_status != last_led_status)
	{
		last_led_status = leds_status;
		osd_set_leds(leds_status);
	}

	if (config.debug) {
		// Input
		const bool shift = (GetModifierFlags() & 1) != 0;
		const bool ctrl = (GetModifierFlags() & 2) != 0;
		// ----------------------------------------------------------------
		// Raster debug adjustment mode
		// ----------------------------------------------------------------
		const int  STEP = 1;
		const int  LO = -1824, HI = 1884;
		const int  MINW = -2000, MINH = -2000;

		enum Side { NONE, LEFT, RIGHT, BOTTOM, TOP } moved = NONE;

		// Apply movement
		if (key[AAEKEY_RIGHT]) { game_rect_right += (shift ? -STEP : +STEP); moved = RIGHT; }
		else if (key[AAEKEY_LEFT]) { game_rect_left += (shift ? +STEP : -STEP); moved = LEFT; }
		else if (key[AAEKEY_DOWN]) { game_rect_bottom += (shift ? -STEP : +STEP); moved = BOTTOM; }
		else if (key[AAEKEY_UP]) { game_rect_top += (shift ? +STEP : -STEP); moved = TOP; }

		// Clamp each side to limits
		auto clamp = [](int& v, int lo, int hi) { if (v < lo) v = lo; else if (v > hi) v = hi; };
		clamp(game_rect_left, LO, HI);
		clamp(game_rect_right, LO, HI);
		clamp(game_rect_bottom, LO, HI);
		clamp(game_rect_top, LO, HI);

		// Enforce minimum width/height by pushing back the side we just moved
		if (moved == LEFT && game_rect_right - game_rect_left < MINW) game_rect_left = game_rect_right - MINW;
		if (moved == RIGHT && game_rect_right - game_rect_left < MINW) game_rect_right = game_rect_left + MINW;
		if (moved == BOTTOM && game_rect_top - game_rect_bottom < MINH) game_rect_bottom = game_rect_top - MINH;
		if (moved == TOP && game_rect_top - game_rect_bottom < MINH) game_rect_top = game_rect_bottom + MINH;

		// Re-clamp in case MIN push hit bounds
		clamp(game_rect_left, LO, HI);
		clamp(game_rect_right, LO, HI);
		clamp(game_rect_bottom, LO, HI);
		clamp(game_rect_top, LO, HI);

		// F2 = save raster extents to video.ini
		if (osd_key_pressed_memory(OSD_KEY_F2))
		{
			const std::string name = Machine->gamedrv->name;
			SetIniFile(g_videoIniPath);

			if (config.bezel == 0)
			{
				set_config_string(name.c_str(), "full_left", std::to_string(game_rect_left).c_str());
				set_config_string(name.c_str(), "full_right", std::to_string(game_rect_right).c_str());
				set_config_string(name.c_str(), "full_bottom", std::to_string(game_rect_bottom).c_str());
				set_config_string(name.c_str(), "full_top", std::to_string(game_rect_top).c_str());
			}
			else if (config.bezel && config.artcrop == 0) {
				set_config_string(name.c_str(), "bezel_left", std::to_string(game_rect_left).c_str());
				set_config_string(name.c_str(), "bezel_right", std::to_string(game_rect_right).c_str());
				set_config_string(name.c_str(), "bezel_bottom", std::to_string(game_rect_bottom).c_str());
				set_config_string(name.c_str(), "bezel_top", std::to_string(game_rect_top).c_str());
			}

			else if (config.bezel && config.artcrop) {
				set_config_string(name.c_str(), "crop_left", std::to_string(game_rect_left).c_str());
				set_config_string(name.c_str(), "crop_right", std::to_string(game_rect_right).c_str());
				set_config_string(name.c_str(), "crop_bottom", std::to_string(game_rect_bottom).c_str());
				set_config_string(name.c_str(), "crop_top", std::to_string(game_rect_top).c_str());
			}

			LOG_INFO("Raster debug: saved [%s] L:%d R:%d B:%d T:%d", name.c_str(), game_rect_left, game_rect_right, game_rect_bottom, game_rect_top);
		}

		// F3 = reset raster extents to defaults
		if (osd_key_pressed_memory(OSD_KEY_F3))
		{
			game_rect_left = 0; game_rect_right = 1024; game_rect_bottom = 0; game_rect_top = 1024;
		}

		VF.Print(300, 330, RGB_WHITE, 2.0, "RASTER ADJUST");
		VF.Print(300, 305, RGB_WHITE, 2.0, "L:%d R:%d B:%d T:%d", game_rect_left, game_rect_right, game_rect_bottom, game_rect_top);
		VF.Print(300, 270, RGB_YELLOW, 1.6f, "F2=Save  F3=Reset");
	}

	show_error(); //If there is currently an error condition, show it.
	VF.End();
}

// -----------------------------------------------------------------------------
// setup_ambient
//
// Starts or stops the 3 optional ambient audio loops based on config settings.
// The ambient samples (flyback, psnoise, hiss) are loaded separately from the
// game's own samples by load_ambient_samples() in aae_fileio.cpp.
//
// Each sample is looked up by NAME via nameToNum() so the sample ID is
// independent of how many game samples were loaded before these. If a
// sample was not found (nameToNum returns -1), that ambient effect is
// silently skipped.
//
// Dedicated playback channels:
//   Channel 17 = flyback  (CRT horizontal flyback chatter)
//   Channel 18 = psnoise  (power supply hum)
//   Channel 19 = hiss     (background static)
//
// These channels (17-19) are reserved for ambient use and should not be
// used by game drivers. Game drivers typically use channels 0-16.
// -----------------------------------------------------------------------------

// Channel assignments for ambient audio (reserved, not used by game drivers)
static constexpr int AMBIENT_CH_FLYBACK = 17;
static constexpr int AMBIENT_CH_PSNOISE = 18;
static constexpr int AMBIENT_CH_HISS = 19;

// -----------------------------------------------------------------------------
// setup_ambient
//
// Starts/stops and updates the 3 optional ambient audio loops based on config.
// This version is "live-update safe":
//   - Does NOT restart a loop if it is already playing.
//   - Always reapplies volume immediately when enabled.
//   - Stops immediately when disabled.
//
// Parameters:
//   style - currently unused (kept for compatibility).
// -----------------------------------------------------------------------------
void setup_ambient(int style)
{
	(void)style;

	auto clamp255 = [](int v) -> int {
		if (v < 0) return 0;
		if (v > 255) return 255;
		return v;
		};

	const int ambientVol = clamp255(config.noisevol);
	const int flybackVol = clamp255(config.noisevol / 3);

	// -- Flyback (CRT horizontal flyback chatter) --
	if (config.hvnoise)
	{
		int snum = nameToNum("flyback.wav");
		if (snum != -1)
		{
			if (!sample_playing(AMBIENT_CH_FLYBACK))
				sample_start(AMBIENT_CH_FLYBACK, snum, 1);

			sample_set_volume(AMBIENT_CH_FLYBACK, flybackVol);
		}
		else
		{
			// Only log if user enabled it but it is missing.
			LOG_INFO("Ambient: flyback.wav not loaded, skipping HV chatter");
		}
	}
	else
	{
		if (sample_playing(AMBIENT_CH_FLYBACK))
			sample_stop(AMBIENT_CH_FLYBACK);
	}

	// -- Power Supply Noise --
	if (config.psnoise)
	{
		int snum = nameToNum("psnoise.wav");
		if (snum != -1)
		{
			if (!sample_playing(AMBIENT_CH_PSNOISE))
				sample_start(AMBIENT_CH_PSNOISE, snum, 1);

			sample_set_volume(AMBIENT_CH_PSNOISE, ambientVol);
		}
		else
		{
			LOG_INFO("Ambient: psnoise.wav not loaded, skipping PS noise");
		}
	}
	else
	{
		if (sample_playing(AMBIENT_CH_PSNOISE))
			sample_stop(AMBIENT_CH_PSNOISE);
	}

	// -- Background Hiss --
	if (config.pshiss)
	{
		int snum = nameToNum("hiss.wav");
		if (snum != -1)
		{
			if (!sample_playing(AMBIENT_CH_HISS))
				sample_start(AMBIENT_CH_HISS, snum, 1);

			sample_set_volume(AMBIENT_CH_HISS, ambientVol);
		}
		else
		{
			LOG_INFO("Ambient: hiss.wav not loaded, skipping PS hiss");
		}
	}
	else
	{
		if (sample_playing(AMBIENT_CH_HISS))
			sample_stop(AMBIENT_CH_HISS);
	}
}