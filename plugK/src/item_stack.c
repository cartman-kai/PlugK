#include "pch.h"
#include "item_stack.h"
#include "config.h"
#include <stdio.h>

// --------------------------------------------------------
// 全局状态管理
// --------------------------------------------------------
static BOOL g_bIsItemStackActive = FALSE; // 默认为关闭
static int g_CurrentVersion = 0;

// 地址缓存
static DWORD g_Addr_MinCmp = 0;
static DWORD g_Addr_MaxCmp = 0;

// 常量定义
const int CMP_OFFSET = 2;
// 补丁值 (Patch Values)
const BYTE PATCH_MIN = 0x09;
const BYTE PATCH_MAX = 0x24;
// 原始值 (Original Values) - 根据你的文件注释得知
const BYTE ORIG_MIN = 0x14; // 20
const BYTE ORIG_MAX = 0x1D; // 29

// --------------------------------------------------------
// 内存 Patch 工具函数 (保持不变)
// --------------------------------------------------------
void MemoryPatchByte(DWORD targetAddr, int offset, BYTE newValue)
{
    if (targetAddr == 0)
        return;

    DWORD address = targetAddr + offset;
    DWORD oldProtect;

    if (VirtualProtect((LPVOID)address, 1, PAGE_EXECUTE_READWRITE, &oldProtect))
    {
        *(BYTE *)address = newValue;
        VirtualProtect((LPVOID)address, 1, oldProtect, &oldProtect);
    }
}

// --------------------------------------------------------
// 应用或还原补丁
// --------------------------------------------------------
void ApplyItemStackPatch(BOOL enable)
{
    if (g_Addr_MinCmp == 0 || g_Addr_MaxCmp == 0)
        return;

    if (enable)
    {
        // 开启：写入更宽的范围
        MemoryPatchByte(g_Addr_MinCmp, CMP_OFFSET, PATCH_MIN);
        MemoryPatchByte(g_Addr_MaxCmp, CMP_OFFSET, PATCH_MAX);
        // 提示音：高音代表开启
        Beep(800, 100);
    }
    else
    {
        // 关闭：还原为原始游戏数值
        MemoryPatchByte(g_Addr_MinCmp, CMP_OFFSET, ORIG_MIN);
        MemoryPatchByte(g_Addr_MaxCmp, CMP_OFFSET, ORIG_MAX);
        // 提示音：低音代表关闭
        Beep(400, 100);
    }
}

// --------------------------------------------------------
// 对外接口：切换状态
// --------------------------------------------------------
void ToggleItemStackState()
{
    // 如果 config 里完全禁用了该功能，则快捷键无效
    if (!g_pk_config.enable_item_stack)
        return;

    g_bIsItemStackActive = !g_bIsItemStackActive;
    ApplyItemStackPatch(g_bIsItemStackActive);
}

// --------------------------------------------------------
// 初始化函数
// --------------------------------------------------------
void Mod_item_stack_init(int game_version)
{
    // 即使 config 开启，初始状态也设为 FALSE (默认关闭)
    // 只有按下快捷键才激活
    g_CurrentVersion = game_version;
    g_bIsItemStackActive = FALSE;

    if (game_version == 105)
    {
        g_Addr_MinCmp = 0x0047F013;
        g_Addr_MaxCmp = 0x0047F018;
    }
    else if (game_version == 201)
    {
        g_Addr_MinCmp = 0x0048DE26;
        g_Addr_MaxCmp = 0x0048DE2B;
    }
    else
    {
        g_Addr_MinCmp = 0;
        g_Addr_MaxCmp = 0;
    }

    // 初始化时不执行 Patch，因为默认是关闭的 (Original Values 本来就在内存里)
    // 如果你希望 config=true 时启动即开启，可以在这里调用 ApplyItemStackPatch(TRUE);
}