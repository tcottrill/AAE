///
///  Implementation - C++17 clean-up
///
#include "collision_tests.h"
#include "sys_log.h"

#include <cmath>           // std::hypot, std::pow
#include <algorithm>       // std::min, std::max
#include <limits>          // std::numeric_limits

// ------------------------------------------------------------
//  CIRCLE / CIRCLE
// ------------------------------------------------------------
bool circleCircle(Circle& c1, Circle& c2)
{
    const float distX = c1._x - c2._x;
    const float distY = c1._y - c2._y;
    const float distance = std::hypot(distX, distY);

    return distance <= (c1.radius + c2.radius);
}

// ------------------------------------------------------------
//  RECTANGLE / RECTANGLE
// ------------------------------------------------------------
bool rectRect(const Rect2D& r1, const Rect2D& r2)
{
    return  r1.x + r1.width >= r2.x && // r1 right  past r2 left
        r1.x <= r2.x + r2.width && // r1 left   past r2 right
        r1.y + r1.height >= r2.y && // r1 top    past r2 bottom
        r1.y <= r2.y + r2.height;  // r1 bottom past r2 top
}

// ------------------------------------------------------------
//  CIRCLE / RECTANGLE
// ------------------------------------------------------------
bool circleRect(Circle& c, Rect2D& r)
{
    // clamp circle centre to the rectangle
    float testX = std::clamp(c._x, r.x, r.x + r.width);
    float testY = std::clamp(c._y, r.y, r.y + r.height);

    const float distX = c._x - testX;
    const float distY = c._y - testY;
    const float distance = std::hypot(distX, distY);

    return distance <= c.radius;
}

// ------------------------------------------------------------
//  POLYGON helpers (Separating-Axis Theorem)
// ------------------------------------------------------------
static bool checkCollisionOneSided(vector<Vec2>& a, vector<Vec2>& b)
{
    const std::size_t sides = a.size();

    for (std::size_t i = 0; i < sides; ++i)
    {
        const Vec2  edge = a[(i + 1) % sides] - a[i];
        const Vec2  normal = edge.perp();

        double minA = std::numeric_limits<double>::infinity();
        double maxA = -std::numeric_limits<double>::infinity();
        for (const auto& v : a)
        {
            const double d = normal.dot(v);
            minA = std::min(minA, d);
            maxA = std::max(maxA, d);
        }

        double minB = std::numeric_limits<double>::infinity();
        double maxB = -std::numeric_limits<double>::infinity();
        for (const auto& v : b)
        {
            const double d = normal.dot(v);
            minB = std::min(minB, d);
            maxB = std::max(maxB, d);
        }

        if (minB > maxA || minA > maxB)   // found a separating axis
            return false;
    }
    return true;
}

bool isColliding(vector<Vec2>& object1, vector<Vec2>& object2)
{
    return  checkCollisionOneSided(object1, object2) &&
        checkCollisionOneSided(object2, object1);
}

bool rect_to_poly_collision(Rect2D& r, vector<Vec2>& poly)
{
    vector<Vec2> rectPoly;
    rectPoly.reserve(4);

    const float minx = r.x;
    const float miny = r.y;
    const float maxx = r.x + r.width;
    const float maxy = r.y + r.height;

    rectPoly.emplace_back(minx, miny);
    rectPoly.emplace_back(maxx, miny);
    rectPoly.emplace_back(maxx, maxy);
    rectPoly.emplace_back(minx, maxy);

    return isColliding(rectPoly, poly);
}

Rect2D Rectangle_Test(vector<Vec2>& poly)
{
    Vec2 min = poly.front();
    Vec2 max = poly.front();

    for (const auto& p : poly)
    {
        min.x = std::min(min.x, p.x);
        min.y = std::min(min.y, p.y);
        max.x = std::max(max.x, p.x);
        max.y = std::max(max.y, p.y);
    }
    return Rect2D(min, Vec2(max.x - min.x, max.y - min.y));
}

// ------------------------------------------------------------
//  LINE helpers
// ------------------------------------------------------------
void calculate_line_point(int x1, int y1, int x2, int y2, int distance)
{
    const float vx = static_cast<float>(x2 - x1);  // direction vector
    const float vy = static_cast<float>(y2 - y1);
    const float mag = std::hypot(vx, vy);

    if (mag == 0.0f) return;                       // zero-length line

    const float nx = vx / mag;
    const float ny = vy / mag;

    const int px = (int)(x2 + nx * (float)distance);
    const int py = (int)(y2 + ny * (float)distance);

    (void)px; (void)py;                            // avoid unused-var warning
}

float dist(float px, float py, float x1, float y1)
{
    return std::hypot(px - x1, py - y1);
}

// ------------------------------------------------------------
//  LINE / POINT
// ------------------------------------------------------------
bool linePoint(float x1, float y1, float x2, float y2,
    float px, float py)
{
    const float d1 = dist(px, py, x1, y1);
    const float d2 = dist(px, py, x2, y2);
    const float lineLen = dist(x1, y1, x2, y2);

    constexpr float buffer = 0.1f;     // lenient epsilon

    return (d1 + d2) >= (lineLen - buffer) &&
        (d1 + d2) <= (lineLen + buffer);
}

// ------------------------------------------------------------
//  POINT / CIRCLE
// ------------------------------------------------------------
bool pointCircle(float px, float py, float cx, float cy, float r)
{
    return dist(px, py, cx, cy) <= r;
}

// ------------------------------------------------------------
//  LINE / CIRCLE
// ------------------------------------------------------------
bool lineCircle(float x1, float y1, float x2, float y2,
    float cx, float cy, float r)
{
    // End-points inside the circle? (quick-exit test)
    if (pointCircle(x1, y1, cx, cy, r) ||
        pointCircle(x2, y2, cx, cy, r))
        return true;

    // Vector from A->B
    const float dx = x2 - x1;
    const float dy = y2 - y1;
    const float lenSq = dx * dx + dy * dy;

    if (lenSq == 0.0f)   // degenerate line
        return false;

    // Project circle centre onto the infinite line, normalised to [0,1]
    const float t = ((cx - x1) * dx + (cy - y1) * dy) / lenSq;

    // Clamp to the actual segment
    const float tClamped = std::clamp(t, 0.0f, 1.0f);

    // Closest point on the segment
    const float closestX = x1 + tClamped * dx;
    const float closestY = y1 + tClamped * dy;

    // Collision if within radius
    return dist(closestX, closestY, cx, cy) <= r;
}
