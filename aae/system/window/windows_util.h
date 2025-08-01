// -----------------------------------------------------------------------------
// File: windows_util.h
//
// Description:
// Utility functions and data structures for advanced Win32 window creation.
// Includes DPI-aware support, fullscreen toggling, and window size helpers.
// Designed to work with modern OpenGL applications targeting consistent
// pixel-based layouts.
//
// Core Features:
// - Unified WindowConfig struct for all window modes.
// - DPI awareness toggle.
// - Create centered, custom, or fullscreen windows.
// - Automatically tracks current client size.
// - Handles Windows 11 visual quirks (like corner rounding).
// - Safe toggling of borderless fullscreen via ALT+ENTER or code.
// - Compatible with gl_basics and iniFile for persistent settings.
//
// Dependencies:
// - utf8conv.h (for UTF-8 string support)
// - iniFile.cpp/.h (for saving/loading window size/resolution)
// - gl_basics.h/.cpp (for setting up OpenGL scene size)
//
// -----------------------------------------------------------------------------

#pragma once
#include <windows.h>
#include <string>
#include "MathUtils.h"

enum WindowsOS { Win7, Win8, Win10, Win11, NotFind };

void SaveAndDisableAccessibilityPopups();
void RestoreAccessibilityPopups();
void osMessage(int ID, const char* fmt, ...);
void allegro_message(const char* title, const char* message);
std::string GetLastErrorStdStr();
WindowsOS GetOsVersion();
RECT GetOpenGLScreenRect(HWND hwnd);