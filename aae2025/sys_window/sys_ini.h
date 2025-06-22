#ifndef INI_H
#define INI_H

#include <string>

void SetIniFile(const char* szFileName);

int   get_config_int(const char* szSection, const char* szKey, int iDefaultValue);
float get_config_float(const char* szSection, const char* szKey, float fltDefaultValue);
bool  get_config_bool(const char* szSection, const char* szKey, bool bolDefaultValue);
char* get_config_string(const char* szSection, const char* szKey, const char* szDefaultValue); // Caller must free()

void set_config_int(const char* szSection, const char* szKey, int iValue);
void set_config_float(const char* szSection, const char* szKey, float fltValue);
void set_config_bool(const char* szSection, const char* szKey, bool bolValue);
void set_config_string(const char* szSection, const char* szKey, const char* szValue);

// std string overloads

int   get_config_int(const std::string& section, const std::string& key, int defaultValue);
float get_config_float(const std::string& section, const std::string& key, float defaultValue);
bool  get_config_bool(const std::string& section, const std::string& key, bool defaultValue);
std::string get_config_string(const std::string& section, const std::string& key, const std::string& defaultValue);

void set_config_int(const std::string& section, const std::string& key, int value);
void set_config_float(const std::string& section, const std::string& key, float value);
void set_config_bool(const std::string& section, const std::string& key, bool value);
void set_config_string(const std::string& section, const std::string& key, const std::string& value);
#endif // INI_H