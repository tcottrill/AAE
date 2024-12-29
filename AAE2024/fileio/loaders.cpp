#include "stdio.h"
#include "loaders.h"
#include <time.h>
#include <allegro.h>
#include "alleggl.h"
#include "winalleg.h"
#include "gameroms.h"
#include "aae_mame_driver.h"
#include "samples.h"
//#include "acommon.h"

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

void cpu_mem(int cpunum, int size)
{
	wrlog("Allocating Game Memory, Cpu# %d Amount 0x%x", cpunum, size);
	if (GI[cpunum]) { free(GI[cpunum]); GI[cpunum] = NULL; }
	GI[cpunum] = (unsigned char*)malloc(size);
	if (GI[cpunum] == NULL) { wrlog("Can't allocate system ram for Cpu Emulation! - This is bad."); exit(1); }
	memset(GI[cpunum], 0, size);
	//wrlog("Memory Allocation Completed");
}

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

int load_roms(const char* archname, const struct RomModule* p)
{
	int test = 0;
	int skip = 0;
	char temppath[255];
	const char* rompath = "roms\\";
	unsigned char* zipdata = 0;
	int ret = 0;
	int i, j = 0;

	int size = 0;
	int cpunum = 0;
	int flipnum = 0;

	strcpy(temppath, config.exrompath);
	put_backslash(temppath);
	strcat(temppath, archname);
	strcat(temppath, ".zip");

	test = file_exist(temppath);
	if (test == 0)
	{
		strcpy(temppath, rompath);
		strcat(temppath, archname);
		strcat(temppath, ".zip\0");
	}
	//wrlog("Opening, %s", temppath);

	/// Start Loading ROMS HERE ///

	for (i = 0; p[i].romSize > 0; i += 1)
	{
		//   Check for ROM_REGION: IF SO, decode and skip, also reset the even/odd counter:
		if (p[i].loadAddr == 0x999) { cpu_mem(p[i].loadtype, p[i].romSize); cpunum = p[i].loadtype; flipnum = 0; }
		// else load a rom
		else {
			// Find the requested file, ignore case
			//Is it a ROM_RELOAD? Then use previous filename (there better be one, no checking here)
			if (p[i].filename == (char*)-1)
			{
				wrlog("Loading Rom: %s", p[i - 1].filename);
				zipdata = load_zip_file(p[i - 1].filename, temppath);
			}
			else
			{
				wrlog("Loading Rom: %s", p[i].filename);
				zipdata = load_zip_file(p[i].filename, temppath);
			}
			if (!zipdata)
			{
				wrlog("file not found in zip,%s", p[i].filename);
				if (!in_gui)
				{
					wrlog("A required Rom was not found in zip.\nUse aae gamename -verifyroms to audit your romset.");
				}
				return (0);
			}
			//This is for ROM CONTINUE Support
			if (p[i].filename == (char*)-2) { skip = p[i - 1].romSize; }
			else skip = 0;

			switch (p[i].loadtype)
			{
			case 0:
				for (j = skip; j < p[i].romSize; j++)
				{
					GI[cpunum][j + p[i].loadAddr] = zipdata[j];
				}
				break;

			case 1:
				for (j = 0; j < p[i].romSize; j += 1) //Even odd based on flipword
				{
					GI[cpunum][(j * 2 + flipnum) + p[i].loadAddr] = zipdata[j];
				}
				break;

			default: wrlog("Something bad happened in the ROM Loading Routine"); break;
			}
			//Finished loading Rom
			free(zipdata);
		}
		if (p[i].loadtype) flipnum ^= 1;
	} // Close the archive

	wrlog("Finished loading roms");
	return (1);
}

int load_samples(const char** p, int val)
{
	char temppath[MAX_PATH];
	unsigned char* zipdata = 0;
	int ret = 0;
	int i = 1;
	int size = 0;

	//if (strcmp(p[0], "nosamples.zip") == 0) { return 1; } //No samples, bye!

	strcpy(temppath, "samples\\");//if it's not there, try sample dir
	strcat(temppath, p[0]);
	strcat(temppath, "\0");
	do
	{
		wrlog("Loading Sample %s", p[i]);
		zipdata = load_zip_file((const char*)p[i], temppath);

		if (!zipdata) {
			wrlog("Couldn't find the sound %s.\n Will use silence for that sample.", p[i]);
			if (val == 0)
			{
				game_sounds[i - 1] = create_sample(8, 1, 22000, 220);
				memset((short*)game_sounds[(i - 1)]->data, 0, 220);
			}
			else
			{
				game_sounds[(num_samples + i)] = create_sample(8, 1, 22000, 220);
				memset((short*)game_sounds[(num_samples + i)]->data, 0, 220);
			}
		}
		else {
			//write out temp file
			if (zipdata)
			{
				save_file("temp.wav", zipdata, get_last_zip_file_size());
				free(zipdata);
			}

			if (val == 0) { game_sounds[(i - 1)] = load_sample("temp.wav"); }
			else { game_sounds[(num_samples + (i - 1))] = load_sample("temp.wav"); }
			remove("temp.wav");
		}
		i++;
	} while (p[i]);

	// Close the archive

	if (val == 0) { num_samples = (i - 1); }
	else { num_samples = ((num_samples + i) - 1); }

	return (i - 1);
}

int load_ambient()
{
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

	return 1;
}

int load_hi_aae(int start, int size, int image)
{
	int num = 0;
	char fullpath[180];
	FILE* fd;
	membuffer = (unsigned char*)malloc(size);
	memset(membuffer, 0, size);

	strcpy(fullpath, "hi");
	strcat(fullpath, "\\");
	strcat(fullpath, driver[gamenum].name);
	strcat(fullpath, ".aae");

	fd = fopen(fullpath, "rb");
	if (!fd) { wrlog("Hiscore / nvram file for this game not found"); return 1; }
	wrlog("Loading Hiscore table / nvram");
	fread(membuffer, 1, size, fd);

	memcpy((GI[image] + start), membuffer, size);
	fclose(fd);
	free(membuffer);
	return 0;
}

int save_hi_aae(int start, int size, int image)
{
	int num = 0;
	char fullpath[180];
	membuffer = (unsigned char*)malloc(size + 3);

	memset(membuffer, 0, size);
	strcpy(fullpath, "hi");
	strcat(fullpath, "\\");
	strcat(fullpath, driver[gamenum].name);
	strcat(fullpath, ".aae");
	wrlog("Saving Hiscore table / nvram");
	memcpy(membuffer, (GI[image] + start), size);
	save_file(fullpath, membuffer, size);
	free(membuffer);
	return 0;
}

int verify_rom(const char* archname, const struct RomModule* p, int romnum)
{
	return 1;
}

int verify_sample(const char** p, int num)
{
	return 1;
}

int file_exist(char* filename)
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
		raw_data = load_zip_file(filename, archname);
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
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
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