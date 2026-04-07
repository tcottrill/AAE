//============================================================================
// driver_gui.cpp - AAE Frontend GUI Driver
//============================================================================

// -----------------------------------------------------------------------------
// Game Engine Alpha - Generic Module
// Generic component or utility file for the Game Engine Alpha project. This
// file may contain helpers, shared utilities, or subsystems that integrate
// seamlessly with the engine's rendering, audio, and gameplay frameworks.
//
// Integration:
//   This library is part of the **Game Engine Alpha** project and is tightly
//   integrated with its texture management, logging, and math utility systems.
//
// Usage:
//   Include this module where needed. It is designed to work as a building block
//   for engine subsystems such as rendering, input, audio, or game logic.
//
// License:
//   This program is free software: you can redistribute it and/or modify
//   it under the terms of the GNU General Public License as published by
//   the Free Software Foundation, either version 3 of the License, or
//   (at your option) any later version.
//
//   This program is distributed in the hope that it will be useful,
//   but WITHOUT ANY WARRANTY; without even the implied warranty of
//   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
//   GNU General Public License for more details.
//
//   You should have received a copy of the GNU General Public License
//   along with this program. If not, see <https://www.gnu.org/licenses/>.
//
// -----------------------------------------------------------------------------


#include <cstdlib>
#include <cstring>
#include <cctype>
#include <cmath>

#include "aae_mame_driver.h"
#include "driver_registry.h"
#include "driver_macros.h"
#include "inptport.h"
#include "aae_emulator.h"
#include "acommon.h"
#include "vector_fonts.h"
#include "driver_gui.h"
#include "menu.h"
#include "game_list.h"
#include "emu_vector_draw.h"
#include "gl_shader.h"

// =============================================================================
// Constants
// =============================================================================

static int input_cooldown = 0;

// Virtual screen dimensions (must match AAE_DRIVER_SCREEN below)
static constexpr int kScreenW = 1024;
static constexpr int kScreenH = 768;

// Vertical layout positions (Y-up coordinate system)
static constexpr int kTitleY = 700;
static constexpr int kSelectedY = 375;   // Center of selection highlight
static constexpr int kLineSpacing = 35;    // Pixels between list rows
static constexpr int kVisibleRows = 8;     // Rows above + below selection
static constexpr int kFooterY1 = 30;
static constexpr int kFooterY2 = 7;

// Text scales
static constexpr float kTitleScale = 2.8f;
static constexpr float kSelectedScale = 2.3f;
static constexpr float kFooterScale = 1.3f;

static constexpr float kListScale = 2.0f;

// Selection highlight bar (filled rectangle behind the selected title)
static constexpr float kSelBarWidth = 910.0f;  // Sized to reach near the ship glyph tips
static constexpr float kSelBarHeight = 30.0f;   // Vertical padding around the text
static constexpr float kSelBarYOffset = 8.0f;    // Nudge up to center on text (half font height * scale)
static constexpr rgb_t kSelBarColor = MAKE_RGBA(18, 14, 55, 130);  // Dark indigo, semi-transparent

// ---- Title "bling" (tuning knobs) ----------------------------------------
// Hue ping-pongs between Min and Max.  Occasionally a bright flash sweeps
// left-to-right across the title text.
static constexpr float kTitleHueSpeed = 0.2f;    // Degrees of hue shift per frame
static constexpr float kTitleHueMin = 160.0f;  // Start of hue range (cyan)
static constexpr float kTitleHueMax = 300.0f;  // End of hue range (magenta)

// Flash sweep
static constexpr float kFlashDuration = 1.0f;    // Seconds for full left-to-right sweep
static constexpr float kFlashWidth = 0.15f;   // Width of bright band (0-1 of text width)
static constexpr int   kFlashCooldownMin = 280;     // Min frames between flashes (~3 s at 60 fps)
static constexpr int   kFlashCooldownMax = 580;     // Max frames between flashes (~8 s)
static constexpr int   kFlashR = 255;     // Flash color R
static constexpr int   kFlashG = 255;     // Flash color G
static constexpr int   kFlashB = 240;     // Flash color B (warm white)

// Ship glyph animation
static constexpr int   kGlyphBaseX = 30;    // Left glyph X when idle
static constexpr int   kGlyphRightX = 992;   // Right glyph X when idle
static constexpr int   kGlyphY = 382;
static constexpr float kGlyphScale = 3.0f;
static constexpr int   kRotIdle = 90;    // Idle rotation offset
static constexpr int   kRotMin = 60;
static constexpr int   kRotMax = 120;
static constexpr int   kRotResetTicks = 200;   // Frames before auto-return

// Shot / explosion animation
static constexpr int   kShotStartX = 55;
static constexpr int   kShotTargetX = 512;   // Converge at center
static constexpr int   kShotSpeed = 8;
static constexpr int   kExplosionFrame = 55;    // Frame when explosion begins
static constexpr float kMaxScale = 40.0f; // Scale at which animation ends
static constexpr int   kExplosionGlyph = 129;   // Font slot for explosion graphic
static constexpr int   kExplosionBaseSize = 8;   // Starting size before growth kicks in

// Starfield
static constexpr int   kNumStars = 80;
static constexpr int   kMaxStarSpeed = 3;
static constexpr int   kBlinkChance = 10;   // ~1 in 10 stars will blink
static constexpr int   kBlinkRateMin = 8;    // Fastest blink (frames per half-cycle)
static constexpr int   kBlinkRateMax = 30;   // Slowest blink

// =============================================================================
// Starfield
// =============================================================================
static GLuint s_starVAO = 0;
static GLuint s_starVBO = 0;

struct StarVertex {
	GLfloat x, y;
	GLfloat r, g, b, a;
};

struct Star
{
	int x, y;
	int r, g, b;
	int speed;
	int blink;          // 0 = steady, 1 = blinks
	int blinkRate;      // Frames per half-cycle (on->off or off->on)
	int blinkTimer;     // Counts down each frame; toggles visibility at 0
	int blinkVisible;   // Current phase: 1 = visible, 0 = hidden
};

static Star s_stars[256];

static void fillStars(Star stars[], int count)
{
	for (int i = 0; i < count; ++i)
	{
		stars[i].y = rand() % kScreenH;
		stars[i].x = rand() % kScreenW;
		stars[i].speed = (rand() % kMaxStarSpeed) + 2;
		stars[i].r = rand() % 256;
		stars[i].g = rand() % 256;
		stars[i].b = rand() % 256;

		stars[i].blink = ((rand() % kBlinkChance) == 0) ? 1 : 0;
		if (stars[i].blink)
		{
			stars[i].blinkRate = kBlinkRateMin + (rand() % (kBlinkRateMax - kBlinkRateMin + 1));
			stars[i].blinkTimer = rand() % stars[i].blinkRate;  // Stagger start
			stars[i].blinkVisible = 1;
		}
		else
		{
			stars[i].blinkRate = 0;
			stars[i].blinkTimer = 0;
			stars[i].blinkVisible = 1;
		}
	}
}

static void moveStars(Star stars[], int count)
{
	for (int i = 0; i < count; ++i)
	{
		stars[i].y -= stars[i].speed;
		if (stars[i].y < 0)
			stars[i].y = kScreenH;

		// Tick blink timer
		if (stars[i].blink)
		{
			stars[i].blinkTimer--;
			if (stars[i].blinkTimer <= 0)
			{
				stars[i].blinkVisible = !stars[i].blinkVisible;
				stars[i].blinkTimer = stars[i].blinkRate;
			}
		}
	}
}

static void drawStars(Star stars[], int count)
{
	static StarVertex buf[256];
	int n = 0;

	for (int i = 0; i < count; ++i)
	{
		if (stars[i].blink && !stars[i].blinkVisible)
			continue;

		buf[n].x = (GLfloat)stars[i].x;
		buf[n].y = (GLfloat)stars[i].y;
		buf[n].r = stars[i].r / 255.0f;
		buf[n].g = stars[i].g / 255.0f;
		buf[n].b = stars[i].b / 255.0f;
		buf[n].a = 1.0f;
		n++;
	}

	if (n == 0) return;

	glPointSize(3.0f);
	bind_shader(fragStarPoint);

	glBindVertexArray(s_starVAO);
	glBindBuffer(GL_ARRAY_BUFFER, s_starVBO);
	glBufferSubData(GL_ARRAY_BUFFER, 0, n * sizeof(StarVertex), buf);
	glDrawArrays(GL_POINTS, 0, n);
	glBindVertexArray(0);

	unbind_shader();
	glPointSize(config.pointsize);
}

static void initStarGPU()
{
	glGenVertexArrays(1, &s_starVAO);
	glGenBuffers(1, &s_starVBO);

	glBindVertexArray(s_starVAO);
	glBindBuffer(GL_ARRAY_BUFFER, s_starVBO);
	glBufferData(GL_ARRAY_BUFFER, kNumStars * sizeof(StarVertex), nullptr, GL_DYNAMIC_DRAW);

	// Attribute 0: position (2 floats)
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(StarVertex), (void*)0);
	// Attribute 1: color (4 floats)
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(StarVertex), (void*)(2 * sizeof(float)));

	glBindVertexArray(0);
}

// =============================================================================
// GUI Input Ports
//   IN0: buttons (all IPF_SPULSE - single pulse per press)
//     bit0 = Select/Start (Button1 / Joy Fire1)
//     bit1 = Exit
//     bit2 = Menu (Button2)
//   IN1: navigation
//     bit0 = Up    (IPF_COUNTER - accelerating repeat for list scrolling)
//     bit1 = Down  (IPF_COUNTER - accelerating repeat for list scrolling)
//     bit2 = Left  (IPF_SPULSE  - single pulse for letter jumping)
//     bit3 = Right (IPF_SPULSE  - single pulse for letter jumping)
// =============================================================================

INPUT_PORTS_START(gui)
PORT_START("IN0")
PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_BUTTON1 | IPF_SPULSE)   // Start Game
PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_EXIT | IPF_SPULSE)   // Exit
PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_BUTTON2 | IPF_SPULSE)   // Menu
PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_BUTTON3 | IPF_SPULSE)
PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_BUTTON4 | IPF_SPULSE)
PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_BUTTON5 | IPF_SPULSE)
PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_BUTTON6 | IPF_SPULSE)
PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_UNUSED)
PORT_START("IN1")
// Removed IPF_COUNTER and IPF_SPULSE so these return raw continuous inputs
PORT_BITX(0x01, IP_ACTIVE_HIGH, IPT_JOYSTICK_UP | IPF_4WAY, "Up", OSD_KEY_UP, OSD_JOY_UP)
PORT_BITX(0x02, IP_ACTIVE_HIGH, IPT_JOYSTICK_DOWN | IPF_4WAY, "Down", OSD_KEY_DOWN, OSD_JOY_DOWN)
PORT_BITX(0x04, IP_ACTIVE_HIGH, IPT_JOYSTICK_LEFT | IPF_4WAY, "Left", OSD_KEY_LEFT, OSD_JOY_LEFT)
PORT_BITX(0x08, IP_ACTIVE_HIGH, IPT_JOYSTICK_RIGHT | IPF_4WAY, "Right", OSD_KEY_RIGHT, OSD_JOY_RIGHT)
PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_UNUSED)
PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_UNUSED)
PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_UNUSED)
PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_UNUSED)
INPUT_PORTS_END

// =============================================================================
// Samples
// =============================================================================
static const char* gui_samples[] =
{
	"aae.zip",
	"error.wav",     // 0
	"opening.wav",   // 1
	"key.wav",       // 2
	"idle.wav",      // 3
	"explode1.wav",  // 4
	"fire.wav",      // 5
};

// =============================================================================
// GUI State
// =============================================================================

// Game selection list (registry-backed, circular doubly-linked)
static GameList  s_gameList;
static const GameNode* s_selection = nullptr;
static int       s_guiDriverIndex = -1;  // Registry index of the GUI driver (to skip)
static int       s_lastPlayedGameNum = -1;  // Persists until program restart

// Ship glyph idle animation
static int   s_rotationCount = 0;
static int   s_rotLeft = kRotIdle;   // Left glyph rotation angle
static int   s_rotRight = -kRotIdle;  // Right glyph rotation angle

// Shot / game-launch animation
struct ShotState
{
	bool  active;
	int   targetGame;
	int   frame;
	int   shotX;
	float textRotation;
	float textScale;
	float explosionScale;
	int   fadeAlpha;
	bool  glyphsConverged;  // True once idle glyphs reach center

	void reset()
	{
		active = false;
		targetGame = 0;
		frame = 0;
		shotX = kShotStartX;
		textRotation = 0.0f;
		textScale = 1.0f;
		explosionScale = 1.0f;
		fadeAlpha = 255;
		glyphsConverged = false;
	}
};

static ShotState s_shot = {};

struct GuiInputState {
	bool start;
	bool prevItem;
	bool nextItem;
	bool prevLetter;
	bool nextLetter;
};

static GuiInputState s_in = {};

// =============================================================================
// Helpers
// =============================================================================

// Convert a hue (0-360) to an RGBA color at full saturation and value.
static rgb_t hueToRGBA(float hue, int alpha)
{
	// HSV->RGB with S=1, V=1
	float h = hue;
	while (h >= 360.0f) h -= 360.0f;
	while (h < 0.0f)    h += 360.0f;

	float c = 1.0f;
	float x = 1.0f - std::fabs(std::fmod(h / 60.0f, 2.0f) - 1.0f);
	float r1 = 0, g1 = 0, b1 = 0;

	if (h < 60) { r1 = c; g1 = x; }
	else if (h < 120) { r1 = x; g1 = c; }
	else if (h < 180) { g1 = c; b1 = x; }
	else if (h < 240) { g1 = x; b1 = c; }
	else if (h < 300) { r1 = x; b1 = c; }
	else { r1 = c; b1 = x; }

	return MAKE_RGBA((int)(r1 * 255), (int)(g1 * 255), (int)(b1 * 255), alpha);
}

// Title animation state (persists across frames).
static float s_titleHue = kTitleHueMin;
static float s_flashPos = -1.0f;  // < 0 = inactive; 0..1 = sweep position
static float s_flashSpeed = 0.0f;   // Normalized units per frame
static int   s_flashCooldown = 120;    // Frames until next flash is eligible

// Compute the base cycling hue color for this frame.
static rgb_t titleBaseColor()
{
	// Ping-pong hue between kTitleHueMin and kTitleHueMax
	s_titleHue += kTitleHueSpeed;
	float range = kTitleHueMax - kTitleHueMin;
	float pos = std::fmod(s_titleHue - kTitleHueMin, range * 2.0f);
	if (pos < 0.0f) pos += range * 2.0f;
	float hue = (pos <= range) ? (kTitleHueMin + pos)
		: (kTitleHueMax - (pos - range));
	return hueToRGBA(hue, 255);
}

// Draw the title with per-character color: base hue cycle + occasional flash sweep.
static void drawTitle()
{
	static const char* kTitleText = "ANOTHER ARCADE EMULATOR (AAE)";
	rgb_t baseColor = titleBaseColor();

	// --- Flash state machine -------------------------------------------------
	if (s_flashPos > -1.0f)
	{
		// Active flash: advance sweep position
		s_flashPos += s_flashSpeed;
		if (s_flashPos > 1.0f + kFlashWidth)
			s_flashPos = -1.0f;  // sweep complete
	}
	else
	{
		// Idle: count down, then random trigger
		s_flashCooldown--;
		if (s_flashCooldown <= 0)
		{
			s_flashPos = -kFlashWidth;   // start just off the left edge
			s_flashSpeed = 1.0f / (kFlashDuration * 60.0f);  // 60 fps
			s_flashCooldown = kFlashCooldownMin
				+ (rand() % (kFlashCooldownMax - kFlashCooldownMin + 1));
		}
	}

	// --- Center the title ----------------------------------------------------
	float totalWidth = VF.GetStringPitch(kTitleText, kTitleScale, 0);
	float startX = (kScreenW * 0.5f) - (totalWidth * 0.5f);

	// --- Render each character with individual color -------------------------
	float x = startX;
	int   len = (int)std::strlen(kTitleText);

	for (int i = 0; i < len; ++i)
	{
		char charStr[2] = { kTitleText[i], '\0' };
		float charWidth = VF.GetStringPitch(charStr, kTitleScale, 0);

		rgb_t color = baseColor;

		// If flash is active, blend toward flash color near the sweep position
		if (s_flashPos >= -kFlashWidth)
		{
			float charCenter = (totalWidth > 0.0f)
				? (x - startX + charWidth * 0.5f) / totalWidth
				: 0.0f;
			float dist = std::fabs(charCenter - s_flashPos);
			float intensity = 1.0f - (dist / kFlashWidth);
			if (intensity > 0.0f)
			{
				if (intensity > 1.0f) intensity = 1.0f;
				int br = RGB_RED(baseColor), fr = kFlashR;
				int bg = RGB_GREEN(baseColor), fg = kFlashG;
				int bb = RGB_BLUE(baseColor), fb = kFlashB;
				int r = br + (int)((float)(fr - br) * intensity);
				int g = bg + (int)((float)(fg - bg) * intensity);
				int b = bb + (int)((float)(fb - bb) * intensity);
				color = MAKE_RGBA(r, g, b, 255);
			}
		}

		VF.Print(x, kTitleY, color, kTitleScale, "%c", kTitleText[i]);
		x += charWidth;
	}
}

// Advance to the next node in direction, skipping the GUI driver entry.
static const GameNode* advanceSkippingGui(const GameNode* node, bool forward)
{
	if (!node) return nullptr;
	const GameNode* n = forward ? node->next : node->prev;
	if (s_guiDriverIndex >= 0 && n && n->gameNum == s_guiDriverIndex)
		n = forward ? n->next : n->prev;
	return n;
}

// Get the uppercase first letter of a node's display name ('\0' if empty).
static char nodeFirstLetter(const GameNode* n)
{
	if (!n || n->displayName.empty()) return '\0';
	return (char)std::toupper((unsigned char)n->displayName[0]);
}

// Clamp an int to [lo, hi].
static int clampInt(int val, int lo, int hi)
{
	if (val < lo) return lo;
	if (val > hi) return hi;
	return val;
}

// =============================================================================
// Ship Glyph Idle Animation
//
// The two ship glyphs on either side of the selection list rock back and
// forth slowly.  Pressing Up/Down tilts them in the corresponding direction.
// =============================================================================
static void updateGlyphAnimation()
{
	s_rotationCount++;
	if (s_rotationCount == kRotResetTicks)
	{
		s_rotLeft = kRotIdle;
		s_rotRight = -kRotIdle;
		s_rotationCount = 0;
	}

	// Drift back toward idle after the first 120 frames of a reset cycle
	if (s_rotationCount > 120)
	{
		if (s_rotLeft > kRotIdle)
		{
			s_rotLeft -= 2;  s_rotLeft = clampInt(s_rotLeft, kRotMin, kRotMax);
			s_rotRight += 2;  s_rotRight = clampInt(s_rotRight, -kRotMax, -kRotMin);
		}
		if (s_rotLeft < kRotIdle)
		{
			s_rotLeft += 2;  s_rotLeft = clampInt(s_rotLeft, kRotMin, kRotMax);
			s_rotRight -= 2;  s_rotRight = clampInt(s_rotRight, -kRotMax, -kRotMin);
		}
	}

	// Input tilts the glyphs
	if (s_in.nextItem)  // Was: if (readinputportbytag("IN1") & 0x02)
	{
		sample_start(2, 2, 0);
		s_rotLeft -= 2;  s_rotLeft = clampInt(s_rotLeft, kRotMin, kRotMax);
		s_rotRight += 2;  s_rotRight = clampInt(s_rotRight, -kRotMax, -kRotMin);
		s_rotationCount = 0;
	}

	if (s_in.prevItem)  // Was: if (readinputportbytag("IN1") & 0x01)
	{
		sample_start(2, 2, 0);
		s_rotLeft += 2;  s_rotLeft = clampInt(s_rotLeft, kRotMin, kRotMax);
		s_rotRight -= 2;  s_rotRight = clampInt(s_rotRight, -kRotMax, -kRotMin);
		s_rotationCount = 0;
	}

	// Select the "thrusting" ship glyph when tilted
	int texOffset = (s_rotLeft != kRotIdle) ? 1 : 0;
	int glyphId = 127 + texOffset;

	glColor4ub(255, 255, 255, 255);

	// VectorFont rotation convention: subtract 90 so 0 faces right
	VF.DrawGlyph((float)(kGlyphBaseX - texOffset * 4), (float)kGlyphY,
		glyphId, RGB_WHITE, kGlyphScale, (float)s_rotLeft - 90.0f);
	VF.DrawGlyph((float)(kGlyphRightX + texOffset * 4), (float)kGlyphY,
		glyphId, RGB_WHITE, kGlyphScale, (float)s_rotRight - 90.0f);
}

static void updateShotAnimation()
{
	if (!s_shot.active || !s_selection) return;

	// Draw the spinning selected title (in the current batch)
	VF.PrintCentered((int)(kSelectedY - (s_shot.textRotation * 8.0f)),
		RGB_YELLOW, s_shot.textScale * kSelectedScale,
		s_shot.textRotation, s_selection->description.c_str());

	// Force the idle glyphs into their converge animation
	s_rotationCount = kRotResetTicks;

	// Wait for glyphs to finish tilting before firing shots
	if (s_rotLeft != kRotIdle && !s_shot.glyphsConverged)
		return;
	s_shot.glyphsConverged = true;

	// Flush text batch, start a new one for the projectiles/explosions
	VF.End();
	VF.Begin();

	// Advance projectiles toward center
	if (s_shot.shotX < kShotTargetX)
	{
		VF.DrawGlyph((float)s_shot.shotX, (float)kGlyphY, '*', RGB_WHITE, 2.0f, s_shot.textRotation);
		VF.DrawGlyph((float)(kScreenW - s_shot.shotX), (float)kGlyphY, '*', RGB_WHITE, 2.0f, s_shot.textRotation);
		s_shot.shotX += kShotSpeed;
	}

	// Explosion phase
	if (s_shot.frame > kExplosionFrame)
	{
		s_shot.explosionScale += 0.5f;
		int blastSize = kExplosionBaseSize + (int)(s_shot.explosionScale * 1.0f);

		float explosionSpin = s_shot.explosionScale * 3.0f;

		if (s_shot.frame == kExplosionFrame + 1)
			sample_start(3, 4, 0);
		if (s_shot.frame == kExplosionFrame + 5)
			sample_start(4, 4, 0);

		// Two overlapping explosions with different color tints, rotating opposite
		int w = s_shot.fadeAlpha;
		rgb_t redTint = MAKE_RGBA(w, 0, 0, w);
		rgb_t cyanTint = MAKE_RGBA(0, w, w, w);
		VF.DrawGlyph(470, 360, kExplosionGlyph, redTint, (float)blastSize, explosionSpin);
		VF.DrawGlyph(520, 400, kExplosionGlyph, cyanTint, (float)blastSize, -explosionSpin);

		if (s_shot.frame > kExplosionFrame + 6)
		{
			s_shot.textRotation += 0.4f;
			s_shot.textScale += 0.5f;
			s_shot.fadeAlpha -= 4;
			if (s_shot.fadeAlpha < 0) s_shot.fadeAlpha = 0;
		}
	}

	// Animation complete - request game switch
	if (s_shot.textScale > kMaxScale)
	{
		const auto& reg = aae::AllDrivers();
		const char* nm = (s_shot.targetGame >= 0 &&
			s_shot.targetGame < (int)reg.size() &&
			reg[s_shot.targetGame] && reg[s_shot.targetGame]->name)
			? reg[s_shot.targetGame]->name : "INVALID";
		LOG_INFO("GUI: request switch -> %d (%s)", s_shot.targetGame, nm);

		s_lastPlayedGameNum = s_shot.targetGame;
		emulator_request_switch(s_shot.targetGame);
		s_shot.reset();
		return;
	}

	s_shot.frame++;

	// Sound effects at specific frames
	if (s_shot.frame == 2) sample_start(5, 5, 0);
	if (s_shot.frame == 4) sample_start(6, 5, 0);
}

// Implement letter-group jumping: skip to the next/previous entry whose first letter differs
//
static const GameNode* jumpToPrevLetterGroup(const GameNode* start)
{
	if (!start) return nullptr;
	const char curLetter = nodeFirstLetter(start);
	const GameNode* n = start->prev;

	while (n && n != start) {
		if (s_guiDriverIndex >= 0 && n->gameNum == s_guiDriverIndex) {
			n = n->prev; continue;
		}

		char letter = nodeFirstLetter(n);
		if (letter != curLetter) {
			// Found a different letter - walk to the first entry in that letter group.
			while (n->prev && n->prev != start) {
				if (s_guiDriverIndex >= 0 && n->prev->gameNum == s_guiDriverIndex) break;
				if (nodeFirstLetter(n->prev) != letter) break;
				n = n->prev;
			}
			break;
		}
		n = n->prev;
	}
	return n;
}

static const GameNode* jumpToNextLetterGroup(const GameNode* start)
{
	if (!start) return nullptr;
	const char curLetter = nodeFirstLetter(start);
	const GameNode* n = start->next;

	while (n && n != start) {
		if (s_guiDriverIndex >= 0 && n->gameNum == s_guiDriverIndex) {
			n = n->next; continue;
		}
		if (nodeFirstLetter(n) != curLetter) break;
		n = n->next;
	}
	return n;
}

// =============================================================================
// Game List Display
// =============================================================================
static void drawGameList()
{
	VF.Begin();

	// Animated title with color cycling and flash sweep
	drawTitle();

	if (!s_selection) { VF.End(); return; }

	// Draw filled highlight bar behind the selected title
	VF.DrawQuad((float)(kScreenW / 2), (float)kSelectedY + kSelBarYOffset,
		kSelBarWidth, kSelBarHeight, kSelBarColor);

	// Draw the selected title (only when not in shot animation - shot draws its own)
	if (!s_shot.active)
		VF.PrintCentered(kSelectedY, RGB_SOFTRED, kSelectedScale, 0, s_selection->description.c_str());

	// Draw surrounding entries above and below
	int visibleCount = 1;
	int offset = kLineSpacing;
	const GameNode* fwd = s_selection;
	const GameNode* bwd = s_selection;

	for (int row = 1; row < kVisibleRows + 1; ++row)
	{
		if ((int)s_gameList.size() > visibleCount + 1)
		{
			fwd = advanceSkippingGui(fwd, true);
			if (fwd)
				VF.PrintCentered(kSelectedY - offset, RGB_WHITE, kListScale, fwd->description.c_str());
			visibleCount++;
		}
		if ((int)s_gameList.size() > visibleCount)
		{
			bwd = advanceSkippingGui(bwd, false);
			if (bwd)
				VF.PrintCentered(kSelectedY + offset, RGB_WHITE, kListScale, bwd->description.c_str());
			visibleCount++;
		}
		offset += kLineSpacing;
	}

	VF.PrintCentered(kFooterY1, RGB_CYAN, kFooterScale, "Press Start 1 to Select Game");
	VF.PrintCentered(kFooterY2, RGB_CYAN, kFooterScale, "2026 TEST GUI BUILD - Press <TAB> for Menu");

	updateShotAnimation();
	VF.End();
}

// =============================================================================
// Input Handling & Logic
// =============================================================================

static void pollInput()
{
	s_in = {}; // Reset every frame

	if (get_menu_status() != 0 || get_exit_confirm_status() != 0)
	{
		input_cooldown = 10;
		return;
	}

	if (input_cooldown > 0) return;

	uint8_t in0 = readinputportbytag("IN0");
	uint8_t in1 = readinputportbytag("IN1");

	// 1. Get RAW hardware states (held = true)
	bool rawUp = (in1 & 0x01) != 0;
	bool rawDown = (in1 & 0x02) != 0;
	bool rawLeft = (in1 & 0x04) != 0;
	bool rawRight = (in1 & 0x08) != 0;

	// 2. Map the physical hardware to logical GUI actions
	bool logicPrevItem = config.flip_gui_controls ? rawLeft : rawUp;
	bool logicNextItem = config.flip_gui_controls ? rawRight : rawDown;
	bool logicPrevLetter = config.flip_gui_controls ? rawUp : rawLeft;
	bool logicNextLetter = config.flip_gui_controls ? rawDown : rawRight;

	// 3. Apply the timing effects (Repeat vs Single Pulse) using the API from inptport.h
	s_in.prevItem = Check_Input(JOY_INPUT_UP, logicPrevItem);
	s_in.nextItem = Check_Input(JOY_INPUT_DOWN, logicNextItem);
	s_in.prevLetter = KeyFlip(JOY_INPUT_LEFT, logicPrevLetter);
	s_in.nextLetter = KeyFlip(JOY_INPUT_RIGHT, logicNextLetter);

	// Start Game
	s_in.start = ((in0 & 0x01) != 0) || mouseb[1];
}

static void processLogic()
{
	if (s_in.start && s_selection && !s_shot.active)
	{
		s_shot.reset();
		s_shot.active = true;
		s_shot.targetGame = s_selection->gameNum;
	}

	if (s_in.prevItem)   s_selection = advanceSkippingGui(s_selection, false);
	if (s_in.nextItem)   s_selection = advanceSkippingGui(s_selection, true);
	if (s_in.prevLetter) s_selection = jumpToPrevLetterGroup(s_selection);
	if (s_in.nextLetter) s_selection = jumpToNextLetterGroup(s_selection);
}

// =============================================================================
// Public Interface (declared in driver_gui.h)
// =============================================================================

int init_gui()
{
	static int firstTime = 1;

	s_rotationCount = 0;

	LOG_INFO("STARTING GUI");
	glcode_vector_hard_clear_fbo1();

	if (firstTime)
	{
		sample_start(1, 1, 0);
		firstTime = 0;
	}

	fillStars(s_stars, kNumStars);
	initStarGPU();

	// Build selection list from the driver registry.
	{
		const auto& reg = aae::AllDrivers();
		s_guiDriverIndex = -1;
		for (int i = 0; i < (int)reg.size(); ++i)
		{
			if (reg[i] && reg[i]->name && std::strcmp(reg[i]->name, "gui") == 0)
			{
				s_guiDriverIndex = i;
				break;
			}
		}
		s_gameList.build(reg);
		s_selection = s_gameList.head();
		if (s_selection && s_guiDriverIndex >= 0 && s_selection->gameNum == s_guiDriverIndex)
			s_selection = s_selection->next;

		// Restore selection to the last-played game (persists until program restart).
		if (s_lastPlayedGameNum >= 0)
		{
			const GameNode* n = s_gameList.head();
			const GameNode* start = n;
			if (n) do
			{
				if (n->gameNum == s_lastPlayedGameNum)
				{
					s_selection = n;
					break;
				}
				n = n->next;
			} while (n && n != start);
		}
	}

	s_shot.reset();
	s_titleHue = kTitleHueMin;
	s_flashPos = -1.0f;
	s_flashCooldown = 120;

	sample_start(0, 3, 1);

	return 0;
}

void run_gui()
{
	cache_clear();
	vector_clear_list();

	moveStars(s_stars, kNumStars);
	drawStars(s_stars, kNumStars);

	if (input_cooldown) { input_cooldown--; }

	// 1. Poll the inputs and resolve flip settings
	pollInput();

	// 2. Apply logical actions to the menu
	processLogic();

	// 3. Update visuals (which safely consume the polled logical actions)
	updateGlyphAnimation();
	drawGameList();
}

void end_gui()
{
	LOG_INFO("EXITING GUI");
	sample_stop(1);
	if (s_starVAO) { glDeleteVertexArrays(1, &s_starVAO); s_starVAO = 0; }
	if (s_starVBO) { glDeleteBuffers(1, &s_starVBO);       s_starVBO = 0; }
}

// =============================================================================
// Driver Registration
// =============================================================================
AAE_DRIVER_BEGIN(gui, "gui", "AAE Frontend GUI")
AAE_DRIVER_ROM(0)
AAE_DRIVER_FUNCS(&init_gui, &run_gui, &end_gui)
AAE_DRIVER_INPUT(input_ports_gui)
AAE_DRIVER_SAMPLES(gui_samples)
AAE_DRIVER_ART_NONE()

AAE_DRIVER_CPUS(
	AAE_CPU_ENTRY(
		/*type*/     CPU_NONE,
		/*freq*/     1,
		/*div*/      1,
		/*ipf*/      1,
		/*int type*/ INT_TYPE_NONE,
		/*int cb*/   0,
		/*r8*/       0,
		/*w8*/       0,
		/*pr*/       nullptr,
		/*pw*/       nullptr,
		/*r16*/      nullptr,
		/*w16*/      nullptr
	),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(60,DEFAULT_60HZ_VBLANK_DURATION, VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(1024, 768, 0, 1024, 0, 768)
AAE_DRIVER_RASTER_NONE()
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_LAYOUT_NONE()
AAE_DRIVER_END()

AAE_REGISTER_DRIVER(gui)