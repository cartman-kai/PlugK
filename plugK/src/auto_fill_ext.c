#include "pch.h"
#include "stash_ext.h"
#include "config.h"
#include "inv_auto_sort.h"
#include "auto_fill_ext.h"
#include <stdio.h>
#include <MinHook.h>

#pragma comment(lib, "../deps/minhook/lib/libMinHook.x86.lib")

// =========================================================
// 全局状态
// =========================================================

// 记录自动填充发生前的原始页面，用于还原
static int g_OriginalPageIdx = -1;
static BOOL g_IsTempSwapped = FALSE;

typedef int(__fastcall *tFindEmptySlot)(void *thisPtr);
static tFindEmptySlot fpOriginalFindEmptySlot = NULL;

static void *fpOriginalItemInExit = NULL;

// =========================================================
// 核心逻辑
// =========================================================

// 检查指定逻辑页面是否有空位，返回 Slot Index (0-49)，无空位返回 -1
int CheckPageForSpace(int pageIdx)
{
    // 利用 stash_ext.h 提供的辅助函数获取该页面的数据指针
    // 无论它在内存中还是缓存中
    int *pageData = GetInvPagePtr(pageIdx);

    if (!pageData)
        return -1;

    for (int i = 0; i < 50; i++)
    {
        if (pageData[i] == -1) // -1 代表空位
        {
            return i;
        }
    }
    return -1;
}

// 还原现场
void RestoreOriginalPage()
{
    if (g_IsTempSwapped && g_OriginalPageIdx != -1)
    {
        // 强制切回原来的页面
        ForceSwitchPage(0, g_OriginalPageIdx);

        g_IsTempSwapped = FALSE;
        g_OriginalPageIdx = -1;
    }
}

// =========================================================
// Hook: 寻找空位
// =========================================================
int __fastcall Detour_FindEmptySlot(void *pCharBase)
{
    // 1. 原生逻辑：检查当前页面
    int slot = fpOriginalFindEmptySlot(pCharBase);

    // 如果找到了，或者功能没开，直接返回
    if (slot != -1 || !g_pk_config.enable_autofill_ext)
    {
        return slot;
    }

    // 2. 环形查找：从 (Current + 1) 开始，找遍所有 10 个页面
    int currentIdx = g_CurrentInvIdx;

    // 防止递归调用导致的死循环，如果已经在临时交换状态，就不再乱切了
    if (g_IsTempSwapped)
        return -1;

    for (int i = 1; i < MAX_PAGES; i++)
    {
        // 计算目标页面索引 (环形)
        int targetIdx = (currentIdx + i) % MAX_PAGES;

        // 检查目标页是否有空位 (只读检查，不发生切换)
        int targetSlot = CheckPageForSpace(targetIdx);

        if (targetSlot != -1)
        {
            // 找到了！
            // 1. 记录当前现场
            g_OriginalPageIdx = currentIdx;
            g_IsTempSwapped = TRUE;

            // 2. 执行切换：把目标页换到内存中
            // 游戏接下来的逻辑会将物品写入内存地址 (0xA4偏移处)
            ForceSwitchPage(0, targetIdx);

            // 3. 返回找到的格子索引
            return targetSlot;
        }
    }

    return -1;
}

// =========================================================
// Hook: ItemIn 函数结束
// =========================================================

void __stdcall Cleanup_Helper()
{
    // 只有发生过临时交换才还原
    if (g_IsTempSwapped)
    {
        RestoreOriginalPage();
    }
}

__declspec(naked) void Detour_ItemIn_Exit()
{
    __asm {
        pushad
        pushfd
        call Cleanup_Helper
        popfd
        popad
        jmp fpOriginalItemInExit
    }
}

// =========================================================
// 初始化
// =========================================================
void Mod_Auto_Fill_Init(int ver)
{
    if (!g_pk_config.enable_autofill_ext)
        return;

    LPVOID targetFindSlot = NULL;
    LPVOID targetFuncExit = NULL;

    if (ver == 105)
    {
        targetFindSlot = (LPVOID)0x0047F290;
        targetFuncExit = (LPVOID)0x0047EFBA;
    }
    else if (ver == 201)
    {
        targetFindSlot = (LPVOID)0x0048E0A0;
        targetFuncExit = (LPVOID)0x0048DDAA;
    }

    if (MH_Initialize() != MH_OK)
        return;

    MH_CreateHook(targetFindSlot, &Detour_FindEmptySlot, (LPVOID *)&fpOriginalFindEmptySlot);
    MH_CreateHook(targetFuncExit, &Detour_ItemIn_Exit, (LPVOID *)&fpOriginalItemInExit);

    MH_EnableHook(MH_ALL_HOOKS);
}