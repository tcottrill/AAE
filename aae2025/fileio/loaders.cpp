#include "stdio.h"
#include "loaders.h"
#include <time.h>
#include "gameroms.h"
#include "aae_mame_driver.h"
#include "memory.h"
#include "path_helper.h"

#include "miniz.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../sys_video/stb_image.h"
#include "../sys_video/stb_image_write.h" // https://github.com/nothings/stb

#define ZIPNOTFOUND 5
#define ROMNOTFOUND 4
#define ROMWRONGSIZE 3
#define ROMOKAY 1
#define ROMERROR 0

const char* artpath = "artwork\\";
const char* samplepath = "samples\\";
//CSHA1 sha1;

// TODO: Nope. I don't like this. TODO: Move everything back to aae_fileio, and add the newer rom loading code from aae2016


void swap(unsigned char* src, unsigned char* dst, int length, int odd)
{
	int count;
	for (count = 0; count < length; count += 1)
		dst[(count << 1) | (odd)] = src[(count)];
}

void bswap(char* mem, int length)
{
	int i, j;
	for (i = 0; i < (length / 2); i += 1)
	{
		j = mem[i * 2 + 0];
		mem[i * 2 + 0] = mem[i * 2 + 1];
		mem[i * 2 + 1] = j;
	}
}

// TODO: Fix Alternate path for Sample Loading from MAME or other dir.
int read_samples(const char** samplenames, int val)
{
	char temppath[MAX_PATH];
	const char* def_sample_path = "samples\\"; //Change this to get config
	int ret = 0;
	int i = 0;
	int total = 0;

	if (samplenames == 0 || samplenames[0] == 0) return 0;

	i = 0;
	while (samplenames[i] != 0) { i++; }
	total = i;
	wrlog("Total number of Samples is %d", total);

	//Build a path
	strcpy(temppath, def_sample_path);//if it's not there, try sample dir
	//get_config_string("directory","samplepath","samples");
	strcat(temppath, (char*)samplenames[0]);
	strcat(temppath, "\0");

	//Load Samples
	for (i = 1; i < total; i++)
	{
		wrlog("Path: %s Sample name: %s ", temppath, samplenames[i]);
		load_sample(temppath, (char*)samplenames[i], config.audio_force_resample ? 1:0);
	}
	num_samples = total - 2;
	if (num_samples == 0) { wrlog("Samples loading failure, bad with no sound..."); }
	wrlog("Completed Loading Samples: Num %d", num_samples);

	return EXIT_SUCCESS;
}

// TODO: Add this back in
int load_ambient()
{
/*
	game_sounds[(num_samples + 1)] = load_sample("aaeres\\flyback.wav");
	if (!game_sounds[(num_samples + 1)]) {
		allegro_message("Error reading ambient WAV file. Where did it go?");
		return 0;
	}
	else { num_samples++; }
	game_sounds[(num_samples + 1)] = load_sample("aaeres\\psnoise.wav");
	if (!game_sounds[(num_samples + 1)]) {
		allegro_message("Error reading ambient WAV file. Where did it go?");
		return 0;
	}
	else { num_samples++; }
	game_sounds[(num_samples + 1)] = load_sample("aaeres\\hiss.wav");
	if (!game_sounds[(num_samples + 1)]) {
		allegro_message("Error reading ambient WAV file. Where did it go?");
		return 0;
	}
	*/
	return 1;
}

int load_hi_aae(int start, int size, int image)
{
	int num = 0;
	std::string fullpath;
	FILE* fd;
	membuffer = (unsigned char*)malloc(size);
	memset(membuffer, 0, size);

	fullpath = getpathM("hi", 0);
	fullpath.append("\\");
	fullpath.append(driver[gamenum].name);
	fullpath.append(".aae");


	fd = fopen(fullpath.c_str(), "rb");
	if (!fd) { wrlog("Hiscore / nvram file for this game not found"); return 1; }
	wrlog("Loading Hiscore table / nvram");
	fread(membuffer, 1, size, fd);

	memcpy((Machine->memory_region[CPU0] + start), membuffer, size);
	fclose(fd);
	free(membuffer);
	return 0;
}

int save_hi_aae(int start, int size, int image)
{
	int num = 0;
	std::string fullpath;
	fullpath = getpathM("hi", 0);
	fullpath.append("\\");
	fullpath.append(driver[gamenum].name);
	fullpath.append(".aae");
		
	wrlog("Saving Hiscore table / nvram");
	membuffer = (unsigned char*)malloc(size + 3);
	memset(membuffer, 0, size);
	memcpy(membuffer, (Machine->memory_region[CPU0] + start), size);
	save_file(fullpath.c_str(), membuffer, size);
	free(membuffer);
	return 0;
}

// TODO: Add this back in
int verify_rom(const char* archname, const struct RomModule* p, int romnum)
{
	return 1;
}
// TODO: Add this back in
int verify_sample(const char** p, int num)
{
	return 1;
}
// TODO: MOVE THIS
int file_exist(const char* filename)
{
	FILE* fd = fopen(filename, "rb");
	if (!fd) return (0);
	fclose(fd);
	return (1);
}

/*
void flipVertically(int width, int height, char* data)
{
	char rgb[3];

	for (int y = 0; y < height / 2; ++y)
	{
		for (int x = 0; x < width; ++x)
		{
			int top = (x + y * width) * 3;
			int bottom = (x + (height - y - 1) * width) * 3;

			memcpy(rgb, data + top, sizeof(rgb));
			memcpy(data + top, data + bottom, sizeof(rgb));
			memcpy(data + bottom, rgb, sizeof(rgb));
		}
	}
}
*/
void snapshot()
{
	unsigned char* bmpBuffer;
	time_t tim;
	struct tm tm;
	char buf[15];
	char temppath[80] = { 0 };

	int Width = 0;
	int Height = 0;
	int i = 0;

	RECT rect;

	GetClientRect(win_get_window(), &rect);
	Width = rect.right - rect.left;
	Height = rect.bottom - rect.top;

	memset(&temppath[0], 0, sizeof(temppath));

	//Get time/date stamp
	tim = time(0);
	tm = *localtime(&tim);
	strftime(buf, sizeof buf, "%Y%m%d%H%M%S", &tm);
	puts(buf);
	//////
	bmpBuffer = (unsigned char*)malloc(Width * Height * 4);
	wrlog("Snap width %d , Height %d", Width, Height);

	if (!bmpBuffer)
	{
		wrlog("Error creating buffer for snapshot");
		return;
	}

	glReadPixels((GLint)0, (GLint)0, (GLint)Width, (GLint)Height, GL_RGBA, GL_UNSIGNED_BYTE, bmpBuffer);

	//Flip Texture
	int width_in_bytes = Width * STBI_rgb_alpha;
	unsigned char* top = NULL;
	unsigned char* bottom = NULL;
	unsigned char temp = 0;
	int half_height = Height / 2;

	for (int row = 0; row < half_height; row++)
	{
		top = bmpBuffer + row * width_in_bytes;
		bottom = bmpBuffer + (Height - row - 1) * width_in_bytes;
		for (int col = 0; col < width_in_bytes; col++)
		{
			temp = *top;
			*top = *bottom;
			*bottom = temp;
			top++;
			bottom++;
		}
	}

	//Make Full PATH
	// Add test here to make sure the SNAP Directory exists.
	strcat(temppath, "snap\\");
	strcat(temppath, driver[gamenum].name);
	strcat(temppath, "_");
	strcat(temppath, buf);
	strcat(temppath, ".png");
	strcat(temppath, "\0");
	LOG_DEBUG("Saving Snapshot: %s", temppath);

	stbi_write_png(temppath, Width, Height, 4, bmpBuffer, Width * 4);
	free(bmpBuffer);
}

GLuint load_texture(const char* filename, const char* archname, int numcomponents, int filter)
{
	GLuint tex = 0;
	int width = 0;
	int height = 0;
	int comp = 0;
	unsigned char* image_data = 0;
	unsigned char* raw_data = 0;
	mz_zip_archive_file_stat* file_stat = 0;

	//Stupid
	stbi_set_flip_vertically_on_load(1);

	//If no archive name is supplied, it's assumed the file is being loaded from the file system.
	if (!archname)
	{
		image_data = stbi_load(filename, &width, &height, &comp, STBI_rgb_alpha);
	}
	else
	{
		//Otherwise load with our generic zip functionality
		raw_data = load_zip_file(archname, filename);
		image_data = stbi_load_from_memory(raw_data, (int)(get_last_zip_file_size()), &width, &height, &comp, STBI_rgb_alpha);
	}
	if (!image_data) {
		wrlog("ERROR: could not load %s\n", filename);
		//	return 0;
	}
	wrlog("Texture x is %d, y is %d, components %d", width, height, comp);
	// NPOT check, we checked the card capabilities beforehand, flag if it's not npot

	if ((width & (width - 1)) != 0 || (height & (height - 1)) != 0) {
		wrlog("WARNING: texture %s is not power-of-2 dimensions\n", filename);
	}

	//Create Texture

	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	// OLD OpenGL 2.0 style binding
	glTexImage2D(
		GL_TEXTURE_2D,
		0,
		GL_RGBA8,
		width,
		height,
		0,
		GL_RGBA,
		GL_UNSIGNED_BYTE,
		image_data
	);

	//glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, width, height);
	//glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, image_data);
	//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	//Check for errors
	//check_gl_error();

	//The below gluBuild2DMipmaps is ONLY for maximum compatibility using OpenGL 1.4 - 2.0.
	//In a real program, you should be using GLEW or some other library to allow glGenerateMipmap to work.
	// it's much better,and you should be using an OpenGL 3+ context anyways. This is just for my example program.
	//gluBuild2DMipmaps(GL_TEXTURE_2D, 4, width, height, GL_RGBA, GL_UNSIGNED_BYTE, image_data); //Never use this in anything but an opengl 2 demo.
	//This is openGL3 now so:
	//glGenerateMipmap(GL_TEXTURE_2D); //Only if using opengl 3+, next example...

	//Now that we're done making a texture, free the image data
	stbi_image_free(image_data);
	free(raw_data);
	//set image data properties

	wrlog("New Texture created:");
	wrlog("Texture ID: %d", tex);
	return tex;
}