#define NOMINMAX
#include "stdio.h"
#include "texture_handler.h"
#include "sys_window.h"   // GetSystemWindow, for the snapshot size
#include <cmath>         // floor, log2
#include <time.h>
#include <vector>
#include <mutex>
#include <sstream>
#include <filesystem>
#include <unordered_map>
#include "aae_mame_driver.h"
#include "menu.h" // This is just for load_texture
#include "path_helper.h"
#include "sys_gl.h"
#ifdef _WIN32
#include "win32/win32_private.h"   // win_get_window
#endif  // win_get_window; no longer pulled in via aae_mame_driver.h

//#define STB_IMAGE_IMPLEMENTATION
//#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image.h"
#include "stb_image_write.h" // https://github.com/nothings/stb

rtex_t error_tex[2];
rtex_t pause_tex[2];
rtex_t fun_tex[4];
rtex_t art_tex[8];
rtex_t game_tex[10];
rtex_t menu_tex[7]; // Now unused.

// Keep track of every texture created.
static std::vector<GLuint> g_textures;
static std::mutex       g_texMutex;
// Track width/height for each created texture
static std::unordered_map<GLuint, std::pair<int, int>> g_texSize;

// Registers a texture ID for later bulk-deletion
inline void register_texture(GLuint id) noexcept
{
	if (id)
	{
		std::lock_guard<std::mutex> lock(g_texMutex);
		g_textures.push_back(id);
	}
}

// Deletes all registered textures
void destroy_all_textures()
{
	std::lock_guard<std::mutex> lock(g_texMutex);
	if (!g_textures.empty())
	{
		glDeleteTextures(
			static_cast<GLsizei>(g_textures.size()),
			g_textures.data()
		);
		LOG_INFO("destroy_all_textures - deleted %zu textures", g_textures.size());
		g_textures.clear();
		g_texSize.clear();
	}
}

bool get_texture_size(rtex_t tex, int* outW, int* outH)
{
	if (!tex || !outW || !outH) return false;

	// First try our registry (fast path)
	{
		std::lock_guard<std::mutex> lock(g_texMutex);
		auto it = g_texSize.find(tex);
		if (it != g_texSize.end()) {
			*outW = it->second.first;
			*outH = it->second.second;
			return true;
		}
	}

	// Fallback: query GL (works for textures not loaded via load_texture)
	if (!glIsTexture(tex)) return false;

	GLint prevBinding = 0;
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevBinding);

	glBindTexture(GL_TEXTURE_2D, tex);
	GLint w = 0, h = 0;
	glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
	glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);

	// Restore previous binding
	glBindTexture(GL_TEXTURE_2D, (GLuint)prevBinding);

	if (w > 0 && h > 0) {
		// Cache for next time
		std::lock_guard<std::mutex> lock(g_texMutex);
		g_texSize[tex] = { (int)w, (int)h };
		*outW = (int)w;
		*outH = (int)h;
		return true;
	}
	return false;
}

// filter: 0 = linear, 1 = mipmap
// modernGL: false = legacy (default), true = OpenGL3+ path
rtex_t load_texture(const char* filename,
	const char* archname,
	int numcomponents,
	int filter,
	bool modernGL)
{
	GLuint tex = 0;
	int width = 0, height = 0, comp = 0;
	unsigned char* image_data = nullptr;
	unsigned char* raw_data = nullptr;

	stbi_set_flip_vertically_on_load(1);

	// load from FS or ZIP
	if (!archname) {
		image_data = stbi_load(filename, &width, &height, &comp, numcomponents);
	}
	else {
		raw_data = load_zip_file(archname, filename);
		if (raw_data) {
			size_t size = get_last_zip_file_size();
			image_data = stbi_load_from_memory(raw_data, (int)size, &width, &height, &comp, numcomponents);
		}
	}

	if (!image_data) {
		LOG_ERROR("ERROR: could not load file %s", filename);
		if (raw_data) free(raw_data);
		return 0;
	}
	LOG_INFO("Texture %s: %dx%d, input components %d", filename, width, height, comp);

	// NPOT warning
	if ((width & (width - 1)) != 0 || (height & (height - 1)) != 0) {
		LOG_INFO("NOTE: texture %s is not power of 2 dimensions", filename);
	}

	// generate & bind
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);

	// set wrapping
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	if (!modernGL) {
		// --- Legacy GL (OGL 1.4-2.x) path ---
		glTexImage2D(
			GL_TEXTURE_2D, 0,
			(numcomponents == 4 ? GL_RGBA8 : GL_RGB8),
			width, height, 0,
			(numcomponents == 4 ? GL_RGBA : GL_RGB),
			GL_UNSIGNED_BYTE,
			image_data
		);

		if (filter == 1) {
			// build legacy mipmaps
			gluBuild2DMipmaps(
				GL_TEXTURE_2D,
				numcomponents,
				width, height,
				(numcomponents == 4 ? GL_RGBA : GL_RGB),
				GL_UNSIGNED_BYTE,
				image_data
			);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		}
		else {
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		}
	}
	else {
		// --- Modern GL (3.x+) path ---
		// allocate immutable storage
		glTexStorage2D(
			GL_TEXTURE_2D,
			1 + (filter == 1 ? (int)floor(log2(std::max(width, height))) : 0),
			(numcomponents == 4 ? GL_RGBA8 : GL_RGB8),
			width, height
		);
		// upload base level
		glTexSubImage2D(
			GL_TEXTURE_2D, 0,
			0, 0,
			width, height,
			(numcomponents == 4 ? GL_RGBA : GL_RGB),
			GL_UNSIGNED_BYTE,
			image_data
		);

		if (filter == 1) {
			glGenerateMipmap(GL_TEXTURE_2D);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		}
		else {
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		}
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	}

	// cleanup
	stbi_image_free(image_data);
	if (raw_data) free(raw_data);

	// Track for bulk-deletion
	register_texture(tex);
	std::lock_guard<std::mutex> lock(g_texMutex);
	g_texSize[tex] = { width, height };
	LOG_INFO("New Texture created: ID %u", tex);
	return tex;
}

// -----------------------------------------------------------------------------
//  snapshot (F12)
//
//  Split into a SHARED writer and a per-chain pixel source, so the GL and the
//  Vulkan chains emit byte-identically named files and only the way the pixels
//  are fetched differs:
//
//    snapshot_write_rgba8_png()  - snap/ creation, the timestamped filename,
//                                  stbi_write_png and the log line. Written
//                                  ONCE, here; the Vulkan swapchain readback
//                                  (aae_video_vk/snapshot_vk.cpp) calls it too.
//    glchain_snapshot()          - the GL pixel source. Behavior unchanged.
//
//  The public snapshot() entry point moved to aae_video/renderer_dispatch.cpp
//  with the rest of the chain routing (it is still declared in this header).
//
//  Both chains hand over a TIGHTLY PACKED, TOP-ROW-FIRST RGBA8 buffer:
//  buffer scanline 0 is the topmost row of the image, which is exactly what
//  PNG scanline 0 must be. GL needs a vertical flip to satisfy that contract
//  (glReadPixels' origin is bottom-left); a Vulkan image's row 0 is already
//  the top row, so the VK path must NOT flip.
// -----------------------------------------------------------------------------
bool snapshot_write_rgba8_png(const unsigned char* rgba, int width, int height)
{
	if (!rgba || width <= 0 || height <= 0)
	{
		LOG_ERROR("snapshot - invalid pixel buffer (%dx%d)", width, height);
		return false;
	}

	std::filesystem::create_directory("snap");

	// generate timestamp
	const auto now = std::chrono::system_clock::to_time_t(
		std::chrono::system_clock::now());
	// localtime_s and localtime_r do the same job with REVERSED arguments -
	// MSVC takes (tm*, time_t*), POSIX takes (time_t*, tm*). Swapping them
	// compiles cleanly and produces garbage, so spell it out.
	tm tmNow{};
#ifdef _WIN32
	localtime_s(&tmNow, &now);
#else
	localtime_r(&now, &tmNow);
#endif

	std::ostringstream oss;
	oss << std::put_time(&tmNow, "%Y%m%d%H%M%S");

	// Machine is a pointer to a file-static RunningMachine and is never null,
	// but gamedrv is only populated once a driver is loaded. The fallback name
	// exists purely so a stray F12 before/after a game load cannot dereference
	// null; with a driver loaded (including the "gui" front-end driver) the
	// name is identical to what the GL path has always produced.
	const char* gameName =
		(Machine && Machine->gamedrv && Machine->gamedrv->name)
		? Machine->gamedrv->name : "aae";

	std::filesystem::path outPath =
		std::filesystem::path("snap")
		/ (std::string(gameName) + "_" + oss.str() + ".png");

	stbi_write_png(outPath.string().c_str(),
		width, height, 4,
		rgba, width * 4);

	LOG_INFO("snapshot saved: %s", outPath.string().c_str());
	return true;
}

void glchain_snapshot()
{
	// Portable: ISystemWindow reports its own client size, so no RECT and no
	// platform window handle are needed.
	ISystemWindow& win = GetSystemWindow();
	int width  = win.ClientWidth();
	int height = win.ClientHeight();

	if (width <= 0 || height <= 0)
	{
		LOG_ERROR("snapshot - invalid window size");
		return;
	}

	std::vector<unsigned char> buffer(static_cast<size_t>(width) * height * 4);
	glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, buffer.data());

	// vertical flip so PNG is upright
	for (int y = 0; y < height / 2; ++y)
		std::swap_ranges(buffer.data() + y * width * 4,
			buffer.data() + (y + 1) * width * 4,
			buffer.data() + (height - y - 1) * width * 4);

	snapshot_write_rgba8_png(buffer.data(), width, height);
}

// Binds and configures a 2D texture.
/// @param texture     OpenGL texture ID to bind.
/// @param linear      Use linear filtering if true (default: true).
/// @param mipmapping  Enable trilinear mipmap filtering if true (default: false).
/// @param blendMode   Enable Blending
/// @param setColor    Reset current color to opaque white if true (default: false).

void set_texture(rtex_t* texture, bool linear, bool mipmapping, bool blending, bool set_color)
{
	// Determine filters
	GLenum magFilter = linear ? GL_LINEAR : GL_NEAREST;
	GLenum minFilter = mipmapping
		? GL_LINEAR_MIPMAP_LINEAR
		: magFilter;

	// Bind
	glBindTexture(GL_TEXTURE_2D, *texture);

	// Filtering
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magFilter);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter);

	// Wrapping
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	if (blending) {
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}
}

// ---------------------------------------------------------------------------
// make_single_bitmap
// Loads a single texture (filename) from inside a ZIP archive (archname).
//
// Search order:
//   1a. ZIP archive in config.exartpath (external artwork directory)
//   1b. Unzipped folder in config.exartpath matching the game name
//   2a. ZIP archive in the default "artwork" folder (next to the exe)
//   2b. Unzipped folder in the default "artwork" folder matching the game name
//
// This mirrors the ROM loader pattern where config.exrompath is checked
// before falling back to the default roms directory.
//
// Populates *texture with the GL handle.
// Returns 1 on success, 0 on failure.
// ---------------------------------------------------------------------------
int make_single_bitmap(rtex_t* texture, const char* filename, const char* archname, int mtype)
{
	std::string archivePath;

	// ---------------------------------------------------------------
	// Helper: build a path by joining a root directory with a child.
	// Ensures exactly one backslash separator between root and child.
	// ---------------------------------------------------------------
	auto join_path = [](const std::string& root, const char* child) -> std::string {
		if (root.empty() || !child || !child[0])
			return std::string();
		std::string p = root;
		if (p.back() != '\\' && p.back() != '/')
			p.push_back('\\');
		p.append(child);
		return p;
		};

	// ---------------------------------------------------------------
	// 1. Try the external artwork path first (config.exartpath)
	//    Set from the ini key "mame_artwork_path" (e.g. "C:\mame\artwork").
	//    Only attempted if the path is configured and non-empty.
	// ---------------------------------------------------------------
	if (config.exartpath && config.exartpath[0] != '\0')
	{
		// 1a. Try ZIP archive in external path: <exartpath>\<archname>
		//     archname already includes .zip (from the driver artwork table),
		//     so we just join the path directly -- no extra suffix needed.
		archivePath = join_path(config.exartpath, archname);

		if (!archivePath.empty() && file_exists(archivePath.c_str()))
		{
			LOG_INFO("Loading bitmap: '%s' from external archive: %s", filename, archivePath.c_str());
			*texture = load_texture(filename, archivePath.c_str(), 4, 1);

			if (*texture != 0)
				return 1;

			LOG_ERROR("External archive found but texture load failed for '%s'", filename);
		}

		// 1b. Try unzipped folder in external path: <exartpath>\<gamename>\<filename>
		if (Machine && Machine->gamedrv && Machine->gamedrv->name)
		{
			std::string folderBase = join_path(config.exartpath, Machine->gamedrv->name);
			std::filesystem::path fullFilePath = std::filesystem::path(folderBase) / filename;

			if (std::filesystem::exists(fullFilePath))
			{
				LOG_INFO("Loading bitmap: '%s' from external folder: %s", filename, fullFilePath.string().c_str());
				std::string fullPathStr = fullFilePath.string();
				// Pass full path as filename and nullptr as archname
				// to bypass ZIP reader and use stbi_load directly.
				*texture = load_texture(fullPathStr.c_str(), 0, 4, 1);

				if (*texture != 0)
					return 1;

				LOG_ERROR("External folder file found but texture load failed for '%s'", filename);
			}
		}

		LOG_INFO("Bitmap '%s' not found in external path '%s', trying default artwork folder",
			filename, config.exartpath);
	}

	// ---------------------------------------------------------------
	// 2. Try the default artwork path (relative to the exe directory)
	// ---------------------------------------------------------------

	// 2a. Try ZIP archive in default artwork folder.
	//     getpathM("artwork", archname) resolves relative to the exe dir,
	//     e.g. "C:\AAE\artwork\asteroid.zip".
	archivePath = getpathM("artwork", archname);
	LOG_INFO("Loading bitmap: '%s' from default archive: %s", filename, archivePath.c_str());

	*texture = load_texture(filename, archivePath.c_str(), 4, 1);

	if (*texture != 0)
		return 1;

	// 2b. Fallback: Try unzipped folder in default artwork path.
	//     Looks for artwork\<gamename>\<filename> (e.g. "artwork\bzone\backdrop.png").
	if (Machine && Machine->gamedrv && Machine->gamedrv->name)
	{
		std::string folderPath = getpathM("artwork", Machine->gamedrv->name);
		std::filesystem::path fullFilePath = std::filesystem::path(folderPath) / filename;

		if (std::filesystem::exists(fullFilePath))
		{
			LOG_INFO("Archive load failed. Falling back to direct file: %s", fullFilePath.string().c_str());

			// Pass the full path as filename and 0 (nullptr) as archname
			// to force load_texture to bypass the ZIP reader and use stbi_load.
			std::string fullPathStr = fullFilePath.string();
			*texture = load_texture(fullPathStr.c_str(), 0, 4, 1);

			if (*texture != 0)
				return 1;
		}
	}

	LOG_ERROR("make_single_bitmap: failed to load '%s' from all paths (external and default)", filename);
	return 0;
}

// ---------------------------------------------------------------------------
// load_artwork
// Iterates the artworks table for the current game and loads each entry
// into the appropriate texture slot (art_tex, fun_tex, or game_tex).
// Records which art layers loaded successfully in art_loaded[].
//
// After loading, any config flags (artwork, overlay, bezel) whose
// corresponding textures failed to load are disabled so the renderer
// never references invalid texture handles. If the bezel is unavailable,
// artcrop is also disabled since it only applies to bezel rendering.
//
// Finally, the four g_xxxAvailable flags declared in menu.h are set so
// the Video setup menu can gray out artwork items that have no files.
// These flags are set AFTER the config flags have been cleaned up, so
// they always reflect the true loaded state rather than the ini setting.
// ---------------------------------------------------------------------------
void load_artwork(const struct artworks* p)
{
	int goodload = 0;
	int type = 0;

	// Artwork layer assignments:
	//   art_tex[0] = Backdrop    -> controlled by config.artwork
	//   art_tex[1] = Overlay     -> controlled by config.overlay
	//   art_tex[2] = Bezel mask  (Tempest/Tacscan rotated bezel - TODO: replace with shader)
	//   art_tex[3] = Bezel frame -> controlled by config.bezel
	//   art_tex[4] = Screen burn (reserved)

	for (int i = 0; p[i].filename != NULL; i++)
	{
		goodload = 0;

		switch (p[i].type)
		{
		case FUN_TEX:
			goodload = make_single_bitmap(&fun_tex[p[i].target], p[i].filename, p[i].zipfile, 0);
			break;

		case ART_TEX:
			goodload = make_single_bitmap(&art_tex[p[i].target], p[i].filename, p[i].zipfile, type);
			if (goodload) { art_loaded[p[i].target] = 1; }
			break;

		case GAME_TEX:
			goodload = make_single_bitmap(&game_tex[p[i].target], p[i].filename, p[i].zipfile, 0);
			break;

		default:
			LOG_ERROR("load_artwork: unknown artwork type %d for '%s'", p[i].type, p[i].filename);
			break;
		}

		if (!goodload)
		{
			LOG_INFO("load_artwork: could not load '%s' (type=%d, target=%d).",
				p[i].filename, p[i].type, p[i].target);
		}
	}

	// --- Disable config flags for any art layers that failed to load. ---
	// This prevents the renderer from trying to use textures that do not
	// exist, and keeps the menu toggles honest. Without this, enabling
	// artwork/overlay/bezel in the menu for a game that has no art files
	// would reference invalid texture handles.

	// Backdrop (art_tex[0])
	if (!art_loaded[0] && config.artwork)
	{
		LOG_INFO("load_artwork: backdrop not available - disabling config.artwork.");
		config.artwork = 0;
	}

	// Overlay (art_tex[1])
	if (!art_loaded[1] && config.overlay)
	{
		LOG_INFO("load_artwork: overlay not available - disabling config.overlay.");
		config.overlay = 0;
	}

	// Bezel frame (art_tex[3]).
	// If the bezel did not load we also force artcrop off because artcrop
	// adjusts bezel zoom/pan - it has no meaning without a bezel texture.
	if (!art_loaded[3] && config.bezel)
	{
		LOG_INFO("load_artwork: bezel not available - disabling config.bezel and config.artcrop.");
		config.bezel = 0;
		config.artcrop = 0;
	}

	// --- Update menu availability flags (declared in menu.h). ---
	// These are read by BuildVideoMenu() each time the player opens the
	// Video setup screen. Items whose flag is 0 are rendered grayed-out
	// with a "NOT LOADED" label and cannot be toggled.
	//
	// We use art_loaded[] directly here rather than the config flags,
	// because the config flags may have just been zeroed above. We want
	// the menu to show "NOT LOADED" if the file was never found, not just
	// if the ini had it disabled.

	g_artworkAvailable = art_loaded[0];   // backdrop
	g_overlayAvailable = art_loaded[1];   // color gel overlay
	g_bezelAvailable = art_loaded[3];   // bezel frame

	// Crop-bezel only makes sense when a bezel is present.
	// Even if the artworks table somehow loaded art_tex[2] (the mask),
	// we still gate artcrop on the bezel frame itself.
	g_artcropAvailable = art_loaded[3];

	LOG_INFO("load_artwork: availability flags: artwork=%d overlay=%d bezel=%d artcrop=%d",
		g_artworkAvailable, g_overlayAvailable, g_bezelAvailable, g_artcropAvailable);
}
