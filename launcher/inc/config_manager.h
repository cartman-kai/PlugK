#pragma once
#include <string>
#include <vector>

extern "C" {
#include "config.h"
}

namespace ConfigManager {
    bool Initialize(const std::string& path);
    void Save();
    bool NeedsGeneration();
    void GenerateDefault(const std::string& path);
    const std::string& GetIniPath();
    
    // UI specific
    int GetActiveModCount();
    std::string GetResolutionString();
}
