#include <windows.h>
#include <shlwapi.h>
#include <stdio.h>
#include <fstream>
#include <sstream>
#include <regex>
#include <vector>
#include <string>

#include "../inc/config_manager.h"

#pragma comment(lib, "shlwapi.lib")


namespace ConfigManager {
    static std::string g_currentIniPath;

    bool Initialize(const std::string& path) {
        g_currentIniPath = path;
        pk_config_load(path.c_str());
        return true;
    }

    bool NeedsGeneration() {
        return !PathFileExistsA(g_currentIniPath.c_str());
    }

    void GenerateDefault(const std::string& path) {
        pk_config_create_default(path.c_str());
    }

    void Save() {
        // Robust save implementation preserving comments using Regex
        std::ifstream inFile(g_currentIniPath);
        std::vector<std::string> lines;
        std::string line;

        if (inFile.is_open()) {
            while (std::getline(inFile, line))
                lines.push_back(line);
            inFile.close();
        }

        auto UpdateValueInLines = [&](const char* section, const char* key, int val) {
            std::string sSection = std::string("[") + section + "]";
            bool inCorrectSection = false;
            
            for (auto& l : lines) {
                // Check if line is a section header
                if (l.find("[") != std::string::npos && l.find("]") != std::string::npos) {
                     if (l.find(sSection) != std::string::npos) {
                         inCorrectSection = true;
                     } else {
                         inCorrectSection = false;
                     }
                     continue;
                }

                if (inCorrectSection) {
                    // Regex match for Key=Value (careful with whitespace)
                    // ^\s*key\s*=\s*(-?\d+)(.*)
                    std::string keyPattern = std::string(key);
                    // Create regex: start of line, optional whitespace, key, optional whitespace, =, optional whitespace, capture value, capture rest
                    std::regex re("^\\s*" + keyPattern + "\\s*=\\s*(-?\\d+)(.*)");
                    std::smatch match;
                    
                    if (std::regex_search(l, match, re)) {
                        char buf[512];
                        // match[2] contains any inline comments or whitespace after the value
                        std::string tail = match[2].str();
                        snprintf(buf, sizeof(buf), "%s=%d%s", key, val, tail.c_str());
                        l = buf;
                        return; // Found and updated
                    }
                }
            }
        };

#define X(type, name, sec, key, val, desc) UpdateValueInLines(sec, key, (int)g_pk_config.name);
#include "config_def.h"
#undef X

        std::ofstream outFile(g_currentIniPath);
        for (const auto& l : lines)
            outFile << l << "\n";
    }

    const std::string& GetIniPath() {
        return g_currentIniPath;
    }

    int GetActiveModCount() {
        int count = 0;
#define X(type, name, sec, key, val, desc) if (type == TYPE_BOOL && g_pk_config.name) count++;
#include "config_def.h"
#undef X
        return count;
    }

    std::string GetResolutionString() {
        char buf[64];
        if (g_pk_config.res_enabled) {
            snprintf(buf, sizeof(buf), "%d x %d", g_pk_config.res_width, g_pk_config.res_height);
        } else {
            snprintf(buf, sizeof(buf), "原版 (Original)");
        }
        return buf;
    }
}
