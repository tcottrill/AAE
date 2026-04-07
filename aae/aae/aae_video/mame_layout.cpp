// ====================================================================
// mame_layout.cpp - MAME .lay file parser and OpenGL renderer
//
// Rendering uses GLSL 330 compatibility shaders with explicit VAO/VBO
// vertex submission. 
//
// Dependencies:
//   - tinyxml2 (XML parsing)
//   - stb_image (artwork PNG loading)
//   - shader_util.h (CompileShader / LinkShaderProgram)
//   - framework.h (GLEW, GL types)
//   - sys_log.h (logging)
// ====================================================================

#include "mame_layout.h"
#include "shader_util.h"    // CompileShader(), LinkShaderProgram()
#include "sys_log.h"
#include "tinyxml2.h"
#include "config.h"
#include "aae_fileio.h"
#include "aae_mame_driver.h"  // VIDEO_TYPE_RASTER_BW, VIDEO_TYPE_RASTER_COLOR
#include "path_helper.h"       // getpathM()
#include "menu.h"
#include "stb_image.h"

#include <algorithm>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <filesystem>

using namespace tinyxml2;

// ====================================================================
// Globals
// ====================================================================
bool        g_layoutEnabled = false;
LayoutData  g_layoutData;
LayoutView* g_activeView = nullptr;
float       g_layoutAspect = 1.0f;
bool        g_layoutShowBezel = true;
bool        g_layoutShowOverlay = true;
bool        g_layoutShowBackdrop = true;
bool        g_layoutZoomToScreen = false;

// ====================================================================
// Layout Shader State (module-internal)
// ====================================================================
static GLuint s_layoutVAO = 0;
static GLuint s_layoutVBO = 0;
static GLuint s_singleTexShader = 0;  // backdrop, bezel, screen-no-overlay
static GLuint s_dualTexShader = 0;  // screen * overlay multiply
static GLint  s_st_uAlpha = -1;  // single-tex alpha uniform
static GLint  s_dt_uAlpha = -1;  // dual-tex alpha uniform
static GLint  s_dt_uOverlayMode = -1;  // dual-tex overlay mode (0=pure multiply, 1=2x multiply)
static GLint  s_dt_uOverlayUVXform = -1; // dual-tex overlay UV transform (scaleU, scaleV, offsetU, offsetV)
static bool   s_layoutShadersReady = false;

// Quad vertex: position (NDC) + texcoord
struct LayoutQuadVertex {
	float px, py;   // position in NDC
	float tx, ty;   // texcoord
};

// ====================================================================
// Shader Init (called lazily on first Layout_Render)
//
// These shaders use explicit attribute locations and do not touch
// any fixed-function GL state, so they coexist safely with the
// existing compatibility-mode pipeline used by the vector renderer.
// ====================================================================
static void InitLayoutShaders()
{
	if (s_layoutShadersReady) return;

	// ---- Shared vertex shader source ----
	// Passes position and texcoord through. No matrix transforms --
	// positions are computed as NDC on the CPU side.
	const char* vsSrc = R"glsl(
	#version 330 compatibility
	layout(location = 0) in vec2 inPos;
	layout(location = 1) in vec2 inTex;
	out vec2 TexCoord;
	void main() {
		gl_Position = vec4(inPos, 0.0, 1.0);
		TexCoord = inTex;
	}
	)glsl";

	// ---- Single-texture fragment shader ----
	// Samples one texture, multiplied by an alpha uniform.
	// Used for backdrops, bezels, and screens without an overlay.
	const char* fsSingleSrc = R"glsl(
	#version 330 compatibility
	in vec2 TexCoord;
	out vec4 FragColor;
	uniform sampler2D tex0;
	uniform float uAlpha;
	void main() {
		vec4 c = texture(tex0, TexCoord);
		FragColor = vec4(c.rgb, c.a * uAlpha);
	}
	)glsl";

	// ---- Dual-texture fragment shader ----
	// Multiplies screen (tex0) by overlay (tex1) with alpha.
	// This gives the color-gel effect seen on games like Space Invaders.
	//
	// u_overlayMode selects the multiply variant to match the blend modes
	// used by final_render_raster's FBO-side overlay compositing:
	//   mode 0 (BW):    pure multiply   = screen * overlay
	//                    (matches GL_DST_COLOR, GL_ZERO)
	//   mode 1 (color): 2x multiply     = min(screen * overlay * 2, 1)
	//                    (matches GL_DST_COLOR, GL_SRC_COLOR)
	//
	// u_overlayUVXform maps screen-space UVs to overlay-space UVs.
	// The overlay may cover only part of the screen (e.g. Clowns balloon
	// strips). Areas outside the overlay return white (multiply identity)
	// so they are unaffected.
	const char* fsDualSrc = R"glsl(
	#version 330 compatibility
	in vec2 TexCoord;
	out vec4 FragColor;
	uniform sampler2D tex0;
	uniform sampler2D tex1;
	uniform float uAlpha;
	uniform int u_overlayMode;
	uniform vec4 u_overlayUVXform;  // (scaleU, scaleV, offsetU, offsetV)
	void main() {
		vec4 screen = texture(tex0, TexCoord);
		vec2 ovUV = TexCoord * u_overlayUVXform.xy + u_overlayUVXform.zw;
		vec3 ovColor;
		if (ovUV.x < 0.0 || ovUV.x > 1.0 || ovUV.y < 0.0 || ovUV.y > 1.0)
			ovColor = vec3(1.0);  // outside overlay = white = multiply identity
		else
			ovColor = texture(tex1, ovUV).rgb;
		vec3 result;
		if (u_overlayMode == 1)
			result = min(screen.rgb * ovColor * 2.0, vec3(1.0));
		else
			result = screen.rgb * ovColor;
		FragColor = vec4(result, screen.a * uAlpha);
	}
	)glsl";
	// ---- Compile single-texture program ----
	{
		GLuint vs = CompileShader(GL_VERTEX_SHADER, vsSrc, "Layout_SingleTex_VS");
		GLuint fs = CompileShader(GL_FRAGMENT_SHADER, fsSingleSrc, "Layout_SingleTex_FS");
		s_singleTexShader = LinkShaderProgram(vs, fs);
		// LinkShaderProgram deletes vs/fs after linking
	}
	if (s_singleTexShader == 0) {
		LOG_ERROR("InitLayoutShaders: Failed to create single-tex shader");
		return;
	}
	s_st_uAlpha = glGetUniformLocation(s_singleTexShader, "uAlpha");

	// Set tex0 sampler to texture unit 0
	glUseProgram(s_singleTexShader);
	glUniform1i(glGetUniformLocation(s_singleTexShader, "tex0"), 0);
	glUseProgram(0);

	// ---- Compile dual-texture program ----
	{
		GLuint vs = CompileShader(GL_VERTEX_SHADER, vsSrc, "Layout_DualTex_VS");
		GLuint fs = CompileShader(GL_FRAGMENT_SHADER, fsDualSrc, "Layout_DualTex_FS");
		s_dualTexShader = LinkShaderProgram(vs, fs);
	}
	if (s_dualTexShader == 0) {
		LOG_ERROR("InitLayoutShaders: Failed to create dual-tex shader");
		return;
	}
	s_dt_uAlpha = glGetUniformLocation(s_dualTexShader, "uAlpha");
	s_dt_uOverlayMode = glGetUniformLocation(s_dualTexShader, "u_overlayMode");
	s_dt_uOverlayUVXform = glGetUniformLocation(s_dualTexShader, "u_overlayUVXform");

	// Set tex0 to unit 0, tex1 to unit 1
	glUseProgram(s_dualTexShader);
	glUniform1i(glGetUniformLocation(s_dualTexShader, "tex0"), 0);
	glUniform1i(glGetUniformLocation(s_dualTexShader, "tex1"), 1);
	glUseProgram(0);

	// ---- Create shared VAO/VBO for dynamic quads ----
	glGenVertexArrays(1, &s_layoutVAO);
	glGenBuffers(1, &s_layoutVBO);

	glBindVertexArray(s_layoutVAO);
	glBindBuffer(GL_ARRAY_BUFFER, s_layoutVBO);
	// Allocate for 4 vertices, will be updated each draw call
	glBufferData(GL_ARRAY_BUFFER, 4 * sizeof(LayoutQuadVertex), nullptr, GL_DYNAMIC_DRAW);

	// Attribute 0: position (2 floats)
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(LayoutQuadVertex), (void*)0);
	// Attribute 1: texcoord (2 floats)
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(LayoutQuadVertex), (void*)(2 * sizeof(float)));

	glBindVertexArray(0);

	s_layoutShadersReady = true;
	LOG_INFO("Layout shaders initialized (GLSL 330 compatibility)");
}

// ====================================================================
// Helpers
// ====================================================================

static LayerType LayerTypeFromTag(const char* tag)
{
	if (!tag) return LayerType::Bezel;
	if (strcmp(tag, "backdrop") == 0) return LayerType::Backdrop;
	if (strcmp(tag, "screen") == 0) return LayerType::Screen;
	if (strcmp(tag, "overlay") == 0) return LayerType::Overlay;
	return LayerType::Bezel;  // "bezel" or anything unrecognized
}

// Sort priority for layer compositing order
static int LayerOrder(LayerType t)
{
	switch (t) {
	case LayerType::Backdrop: return 0;
	case LayerType::Screen:   return 1;
	case LayerType::Overlay:  return 2;
	case LayerType::Bezel:    return 3;
	}
	return 3;
}

// ====================================================================
// Bake a procedural element (colored rects) into an RGBA texture.
// The element's rects use an element-local coordinate system.
// We determine the bounding box from the rects and rasterize at 1:1.
// ====================================================================
static GLuint BakeProceduralTexture(LayoutElement& elem)
{
	if (elem.rects.empty()) return 0;

	// Find bounding box of all rects
	float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
	for (auto& r : elem.rects) {
		minX = ((std::min))(minX, r.x);
		minY = (std::min)(minY, r.y);
		maxX = (std::max)(maxX, r.x + r.w);
		maxY = (std::max)(maxY, r.y + r.h);
	}

	int texW = (int)std::ceil(maxX - minX);
	int texH = (int)std::ceil(maxY - minY);
	if (texW < 1) texW = 1;
	if (texH < 1) texH = 1;

	// Cap texture size to something reasonable
	if (texW > 2048) texW = 2048;
	if (texH > 2048) texH = 2048;

	// Allocate RGBA buffer, initialize to transparent black
	std::vector<uint8_t> pixels(texW * texH * 4, 0);

	// Rasterize each rect (painter's algorithm - later rects overwrite earlier)
	for (auto& r : elem.rects) {
		int rx0 = (int)(r.x - minX);
		int ry0 = (int)(r.y - minY);
		int rx1 = (int)(r.x + r.w - minX);
		int ry1 = (int)(r.y + r.h - minY);
		rx0 = (std::max)(0, (std::min)(rx0, texW));
		ry0 = (std::max)(0, (std::min)(ry0, texH));
		rx1 = (std::max)(0, (std::min)(rx1, texW));
		ry1 = (std::max)(0, (std::min)(ry1, texH));

		uint8_t cr = (uint8_t)(r.r * 255.0f);
		uint8_t cg = (uint8_t)(r.g * 255.0f);
		uint8_t cb = (uint8_t)(r.b * 255.0f);
		uint8_t ca = (uint8_t)(r.a * 255.0f);

		for (int y = ry0; y < ry1; y++) {
			for (int x = rx0; x < rx1; x++) {
				int idx = (y * texW + x) * 4;
				pixels[idx + 0] = cr;
				pixels[idx + 1] = cg;
				pixels[idx + 2] = cb;
				pixels[idx + 3] = ca;
			}
		}
	}

	GLuint tex;
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texW, texH, 0,
		GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	elem.texWidth = texW;
	elem.texHeight = texH;

	LOG_INFO("Baked procedural texture '%s': %dx%d", elem.name.c_str(), texW, texH);
	return tex;
}

// ====================================================================
// Parsing
// ====================================================================

bool Layout_Parse(const std::string& layFilename, const std::string& zipFile,
	const std::string& artworkDir, LayoutData& outData)
{
	outData.elements.clear();
	outData.views.clear();

	tinyxml2::XMLDocument doc;

	if (!zipFile.empty())
	{
		// Load .lay from ZIP archive
		unsigned char* data = load_zip_file(zipFile.c_str(), layFilename.c_str());
		if (!data) {
			LOG_ERROR("Layout_Parse: '%s' not found in '%s'", layFilename.c_str(), zipFile.c_str());
			return false;
		}
		size_t size = get_last_zip_file_size();
		XMLError err = doc.Parse((const char*)data, size);
		free(data);

		if (err != XML_SUCCESS) {
			LOG_ERROR("Layout_Parse: Failed to parse '%s' from '%s'", layFilename.c_str(), zipFile.c_str());
			return false;
		}
		LOG_INFO("Layout_Parse: Loaded '%s' from ZIP '%s' (%zu bytes)", layFilename.c_str(), zipFile.c_str(), size);
	}
	else
	{
		// Load .lay from filesystem (loose file)
		std::string fullPath = artworkDir + "\\" + layFilename;
		if (doc.LoadFile(fullPath.c_str()) != XML_SUCCESS) {
			LOG_ERROR("Layout_Parse: Failed to load '%s'", fullPath.c_str());
			return false;
		}
	}

	XMLElement* root = doc.FirstChildElement("mamelayout");
	if (!root) {
		LOG_ERROR("Layout_Parse: No <mamelayout> root element in '%s'", layFilename.c_str());
		return false;
	}

	// ---- Parse <element> definitions ----
	for (XMLElement* el = root->FirstChildElement("element"); el;
		el = el->NextSiblingElement("element"))
	{
		const char* name = el->Attribute("name");
		if (!name) continue;

		LayoutElement elem;
		elem.name = name;

		// Check for <image file="..."/>
		XMLElement* img = el->FirstChildElement("image");
		if (img && img->Attribute("file")) {
			elem.imageFile = img->Attribute("file");
		}

		// Check for <rect> children (procedural element)
		for (XMLElement* rectEl = el->FirstChildElement("rect"); rectEl;
			rectEl = rectEl->NextSiblingElement("rect"))
		{
			LayoutRect lr;
			lr.x = 0; lr.y = 0; lr.w = 1; lr.h = 1;
			lr.r = 1; lr.g = 1; lr.b = 1; lr.a = 1;

			XMLElement* bounds = rectEl->FirstChildElement("bounds");
			if (bounds) {
				// Support both left/top/right/bottom and x/y/width/height
				if (bounds->Attribute("left")) {
					lr.x = bounds->FloatAttribute("left", 0);
					lr.y = bounds->FloatAttribute("top", 0);
					float right = bounds->FloatAttribute("right", lr.x + 1);
					float bottom = bounds->FloatAttribute("bottom", lr.y + 1);
					lr.w = right - lr.x;
					lr.h = bottom - lr.y;
				}
				else {
					lr.x = bounds->FloatAttribute("x", 0);
					lr.y = bounds->FloatAttribute("y", 0);
					lr.w = bounds->FloatAttribute("width", 1);
					lr.h = bounds->FloatAttribute("height", 1);
				}
			}

			XMLElement* color = rectEl->FirstChildElement("color");
			if (color) {
				lr.r = color->FloatAttribute("red", 1.0f);
				lr.g = color->FloatAttribute("green", 1.0f);
				lr.b = color->FloatAttribute("blue", 1.0f);
				lr.a = color->FloatAttribute("alpha", 1.0f);
			}

			elem.rects.push_back(lr);
		}

		outData.elements[name] = elem;
	}

	// ---- Parse <view> definitions ----
	for (XMLElement* viewEl = root->FirstChildElement("view"); viewEl;
		viewEl = viewEl->NextSiblingElement("view"))
	{
		const char* viewName = viewEl->Attribute("name");
		if (!viewName) continue;

		LayoutView view;
		view.name = viewName;

		float minX = 1e9f, minY = 1e9f;
		float maxX = -1e9f, maxY = -1e9f;

		for (XMLElement* child = viewEl->FirstChildElement(); child;
			child = child->NextSiblingElement())
		{
			const char* tag = child->Name();
			LayerType layer = LayerTypeFromTag(tag);

			LayoutDrawable drawable;
			drawable.layer = layer;
			drawable.element = nullptr;
			drawable.x = 0; drawable.y = 0; drawable.w = 0; drawable.h = 0;
			drawable.alpha = 1.0f;
			drawable.screenIndex = 0;

			// Read bounds
			XMLElement* bounds = child->FirstChildElement("bounds");
			if (bounds) {
				drawable.x = bounds->FloatAttribute("x", 0);
				drawable.y = bounds->FloatAttribute("y", 0);
				drawable.w = bounds->FloatAttribute("width", 0);
				drawable.h = bounds->FloatAttribute("height", 0);
			}

			// Read alpha from <color alpha="...">
			XMLElement* color = child->FirstChildElement("color");
			if (color) {
				drawable.alpha = color->FloatAttribute("alpha", 1.0f);
			}

			// Screen entry -- record screen bounds in the view
			if (layer == LayerType::Screen) {
				drawable.screenIndex = child->IntAttribute("index", 0);
				view.screenX = drawable.x;
				view.screenY = drawable.y;
				view.screenW = drawable.w;
				view.screenH = drawable.h;
			}

			// Element reference -- link to the parsed element definition
			const char* elemName = child->Attribute("element");
			if (elemName) {
				auto it = outData.elements.find(elemName);
				if (it != outData.elements.end()) {
					drawable.element = &it->second;
				}
				else {
					LOG_WARN("Layout_Parse: View '%s' references unknown element '%s'",
						viewName, elemName);
				}
			}

			// Update the view bounding box
			if (drawable.w > 0 && drawable.h > 0) {
				minX = (std::min)(minX, drawable.x);
				minY = (std::min)(minY, drawable.y);
				maxX = (std::max)(maxX, drawable.x + drawable.w);
				maxY = (std::max)(maxY, drawable.y + drawable.h);
			}

			view.drawables.push_back(drawable);
		}

		// Store the computed bounding box
		if (minX < maxX && minY < maxY) {
			view.boundsX = minX;
			view.boundsY = minY;
			view.boundsW = maxX - minX;
			view.boundsH = maxY - minY;
		}

		// Sort drawables by layer order (stable sort preserves XML order within a layer)
		std::stable_sort(view.drawables.begin(), view.drawables.end(),
			[](const LayoutDrawable& a, const LayoutDrawable& b) {
				return LayerOrder(a.layer) < LayerOrder(b.layer);
			});

		outData.views.push_back(view);
		LOG_INFO("Layout_Parse: View '%s' - bounds (%.0f,%.0f %.0fx%.0f) screen (%.0f,%.0f %.0fx%.0f)",
			viewName, view.boundsX, view.boundsY, view.boundsW, view.boundsH,
			view.screenX, view.screenY, view.screenW, view.screenH);
	}

	//LOG_INFO("Layout_Parse: Loaded %d elements, %d views from '%s'", (int)outData.elements.size(), (int)outData.views.size(), layFile.c_str());
	return true;
}

// ====================================================================
// Texture Loading
// ====================================================================

bool Layout_LoadTextures(LayoutData& data, const std::string& zipFile,
	const std::string& artworkDir)
{
	for (auto& pair : data.elements) {
		LayoutElement& elem = pair.second;

		if (elem.textureID != 0) continue;  // already loaded

		if (!elem.imageFile.empty()) {
			int w, h, channels;
			unsigned char* imgData = nullptr;

			// Try loading from ZIP first, then fall back to loose file
			if (!zipFile.empty())
			{
				unsigned char* zipData = load_zip_file(zipFile.c_str(), elem.imageFile.c_str());
				if (zipData) {
					size_t zipSize = get_last_zip_file_size();
					stbi_set_flip_vertically_on_load(0);
					imgData = stbi_load_from_memory(zipData, (int)zipSize, &w, &h, &channels, 4);
					free(zipData);
				}
			}

			// Fallback: try loose file in artwork directory
			if (!imgData) {
				std::string fullPath = artworkDir + "\\" + elem.imageFile;
				stbi_set_flip_vertically_on_load(0);
				imgData = stbi_load(fullPath.c_str(), &w, &h, &channels, 4);
			}

			if (!imgData) {
				LOG_WARN("Layout_LoadTextures: Failed to load '%s'", elem.imageFile.c_str());
				continue;
			}

			GLuint tex;
			glGenTextures(1, &tex);
			glBindTexture(GL_TEXTURE_2D, tex);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
				GL_RGBA, GL_UNSIGNED_BYTE, imgData);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			stbi_image_free(imgData);

			elem.textureID = tex;
			elem.texWidth = w;
			elem.texHeight = h;
			LOG_INFO("Layout_LoadTextures: Loaded '%s' (%dx%d) -> tex %u",
				elem.imageFile.c_str(), w, h, tex);
		}
		else if (!elem.rects.empty()) {
			elem.textureID = BakeProceduralTexture(elem);
		}
	}

	return true;
}

// ====================================================================
// View Lookup
// ====================================================================

LayoutView* Layout_FindView(LayoutData& data, const std::string& viewName)
{
	for (auto& v : data.views) {
		if (v.name == viewName) return &v;
	}
	// If exact match not found, try the first view as a fallback
	if (!data.views.empty()) {
		LOG_WARN("Layout_FindView: '%s' not found, falling back to first view '%s'",
			viewName.c_str(), data.views[0].name.c_str());
		return &data.views[0];
	}
	return nullptr;
}

// ====================================================================
// Rendering
// ====================================================================

// Upload a quad and draw it using the currently bound shader.
// Coordinates are in NDC (-1 to +1).
static void DrawQuadNDC(float nx0, float ny0, float nx1, float ny1)
{
	LayoutQuadVertex verts[4] = {
		{ nx0, ny0,  0.0f, 0.0f },   // top-left
		{ nx1, ny0,  1.0f, 0.0f },   // top-right
		{ nx1, ny1,  1.0f, 1.0f },   // bottom-right
		{ nx0, ny1,  0.0f, 1.0f }    // bottom-left
	};

	glBindBuffer(GL_ARRAY_BUFFER, s_layoutVBO);
	glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
}

// V-flipped variant for FBO textures rendered with Y-down ortho (e.g. img5a).
// In such textures the top row of the image is at V=1 and the bottom at V=0,
// so we swap the V coordinates to display the image right-side-up.
static void DrawQuadNDC_FlipV(float nx0, float ny0, float nx1, float ny1)
{
	LayoutQuadVertex verts[4] = {
		{ nx0, ny0,  0.0f, 1.0f },   // top-left  (V flipped)
		{ nx1, ny0,  1.0f, 1.0f },   // top-right (V flipped)
		{ nx1, ny1,  1.0f, 0.0f },   // bottom-right (V flipped)
		{ nx0, ny1,  0.0f, 0.0f }    // bottom-left  (V flipped)
	};

	glBindBuffer(GL_ARRAY_BUFFER, s_layoutVBO);
	glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
}

// Rotated-UV variant for system rotation.
// The quad geometry stays axis-aligned in NDC (landscape rectangle), but the
// texture coordinates are rotated so the portrait FBO content appears rotated
// on screen. This is equivalent to rotating the entire scene.
//
// sysRot is config.system_rotation (ORIENTATION_xxx flags).
// ROT90  (SWAP_XY|FLIP_X): CW 90  - UVs: TL(0,1) TR(0,0) BR(1,0) BL(1,1)
// ROT270 (SWAP_XY|FLIP_Y): CCW 90 - UVs: TL(1,0) TR(1,1) BR(0,1) BL(0,0)
static void DrawQuadNDC_SysRot(float nx0, float ny0, float nx1, float ny1, int sysRot)
{
	// Default UVs (no rotation) - same as DrawQuadNDC
	float tl_u = 0.0f, tl_v = 0.0f;  // top-left
	float tr_u = 1.0f, tr_v = 0.0f;  // top-right
	float br_u = 1.0f, br_v = 1.0f;  // bottom-right
	float bl_u = 0.0f, bl_v = 1.0f;  // bottom-left

	if (sysRot & ORIENTATION_SWAP_XY)
	{
		if (sysRot & ORIENTATION_FLIP_X)
		{
			// ROT90 (CW 90): left edge of texture goes to top of screen
			tl_u = 0.0f; tl_v = 1.0f;
			tr_u = 0.0f; tr_v = 0.0f;
			br_u = 1.0f; br_v = 0.0f;
			bl_u = 1.0f; bl_v = 1.0f;
		}
		else // FLIP_Y (ROT270, CCW 90)
		{
			// ROT270 (CCW 90): right edge of texture goes to top of screen
			tl_u = 1.0f; tl_v = 0.0f;
			tr_u = 1.0f; tr_v = 1.0f;
			br_u = 0.0f; br_v = 1.0f;
			bl_u = 0.0f; bl_v = 0.0f;
		}
	}

	LayoutQuadVertex verts[4] = {
		{ nx0, ny0,  tl_u, tl_v },   // top-left
		{ nx1, ny0,  tr_u, tr_v },   // top-right
		{ nx1, ny1,  br_u, br_v },   // bottom-right
		{ nx0, ny1,  bl_u, bl_v }    // bottom-left
	};

	glBindBuffer(GL_ARRAY_BUFFER, s_layoutVBO);
	glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
}

// Convert layout coordinates to NDC given the current camera transform.
// scale/offsetX/offsetY map layout-space to pixel-space,
// winW/winH convert pixel-space to NDC.
static void LayoutToNDC(float x, float y, float w, float h,
	float scale, float offsetX, float offsetY,
	float winW, float winH,
	float& nx0, float& ny0, float& nx1, float& ny1)
{
	float x0 = offsetX + x * scale;
	float y0 = offsetY + y * scale;
	float x1 = offsetX + (x + w) * scale;
	float y1 = offsetY + (y + h) * scale;

	// Convert pixel coords to NDC: x -> [-1, +1], y -> [+1, -1] (top-down)
	nx0 = (x0 / winW) * 2.0f - 1.0f;
	ny0 = 1.0f - (y0 / winH) * 2.0f;
	nx1 = (x1 / winW) * 2.0f - 1.0f;
	ny1 = 1.0f - (y1 / winH) * 2.0f;
}

// Draw a single-textured quad at layout position with alpha
static void DrawLayoutQuad(GLuint texID, float x, float y, float w, float h,
	float alpha, float scale, float offsetX, float offsetY,
	float winW, float winH)
{
	if (texID == 0 && alpha <= 0.0f) return;

	float nx0, ny0, nx1, ny1;
	LayoutToNDC(x, y, w, h, scale, offsetX, offsetY, winW, winH,
		nx0, ny0, nx1, ny1);

	glUseProgram(s_singleTexShader);
	glUniform1f(s_st_uAlpha, alpha);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, texID);

	DrawQuadNDC(nx0, ny0, nx1, ny1);
}

void Layout_Render(const LayoutView& view, GLuint screenTexture, int winW, int winH, int videoAttributes)
{
	if (winW < 1 || winH < 1) return;

	// Sync layout visibility flags from the main config settings.
   // The menu system controls config.artwork/overlay/bezel, and the
   // layout renderer uses g_layoutShow* flags. Keep them in sync
   // so the player's menu choices take effect immediately.
	g_layoutShowBackdrop = (config.artwork != 0);
	g_layoutShowOverlay = (config.overlay != 0);
	g_layoutShowBezel = (config.bezel != 0);

	// Lazy-init shaders on first call
	if (!s_layoutShadersReady) {
		InitLayoutShaders();
		if (!s_layoutShadersReady) return;  // shader init failed
	}

	// ---- System Rotation Detection ----
	// When config.system_rotation has SWAP_XY, the entire display is rotated
	// 90 or 270 degrees. In this mode:
	//   - Bezel is forced OFF (it doesn't compose well rotated)
	//   - Screen aspect inverts (portrait -> landscape or vice versa)
	//   - Backdrop uses "cover fill" to fill the rotated window
	//   - Screen quad uses rotated UV mapping
	const int sysRot = config.system_rotation;
	const bool sysRotated = (sysRot & ORIENTATION_SWAP_XY) != 0;

	// ---- Viewport / Camera Calculation ----
	  // Determine which region of the layout to show.
	  //
	  // Zoom to the screen area when:
	  //   - The user explicitly enabled zoom-to-screen mode
	  //   - Crop bezel is enabled (config.artcrop)
	  //   - Both backdrop and bezel are hidden (only screen/overlay visible)
	  //   - System rotation is active (bezel forced off)
	  //
	  // When zoomed to screen, the game fills the window and any visible
	  // overlay is stretched to match the screen bounds. When showing the
	  // full layout, the game sits within the artwork at its defined position.
	bool zoomToScreen = g_layoutZoomToScreen
		|| (config.artcrop != 0)
		|| (!g_layoutShowBezel)
		|| sysRotated;

	float camX, camY, camW, camH;
	if (zoomToScreen && view.screenW > 0 && view.screenH > 0) {
		camX = view.screenX; camY = view.screenY;
		camW = view.screenW; camH = view.screenH;
	}
	else {
		camX = view.boundsX; camY = view.boundsY;
		camW = view.boundsW; camH = view.boundsH;
	}
	if (camW <= 0 || camH <= 0) return;

	// For system rotation with SWAP_XY, the camera aspect is inverted.
	// A portrait layout (3:4) becomes landscape (4:3) in the window.
	float aspectCam = camW / camH;
	if (sysRotated) aspectCam = camH / camW;

	// ---- Aspect Override: constrain rendering area ----
	// When the user has an aspect override active (use_aspect=1 or -aspect),
	// compute a sub-rectangle of the window that has the override aspect.
	// The layout renders into this constrained area; black bars from the
	// glClear in final_render_raster fill the remainder.
	//
	// In windowed mode the window was already resized to match, so
	// vpW==winW and vpH==winH (no visible letterboxing here).
	// In fullscreen the window is the monitor's native shape, so this
	// creates the correct pillarbox/letterbox.
	int vpX = 0, vpY = 0, vpW = winW, vpH = winH;
	{
		auto& ws = GetWindowSetup();
		if (ws.aspectOverrideActive && ws.aspectRatio > 0.0f)
		{
			float windowAspect = (float)winW / (float)winH;
			if (windowAspect > ws.aspectRatio)
			{
				// Window wider than target -- pillarbox
				vpW = (int)((float)winH * ws.aspectRatio + 0.5f);
				vpX = (winW - vpW) / 2;
			}
			else if (windowAspect < ws.aspectRatio)
			{
				// Window taller than target -- letterbox
				vpH = (int)((float)winW / ws.aspectRatio + 0.5f);
				vpY = (winH - vpH) / 2;
			}
			// Replace the effective window dimensions for all NDC math below.
			winW = vpW;
			winH = vpH;
		}
	}
	float aspectWin = (float)winW / (float)winH;

	// Compute scale and offset to fit the camera region into the window
	// while maintaining the (possibly inverted) aspect ratio.
	float scale, offsetX, offsetY;

	if (sysRotated)
	{
		// Rotated path: the screen area (camW x camH, portrait) is being
		// displayed as landscape (camH x camW). Fit the rotated dimensions
		// into the window.
		float rotW = camH;  // what was height is now width
		float rotH = camW;  // what was width is now height

		if (aspectWin > aspectCam) {
			// Window wider than rotated layout - pillarbox
			scale = (float)winH / rotH;
		}
		else {
			// Window taller than rotated layout - letterbox
			scale = (float)winW / rotW;
		}

		// Center the rotated screen in the window.
		// The screen quad NDC is computed directly below, not via LayoutToNDC.
		offsetX = 0;
		offsetY = 0;
	}
	else
	{
		// Normal (non-rotated) path - original letterbox/pillarbox logic.
		if (aspectWin > aspectCam) {
			// Window is wider than layout -- pillarbox (black bars on sides)
			scale = (float)winH / camH;
			offsetX = (winW - (camW * scale)) / 2.0f - camX * scale;
			offsetY = -camY * scale;
		}
		else {
			// Window is taller than layout -- letterbox (black bars top/bottom)
			scale = (float)winW / camW;
			offsetX = -camX * scale;
			offsetY = (winH - (camH * scale)) / 2.0f - camY * scale;
		}
	}

	float fWinW = (float)winW;
	float fWinH = (float)winH;

	// ---- 1. Find the Overlay Texture ID and Bounds (if it exists) ----
	// We look for the first overlay element with a valid texture.
	// This texture will be multiplied into the screen layer.
	// The overlay may cover only part of the screen (e.g. Clowns balloon
	// strips), so we also record its layout-space bounds for UV remapping.
	GLuint overlayTexID = 0;
	float  overlayX = 0, overlayY = 0, overlayW = 0, overlayH = 0;
	if (g_layoutShowOverlay) {
		for (const auto& d : view.drawables) {
			if (d.layer == LayerType::Overlay && d.element && d.element->textureID != 0) {
				overlayTexID = d.element->textureID;
				overlayX = d.x;
				overlayY = d.y;
				overlayW = d.w;
				overlayH = d.h;
				break;  // Use the first overlay found
			}
		}
	}

	// ---- Save GL state that we modify ----
	// The existing pipeline may have specific blend/texture state set.
	// We save and restore to avoid interference.
	GLboolean prevBlend = glIsEnabled(GL_BLEND);
	GLint prevBlendSrc, prevBlendDst;
	glGetIntegerv(GL_BLEND_SRC_ALPHA, &prevBlendSrc);
	glGetIntegerv(GL_BLEND_DST_ALPHA, &prevBlendDst);

	// Set the viewport to the (possibly constrained) rendering area
	glViewport(vpX, vpY, vpW, vpH);

	// Ensure fixed-function texture state doesn't interfere with our
	// GLSL shaders. The caller (final_render_raster) may have enabled
	// GL_TEXTURE_2D on unit 0 via the "nuclear sanitation" block.
	glActiveTexture(GL_TEXTURE1);
	glDisable(GL_TEXTURE_2D);
	glActiveTexture(GL_TEXTURE0);
	glDisable(GL_TEXTURE_2D);
	glUseProgram(0);  // clean slate before we bind our own shaders

	glEnable(GL_BLEND);
	glBindVertexArray(s_layoutVAO);

	// ====================================================================
	// SYSTEM-ROTATED RENDERING PATH
	//
	// When the display is rotated 90/270 degrees:
	//   1. Backdrop: "cover fill" - scale to fill the entire window,
	//      center, let overflow crop. Uses standard (non-rotated) UVs
	//      because the backdrop art is pre-authored in portrait and we
	//      want it to fill the landscape window as atmosphere.
	//   2. Screen: centered with correct aspect, UV-rotated so the
	//      portrait game image appears landscape.
	//   3. Overlay: applied to the screen via dual-tex shader with the
	//      same rotated UVs.
	//   4. Bezel: skipped entirely.
	// ====================================================================
	if (sysRotated)
	{
		// ---- BACKDROP: Disabled when system-rotated ----
		// MAME .lay backdrops are positioned as partial regions within the
		// bezel frame composition, not as standalone fullscreen backgrounds.
		// They don't look right cover-filled into a rotated window.
		// Future: support a custom fullscreen background element in the
		// .lay format for rotation mode.

		// ---- SCREEN: Rotated, centered, aspect-correct ----
		// The screen area in the layout is portrait (e.g. 1308x1744).
		// After rotation, it displays as landscape (1744x1308).
		// Compute the pixel-space rectangle for the rotated screen,
		// centered in the window with correct aspect.
		{
			// Find the screen drawable
			for (const auto& d : view.drawables)
			{
				if (d.layer != LayerType::Screen) continue;

				// The screen's native shape is portrait (d.w x d.h).
				// After rotation, display shape is (d.h x d.w).
				float scrDispAspect = d.h / d.w;  // rotated aspect

				float scrPixW, scrPixH;

				if (aspectWin > scrDispAspect) {
					// Window wider than rotated screen - fit to height
					scrPixH = fWinH;
					scrPixW = fWinH * scrDispAspect;
				}
				else {
					// Window taller than rotated screen - fit to width
					scrPixW = fWinW;
					scrPixH = fWinW / scrDispAspect;
				}

				float scrX0 = (fWinW - scrPixW) / 2.0f;
				float scrY0 = (fWinH - scrPixH) / 2.0f;

				// Convert to NDC
				float snx0 = (scrX0 / fWinW) * 2.0f - 1.0f;
				float sny0 = 1.0f - (scrY0 / fWinH) * 2.0f;
				float snx1 = ((scrX0 + scrPixW) / fWinW) * 2.0f - 1.0f;
				float sny1 = 1.0f - ((scrY0 + scrPixH) / fWinH) * 2.0f;

				glBlendFunc(GL_ONE, GL_ONE);

				if (overlayTexID != 0)
				{
					// Overlay UV transform: the overlay bounds are in the same
					// layout space as the screen. Since both rotate together,
					// the relative UV mapping is unchanged.
					float scaleU = d.w / overlayW;
					float scaleV = d.h / overlayH;
					float offU = -(overlayX - d.x) / overlayW;
					float offV = -(overlayY - d.y) / overlayH;

					int overlayMode = (videoAttributes & VIDEO_TYPE_RASTER_BW) ? 0 : 1;
					glUseProgram(s_dualTexShader);
					glUniform1f(s_dt_uAlpha, d.alpha);
					glUniform1i(s_dt_uOverlayMode, overlayMode);
					glUniform4f(s_dt_uOverlayUVXform, scaleU, scaleV, offU, offV);
					glActiveTexture(GL_TEXTURE0);
					glBindTexture(GL_TEXTURE_2D, screenTexture);
					glActiveTexture(GL_TEXTURE1);
					glBindTexture(GL_TEXTURE_2D, overlayTexID);
					DrawQuadNDC_SysRot(snx0, sny0, snx1, sny1, sysRot);
					glActiveTexture(GL_TEXTURE0);
				}
				else
				{
					glUseProgram(s_singleTexShader);
					glUniform1f(s_st_uAlpha, d.alpha);
					glActiveTexture(GL_TEXTURE0);
					glBindTexture(GL_TEXTURE_2D, screenTexture);
					DrawQuadNDC_SysRot(snx0, sny0, snx1, sny1, sysRot);
				}

				glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
				break;  // Only one screen
			}
		}

		// ---- Bezel: skipped when system-rotated ----

		// ---- Restore GL state ----
		glBindVertexArray(0);
		glUseProgram(0);

		if (!prevBlend) glDisable(GL_BLEND);
		else glBlendFunc(prevBlendSrc, prevBlendDst);
		return;
	}

	// ====================================================================
	// NORMAL (NON-ROTATED) RENDERING PATH
	// Original code path - no system rotation active.
	// ====================================================================

	// ---- 2. Render Loop ----
	// Walk all drawables in layer order (backdrop, screen, overlay, bezel).
	// The overlay layer is NOT drawn separately -- it is merged into the
	// screen layer via the dual-texture shader.
	for (const auto& d : view.drawables) {
		// Skip hidden layers based on user toggle settings
		if (d.layer == LayerType::Bezel && !g_layoutShowBezel)    continue;
		if (d.layer == LayerType::Backdrop && !g_layoutShowBackdrop) continue;

		// Skip drawing the Overlay as a separate layer (merged into Screen)
		if (d.layer == LayerType::Overlay) continue;

		// ---- DRAW BACKDROP / BEZEL ----
		if (d.layer != LayerType::Screen) {
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			if (d.element && d.element->textureID != 0) {
				DrawLayoutQuad(d.element->textureID, d.x, d.y, d.w, d.h,
					d.alpha, scale, offsetX, offsetY, fWinW, fWinH);
			}
		}

		// ---- DRAW SCREEN (with optional overlay multiply) ----
		// img5a is rendered with Y-up ortho (set_ortho) into fbo_raster,
		// so texture V=0 is the top of the game image and standard UV
		// mapping via DrawQuadNDC displays it right-side-up.
		//
		// Blend: additive (GL_ONE, GL_ONE). Game pixels ADD light on top
		// of the backdrop, just like a real CRT. Black (0,0,0) adds
		// nothing = transparent. This matches the blending used by
		// final_render_raster's img5a blit.
		else if (d.layer == LayerType::Screen) {
			float nx0, ny0, nx1, ny1;
			LayoutToNDC(d.x, d.y, d.w, d.h, scale, offsetX, offsetY, fWinW, fWinH,
				nx0, ny0, nx1, ny1);

			glBlendFunc(GL_ONE, GL_ONE);

			if (overlayTexID != 0) {
				// Overlay present -- multiply screen by overlay color gel.
				// The overlay mode selects the multiply variant:
				//   BW games:    mode 0 = pure multiply   (screen * overlay)
				//   Color games: mode 1 = 2x multiply     (screen * overlay * 2)
				//
				// The overlay may cover only a sub-region of the screen
				// (e.g. Clowns: balloons at the top only). Compute a UV
				// transform that maps screen-space [0,1] UVs to overlay
				// texture UVs. The shader returns white (multiply identity)
				// for screen pixels outside the overlay bounds.
				//
				// UV mapping: screen UV -> overlay UV
				//   ovU = (screenU - (overlayX - screenX) / screenW) * (screenW / overlayW)
				//       = screenU * (screenW / overlayW) + (-(overlayX - screenX) / overlayW)
				//   ovV = screenV * (screenH / overlayH) + (-(overlayY - screenY) / overlayH)
				float scaleU = d.w / overlayW;
				float scaleV = d.h / overlayH;
				float offsetU = -(overlayX - d.x) / overlayW;
				float offsetV = -(overlayY - d.y) / overlayH;

				int overlayMode = (videoAttributes & VIDEO_TYPE_RASTER_BW) ? 0 : 1;
				glUseProgram(s_dualTexShader);
				glUniform1f(s_dt_uAlpha, d.alpha);
				glUniform1i(s_dt_uOverlayMode, overlayMode);
				glUniform4f(s_dt_uOverlayUVXform, scaleU, scaleV, offsetU, offsetV);
				glActiveTexture(GL_TEXTURE0);
				glBindTexture(GL_TEXTURE_2D, screenTexture);
				glActiveTexture(GL_TEXTURE1);
				glBindTexture(GL_TEXTURE_2D, overlayTexID);
				DrawQuadNDC(nx0, ny0, nx1, ny1);
				glActiveTexture(GL_TEXTURE0);
			}
			else {
				// No overlay -- draw screen directly
				glUseProgram(s_singleTexShader);
				glUniform1f(s_st_uAlpha, d.alpha);
				glActiveTexture(GL_TEXTURE0);
				glBindTexture(GL_TEXTURE_2D, screenTexture);
				DrawQuadNDC(nx0, ny0, nx1, ny1);
			}

			// Restore standard blend for subsequent layers
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		}
	}

	// ---- Restore GL state ----
	glBindVertexArray(0);
	glUseProgram(0);

	if (!prevBlend) glDisable(GL_BLEND);
	else glBlendFunc(prevBlendSrc, prevBlendDst);
}

// ====================================================================
// Layout_GetOverlayTexture
// Returns the GL texture ID of the first overlay element, or 0.
// ====================================================================
GLuint Layout_GetOverlayTexture(const LayoutView& view)
{
	if (!g_layoutShowOverlay) return 0;
	for (const auto& d : view.drawables) {
		if (d.layer == LayerType::Overlay && d.element && d.element->textureID != 0) {
			return d.element->textureID;
		}
	}
	return 0;
}

// ====================================================================
// Recalculate g_layoutAspect based on zoom mode
// ====================================================================
void Layout_UpdateAspect()
{
	if (!g_activeView) return;

	const bool sysRotated = (config.system_rotation & ORIENTATION_SWAP_XY) != 0;

	// When system-rotated, always use screen-only aspect (bezel forced off).
	bool zoomToScreen = g_layoutZoomToScreen || sysRotated;

	if (zoomToScreen) {
		// Zoom mode: aspect ratio is the screen area only
		if (g_activeView->screenW > 0 && g_activeView->screenH > 0)
		{
			g_layoutAspect = g_activeView->screenW / g_activeView->screenH;
			if (sysRotated) g_layoutAspect = 1.0f / g_layoutAspect;
		}
	}
	else {
		// Full layout: aspect ratio is the entire view bounding box
		if (g_activeView->boundsW > 0 && g_activeView->boundsH > 0)
			g_layoutAspect = g_activeView->boundsW / g_activeView->boundsH;
	}
}

// ====================================================================
// Layout_CreateDefaultScreen
//
// Builds a minimal synthetic layout containing a single screen-only
// view with no backdrop, overlay, or bezel layers. This allows the
// layout rendering path in final_render_raster() to handle games
// that have no .lay artwork file, giving them the same additive-blend
// compositing and correct viewport placement as games with artwork.
//
// screenW/screenH should be the game's oriented visible-area
// dimensions (after SWAP_XY if applicable). The synthetic view is
// set up so that Layout_RenderBackdrop() returns screen = full window
// and draws nothing, and Layout_RenderForeground() draws nothing.
// ====================================================================
void Layout_CreateDefaultScreen(LayoutData& outData, float screenW, float screenH)
{
	outData.elements.clear();
	outData.views.clear();

	LayoutView view;
	view.name = "Default_Screen";

	// Screen drawable - no element pointer (nullptr), just bounds
	LayoutDrawable screen;
	screen.layer = LayerType::Screen;
	screen.element = nullptr;
	screen.x = 0.0f;
	screen.y = 0.0f;
	screen.w = screenW;
	screen.h = screenH;
	screen.alpha = 1.0f;
	screen.screenIndex = 0;

	view.drawables.push_back(screen);

	// Screen bounds = view bounds (no artwork border)
	view.screenX = 0.0f;
	view.screenY = 0.0f;
	view.screenW = screenW;
	view.screenH = screenH;
	view.boundsX = 0.0f;
	view.boundsY = 0.0f;
	view.boundsW = screenW;
	view.boundsH = screenH;

	outData.views.push_back(view);

	LOG_INFO("Layout_CreateDefaultScreen: synthetic view %.0fx%.0f", screenW, screenH);
}

// ====================================================================
// Layout_LoadForGame
//
// Search for a .lay file for the given game driver, parse it, load
// textures, and select the default view. If no .lay is found (or
// parsing fails), create a synthetic screen-only layout so that the
// layout rendering path is always active for raster games.
//
// Search order:
//   1. External artwork path (config.exartpath)
//      a. ZIP archive: <exartpath>\<gamename>.zip
//      b. Loose files: <exartpath>\<gamename>\<layFilename>
//   2. Local artwork directory (relative to exe)
//      a. ZIP archive: artwork\<gamename>.zip
//      b. Loose files: artwork\<gamename>\<layFilename>
// ====================================================================
void Layout_LoadForGame(const AAEDriver* drv)
{
	if (!drv) return;

	if (drv->layoutFile && drv->layoutFile[0] != '\0')
	{
		const char* gameName = drv->name;
		const char* layFilename = drv->layoutFile;
		std::string viewName = drv->defaultView ? drv->defaultView : "";

		std::string zipFile;    // path to the ZIP archive (empty if using loose files)
		std::string artDir;     // path to the loose-file artwork directory
		bool found = false;

		// Helper: append path separator if needed
		auto ensureTrailingSep = [](std::string& p) {
			if (!p.empty() && p.back() != '\\' && p.back() != '/')
				p.push_back('\\');
			};

		// 1. Try external artwork path (config.exartpath)
		if (!found && config.exartpath && config.exartpath[0] != '\0')
		{
			std::string extBase = config.exartpath;
			ensureTrailingSep(extBase);

			// 1a. ZIP archive: <exartpath>\<gamename>.zip
			std::string testZip = extBase + gameName + ".zip";
			if (file_exists(testZip.c_str()))
			{
				zipFile = testZip;
				artDir = extBase + gameName;
				found = true;
				LOG_INFO("Layout: found ZIP in external path: %s", zipFile.c_str());
			}

			// 1b. Loose files: <exartpath>\<gamename>\<layFilename>
			if (!found)
			{
				artDir = extBase + gameName;
				std::string testLay = artDir + "\\" + layFilename;
				if (std::filesystem::exists(testLay))
				{
					zipFile.clear();
					found = true;
					LOG_INFO("Layout: found loose file in external path: %s", testLay.c_str());
				}
			}
		}

		// 2. Try local artwork directory (relative to exe)
		if (!found)
		{
			std::string localBase = getpathM("artwork", nullptr);
			ensureTrailingSep(localBase);

			// 2a. ZIP archive: artwork\<gamename>.zip
			std::string testZip = localBase + gameName + ".zip";
			if (file_exists(testZip.c_str()))
			{
				zipFile = testZip;
				artDir = localBase + gameName;
				found = true;
				LOG_INFO("Layout: found ZIP in local path: %s", zipFile.c_str());
			}

			// 2b. Loose files: artwork\<gamename>\<layFilename>
			if (!found)
			{
				artDir = localBase + gameName;
				std::string testLay = artDir + "\\" + layFilename;
				if (std::filesystem::exists(testLay))
				{
					zipFile.clear();
					found = true;
					LOG_INFO("Layout: found loose file in local path: %s", testLay.c_str());
				}
			}
		}
		if (found)
		{
			if (Layout_Parse(layFilename, zipFile, artDir, g_layoutData))
			{
				Layout_LoadTextures(g_layoutData, zipFile, artDir);
				g_activeView = Layout_FindView(g_layoutData, viewName);

				if (g_activeView)
				{
					g_layoutAspect = g_activeView->boundsW / g_activeView->boundsH;
					g_layoutEnabled = true;
					// Update menu availability flags from layout drawables.
					// This bridges the layout system with the menu flags that
					 // load_artwork() would normally set for the legacy path.
					for (const auto& d : g_activeView->drawables)
					{
						if (d.layer == LayerType::Bezel && d.element && d.element->textureID)
						{
							g_bezelAvailable = 1;
							g_artcropAvailable = 1;
						}
						if (d.layer == LayerType::Backdrop && d.element && d.element->textureID)
							g_artworkAvailable = 1;
						if (d.layer == LayerType::Overlay && d.element && d.element->textureID)
							g_overlayAvailable = 1;
					}

					LOG_INFO("Layout loaded: view='%s' aspect=%.3f",
						g_activeView->name.c_str(), g_layoutAspect);
				}
				else
				{
					LOG_WARN("Layout loaded but view '%s' not found", viewName.c_str());
				}
			}
			else
			{
				LOG_WARN("Layout parse failed for game '%s'", gameName);
			}
		}
		else
		{
			LOG_INFO("Layout: no .lay file found for game '%s'", gameName);
		}
	}

	// ---- Fallback: create a synthetic screen-only layout ----
	// If no .lay file was loaded (either not found, parse failed, or the
	// driver has no layoutFile), create a minimal layout so that
	// final_render_raster() always uses the layout path. This gives every
	// raster game the correct additive-blend compositing.
	if (!g_layoutEnabled)
	{
		const rectangle& va = Machine->drv->visible_area;
		float scrW = (float)(va.max_x - va.min_x + 1);
		float scrH = (float)(va.max_y - va.min_y + 1);

		// Apply orientation - match what fbo_init_raster() does
		if (Machine->drv->rotation & ORIENTATION_SWAP_XY)
		{
			float t = scrW;
			scrW = scrH;
			scrH = t;
		}

		Layout_CreateDefaultScreen(g_layoutData, scrW, scrH);
		g_activeView = &g_layoutData.views[0];
		g_layoutAspect = scrW / scrH;
		g_layoutEnabled = true;

		LOG_INFO("Synthetic layout created for '%s': %.0fx%.0f aspect=%.3f",
			drv->name, scrW, scrH, g_layoutAspect);
	}
}

// ====================================================================
// Game Dimension & Aspect Ratio Utilities
//
// The game's oriented output dimensions and pixel aspect ratio are 
// computed here so the layout system and window management code can 
// query them.
// ====================================================================

static int   s_gameWidth = 0;
static int   s_gameHeight = 0;
static float s_pixelAspect = 1.0f;
/*
void Layout_InitGameDimensions()
{
	s_gameWidth = 0;
	s_gameHeight = 0;
	s_pixelAspect = 1.0f;

	if (!Machine || !Machine->drv)
		return;

	const rectangle& va = Machine->drv->visible_area;
	int srcW = va.max_x - va.min_x + 1;
	int srcH = va.max_y - va.min_y + 1;

	if (Machine->orientation & ORIENTATION_SWAP_XY) {
		s_gameWidth = srcH;
		s_gameHeight = srcW;
	}
	else {
		s_gameWidth = srcW;
		s_gameHeight = srcH;
	}

	if (s_gameWidth > 0 && s_gameHeight > 0) {
		float nativeAspect = (float)s_gameWidth / (float)s_gameHeight;
		float targetAspect = (s_gameHeight > s_gameWidth)
			? (3.0f / 4.0f)
			: (4.0f / 3.0f);
		s_pixelAspect = targetAspect / nativeAspect;
	}

	LOG_INFO("Layout_InitGameDimensions: output=%dx%d pixelAspect=%.3f",
		s_gameWidth, s_gameHeight, s_pixelAspect);
}
*/
void Layout_InitGameDimensions()
{
	s_gameWidth = 0;
	s_gameHeight = 0;
	s_pixelAspect = 1.0f;

	if (!Machine || !Machine->drv)
		return;

	const rectangle& va = Machine->drv->visible_area;
	int srcW = va.max_x - va.min_x + 1;
	int srcH = va.max_y - va.min_y + 1;

	// Use the DRIVER's rotation for dimension swapping, not the composed
	// Machine->orientation. The FBO is allocated based on drv->rotation
	// only (see fbo_init_raster), and system rotation (config.system_rotation)
	// is applied at display time in Layout_Render, not here.
	const int drvRot = Machine->drv->rotation;

	if (drvRot & ORIENTATION_SWAP_XY) {
		s_gameWidth = srcH;
		s_gameHeight = srcW;
	}
	else {
		s_gameWidth = srcW;
		s_gameHeight = srcH;
	}

	if (s_gameWidth > 0 && s_gameHeight > 0) {
		float nativeAspect = (float)s_gameWidth / (float)s_gameHeight;

		// Determine target aspect from driver rotation, not dimension shape.
		// Vector games can have coordinate spaces taller than wide (e.g. Star Wars
		// AVG space is 251x281) while still being landscape 4:3 monitors.
		// The rotation field is the authoritative source for physical orientation.
		bool isVertical = (drvRot & ORIENTATION_SWAP_XY) != 0;
		float targetAspect = isVertical ? (3.0f / 4.0f) : (4.0f / 3.0f);

		s_pixelAspect = targetAspect / nativeAspect;
	}

	LOG_INFO("Layout_InitGameDimensions: output=%dx%d pixelAspect=%.3f drvRotation=0x%X sysRotation=0x%X",
		s_gameWidth, s_gameHeight, s_pixelAspect, drvRot, config.system_rotation);
}

void Layout_ResetGameDimensions()
{
	s_gameWidth = 0;
	s_gameHeight = 0;
	s_pixelAspect = 1.0f;
}

int   Layout_GetGameWidth() { return s_gameWidth; }
int   Layout_GetGameHeight() { return s_gameHeight; }
float Layout_GetPixelAspect() { return s_pixelAspect; }

float Layout_ComputeGameAspect()
{
	// System rotation: when SWAP_XY is active, bezel is forced off and the
	// aspect ratio is derived from the screen area only, then inverted.
	const bool sysRotated = (config.system_rotation & ORIENTATION_SWAP_XY) != 0;

	// If a layout is active, compute aspect from the layout view.
	// When bezel is off or zoom-to-screen is on, use the screen area
	// aspect (gives 4:3 for horizontal games). When showing the full
	// layout with bezel, use the layout bounds aspect.
	if (g_layoutEnabled && g_activeView)
	{
		g_layoutShowBezel = (config.bezel != 0);
		g_layoutShowBackdrop = (config.artwork != 0);
		g_layoutShowOverlay = (config.overlay != 0);

		// System rotation forces zoom-to-screen (bezel disabled).
		bool zoomToScreen = g_layoutZoomToScreen
			|| (config.artcrop != 0)
			|| (!g_layoutShowBezel)
			|| sysRotated;

		if (zoomToScreen && g_activeView->screenW > 0 && g_activeView->screenH > 0)
		{
			float aspect = g_activeView->screenW / g_activeView->screenH;
			if (sysRotated) aspect = 1.0f / aspect;
			LOG_INFO("Layout_ComputeGameAspect: from screen area: %.3f%s",
				aspect, sysRotated ? " (system-rotated)" : "");
			return aspect;
		}
		else if (g_activeView->boundsW > 0 && g_activeView->boundsH > 0)
		{
			float aspect = g_activeView->boundsW / g_activeView->boundsH;
			if (sysRotated) aspect = 1.0f / aspect;
			LOG_INFO("Layout_ComputeGameAspect: from layout bounds: %.3f%s",
				aspect, sysRotated ? " (system-rotated)" : "");
			return aspect;
		}
	}

	// No layout -- use the game's pixel dimensions with pixel aspect correction.
	if (s_gameWidth > 0 && s_gameHeight > 0)
	{
		float aspect = ((float)s_gameWidth * s_pixelAspect) / (float)s_gameHeight;
		if (sysRotated) aspect = 1.0f / aspect;
		LOG_INFO("Layout_ComputeGameAspect: from game dims: %.3f (%dx%d pixelAspect=%.3f)%s",
			aspect, s_gameWidth, s_gameHeight, s_pixelAspect,
			sysRotated ? " (system-rotated)" : "");
		return aspect;
	}

	// Fallback: use the driver's screen dimensions directly.
	if (Machine && Machine->drv)
	{
		const rectangle& va = Machine->drv->visible_area;
		int scrW = (va.max_x - va.min_x + 1);
		int scrH = (va.max_y - va.min_y + 1);
		if (scrW > 0 && scrH > 0)
		{
			float aspect = (float)scrW / (float)scrH;
			if (sysRotated) aspect = 1.0f / aspect;
			LOG_INFO("Layout_ComputeGameAspect: from driver screen: %.3f (%dx%d)%s",
				aspect, scrW, scrH, sysRotated ? " (system-rotated)" : "");
			return aspect;
		}
	}

	return 0.0f;
}

// ====================================================================
// Cleanup
// ====================================================================

void Layout_FreeTextures(LayoutData& data)
{
	// Free all element textures (artwork PNGs and baked procedurals)
	for (auto& pair : data.elements) {
		if (pair.second.textureID != 0) {
			glDeleteTextures(1, &pair.second.textureID);
			pair.second.textureID = 0;
		}
	}

	// Clean up layout shader resources
	if (s_layoutVAO) { glDeleteVertexArrays(1, &s_layoutVAO); s_layoutVAO = 0; }
	if (s_layoutVBO) { glDeleteBuffers(1, &s_layoutVBO);      s_layoutVBO = 0; }
	if (s_singleTexShader) { glDeleteProgram(s_singleTexShader);    s_singleTexShader = 0; }
	if (s_dualTexShader) { glDeleteProgram(s_dualTexShader);      s_dualTexShader = 0; }
	s_layoutShadersReady = false;
}