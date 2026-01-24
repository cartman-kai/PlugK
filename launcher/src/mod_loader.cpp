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

    static std::string GetBasePath() {
        char path[MAX_PATH];
        GetModuleFileNameA(NULL, path, MAX_PATH);
        PathRemoveFileSpecA(path);
        return std::string(path);
    }

    FileStatus CheckStatus() {
        FileStatus status;
        std::string base = GetBasePath();
        status.dllExists = PathFileExistsA((base + "\\PlugK.dll").c_str());
        status.iniExists = PathFileExistsA((base + "\\PlugK.ini").c_str());
        status.exeExists = PathFileExistsA((base + "\\ComeOn.exe").c_str());
        return status;
    }

    BOOL InjectDLL(PROCESS_INFORMATION* pi, const char* dllPath) {
        HANDLE hProcess = pi->hProcess;
        LPVOID pRemoteBuf;
        DWORD dwBufSize = (DWORD)(strlen(dllPath) + 1);
        FARPROC pThreadProc;

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
        CloseHandle(hThread);
        VirtualFreeEx(hProcess, pRemoteBuf, 0, MEM_RELEASE);
        return TRUE;
    }

    bool LaunchOriginal() {
        std::string base = GetBasePath();
        std::string exePath = base + "\\ComeOn.exe";
        std::string workDir = base;

        STARTUPINFOA si = { sizeof(si) };
        PROCESS_INFORMATION pi = { 0 };
        
        // Quote the path for command line safety
        std::string cmdLine = "\"" + exePath + "\"";

        BOOL bRet = CreateProcessA(NULL, (LPSTR)cmdLine.c_str(), NULL, NULL, FALSE, 0, NULL, workDir.c_str(), &si, &pi);
        if (bRet) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        } else {
            char buf[256];
            sprintf_s(buf, "启动失败 (原版): %d\n路径: %s", GetLastError(), exePath.c_str());
            MessageBoxA(NULL, buf, "错误", MB_ICONERROR);
        }
        return bRet != FALSE;
    }

    bool LaunchWithMod() {
        std::string base = GetBasePath();
        std::string exePath = base + "\\ComeOn.exe";
        std::string dllPath = base + "\\PlugK.dll";
        std::string workDir = base;

        if (!PathFileExistsA(dllPath.c_str())) {
            MessageBoxA(NULL, "启动失败: 找不到 PlugK.dll", "错误", MB_ICONERROR);
            return false;
        }

        STARTUPINFOA si = { sizeof(si) };
        PROCESS_INFORMATION pi = { 0 };

        std::string cmdLine = "\"" + exePath + "\"";

        // Launch suspended
        BOOL bRet = CreateProcessA(NULL, (LPSTR)cmdLine.c_str(), NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, workDir.c_str(), &si, &pi);
        
        if (!bRet) {
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
        } else {
            MessageBoxA(NULL, "启动失败: DLL 注入失败！\n请确保以管理员身份运行，并检查杀毒软件。", "错误", MB_ICONERROR);
            TerminateProcess(pi.hProcess, 0);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            return false;
        }
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
