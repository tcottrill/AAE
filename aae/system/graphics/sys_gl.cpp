// -----------------------------------------------------------------------------
// Description
// OpenGL Context Creation and Feature Reporting using GLEW/WGL on Windows
// Default build requests OpenGL 4.2; WIN7BUILD requests OpenGL 3.3.
// Falls back to 3.3 then 2.1 if the primary request fails.
// Logs capabilities, enables vsync control.
// -----------------------------------------------------------------------------

#ifdef _WIN32
#include <windows.h>
#endif
#include <cstdio>
#include <cstring>   // strstr, used by the GLX capability probe
#include <string>
#include <sstream>

#ifdef _WIN32
#include "glew.h"
#include "wglew.h"
#include "win32/win32_private.h"
#else
// Linux: GLEW and GLX. glxew.h pulls <GL/glx.h> itself and must come after
// glew.h. Xlib is reached only through GLX here - this file never touches a
// window directly; LinuxWindow hands it the display/window via
// sys_gl_set_x11_target() below.
#include <GL/glew.h>
#include <GL/glx.h>
#endif
#include "sys_log.h"
#include "sys_gl.h"

// -----------------------------------------------------------------------------
// Static Globals
// -----------------------------------------------------------------------------
#ifdef _WIN32
static HDC   hDC = nullptr;
static HGLRC hRC = nullptr;
#else
// The X11 display/window LinuxWindow hands us via sys_gl_set_x11_target().
// This file never opens a display of its own - the dependency runs one way,
// window -> GL, exactly as it does on Windows where winmain supplies the HWND.
static Display*    gDpy = nullptr;
static GLXDrawable gWin = 0;
static GLXContext  gCtx = nullptr;
static GLXFBConfig gFbc = nullptr;   // chosen by sys_gl_choose_x11_visual()

// -----------------------------------------------------------------------------
// sys_gl_choose_x11_visual
//
// MUST be called BEFORE the X window is created, and the returned visual MUST
// be the one XCreateWindow uses (with a colormap made from it).
//
// GLX requires the drawable's visual to be compatible with the context's
// framebuffer config. Creating the window with CopyFromParent and *then*
// picking an FBConfig independently is the classic way to get a window that
// makes a context, reports no error, swaps happily - and displays nothing.
//
// Returns an XVisualInfo* the caller must XFree(), or nullptr on failure.
// -----------------------------------------------------------------------------
void* sys_gl_choose_x11_visual(void* display, int screen, int enableMultisample)
{
	Display* dpy = static_cast<Display*>(display);
	if (!dpy) return nullptr;

	int fbAttribs[] = {
		GLX_X_RENDERABLE,  True,
		GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT,
		GLX_RENDER_TYPE,   GLX_RGBA_BIT,
		GLX_X_VISUAL_TYPE, GLX_TRUE_COLOR,
		GLX_RED_SIZE,      8,
		GLX_GREEN_SIZE,    8,
		GLX_BLUE_SIZE,     8,
		GLX_ALPHA_SIZE,    8,
		GLX_DEPTH_SIZE,    24,
		GLX_STENCIL_SIZE,  8,
		GLX_DOUBLEBUFFER,  True,
		GLX_SAMPLE_BUFFERS, enableMultisample ? 1 : 0,
		GLX_SAMPLES,        enableMultisample ? 4 : 0,
		None
	};

	int fbCount = 0;
	GLXFBConfig* fbc = glXChooseFBConfig(dpy, screen, fbAttribs, &fbCount);
	if ((!fbc || fbCount == 0) && enableMultisample) {
		// MSAA is optional - retry without rather than failing outright.
		LOG_INFO("GLX: no multisample framebuffer config, retrying without MSAA");
		fbAttribs[24] = 0;   // GLX_SAMPLE_BUFFERS value
		fbAttribs[26] = 0;   // GLX_SAMPLES value
		fbc = glXChooseFBConfig(dpy, screen, fbAttribs, &fbCount);
	}
	if (!fbc || fbCount == 0) {
		LOG_ERROR("GLX: no suitable framebuffer config found");
		return nullptr;
	}

	gFbc = fbc[0];
	XVisualInfo* vi = glXGetVisualFromFBConfig(dpy, gFbc);
	XFree(fbc);

	if (!vi) {
		LOG_ERROR("GLX: glXGetVisualFromFBConfig returned no visual");
		return nullptr;
	}

	LOG_INFO("GLX: chose visual 0x%lx depth %d for the window",
	         (unsigned long)vi->visualid, vi->depth);
	return vi;
}

void sys_gl_set_x11_target(void* display, unsigned long window)
{
	gDpy = static_cast<Display*>(display);
	gWin = static_cast<GLXDrawable>(window);
}
#endif
static bool gOpenGLInitialized = false;

void CheckGLErrorEx(const char* label, const char* file, int line) {
	GLenum err;
	while ((err = glGetError()) != GL_NO_ERROR) {
		const char* errStr = "UNKNOWN_ERROR";
		switch (err) {
		case GL_INVALID_ENUM:                  errStr = "GL_INVALID_ENUM"; break;
		case GL_INVALID_VALUE:                 errStr = "GL_INVALID_VALUE"; break;
		case GL_INVALID_OPERATION:             errStr = "GL_INVALID_OPERATION"; break;
		case GL_STACK_OVERFLOW:                errStr = "GL_STACK_OVERFLOW"; break;
		case GL_STACK_UNDERFLOW:               errStr = "GL_STACK_UNDERFLOW"; break;
		case GL_OUT_OF_MEMORY:                 errStr = "GL_OUT_OF_MEMORY"; break;
		case GL_INVALID_FRAMEBUFFER_OPERATION: errStr = "GL_INVALID_FRAMEBUFFER_OPERATION"; break;
		}

		if (file && label) {
			LOG_ERROR("OpenGL Error [%s] (%#x) in '%s' at %s:%d", errStr, err, label, file, line);
		}
		else if (file) {
			LOG_ERROR("OpenGL Error [%s] (%#x) at %s:%d", errStr, err, file, line);
		}
		else if (label) {
			LOG_ERROR("OpenGL Error [%s] (%#x) in '%s'", errStr, err, label);
		}
		else {
			LOG_ERROR("OpenGL Error [%s] (%#x)", errStr, err);
		}
	}
}

// -----------------------------------------------------------------------------
// ReportOpenGLCapabilities
// Logs hardware limits, extensions, and feature support
// -----------------------------------------------------------------------------
static void ReportOpenGLCapabilities()
{
	LOG_INFO("GL_VENDOR: %s", glGetString(GL_VENDOR));
	LOG_INFO("GL_RENDERER: %s", glGetString(GL_RENDERER));
	LOG_INFO("GL_VERSION: %s", glGetString(GL_VERSION));

	const char* glslVersionStr = reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION));
	if (glslVersionStr)
	{
		LOG_INFO("GL_SHADING_LANGUAGE_VERSION: %s", glslVersionStr);
		int major = 0, minor = 0;
#ifdef _WIN32
		if (sscanf_s(glslVersionStr, "%d.%d", &major, &minor) == 2)
#else
		if (sscanf(glslVersionStr, "%d.%d", &major, &minor) == 2)
#endif
		{
			if (major >= 4)
				LOG_INFO("GLSL 4.x or higher is supported");
			else
				LOG_ERROR("GLSL version is below 4.x - some shaders may be incompatible");
		}
		else {
			LOG_ERROR("Unable to parse GLSL version string");
		}
	}
	else {
		LOG_ERROR("glGetString(GL_SHADING_LANGUAGE_VERSION) returned null");
	}

	GLint maxTexSize = 0;
	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTexSize);
	LOG_INFO("GL_MAX_TEXTURE_SIZE = %d", maxTexSize);

	GLint maxTexUnits = 0;
	glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &maxTexUnits);
	LOG_INFO("GL_MAX_TEXTURE_IMAGE_UNITS = %d", maxTexUnits);

	GLint maxDrawBuffers = 0;
	glGetIntegerv(GL_MAX_DRAW_BUFFERS, &maxDrawBuffers);
	LOG_INFO("GL_MAX_DRAW_BUFFERS = %d", maxDrawBuffers);

	if (GLEW_EXT_framebuffer_multisample) {
		GLint maxSamples = 0;
		glGetIntegerv(GL_MAX_SAMPLES_EXT, &maxSamples);
		LOG_INFO("GL_EXT_framebuffer_multisample supported: max samples = %d", maxSamples);
	}
	else {
		LOG_ERROR("GL_EXT_framebuffer_multisample NOT supported");
	}

	if (GLEW_ARB_texture_non_power_of_two)		LOG_INFO("GL_ARB_texture_non_power_of_two supported: NPOT textures available.");
	else
		LOG_ERROR("GL_ARB_texture_non_power_of_two NOT supported");

	if (GLEW_EXT_framebuffer_object)		LOG_INFO("GL_EXT_framebuffer_object supported: FBO rendering available.");
	else
		LOG_ERROR("GL_EXT_framebuffer_object NOT supported");

	if (GLEW_ARB_texture_float)		LOG_INFO("GL_ARB_texture_float supported: High precision textures available.");
	else
		LOG_ERROR("GL_ARB_texture_float NOT supported");

	if (GLEW_ARB_shader_objects && GLEW_ARB_shading_language_100)
		LOG_INFO("GLSL (ARB_shader_objects) supported");
	else
		LOG_ERROR("GLSL support NOT available");

#ifdef _WIN32
	if (wglewIsSupported("WGL_EXT_swap_control")) {
		LOG_INFO("WGL_EXT_swap_control supported: vsync control available");
		if (wglGetSwapIntervalEXT)
			LOG_INFO("Current vsync swap interval = %d", wglGetSwapIntervalEXT());
		else
			LOG_ERROR("wglGetSwapIntervalEXT function pointer not available");
	}
	else {
		LOG_ERROR("WGL_EXT_swap_control NOT supported");
	}
#else
	// GLX equivalent, queried directly rather than through GLEW so this does
	// not depend on glxewInit() having run.
	{
		const char* exts = gDpy ? glXQueryExtensionsString(gDpy, DefaultScreen(gDpy)) : nullptr;
		if (exts && strstr(exts, "GLX_EXT_swap_control"))
			LOG_INFO("GLX_EXT_swap_control supported: vsync control available");
		else
			LOG_INFO("GLX_EXT_swap_control NOT advertised - vsync may be compositor-controlled");
	}
#endif
}

// GetGLDC()/GetGLRC() removed in Phase 3c - see the note in sys_gl.h. hDC and
// hRC are still used internally by this file; only the public accessors, which
// nothing called, are gone.

// -----------------------------------------------------------------------------
// InitOpenGLContext
// Initializes the OpenGL rendering context using WGL, with support for:
// - Legacy OpenGL 2.1 fallback
// - OpenGL 4.2 core or compatibility profile (default build)
// - OpenGL 3.3 core or compatibility profile (WIN7BUILD)
// - Optional multisampling (MSAA) via command-line switch
//
// Parameters:
//   forceLegacyGL2    - Forces use of legacy OpenGL 2.1 context, ignoring modern support
//   enableMultisample - If true, requests 4x MSAA (multisample anti-aliasing) if available
//   useCoreProfile    - If true, requests a forward-compatible core profile context;
//                       otherwise a compatibility profile is requested
//
// Returns:
//   true if context initialization succeeded, false if any stage failed.
//
// Behavior:
//   - Uses a temporary OpenGL context to initialize GLEW and check WGL extensions
//   - Attempts to set a multisample pixel format if requested and supported
//   - Creates either a core or compatibility context at the build's target
//     version (4.2 default / 3.3 under WIN7BUILD), falling back to 3.3 then 2.1
//   - Enables GL_MULTISAMPLE if MSAA was successfully requested
//
// Usage:
//   bool msaa = strstr(lpCmdLine, "-msaa") != nullptr;
//   bool core = strstr(lpCmdLine, "-core") != nullptr;
//   InitOpenGLContext(false, msaa, core);
// -----------------------------------------------------------------------------
#ifdef _WIN32

bool InitOpenGLContext(bool forceLegacyGL2, bool enableMultisample, bool useCoreProfile)
{
	HWND hwnd = win_get_window();
	hDC = GetDC(hwnd);

	// Step 1: Set temporary pixel format (required to create temp context)
	PIXELFORMATDESCRIPTOR tempPFD = {};
	tempPFD.nSize = sizeof(tempPFD);
	tempPFD.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
	tempPFD.iPixelType = PFD_TYPE_RGBA;
	tempPFD.cColorBits = 32;
	tempPFD.cDepthBits = 24;
	tempPFD.cStencilBits = 8;
	tempPFD.iLayerType = PFD_MAIN_PLANE;

	int tempFormat = ChoosePixelFormat(hDC, &tempPFD);
	if (!tempFormat || !SetPixelFormat(hDC, tempFormat, &tempPFD)) {
		LOG_ERROR("Failed to set temporary pixel format");
		return false;
	}

	// Step 2: Create temporary OpenGL context
	HGLRC tempContext = wglCreateContext(hDC);
	if (!tempContext || !wglMakeCurrent(hDC, tempContext)) {
		LOG_ERROR("Failed to create/make current temporary OpenGL context");
		return false;
	}

	// Step 3: Init GLEW
	glewExperimental = GL_TRUE;
	if (glewInit() != GLEW_OK) {
		LOG_ERROR("GLEW init failed");
		return false;
	}

	// Step 4: If requested, try MSAA with modern pixel format
	if (enableMultisample &&
		wglewIsSupported("WGL_ARB_multisample") &&
		wglewIsSupported("WGL_ARB_pixel_format"))
	{
		const int msaaAttribs[] = {
			WGL_DRAW_TO_WINDOW_ARB, GL_TRUE,
			WGL_SUPPORT_OPENGL_ARB, GL_TRUE,
			WGL_DOUBLE_BUFFER_ARB, GL_TRUE,
			WGL_PIXEL_TYPE_ARB, WGL_TYPE_RGBA_ARB,
			WGL_COLOR_BITS_ARB, 32,
			WGL_DEPTH_BITS_ARB, 24,
			WGL_STENCIL_BITS_ARB, 8,
			WGL_SAMPLE_BUFFERS_ARB, 1,
			WGL_SAMPLES_ARB, 4, // 4x MSAA
			0
		};

		int format;
		UINT numFormats;
		if (wglChoosePixelFormatARB(hDC, msaaAttribs, nullptr, 1, &format, &numFormats) && numFormats > 0) {
			PIXELFORMATDESCRIPTOR finalPFD;
			DescribePixelFormat(hDC, format, sizeof(finalPFD), &finalPFD);
			if (SetPixelFormat(hDC, format, &finalPFD)) {
				LOG_INFO("Using multisample pixel format (4x MSAA)");
			}
			else {
				LOG_INFO("Failed to set multisample pixel format, continuing without MSAA");
			}
		}
		else {
			LOG_INFO("Multisample format not supported, continuing without MSAA");
		}
	}
	else {
		LOG_INFO("MSAA not enabled or not supported - using legacy pixel format");
	}

	// Step 5: Create final OpenGL context
	if (forceLegacyGL2 || !wglewIsSupported("WGL_ARB_create_context")) {
		LOG_INFO("Using legacy OpenGL 2.1 context");
		hRC = tempContext;
	}
	else {
#ifdef WIN7BUILD
		const int reqMajor = 3, reqMinor = 3;
#else
		const int reqMajor = 4, reqMinor = 2;
#endif
		LOG_INFO("Creating OpenGL %d.%d %s profile context", reqMajor, reqMinor,
			useCoreProfile ? "core" : "compatibility");
		const int attribsPrimary[] = {
			WGL_CONTEXT_MAJOR_VERSION_ARB, reqMajor,
			WGL_CONTEXT_MINOR_VERSION_ARB, reqMinor,
			WGL_CONTEXT_FLAGS_ARB, useCoreProfile ? WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB : 0,
			WGL_CONTEXT_PROFILE_MASK_ARB,
				useCoreProfile ? WGL_CONTEXT_CORE_PROFILE_BIT_ARB : WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB,
			0
		};

		hRC = wglCreateContextAttribsARB(hDC, 0, attribsPrimary);

		if (!hRC) {
			LOG_INFO("OpenGL %d.%d not available, trying 3.3 compatibility...", reqMajor, reqMinor);
			const int attribs33[] = {
				WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
				WGL_CONTEXT_MINOR_VERSION_ARB, 3,
				WGL_CONTEXT_FLAGS_ARB, 0,
				WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB,
				0
			};
			hRC = wglCreateContextAttribsARB(hDC, 0, attribs33);
		}

		if (!hRC) {
			LOG_ERROR("wglCreateContextAttribsARB failed - falling back to OpenGL 2.1");
			hRC = tempContext;
		}
		else {
			wglMakeCurrent(nullptr, nullptr);
			wglDeleteContext(tempContext);
			wglMakeCurrent(hDC, hRC);
		}
	}

	if (enableMultisample)
		glEnable(GL_MULTISAMPLE);

	LOG_INFO("OpenGL %s, GLSL %s", glGetString(GL_VERSION), glGetString(GL_SHADING_LANGUAGE_VERSION));
	ReportOpenGLCapabilities();
	return true;
}

// -----------------------------------------------------------------------------
// DeleteGLContext
// Shuts down and deletes the OpenGL rendering context
// -----------------------------------------------------------------------------
void DeleteGLContext()
{
	if (hRC) {
		wglMakeCurrent(nullptr, nullptr);
		wglDeleteContext(hRC);
		hRC = nullptr;
	}
	if (hDC) {
		HWND hwnd = win_get_window(); // <--- re-fetch here
		if (hwnd) {
			ReleaseDC(hwnd, hDC); // only call if hwnd is valid
		}
		hDC = nullptr;
	}
	gOpenGLInitialized = false;
}

// -----------------------------------------------------------------------------
// SetvSync
// Enables or disables vertical sync if available
// -----------------------------------------------------------------------------
void SetvSync(bool enabled)
{
	if (wglSwapIntervalEXT)
		wglSwapIntervalEXT(enabled ? 1 : 0);
	else
		LOG_INFO("SetvSync called, but WGL_EXT_swap_control not supported");
}

// -----------------------------------------------------------------------------
// GLSwapBuffers
// Swaps the front and back buffers
// -----------------------------------------------------------------------------
void GLSwapBuffers()
{
	if (hDC) {
		SwapBuffers(hDC);
	}
	else {
		LOG_ERROR("GLSwapBuffers called with null device context");
	}
}

#else  // ---------------------------------------------------------------- GLX

// -----------------------------------------------------------------------------
// InitOpenGLContext (GLX)
//
// Same contract as the WGL version above. LinuxWindow must have called
// sys_gl_set_x11_target() first.
//
// The one thing that MUST NOT be simplified: glXCreateContextAttribsARB is an
// EXTENSION entry point and has to be resolved through glXGetProcAddressARB.
// Calling glXCreateContext() instead compiles and runs, but silently yields a
// legacy compatibility context - and the renderer's "#version 330 core"
// shaders then fail at draw time, far away from the real cause.
// -----------------------------------------------------------------------------
bool InitOpenGLContext(bool forceLegacyGL2, bool enableMultisample, bool useCoreProfile)
{
	if (!gDpy || !gWin) {
		LOG_ERROR("InitOpenGLContext: no X11 target - LinuxWindow must call "
		          "sys_gl_set_x11_target() before this");
		return false;
	}

	if (!gFbc) {
		LOG_ERROR("InitOpenGLContext: no framebuffer config - "
		          "sys_gl_choose_x11_visual() must run before the window is created");
		return false;
	}
	GLXFBConfig chosen = gFbc;

	typedef GLXContext (*PFNGLXCREATECONTEXTATTRIBSARB)(Display*, GLXFBConfig, GLXContext, Bool, const int*);
	PFNGLXCREATECONTEXTATTRIBSARB createContextAttribs =
		(PFNGLXCREATECONTEXTATTRIBSARB)glXGetProcAddressARB(
			(const GLubyte*)"glXCreateContextAttribsARB");

	if (!createContextAttribs) {
		LOG_ERROR("GLX: glXCreateContextAttribsARB unavailable - cannot create a "
		          "core profile context, and the 330 core shaders require one");
		return false;
	}

	// Match the Windows request order: primary target, then 3.3, then legacy.
	const int wantMajor = forceLegacyGL2 ? 2 : 4;
	const int wantMinor = forceLegacyGL2 ? 1 : 2;

	int attribs[] = {
		GLX_CONTEXT_MAJOR_VERSION_ARB, wantMajor,
		GLX_CONTEXT_MINOR_VERSION_ARB, wantMinor,
		GLX_CONTEXT_PROFILE_MASK_ARB,
			useCoreProfile ? GLX_CONTEXT_CORE_PROFILE_BIT_ARB
			               : GLX_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB,
		None
	};

	gCtx = createContextAttribs(gDpy, chosen, nullptr, True, attribs);

	if (!gCtx && !forceLegacyGL2) {
		LOG_INFO("GLX: %d.%d context refused, falling back to 3.3", wantMajor, wantMinor);
		attribs[1] = 3;
		attribs[3] = 3;
		gCtx = createContextAttribs(gDpy, chosen, nullptr, True, attribs);
	}
	if (!gCtx) {
		LOG_ERROR("GLX: could not create an OpenGL context");
		return false;
	}

	if (!glXMakeCurrent(gDpy, gWin, gCtx)) {
		LOG_ERROR("GLX: glXMakeCurrent failed");
		glXDestroyContext(gDpy, gCtx);
		gCtx = nullptr;
		return false;
	}

	glewExperimental = GL_TRUE;   // required for core profiles
	GLenum err = glewInit();
	if (err != GLEW_OK) {
		LOG_ERROR("GLEW init failed: %s", glewGetErrorString(err));
		return false;
	}
	// A core profile makes glewInit leave a spurious GL_INVALID_ENUM behind.
	glGetError();

	if (enableMultisample)
		glEnable(GL_MULTISAMPLE);

	gOpenGLInitialized = true;
	ReportOpenGLCapabilities();
	return true;
}

void DeleteGLContext()
{
	if (gDpy && gCtx) {
		glXMakeCurrent(gDpy, 0, nullptr);
		glXDestroyContext(gDpy, gCtx);
		gCtx = nullptr;
	}
	gOpenGLInitialized = false;
}

void SetvSync(bool enabled)
{
	if (!gDpy || !gWin) return;

	typedef void (*PFNGLXSWAPINTERVALEXT)(Display*, GLXDrawable, int);
	static PFNGLXSWAPINTERVALEXT swapIntervalEXT =
		(PFNGLXSWAPINTERVALEXT)glXGetProcAddressARB((const GLubyte*)"glXSwapIntervalEXT");

	if (swapIntervalEXT) {
		swapIntervalEXT(gDpy, gWin, enabled ? 1 : 0);
		return;
	}

	// MESA and SGI fallbacks, in that order.
	typedef int (*PFNGLXSWAPINTERVALINT)(int);
	static PFNGLXSWAPINTERVALINT swapIntervalMESA =
		(PFNGLXSWAPINTERVALINT)glXGetProcAddressARB((const GLubyte*)"glXSwapIntervalMESA");
	if (swapIntervalMESA) { swapIntervalMESA(enabled ? 1 : 0); return; }

	static PFNGLXSWAPINTERVALINT swapIntervalSGI =
		(PFNGLXSWAPINTERVALINT)glXGetProcAddressARB((const GLubyte*)"glXSwapIntervalSGI");
	if (swapIntervalSGI) { swapIntervalSGI(enabled ? 1 : 0); return; }

	// Say so rather than silently doing nothing - under a compositor vsync is
	// often forced on regardless, and that is worth knowing when timing looks odd.
	LOG_INFO("SetvSync: no GLX swap-control extension; vsync is compositor-controlled");
}

void GLSwapBuffers()
{
	// The first three swaps are logged and no more. This carried a `%120`
	// clause as well, which at 60fps put a line in the log every two seconds
	// for the whole session - added while chasing a window that never
	// presented, and left in after that was solved. Logging is asynchronous so
	// one line is unlikely to cost a frame, but per-frame diagnostics do not
	// belong in a build anyone plays, and "every two seconds" is a poor thing
	// to have in the log while investigating a stutter that recurs every few
	// seconds.
	static int s_swapCount = 0;
	if (s_swapCount < 3) {
		++s_swapCount;
		LOG_INFO("GLSwapBuffers #%d (dpy=%p win=%lu)", s_swapCount,
		         (void*)gDpy, (unsigned long)gWin);
	}

	if (gDpy && gWin)
		glXSwapBuffers(gDpy, gWin);
	else
		LOG_ERROR("GLSwapBuffers called with no X11 target");
}

#endif // _WIN32

// -----------------------------------------------------------------------------
// CheckGLVersionSupport
// Displays warning if OpenGL version is below 2.0
// -----------------------------------------------------------------------------
void CheckGLVersionSupport()
{
	const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
	int major = 0, minor = 0;

#ifdef _WIN32
	if (sscanf_s(version, "%d.%d", &major, &minor) != 2)
#else
	if (sscanf(version, "%d.%d", &major, &minor) != 2)
#endif
	{
		LOG_DEBUG("Failed to parse OpenGL version string: %s", version);
		return;
	}

	LOG_DEBUG("OpenGL Version supported %d.%d", major, minor);

	if (major < 2)
	{
#ifdef _WIN32
		MessageBox(nullptr, L"This program may not work. Your OpenGL version is less than 2.0.",
			L"OpenGL Version Warning", MB_ICONERROR | MB_OK);
#else
		// No portable dialog, and AAE takes no toolkit dependency. Error level
		// so it is visible on the console and in systemlog.txt.
		LOG_ERROR("This program may not work: your OpenGL version (%d.%d) is "
		          "less than 2.0", major, minor);
#endif
	}
}

// -----------------------------------------------------------------------------
// ReSizeGLScene
// Resets OpenGL viewport
// -----------------------------------------------------------------------------

float ReSizeGLScene(int width, int height)
{
	if (height == 0) height = 1;
	glViewport(0, 0, width, height);
	return static_cast<float>(width) / height;
}

// -----------------------------------------------------------------------------
// ViewOrtho
// Sets up 2D orthographic projection
// -----------------------------------------------------------------------------
void ViewOrtho(int width, int height)
{
	// Core profile: the fixed-function projection/modelview matrices are no longer
	// used -- the renderer drives projection via g_proj / per-shader uProj uniforms.
	// Kept as a no-op for its existing window-setup / resize call sites.
	(void)width; (void)height;
}

// -----------------------------------------------------------------------------
// IsOpenGLInitialized
// Returns true if InitOpenGLContext() was successful
// -----------------------------------------------------------------------------
bool IsOpenGLInitialized()
{
	return gOpenGLInitialized;
}