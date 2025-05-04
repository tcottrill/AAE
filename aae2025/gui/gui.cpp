#include "gui.h"
#include "aae_mame_driver.h"
#include "glcode.h"
#include "gl_texturing.h"
#include "animation.h"
#include "fonts.h"
#include "menu.h"

#pragma warning( disable : 4305 4244 )

static int rottoggle = 0;
static float rotation = 0;
static int rotationcount;
static int rotation1 = 90;
static int rotation2 = -90;

static float scale = 1;
static float sscale;

static int shipimg = 0;
static int s_animate = 0;

static int keyin = 1;
static int new_gamenum;

void get_input()
{
	if (get_menu_status() == 0) {
		if ((readinputportbytag("IN0") & 0x01) || mouseb[1])
		{
			new_gamenum = gamelist[keyin].gamenum;
			gui_animate(1);
			s_animate = 1;
		}

		if (readinputportbytag("IN1") & 0x01) { keyin--; sample_start(3, 2, 0); }   //Up
		if (readinputportbytag("IN1") & 0x02) { keyin++; sample_start(3, 2, 0); }   //Down
		if (readinputportbytag("IN1") & 0x04) { keyin--;   sample_start(3, 2, 0); } //Left
		if (readinputportbytag("IN1") & 0x08) { keyin++;   sample_start(3, 2, 0); } //Right
	}
}

void run_gui()
{
	//Stars
	movestars(stars);
	placestars(stars);
	//Input
	get_input();
	gui_input(1, num_games - 2);
	//Graphics
	gui_animate(0);
	gui_loop();
	fadeit();
}

void end_gui()
{
	wrlog("EXITING  GUI");
	sample_stop(1);

	wrlog("Deleting GUI Textures.");
	glDeleteTextures(1, &fun_tex[0]);
	glDeleteTextures(1, &fun_tex[1]);
	glDeleteTextures(1, &fun_tex[2]);
	glDeleteTextures(1, &fun_tex[3]);
}

void gui_loop()
{
	int i = 1;
	int pi;
	int b = 1;
	int g = 1;
	int r = 35;
	int center = 375;
	int x;

	glLoadIdentity();

	glPrint_centered(700, "ANOTHER ARCADE EMULATOR (AAE)", 20, 20, 255, 255, 1.3, 0, 0);
	glPrint_centered(702, "ANOTHER ARCADE EMULATOR (AAE)", 255, 255, 255, 255, 1.3, 0, 0);

	i = keyin;
	b = keyin;

	//	wrlog("KEYin here is %d ", i);

	if (s_animate == 0) { glPrint_centered(center, gamelist[i].glname, 255, 255, 0, 255, 1.2, 0, 0); }
	pi = i;

	for (x = 1; x < 9; x++)//9
	{
		if (num_games > (g + 1))
		{
			i = gamelist[i].next;
			glPrint_centered((center - r), gamelist[i].glname, 255, 255, 255, (270 - x * 15), 1, 0, 0); g++;
		}
		if (num_games > g)
		{
			b = gamelist[b].prev;
			glPrint_centered((center + r), gamelist[b].glname, 255, 255, 255, (270 - x * 15), 1, 0, 0); g++;
		}

		r += 35;
	}
	glPrint_centered(30, "Press Start 1 to Select Game", 20, 20, 255, 255, 1.3, 0, 0);
	glPrint_centered(33, "Press Start 1 to Select Game", 255, 255, 255, 255, 1.3, 0, 0);
	glPrint_centered(7, "2024 Rebuild C Version - Press <TAB> for Menu", 168, 40, 40, 255, .6, 0, 0);
	shot_animate(pi);
}

void gui_input(int from, int to)
{
	if (keyin > to) { keyin = from; }
	if (keyin < from) { keyin = to; }
}

void gui_animate(int reset)
{
	int texnum = 0;

	rotationcount++;
	if (rotationcount == 400) { rotation1 = 90; rotation2 = -90; rotationcount = 0; }
	if (rotationcount > 120)
	{
		if (rotation1 > 90)
		{
			//start rotating back down
			rotation1 -= 2;
			if (rotation1 < 60) rotation1 = 60;
			rotation2 += 2;
			if (rotation2 > -60) rotation2 = -60;
			//texnum = 1;
		}
		if (rotation1 < 90)
		{
			rotation1 += 2;//start rotating back up
			if (rotation1 > 120) rotation1 = 120;
			rotation2 -= 2;
			if (rotation2 < -120) rotation2 = -120;
			//texnum = 1;
		}
	}
	if (readinputportbytag("IN1") & 0x01)
	{
		rotation1 -= 2;
		if (rotation1 < 60) rotation1 = 60;
		rotation2 += 2;
		if (rotation2 > -60) rotation2 = -60;
		rotationcount = 0; //reset
	}
	if (readinputportbytag("IN1") & 0x02)
	{
		rotation1 += 2;
		if (rotation1 > 120) rotation1 = 120;
		rotation2 -= 2;
		if (rotation2 < -120) rotation2 = -120;
		rotationcount = 0; //reset
	}

	if (rotation1 != 90) { texnum = 1; }

	glColor4ub(255, 255, 255, 255);
	draw_center_tex(&fun_tex[texnum], 32, 30, 392, rotation2, NORMAL, 255, 255, 255, 255, 2);
	draw_center_tex(&fun_tex[texnum], 32, 992, 392, rotation1, NORMAL, 255, 255, 255, 255, 2);
}

//All the code below is dependent on the monitor framerate being 60fps TODO:, add real timing
void shot_animate(int i)
{
	static int w = 255;
	static int fcount = 0;
	static int xframe = 55;
	int x = 8;

	if (s_animate == 0) { return; }
	glPrint_centered((375 - (rotation * 3)), gamelist[i].glname, 255 - (rotation * 3), 255 - (rotation * 3), 0, 255, scale, rotation, 0);
	//START THE PROCESS

	rotationcount = 400;
	//DELAY IF SHIPS NOT AT CENTER
	if (rotation1 != 90 && rottoggle == 0) { return; }
	rottoggle = 1;

	if (xframe < 512) {
		draw_center_tex(&fun_tex[3], 16, xframe, 392, 0, NORMAL, 255, 255, 255, 255, 2);
		draw_center_tex(&fun_tex[3], 16, 1024 - xframe, 392, 0, FLIP, 255, 255, 255, 255, 2);
		xframe += 8;
	}

	if (fcount > 55) { //60
		sscale += .5;//.3
		x += sscale * 10;

		if (fcount == 56)
		{
			//voice_set_pan(4, 0);
			sample_start(4, 4, 0);
		}
		if (fcount == 60)
		{
			//voice_set_pan(5, 255);
			sample_start(5, 4, 0);
		}

		draw_center_tex(&fun_tex[2], x, 470, 380, 0, NORMAL, w, 0, 0, 255, 2);
		draw_center_tex(&fun_tex[2], x, 520, 390, 0, NORMAL, 0, w, w, 255, 2);

		if (fcount > 65)
		{
			rotation += .4;
			scale += .5;//.3
			w -= 4; if (w < 0) { w = 0; }
		}
	}                            //*17

	if (scale > 40)
		//END GUI PROCESSING and do the selected game processing when the animation gets to a certain point off screen.
	{
		sample_stop(1);
		s_animate = 0;
		rotation = 0;
		scale = 1;
		sscale = 1;
		w = 255;
		fcount = 0;
		xframe = 55;
		rottoggle = 0;
		//gamenum = new_gamenum;
		done = 2;
		//reset_for_new_game(new_gamenum, 1);
		glClear(GL_COLOR_BUFFER_BIT);
		//allegro_gl_flip();
	}
	//START GAME GOES HERE
	fcount++;

	if (fcount == 2)
	{
		//voice_set_pan(6, 0);
		sample_start(6, 5, 0);
	}
	if (fcount == 4)
	{
		//voice_set_pan(7, 255);
		sample_start(7, 5, 0);
	}
}

int init_gui()
{
	static int mtime = 0;
	rotationcount = 0;

	wrlog("STARTING GUI");

	//Is this the first time through?? Then init the GUI textures
	if (!mtime) { sample_start(2, 1, 0); mtime++; }

	//set_window_title("AAE GUI (Build xxxxxxxxx)");

	make_single_bitmap(&fun_tex[0], "ship.png", "aae.zip", 0);
	make_single_bitmap(&fun_tex[1], "shipeng.png", "aae.zip", 0);
	make_single_bitmap(&fun_tex[2], "explosion.png", "aae.zip", 0);
	make_single_bitmap(&fun_tex[3], "star.png", "aae.zip", 0);

	sample_set_volume(1, config.mainvol);
	sample_start(1, 3, 1);
	return 0;
}