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


#include <allegro.h>
#include "alleggl.h"
#include "winalleg.h"
#include "osd_cpu.h"
#include "glcode.h"
#include "pokey.h"
#include "rand.h"
#include "fonts.h"
#include "samples.h"
#include "menu.h"
#include "aae_mame_driver.h"
#include "vector_fonts.h"
#include "gl_texturing.h"
#include "colordefs.h"

extern int errorsound;
extern int show_fps;
extern int menulevel;
extern int gamenum;

//For stickykeys
TOGGLEKEYS g_StartupToggleKeys = { sizeof(TOGGLEKEYS), 0 };
FILTERKEYS g_StartupFilterKeys = { sizeof(FILTERKEYS), 0 };
STICKYKEYS g_StartupStickyKeys = { sizeof(STICKYKEYS), 0 };



void set_aae_leds(int a, int b, int c)
{
	int ks = 0;

	if (config.kbleds) {
		if (a > 1)a = 1;
		if (b > 1)b = 1;
		if (c > 1)c = 1;

		if (a > -1) {
			ks = (GetKeyState(VK_NUMLOCK) & 1);
			//wrlog("KS VALUE for Numlock is %d",ks);
			if (a != ks) {
				keybd_event(VK_NUMLOCK, 0x45, KEYEVENTF_EXTENDEDKEY, 0);
				keybd_event(VK_NUMLOCK, 0x45, KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP, 0);
			}
		}

		if (b > -1) {
			ks = (GetKeyState(VK_CAPITAL) & 1);
			//wrlog("KS VALUE for Capital is %d",ks);
			if (b != ks)
			{
				keybd_event(VK_CAPITAL, 0x45, KEYEVENTF_EXTENDEDKEY, 0);
				keybd_event(VK_CAPITAL, 0x45, KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP, 0);
			}
		}

		if (c > -1) {
			ks = (GetKeyState(VK_SCROLL) & 1);
			//wrlog("KS VALUE for Scroll is %d",ks);
			if (c != ks) {
				keybd_event(VK_SCROLL, 0x45, KEYEVENTF_EXTENDEDKEY, 0);
				keybd_event(VK_SCROLL, 0x45, KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP, 0);
			}
		}
	}
}

//TODO: None of below belongs in here

//PLease get rid of this
UINT8 randRead(unsigned int address, struct MemoryReadByte* psMemRead)
{
	return randintmm(0, 255);
}

void sanity_check_config(void)
{
	//SANITY CHECKS GO HERE
	if (config.prescale < 1 || config.prescale > 2)
	{
		wrlog("!!!!!Raster prescale set to unsupported value, supported values are 1 and 2!!!!!");
		config.prescale = 2; have_error = 3;
	}

	if (config.anisfilter < 2 || config.anisfilter > 16 || (config.anisfilter % 2 != 0))
	{
		if (config.anisfilter != 0) { //FINAl CHECK
			wrlog("!!!!!Ansitropic Filterings set to unsupported value, supported values are 2,4,8,16 !!!!!");
			have_error = 3;
			config.anisfilter = 0; //RESET TIL FIXED
		}
	}

	if (config.priority < 0 || config.priority > 4)
	{
		wrlog("!!!!!Priority set to unsupported value, supported values are 0,1,2,3,4 - defaulted to 1!!!!!");
		config.priority = 1;
	}
}

void video_loop(void)
{
	//If were displaying the menu, go ahead and show it.
		
	fontmode_start();
	if (get_menu_status())
	{
		glColor4f(1, 1, 1, 1);
		do_the_menu();
	}

	if (show_fps)
	{
		fprint(400.00, 750.0, RGB_WHITE, 2.0, " Speed: %2.0f%% %2.0f out of %d FPS", ((fps_count / frameavg) / driver[gamenum].fps) * 100, fps_count / frameavg, driver[gamenum].fps);
	}
	if (config.debug)
	{
		fprint(300, 330, RGB_WHITE, 2.0, "sx:%d sy:%d ex:%d ey:%d", msx, msy, esx, esy);
	}
	show_error(); //If there is currently an error condition, show it.
	fontmode_end();
}

// Note to self: Move this to the GL code. 

void return_to_menu(void)
{
	free_samples(); //Free and allocated Samples
	free_game_textures(); //Free textures
	wrlog("Done Freeing All"); //Log it.
	gamenum = 0; //Set gamenum to zero (menu)
	done = 0; //Set done false
}
//
// Code below is currently disabled with a return. Why?
//
void setup_ambient(int style)
{
	return;

	if (gamenum == 0) return;

	if (config.hvnoise)
	{
		if (num_samples == 5) { sample_start(1, num_samples - 1, 1); sample_set_volume(1, config.noisevol); }
		else { sample_start(13, num_samples - 1, 1); sample_set_volume(13, config.noisevol); }
	}
	if (config.hvnoise == 0)

	{
		if (num_samples == 5) { sample_stop(1); }
		else { sample_stop(13); }
	}
	if (config.psnoise)
	{
		if (num_samples == 5) { sample_start(2, num_samples - 2, 1); sample_set_volume(2, config.noisevol); }
		else { sample_start(14, num_samples - 2, 1); sample_set_volume(14, config.noisevol); }
	}
	if (config.psnoise == 0)

	{
		if (num_samples == 5) { sample_stop(2); }
		else { sample_stop(14); }
	}
	if (config.pshiss)
	{
		if (num_samples == 5) { sample_start(3, num_samples - 3, 1); sample_set_volume(3, config.noisevol); }
		else { sample_start(15, num_samples - 3, 1); sample_set_volume(15, config.noisevol); }
	}
	if (config.pshiss == 0)

	{
		if (num_samples == 5) { sample_stop(3); }
		else { sample_stop(15); }
	}

	/*
		if (style==RASTER){
						   if (config.pshiss)
						   {play_sample(aae_sounds[1],config.noisevol,128,1000,1);}
						  }

		if (style==0) {
					   stop_sample(aae_sounds[1]);
					   stop_sample(aae_sounds[2]);
					   stop_sample(aae_sounds[3]);
					  }
			*/
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