#pragma once
// ====================================================================
// mame_layout.h - MAME .lay file parser and renderer
//
// Parses XML layout files (using tinyxml2), loads artwork textures
// (using stb_image), generates procedural overlay/rect textures,
// and renders the complete layout using OpenGL.
//
// Layer draw order matches MAME convention:
//   backdrop -> screen -> overlay -> bezel
//
// The overlay layer is not drawn separately; instead it is multiplied
// into the screen layer via a dual-texture shader so the color gel
// effect is applied directly to the emulator framebuffer.
//
// Integration:
//   - Call Layout_Parse() after selecting a game to load the .lay XML.
//   - Call Layout_LoadTextures() after the GL context is ready.
//   - Call Layout_FindView() to pick the active view.
//   - Call Layout_Render() each frame to draw the complete layout.
//   - Call Layout_FreeTextures() on shutdown.
//
// This module is self-contained. It creates its own VAO, VBO, and
// shader programs (lazy-initialized on first render) and does not
// touch any fixed-function pipeline state used by the existing
// vector/raster rendering path.
// ====================================================================

#include <string>
#include <vector>
#include <map>

#include "framework.h"  // pulls in glew.h, wglew.h, GL types

// ====================================================================
// Layer types matching MAME draw order:
//   backdrop -> screen -> overlay -> bezel
// ====================================================================
enum class LayerType {
    Backdrop,
    Screen,
    Overlay,
    Bezel
};

// A colored rectangle within an element definition (for procedural elements)
struct LayoutRect {
    float x, y, w, h;          // bounds (in element-local coords)
    float r, g, b, a;          // color
};

// An element definition from <element name="...">
// Can be image-based or procedural (composed of colored rects)
struct LayoutElement {
    std::string name;
    std::string imageFile;              // empty if procedural
    std::vector<LayoutRect> rects;      // non-empty if procedural
    GLuint textureID = 0;               // generated texture (image or baked procedural)
    int texWidth = 0, texHeight = 0;
};

// A single drawable item within a view (bezel, backdrop, overlay, or screen)
struct LayoutDrawable {
    LayerType layer;
    LayoutElement* element;     // nullptr for screen entries
    float x, y, w, h;          // bounds in layout coordinates
    float alpha;                // from <color alpha="...">
    int screenIndex;            // only meaningful for LayerType::Screen
};

// A named view from the layout file
struct LayoutView {
    std::string name;
    std::vector<LayoutDrawable> drawables;

    // Screen bounds within this view (from the <screen> element)
    float screenX = 0, screenY = 0, screenW = 0, screenH = 0;

    // Total bounding box of all drawables in this view
    float boundsX = 0, boundsY = 0, boundsW = 0, boundsH = 0;
};

// Top-level layout data returned by Layout_Parse()
struct LayoutData {
    std::map<std::string, LayoutElement> elements;
    std::vector<LayoutView> views;
};

// ====================================================================
// API
// ====================================================================

// Parse a .lay XML file. 
// If zipFile is non-empty, loads layFilename from inside the ZIP archive.
// If zipFile is empty, loads layFilename as a filesystem path.
// artworkDir is used for fallback loose-file loading.
bool Layout_Parse(const std::string& layFilename, const std::string& zipFile,
    const std::string& artworkDir, LayoutData& outData);

// Load textures for all elements in the layout.
// If zipFile is non-empty, loads PNGs from inside the ZIP archive.
// If zipFile is empty, loads PNGs from artworkDir on the filesystem.
bool Layout_LoadTextures(LayoutData& data, const std::string& zipFile,
    const std::string& artworkDir);

// Find a view by name. Returns nullptr if not found.
LayoutView* Layout_FindView(LayoutData& data, const std::string& viewName);

// Render the complete layout into the current OpenGL viewport.
// screenTexture is the emulator framebuffer texture to draw in the <screen> slot.
// winW/winH are the current window client area dimensions in pixels.
// videoAttributes is the game's video_attributes flags (from Machine->drv).
// Used to select the correct overlay blending mode:
//   VIDEO_TYPE_RASTER_BW  -> pure multiply   (screen * overlay)
//   VIDEO_TYPE_RASTER_COLOR -> 2x multiply   (screen * overlay * 2, clamped)
void Layout_Render(const LayoutView& view, GLuint screenTexture,
    int winW, int winH, int videoAttributes = 0);

void Layout_CreateDefaultScreen(LayoutData& outData, float screenW, float screenH);

// Returns the GL texture ID of the first overlay element in the view,
// or 0 if no overlay exists or overlays are disabled.
GLuint Layout_GetOverlayTexture(const LayoutView& view);

// Free all OpenGL textures owned by the layout, plus the layout
// shader programs and VAO/VBO. Safe to call multiple times.
void Layout_FreeTextures(LayoutData& data);

// ====================================================================
// Game dimension & aspect ratio utilities
//
// These replace the old raster_render module. The game's oriented
// output dimensions and pixel aspect ratio are computed here so the
// layout system and window management code can query them.
// ====================================================================

// Compute the game's oriented output dimensions and pixel aspect ratio
// from Machine->drv->visible_area and Machine->orientation.
// Call after Machine is set up (after run_a_game / vh_open).
void Layout_InitGameDimensions();

// Reset game dimensions to zero. Call when stopping a game.
void Layout_ResetGameDimensions();

// Query the computed game output dimensions (after orientation).
int   Layout_GetGameWidth();
int   Layout_GetGameHeight();
float Layout_GetPixelAspect();

// Compute the display aspect ratio for the current game, taking into
// account the active layout view, bezel/crop settings, orientation,
// and pixel aspect correction. Returns 0 if no valid dimensions.
float Layout_ComputeGameAspect();

// Load (or create) a layout for the given game driver.
// Searches for a .lay file in the external artwork path and local artwork
// directory (ZIP first, then loose files). If no .lay is found or parsing
// fails, creates a synthetic screen-only layout as a fallback.
// Sets g_layoutEnabled, g_activeView, g_layoutAspect, and g_layoutData.
struct AAEDriver;
void Layout_LoadForGame(const AAEDriver* drv);

// ====================================================================
// Global layout state (set by the app, read by the renderer)
// ====================================================================
extern bool        g_layoutEnabled;
extern LayoutData  g_layoutData;
extern LayoutView* g_activeView;
extern float       g_layoutAspect;   // boundsW / boundsH of active view

// Layout display options (toggled by menu or config)
extern bool g_layoutShowBezel;       // show/hide bezel artwork
extern bool g_layoutShowOverlay;     // show/hide color overlay
extern bool g_layoutShowBackdrop;    // show/hide backdrop
extern bool g_layoutZoomToScreen;    // zoom to screen area (like MAME "Zoom to Screen Area")

// Call after changing zoom/bezel/show settings to recalculate g_layoutAspect
void Layout_UpdateAspect();
