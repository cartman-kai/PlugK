#include <windows.h>
#include <stdio.h>
#include <shlwapi.h>
#include <time.h>
#include < tchar.h >

#pragma comment(linker, "/subsystem:windows /entry:WinMainCRTStartup")

#pragma comment(lib, "shlwapi.lib")

// 全局配置
char g_targetExe[MAX_PATH] = "ComeOn.exe";
char g_dllName[MAX_PATH] = "PlugK.dll";
char g_logPath[MAX_PATH] = "loader.log";

// 简单的文件日志记录
void LogError(const wchar_t *message, DWORD errorCode)
{
    FILE *fp = fopen(g_logPath, "a");
    if (fp)
    {
        time_t now = time(NULL);
        char timeStr[64];
        struct tm tm_info;
        localtime_s(&tm_info, &now);
        strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &tm_info);

        fprintf(fp, "[%s] Error: %ls (Code: %d)\n", timeStr, message, errorCode);
        fclose(fp);
    }

    // 弹窗提示
    wchar_t fullMsg[512];
    swprintf_s(fullMsg, 512, L"%ls\nError Code: %d", message, errorCode);
    MessageBoxW(NULL, fullMsg, L"PlugK Loader Error", MB_ICONERROR | MB_OK);
}

void LoadConfig()
{
    char iniPath[MAX_PATH];
    GetCurrentDirectoryA(MAX_PATH, iniPath);
    strcat_s(iniPath, MAX_PATH, "\\PlugKLoader.ini");

    if (PathFileExistsA(iniPath))
    {
        GetPrivateProfileStringA("General", "GameExe", "ComeOn.exe", g_targetExe, MAX_PATH, iniPath);
        GetPrivateProfileStringA("General", "DllName", "PlugK.dll", g_dllName, MAX_PATH, iniPath);
    }
}

// 核心注入函数 (保持不变，省略部分细节以节省篇幅，重点在错误处理调用 LogError)
BOOL InjectDLL(PROCESS_INFORMATION *pi, const char *dllPath)
{
    HANDLE hProcess = pi->hProcess;
    LPVOID pRemoteBuf;
    DWORD dwBufSize = (DWORD)(strlen(dllPath) + 1);
    FARPROC pThreadProc;

    // 1. 在目标进程中分配内存，用于存放 DLL 的路径字符串
    pRemoteBuf = VirtualAllocEx(hProcess, NULL, dwBufSize, MEM_COMMIT, PAGE_READWRITE);
    if (pRemoteBuf == NULL)
    {
        LogError(L"[!] 无法在目标进程分配内存。错误码: %d\n", GetLastError());
        return FALSE;
    }

    // 2. 将 DLL 的完整路径写入目标进程内存
    if (!WriteProcessMemory(hProcess, pRemoteBuf, (LPVOID)dllPath, dwBufSize, NULL))
    {
        LogError(L"[!] 写入内存失败。错误码: %d\n", GetLastError());
        VirtualFreeEx(hProcess, pRemoteBuf, 0, MEM_RELEASE);
        return FALSE;
    }

    // 3. 获取 LoadLibraryA 函数的地址 (在 Kernel32.dll 中)
    // 因为 Kernel32.dll 在所有进程中的加载基址几乎相同，所以可以直接用本进程的地址
    pThreadProc = GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryA");
    if (pThreadProc == NULL)
    {
        LogError(L"[!] 无法获取 LoadLibraryA 地址。\n", GetLastError());
        VirtualFreeEx(hProcess, pRemoteBuf, 0, MEM_RELEASE);
        return FALSE;
    }

    // 4. 在目标进程创建远程线程，执行 LoadLibraryA(dllPath)
    HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0, (LPTHREAD_START_ROUTINE)pThreadProc, pRemoteBuf, 0, NULL);
    if (hThread == NULL)
    {
        LogError(L"[!] 创建远程线程失败。错误码: %d\n", GetLastError());
        VirtualFreeEx(hProcess, pRemoteBuf, 0, MEM_RELEASE);
        return FALSE;
    }

    // 5. 等待注入完成
    WaitForSingleObject(hThread, INFINITE);
    CloseHandle(hThread);

    // 6. 清理目标进程中的临时内存
    VirtualFreeEx(hProcess, pRemoteBuf, 0, MEM_RELEASE);
    return TRUE;
}

int WINAPI WinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPSTR lpCmdLine,
    _In_ int nCmdShow)
{
    // 1. 加载配置
    LoadConfig();

    // 2. 检查 DLL 是否存在，并获取完整路径 (重要！注入必须用完整路径)
    char currentDir[MAX_PATH];
    GetCurrentDirectoryA(MAX_PATH, currentDir);

    char fullDllPath[MAX_PATH];
    snprintf(fullDllPath, MAX_PATH, "%s\\%s", currentDir, g_dllName);

    if (!PathFileExistsA(fullDllPath))
    {
        LogError(L"未找到 DLL 文件，请确认 PlugK.dll 是否在当前目录。", 0);
        return 1;
    }

    if (!PathFileExistsA(g_targetExe))
    {
        LogError(L"未找到游戏主程序 (ComeOn.exe)。", 0);
        return 1;
    }

    // 4. 以挂起模式 (Suspended) 启动游戏
    // 这样可以在游戏执行任何代码之前完成注入，防止错过初始化时机
    STARTUPINFOA si = {0};
    PROCESS_INFORMATION pi = {0};
    si.cb = sizeof(si);

    BOOL bRet = CreateProcessA(NULL, g_targetExe, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, NULL, &si, &pi);

    if (!bRet)
    {
        LogError(L"启动游戏进程失败。", GetLastError());
        return 1;
    }

    // 5. 执行注入
    if (!InjectDLL(&pi, fullDllPath))
    {
        // InjectDLL 内部已经记录了日志
        TerminateProcess(pi.hProcess, 0);
        return 1;
    }

    ResumeThread(pi.hThread);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return 0;
}