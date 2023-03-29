#include "CInput.h"


CInput::CInput()
{
	m_keyboard	= NULL;
	m_mouse		= NULL;
	m_joystick	= NULL;
	mX = 0.0f;
	mY = 0.0f;
}
CInput::~CInput()
{
	this->Shutdown();
}
// shutdown input system
bool CInput::Shutdown(void)
{
	SafeRelease(lpdi);
	return true;
}


// Initiates input system
bool CInput::Init( void )
{
	if (FAILED(DirectInput8Create(GetModuleHandle(NULL), DIRECTINPUT_VERSION, IID_IDirectInput8, (void**)&lpdi, NULL)))
		return false;
	
	for (int i = 0; i < 256; i++)
		keypress_state[i] = 0;

	return true;
}


bool CInput::Init_Keyboard(HWND hWnd)
{
	if (FAILED(lpdi->CreateDevice(GUID_SysKeyboard, &m_keyboard, NULL)))
		return false;
	if (FAILED(m_keyboard->SetDataFormat(&c_dfDIKeyboard)))
		return false;
	if (FAILED(m_keyboard->SetCooperativeLevel(hWnd, DISCL_BACKGROUND |
		DISCL_NONEXCLUSIVE)))
		return false;
	if (FAILED(m_keyboard->Acquire()))
		return false;
	return true;
}

bool CInput::Acquire_Keyboard()
{
	if (FAILED(m_keyboard->Acquire()))
		return false;
	return true;
}

bool CInput::Unacquire_Keyboard()
{
	if (FAILED(m_keyboard->Unacquire()))
		return false;
	return true;
}

bool CInput::Read_Keyboard()
{
	if (FAILED(m_keyboard->GetDeviceState(sizeof(UCHAR[256]), (LPVOID)keystate)))
		return false;
	return true;
}

bool CInput::Release_Keyboard()
{
	SafeRelease(m_keyboard);
	return true;
}

bool CInput::KeyDown(DWORD key)
{
	return ((keystate[key] & 0x80) ? true : false);
}

bool CInput::KeyUp(DWORD key)
{
	return ((keystate[key] & 0x80) ? false : true);
}

bool CInput::KeyPress(DWORD key)
{
	if (KeyDown(key))
		keypress_state[key] = 1;

	if (keypress_state[key] == 1)
		if (KeyUp(key))
			keypress_state[key] = 2;

	if (keypress_state[key] == 2)
	{
		keypress_state[key] = 0;
		return true;
	}
	
	return false;
}








bool CInput::Init_Mouse(HWND hWnd)
{
	if (FAILED(lpdi->CreateDevice(GUID_SysMouse, &m_mouse, NULL)))
		return false;
	if (FAILED(m_mouse->SetCooperativeLevel(hWnd, DISCL_BACKGROUND |
		DISCL_NONEXCLUSIVE)))
		return false;
	if (FAILED(m_mouse->SetDataFormat(&c_dfDIMouse)))
		return false;
	if (FAILED(m_mouse->Acquire()))
		return false;
	return true;
}

bool CInput::Acquire_Mouse(void)
{
	if (FAILED(m_mouse->Acquire()))
		return false;
	return true;
}

bool CInput::Unacquire_Mouse(void)
{
	if (FAILED(m_mouse->Unacquire()))
		return false;
	return true;
}

bool CInput::Read_Mouse(void)
{
	if (FAILED(m_mouse->GetDeviceState(sizeof(DIMOUSESTATE), 
		(LPVOID)&mouse_state)))
		return false;
	return true;
}

void CInput::Get_Movement(float &m_x, float &m_y)
{
	m_x = (float)mouse_state.lX;
	m_y = (float)mouse_state.lY;
}

void CInput::Get_Mouse_Coords(float &m_x, float &m_y)
{
	Get_Mouse_X(m_x);
	Get_Mouse_Y(m_y);
}

void CInput::Get_Mouse_X(float &m_x)
{
	mX += mouse_state.lX;
	if (mX < 0)
		mX = 0;
	
	m_x = mX;
}

void CInput::Get_Mouse_Y(float &m_y)
{
	mY += mouse_state.lY;
	if (mY < 0)
		mY = 0;
	
	m_y = mY;
}

bool CInput::Button_Down(int mouse_button)
{
	return ((mouse_state.rgbButtons[mouse_button] & 0x80) ? true : false);
}

bool CInput::Button_Up(int mouse_button)
{
	return ((mouse_state.rgbButtons[mouse_button] & 0x80) ? false : true);
}

bool CInput::Release_Mouse()
{
	if (Unacquire_Mouse())
		return false;
	SafeRelease(m_mouse);
	return true;
}














//-----------------------------------------------------------------------------
// Name: EnumJoysticksCallback()
// Desc: Called once for each enumerated joystick. If we find one, create a
//       device interface on it so we can play with it.
//-----------------------------------------------------------------------------
BOOL CALLBACK CInput::EnumJoysticksCallback( const DIDEVICEINSTANCE* pdidInstance,
                                     VOID* pContext )
{
    HRESULT hr;
	pContext;

    // Obtain an interface to the enumerated joystick.
    hr = lpdi->CreateDevice( pdidInstance->guidInstance, &this->m_joystick, NULL );

    // If it failed, then we can't use this joystick. (Maybe the user unplugged
    // it while we were in the middle of enumerating it.)
    if( FAILED(hr) ) 
        return DIENUM_CONTINUE;

    // Stop enumeration. Note: we're just taking the first joystick we get. You
    // could store all the enumerated joysticks and let the user pick.
    return DIENUM_STOP;
}



//-----------------------------------------------------------------------------
// Name: EnumObjectsCallback()
// Desc: Callback function for enumerating objects (axes, buttons, POVs) on a 
//       joystick. This function enables user interface elements for objects
//       that are found to exist, and scales axes min/max values.
//-----------------------------------------------------------------------------
BOOL CALLBACK CInput::EnumObjectsCallback( const DIDEVICEOBJECTINSTANCE* pdidoi,
                                   VOID* pContext )
{
	pContext; // no ref 

    // For axes that are returned, set the DIPROP_RANGE property for the
    // enumerated axis in order to scale min/max values.
    if( pdidoi->dwType & DIDFT_AXIS )
    {
        DIPROPRANGE diprg; 
        diprg.diph.dwSize       = sizeof(DIPROPRANGE); 
        diprg.diph.dwHeaderSize = sizeof(DIPROPHEADER); 
        diprg.diph.dwHow        = DIPH_BYID; 
        diprg.diph.dwObj        = pdidoi->dwType; // Specify the enumerated axis
        diprg.lMin              = -1000; 
        diprg.lMax              = +1000; 
    
        // Set the range for the axis
        if( FAILED( this->m_joystick->SetProperty( DIPROP_RANGE, &diprg.diph ) ) ) 
            return DIENUM_STOP;
         
    }


    // Set the UI to reflect what objects the joystick supports
    if (pdidoi->guidType == GUID_XAxis)
    {
		// This joystick has a GUID_XAxis
        // EnableWindow( GetDlgItem( hDlg, IDC_X_AXIS ), TRUE );
        // EnableWindow( GetDlgItem( hDlg, IDC_X_AXIS_TEXT ), TRUE );
    }
    if (pdidoi->guidType == GUID_YAxis)
    {
		// This joystick has a GUID_XAxis
        // EnableWindow( GetDlgItem( hDlg, IDC_Y_AXIS ), TRUE );
        // EnableWindow( GetDlgItem( hDlg, IDC_Y_AXIS_TEXT ), TRUE );
    }
    if (pdidoi->guidType == GUID_ZAxis)
    {
		// This joystick has a GUID_XAxis
        // EnableWindow( GetDlgItem( hDlg, IDC_Z_AXIS ), TRUE );
        // EnableWindow( GetDlgItem( hDlg, IDC_Z_AXIS_TEXT ), TRUE );
    }
    return DIENUM_CONTINUE;
}



bool CInput::Init_Joystick(HWND hWnd)
{
	// SWS Joystick
	HRESULT hr;
	// Register with the DirectInput subsystem and get a pointer
	// to a IDirectInput interface we can use.
	// Create a DInput object
	if( FAILED( DirectInput8Create( GetModuleHandle(NULL), DIRECTINPUT_VERSION, IID_IDirectInput8, (VOID**)&lpdi, NULL ) ) )
		return false;

	// Look for a simple joystick we can use for this sample program.
	if( FAILED( this->lpdi->EnumDevices( DI8DEVCLASS_GAMECTRL, this->EnumJoysticksCallback, NULL, DIEDFL_ATTACHEDONLY ) ) )
		return false;

	// Make sure we got a joystick
	if( NULL == this->m_joystick )
		{
		//	MessageBox( NULL, TEXT("Joystick not found. The sample will now exit."),TEXT("DirectInput Sample"), MB_ICONERROR | MB_OK );
			return false;
		}

	// Set the data format to "simple joystick" - a predefined data format 
	//
	// A data format specifies which controls on a device we are interested in,
	// and how they should be reported. This tells DInput that we will be
	// passing a DIJOYSTATE2 structure to IDirectInputDevice::GetDeviceState().
	if( FAILED( this->m_joystick->SetDataFormat( &c_dfDIJoystick2 ) ) )
		return false;

	// Set the cooperative level to let DInput know how this device should
	// interact with the system and with other DInput applications.
	if( FAILED( this->m_joystick->SetCooperativeLevel( hWnd, DISCL_EXCLUSIVE | DISCL_FOREGROUND ) ) )
		return false;

	// Enumerate the joystick objects. The callback function enabled user
	// interface elements for objects that are found, and sets the min/max
	// values property for discovered axes.
	if( FAILED( this->m_joystick->EnumObjects( this->EnumObjectsCallback,(VOID*)hWnd, DIDFT_ALL ) ) )
		return false;

	// Everything woked.. 
	return true;
}
bool CInput::Release_Joystick(void)
{
	// Unacquire the device one last time just in case 
    // the app tried to exit while the device is still acquired.
	if( this->m_joystick ) 
        this->m_joystick->Unacquire();
    
    // Release any DirectInput objects.
    SafeRelease( this->m_joystick );
	return true;
}
bool CInput::Read_Joystick(void)
{
	return false;
}

