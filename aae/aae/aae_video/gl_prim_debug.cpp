#include "framework.h"
#include "gl_prim_debug.h"
#include <algorithm>
#include <cmath>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

void glCircle(float cx, float cy, float r, int num_segments)
{
	if (num_segments <= 0) return;

	std::vector<float> vertices;
	vertices.reserve(num_segments * 2);

	for (int ii = 0; ii < num_segments; ii++) {
		float theta = 2.0f * M_PI * float(ii) / float(num_segments); // get the current angle
		vertices.push_back(cx + r * cosf(theta)); // x component
		vertices.push_back(cy + r * sinf(theta)); // y component
	}

	glEnableClientState(GL_VERTEX_ARRAY);
	glVertexPointer(2, GL_FLOAT, 0, vertices.data());
	glDrawArrays(GL_LINE_LOOP, 0, num_segments);
	glDisableClientState(GL_VERTEX_ARRAY);
}

void glPoint(float x, float y)
{
	float pt[2] = { x, y };

	glDisable(GL_TEXTURE_2D);
	glColor3f(0.5f, 1.0f, 0.5f);
	glPointSize(4.0f);

	glEnableClientState(GL_VERTEX_ARRAY);
	glVertexPointer(2, GL_FLOAT, 0, pt);
	glDrawArrays(GL_POINTS, 0, 1);
	glDisableClientState(GL_VERTEX_ARRAY);
}

void glLine(float sx, float sy, float ex, float ey)
{
	float pts[4] = { sx, sy, ex, ey };

	glEnableClientState(GL_VERTEX_ARRAY);
	glVertexPointer(2, GL_FLOAT, 0, pts);
	glDrawArrays(GL_LINES, 0, 2);
	glDisableClientState(GL_VERTEX_ARRAY);
}

void glRectfromCenter(float x, float y, float size)
{
	float half = size / 2.0f;
	float pts[8] = {
		x - half, y - half,
		x + half, y - half,
		x + half, y + half,
		x - half, y + half
	};

	glEnableClientState(GL_VERTEX_ARRAY);
	glVertexPointer(2, GL_FLOAT, 0, pts);
	glDrawArrays(GL_LINE_LOOP, 0, 4);
	glDisableClientState(GL_VERTEX_ARRAY);
}

void glRect(float xmin, float xmax, float ymin, float ymax)
{
	float pts[8] = {
		xmin, ymin,
		xmin, ymax,
		xmax, ymax,
		xmax, ymin
	};

	glEnableClientState(GL_VERTEX_ARRAY);
	glVertexPointer(2, GL_FLOAT, 0, pts);
	// Changed GL_QUADS to GL_TRIANGLE_FAN for better modern/forward compatibility
	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
	glDisableClientState(GL_VERTEX_ARRAY);
}

void glRectR(float x, float y, float size, float angle)
{
	float half = size / 2.0f;

	// Create base square coordinates
	float pts[8] = {
		x - half, y - half,
		x + half, y - half,
		x + half, y + half,
		x - half, y + half
	};

	// Apply rotation if required
	if (angle != 0.0f)
	{
		float rad = angle * (M_PI / 180.0f);
		float c = cosf(rad);
		float s = sinf(rad);

		for (int i = 0; i < 4; i++)
		{
			// Translate point back to origin
			float px = pts[i * 2] - x;
			float py = pts[i * 2 + 1] - y;

			// Rotate and translate back
			pts[i * 2] = x + (px * c - py * s);
			pts[i * 2 + 1] = y + (px * s + py * c);
		}
	}

	glEnableClientState(GL_VERTEX_ARRAY);
	glVertexPointer(2, GL_FLOAT, 0, pts);
	glDrawArrays(GL_LINE_LOOP, 0, 4);
	glDisableClientState(GL_VERTEX_ARRAY);
}