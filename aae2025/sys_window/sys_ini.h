#ifndef INI_H
#define INI_H

 void SetIniFile(const char* szFileName);

 int get_config_int(const char* szSection, const char* szKey, const int iDefaultValue);
 float get_config_float(const char* szSection, const char* szKey, const float fltDefaultValue);
 bool get_config_bool(const char* szSection, const char* szKey, const bool bolDefaultValue);
 char* get_config_string(const char* szSection, const char* szKey, const char* szDefaultValue);
 
 void set_config_int(const char* szSection, const char* szKey, const int iValue);
 void set_config_float(const char* szSection, const char* szKey, const float fltValue);
 void set_config_bool(const char* szSection, const char* szKey, const bool bolValue);
 void set_config_string(const char* szSection, const char* szKey, const char* szValue);
 

#endif // INI_H