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
//#include "loaders.h"

#include "sys_video/glcode.h"
#include "sndhrdw/pokey.h"
#include "rand.h"
#include "sys_video/fonts.h"
#include "sndhrdw/samples.h"
#include "menu.h"
#include "aae_mame_driver.h"
#include "log.h"
#include "sys_video/vector_fonts.h"

extern int errorsound;
extern int show_fps;
extern int menulevel;
extern int gamenum;

//PLease Move this
int vector_timer(int deltax, int deltay)
{
	deltax = abs(deltax);
	deltay = abs(deltay);

	if (deltax > deltay)
		return deltax >> VEC_SHIFT;
	else
		return deltay >> VEC_SHIFT;
}

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

//Please Move this
void NoWrite(UINT32 address, UINT8 data, struct MemoryWriteByte* psMemWrite)
{
	;
}

//Please Move this
void playstreamedsample(int channel, signed char* data, int len, int vol)
{
	int i;
	unsigned char* p = (unsigned char*)get_audio_stream_buffer(stream);

	if (!p) return;
	if (channel) return;
	//Streams[channel].bUpdated=TRUE;
	//Streams[channel].nVol=vol;

	for (i = 0; i < (len); i++) {
		p[i] = data[i];

		((short*)p)[i] ^= 0x8000;
	}

	free_audio_stream_buffer(stream);
}

//PLease move this
void do_AYsound(void)
{
	int i;

	signed char* p = (signed char*)get_audio_stream_buffer(stream);

	if (p)
	{  //AYUpdate();
		for (i = 0; i < (1500); i++) {
			p[i] = aybuffer[i];

			((short*)p)[i] ^= 0x8000;
		}
		free_audio_stream_buffer(stream);
	}
}

//TODO: Relocate sound section above

void center_window(void)
{
	//   long lWS;
   //    long lWSEX;

	HWND wnd = win_get_window();
	HWND desktop = GetDesktopWindow();
	RECT wndRect, desktopRect;
	int  w, h, dw, dh;

	GetWindowRect(wnd, &wndRect);
	GetWindowRect(desktop, &desktopRect);
	w = wndRect.right - wndRect.left;
	h = wndRect.bottom - wndRect.top;
	dw = desktopRect.right - desktopRect.left;
	dh = desktopRect.bottom - desktopRect.top;

	MoveWindow(wnd, (dw - w) / 2, (dh - h) / 2, w, h, TRUE);

	//lWS = GetWindowLong(wnd,GWL_STYLE); // for current WS style.
	//lWSEX = GetWindowLong(WS_EX); //for current WS_EX style.

	// Bitwise /*OR*/AND the flags you don't want (WS_OVERLAPPEDWINDOW etc.)
	//lWS /*=|*/ &= ~WS_OVERLAPPEDWINDOW;
	//lWSEX /*=|*/ &= ~WS_EX_WHATEVERELSE

	// Add the flags you want.
	//lWS /*=&*/ |= WS_POPUP;
	//lWSEX /*=&*/ |= WS_EX_WHATEVERELSE

	// Set the new styles:
	//SetWindowLong(wnd,GWL_STYLE,lWS);
	//SetWindowLong(wnd,GWL_STYLE,lWSEX);
	//THIS IS JUST FOR TESTING PURPOSES!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
	//Update window
	//SetWindowPos(wnd,HWND_TOP,0,SCREEN_H,SCREEN_W,SCREEN_H,SWP_SHOWWINDOW );
}

void Set_ForeGround(void)
{
	HWND hCurrWnd;
	int iMyTID;
	int iCurrTID;

	hCurrWnd = win_get_window();
	iMyTID = GetCurrentThreadId();
	iCurrTID = GetWindowThreadProcessId(hCurrWnd, 0);

	AttachThreadInput(iMyTID, iCurrTID, TRUE);
	SetForegroundWindow(hCurrWnd);
	AttachThreadInput(iMyTID, iCurrTID, FALSE);
	remove_mouse(); //Reset mouse to ensure cursor is removed and to regain exclusive control
	//install_mouse();
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
	HWND hwnd;
	hwnd = GetForegroundWindow();
	// glPrint(200, 230, 255,255,255,255,1,0,0,"Hwind %x",hwnd);

	//show_info();

	//If were displaying the menu, go ahead and show it.
	if (get_menu_status())
	{
		fontmode_start();
		glColor4f(1, 1, 1, 1);
		do_the_menu();
		fontmode_end();
	}

	show_error(); //If there is currently an error condition, show it.

	if (show_fps)
	{
		glPrint(535, 740, 255, 255, 175, 255, .8, 0, 0, " Speed: %2.0f %% %2.0f out of %d frames per second", ((fps_count / frameavg) / driver[gamenum].fps) * 100, fps_count / frameavg, driver[gamenum].fps);
	}
	if (config.debug)
	{
		glPrint(300, 330, 255, 255, 255, 255, 1, 0, 0, "sx:%d sy:%d ex:%d ey:%d", msx, msy, esx, esy);
	}
}

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