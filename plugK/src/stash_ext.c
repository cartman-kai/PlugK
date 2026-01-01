#include "pch.h"
#include "stash_ext.h"
#include "config.h"
#include "inv_auto_sort.h"
#include <stdio.h>

// ---------------------------------------------------------
// 全局变量与定义
// ---------------------------------------------------------

static int g_StashPageB[50];
static int g_CurrentPageIndex = 0; // 0=A面, 1=B面
static int g_GameVersion = 0;

static DWORD g_RetAddr_Save = 0;
static DWORD g_RetAddr_Load = 0;
static DWORD g_RetAddr_Exit = 0;

#define ADDR_105_SAVE_HOOK 0x004815BA
#define ADDR_105_LOAD_HOOK 0x00481B2A
#define ADDR_105_EXIT_HOOK 0x004AD015
#define OFFSET_STASH_ARR 0x1FC

// [新增] 标记当前角色是否支持并成功加载了扩展箱逻辑
static BOOL g_IsStashExtReady = FALSE;

// 返回值：TRUE 表示成功获取合法名称，FALSE 表示名称非法
BOOL ExtractRoleName(const char *fullPath, char *outName, int maxLen)
{
    if (!fullPath || !outName || maxLen <= 0)
        return FALSE;

    // 1. 找到最后一个 '\' (路径结束)
    const char *pSlash = strrchr(fullPath, '\\');
    const char *pStart = (pSlash) ? pSlash + 1 : fullPath;

    // 2. 找到第一个 '.' (扩展名开始)
    const char *pDot = strchr(pStart, '.'); // 使用 strchr 找第一个点更稳妥

    int len = 0;
    if (pDot)
    {
        len = (int)(pDot - pStart);
    }
    else
    {
        len = (int)strlen(pStart);
    }

    // --- 安全检查：如果没有名称，或者名称太短 ---
    if (len <= 0)
        return FALSE;
    if (len >= maxLen)
        len = maxLen - 1;

    // 3. 拷贝
    strncpy_s(outName, maxLen, pStart, len);
    outName[len] = '\0';

    // --- 进一步检查：是否全是空格 ---
    // 有些用户可能输入一串空格，这在 Windows 文件名中也是危险的
    BOOL hasValidChar = FALSE;
    for (int i = 0; i < len; i++)
    {
        if (outName[i] != ' ')
        {
            hasValidChar = TRUE;
            break;
        }
    }

    return hasValidChar;
}

BOOL GetExtSavePath(const char *originalPath, char *outPath, int maxLen)
{
    char roleName[64] = {0};

    // 如果名称提取失败（非法字符或为空），直接返回 FALSE
    if (!ExtractRoleName(originalPath, roleName, 64))
    {
        return FALSE;
    }

    char dllPath[MAX_PATH];
    GetModuleFileNameA(NULL, dllPath, MAX_PATH);
    char *lastSlash = strrchr(dllPath, '\\');
    if (lastSlash)
        *lastSlash = 0;

    // 只有名称合法，才拼接路径
    _snprintf_s(outPath, maxLen, _TRUNCATE, "%s\\Save\\%s.pks", dllPath, roleName);
    return TRUE;
}

// ---------------------------------------------------------
// 业务逻辑 (改回默认调用约定，由ASM负责平栈)
// ---------------------------------------------------------

void ProcessSaveStash(const char *savePath)
{
    if (!savePath)
        return;

    char pkPath[MAX_PATH];
    // [检查点] 如果路径非法，直接退出，不写文件
    if (!GetExtSavePath(savePath, pkPath, MAX_PATH))
    {
        return;
    }

    DWORD charBase = GetCharacterBase();
    if (charBase == 0)
        return;

    int *gameStash = (int *)(charBase + OFFSET_STASH_ARR);
    int *dataToWrite = g_StashPageB;

    FILE *fp = fopen(pkPath, "wb");
    if (fp)
    {
        fwrite(dataToWrite, sizeof(int), 50, fp);
        fclose(fp);
    }
}

void ProcessLoadStash(const char *loadPath)
{
    if (!loadPath)
        return;

    // 1. 无论如何先清理旧数据
    g_CurrentPageIndex = 0;
    for (int i = 0; i < 50; i++)
        g_StashPageB[i] = -1;

    char pkPath[MAX_PATH];
    // 2. 检查文件名合法性 (处理你提到的 / \ 空格等问题)
    // [检查点] 如果路径非法，说明该角色不支持扩展箱，直接返回
    if (!GetExtSavePath(loadPath, pkPath, MAX_PATH))
    {
        g_IsStashExtReady = FALSE; // 名称非法，不开启功能
        return;
    }

    FILE *fp = fopen(pkPath, "rb");
    if (fp)
    {
        // 3. 名称合法，标志位置为 TRUE，表示此角色允许使用扩展箱
        g_IsStashExtReady = TRUE;
        fread(g_StashPageB, sizeof(int), 50, fp);
        fclose(fp);
    }
}

void ProcessCleanupStash()
{
    for (int i = 0; i < 50; i++)
        g_StashPageB[i] = -1;
    g_CurrentPageIndex = 0;
    g_IsStashExtReady = FALSE; // 角色退出，关闭功能
}

void ToggleStash()
{
    // 如果功能未就绪（非法名称角色或未在游戏内），直接拦截
    if (!g_IsStashExtReady)
    {
        Beep(200, 100); // 提示音：功能不可用
        return;
    }

    DWORD charBase = GetCharacterBase();
    if (charBase == 0)
        return;

    int *gameStash = (int *)(charBase + OFFSET_STASH_ARR);

    for (int i = 0; i < 50; i++)
    {
        int temp = gameStash[i];
        gameStash[i] = g_StashPageB[i];
        g_StashPageB[i] = temp;
    }

    g_CurrentPageIndex = !g_CurrentPageIndex;

    if (g_CurrentPageIndex == 0)
    {
        Beep(500, 100);
    }
    else
    {
        Beep(1000, 100);
    }
}

// ---------------------------------------------------------
// Hooks (Naked Functions - 修正版)
// ---------------------------------------------------------

// Hook @ 004815BA (Save)
// 使用 PUSHAD 保存所有寄存器 (32字节) + PUSHFD (4字节) = 36 (0x24)
// 原始偏移 0x3C + 0x24 = 0x60
void __declspec(naked) Hook_Save_Entry()
{
    __asm {
        pushad // 保存 EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI
        pushfd // 保存标志位

                // 计算参数地址: ESP + 0x60
        lea eax, [esp + 0x60]
        mov eax, [eax] // eax 现在是 char* savePath

        // 压入参数
        push eax
        call ProcessSaveStash
        add esp, 4 // 手动平栈 (Cdecl)

        popfd
        popad

                // 恢复原始指令
        mov eax, 1
        jmp g_RetAddr_Save
    }
}

// Hook @ 00481B2A (Load)
// 使用 PUSHAD (32) + PUSHFD (4) = 36 (0x24)
// 原始偏移 0x6C + 0x24 = 0x90
void __declspec(naked) Hook_Load_Entry()
{
    __asm {
        pushad
        pushfd

                // 计算参数地址: ESP + 0x90
        lea eax, [esp + 0x90]
        mov eax, [eax] // eax 现在是 char* loadPath

        // 压入参数
        push eax
        call ProcessLoadStash
        add esp, 4 // 手动平栈 (Cdecl)

        popfd
        popad

                // 恢复原始指令
        mov eax, 1
        jmp g_RetAddr_Load
    }
}

// Hook @ 004AD015 (Exit Cleanup)
// 这里没有参数需要从栈上读，所以 pushad 不影响逻辑
void __declspec(naked) Hook_Exit_Entry()
{
    __asm {
        pushad
        pushfd

        call ProcessCleanupStash
                    // 无参数，不需要 add esp

        popfd
        popad

                            // 恢复原始指令 (10 bytes)
                            // mov dword ptr ds:[0x005483A8], 2
        mov eax, 0x005483A8
        mov dword ptr [eax], 2

        jmp g_RetAddr_Exit
    }
}

void InstallStashExtJMPHook(DWORD address, void *hookFunc, DWORD *retAddr, int instrLen)
{
    DWORD oldProtect;
    VirtualProtect((void *)address, instrLen, PAGE_EXECUTE_READWRITE, &oldProtect);

    *(BYTE *)address = 0xE9;
    DWORD offset = (DWORD)hookFunc - address - 5;
    *(DWORD *)(address + 1) = offset;

    for (int i = 5; i < instrLen; i++)
    {
        *(BYTE *)(address + i) = 0x90;
    }

    *retAddr = address + instrLen;
    VirtualProtect((void *)address, instrLen, oldProtect, &oldProtect);
}

// ---------------------------------------------------------
// 初始化
// ---------------------------------------------------------

DWORD WINAPI StashInputThread(LPVOID lpParam)
{
    while (1)
    {
        Sleep(100);
        if (g_pk_config.stash_ext_enabled)
        {
            if (GetAsyncKeyState(VK_OEM_COMMA) & 0x8000)
            {
                ToggleStash();
                Sleep(176);
            }
        }
    }
    return 0;
}

void Mod_Stash_Ext_Init(int ver)
{
    if (!g_pk_config.stash_ext_enabled)
        return;
    if (ver != 105)
        return;
    g_GameVersion = ver;

    for (int i = 0; i < 50; i++)
        g_StashPageB[i] = -1;
    g_CurrentPageIndex = 0;

    InstallStashExtJMPHook(ADDR_105_SAVE_HOOK, Hook_Save_Entry, &g_RetAddr_Save, 5);
    InstallStashExtJMPHook(ADDR_105_LOAD_HOOK, Hook_Load_Entry, &g_RetAddr_Load, 5);
    InstallStashExtJMPHook(ADDR_105_EXIT_HOOK, Hook_Exit_Entry, &g_RetAddr_Exit, 10);

    CreateThread(NULL, 0, StashInputThread, NULL, 0, NULL);
}

void Mod_Stash_Ext_Cleanup()
{
}