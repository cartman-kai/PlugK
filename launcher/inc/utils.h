#pragma once
#include <windows.h>
#include <string>

namespace Utils {
    float GetDPIScale();
    bool CreateDesktopShortcut(const std::string& targetPath, const std::string& name, const std::string& args = "");
    bool ShortcutExists(const std::string& name);
}
