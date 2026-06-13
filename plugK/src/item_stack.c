#include "pch.h"
#include "item_stack.h"
#include "config.h"
#include "show_tips.h"
#include <stdio.h>
#include <string.h>

// --------------------------------------------------------
// 全局状态管理
// --------------------------------------------------------
static BOOL g_bIsItemStackActive = FALSE; // 默认为关闭
static int g_CurrentVersion = 0;

// 地址缓存
static DWORD g_Addr_MinCmp = 0;
static DWORD g_Addr_MaxCmp = 0;
static DWORD g_Addr_ItemInNewSlotHook = 0;
static DWORD g_Addr_ItemInNewSlotRet = 0;
static DWORD g_Addr_TableGetInt = 0;
static DWORD g_Addr_FindConsumableSlot = 0;
static DWORD g_Addr_FindThrowSlot = 0;
static DWORD g_Addr_FindInventorySlot = 0;

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

static const BYTE ORIG_ITEM_IN_NEW_SLOT_105[] = {
    0x8B, 0xCB,                         // mov ecx, ebx
    0xE8, 0xF2, 0x01, 0x00, 0x00,       // call sub_47F270
    0x89, 0x44, 0x24, 0x30,             // mov [esp+30h], eax
    0xEB, 0x3B                          // jmp loc_47F0BF
};

static const BYTE ORIG_ITEM_IN_NEW_SLOT_201[] = {
    0x8B, 0xCB,                         // mov ecx, ebx
    0xE8, 0xEF, 0x01, 0x00, 0x00,       // call sub_48E080
    0x89, 0x44, 0x24, 0x30,             // mov [esp+30h], eax
    0xEB, 0x3B                          // jmp loc_48DED2
};

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

static void MemoryPatchBytes(DWORD targetAddr, const BYTE *data, SIZE_T size)
{
    if (targetAddr == 0 || data == NULL || size == 0)
        return;

    DWORD oldProtect;
    if (VirtualProtect((LPVOID)targetAddr, size, PAGE_EXECUTE_READWRITE, &oldProtect))
    {
        memcpy((void *)targetAddr, data, size);
        VirtualProtect((LPVOID)targetAddr, size, oldProtect, &oldProtect);
        FlushInstructionCache(GetCurrentProcess(), (LPCVOID)targetAddr, size);
    }
}

static void MemoryPatchJump(DWORD source, DWORD target, SIZE_T length)
{
    if (source == 0 || target == 0 || length < 5)
        return;

    BYTE patch[16];
    if (length > sizeof(patch))
        return;

    memset(patch, 0x90, length);
    patch[0] = 0xE9;
    *(DWORD *)(patch + 1) = target - source - 5;
    MemoryPatchBytes(source, patch, length);
}

// ItemIn 在“找不到可合并堆叠，需要新建物品对象”时会先选择一个槽位。
// 原版只有 Type 20-29 会走这里；物品叠加补丁把范围扩展到 9-36 后，
// Type 10-19 和 Type 30-35 也会误用 Type 20-29 的投掷栏槽位。
// 这里按原游戏槽位规则重新分派：
//   Type 10-19 -> 50-55 快捷道具槽
//   Type 20-29 -> 56-61 投掷物品快捷槽
//   其它类型   -> 0-49  普通背包
__declspec(naked) void ItemInNewSlotDispatch()
{
    __asm {
        mov eax, [esp+14h]
        test eax, eax
        jz use_inventory_slot

        push 2
        mov ecx, eax
        call dword ptr [g_Addr_TableGetInt]

        cmp eax, 10
        jl use_inventory_slot
        cmp eax, 19
        jle use_consumable_slot
        cmp eax, 20
        jl use_inventory_slot
        cmp eax, 29
        jle use_throw_slot
        jmp use_inventory_slot

    use_consumable_slot:
        mov ecx, ebx
        call dword ptr [g_Addr_FindConsumableSlot]
        jmp store_slot

    use_throw_slot:
        mov ecx, ebx
        call dword ptr [g_Addr_FindThrowSlot]
        jmp store_slot

    use_inventory_slot:
        mov ecx, ebx
        call dword ptr [g_Addr_FindInventorySlot]

    store_slot:
        mov [esp+30h], eax
        jmp dword ptr [g_Addr_ItemInNewSlotRet]
    }
}

static void ApplyItemInNewSlotPatch(BOOL enable)
{
    if (g_Addr_ItemInNewSlotHook == 0)
        return;

    if (enable)
    {
        MemoryPatchJump(g_Addr_ItemInNewSlotHook,
                        (DWORD)ItemInNewSlotDispatch,
                        sizeof(ORIG_ITEM_IN_NEW_SLOT_105));
    }
    else
    {
        if (g_CurrentVersion == 201)
        {
            MemoryPatchBytes(g_Addr_ItemInNewSlotHook,
                             ORIG_ITEM_IN_NEW_SLOT_201,
                             sizeof(ORIG_ITEM_IN_NEW_SLOT_201));
        }
        else
        {
            MemoryPatchBytes(g_Addr_ItemInNewSlotHook,
                             ORIG_ITEM_IN_NEW_SLOT_105,
                             sizeof(ORIG_ITEM_IN_NEW_SLOT_105));
        }
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
        ApplyItemInNewSlotPatch(TRUE);
    }
    else
    {
        // 关闭：还原为原始游戏数值
        MemoryPatchByte(g_Addr_MinCmp, CMP_OFFSET, ORIG_MIN);
        MemoryPatchByte(g_Addr_MaxCmp, CMP_OFFSET, ORIG_MAX);
        ApplyItemInNewSlotPatch(FALSE);
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
        // 1.05: sub_47EF40
        g_Addr_MinCmp = 0x0047F013;
        g_Addr_MaxCmp = 0x0047F018;
        g_Addr_ItemInNewSlotHook = 0x0047F077;
        g_Addr_ItemInNewSlotRet = 0x0047F0BF;
        g_Addr_TableGetInt = 0x004D0210;
        g_Addr_FindConsumableSlot = 0x0047F250;
        g_Addr_FindThrowSlot = 0x0047F270;
        g_Addr_FindInventorySlot = 0x0047F290;

        // 仓库合并: cmp/add/mov 中的 9
        AddLimitPatch(0x0047F4D7, 2, STACK_LIMIT_BYTE);
        AddLimitPatch(0x0047F4DC, 2, STACK_LIMIT_NEG_BYTE);
        AddLimitPatch(0x0047F4DF, 3, STACK_LIMIT_DWORD);

        // 背包 setItemCount: cmp/mov/add 中的 9
        AddLimitPatch(0x0047CEE5, 2, STACK_LIMIT_BYTE);
        AddLimitPatch(0x0047CEEA, 3, STACK_LIMIT_DWORD);
        AddLimitPatch(0x0047CEF1, 2, STACK_LIMIT_NEG_BYTE);

        // 道具栏 cmp/add
        AddLimitPatch(0x0047EDD0, 2, STACK_LIMIT_BYTE);
        AddLimitPatch(0x0047EDD5, 1, STACK_LIMIT_DWORD);
    }
    else if (game_version == 201)
    {
        // 2.01: sub_48DD30
        g_Addr_MinCmp = 0x0048DE26;
        g_Addr_MaxCmp = 0x0048DE2B;
        g_Addr_ItemInNewSlotHook = 0x0048DE8A;
        g_Addr_ItemInNewSlotRet = 0x0048DED2;
        g_Addr_TableGetInt = 0x004E4EF0;
        g_Addr_FindConsumableSlot = 0x0048E060;
        g_Addr_FindThrowSlot = 0x0048E080;
        g_Addr_FindInventorySlot = 0x0048E0A0;

        // 仓库: cmp/add/mov 中的 9
        AddLimitPatch(0x0048E2E7, 2, STACK_LIMIT_BYTE);
        AddLimitPatch(0x0048E2EC, 2, STACK_LIMIT_NEG_BYTE);
        AddLimitPatch(0x0048E2EF, 3, STACK_LIMIT_DWORD);

        // 背包: cmp/mov/add 中的 9
        AddLimitPatch(0x0048BC15, 2, STACK_LIMIT_BYTE);
        AddLimitPatch(0x0048BC1A, 3, STACK_LIMIT_DWORD);
        AddLimitPatch(0x0048BC21, 2, STACK_LIMIT_NEG_BYTE);

        // 道具栏: cmp/mov 中的 9
        AddLimitPatch(0x0048DBC0, 2, STACK_LIMIT_BYTE);
        AddLimitPatch(0x0048DBC5, 1, STACK_LIMIT_DWORD);
    }
    else
    {
        g_Addr_MinCmp = 0;
        g_Addr_MaxCmp = 0;
        g_Addr_ItemInNewSlotHook = 0;
        g_Addr_ItemInNewSlotRet = 0;
        g_Addr_TableGetInt = 0;
        g_Addr_FindConsumableSlot = 0;
        g_Addr_FindThrowSlot = 0;
        g_Addr_FindInventorySlot = 0;
    }

    ApplyItemStackPatch(g_bIsItemStackActive);
    ApplyItemStackLimitPatch();

    // 初始化时不执行 Patch，因为默认是关闭的 (Original Values 本来就在内存里)
    // 如果你希望 config=true 时启动即开启，可以在这里调用 ApplyItemStackPatch(TRUE);
}
