// Windows & System
#include <windows.h>
#include <shlwapi.h>
#include <stdio.h>
#include <vector>
#include <string>

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

    FileStatus CheckStatus() {
        FileStatus status = {};
        std::string base = GetBasePath();
        status.dllExists = FileExists(base + "\\PlugK.dll");
        status.iniExists = FileExists(base + "\\PlugK.ini");
        status.exeExists = FileExists(base + "\\ComeOn.exe");
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

    bool LaunchOriginal() {
        std::string base = GetBasePath();
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

    bool LaunchWithMod() {
        std::string base = GetBasePath();
        std::string exePath = base + "\\ComeOn.exe";
        std::string dllPath = base + "\\PlugK.dll";
        std::string workDir = base;

        if (!FileExists(dllPath)) {
            MessageBoxA(NULL, "启动失败: 找不到 PlugK.dll", "错误", MB_ICONERROR);
            return false;
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
