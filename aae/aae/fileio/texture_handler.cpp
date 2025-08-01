#define NOMINMAX
#include "stdio.h"
#include "texture_handler.h"
#include <time.h>
#include <vector>
#include <mutex>
#include <sstream>
#include <filesystem>
#include "aae_mame_driver.h"

//#define STB_IMAGE_IMPLEMENTATION
//#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image.h"
#include "stb_image_write.h" // https://github.com/nothings/stb


GLuint error_tex[2];
GLuint pause_tex[2];
GLuint fun_tex[4];
GLuint art_tex[8];
GLuint game_tex[10];
GLuint menu_tex[7];

// Keep track of every texture created.
static std::vector<GLuint> g_textures;
static std::mutex       g_texMutex;

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
	}
}

// filter: 0 = linear, 1 = mipmap
// modernGL: false = legacy (default), true = OpenGL3+ path
GLuint load_texture(const char* filename,
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
		size_t size = get_last_zip_file_size();
		image_data = stbi_load_from_memory(raw_data, (int)size, &width, &height, &comp, numcomponents);
	}

	if (!image_data) {
		LOG_ERROR("ERROR: could not load %s", filename);
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

	LOG_INFO("New Texture created: ID %u", tex);
	return tex;
}

// -----------------------------------------------------------------------------
//  snapshot
// -----------------------------------------------------------------------------
void snapshot()
{
	RECT rc{};
	GetClientRect(win_get_window(), &rc);
	int width = rc.right - rc.left;
	int height = rc.bottom - rc.top;

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

	std::filesystem::create_directory("snap");

	// generate timestamp
	const auto now = std::chrono::system_clock::to_time_t(
		std::chrono::system_clock::now());
	tm tmNow{};
	localtime_s(&tmNow, &now);

	std::ostringstream oss;
	oss << std::put_time(&tmNow, "%Y%m%d%H%M%S");

	std::filesystem::path outPath =
		std::filesystem::path("snap")
		/ (std::string(Machine->gamedrv->name) + "_" + oss.str() + ".png");

	stbi_write_png(outPath.string().c_str(),
		width, height, 4,
		buffer.data(), width * 4);

	LOG_INFO("snapshot saved: %s", outPath.string().c_str());
}

// Binds and configures a 2D texture.
/// @param texture     OpenGL texture ID to bind.
/// @param linear      Use linear filtering if true (default: true).
/// @param mipmapping  Enable trilinear mipmap filtering if true (default: false).
/// @param blendMode   Enable Blending
/// @param setColor    Reset current color to opaque white if true (default: false).

void set_texture(GLuint* texture, GLboolean linear, GLboolean mipmapping, GLboolean blending, GLboolean set_color)
{
	// Determine filters
	GLenum magFilter = linear ? GL_LINEAR : GL_NEAREST;
	GLenum minFilter = mipmapping
		? GL_LINEAR_MIPMAP_LINEAR
		: magFilter;

	// Bind & env
	glBindTexture(GL_TEXTURE_2D, *texture);
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

	// Filtering
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magFilter);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter);

	// Wrapping
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	// Enable texturing
	glEnable(GL_TEXTURE_2D);

	// Optional color reset
	if (set_color) {
		glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
	}

	if (blending) {
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}
}