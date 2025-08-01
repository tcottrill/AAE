// -----------------------------------------------------------------------------
// hotspot.cpp
//
// Description:
// HotSpot interaction system for 2D GUIs. Supports both rectangle and convex
// polygon hotspots. Handles hover/click detection, callback triggering, and
// OpenGL debug drawing.
//
// License:
// This is free and unencumbered software released into the public domain
// under the Unlicense. For more information, see <http://unlicense.org/>
// -----------------------------------------------------------------------------

#include "hotspot.h"
#include "collision_tests.h"
#include "sys_gl.h"
#include "sys_log.h"

HotSpot::HotSpot() {}
HotSpot::~HotSpot() {}

void HotSpot::Clear() {
    entries.clear();
}

void HotSpot::AddRect(const Rect2D& r, const std::string& name, std::function<void()> cb) {
    entries.emplace_back(r, name, cb);
}

void HotSpot::AddFromCenter(float x, float y, float width, float height, const std::string& name, std::function<void()> cb) {
    float minx = x - width / 2.0f;
    float miny = y - height / 2.0f;
    Rect2D rect(minx, miny, width, height);
    AddRect(rect, name, cb);
}

void HotSpot::AddFromCenter(const Vec2& center, const Vec2& size, const std::string& name, std::function<void()> cb) {
    float minx = center.x - size.x / 2.0f;
    float miny = center.y - size.y / 2.0f;
    Rect2D rect(minx, miny, size.x, size.y);
    AddRect(rect, name, cb);
}

void HotSpot::AddPolygon(const std::vector<Vec2>& points, const std::string& name, std::function<void()> cb) {
    HotSpotEntry entry(Rectangle_Test(const_cast<std::vector<Vec2>&>(points)), name, cb);
    entry.isPolygon = true;
    entry.polygon = points;
    entries.emplace_back(std::move(entry));
}

void HotSpot::AddCircle(const Vec2& center, float radius,
    const std::string& name, std::function<void()> cb) {
    entries.emplace_back(center, radius, name, cb);
}

void HotSpot::UpdateMouseRect(float x, float y, float width, float height) {
    float minx = x - width / 2.0f;
    float miny = y - height / 2.0f;
    mouseRect = Rect2D(minx, miny, width, height);
}

void HotSpot::Update(int mouseButtons, float mouseX, float mouseY) {
    UpdateMouseRect(mouseX, mouseY);

    std::vector<Vec2> mouseQuad = {
        Vec2(mouseRect.x, mouseRect.y),
        Vec2(mouseRect.x + mouseRect.width, mouseRect.y),
        Vec2(mouseRect.x + mouseRect.width, mouseRect.y + mouseRect.height),
        Vec2(mouseRect.x, mouseRect.y + mouseRect.height)
    };

    for (auto& entry : entries) {
        if (!entry.active) continue;

        bool overlap = false;

        if (entry.isPolygon) {
            overlap = isColliding(entry.polygon, mouseQuad);
        }
        else if (entry.isCircle) {
            // Test center of mouseRect against circle
            float cx = mouseRect.x + mouseRect.width * 0.5f;
            float cy = mouseRect.y + mouseRect.height * 0.5f;
            float dx = cx - entry.circleCenter.x;
            float dy = cy - entry.circleCenter.y;
            overlap = (dx * dx + dy * dy) <= (entry.circleRadius * entry.circleRadius);
        }
        else {
            overlap = rectRect(mouseRect, entry.rect);
        }

        entry.mouseover = overlap;
        bool justClicked = overlap && (mouseButtons & 1) && !(lastMouseButtons & 1);
        entry.clicked = justClicked;

        if (entry.clicked)
            LOG_INFO("HotSpot clicked: %s", entry.name.c_str());

        if (justClicked && entry.callback)
            entry.callback();
    }

    lastMouseButtons = mouseButtons;
}

bool HotSpot::IsClicked(int index) const {
    return index >= 0 && index < (int)entries.size() && entries[index].clicked;
}

bool HotSpot::IsMouseOver(int index) const {
    return index >= 0 && index < (int)entries.size() && entries[index].mouseover;
}

bool HotSpot::IsClickedByName(const std::string& name) const {
    for (const auto& e : entries) {
        if (e.name == name && e.clicked)
            return true;
    }
    return false;
}

bool HotSpot::WasClickedByName(const std::string& name) {
    for (auto& e : entries) {
        if (e.name == name && e.clicked) {
            e.clicked = false;
            return true;
        }
    }
    return false;
}

bool HotSpot::IsMouseOverByName(const std::string& name) const {
    for (const auto& e : entries) {
        if (e.name == name && e.mouseover)
            return true;
    }
    return false;
}

void HotSpot::DebugDraw() const {
    glDisable(GL_TEXTURE_2D);

    for (const auto& e : entries) {
        // Color based on mouseover status
        if (e.mouseover)
            glColor4f(0.0f, 1.0f, 0.0f, 0.8f);  // Green if hovered
        else
            glColor4f(1.0f, 0.0f, 0.0f, 0.4f);  // Red otherwise

        if (e.isPolygon) {
            glBegin(GL_LINE_LOOP);
            for (const Vec2& v : e.polygon)
                glVertex2f(v.x, v.y);
            glEnd();
        }
        else if (e.isCircle) {
            const int segments = 32;
            float cx = e.circleCenter.x;
            float cy = e.circleCenter.y;
            float r = e.circleRadius;

            glBegin(GL_LINE_LOOP);
            for (int i = 0; i < segments; ++i) {
                float theta = (float)i / segments * 2.0f * k_pi;
                float x = cx + cosf(theta) * r;
                float y = cy + sinf(theta) * r;
                glVertex2f(x, y);
            }
            glEnd();
        }
        else {
            const Rect2D& r = e.rect;
            glBegin(GL_LINE_LOOP);
            glVertex2f(r.x, r.y);
            glVertex2f(r.x + r.width, r.y);
            glVertex2f(r.x + r.width, r.y + r.height);
            glVertex2f(r.x, r.y + r.height);
            glEnd();
        }
    }

    // Draw mouse probe rect in cyan
    glColor4f(0.0f, 1.0f, 1.0f, 0.6f);
    glBegin(GL_LINE_LOOP);
    glVertex2f(mouseRect.x, mouseRect.y);
    glVertex2f(mouseRect.x + mouseRect.width, mouseRect.y);
    glVertex2f(mouseRect.x + mouseRect.width, mouseRect.y + mouseRect.height);
    glVertex2f(mouseRect.x, mouseRect.y + mouseRect.height);
    glEnd();

    glEnable(GL_TEXTURE_2D);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f); // reset to white
}
