// New code update 7/17/2026
// Updated 6/22/2025 for secure functions and bugfixes
// Removed dependency on legacy Windows functions. 
// Added string functions for other uses. 
// Some code below was written with the assistance of ChatGPT

/*
This is free and unencumbered software released into the public domain.

Anyone is free to copy, modify, publish, use, compile, sell, or
distribute this software, either in source code form or as a compiled
binary, for any purpose, commercial or non - commercial, and by any
means.

In jurisdictions that recognize copyright laws, the author or authors
of this software dedicate any and all copyright interest in the
software to the public domain.We make this dedication for the benefit
of the public at large and to the detriment of our heirs and
successors.We intend this dedication to be an overt act of
relinquishment in perpetuity of all present and future rights to this
software under copyright law.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

For more information, please refer to < https://unlicense.org/>
*/


#include "iniFile.h"
#include "sys_log.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <map>
#include <vector>
#include <fstream>
#include <sstream>
#include <algorithm>

#define MAX_INI 255

static char m_szFileName[MAX_INI] = { 0 };

struct IniEntry {
    std::string key;
    std::string value;
    std::string original_line;
    bool is_comment = false;
};

static std::map<std::string, std::vector<IniEntry>> ini_data;

static std::string trim(const std::string& s) {
    // NOTE: this used find_first_not_of("") -- an EMPTY charset -- making it
    // a no-op. On CRLF files the trailing \r then survived into every line,
    // so "[section]\r" never matched the [..] test and whole sections were
    // silently unparsed (reads fell back to defaults, and saves appended
    // duplicate keys instead of updating). Trim real whitespace.
    const char* ws = " \t\r\n";
    size_t start = s.find_first_not_of(ws);
    size_t end = s.find_last_not_of(ws);
    return (start == std::string::npos) ? "" : s.substr(start, end - start + 1);
}

void LoadIniFile() {
    ini_data.clear();
    std::ifstream file(m_szFileName);
    if (!file.is_open()) return;

    std::string line, section;
    while (std::getline(file, line)) {
        std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#') {
            ini_data[section].push_back({ "", "", line, true });
        } else if (trimmed.front() == '[' && trimmed.back() == ']') {
            section = trimmed.substr(1, trimmed.size() - 2);
            ini_data[section]; // Ensure section exists
        } else {
            size_t eq = line.find('=');
            if (eq != std::string::npos) {
                std::string key = line.substr(0, eq);
                std::string value = line.substr(eq + 1);

                // Trim both (include \r so CRLF files parse identically)
                key.erase(0, key.find_first_not_of(" \t\r\n"));
                key.erase(key.find_last_not_of(" \t\r\n") + 1);
                value.erase(0, value.find_first_not_of(" \t\r\n"));
                value.erase(value.find_last_not_of(" \t\r\n") + 1);

                // NOTE (2026-07-17): inline comment stripping REMOVED.
                // Comments are FULL-LINE only (';' or '#' at line start).
                // Both characters are legal in Windows paths and in device
                // identity strings; stripping them here silently corrupted
                // values on every reload (device-path assignments came back
                // truncated as "\\?\HID" and never matched again). Numeric
                // values carrying a legacy inline comment still parse fine:
                // atoi/atof stop at the first non-numeric character.
                //
                // Values MAY contain spaces (Windows paths do). Keys must
                // not -- a key with spaces is ambiguous and unreadable.
                if (key.empty() || key.find(' ') != std::string::npos) {
                    ini_data[section].push_back({ "", "", line, true }); // treat as invalid
                }
                else {
                    // Duplicate keys within a section: LAST occurrence wins
                    // (standard ini semantics). This also self-heals files
                    // that accumulated duplicate blocks while the CRLF bug
                    // above kept appending instead of updating -- the stale
                    // first copy is dropped, the user's latest value is kept,
                    // and the next save writes the section back clean.
                    bool updated = false;
                    for (auto& entry : ini_data[section]) {
                        if (!entry.is_comment && entry.key == key) {
                            entry.value = value;
                            entry.original_line = key + "=" + value;
                            updated = true;
                            break;
                        }
                    }
                    if (!updated)
                        ini_data[section].push_back({ key, value, line, false });
                }
            }
            else {
                ini_data[section].push_back({ "", "", line, true });
            }

        }
    }
}

void SaveIniFile() {
    // Atomic save: write to a temp file, then swap it in. The previous
    // in-place rewrite truncated the target first -- a crash, power loss,
    // or full disk mid-write left a gutted config and silently reset every
    // setting to defaults on the next run.
    const std::string tmpname = std::string(m_szFileName) + ".tmp";
    {
        std::ofstream file(tmpname, std::ios::trunc);
        if (!file.is_open()) {
            LOG_ERROR("SaveIniFile: cannot open temp file '%s'", tmpname.c_str());
            return;
        }

        for (const auto& sec : ini_data) {
            if (!sec.first.empty())
                file << "[" << sec.first << "]\n";  // newline after section

            // Drop TRAILING blank lines from the section body -- one canonical
            // blank is appended below. Without this, the separator would be
            // re-read as section content on the next load and every save+load
            // cycle would grow the file by one blank line per section.
            // Blank lines in the middle of a section are preserved.
            size_t last = sec.second.size();
            while (last > 0) {
                const auto& e = sec.second[last - 1];
                if (e.is_comment && trim(e.original_line).empty())
                    --last;
                else
                    break;
            }

            for (size_t i = 0; i < last; ++i) {
                const auto& entry = sec.second[i];
                if (entry.is_comment || entry.key.empty())
                    file << entry.original_line << "\n";  // ensure comment ends in newline
                else
                    file << entry.key << "=" << entry.value << "\n";  // newline after key=value
            }

            file << "\n"; // blank line after each section for readability
        }

        file.flush();
        if (!file.good()) {
            LOG_ERROR("SaveIniFile: write to '%s' failed (disk full?); keeping existing '%s'",
                tmpname.c_str(), m_szFileName);
            file.close();
            std::remove(tmpname.c_str());
            return;
        }
    }

    std::remove(m_szFileName);
    if (std::rename(tmpname.c_str(), m_szFileName) != 0) {
        LOG_ERROR("SaveIniFile: rename '%s' -> '%s' failed", tmpname.c_str(), m_szFileName);
    }
}

void SetIniFile(const char* szFileName) {
    // snprintf rather than MSVC's strncpy_s/_TRUNCATE: it truncates and always
    // null-terminates, which is exactly what _TRUNCATE asked for, and it is
    // standard on both platforms. (Plain strncpy would NOT null-terminate on
    // overflow - that is the trap this avoids.)
    snprintf(m_szFileName, MAX_INI, "%s", szFileName ? szFileName : "");
    LoadIniFile();
}

std::string get_value(const char* section, const char* key, const char* defval) {
    auto it = ini_data.find(section);
    if (it != ini_data.end()) {
        for (const auto& entry : it->second) {
            if (!entry.is_comment && entry.key == key)
                return entry.value;
        }
    }
    return defval;
}

// Write-time guard: reject anything that would be unreadable or corrupting
// on the NEXT load. Failing loudly here beats the old failure mode, where
// the write succeeded and the value silently came back wrong at startup
// (that's how device-path assignments vanished across restarts).
static bool ini_write_ok(const char* section, const char* key, const char* value) {
    if (!key || !key[0]) {
        LOG_ERROR("ini: rejected write to [%s]: empty key", section);
        return false;
    }
    if (key[0] == ';' || key[0] == '#' || key[0] == '[') {
        LOG_ERROR("ini: rejected key '%s' in [%s]: would parse as comment/section", key, section);
        return false;
    }
    if (strpbrk(key, " =\r\n")) {
        LOG_ERROR("ini: rejected key '%s' in [%s]: contains space/'='/newline", key, section);
        return false;
    }
    if (value && strpbrk(value, "\r\n")) {
        LOG_ERROR("ini: rejected value for [%s] %s: embedded newline would break the file", section, key);
        return false;
    }
    return true;
}

void update_or_add_entry(const char* section, const char* key, const char* value) {
    if (!ini_write_ok(section, key, value))
        return;

    auto& entries = ini_data[section];
    for (auto& entry : entries) {
        if (!entry.is_comment && entry.key == key) {
            entry.value = value;
            entry.original_line = std::string(key) + "=" + value;
            SaveIniFile();
            return;
        }
    }
    entries.push_back({ key, value, std::string(key) + "=" + value, false });
    SaveIniFile();
}

int get_config_int(const char* section, const char* key, int defval) {
    return std::atoi(get_value(section, key, std::to_string(defval).c_str()).c_str());
}

float get_config_float(const char* section, const char* key, float defval) {
    return (float) std::atof(get_value(section, key, std::to_string(defval).c_str()).c_str());
}

bool get_config_bool(const char* section, const char* key, bool defval) {
    std::string val = get_value(section, key, defval ? "True" : "False");
    std::transform(val.begin(), val.end(), val.begin(), ::tolower);
    return (val == "true" || val == "1" || val == "yes");
}

char* get_config_string(const char* section, const char* key, const char* defval) {
    std::string val = get_value(section, key, defval);
    char* res = new char[val.size() + 1];
    // memcpy with the exact length, rather than MSVC's strcpy_s: the buffer is
    // sized from val.size() a line above, so there is nothing to check.
    std::memcpy(res, val.c_str(), val.size() + 1);

    // Caller must delete[] the returned pointer!
    return res;
}

void set_config_string(const char* section, const char* key, const char* val) {
    update_or_add_entry(section, key, val);
}

void set_config_int(const char* section, const char* key, int val) {
    set_config_string(section, key, std::to_string(val).c_str());
}

void set_config_float(const char* section, const char* key, float val) {
    std::ostringstream oss;
    oss << std::fixed << val;

    std::string str = oss.str();
    str.erase(str.find_last_not_of('0') + 1); // remove trailing 0s
    if (!str.empty() && str.back() == '.')    // remove trailing .
        str.pop_back();

    set_config_string(section, key, str.c_str());
}

void set_config_bool(const char* section, const char* key, bool val) {
    set_config_string(section, key, val ? "True" : "False");
}


// std::string overloads
int get_config_int(const std::string& section, const std::string& key, int defaultValue) {
    return get_config_int(section.c_str(), key.c_str(), defaultValue);
}

float get_config_float(const std::string& section, const std::string& key, float defaultValue) {
    return get_config_float(section.c_str(), key.c_str(), defaultValue);
}

bool get_config_bool(const std::string& section, const std::string& key, bool defaultValue) {
    return get_config_bool(section.c_str(), key.c_str(), defaultValue);
}

std::string get_config_string(const std::string& section, const std::string& key, const std::string& defaultValue) {
    char* result = get_config_string(section.c_str(), key.c_str(), defaultValue.c_str());
    std::string value(result);
    delete[] result;
    return value;
}

void set_config_int(const std::string& section, const std::string& key, int value) {
    set_config_int(section.c_str(), key.c_str(), value);
}

void set_config_float(const std::string& section, const std::string& key, float value) {
    std::ostringstream oss;
    oss << std::fixed << value;

    std::string str = oss.str();
    str.erase(str.find_last_not_of('0') + 1);
    if (!str.empty() && str.back() == '.')
        str.pop_back();

    set_config_string(section.c_str(), key.c_str(), str.c_str());
}

void set_config_bool(const std::string& section, const std::string& key, bool value) {
    set_config_bool(section.c_str(), key.c_str(), value);
}

void set_config_string(const std::string& section, const std::string& key, const std::string& value) {
    set_config_string(section.c_str(), key.c_str(), value.c_str());
}
