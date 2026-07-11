// Windows & System
#include <windows.h>
#include <shlwapi.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdarg.h>
#include <vector>
#include <string>
#include <algorithm>

// Project
#include "../inc/mod_loader.h"


#pragma comment(lib, "shlwapi.lib")

namespace ModLoader {

    // 获取启动器所在目录，后续所有文件检测和启动都以游戏根目录为基准。
    static std::string GetBasePath() {
        char path[MAX_PATH];
        GetModuleFileNameA(NULL, path, MAX_PATH);
        PathRemoveFileSpecA(path);
        return std::string(path);
    }

    static bool FileExists(const std::string& path) {
        return PathFileExistsA(path.c_str()) == TRUE;
    }

    static void WriteLog(const char* fmt, ...) {
        std::string path = GetBasePath() + "\\PlugKLauncher.log";
        FILE* fp = NULL;
        if (fopen_s(&fp, path.c_str(), "a") != 0 || !fp)
            return;

        SYSTEMTIME st = {};
        GetLocalTime(&st);
        fprintf(fp, "[%04u-%02u-%02u %02u:%02u:%02u] ",
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

        va_list args;
        va_start(args, fmt);
        vfprintf(fp, fmt, args);
        va_end(args);
        fputc('\n', fp);
        fclose(fp);
    }

    static bool ReadFileToBuffer(const std::string& path, std::vector<char>& out) {
        HANDLE hFile = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE)
            return false;

        LARGE_INTEGER size = {};
        if (!GetFileSizeEx(hFile, &size) || size.QuadPart <= 0 || size.QuadPart > 64 * 1024 * 1024) {
            CloseHandle(hFile);
            return false;
        }

        out.resize((size_t)size.QuadPart);
        DWORD read = 0;
        BOOL ok = ReadFile(hFile, out.data(), (DWORD)out.size(), &read, NULL);
        CloseHandle(hFile);
        return ok && read == out.size();
    }

    static bool BufferContainsString(const std::vector<char>& data, const char* needle) {
        size_t len = strlen(needle);
        if (len == 0 || data.size() < len)
            return false;
        return std::search(data.begin(), data.end(), needle, needle + len) != data.end();
    }

    static bool IsSteamComeOnDll(const std::string& path) {
        std::vector<char> data;
        if (!ReadFileToBuffer(path, data))
            return false;

        return BufferContainsString(data, "SteamAPI_RestartAppIfNecessary") &&
               BufferContainsString(data, "SteamAPI_Init");
    }

    static bool IsKnownSteamLauncher(const std::string& path) {
        std::vector<char> data;
        if (!ReadFileToBuffer(path, data))
            return false;

        return BufferContainsString(data, "CreateProcess") &&
               (BufferContainsString(data, "wke.dll") || BufferContainsString(data, "MAINRES"));
    }

    static std::string GetLauncherHookPath() {
        return GetBasePath() + "\\PlugKLauncherHook.dll";
    }

    struct SteamLaunchConfig {
        int version;
        DWORD appId;
    };

    static bool GetSteamLaunchConfig(SteamLaunchConfig& config) {
        std::string base = GetBasePath();
        int version = GetGameVersion();
        DWORD appId = 0;

        if (version == 105) {
            appId = 1792820;
        } else if (version == 201) {
            appId = 2924510;
        } else {
            return false;
        }

        std::string launcherPath = base + "\\launcher.exe";
        std::string comeOnDllPath = base + "\\ComeOn.dll";
        std::string steamApiPath = base + "\\steam_api.dll";

        if (!FileExists(launcherPath) || !FileExists(comeOnDllPath) || !FileExists(steamApiPath))
            return false;

        if (!IsSteamComeOnDll(comeOnDllPath) || !IsKnownSteamLauncher(launcherPath))
            return false;

        config.version = version;
        config.appId = appId;
        return true;
    }

    static bool GetProcessImagePath(DWORD pid, std::wstring& outPath) {
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
        if (snapshot == INVALID_HANDLE_VALUE)
            return false;

        MODULEENTRY32 module = {};
        module.dwSize = sizeof(module);
        bool ok = false;
        if (Module32First(snapshot, &module)) {
            outPath = module.szExePath;
            ok = true;
        }
        CloseHandle(snapshot);
        return ok;
    }

    static std::wstring ToWide(const std::string& text) {
        int count = MultiByteToWideChar(CP_ACP, 0, text.c_str(), -1, NULL, 0);
        if (count <= 0)
            return L"";

        std::wstring result;
        result.resize(count - 1);
        MultiByteToWideChar(CP_ACP, 0, text.c_str(), -1, &result[0], count);
        return result;
    }

    static bool IsSamePath(const std::wstring& a, const std::wstring& b) {
        wchar_t fullA[MAX_PATH] = { 0 };
        wchar_t fullB[MAX_PATH] = { 0 };
        if (!GetFullPathNameW(a.c_str(), MAX_PATH, fullA, NULL) ||
            !GetFullPathNameW(b.c_str(), MAX_PATH, fullB, NULL))
            return _wcsicmp(a.c_str(), b.c_str()) == 0;
        return _wcsicmp(fullA, fullB) == 0;
    }

    static bool ProcessHasModule(DWORD pid, const wchar_t* moduleName) {
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
        if (snapshot == INVALID_HANDLE_VALUE)
            return false;

        MODULEENTRY32 module = {};
        module.dwSize = sizeof(module);
        bool found = false;
        if (Module32First(snapshot, &module)) {
            do {
                if (_wcsicmp(module.szModule, moduleName) == 0) {
                    found = true;
                    break;
                }
            } while (Module32Next(snapshot, &module));
        }
        CloseHandle(snapshot);
        return found;
    }

    static BOOL InjectDLLToProcess(DWORD pid, const char* dllPath) {
        HANDLE hProcess = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                                      PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
                                      FALSE, pid);
        if (!hProcess)
            return FALSE;

        PROCESS_INFORMATION pi = {};
        pi.hProcess = hProcess;
        BOOL ok = InjectDLL(&pi, dllPath);
        CloseHandle(hProcess);
        return ok;
    }

    static DWORD WINAPI SteamLauncherMonitorThread(LPVOID param) {
        char* basePath = (char*)param;
        std::string base = basePath ? basePath : "";
        if (basePath)
            LocalFree(basePath);

        std::wstring launcherPath = ToWide(base + "\\launcher.exe");
        std::string hookPath = base + "\\PlugKLauncherHook.dll";
        std::vector<DWORD> attempted;

        WriteLog("Steam launcher monitor started.");

        DWORD start = GetTickCount();
        while (GetTickCount() - start < 30 * 60 * 1000) {
            DWORD elapsed = GetTickCount() - start;
            HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
            if (snapshot == INVALID_HANDLE_VALUE) {
                Sleep(elapsed < 30 * 1000 ? 50 : 500);
                continue;
            }

            PROCESSENTRY32 process = {};
            process.dwSize = sizeof(process);
            if (Process32First(snapshot, &process)) {
                do {
                    if (_wcsicmp(process.szExeFile, L"launcher.exe") != 0)
                        continue;

                    if (std::find(attempted.begin(), attempted.end(), process.th32ProcessID) != attempted.end())
                        continue;

                    std::wstring imagePath;
                    if (!GetProcessImagePath(process.th32ProcessID, imagePath))
                        continue;

                    if (!IsSamePath(imagePath, launcherPath))
                        continue;

                    if (ProcessHasModule(process.th32ProcessID, L"PlugKLauncherHook.dll")) {
                        attempted.push_back(process.th32ProcessID);
                        WriteLog("launcher.exe pid=%lu already has PlugKLauncherHook.dll.", process.th32ProcessID);
                        continue;
                    }

                    WriteLog("injecting PlugKLauncherHook.dll into launcher.exe pid=%lu.", process.th32ProcessID);
                    if (InjectDLLToProcess(process.th32ProcessID, hookPath.c_str())) {
                        WriteLog("inject succeeded: pid=%lu.", process.th32ProcessID);
                        attempted.push_back(process.th32ProcessID);
                    } else {
                        WriteLog("inject failed: pid=%lu last_error=%lu.", process.th32ProcessID, GetLastError());
                    }
                } while (Process32Next(snapshot, &process));
            }
            CloseHandle(snapshot);
            Sleep(elapsed < 30 * 1000 ? 50 : 500);
        }

        WriteLog("Steam launcher monitor stopped.");
        return 0;
    }

    static void StartSteamLauncherMonitor(const std::string& base) {
        char* copy = (char*)LocalAlloc(LMEM_FIXED, base.size() + 1);
        if (!copy)
            return;
        memcpy(copy, base.c_str(), base.size() + 1);

        HANDLE thread = CreateThread(NULL, 0, SteamLauncherMonitorThread, copy, 0, NULL);
        if (thread)
            CloseHandle(thread);
        else
            LocalFree(copy);
    }

    FileStatus CheckStatus() {
        FileStatus status = {};
        std::string base = GetBasePath();
        status.dllExists = FileExists(base + "\\PlugK.dll");
        status.iniExists = FileExists(base + "\\PlugK.ini");
        status.exeExists = FileExists(base + "\\ComeOn.exe");
        status.steamLauncherExists = FileExists(base + "\\launcher.exe");
        status.launcherHookExists = FileExists(GetLauncherHookPath());
        status.steamReady = IsSteamAvailable();
        return status;
    }

    // 在目标进程中分配 DLL 路径并远程调用 LoadLibraryA，返回值非零才认为注入成功。
    BOOL InjectDLL(PROCESS_INFORMATION* pi, const char* dllPath) {
        HANDLE hProcess = pi->hProcess;
        LPVOID pRemoteBuf;
        DWORD dwBufSize = (DWORD)(strlen(dllPath) + 1);
        FARPROC pThreadProc;
        DWORD exitCode = 0;

        pRemoteBuf = VirtualAllocEx(hProcess, NULL, dwBufSize, MEM_COMMIT, PAGE_READWRITE);
        if (pRemoteBuf == NULL) return FALSE;

        if (!WriteProcessMemory(hProcess, pRemoteBuf, (LPVOID)dllPath, dwBufSize, NULL)) {
            VirtualFreeEx(hProcess, pRemoteBuf, 0, MEM_RELEASE);
            return FALSE;
        }

        pThreadProc = GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryA");
        if (pThreadProc == NULL) {
            VirtualFreeEx(hProcess, pRemoteBuf, 0, MEM_RELEASE);
            return FALSE;
        }

        HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0, (LPTHREAD_START_ROUTINE)pThreadProc, pRemoteBuf, 0, NULL);
        if (hThread == NULL) {
            VirtualFreeEx(hProcess, pRemoteBuf, 0, MEM_RELEASE);
            return FALSE;
        }

        WaitForSingleObject(hThread, INFINITE);
        GetExitCodeThread(hThread, &exitCode);
        CloseHandle(hThread);
        VirtualFreeEx(hProcess, pRemoteBuf, 0, MEM_RELEASE);
        return exitCode != 0;
    }

    static bool LaunchProcess(const std::string& exePath, const std::string& workDir, DWORD creationFlags, PROCESS_INFORMATION* outPi) {
        STARTUPINFOA si = { sizeof(si) };
        std::string cmdLine = "\"" + exePath + "\"";
        return CreateProcessA(NULL, (LPSTR)cmdLine.c_str(), NULL, NULL, FALSE, creationFlags, NULL, workDir.c_str(), &si, outPi) != FALSE;
    }

    bool IsSteamAvailable() {
        SteamLaunchConfig config = {};
        return GetSteamLaunchConfig(config);
    }

    bool IsSteam105Available() {
        SteamLaunchConfig config = {};
        return GetSteamLaunchConfig(config) && config.version == 105;
    }

    bool LaunchOriginal() {
        std::string base = GetBasePath();
        SteamLaunchConfig steamConfig = {};
        if (GetSteamLaunchConfig(steamConfig)) {
            char url[64];
            sprintf_s(url, "steam://rungameid/%lu", steamConfig.appId);
            HINSTANCE shellResult = ShellExecuteA(NULL, "open", url, NULL, base.c_str(), SW_SHOWNORMAL);
            if ((INT_PTR)shellResult > 32)
                return true;

            std::string launcherPath = base + "\\launcher.exe";
            PROCESS_INFORMATION pi = { 0 };
            if (LaunchProcess(launcherPath, base, 0, &pi)) {
                CloseHandle(pi.hThread);
                CloseHandle(pi.hProcess);
                return true;
            }

            char buf[256];
            sprintf_s(buf, "启动失败 (Steam launcher): %d\n路径: %s", GetLastError(), launcherPath.c_str());
            MessageBoxA(NULL, buf, "错误", MB_ICONERROR);
            return false;
        }

        std::string exePath = base + "\\ComeOn.exe";
        std::string workDir = base;

        PROCESS_INFORMATION pi = { 0 };
        if (LaunchProcess(exePath, workDir, 0, &pi)) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            return true;
        }

        char buf[256];
        sprintf_s(buf, "启动失败 (原版): %d\n路径: %s", GetLastError(), exePath.c_str());
        MessageBoxA(NULL, buf, "错误", MB_ICONERROR);
        return false;
    }

    static bool LaunchSteamWithMod(const SteamLaunchConfig& config) {
        std::string base = GetBasePath();
        std::string launcherPath = base + "\\launcher.exe";
        std::string hookDllPath = GetLauncherHookPath();

        WriteLog("LaunchSteamWithMod requested: version=%d appid=%lu.", config.version, config.appId);
        if (!FileExists(hookDllPath)) {
            WriteLog("missing PlugKLauncherHook.dll.");
            MessageBoxA(NULL, "启动失败: 找不到 PlugKLauncherHook.dll", "错误", MB_ICONERROR);
            return false;
        }

        StartSteamLauncherMonitor(base);

        char url[64];
        sprintf_s(url, "steam://rungameid/%lu", config.appId);
        HINSTANCE shellResult = ShellExecuteA(NULL, "open", url, NULL, base.c_str(), SW_SHOWNORMAL);
        if ((INT_PTR)shellResult > 32) {
            WriteLog("Steam protocol launch requested: %s.", url);
            return true;
        }

        WriteLog("Steam protocol launch failed: result=%Id. Falling back to direct launcher start.", (INT_PTR)shellResult);
        PROCESS_INFORMATION pi = { 0 };
        if (!LaunchProcess(launcherPath, base, CREATE_SUSPENDED, &pi)) {
            WriteLog("CreateProcess launcher.exe failed: %lu.", GetLastError());
            char buf[256];
            sprintf_s(buf, "启动失败 (创建 Steam launcher): %d", GetLastError());
            MessageBoxA(NULL, buf, "错误", MB_ICONERROR);
            return false;
        }

        if (!InjectDLL(&pi, hookDllPath.c_str())) {
            WriteLog("initial launcher hook inject failed: pid=%lu last_error=%lu.", pi.dwProcessId, GetLastError());
            MessageBoxA(NULL, "启动失败: Steam launcher hook 注入失败。", "错误", MB_ICONERROR);
            TerminateProcess(pi.hProcess, 0);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            return false;
        }

        WriteLog("initial launcher hook inject succeeded: pid=%lu.", pi.dwProcessId);
        ResumeThread(pi.hThread);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return true;
    }

    bool LaunchWithMod() {
        std::string base = GetBasePath();
        std::string exePath = base + "\\ComeOn.exe";
        std::string dllPath = base + "\\PlugK.dll";
        std::string workDir = base;

        if (!FileExists(dllPath)) {
            MessageBoxA(NULL, "启动失败: 找不到 PlugK.dll", "错误", MB_ICONERROR);
            return false;
        }

        SteamLaunchConfig steamConfig = {};
        if (GetSteamLaunchConfig(steamConfig)) {
            return LaunchSteamWithMod(steamConfig);
        }

        PROCESS_INFORMATION pi = { 0 };
        if (!LaunchProcess(exePath, workDir, CREATE_SUSPENDED, &pi)) {
            char buf[256];
            sprintf_s(buf, "启动失败 (创建进程): %d", GetLastError());
            MessageBoxA(NULL, buf, "错误", MB_ICONERROR);
            return false;
        }

        if (InjectDLL(&pi, dllPath.c_str())) {
            ResumeThread(pi.hThread);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            return true;
        }

        MessageBoxA(NULL, "启动失败: DLL 注入失败！\n请确保以管理员身份运行，并检查杀毒软件。", "错误", MB_ICONERROR);
        TerminateProcess(pi.hProcess, 0);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return false;
    }
    int GetGameVersion() {
        std::string exePath = GetBasePath() + "\\ComeOn.exe";
        HANDLE hFile = CreateFileA(exePath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) return 0;

        int version = 0;
        
        // 1.05 Check: Offset 0x6ED18 -> 0x3F866666
        if (SetFilePointer(hFile, 0x6ED18, NULL, FILE_BEGIN) != INVALID_SET_FILE_POINTER) {
            DWORD val = 0;
            DWORD read = 0;
            if (ReadFile(hFile, &val, 4, &read, NULL) && read == 4) {
                if (val == 0x3F866666) version = 105;
            }
        }
        
        // 2.01 Check: Offset 0x7CF28 -> 0x4000A3D7
        if (version == 0 && SetFilePointer(hFile, 0x7CF28, NULL, FILE_BEGIN) != INVALID_SET_FILE_POINTER) {
            DWORD val = 0;
            DWORD read = 0;
            if (ReadFile(hFile, &val, 4, &read, NULL) && read == 4) {
                 if (val == 0x4000A3D7) version = 201;
            }
        }

        CloseHandle(hFile);
        return version;
    }
}
