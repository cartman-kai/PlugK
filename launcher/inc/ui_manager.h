#pragma once
#include <windows.h>
#include "imgui.h"

namespace UIManager {
    void Initialize(HWND hwnd, float dpiScale);
    void Render();
    void Cleanup();

    // Pages
    void RenderMainPage();
    void RenderSettingsPage();
    
    // UI Helpers
    void SetupStyles(float dpiScale);
    void ShowSaveNotification(const char* text);
    
    // Dynamic window size based on current view
    // Returns true if window size should change, and fills width/height
    bool GetDesiredWindowSize(int* outWidth, int* outHeight);
}
