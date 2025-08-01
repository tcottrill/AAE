// -----------------------------------------------------------------------------
// hotspot.h
//
// Description:
// HotSpot system for 2D GUI interaction. Manages rectangular, circular, and convex
// polygon regions that respond to mouse hover and click events. Supports named
// callbacks, SAT-based hit detection for polygons, and OpenGL debug drawing.
//
// License:
// This is free and unencumbered software released into the public domain
// under the Unlicense. For more information, see <http://unlicense.org/>
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// Usage Examples
// -----------------------------------------------------------------------------

// Rectangle HotSpot with Lambda
//
// hotspot.AddFromCenter(460, 575, 190, 89, "start_right", []() {
//     wrlog("start_right clicked via callback");
//     ani_counter = 550;
//     current_game_screen = 1;
// });

// Rectangle HotSpot with Named Function
//
// void StartLeftClicked() {
//     wrlog("start_left clicked via callback");
//     ani_counter = 550;
//     current_game_screen = 1;
// }
//
// hotspot.AddFromCenter(Vec2{ 190, 575 }, Vec2{ 190, 89 }, "start_left", StartLeftClicked);

// Polygon HotSpot (Convex Shape)
//
// std::vector<Vec2> triangle = { {100, 100}, {150, 50}, {200, 100} };
//
// hotspot.AddPolygon(triangle, "triangle_button", []() {
//     wrlog("triangle_button clicked");
// });

// Circle HotSpot with Named Function
//
// void CircleClicked() {
//     wrlog("circle_button clicked");
//     score += 10;
// }
//
// hotspot.AddCircle(Vec2(300, 200), 50.0f, "circle_button", CircleClicked);

// Non-Static Class Method (via std::bind)
//
// class MyUI {
// public:
//     void OnSettingsClicked() {
//         wrlog("settings clicked from member function");
//     }
// };
//
// MyUI ui;
// using std::placeholders::_1;
// hotspot.AddFromCenter(Vec2{400, 300}, Vec2{100, 50}, "settings", std::bind(&MyUI::OnSettingsClicked, &ui));


#pragma once

#include "MathUtils.h"
#include "rect2d.h"
#include <vector>
#include <string>
#include <functional>

class HotSpotEntry {
public:
    bool active = true;                   // Whether the hotspot is enabled
    bool mouseover = false;               // Is the mouse currently over it?
    bool clicked = false;                 // Was the hotspot clicked this frame?
    bool isPolygon = false;               // True if this is a polygon hotspot
    bool isCircle = false;                // True if this is a circle hotspot
    Vec2 circleCenter;                    // Circle center
    float circleRadius = 0.0f;            // Circle radius

    Rect2D rect;                          // Used for rectangle hotspots
    std::vector<Vec2> polygon;           // Used for convex polygon hotspots
    std::string name;                    // Optional name for lookup
    std::function<void()> callback;      // Optional callback triggered on click

    HotSpotEntry(const Rect2D& r, const std::string& n = "", std::function<void()> cb = nullptr)
        : rect(r), name(n), callback(cb) {
    }

    // New constructor for circles
    HotSpotEntry(const Vec2& center, float radius, const std::string& n = "", std::function<void()> cb = nullptr)
        : circleCenter(center), circleRadius(radius), name(n), callback(cb) {
        isCircle = true;
    }
};

class HotSpot {
public:
    HotSpot();
    ~HotSpot();

    // -------------------------------------------------------------------------
    // void DebugDraw() const
    //
    // Description:
    // Draws all hotspots and the mouse box using OpenGL. Useful for debugging.
    // Rectangles are red/green, polygons are outlined, and mouse box is cyan.
    // -------------------------------------------------------------------------
    void DebugDraw() const;

    // -------------------------------------------------------------------------
    // void Clear()
    //
    // Description:
    // Removes all currently stored hotspots.
    // -------------------------------------------------------------------------
    void Clear();

    // -------------------------------------------------------------------------
    // void AddRect(...)
    //
    // Description:
    // Adds a hotspot using a rectangular region.
    // -------------------------------------------------------------------------
    void AddRect(const Rect2D& r, const std::string& name = "", std::function<void()> cb = nullptr);

    // -------------------------------------------------------------------------
    // void AddFromCenter(...)
    //
    // Description:
    // Adds a hotspot based on center coordinates and size.
    // -------------------------------------------------------------------------
    void AddFromCenter(float x, float y, float width, float height,
        const std::string& name = "", std::function<void()> cb = nullptr);

    // -------------------------------------------------------------------------
    // void AddFromCenter(...)
    //
    // Description:
    // Adds a hotspot using center and size vectors.
    // -------------------------------------------------------------------------
    void AddFromCenter(const Vec2& center, const Vec2& size,
        const std::string& name = "", std::function<void()> cb = nullptr);

    // -------------------------------------------------------------------------
    // void AddPolygon(...)
    //
    // Description:
    // Adds a hotspot defined by a convex polygon using Vec2 points.
    // -------------------------------------------------------------------------
    void AddPolygon(const std::vector<Vec2>& points,
        const std::string& name = "", std::function<void()> cb = nullptr);

    // -------------------------------------------------------------------------
    // void AddCircle(...)
    //
    // Description:
    // Adds a hotspot defined by a circle (center + radius).
    // -------------------------------------------------------------------------
    void AddCircle(const Vec2& center, float radius,
        const std::string& name = "", std::function<void()> cb = nullptr);

    // -------------------------------------------------------------------------
    // bool IsMouseOverByName(...)
    //
    // Description:
    // Returns true if the named hotspot is currently under the mouse.
    // -------------------------------------------------------------------------
    bool IsMouseOverByName(const std::string& name) const;

    // -------------------------------------------------------------------------
    // void UpdateMouseRect(...)
    //
    // Description:
    // Sets the mouse probe rectangle used for hit testing this frame.
    // -------------------------------------------------------------------------
    void UpdateMouseRect(float x, float y, float width = 4, float height = 4);

    // -------------------------------------------------------------------------
    // void Update(...)
    //
    // Description:
    // Updates all hotspots with current mouse position and button state.
    // Triggers callbacks and updates `mouseover`/`clicked` flags.
    // -------------------------------------------------------------------------
    void Update(int mouseButtons, float mouseX, float mouseY);

    // -------------------------------------------------------------------------
    // bool IsClicked(int index)
    //
    // Description:
    // Returns true if the indexed hotspot was clicked this frame.
    // -------------------------------------------------------------------------
    bool IsClicked(int index) const;

    // -------------------------------------------------------------------------
    // bool IsMouseOver(int index)
    //
    // Description:
    // Returns true if the mouse is over the indexed hotspot.
    // -------------------------------------------------------------------------
    bool IsMouseOver(int index) const;

    // -------------------------------------------------------------------------
    // bool IsClickedByName(...)
    //
    // Description:
    // Returns true if the named hotspot was clicked this frame.
    // -------------------------------------------------------------------------
    bool IsClickedByName(const std::string& name) const;

    // -------------------------------------------------------------------------
    // bool WasClickedByName(...)
    //
    // Description:
    // Returns true if the named hotspot was clicked this frame,
    // and clears the clicked flag (for one-shot detection).
    // -------------------------------------------------------------------------
    bool WasClickedByName(const std::string& name);

    // -------------------------------------------------------------------------
    // size_t GetCount() const
    //
    // Description:
    // Returns the total number of registered hotspots.
    // -------------------------------------------------------------------------
    size_t GetCount() const { return entries.size(); }

    // -------------------------------------------------------------------------
    // const HotSpotEntry& GetEntry(size_t index) const
    //
    // Description:
    // Returns the hotspot at the given index.
    // -------------------------------------------------------------------------
    const HotSpotEntry& GetEntry(size_t index) const { return entries[index]; }

private:
    std::vector<HotSpotEntry> entries;
    Rect2D mouseRect;
    int lastMouseButtons = 0;
};
