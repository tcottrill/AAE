//Taken from and thanks to the Allegro Project  http://liballeg.org/ for this code

#include "windows.h"
#include <mmsystem.h>
#include "joystick.h"

#pragma comment (lib, "winmm.lib")  

static int joystick_win32_init();
static void joystick_win32_exit();
static int joystick_win32_poll();


static char name_x[] = "X";
static char name_y[] = "Y";
static char name_stick[] = "stick";
static char name_throttle[] = "throttle";
static char name_rudder[] = "rudder";
static char name_slider[] = "slider";
static char name_hat[] = "hat";
static const char *name_b[MAX_JOYSTICK_BUTTONS] = {
   "B1", "B2", "B3", "B4", "B5", "B6", "B7", "B8",
   "B9", "B10", "B11", "B12", "B13", "B14", "B15", "B16",
   "B17", "B18", "B19", "B20", "B21", "B22", "B23", "B24",
   "B25", "B26", "B27", "B28", "B29", "B30", "B31", "B32"
};

#define JOY_POVFORWARD_WRAP  36000

/* joystick routines */
#define WINDOWS_MAX_AXES   6

#define WINDOWS_JOYSTICK_INFO_MEMBERS        \
   int caps;                                 \
   int num_axes;                             \
   int axis[WINDOWS_MAX_AXES];               \
   char *axis_name[WINDOWS_MAX_AXES];        \
   int hat;                                  \
   char *hat_name;                           \
   int num_buttons;                          \
   int button[MAX_JOYSTICK_BUTTONS];         \
   char *button_name[MAX_JOYSTICK_BUTTONS];

typedef struct WINDOWS_JOYSTICK_INFO {
	WINDOWS_JOYSTICK_INFO_MEMBERS
} WINDOWS_JOYSTICK_INFO;


struct WIN32_JOYSTICK_INFO {
	WINDOWS_JOYSTICK_INFO_MEMBERS
		int device;
	int axis_min[WINDOWS_MAX_AXES];
	int axis_max[WINDOWS_MAX_AXES];
};

struct WIN32_JOYSTICK_INFO win32_joystick[MAX_JOYSTICKS];
static int win32_joy_num = 0;

/* global joystick information */
int num_joysticks;
int _joystick_installed;
JOYSTICK_INFO joy[MAX_JOYSTICKS];


static void clear_joystick_vars(void)
{
	const char *unused = "unused";
	int i, j, k;

#define ARRAY_SIZE(a)   ((int)sizeof((a)) / (int)sizeof((a)[0]))

	for (i = 0; i < ARRAY_SIZE(joy); i++) {
		joy[i].flags = 0;
		joy[i].num_sticks = 0;
		joy[i].num_buttons = 0;

		for (j = 0; j < ARRAY_SIZE(joy[i].stick); j++) {
			joy[i].stick[j].flags = 0;
			joy[i].stick[j].num_axis = 0;
			joy[i].stick[j].name = unused;

			for (k = 0; k < ARRAY_SIZE(joy[i].stick[j].axis); k++) {
				joy[i].stick[j].axis[k].pos = 0;
				joy[i].stick[j].axis[k].d1 = FALSE;
				joy[i].stick[j].axis[k].d2 = FALSE;
				joy[i].stick[j].axis[k].name = unused;
			}
		}

		for (j = 0; j < ARRAY_SIZE(joy[i].button); j++) {
			joy[i].button[j].b = FALSE;
			joy[i].button[j].name = unused;
		}
	}

	num_joysticks = 0;
}



/* win_update_joystick_status:
 *  Updates the specified joystick info structure by translating
 *  Windows joystick status into Allegro joystick status.
 */
int win_update_joystick_status(int n, WINDOWS_JOYSTICK_INFO *win_joy)
{
	int n_stick, n_axis, n_but, max_stick, win_axis, p;

	if (n >= num_joysticks)
		return -1;

	/* sticks */
	n_stick = 0;
	win_axis = 0;

	/* skip hat at this point, it will be handled later */
	if (win_joy->caps & JOYCAPS_HASPOV)
		max_stick = joy[n].num_sticks - 1;
	else
		max_stick = joy[n].num_sticks;

	for (n_stick = 0; n_stick < max_stick; n_stick++) {
		for (n_axis = 0; n_axis < joy[n].stick[n_stick].num_axis; n_axis++) {
			p = win_joy->axis[win_axis];

			/* set pos of analog stick */
			if (joy[n].stick[n_stick].flags & JOYFLAG_ANALOGUE) {
				if (joy[n].stick[n_stick].flags & JOYFLAG_SIGNED)
					joy[n].stick[n_stick].axis[n_axis].pos = p - 128;
				else
					joy[n].stick[n_stick].axis[n_axis].pos = p;
			}

			/* set pos of digital stick */
			if (joy[n].stick[n_stick].flags & JOYFLAG_DIGITAL) {
				if (p < 64)
					joy[n].stick[n_stick].axis[n_axis].d1 = TRUE;
				else
					joy[n].stick[n_stick].axis[n_axis].d1 = FALSE;

				if (p > 192)
					joy[n].stick[n_stick].axis[n_axis].d2 = TRUE;
				else
					joy[n].stick[n_stick].axis[n_axis].d2 = FALSE;
			}

			win_axis++;
		}
	}

	/* hat */
	if (win_joy->caps & JOYCAPS_HASPOV) {
		/* emulate analog joystick */
		joy[n].stick[n_stick].axis[0].pos = 0;
		joy[n].stick[n_stick].axis[1].pos = 0;

		/* left */
		if ((win_joy->hat > JOY_POVBACKWARD) && (win_joy->hat < JOY_POVFORWARD_WRAP)) {
			joy[n].stick[n_stick].axis[0].d1 = TRUE;
			joy[n].stick[n_stick].axis[0].pos = -128;
		}
		else {
			joy[n].stick[n_stick].axis[0].d1 = FALSE;
		}

		/* right */
		if ((win_joy->hat > JOY_POVFORWARD) && (win_joy->hat < JOY_POVBACKWARD)) {
			joy[n].stick[n_stick].axis[0].d2 = TRUE;
			joy[n].stick[n_stick].axis[0].pos = +128;
		}
		else {
			joy[n].stick[n_stick].axis[0].d2 = FALSE;
		}

		/* forward */
		if (((win_joy->hat > JOY_POVLEFT) && (win_joy->hat <= JOY_POVFORWARD_WRAP)) ||
			((win_joy->hat >= JOY_POVFORWARD) && (win_joy->hat < JOY_POVRIGHT))) {
			joy[n].stick[n_stick].axis[1].d1 = TRUE;
			joy[n].stick[n_stick].axis[1].pos = -128;
		}
		else {
			joy[n].stick[n_stick].axis[1].d1 = FALSE;
		}

		/* backward */
		if ((win_joy->hat > JOY_POVRIGHT) && (win_joy->hat < JOY_POVLEFT)) {
			joy[n].stick[n_stick].axis[1].d2 = TRUE;
			joy[n].stick[n_stick].axis[1].pos = +128;
		}
		else {
			joy[n].stick[n_stick].axis[1].d2 = FALSE;
		}
	}

	/* buttons */
	for (n_but = 0; n_but < win_joy->num_buttons; n_but++)
		joy[n].button[n_but].b = win_joy->button[n_but];

	return 0;
}



/* win_add_joystick:
 *  Adds a new joystick (fills in a new joystick info structure).
 */
int win_add_joystick(WINDOWS_JOYSTICK_INFO *win_joy)
{
	int n_stick, n_but, max_stick, win_axis;

	if (num_joysticks == MAX_JOYSTICKS - 1)
		return -1;

	joy[num_joysticks].flags = JOYFLAG_ANALOGUE | JOYFLAG_DIGITAL;

	/* how many sticks ? */
	n_stick = 0;

	if (win_joy->num_axes > 0) {
		win_axis = 0;

		/* main analogue stick */
		if (win_joy->num_axes > 1) {
			joy[num_joysticks].stick[n_stick].flags = JOYFLAG_DIGITAL | JOYFLAG_ANALOGUE | JOYFLAG_SIGNED;
			joy[num_joysticks].stick[n_stick].axis[0].name = (win_joy->axis_name[0] ? win_joy->axis_name[0] : name_x);
			joy[num_joysticks].stick[n_stick].axis[1].name = (win_joy->axis_name[1] ? win_joy->axis_name[1] : name_y);
			joy[num_joysticks].stick[n_stick].name = name_stick;

			/* Z-axis: throttle */
			if (win_joy->caps & JOYCAPS_HASZ) {
				joy[num_joysticks].stick[n_stick].num_axis = 3;
				joy[num_joysticks].stick[n_stick].axis[2].name = (win_joy->axis_name[2] ? win_joy->axis_name[2] : name_throttle);
				win_axis += 3;
			}
			else {
				joy[num_joysticks].stick[n_stick].num_axis = 2;
				win_axis += 2;
			}

			n_stick++;
		}

		/* first 1-axis stick: rudder */
		if (win_joy->caps & JOYCAPS_HASR) {
			joy[num_joysticks].stick[n_stick].flags = JOYFLAG_DIGITAL | JOYFLAG_ANALOGUE | JOYFLAG_UNSIGNED;
			joy[num_joysticks].stick[n_stick].num_axis = 1;
			joy[num_joysticks].stick[n_stick].axis[0].name = "";
			joy[num_joysticks].stick[n_stick].name = (win_joy->axis_name[win_axis] ? win_joy->axis_name[win_axis] : name_rudder);
			win_axis++;
			n_stick++;
		}

		max_stick = (win_joy->caps & JOYCAPS_HASPOV ? MAX_JOYSTICK_STICKS - 1 : MAX_JOYSTICK_STICKS);

		/* other 1-axis sticks: sliders */
		while ((win_axis < win_joy->num_axes) && (n_stick < max_stick)) {
			joy[num_joysticks].stick[n_stick].flags = JOYFLAG_DIGITAL | JOYFLAG_ANALOGUE | JOYFLAG_UNSIGNED;
			joy[num_joysticks].stick[n_stick].num_axis = 1;
			joy[num_joysticks].stick[n_stick].axis[0].name = "";
			joy[num_joysticks].stick[n_stick].name = (win_joy->axis_name[win_axis] ? win_joy->axis_name[win_axis] : name_slider);
			win_axis++;
			n_stick++;
		}

		/* hat */
		if (win_joy->caps & JOYCAPS_HASPOV) {
			joy[num_joysticks].stick[n_stick].flags = JOYFLAG_DIGITAL | JOYFLAG_SIGNED;
			joy[num_joysticks].stick[n_stick].num_axis = 2;
			joy[num_joysticks].stick[n_stick].axis[0].name = "left/right";
			joy[num_joysticks].stick[n_stick].axis[1].name = "up/down";
			joy[num_joysticks].stick[n_stick].name = (win_joy->hat_name ? win_joy->hat_name : name_hat);
			n_stick++;
		}
	}

	joy[num_joysticks].num_sticks = n_stick;

	/* how many buttons ? */
	joy[num_joysticks].num_buttons = win_joy->num_buttons;

	/* fill in the button names */
	for (n_but = 0; n_but < joy[num_joysticks].num_buttons; n_but++)
		joy[num_joysticks].button[n_but].name = (win_joy->button_name[n_but] ? win_joy->button_name[n_but] : name_b[n_but]);

	num_joysticks++;

	return 0;
}



/* win_remove_all_joysticks:
 *  Removes all registered joysticks.
 */
void win_remove_all_joysticks()
{
	num_joysticks = 0;
}




/* joystick_win32_poll:
 *  Polls the Win32 joystick devices.
 */
static int joystick_win32_poll()
{
	int n_joy, n_axis, n_but, p, range;
	JOYINFOEX js;

	for (n_joy = 0; n_joy < win32_joy_num; n_joy++) {
		js.dwSize = sizeof(js);
		js.dwFlags = JOY_RETURNALL;

		if (joyGetPosEx(win32_joystick[n_joy].device, &js) == JOYERR_NOERROR) {

			/* axes */
			win32_joystick[n_joy].axis[0] = js.dwXpos;
			win32_joystick[n_joy].axis[1] = js.dwYpos;
			n_axis = 2;

			if (win32_joystick[n_joy].caps & JOYCAPS_HASZ) {
				win32_joystick[n_joy].axis[n_axis] = js.dwZpos;
				n_axis++;
			}

			if (win32_joystick[n_joy].caps & JOYCAPS_HASR) {
				win32_joystick[n_joy].axis[n_axis] = js.dwRpos;
				n_axis++;
			}

			if (win32_joystick[n_joy].caps & JOYCAPS_HASU) {
				win32_joystick[n_joy].axis[n_axis] = js.dwUpos;
				n_axis++;
			}

			if (win32_joystick[n_joy].caps & JOYCAPS_HASV) {
				win32_joystick[n_joy].axis[n_axis] = js.dwVpos;
				n_axis++;
			}

			/* map Windows axis range to 0-256 Allegro range */
			for (n_axis = 0; n_axis < win32_joystick[n_joy].num_axes; n_axis++) {
				p = win32_joystick[n_joy].axis[n_axis] - win32_joystick[n_joy].axis_min[n_axis];
				range = win32_joystick[n_joy].axis_max[n_axis] - win32_joystick[n_joy].axis_min[n_axis];

				if (range > 0)
					win32_joystick[n_joy].axis[n_axis] = p * 256 / range;
				else
					win32_joystick[n_joy].axis[n_axis] = 0;
			}

			/* hat */
			if (win32_joystick[n_joy].caps & JOYCAPS_HASPOV)
				win32_joystick[n_joy].hat = js.dwPOV;

			/* buttons */
			for (n_but = 0; n_but < win32_joystick[n_joy].num_buttons; n_but++)
				win32_joystick[n_joy].button[n_but] = ((js.dwButtons & (1 << n_but)) != 0);
		}
		else {
			for (n_axis = 0; n_axis < win32_joystick[n_joy].num_axes; n_axis++)
				win32_joystick[n_joy].axis[n_axis] = 0;

			if (win32_joystick[n_joy].caps & JOYCAPS_HASPOV)
				win32_joystick[n_joy].hat = 0;

			for (n_but = 0; n_but < win32_joystick[n_joy].num_buttons; n_but++)
				win32_joystick[n_joy].button[n_but] = FALSE;
		}

		win_update_joystick_status(n_joy, (WINDOWS_JOYSTICK_INFO *)&win32_joystick[n_joy]);
	}

	return 0;
}



/* joystick_win32_init:
 *  Initialises the Win32 joystick driver.
 */
static int joystick_win32_init()
{
	JOYCAPS caps;
	JOYINFOEX js;
	int n_joyat, n_joy, n_axis;

	win32_joy_num = joyGetNumDevs();

	// if (win32_joy_num > MAX_JOYSTICKS)
	   // _TRACE(PREFIX_W "The system supports more than %d joysticks\n", MAX_JOYSTICKS);

	 /* retrieve joystick infos */
	n_joy = 0;
	for (n_joyat = 0; n_joyat < win32_joy_num; n_joyat++) {
		if (n_joy == MAX_JOYSTICKS)
			break;

		if (joyGetDevCaps(n_joyat, &caps, sizeof(caps)) == JOYERR_NOERROR) {
			/* is the joystick physically attached? */
			js.dwSize = sizeof(js);
			js.dwFlags = JOY_RETURNALL;
			if (joyGetPosEx(n_joyat, &js) == JOYERR_UNPLUGGED)
				continue;

			memset(&win32_joystick[n_joy], 0, sizeof(struct WIN32_JOYSTICK_INFO));

			/* set global properties */
			win32_joystick[n_joy].device = n_joyat;
			win32_joystick[n_joy].caps = caps.wCaps;
			win32_joystick[n_joy].num_buttons = MIN(caps.wNumButtons, MAX_JOYSTICK_BUTTONS);
			win32_joystick[n_joy].num_axes = MIN(caps.wNumAxes, WINDOWS_MAX_AXES);

			/* fill in ranges of axes */
			win32_joystick[n_joy].axis_min[0] = caps.wXmin;
			win32_joystick[n_joy].axis_max[0] = caps.wXmax;
			win32_joystick[n_joy].axis_min[1] = caps.wYmin;
			win32_joystick[n_joy].axis_max[1] = caps.wYmax;
			n_axis = 2;

			if (caps.wCaps & JOYCAPS_HASZ) {
				win32_joystick[n_joy].axis_min[2] = caps.wZmin;
				win32_joystick[n_joy].axis_max[2] = caps.wZmax;
				n_axis++;
			}

			if (caps.wCaps & JOYCAPS_HASR) {
				win32_joystick[n_joy].axis_min[n_axis] = caps.wRmin;
				win32_joystick[n_joy].axis_max[n_axis] = caps.wRmax;
				n_axis++;
			}

			if (caps.wCaps & JOYCAPS_HASU) {
				win32_joystick[n_joy].axis_min[n_axis] = caps.wUmin;
				win32_joystick[n_joy].axis_max[n_axis] = caps.wUmax;
				n_axis++;
			}

			if (caps.wCaps & JOYCAPS_HASV) {
				win32_joystick[n_joy].axis_min[n_axis] = caps.wVmin;
				win32_joystick[n_joy].axis_max[n_axis] = caps.wVmax;
				n_axis++;
			}

			/* register this joystick */
			if (win_add_joystick((WINDOWS_JOYSTICK_INFO *)&win32_joystick[n_joy]) != 0)
				break;

			n_joy++;
		}
	}

	win32_joy_num = n_joy;

	return (win32_joy_num == 0);
}



/* joystick_win32_exit:
 *  Shuts down the Win32 joystick driver.
 */
static void joystick_win32_exit(void)
{
	win_remove_all_joysticks();
	win32_joy_num = 0;
}




int install_joystick()
{

	if (_joystick_installed)
		return 0;

	clear_joystick_vars();
	joystick_win32_init();

	poll_joystick();

	_joystick_installed = TRUE;

	return 0;
}



/* remove_joystick:
 *  Shuts down the joystick module.
 */
void remove_joystick()
{
	joystick_win32_exit();
	_joystick_installed = FALSE;

}



/* poll_joystick:
 *  Reads the current input state into the joystick status variables.
 */
int poll_joystick()
{
	if (_joystick_installed) { joystick_win32_poll(); }

	return -1;
}

