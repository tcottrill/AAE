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
#include "osd_cpu.h"
#include "glcode.h"
#include "menu.h"
#include "aae_mame_driver.h"
#include "vector_fonts.h"
#include "gl_texturing.h"
#include "colordefs.h"

extern int errorsound;
extern int show_fps;
extern int menulevel;
extern int gamenum;

int leds_status = 0;
static int last_led_status = 0;

//For stickykeys
TOGGLEKEYS g_StartupToggleKeys = { sizeof(TOGGLEKEYS), 0 };
FILTERKEYS g_StartupFilterKeys = { sizeof(FILTERKEYS), 0 };
STICKYKEYS g_StartupStickyKeys = { sizeof(STICKYKEYS), 0 };

#pragma warning( disable : 4244 )

/*-------------------------------------------------
	set_led_status - set the state of a given LED
-------------------------------------------------*/

void set_led_status(int num, int on)
{
	if (on)
		leds_status |= (1 << num);
	else
		leds_status &= ~(1 << num);
}

/*

void set_aae_leds(int a, int b, int c)
{
	int ks = 0;

	if (config.kbleds) {
		if (a > 1)a = 1;
		if (b > 1)b = 1;
		if (c > 1)c = 1;

		if (a > -1) {
			ks = (GetKeyState(VK_NUMLOCK) & 1);
			//LOG_INFO("KS VALUE for Numlock is %d",ks);
			if (a != ks) {
				keybd_event(VK_NUMLOCK, 0x45, KEYEVENTF_EXTENDEDKEY, 0);
				keybd_event(VK_NUMLOCK, 0x45, KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP, 0);
			}
		}

		if (b > -1) {
			ks = (GetKeyState(VK_CAPITAL) & 1);
			//LOG_INFO("KS VALUE for Capital is %d",ks);
			if (b != ks)
			{
				keybd_event(VK_CAPITAL, 0x45, KEYEVENTF_EXTENDEDKEY, 0);
				keybd_event(VK_CAPITAL, 0x45, KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP, 0);
			}
		}

		if (c > -1) {
			ks = (GetKeyState(VK_SCROLL) & 1);
			//LOG_INFO("KS VALUE for Scroll is %d",ks);
			if (c != ks) {
				keybd_event(VK_SCROLL, 0x45, KEYEVENTF_EXTENDEDKEY, 0);
				keybd_event(VK_SCROLL, 0x45, KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP, 0);
			}
		}
	}
}
*/

//TODO: None of below belongs in here

void sanity_check_config(void)
{
	//SANITY CHECKS GO HERE
	if (config.prescale < 1 || config.prescale > 2)
	{
		LOG_INFO("!!!!!Raster prescale set to unsupported value, supported values are 1 and 2!!!!!");
		config.prescale = 2; have_error = 3;
	}

	if (config.anisfilter < 2 || config.anisfilter > 16 || (config.anisfilter % 2 != 0))
	{
		if (config.anisfilter != 0) { //FINAL CHECK
			LOG_INFO("!!!!!Ansitropic Filterings set to unsupported value, supported values are 2,4,8,16 !!!!!");
			have_error = 3;
			config.anisfilter = 0; //RESET TIL FIXED
		}
	}

	if (config.priority < 0 || config.priority > 4)
	{
		LOG_INFO("!!!!!Priority set to unsupported value, supported values are 0,1,2,3,4 - defaulted to 1!!!!!");
		config.priority = 1;
	}
}

void video_loop(void)
{
	//If were displaying the menu, go ahead and show it.
	fontmode_start();

	int err = glGetError();
	if (err != 0)
	{
		LOG_INFO("openglerror in video loop 1: %d", err);
	}
	if (get_menu_status())
	{
		glColor4f(1, 1, 1, 1);
		do_the_menu();
	}
	err = glGetError();
	if (err != 0)
	{
		LOG_INFO("openglerror in video loop 2: %d", err);
	}
	if (show_fps)
	{
		fprint(400.00, 750.0, RGB_WHITE, 2.0, " Speed: %2.0f%% %2.0f out of %d FPS", ((fps_count / frameavg) / Machine->gamedrv->fps) * 100, fps_count / frameavg, Machine->gamedrv->fps);
	}

	if (config.debug)
	{
		fprint(300, 330, RGB_WHITE, 2.0, "sx:%d sy:%d ex:%d ey:%d", msx, msy, esx, esy);
	}

	if (leds_status != last_led_status)
	{
		last_led_status = leds_status;
		osd_set_leds(leds_status);
	}

	//fprint(200.00, 200.0, RGB_WHITE, 2.0, " Menu Level: %d", get_menu_level());

	show_error(); //If there is currently an error condition, show it.
	fontmode_end();
}

// Note to self: Move this to the GL code.


void setup_ambient(int style)
{
	int samplenum;

	if (config.hvnoise)
	{
		samplenum = nameToNum("flyback");

		if (samplenum != -1) sample_set_volume(17, config.noisevol / 3); sample_start(17, nameToNum("flyback"), 1);
	}
	else
		sample_stop(17);

	if (config.psnoise)
	{
		samplenum = nameToNum("psnoise");

		if (samplenum != -1) { sample_start(18, nameToNum("psnoise"), 1); sample_set_volume(18, config.noisevol); }
	}
	else
		sample_stop(18);

	if (config.pshiss)
	{
		samplenum = nameToNum("hiss");

		if (samplenum != -1) {
			sample_set_volume(19, config.noisevol); sample_start(19, nameToNum("hiss"), 1);
		}
	}
	else
		sample_stop(19);
}

void AllowAccessibilityShortcutKeys(int bAllowKeys)
{
	if (bAllowKeys)
	{
		// Restore StickyKeys/etc to original state and enable Windows key
		STICKYKEYS sk = g_StartupStickyKeys;
		TOGGLEKEYS tk = g_StartupToggleKeys;
		FILTERKEYS fk = g_StartupFilterKeys;

		SystemParametersInfo(SPI_SETSTICKYKEYS, sizeof(STICKYKEYS), &g_StartupStickyKeys, 0);
		SystemParametersInfo(SPI_SETTOGGLEKEYS, sizeof(TOGGLEKEYS), &g_StartupToggleKeys, 0);
		SystemParametersInfo(SPI_SETFILTERKEYS, sizeof(FILTERKEYS), &g_StartupFilterKeys, 0);
	}

	else
	{
		// Disable StickyKeys/etc shortcuts but if the accessibility feature is on,
		// then leave the settings alone as its probably being usefully used
		SystemParametersInfo(SPI_GETSTICKYKEYS, sizeof(STICKYKEYS), &g_StartupStickyKeys, 0);
		SystemParametersInfo(SPI_GETTOGGLEKEYS, sizeof(TOGGLEKEYS), &g_StartupToggleKeys, 0);
		SystemParametersInfo(SPI_GETFILTERKEYS, sizeof(FILTERKEYS), &g_StartupFilterKeys, 0);

		FILTERKEYS fkOff = g_StartupFilterKeys;
		STICKYKEYS skOff = g_StartupStickyKeys;
		TOGGLEKEYS tkOff = g_StartupToggleKeys;

		if ((skOff.dwFlags & SKF_STICKYKEYSON) == 0)
		{
			// Disable the hotkey and the confirmation
			skOff.dwFlags &= ~SKF_HOTKEYACTIVE;
			skOff.dwFlags &= ~SKF_CONFIRMHOTKEY;

			SystemParametersInfo(SPI_SETSTICKYKEYS, sizeof(STICKYKEYS), &skOff, 0);
		}

		if ((tkOff.dwFlags & TKF_TOGGLEKEYSON) == 0)
		{
			// Disable the hotkey and the confirmation
			tkOff.dwFlags &= ~TKF_HOTKEYACTIVE;
			tkOff.dwFlags &= ~TKF_CONFIRMHOTKEY;

			SystemParametersInfo(SPI_SETTOGGLEKEYS, sizeof(TOGGLEKEYS), &tkOff, 0);
		}

		if ((fkOff.dwFlags & FKF_FILTERKEYSON) == 0)
		{
			// Disable the hotkey and the confirmation
			fkOff.dwFlags &= ~FKF_HOTKEYACTIVE;
			fkOff.dwFlags &= ~FKF_CONFIRMHOTKEY;

			SystemParametersInfo(SPI_SETFILTERKEYS, sizeof(FILTERKEYS), &fkOff, 0);
		}
	}
}

// Save the current sticky/toggle/filter key settings so they can be restored them later
//SystemParametersInfo(SPI_GETSTICKYKEYS, sizeof(STICKYKEYS), &g_StartupStickyKeys, 0);
//SystemParametersInfo(SPI_GETTOGGLEKEYS, sizeof(TOGGLEKEYS), &g_StartupToggleKeys, 0);
//SystemParametersInfo(SPI_GETFILTERKEYS, sizeof(FILTERKEYS), &g_StartupFilterKeys, 0);