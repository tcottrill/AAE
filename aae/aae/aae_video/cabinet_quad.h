//==========================================================================
// AAE - Another Arcade Emulator
// Copyright (C) 2024-2026 Tim Cottrill - GNU GPL v3 or later.
//==========================================================================
// cabinet_quad.h - shared quad rule for vector cabinet art (backdrop, bezel).
//
// Both renderers (OpenGL final_render / Vulkan RecordCompositeLayered) place a
// cabinet art layer on the 1024x1024 vector canvas, which is later shown on
// screen at aspect D (4:3 for landscape games, 3:4 for sideways games; see
// cabinet_canvas_aspect). At 4:3 canvas height 1024 displays as 768/1024 of the
// canvas width; in general the displayed width of 1024 canvas units is
// 1024*D, so aspect A needs 1024*A/D canvas units. A layer is drawn at the TRUE aspect of its own texture, centred on the
// canvas, so a non-4:3 image is no longer stretched. For A == 4/3 the result is
// exactly the historical full-canvas quad.
//
// Canvas units, Y-UP (origin bottom-left), same space as the old
// DrawCabinetScaledLayer.
//==========================================================================
#pragma once

struct CabinetQuad
{
	float x1, y1, x2, y2;   // left, bottom, right, top (canvas units)
};

// On-screen aspect (width/height) at which the square canvas is DISPLAYED.
// wsAspect : GetWindowSetup().aspectRatio (the letterbox aspect of the canvas
//            as shown, i.e. already the post-rotation screen shape).
// rotIndex : system rotation index (0 none, 1 = -ror, 2 = -rol, 3 = 180).
// For 90/270 the canvas is turned, so its own x/y map to the screen's y/x and
// the aspect it is shown at is the inverse of the window aspect. 180 keeps it.
// Shared by GL and VK so both place cabinet art identically. <= 0 -> 4:3.
inline float cabinet_canvas_aspect(float wsAspect, int rotIndex)
{
	float d = wsAspect;
	if (!(d > 0.0f)) return 4.0f / 3.0f;
	if (rotIndex == 1 || rotIndex == 2) d = 1.0f / d;
	return d;
}

// texW/texH : the layer's texture size in pixels (<=0 falls back to 4:3).
// crop      : config.artcrop.
// bezelx/bezely/bezelzoom : the per-game video.ini crop globals.
// canvasAspect : on-screen aspect the canvas is displayed at
//                (cabinet_canvas_aspect); 4/3 reproduces the historical 768*A.
inline CabinetQuad cabinet_art_quad(float texW, float texH, bool crop,
	float bezelx, float bezely, float bezelzoom, float canvasAspect)
{
	const float A = (texW > 0.0f && texH > 0.0f) ? (texW / texH) : (4.0f / 3.0f);
	const float D = (canvasAspect > 0.0f) ? canvasAspect : (4.0f / 3.0f);
	const float w = 1024.0f * A / D;  // canvas width that shows at aspect A
	const float x0 = 512.0f - w / 2.0f; // centred; < 0 for A > 4/3

	CabinetQuad q;
	if (crop)
	{
		q.x1 = bezelx + x0 * bezelzoom;
		q.y1 = bezely + 0.0f * bezelzoom;
		q.x2 = bezelx + (x0 + w) * bezelzoom;
		q.y2 = bezely + 1024.0f * bezelzoom;
	}
	else
	{
		q.x1 = x0;
		q.y1 = 0.0f;
		q.x2 = x0 + w;
		q.y2 = 1024.0f;
	}
	return q;
}
