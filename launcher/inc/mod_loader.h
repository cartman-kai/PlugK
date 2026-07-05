#pragma once
#include <windows.h>
#include <string>

namespace ModLoader {
    struct FileStatus {
        bool dllExists;
        bool iniExists;
        bool exeExists;
        bool steamLauncherExists;
        bool launcherHookExists;
        bool steamReady;
    };

    FileStatus CheckStatus();
    bool LaunchOriginal();
    bool LaunchWithMod();
    
    // Core injection logic from loader.c (internal)
    BOOL InjectDLL(PROCESS_INFORMATION* pi, const char* dllPath);

    int GetGameVersion(); // Returns 105, 201 or 0 (unknown)
    bool IsSteamAvailable();
    bool IsSteam105Available();
}
