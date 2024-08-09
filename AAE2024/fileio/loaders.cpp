#include "stdio.h"
#include "loaders.h"
#include <time.h>
#include <allegro.h>
#include "alleggl.h"
#include "winalleg.h"
#include "../gameroms.h"
#include "../aae_mame_driver.h"
//#include "sndhrdw/samples.h"
//#include "acommon.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
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

int load_roms(const char* archname, const struct RomModule p[])
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
	wrlog("Opening, %s", temppath);

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

int load_samples(const char* p[], int val)
{
	char temppath[255];
	unsigned char* zipdata = 0;
	int ret = 0;
	int i = 1;
	int size = 0;

	if (strcmp(p[0], "nosamples.zip") == 0) { return 1; } //No samples, bye!

	strcpy(temppath, "samples\\");//if it's not there, try sample dir
	strcat(temppath, p[0]);
	strcat(temppath, "\0");

	wrlog("Opening, %s", temppath);

	do
	{
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

int verify_rom(const char* archname, const struct RomModule p[], int romnum)
{
	return 1;
}

int verify_sample(const char* p[], int num)
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

void snapshot(const char* filename)
{
	BITMAP* tempbmp;
	BYTE* bmpBuffer;
	//BYTE* copybuffer;
	FILE* filePtr;
	time_t tim;
	struct tm tm;
	char buf[15];
	char temppath[80];
	char temppathp[80];
	int windowWidth = 0;
	int windowHeight = 0;
	int i = 0;
	BITMAPFILEHEADER bitmapFileHeader;
	BITMAPINFOHEADER bitmapInfoHeader;
	///////////////////////////////////
	windowWidth = SCREEN_W + 1;
	windowHeight = SCREEN_H + 1;
	bitmapFileHeader.bfType = 0x4D42; //"BM"
	bitmapFileHeader.bfSize = windowWidth * windowHeight * 3;
	bitmapFileHeader.bfReserved1 = 0;
	bitmapFileHeader.bfReserved2 = 0;
	bitmapFileHeader.bfOffBits =
		sizeof(bitmapFileHeader) + sizeof(bitmapInfoHeader);

	bitmapInfoHeader.biSize = sizeof(bitmapInfoHeader);
	bitmapInfoHeader.biWidth = windowWidth - 1;
	bitmapInfoHeader.biHeight = windowHeight - 1;
	bitmapInfoHeader.biPlanes = 1;
	bitmapInfoHeader.biBitCount = 24;
	bitmapInfoHeader.biCompression = BI_RGB;
	bitmapInfoHeader.biSizeImage = 0;
	bitmapInfoHeader.biXPelsPerMeter = 0; // ?
	bitmapInfoHeader.biYPelsPerMeter = 0; // ?
	bitmapInfoHeader.biClrUsed = 0;
	bitmapInfoHeader.biClrImportant = 0;

	//Get time/date stamp
	tim = time(0);
	tm = *localtime(&tim);
	strftime(buf, sizeof buf, "%Y%m%d%H%M%S", &tm);
	puts(buf);
	//////
	bmpBuffer = (BYTE*)malloc(windowWidth * windowHeight * 3);
	//copybuffer = (BYTE*)malloc(sizeof(bitmapFileHeader) + sizeof(bitmapInfoHeader) + (windowWidth * windowHeight * 3));

	if (!bmpBuffer)
		return;

	glReadPixels((GLint)0, (GLint)0, (GLint)windowWidth - 1, (GLint)windowHeight - 1, GL_BGR, GL_UNSIGNED_BYTE, bmpBuffer);
	//Make Full PATH
	strcpy(temppath, "snap\\");
	strcat(temppath, driver[gamenum].name);
	strcat(temppath, buf);
	strcat(temppath, ".bmp");
	strcat(temppath, "\0");
	wrlog("Saving Snapshot: %s", temppath);

	//int saved = stbi_write_png(filename, windowWidth, windowHeight, 3, bmpBuffer, 0);

	filePtr = fopen(temppath, "wb");
	if (!filePtr)
		return;

	fwrite(&bitmapFileHeader, sizeof(bitmapFileHeader), 1, filePtr);
	fwrite(&bitmapInfoHeader, sizeof(bitmapInfoHeader), 1, filePtr);
	fwrite(bmpBuffer, windowWidth * windowHeight * 3, 1, filePtr);
	fclose(filePtr);
	free(bmpBuffer);
}