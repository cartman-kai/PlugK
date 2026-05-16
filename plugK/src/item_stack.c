#include "pch.h"
#include "item_stack.h"
#include "config.h"
#include "show_tips.h"
#include <stdio.h>

// --------------------------------------------------------
// 全局状态管理
// --------------------------------------------------------
static BOOL g_bIsItemStackActive = FALSE; // 默认为关闭
static int g_CurrentVersion = 0;

// 地址缓存
static DWORD g_Addr_MinCmp = 0;
static DWORD g_Addr_MaxCmp = 0;

typedef enum StackLimitPatchType
{
    STACK_LIMIT_BYTE,
    STACK_LIMIT_DWORD,
    STACK_LIMIT_NEG_BYTE
} StackLimitPatchType;

typedef struct StackLimitPatch
{
    DWORD Address;
    int Offset;
    StackLimitPatchType Type;
} StackLimitPatch;

static StackLimitPatch g_LimitPatches[16];
static int g_LimitPatchCount = 0;

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

void MemoryPatchDword(DWORD targetAddr, int offset, DWORD newValue)
{
    if (targetAddr == 0)
        return;

    DWORD address = targetAddr + offset;
    DWORD oldProtect;

    if (VirtualProtect((LPVOID)address, sizeof(DWORD), PAGE_EXECUTE_READWRITE, &oldProtect))
    {
        *(DWORD *)address = newValue;
        VirtualProtect((LPVOID)address, sizeof(DWORD), oldProtect, &oldProtect);
    }
}

int GetItemStackLimit()
{
    if (!g_pk_config.item_stack_limit_enabled)
        return 9;

    if (g_pk_config.item_stack_limit < 1)
        return 1;
    if (g_pk_config.item_stack_limit > 127)
        return 127;
    return g_pk_config.item_stack_limit;
}

static void AddLimitPatch(DWORD address, int offset, StackLimitPatchType type)
{
    if (g_LimitPatchCount >= (int)(sizeof(g_LimitPatches) / sizeof(g_LimitPatches[0])))
        return;

    g_LimitPatches[g_LimitPatchCount].Address = address;
    g_LimitPatches[g_LimitPatchCount].Offset = offset;
    g_LimitPatches[g_LimitPatchCount].Type = type;
    g_LimitPatchCount++;
}

static void ApplyItemStackLimitPatch(void)
{
    int limit = GetItemStackLimit();

    for (int i = 0; i < g_LimitPatchCount; i++)
    {
        StackLimitPatch *patch = &g_LimitPatches[i];
        if (patch->Type == STACK_LIMIT_DWORD)
        {
            MemoryPatchDword(patch->Address, patch->Offset, (DWORD)limit);
        }
        else if (patch->Type == STACK_LIMIT_NEG_BYTE)
        {
            MemoryPatchByte(patch->Address, patch->Offset, (BYTE)(0 - limit));
        }
        else
        {
            MemoryPatchByte(patch->Address, patch->Offset, (BYTE)limit);
        }
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
    }
    else
    {
        // 关闭：还原为原始游戏数值
        MemoryPatchByte(g_Addr_MinCmp, CMP_OFFSET, ORIG_MIN);
        MemoryPatchByte(g_Addr_MaxCmp, CMP_OFFSET, ORIG_MAX);
    }
}

// --------------------------------------------------------
// 对外接口：切换状态
// --------------------------------------------------------
void ToggleItemStackState()
{
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
    g_bIsItemStackActive = g_pk_config.enable_gem_stack;
    g_LimitPatchCount = 0;

    if (game_version == 105)
    {
        g_Addr_MinCmp = 0x0047F013;
        g_Addr_MaxCmp = 0x0047F018;

        // 仓库合并: cmp/add/mov 中的 9
        AddLimitPatch(0x0047F4D7, 2, STACK_LIMIT_BYTE);
        AddLimitPatch(0x0047F4DC, 2, STACK_LIMIT_NEG_BYTE);
        AddLimitPatch(0x0047F4DF, 3, STACK_LIMIT_DWORD);

        // 背包与道具栏 setItemCount: cmp/mov/add 中的 9
        AddLimitPatch(0x0047CEE5, 2, STACK_LIMIT_BYTE);
        AddLimitPatch(0x0047CEEA, 3, STACK_LIMIT_DWORD);
        AddLimitPatch(0x0047CEF1, 2, STACK_LIMIT_NEG_BYTE);
    }
    else if (game_version == 201)
    {
        g_Addr_MinCmp = 0x0048DE26;
        g_Addr_MaxCmp = 0x0048DE2B;

        // 仓库合并: cmp/add/mov 中的 9
        AddLimitPatch(0x0048E2E7, 2, STACK_LIMIT_BYTE);
        AddLimitPatch(0x0048E2EC, 2, STACK_LIMIT_NEG_BYTE);
        AddLimitPatch(0x0048E2EF, 3, STACK_LIMIT_DWORD);

        // 背包合并判断: cmp/mov 中的 9
        AddLimitPatch(0x0048DBC0, 2, STACK_LIMIT_BYTE);
        AddLimitPatch(0x0048DBC5, 1, STACK_LIMIT_DWORD);
    }
    else
    {
        g_Addr_MinCmp = 0;
        g_Addr_MaxCmp = 0;
    }

    ApplyItemStackPatch(g_bIsItemStackActive);
    ApplyItemStackLimitPatch();

    // 初始化时不执行 Patch，因为默认是关闭的 (Original Values 本来就在内存里)
    // 如果你希望 config=true 时启动即开启，可以在这里调用 ApplyItemStackPatch(TRUE);
}
