pragma once

///
// http://www.gamedev.net/reference/articles/article1607.asp
///

#define WIN32_LEAN_AND_MEAN			// trim the excess fat from Windows
#include <windows.h>				// standard Windows app include
#include <dinput.h>

typedef unsigned char UCHAR;
#define SafeRelease(x)	if (x) {x->Release(); x=NULL;}

/*
Turn-Based  
http://msdn.microsoft.com/archive/default.asp?url=/archive/en-us/directx9_c/directx/input/ref/actionmapconsts/strategy/turn_based.asp

enum Actions {
    null = 1,
    INPUT_UP,
    INPUT_DOWN,
    INPUT_LEFT,
    INPUT_RIGHT,
    INPUT_YES,
    INPUT_NO,
    INPUT_MENU
};

	DIACTION g_rgGameAction[] = {
    // Device input pre-defined by DirectInput, according to genre
	
	{INPUT_DOWN,			DIBUTTON_STRATEGYT_BACK_LINK,		0, TEXT("Down") ,},
	{INPUT_LEFT,			DIBUTTON_STRATEGYT_LEFT_LINK,		0, TEXT("Left") ,},
	{INPUT_RIGHT,	    	DIBUTTON_STRATEGYT_RIGHT_LINK,		0, TEXT("Right"),},
	{INPUT_UP  ,			DIBUTTON_STRATEGYT_FORWARD_LINK,	0, TEXT("Up")   ,},
	{INPUT_YES,  			DIBUTTON_STRATEGYT_APPLY,			0, TEXT("Yes") ,},
	{INPUT_MENU,  			DIBUTTON_STRATEGYT_MENU,			0, TEXT("Menu") ,},
		

	// Keyboard input mappings
	{INPUT_DOWN,			DIKEYBOARD_DOWN,	0, TEXT("Down") ,},
	{INPUT_LEFT,			DIKEYBOARD_LEFT,	0, TEXT("Left") ,},
	{INPUT_RIGHT,	    	DIKEYBOARD_RIGHT,	0, TEXT("Right"),},
	{INPUT_UP  ,			DIKEYBOARD_UP,		0, TEXT("Up")   ,},
	{INPUT_MENU,			DIKEYBOARD_ESCAPE,	0, TEXT("Menu") ,},
	{INPUT_NO,		    	DIKEYBOARD_N,		0, TEXT("No")   ,},
	{INPUT_YES,	    		DIKEYBOARD_Y,		0, TEXT("Yes")  ,},
	};

*/
class CInput 
{
	public:
		CInput();
		~CInput();

	bool Init( void );
	bool Shutdown(void);


	// KeyBoard
	// =============================
	bool Init_Keyboard(HWND hWnd);
	bool Acquire_Keyboard();
	bool Unacquire_Keyboard();
	bool Release_Keyboard();
	bool Read_Keyboard();
	bool KeyDown(DWORD key);
	bool KeyUp(DWORD key);
	bool KeyPress(DWORD key);

	// Mouse
	// =============================
	bool Init_Mouse(HWND hWnd);
	bool Acquire_Mouse(void);
	bool Unacquire_Mouse(void);
	bool Release_Mouse(void);
	bool Read_Mouse(void);
	void Get_Mouse_X(float &m_x);
	void Get_Mouse_Y(float &m_y);
	void Get_Mouse_Coords(float &m_x, float &m_y);
	void Get_Movement(float &m_x, float &m_y);
	bool Button_Down(int mouse_button);
	bool Button_Up(int mouse_button);

	// Joystick
	// ==============================
	bool Init_Joystick(HWND hWnd);
	bool Release_Joystick(void);
	bool Read_Joystick(void);
	BOOL CALLBACK EnumJoysticksCallback( const DIDEVICEINSTANCE* pdidInstance,VOID* pContext );
	BOOL CALLBACK EnumObjectsCallback( const DIDEVICEOBJECTINSTANCE* pdidoi, VOID* pContext );

	private:

	LPDIRECTINPUT		lpdi;
	LPDIRECTINPUTDEVICE m_keyboard;
	LPDIRECTINPUTDEVICE m_mouse ;
	LPDIRECTINPUTDEVICE m_joystick ; //g_pJoystick
	

	
	UCHAR keystate[256];
	UCHAR keypress_state[256];

	float				mX , mY ;
	DIMOUSESTATE		mouse_state;


};
