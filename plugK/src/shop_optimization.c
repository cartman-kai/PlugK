#include "pch.h"
#include "shop_optimization.h"
#include "config.h"
#include <windows.h>
#include <stdlib.h> // for rand(), qsort()

// ---------------------------------------------------------
// 全局地址 (v1.05)
// ---------------------------------------------------------
// 1. 商店数量随机化 Hook
static DWORD g_Addr_ShopHook1 = 0x004536BF;
static DWORD g_Addr_ShopHook1_Ret = 0x004536D9;
static DWORD g_Addr_ShopHook2 = 0x00453AE2;
static DWORD g_Addr_ShopHook2_Ret = 0x00453AFC;

// 2. 物品模板堆叠补丁 Hook
static DWORD g_Addr_TemplateHook = 0x004D07FE;
static DWORD g_Addr_TemplateRet = 0x004D0804;
static DWORD g_Addr_TableCount = 0x00548340;
static DWORD g_Addr_TablePtr = 0x00548344;

// 3. [新增] 商店排序 Hook
// 00453A4A: mov large fs:0, ecx (7 bytes)
static DWORD g_Addr_ShopSortHook = 0x00453A4A;
// Hook 覆盖了 7 字节，后续指令是 add esp, 60h; retn 4
// 我们将在 Trampoline 中手动补全这些逻辑

// ---------------------------------------------------------
// 辅助结构体：用于排序
// ---------------------------------------------------------
typedef struct
{
    DWORD SlotIndex; // 原始格子索引 (调试用)
    DWORD ItemPtr;   // 物品对象指针
    DWORD ItemID;    // 物品ID
    DWORD ItemCount; // 物品数量
} ShopSortNode;

// ---------------------------------------------------------
// 排序比较函数
// ---------------------------------------------------------
int CompareShopItems(const void *a, const void *b)
{
    ShopSortNode *itemA = (ShopSortNode *)a;
    ShopSortNode *itemB = (ShopSortNode *)b;

    // 规则 1: 分组
    // Group 1: ID > 15 (装备、宝石等)
    // Group 2: ID <= 15 (消耗品、药水)
    int groupA = (itemA->ItemID > 15) ? 1 : 2;
    int groupB = (itemB->ItemID > 15) ? 1 : 2;

    // 如果组别不同，Group 1 排在 Group 2 前面
    if (groupA != groupB)
    {
        return groupA - groupB; // 1 - 2 = -1 (A在前)
    }

    // 如果组别相同
    if (groupA == 1)
    {
        // Group 1 (装备): ID 从大到小 (Descending)
        if (itemA->ItemID != itemB->ItemID)
        {
            return (int)itemB->ItemID - (int)itemA->ItemID;
        }
    }
    else
    {
        // Group 2 (消耗品): ID 从小到大 (Ascending)
        if (itemA->ItemID != itemB->ItemID)
        {
            return (int)itemA->ItemID - (int)itemB->ItemID;
        }
    }

    // 如果 ID 相同，按照数量从大到小 (Descending)
    if (itemA->ItemCount != itemB->ItemCount)
    {
        return (int)itemB->ItemCount - (int)itemA->ItemCount;
    }

    return 0;
}

// ---------------------------------------------------------
// 执行商店排序逻辑 (C函数)
// ---------------------------------------------------------
void PerformShopSort(DWORD shopAddress)
{
    if (shopAddress == 0 || IsBadReadPtr((void *)shopAddress, 0xC8))
        return;

    // 商店结构：+0x00 标识, +0x04 开始是 50 个指针
    DWORD *pSlotArray = (DWORD *)(shopAddress + 0x04);

    ShopSortNode items[50];
    int validCount = 0;

    // 1. 读取有效物品
    for (int i = 0; i < 50; i++)
    {
        DWORD itemPtr = pSlotArray[i];

        if (itemPtr != 0 && !IsBadReadPtr((void *)itemPtr, 0x20))
        {
            // 读取物品 ID (+0x18) 和 数量 (+0x1C)
            DWORD id = *(DWORD *)(itemPtr + 0x18);
            DWORD count = *(DWORD *)(itemPtr + 0x1C);

            items[validCount].SlotIndex = i;
            items[validCount].ItemPtr = itemPtr;
            items[validCount].ItemID = id;
            items[validCount].ItemCount = count;
            validCount++;
        }
    }

    if (validCount == 0)
        return;

    // 2. 排序
    qsort(items, validCount, sizeof(ShopSortNode), CompareShopItems);

    // 3. 写回商店内存
    // 先清空
    for (int i = 0; i < 50; i++)
    {
        pSlotArray[i] = 0;
    }
    // 填入排序后的指针
    for (int i = 0; i < validCount; i++)
    {
        pSlotArray[i] = items[i].ItemPtr;
    }
}

// ---------------------------------------------------------
// 内存补丁逻辑：修改回复药为可堆叠
// ---------------------------------------------------------
void PatchItemStackability()
{
    DWORD count = *(DWORD *)g_Addr_TableCount;
    DWORD tableBase = *(DWORD *)g_Addr_TablePtr;
    if (count == 0 || tableBase == 0)
        return;

    for (DWORD i = 0; i < count; i++)
    {
        DWORD entryAddress = tableBase + (i * 16);
        DWORD itemDataPtr = *(DWORD *)(entryAddress + 8);
        if (itemDataPtr == 0 || IsBadReadPtr((void *)itemDataPtr, 0x20))
            continue;

        DWORD itemID = *(DWORD *)(itemDataPtr + 4);
        if (itemID >= 4 && itemID <= 15)
        {
            int *pCanStack = (int *)(itemDataPtr + 0x18);
            *pCanStack = 1;
        }
    }
}

// ---------------------------------------------------------
// Template Load Hook Trampoline
// ---------------------------------------------------------
__declspec(naked) void TemplateLoad_Trampoline()
{
    __asm {
        add esp, 0x2824
        pushad
        call PatchItemStackability
        popad
        ret 0x10
    }
}

// ---------------------------------------------------------
// Shop Quantity Calculation
// ---------------------------------------------------------
int CalculateShopQuantity(int itemType)
{
    if (itemType == 10 || (itemType >= 20 && itemType <= 29))
    {
        return (rand() % 9) + 1;
    }
    return 1;
}

// ---------------------------------------------------------
// Shop Hook 1 & 2 Trampolines (Quantity)
// ---------------------------------------------------------
__declspec(naked) void ShopQtyHook1_Trampoline()
{
    __asm {
        push eax
        push ecx
        push edx
        push eax 
        call CalculateShopQuantity
        add esp, 4
        mov edi, eax 
        pop edx
        pop ecx
        pop eax
        jmp [g_Addr_ShopHook1_Ret]
    }
}

__declspec(naked) void ShopQtyHook2_Trampoline()
{
    __asm {
        push eax
        push ecx
        push edx
        push eax 
        call CalculateShopQuantity
        add esp, 4
        mov edi, eax 
        pop edx
        pop ecx
        pop eax
        jmp [g_Addr_ShopHook2_Ret]
    }
}

// ---------------------------------------------------------
// [新增] Shop Sort Hook Trampoline
// ---------------------------------------------------------
__declspec(naked) void ShopSortHook_Trampoline()
{
    __asm {
        // 保存所有寄存器状态
        pushad

            // 获取商店地址
            // 根据您的调研：在执行 00453A4A 时，[esp+4] 保存了商店地址
            // 但是我们刚才执行了 pushad (8个寄存器 * 4 = 32字节 = 0x20)
            // 所以原本的 esp 现在变成了 esp + 0x20
            // 原本的 [esp+4] 现在就是 [esp + 0x20 + 0x04] = [esp + 0x24]
        
        mov eax, [esp + 0x24]

        // 调用排序函数
        push eax // 参数 shopAddress
        call PerformShopSort
        add esp, 4

        // 恢复寄存器
        popad

            // [执行被覆盖的指令 & 还原函数返回逻辑]
            // 原指令: 00453A4A: mov large fs:0, ecx (7 bytes)
            // 后续指令: 00453A51: add esp, 60h
            // 后续指令: 00453A54: retn 4

            // 1. 执行被覆盖的 FS 操作
        mov dword ptr fs:[0x0], ecx

                                    // 2. 执行原函数的栈清理
        add esp, 0x60

                          // 3. 执行原函数的返回
        ret 4
    }
}

// ---------------------------------------------------------
// 工具: 安装 JMP Hook
// ---------------------------------------------------------
void InstallShopItemJmpHook(DWORD hookAddress, DWORD targetFunction, int len)
{
    DWORD oldProtect;
    VirtualProtect((LPVOID)hookAddress, len, PAGE_EXECUTE_READWRITE, &oldProtect);
    *(BYTE *)hookAddress = 0xE9;
    *(DWORD *)(hookAddress + 1) = targetFunction - hookAddress - 5;
    for (int i = 5; i < len; i++)
    {
        *(BYTE *)(hookAddress + i) = 0x90;
    }
    VirtualProtect((LPVOID)hookAddress, len, oldProtect, &oldProtect);
}

// ---------------------------------------------------------
// Mod 初始化
// ---------------------------------------------------------
void Mod_shop_opt_init(int game_version)
{
    if (game_version != 105)
        return;
    if (!g_pk_config.optimize_shop)
        return;

    // 1. 模板堆叠补丁
    InstallShopItemJmpHook(g_Addr_TemplateHook, (DWORD)TemplateLoad_Trampoline, 6);

    // 2. 商店数量随机化
    InstallShopItemJmpHook(g_Addr_ShopHook1, (DWORD)ShopQtyHook1_Trampoline, 5);
    InstallShopItemJmpHook(g_Addr_ShopHook2, (DWORD)ShopQtyHook2_Trampoline, 5);

    // 3. [新增] 商店排序
    // Hook 点: 00453A4A (mov large fs:0, ecx) -> 长度 7 字节
    InstallShopItemJmpHook(g_Addr_ShopSortHook, (DWORD)ShopSortHook_Trampoline, 7);
}