#include "sys_ini.h"
#include <iostream>
#include <Windows.h>
#include "log.h"

#define MAX_INI 255

#pragma warning (disable : 4996 )
#pragma warning (disable : 4244 )

char m_szFileName[MAX_INI];


void SetIniFile(const char* szFileName)
{
 memset(m_szFileName, 0x00, MAX_INI);
 memcpy(m_szFileName, szFileName, strlen(szFileName));
}


int get_config_int(const char* szSection, const char* szKey, const int iDefaultValue)
{
 int iResult = GetPrivateProfileIntA(szSection,  szKey, iDefaultValue, m_szFileName); 
 return iResult;
}


float get_config_float(const char* szSection, const char* szKey, const float fltDefaultValue)
{
 char szResult[255];
 char szDefault[255];
 float fltResult;
 sprintf(szDefault, "%f",fltDefaultValue);
 GetPrivateProfileStringA(szSection,  szKey, szDefault, szResult, MAX_INI, m_szFileName); 
 fltResult =  atof(szResult);
 return fltResult;
}


bool get_config_bool(const char* szSection, const char* szKey, const bool bolDefaultValue)
{
 char szResult[MAX_INI];
 char szDefault[MAX_INI];
 bool bolResult;
 sprintf(szDefault, "%s", bolDefaultValue? "True" : "False");
 GetPrivateProfileStringA(szSection, szKey, szDefault, szResult, MAX_INI, m_szFileName); 
 bolResult =  (strcmp(szResult, "True") == 0 || 
		strcmp(szResult, "true") == 0) ? true : false;
 return bolResult;
}


char* get_config_string(const char* szSection, const char* szKey, const char* szDefaultValue)
{
 char* szResult = new char[MAX_INI];
 memset(szResult, 0x00, MAX_INI);
 GetPrivateProfileStringA(szSection,  szKey, 
		szDefaultValue, szResult, MAX_INI, m_szFileName); 
 return szResult;
}


void set_config_int(const char* szSection, const char* szKey, const int iValue)
{
 char szValue[MAX_INI];
 sprintf(szValue, "%d", iValue);
 WritePrivateProfileStringA(szSection,  szKey, szValue, m_szFileName); 
}


void set_config_float(const char* szSection, const char* szKey, const float fltValue)
{
 char szValue[MAX_INI];
 sprintf(szValue, "%f", fltValue);
 WritePrivateProfileStringA(szSection,  szKey, szValue, m_szFileName); 
}


void set_config_bool(const char* szSection, const char* szKey, const bool bolValue)
{
 char szValue[MAX_INI];
 sprintf(szValue, "%s", bolValue ? "True" : "False");
 WritePrivateProfileStringA(szSection,  szKey, szValue, m_szFileName); 
}


void set_config_string(const char* szSection, const char* szKey, const char* szValue)
{
 WritePrivateProfileStringA(szSection,  szKey, szValue, m_szFileName);
}