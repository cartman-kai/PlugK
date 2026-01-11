#include "pch.h"
#include "stash_ext.h"
#include "config.h"
#include "inv_auto_sort.h"
#include "show_tips.h"
#include <stdio.h>
#include <MinHook.h>

// ---------------------------------------------------------
// 全局变量
// ---------------------------------------------------------

// 扩展页面缓存 (索引 1-9)
int g_StashPages[MAX_PAGES - 1][50];
int g_InvPages[MAX_PAGES - 1][50];

// 第 0 页备份缓存 (当玩家切到其他页时，第 0 页的数据存在这里)
int g_StashPageZero[50];
int g_InvPageZero[50];

// 当前页索引
int g_CurrentStashIdx = 0; // 0 是游戏原始内存页
int g_CurrentInvIdx = 0;

static int g_GameVersion = 0;
static BOOL g_IsStashExtReady = FALSE;

// Hook 返回地址
static DWORD g_RetAddr_Save = 0;
static DWORD g_RetAddr_Load = 0;
static DWORD g_RetAddr_Exit = 0;

// 内存偏移
#define OFFSET_STASH_ARR 0x1FC
#define OFFSET_INV_ARR 0xA4

// 1.05 地址
#define ADDR_105_SAVE_HOOK 0x004815BA
#define ADDR_105_LOAD_HOOK 0x00481B2A
#define ADDR_105_EXIT_HOOK 0x004AD015

// 2.01 地址
#define ADDR_201_SAVE_HOOK 0x00461B80
#define ADDR_201_LOAD_HOOK 0x00490953
#define ADDR_201_EXIT_HOOK 0x004BFDC5

// 存档结构定义 (V3)
typedef struct
{
    char magic[4];       // "PKS3"
    int currentStashIdx; // 存档时玩家停留的页码
    int currentInvIdx;
    int stashPages[MAX_PAGES - 1][50]; // 9个扩展页
    int invPages[MAX_PAGES - 1][50];   // 9个扩展页
} PksFileV3;

// ---------------------------------------------------------
// 内部辅助
// ---------------------------------------------------------

void ResetCache()
{
    // 清空扩展页
    memset(g_StashPages, -1, sizeof(g_StashPages));
    memset(g_InvPages, -1, sizeof(g_InvPages));
    // 清空 Page 0 备份
    memset(g_StashPageZero, -1, sizeof(g_StashPageZero));
    memset(g_InvPageZero, -1, sizeof(g_InvPageZero));

    g_CurrentStashIdx = 0;
    g_CurrentInvIdx = 0;
    g_IsStashExtReady = FALSE;
}

// ---------------------------------------------------------
// 数据指针获取辅助 (供 inv_auto_sort 和 auto_fill 使用)
// ---------------------------------------------------------

// 获取背包某一页的数据指针
int *GetInvPagePtr(int logicalIdx)
{
    if (logicalIdx < 0 || logicalIdx >= MAX_PAGES)
        return NULL;

    // 1. 如果请求的是当前正在显示的页面 -> 返回游戏内存地址
    if (logicalIdx == g_CurrentInvIdx)
    {
        DWORD charBase = GetCharacterBase();
        if (charBase)
            return (int *)(charBase + OFFSET_INV_ARR);
        return NULL;
    }

    // 2. 如果请求的是第 0 页，且当前不在第 0 页 -> 返回 Page0 缓存
    if (logicalIdx == 0)
    {
        return g_InvPageZero;
    }

    // 3. 其他情况 -> 返回 g_InvPages 数组 (注意下标偏移，g_InvPages[0] 存的是 Page 1)
    return g_InvPages[logicalIdx - 1];
}

// 获取储物箱某一页的数据指针
int *GetStashPagePtr(int logicalIdx)
{
    if (logicalIdx < 0 || logicalIdx >= MAX_PAGES)
        return NULL;

    if (logicalIdx == g_CurrentStashIdx)
    {
        DWORD charBase = GetCharacterBase();
        if (charBase)
            return (int *)(charBase + OFFSET_STASH_ARR);
        return NULL;
    }

    if (logicalIdx == 0)
    {
        return g_StashPageZero;
    }

    return g_StashPages[logicalIdx - 1];
}

// ---------------------------------------------------------
// 辅助函数
// ---------------------------------------------------------

BOOL ExtractRoleName(const char *fullPath, char *outName, int maxLen)
{
    if (!fullPath || !outName || maxLen <= 0)
        return FALSE;
    const char *pSlash = strrchr(fullPath, '\\');
    const char *pStart = (pSlash) ? pSlash + 1 : fullPath;
    const char *pDot = strchr(pStart, '.');
    int len = (pDot) ? (int)(pDot - pStart) : (int)strlen(pStart);
    if (len <= 0 || len >= maxLen)
        return FALSE;
    strncpy_s(outName, maxLen, pStart, len);
    outName[len] = '\0';
    return TRUE;
}

BOOL GetExtSavePath(const char *originalPath, char *outPath, int maxLen)
{
    char roleName[64] = {0};
    if (!ExtractRoleName(originalPath, roleName, 64))
        return FALSE;
    char dllPath[MAX_PATH];
    GetModuleFileNameA(NULL, dllPath, MAX_PATH);
    char *lastSlash = strrchr(dllPath, '\\');
    if (lastSlash)
        *lastSlash = 0;
    _snprintf_s(outPath, maxLen, _TRUNCATE, "%s\\Save\\%s.pks", dllPath, roleName);
    return TRUE;
}

// ---------------------------------------------------------
// 存档与读档
// ---------------------------------------------------------

void ProcessSaveExt(const char *savePath)
{
    if (!savePath)
        return;
    char pkPath[MAX_PATH];
    if (!GetExtSavePath(savePath, pkPath, MAX_PATH))
        return;

    PksFileV3 data;
    memcpy(data.magic, "PKS3", 4);
    data.currentStashIdx = g_CurrentStashIdx;
    data.currentInvIdx = g_CurrentInvIdx;
    memcpy(data.stashPages, g_StashPages, sizeof(g_StashPages));
    memcpy(data.invPages, g_InvPages, sizeof(g_InvPages));

    FILE *fp = NULL;
    if (fopen_s(&fp, pkPath, "wb") == 0 && fp)
    {
        fwrite(&data, sizeof(PksFileV3), 1, fp);
        fclose(fp);
        g_IsStashExtReady = TRUE;
    }
}

void ProcessLoadExt(const char *loadPath)
{
    ResetCache();
    if (!loadPath)
        return;

    char pkPath[MAX_PATH];
    if (!GetExtSavePath(loadPath, pkPath, MAX_PATH))
        return;

    FILE *fp = NULL;
    if (fopen_s(&fp, pkPath, "rb") != 0 || !fp)
    {
        g_IsStashExtReady = TRUE;
        return;
    }

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (size >= (long)sizeof(PksFileV3))
    {
        // V3 格式
        PksFileV3 data;
        if (fread(&data, sizeof(PksFileV3), 1, fp) == 1 && strncmp(data.magic, "PKS3", 4) == 0)
        {
            g_CurrentStashIdx = data.currentStashIdx;
            g_CurrentInvIdx = data.currentInvIdx;
            memcpy(g_StashPages, data.stashPages, sizeof(g_StashPages));
            memcpy(g_InvPages, data.invPages, sizeof(g_InvPages));
        }
    }
    else if (size == 404)
    {
        // V2 格式兼容
        char magic[4];
        fread(magic, 4, 1, fp);
        if (strncmp(magic, "PKS2", 4) == 0)
        {
            fread(g_StashPages[0], sizeof(int), 50, fp); // 旧版 B 面放入 Page 1
            fread(g_InvPages[0], sizeof(int), 50, fp);
        }
    }

    fclose(fp);
    g_IsStashExtReady = TRUE;
}

// ---------------------------------------------------------
// 环形切换逻辑
// ---------------------------------------------------------

void SwapToPage(DWORD offset, int *currentIdx, int cacheArray[MAX_PAGES - 1][50], int *zeroPageCache, int direction)
{
    DWORD charBase = GetCharacterBase();
    if (!charBase || !g_IsStashExtReady)
        return;

    int *gameArr = (int *)(charBase + offset);
    int nextIdx = (*currentIdx + direction) % MAX_PAGES;
    if (nextIdx < 0)
        nextIdx += MAX_PAGES;

    if (*currentIdx == nextIdx)
        return;

    // 确定 "当前页" 切走后应该存哪里
    // 如果当前是 0 页，存入 zeroPageCache
    // 如果当前是 1-9 页，存入 cacheArray[idx-1]
    int *oldCachePtr = (*currentIdx == 0) ? zeroPageCache : cacheArray[*currentIdx - 1];

    // 确定 "下一页" 数据从哪里读
    int *nextCachePtr = (nextIdx == 0) ? zeroPageCache : cacheArray[nextIdx - 1];

    // 1. 备份当前内存数据到对应的缓存位置
    memcpy(oldCachePtr, gameArr, 50 * sizeof(int));

    // 2. 将目标缓存数据覆盖到内存
    memcpy(gameArr, nextCachePtr, 50 * sizeof(int));

    *currentIdx = nextIdx;
}

void ToggleStashEx(int direction)
{
    SwapToPage(OFFSET_STASH_ARR, &g_CurrentStashIdx, g_StashPages, g_StashPageZero, direction);
    char buf[64];
    _snprintf_s(buf, 64, _TRUNCATE, "[储物箱] 已切换至第 %d 页", g_CurrentStashIdx + 1);
    ShowGameLog(buf);
}

void ToggleInventoryEx(int direction)
{
    SwapToPage(OFFSET_INV_ARR, &g_CurrentInvIdx, g_InvPages, g_InvPageZero, direction);
    char buf[64];
    _snprintf_s(buf, 64, _TRUNCATE, "[背包] 已切换至第 %d 页", g_CurrentInvIdx + 1);
    ShowGameLog(buf);
}

// 兼容旧接口
void ToggleStash() { ToggleStashEx(1); }
void ToggleInventory() { ToggleInventoryEx(1); }

// 强制切换页面 (用于自动填充)
void ForceSwitchPage(int type, int targetIdx)
{
    int current = (type == 0) ? g_CurrentInvIdx : g_CurrentStashIdx;
    int diff = targetIdx - current;

    if (diff == 0)
        return;

    if (type == 0)
        ToggleInventoryEx(diff);
    else
        ToggleStashEx(diff);
}

// ---------------------------------------------------------
// Hooks - 1.05
// ---------------------------------------------------------

void __declspec(naked) Hook_105_Save()
{
    __asm {
        pushad
        pushfd
        lea eax, [esp + 0x60] // 0x3C(ret) + 0x24(pushad)
        mov eax, [eax]
        push eax
        call ProcessSaveExt
        add esp, 4
        popfd
        popad;
        mov eax, 1
        jmp g_RetAddr_Save
    }
}

void __declspec(naked) Hook_105_Load()
{
    __asm {
        pushad
        pushfd
        lea eax, [esp + 0x90] // 0x6C + 0x24
        mov eax, [eax]
        push eax
        call ProcessLoadExt
        add esp, 4
        popfd
        popad
        mov eax, 1
        jmp g_RetAddr_Load
    }
}

void __declspec(naked) Hook_105_Exit()
{
    __asm {
        pushad
        pushfd
        call ResetCache
        popfd
        popad
        mov eax, 0x005483A8
        mov dword ptr [eax], 2
        jmp g_RetAddr_Exit
    }
}

// ---------------------------------------------------------
// Hooks - 2.01 (新实现)
// ---------------------------------------------------------

// 2.01 Save Hook @ 00461B80
// 覆盖：
// 00461B80 | 8B 56 1C   | mov edx, dword ptr ds:[esi+1C]
// 00461B83 | 83 C4 04   | add esp, 4
// 总计 6 字节

void __declspec(naked) Hook_201_Save()
{
    __asm {
        // 1. 保存现场
        pushad
        pushfd

                // 2. 获取路径
                // 原始 esp+24 处是路径指针。
                // 经过 pushad(32) + pushfd(4) = 36 (0x24) 字节
                // 所以现在在 esp + 0x24 + 0x24 = 0x48
        mov eax, [esp + 0x48]
        push eax
        call ProcessSaveExt
        add esp, 4

        // 3. 恢复现场
        popfd
        popad

                // 4. 补偿被覆盖的指令
        mov edx, dword ptr ds:[esi+0x1C] // 原指令 1
        add esp, 4 // 原指令 2

                   // 5. 跳转回 00461B86
        jmp g_RetAddr_Save
    }
}

// Hook @ 00490953 (Load)
// 此时 ESP+78 (0x4E) 是路径
// 0x4E + 0x24 = 0x72 (注意: 78h 是 hex, 0x78)
// 0x78 + 0x24 = 0x9C
void __declspec(naked) Hook_201_Load()
{
    __asm {
        pushad
        pushfd
        lea eax, [esp + 0x9C]
        mov eax, [eax]
        push eax
        call ProcessLoadExt
        add esp, 4
        popfd
        popad

                // 恢复原指令
                // mov ecx,dword ptr ss:[esp+44]
        mov ecx, dword ptr ss:[esp+0x44]
        pop edi
        
        jmp g_RetAddr_Load
    }
}

// Hook @ 004BFDC5 (Exit)
// 原指令: mov dword ptr ds:[578938], 2 (10 bytes: C7 05 38 89 57 00 02 00 00 00)
void __declspec(naked) Hook_201_Exit()
{
    __asm {
        pushad
        pushfd
        call ResetCache
        popfd
        popad

                            // 恢复原指令
        mov eax, 0x00578938
        mov dword ptr [eax], 2
        
        jmp g_RetAddr_Exit
    }
}

// ---------------------------------------------------------
// 初始化
// ---------------------------------------------------------

void InstallStashExtJmp(DWORD addr, void *func, DWORD *ret, int len)
{
    DWORD old;
    VirtualProtect((void *)addr, len, PAGE_EXECUTE_READWRITE, &old);
    *(BYTE *)addr = 0xE9;
    *(DWORD *)(addr + 1) = (DWORD)func - addr - 5;
    for (int i = 5; i < len; i++)
        *(BYTE *)(addr + i) = 0x90;
    *ret = addr + len;
    VirtualProtect((void *)addr, len, old, &old);
}

void Mod_Stash_Ext_Init(int ver)
{
    if (!g_pk_config.stash_ext_enabled)
        return;
    g_GameVersion = ver;
    ResetCache();

    if (ver == 105)
    {
        InstallStashExtJmp(ADDR_105_SAVE_HOOK, Hook_105_Save, &g_RetAddr_Save, 5);
        InstallStashExtJmp(ADDR_105_LOAD_HOOK, Hook_105_Load, &g_RetAddr_Load, 5);
        InstallStashExtJmp(ADDR_105_EXIT_HOOK, Hook_105_Exit, &g_RetAddr_Exit, 10);
    }
    else if (ver == 201)
    {
        InstallStashExtJmp(ADDR_201_SAVE_HOOK, Hook_201_Save, &g_RetAddr_Save, 6);
        InstallStashExtJmp(ADDR_201_LOAD_HOOK, Hook_201_Load, &g_RetAddr_Load, 5);
        InstallStashExtJmp(ADDR_201_EXIT_HOOK, Hook_201_Exit, &g_RetAddr_Exit, 10);
    }
}