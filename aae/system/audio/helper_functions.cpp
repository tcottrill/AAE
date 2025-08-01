#include "helper_functions.h"

using namespace std;

static void byteswap(unsigned char& byte1, unsigned char& byte2)
{
	byte1 ^= byte2;
	byte2 ^= byte1;
	byte1 ^= byte2;
}

std::string remove_extension2(const std::string& path)
{
	if (path == "." || path == "..")
		return path;

	size_t pos = path.find_last_of("\\/.");
	if (pos != std::string::npos && path[pos] == '.')
		return path.substr(0, pos);

	return path;
}

std::string remove_extension(const std::string& filename)
{
	size_t lastdot = filename.find_last_of(".");
	if (lastdot == std::string::npos) return filename;
	return filename.substr(0, lastdot);
}

std::string getFileName(std::string filePath, bool withExtension = true, char seperator = '/')
{
	// Get last dot position
	std::size_t dotPos = filePath.rfind('.');
	std::size_t sepPos = filePath.rfind(seperator);

	if (sepPos != std::string::npos)
	{
		return filePath.substr(sepPos + 1, filePath.size() - (withExtension || dotPos != std::string::npos ? 1 : dotPos));
	}
	else return "";
}

std::string dirnameOf(const std::string& fname)
{
	size_t pos = fname.find_last_of("\\/");
	return (std::string::npos == pos)
		? ""
		: fname.substr(0, pos);
}

std::string base_name(const std::string& path)
{
	return path.substr(path.find_last_of("/\\") + 1);
}

bool ends_with(const std::string& s, const std::string& ending)
{
	return (s.size() >= ending.size()) && equal(ending.rbegin(), ending.rend(), s.rbegin());
}