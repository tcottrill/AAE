///
/// WARNING! All tests assume an OpenGL origin with (0,0) at the bottom-left
/// and a positive-up (ascending-Y) coordinate system.
///
/// Most code here is from https://github.com/jeffThompson/CollisionDetection
/// License: CC BY-NC-SA 3.0
///
#pragma once

#include <vector>          // std::vector
#include "MathUtils.h"
#include "circle.h"
#include "rect2d.h"

using std::vector;         // preserve original API style

// CIRCLE/CIRCLE
bool circleCircle(Circle& c1, Circle& c2);

// RECTANGLE/RECTANGLE
bool rectRect(const Rect2D& r1, const Rect2D& r2);

// CIRCLE/RECTANGLE
bool circleRect(Circle& c, Rect2D& r);

// POLYGON helpers
bool checkCollisionOneSided(vector<Vec2>& object1, vector<Vec2>& object2);
bool isColliding(vector<Vec2>& object1, vector<Vec2>& object2);
bool rect_to_poly_collision(Rect2D& f, vector<Vec2>& second);

// Broad-phase AABB for a polygon
Rect2D Rectangle_Test(vector<Vec2>& poly);

// LINE helpers
void  calculate_line_point(int x1, int y1, int x2, int y2, int distance);
float dist(float px, float py, float x1, float y1);

// LINE / POINT / CIRCLE tests
bool linePoint(float x1, float y1, float x2, float y2, float px, float py);
bool pointCircle(float px, float py, float cx, float cy, float r);
bool lineCircle(float x1, float y1, float x2, float y2,
    float cx, float cy, float r);
