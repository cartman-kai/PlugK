// Windows & System
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <vector>

// Project
#include "../inc/utils.h"


#pragma comment(lib, "shlwapi.lib")

namespace Utils {

    float GetDPIScale() {
        HDC hdc = GetDC(NULL);
        int dpi = GetDeviceCaps(hdc, LOGPIXELSY);
        ReleaseDC(NULL, hdc);
        float scale = (float)dpi / 96.0f;
        if (scale < 1.0f) scale = 1.0f;
        return scale;
    }

    bool ShortcutExists(const std::string& name) {
        char desktopPath[MAX_PATH];
        SHGetSpecialFolderPathA(NULL, desktopPath, CSIDL_DESKTOPDIRECTORY, FALSE);
        
        std::string fullPath = std::string(desktopPath) + "\\" + name + ".lnk";
        return PathFileExistsA(fullPath.c_str());
    }

    bool CreateDesktopShortcut(const std::string& targetPath, const std::string& name, const std::string& args) {
        // Initialize COM library. COINIT_APARTMENTTHREADED is required for ShellLink.
        HRESULT hrInit = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
        if (FAILED(hrInit) && hrInit != RPC_E_CHANGED_MODE) {
            return false;
        }

        bool shouldUninitialize = SUCCEEDED(hrInit);
        bool success = false;

        IShellLinkA* pShellLink = NULL;
        HRESULT hr = CoCreateInstance(
            CLSID_ShellLink,
            NULL,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&pShellLink));
            
        if (SUCCEEDED(hr)) {
            pShellLink->SetPath(targetPath.c_str());
            pShellLink->SetArguments(args.c_str());
                
            // Set working directory
            char dir[MAX_PATH];
            strcpy_s(dir, targetPath.c_str());
            PathRemoveFileSpecA(dir);
            pShellLink->SetWorkingDirectory(dir);

            // Set Icon location to target executable
            pShellLink->SetIconLocation(targetPath.c_str(), 0);

            IPersistFile* pPersistFile = NULL;
            hr = pShellLink->QueryInterface(IID_PPV_ARGS(&pPersistFile));
            if (SUCCEEDED(hr)) {
                WCHAR desktopPath[MAX_PATH];
                // Use SHGetFolderPathW to get the path correctly in Unicode
                if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, desktopPath))) {
                    // Convert name (assuming UTF-8) to Wide
                    int nameLen = MultiByteToWideChar(CP_UTF8, 0, name.c_str(), -1, NULL, 0);
                    if (nameLen > 0) {
                        std::vector<WCHAR> nameW(nameLen + 1);
                        MultiByteToWideChar(CP_UTF8, 0, name.c_str(), -1, nameW.data(), nameLen + 1);

                        std::wstring lnkPath = std::wstring(desktopPath) + L"\\" + nameW.data() + L".lnk";
                        if (SUCCEEDED(pPersistFile->Save(lnkPath.c_str(), TRUE))) {
                            success = true;
                        }
                    }
                }

                pPersistFile->Release();
            }

            pShellLink->Release();
        }
        
        // Only uninitialize if we successfully initialized it in this scope
        if (shouldUninitialize) {
            CoUninitialize();
        }
        
        return success;
    }
}
