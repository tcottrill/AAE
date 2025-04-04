#include <stdio.h>
#include <stdlib.h>
#include "aae_fileio.h"
#include "log.h"
#include "sha-1.h"

//For zip file loading
#include "miniz.h"

CSHA1 sha1;

/*
const char* artpath = "artwork\\";
const char* samplepath = "samples\\";
const char* rompath = "roms\\";
*/
unsigned int filesz = 0;
unsigned int uncomp_size = 0;

unsigned int get_last_file_size()
{
	return filesz;
}

unsigned int get_last_zip_file_size()
{
	return  uncomp_size;
}

int getFileSize(FILE* input)
{
	int fileSizeBytes;

	fseek(input, 0, SEEK_END);
	fileSizeBytes = ftell(input);
	fseek(input, 0, SEEK_SET);
	return fileSizeBytes;
}

unsigned char* load_file(const char* filename)
{
	uint8_t* buf = 0;
	filesz = 0;

	wrlog("Opening File");
	FILE* fd = fopen(filename, "rb");
	if (!fd) { wrlog("Filed to find file!! %s.", filename); return (0); }

	filesz = getFileSize(fd);
	buf = (unsigned char*)malloc(filesz);
	fread(buf, 1, filesz, fd);
	fclose(fd);
	return buf;
}

int save_file_char(const char* filename, unsigned char* buf, int size)
{
	FILE* fd = NULL;
	if (!(fd = fopen(filename, "w"))) { wrlog("Saving Ram failed!"); return (0); }
	fwrite(buf, size, 1, fd);
	fclose(fd);
	return (1);
}

int save_file(const char* filename, unsigned char* buf, int size)
{
	FILE* fd = fopen(filename, "wb");
	if (!fd) { wrlog("Filed to save file %s.", filename); return false; }

	fwrite(buf, size, 1, fd);
	fclose(fd);
	return 1;
}


/*
int load_roms(const struct RomModule* p, const char* basename)
{
	int test = 0;
	int skip = 0;

	mz_bool status;
	mz_uint file_index = -1;
	mz_zip_archive zip_archive;
	mz_zip_archive_file_stat file_stat;

	unsigned char* zipdata = 0;
	const char* shatest = 0;
	int ret = 0;
	int i, j = 0;
	int crc = 0;
	int size = 0;
	int cpunum = 0;
	int flipnum = 0;
	int region = 0;
	char temppath[255];

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
	wrlog("Opening Romset at:, %s", temppath);

	memset(&zip_archive, 0, sizeof(zip_archive));
	status = mz_zip_reader_init_file(&zip_archive, temppath, 0);
	if (!status)
	{
		wrlog("Zip File %s failed. %d(Archive not found?)", basename, EXIT_FAILURE);
		ret = EXIT_FAILURE;
		goto end;
	}
	//Start Loading ROMS HERE////
	//wrlog("starting with romsize = %d romsize 1 = %d",p[0].romSize,p[1].romSize);
	for (i = 0; p[i].romSize > 0; i += 1)
	{
		//   Check for ROM_REGION: IF SO, decode and skip, also reset the even/odd counter:
		if (p[i].loadAddr == ROM_REGION_START)
		{
			//Allocate Memory for this region
			cpu_mem(p[i].loadtype, p[i].romSize);
			cpunum = p[i].loadtype;
			flipnum = 0;
		}
		else {
			// Find the requested file, ignore case
			//Is it a ROM_RELOAD? Then use previous filename.
			if (p[i].filename == (char*)-1)
			{
				file_index = mz_zip_reader_locate_file(&zip_archive, p[i - 1].filename, 0, 0);
			}
			else
			{
				file_index = mz_zip_reader_locate_file(&zip_archive, p[i].filename, 0, 0);
			}

			if (file_index == -1)
			{
				wrlog("File not found in zip: %s", p[i].filename);
				ret = EXIT_FAILURE;
				goto end;
			}
			else
			{
				wrlog("File found, continuing");
			}
			//We have a valid file, lets tell everyone.
			if (p[i].filename == (char*)-1) { wrlog("Loading Rom: %s", p[i - 1].filename); }
			else { wrlog("Loading Rom: %s", p[i].filename); }

			// Get information on the current file
			status = mz_zip_reader_file_stat(&zip_archive, file_index, &file_stat);
			if (status != MZ_TRUE) { ret = EXIT_FAILURE; goto end; }
			wrlog("getting file size");
			//Get File Size
			uncomp_size = (size_t)file_stat.m_uncomp_size;

			// Size mismatch?
			if (p[i].romSize != file_stat.m_uncomp_size) { ret = EXIT_FAILURE; goto end; }
			wrlog("Passed size check");

			zipdata = (unsigned char*)malloc(p[i].romSize);

			// Read (decompress) the file
			status = mz_zip_reader_extract_to_mem(&zip_archive, file_index, zipdata, uncomp_size, 0);
			if (status != MZ_TRUE) { ret = EXIT_FAILURE; goto end; }
			wrlog("Passed file extract");
			if (p[i].filename != (char*)-1 && p[i].filename != (char*)-2)
			{
				//Print the SHA1 Checksum of the data
				shatest = sha1.CalculateHash(zipdata, uncomp_size);
				if (strcmp(shatest, p[i].sha) != 0) { wrlog("File SHA-1 Check Mismatch. This file may not work."); }
				wrlog("SHA %s", shatest);
				//CRC Check
				crc = (int)file_stat.m_crc32;
				if (crc != p[i].crc) { wrlog("CRC error on rom file. It may be corrupt."); }
				wrlog("CRC %x and %x", crc, p[i].crc);
				//This is just to display the ROM LOADING Message
			}
			//This is for ROM CONTINUE Support
			if (p[i].filename == (char*)-2) { skip = p[i - 1].romSize; }
			else skip = 0;

			//Get the memory region for the current CPU number
			int region = cpunum;// Machine->drv->cpu[cpunum].memory_region;

			switch (p[i].loadtype)
			{
			case ROM_LOAD_NORMAL:
			{
				for (j = skip; j < p[i].romSize; j++)
				{
					Machine->memory_region[region][j + p[i].loadAddr] = zipdata[j];
				}
				break;
			}
			case ROM_LOAD_16B:
			{
				for (j = 0; j < p[i].romSize; j += 1) //Even odd based on flipnum
				{
					Machine->memory_region[region][(j * 2 + flipnum) + p[i].loadAddr] = zipdata[j];
				}
				break;
			}

			default: wrlog("Something bad happened in the ROM Loading Routine"); break;
			}
			//Finished loading Roms
			free(zipdata);
		}
		if (p[i].loadtype) flipnum ^= 1; //If it's ROM_LOAD_16B, flip the value
	}

end:
	// Close the archive
	mz_zip_reader_end(&zip_archive);

	wrlog("Finished loading roms");

	if (ret == EXIT_SUCCESS)
	{
		wrlog("Zip file loaded Successfully");
		return 0;
	}
	else
	{
		wrlog("Zip file failed to load!");
		return 1;
	}

	return 1;
}

*/


/*

bool file_exists(const char * filename) {
	struct stat fileinfo;

	return !stat(filename, &fileinfo);
}

BOOL DirectoryExists(const char* dirName) {
	DWORD attribs = ::GetFileAttributesA(dirName);
	if (attribs == INVALID_FILE_ATTRIBUTES) {
		return false;
	}
	return (attribs & FILE_ATTRIBUTE_DIRECTORY);
}
*/
// ToDo: Add a debug clause in front of the logging to disable it
unsigned char* load_zip_file(const char* archname, const char* filename)
{
	mz_bool status;
	mz_uint file_index = -1;
	mz_zip_archive zip_archive;
	mz_zip_archive_file_stat file_stat;

	unsigned char* buf = 0;
	int ret = 1; // Zero Means the file didn't load, 1 Means everything is hunky dory. We start with one.

	wrlog("Opening Archive %s", archname);
	// Now try to open the archive.
	memset(&zip_archive, 0, sizeof(zip_archive));
	status = mz_zip_reader_init_file(&zip_archive, archname, 0);
	if (!status) { wrlog("Zip Archive %s not found. (Check your path?)", archname); ret = 0; goto end; }

	// Find the requested file, ignore case
	file_index = mz_zip_reader_locate_file(&zip_archive, filename, 0, 0);
	if (file_index == -1) { wrlog("Error: File %s not found in Zip Archive %s", filename, archname); ret = 0; goto end; }

	// Get information on the current file
	status = mz_zip_reader_file_stat(&zip_archive, file_index, &file_stat);
	if (status != MZ_TRUE) { wrlog("Error reading Zip File Info, it's probably corrupt"); ret = 0; goto end; }

	//Fill in the size in case we need to get it later
	uncomp_size = (unsigned int)file_stat.m_uncomp_size;

	//Create the unsigned char buffer
	buf = (unsigned char*)malloc(uncomp_size);
	if (!buf) { wrlog("Failed to create char buffer, mem error?"); ret = 0; goto end; }

	// Read (decompress) the file
	status = mz_zip_reader_extract_to_mem(&zip_archive, file_index, buf, uncomp_size, 0);
	if (status != MZ_TRUE) { wrlog("Failed to extract zip file to mem for some weird reason"); ret = 0; goto end; }

end:
	// Close the archive
	//wrlog("Closing Archive");
	mz_zip_reader_end(&zip_archive);

	if (ret) { wrlog("Zip file loaded Successfully: %d from archive %s", filename, archname); }
	else { wrlog("Zip file %d in archive %s failed to load!", filename, archname); return 0; }

	return buf;
}

// ToDo: Add a debug clause in front of the logging to disable it
bool save_zip_file(const char* archname, const char* filename, unsigned char* data)
{
	bool status;

	status = mz_zip_add_mem_to_archive_file_in_place(archname, filename, data, strlen((char*)data) + 1, 0, 0, MZ_BEST_COMPRESSION);
	if (!status)
	{
		wrlog("mz_zip_add_mem_to_archive_file_in_place failed!");
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}