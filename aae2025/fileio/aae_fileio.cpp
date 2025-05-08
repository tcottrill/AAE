#include <stdio.h>
#include <stdlib.h>
#include "aae_fileio.h"
#include "log.h"
//For zip file loading
#include "miniz.h"
#include <string>
#include "aae_mame_driver.h"
#include "memory.h"
#include "path_helper.h"
#include "loaders.h"
#include "gameroms.h"
#include "sha-1.h"



/*
const char* artpath = "artwork\\";
const char* samplepath = "samples\\";
const char* rompath = "roms\\";
*/
CSHA1 sha1;
unsigned int filesz = 0;
unsigned int uncomp_size = 0;
uint32_t last_zip_crc = 0;

int get_last_crc()
{
	return last_zip_crc;
}

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

int save_file_char(const char* filename, char* buf, int size)
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


// 5/6/25 - 5/8/25:
//  Added ROM_CONTINUE support
//  Added better ROM_RELOAD support
//  Added NON-MAME crc and SHA-1 checking. 
//  Not the cleanest code, but it's working. 

int load_roms(const char* archname, const struct RomModule* p)
{
	mz_bool status;
	mz_uint file_index = -1;
	mz_zip_archive zip_archive;
	mz_zip_archive_file_stat file_stat;
	std::string temppath;
	unsigned char* zipdata = 0;
	const char* shatest = 0;
	const char* last_reload_filename = nullptr;
	int test = 0;
	int skip = 0;
	int ret = 0;
	int i, j = 0;
	int crc = 0;
	int size = 0;
	int cpunum = 0;
	int region = 0;

	temppath = config.exrompath;
	temppath.append("\\");
	temppath.append(archname);
	temppath.append(".zip");

	test = file_exist(temppath.c_str());
	if (test == 0)
	{
		wrlog("Rom not found in config directory");
		temppath = getpathM("roms", 0);
		temppath.append("\\");
		temppath.append(archname);
		temppath.append(".zip");
	}
	wrlog("Rom Path: %s", temppath.c_str());

	memset(&zip_archive, 0, sizeof(zip_archive));
	status = mz_zip_reader_init_file(&zip_archive, temppath.c_str(), 0);
	if (!status)
	{
		wrlog("Zip File %s failed. %d(Archive not found?)", archname, EXIT_FAILURE);
		ret = EXIT_FAILURE;
		goto end;
	}
	//Start Loading ROMS HERE////
	wrlog("ROM_START(%s)", archname);
	//wrlog("starting with romsize = %d romsize 1 = %d",p[0].romSize,p[1].romSize);
	for (i = 0; p[i].romSize > 0; i += 1)
	{
		//   Check for ROM_REGION: IF SO, decode and skip
		if (p[i].loadAddr == ROM_REGION_START)
		{
			// This is temporary, to help me build romsets with the correct SHA without typing
			//wrlog("ROM_REGION(0x%04x, %s)", p[i].romSize, rom_regions[p[i].loadtype]);
			//Allocate Memory for this region
			new_memory_region(p[i].loadtype, p[i].romSize);
			cpunum = p[i].loadtype;
		}
		else {
			// Find the requested file, ignore case
			// Is this ROM_CONTINUE, then skip to bottom.
			if (p[i].filename == (char*)-2) { goto gohere; }
			//Is it a ROM_RELOAD? Then use previous stored filename. Here we go!
			if (p[i].filename == (char*)-1)
			{
				if (last_reload_filename == 0) // No Previously stored filename?
				{
					last_reload_filename = p[i - 1].filename; // Set it!
				}
				file_index = mz_zip_reader_locate_file(&zip_archive, last_reload_filename, 0, 0);
				////// TEMP PRINTING
				//wrlog("ROM_RELOAD(0x%04x, 0x%04x)", p[i].loadAddr, p[i].romSize);
			}
			else
			{
				// Were loading normally, so lets take this opportunity to reset the rom reload filename for reuse. 
				last_reload_filename = nullptr;
				file_index = mz_zip_reader_locate_file(&zip_archive, p[i].filename, 0, 0);
			}

			if (file_index == -1)
			{
				wrlog("File not found in zip: %s", p[i].filename);
				ret = EXIT_FAILURE;
				goto end;
			}

			//We have a valid file, lets tell everyone.
			if (config.debug_profile_code) {
				if (p[i].filename == (char*)-1)
					wrlog("Loading Rom: %s", p[i - 1].filename);
				else
					wrlog("Loading Rom: %s", p[i].filename);
			}
			// Get information on the current file
			status = mz_zip_reader_file_stat(&zip_archive, file_index, &file_stat);
			if (status != MZ_TRUE) { wrlog("Could not read file in Zip, corrupt?"); ret = EXIT_FAILURE; goto end; }

			//Get File Size
			uncomp_size = (unsigned int)file_stat.m_uncomp_size;

			// Size mismatch?
			if (p[i].romSize != file_stat.m_uncomp_size) 
			{
				// Check if ROM_CONTINUE is screwing things up
				if (p[i + 1].filename != (char*)-2)
				{
					wrlog("Warning: File Size Mismatch, check your rom definition and romset.");
					ret = EXIT_FAILURE; goto end; 
				}
			}

			zipdata = (unsigned char*)malloc(uncomp_size);//p[i].romSize); // We don't use romSize here because of ROM_CONTINUE
			// Read (decompress) the file
			status = mz_zip_reader_extract_to_mem(&zip_archive, file_index, zipdata, uncomp_size, 0);
			if (status != MZ_TRUE) { wrlog("File Failed to Extract"); ret = EXIT_FAILURE; goto end; }
			//wrlog("Passed file extract");
			if (p[i].filename != (char*)-1 && p[i].filename != (char*)-2)
			{
				//SHA-1 Check
				shatest = sha1.CalculateHash(zipdata, uncomp_size);
				if (p[i].sha)
				{
					if (strcmp(shatest, p[i].sha) != 0)
					{
						wrlog("File SHA-1 Check Mismatch. This file may not work.\nSHA: %x File: %x", shatest, p[i].sha);
					}
				}
				else
				{   // Temporarily print out the rom replacement.
					//wrlog("ROM_LOAD(\"%s\", 0x%04x, 0x%04x, CRC(%x) SHA1(%s))", p[i].filename, p[i].loadAddr, p[i].romSize, (int)file_stat.m_crc32, shatest);
				}
				//CRC Check
				crc = (int)file_stat.m_crc32;
				if (crc != p[i].crc)
				{
					if (p[i].crc) wrlog("CRC error on rom file. It may be corrupt.\nCRC %x and %x", crc, p[i].crc);
				}
			}

			region = cpunum;// Machine->drv->cpu[cpunum].memory_region;

		gohere:
			//This is for ROM CONTINUE Support
			if (p[i].filename == (char*)-2) { skip = p[i - 1].romSize; }
			else skip = 0;
			//Get the memory region for the current CPU number

			switch (p[i].loadtype)
			{
			case ROM_LOAD_NORMAL:
			{
				for (j = 0; j < p[i].romSize; j++)
				{
					Machine->memory_region[region][j + p[i].loadAddr] = zipdata[j + skip];
				}
				break;
			}
			case ROM_LOAD_16:
			{
				for (j = 0; j < p[i].romSize; j++)
				{
					Machine->memory_region[region][(j * 2) + p[i].loadAddr] = zipdata[j];
				}
				break;
			}

			default: wrlog("Something bad happened in the ROM Loading Routine"); break;
			}
			//Finished loading Rom
			//wrlog("ROM Loaded");
			if (p[i + 1].filename != (char*)-2)
			{
				free(zipdata);
			}
		}
	}

end:
	// Close the archive
	mz_zip_reader_end(&zip_archive);
	//	wrlog("ROM_END");                   //// TEMPORARY FOR BUILDING ROMSETS
	wrlog("Finished loading roms");

	if (ret == EXIT_SUCCESS)
	{
		if (config.debug_profile_code) { wrlog("Zip file loaded Successfully"); }
		return EXIT_SUCCESS;
	}
	else
	{
		wrlog("Zip file failed to load!");
		return EXIT_FAILURE;
	}
	return EXIT_FAILURE;
}

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