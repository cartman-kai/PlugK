#include <windows.h>
#include <stdio.h>
#include <shlwapi.h> // 需要链接 shlwapi.lib

#pragma comment(lib, "shlwapi.lib")

// 全局配置
char g_targetExe[MAX_PATH] = "ComeOn.exe"; // 默认游戏名
char g_dllName[MAX_PATH] = "PlugK.dll";    // DLL名

// 读取配置
void LoadConfig()
{
    char iniPath[MAX_PATH];
    GetCurrentDirectoryA(MAX_PATH, iniPath);
    strcat_s(iniPath, MAX_PATH, "\\PlugKLoader.ini");

    if (PathFileExistsA(iniPath))
    {
        // 如果有配置文件，优先读取配置文件中的游戏名
        GetPrivateProfileStringA("General", "GameExe", "ComeOn.exe", g_targetExe, MAX_PATH, iniPath);
        // 也可以配置 DLL 名字
        GetPrivateProfileStringA("General", "DllName", "PlugK.dll", g_dllName, MAX_PATH, iniPath);
    }
}

// 注入核心函数
BOOL InjectDLL(PROCESS_INFORMATION* pi, const char* dllPath)
{
    HANDLE hProcess = pi->hProcess;
    LPVOID pRemoteBuf;
    DWORD dwBufSize = (DWORD)(strlen(dllPath) + 1);
    FARPROC pThreadProc;

    // 1. 在目标进程中分配内存，用于存放 DLL 的路径字符串
    pRemoteBuf = VirtualAllocEx(hProcess, NULL, dwBufSize, MEM_COMMIT, PAGE_READWRITE);
    if (pRemoteBuf == NULL)
    {
        printf("[!] 无法在目标进程分配内存。错误码: %d\n", GetLastError());
        return FALSE;
    }

    // 2. 将 DLL 的完整路径写入目标进程内存
    if (!WriteProcessMemory(hProcess, pRemoteBuf, (LPVOID)dllPath, dwBufSize, NULL))
    {
        printf("[!] 写入内存失败。错误码: %d\n", GetLastError());
        VirtualFreeEx(hProcess, pRemoteBuf, 0, MEM_RELEASE);
        return FALSE;
    }

    // 3. 获取 LoadLibraryA 函数的地址 (在 Kernel32.dll 中)
    // 因为 Kernel32.dll 在所有进程中的加载基址几乎相同，所以可以直接用本进程的地址
    pThreadProc = GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryA");
    if (pThreadProc == NULL)
    {
        printf("[!] 无法获取 LoadLibraryA 地址。\n");
        VirtualFreeEx(hProcess, pRemoteBuf, 0, MEM_RELEASE);
        return FALSE;
    }

    // 4. 在目标进程创建远程线程，执行 LoadLibraryA(dllPath)
    HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0, (LPTHREAD_START_ROUTINE)pThreadProc, pRemoteBuf, 0, NULL);
    if (hThread == NULL)
    {
        printf("[!] 创建远程线程失败。错误码: %d\n", GetLastError());
        VirtualFreeEx(hProcess, pRemoteBuf, 0, MEM_RELEASE);
        return FALSE;
    }

    // 5. 等待注入完成
    WaitForSingleObject(hThread, INFINITE);
    CloseHandle(hThread);

    // 6. 清理目标进程中的临时内存
    VirtualFreeEx(hProcess, pRemoteBuf, 0, MEM_RELEASE);

    printf("[+] PlugK.dll 注入成功！\n");
    return TRUE;
}

int main()
{
    // 设置控制台标题
    SetConsoleTitleA("PlugK 启动器");

    printf("==========================================\n");
    printf("        PlugK Game Loader v1.0            \n");
    printf("==========================================\n\n");

    // 1. 加载配置
    LoadConfig();

    // 2. 检查 DLL 是否存在，并获取完整路径 (重要！注入必须用完整路径)
    char fullDllPath[MAX_PATH];
    if (GetFullPathNameA(g_dllName, MAX_PATH, fullDllPath, NULL) == 0)
    {
        printf("[x] 路径解析错误。\n");
        system("pause");
        return 1;
    }

    if (!PathFileExistsA(fullDllPath))
    {
        printf("[x] 错误：未找到 %s\n", g_dllName);
        printf("    请确保 dll 文件与本程序在同一目录下。\n", g_dllName);
        system("pause");
        return 1;
    }

    printf("[*] 目标 DLL: %s\n", fullDllPath);
    printf("[*] 目标 游戏: %s\n", g_targetExe);

    // 3. 检查游戏 EXE 是否存在
    if (!PathFileExistsA(g_targetExe))
    {
        printf("[x] 错误：未找到游戏主程序 \"%s\"\n", g_targetExe);
        printf("    提示：你可以创建一个 PlugKLoader.ini 文件来指定游戏程序名称。\n");
        printf("    [General]\n    GameExe=你的游戏名.exe\n");
        system("pause");
        return 1;
    }

    // 4. 以挂起模式 (Suspended) 启动游戏
    // 这样可以在游戏执行任何代码之前完成注入，防止错过初始化时机
    STARTUPINFOA si = { 0 };
    PROCESS_INFORMATION pi = { 0 };
    si.cb = sizeof(si);

    BOOL bRet = CreateProcessA(
        NULL,           // 应用程序名称
        g_targetExe,    // 命令行 (这里直接放 exe 名)
        NULL,           // 进程安全属性
        NULL,           // 线程安全属性
        FALSE,          // 是否继承句柄
        CREATE_SUSPENDED, // <--- 关键：挂起模式
        NULL,           // 环境变量
        NULL,           // 当前目录
        &si,            // 启动信息
        &pi             // 进程信息 (返回)
    );

    if (!bRet)
    {
        printf("[x] 启动游戏失败。错误码: %d\n", GetLastError());
        system("pause");
        return 1;
    }

    printf("[*] 游戏进程已创建 (PID: %d)，状态：挂起。\n", pi.dwProcessId);

    // 5. 执行注入
    if (InjectDLL(&pi, fullDllPath))
    {
        // 6. 恢复游戏主线程
        printf("[*] 正在恢复游戏运行...\n");
        ResumeThread(pi.hThread);

        // 这里的逻辑看你需求：
        // 如果你希望 Loader 启动游戏后自动关闭，就保留下面的代码
        // 如果你希望 Loader 保持开启显示 Log，可以用 getchar()
    }
    else
    {
        printf("[x] 注入失败，正在终止游戏进程...\n");
        TerminateProcess(pi.hProcess, 0);
    }

    // 关闭句柄
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    // 稍作延时后退出
    Sleep(500);
    return 0;
}