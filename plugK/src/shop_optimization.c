#include "pch.h"
#include "shop_optimization.h"
#include "config.h"
#include "show_tips.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h> // for rand(), qsort()
#include <MinHook.h>

// ---------------------------------------------------------
// 全局地址 (v1.05)
// ---------------------------------------------------------
// 1. 商店数量随机化 Hook
static DWORD g_Addr_ShopHook1 = 0x004536BF;
static DWORD g_Addr_ShopHook1_Ret = 0x004536D9;
static DWORD g_Addr_ShopHook2 = 0x00453AE2;
static DWORD g_Addr_ShopHook2_Ret = 0x00453AFC;

static DWORD g_Addr_TableCount_02 = 0x00548340;
static DWORD g_Addr_TablePtr_02 = 0x00548344;

static DWORD g_Addr_PropMdl_TableCnt_105_01 = 0x005488BC;
static DWORD g_Addr_PropMdl_TablePtr_105_01 = 0x005488C0;
static DWORD g_Addr_PropMdl_TableCnt_201_01 = 0x00578E74;
static DWORD g_Addr_PropMdl_TablePtr_201_01 = 0x00578E78;

static DWORD g_Addr_PropMdl_TableCnt_01 = 0x0;
static DWORD g_Addr_PropMdl_TablePtr_01 = 0x0;

// 3. [新增] 商店排序 Hook
// 00453A4A: mov large fs:0, ecx (7 bytes)
static DWORD g_Addr_ShopSortHook = 0x00453A4A;
// Hook 覆盖了 7 字节，后续指令是 add esp, 60h; retn 4
// 我们将在 Trampoline 中手动补全这些逻辑

static DWORD g_Flag_ItemSetStack_02 = 0;
static DWORD g_Flag_ItemSetStack_01 = 0;

static DWORD g_Flag_GemStackStatus = 0;

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

// 设置物品信息表中可叠加状态
void SetItemStackProp(DWORD count, DWORD tableBaseAddress)
{
    // 基础校验
    if (count == 0 || tableBaseAddress == 0)
        return;

    for (DWORD i = 0; i < count; i++)
    {
        // 关键点：tableBaseAddress 是 DWORD，这里的 + (i * 16) 是按字节增加
        DWORD entryAddress = tableBaseAddress + (i * 16);

        // 安全读取物品数据指针
        DWORD itemDataPtr = *(DWORD *)(entryAddress + 8);
        if (itemDataPtr == 0 || IsBadReadPtr((void *)itemDataPtr, 0x20))
            continue;

        // 读取物品 ID (+0x04) 和 类型 (+0x08)
        DWORD itemID = *(DWORD *)(itemDataPtr + 4);
        DWORD itemType = *(DWORD *)(itemDataPtr + 8);
        int *pCanStack = (int *)(itemDataPtr + 0x18);

        // 逻辑 A: 消耗品/药水堆叠 (ID 4-15)
        if (itemID >= 4 && itemID <= 15 && g_pk_config.shop_item_count)
        {
            *pCanStack = 1;
        }
        // 逻辑 B: 宝石堆叠 (Type 30-35)
        else if (itemType >= 30 && itemType <= 35 && g_pk_config.enable_gem_stack)
        {
            *pCanStack = 1;
            g_Flag_GemStackStatus = 1;
        }
    }
}

void SetItemTableGemStack(DWORD count, DWORD tableBaseAddress, DWORD enableStack)
{
    // 基础校验
    if (count == 0 || tableBaseAddress == 0)
        return;

    for (DWORD i = 0; i < count; i++)
    {
        // 关键点：tableBaseAddress 是 DWORD，这里的 + (i * 16) 是按字节增加
        DWORD entryAddress = tableBaseAddress + (i * 16);

        // 安全读取物品数据指针
        DWORD itemDataPtr = *(DWORD *)(entryAddress + 8);
        if (itemDataPtr == 0 || IsBadReadPtr((void *)itemDataPtr, 0x20))
            continue;

        // 读取物品 ID (+0x04) 和 类型 (+0x08)
        DWORD itemID = *(DWORD *)(itemDataPtr + 4);
        DWORD itemType = *(DWORD *)(itemDataPtr + 8);
        int *pCanStack = (int *)(itemDataPtr + 0x18);

        // 逻辑 B: 宝石堆叠 (Type 30-35)
        if (itemType >= 30 && itemType <= 35 && g_pk_config.enable_gem_stack)
        {
            *pCanStack = enableStack;
        }
    }
}

void SetGemStackProp(DWORD enableStack)
{

    DWORD count_02 = *(DWORD *)g_Addr_TableCount_02;
    DWORD tableBase_02 = *(DWORD *)g_Addr_TablePtr_02;
    if (count_02 == 0 || tableBase_02 == 0)
        return;

    SetItemTableGemStack(count_02, tableBase_02, enableStack);

    DWORD count_01 = *(DWORD *)g_Addr_PropMdl_TableCnt_01;
    DWORD tableBase_01 = *(DWORD *)g_Addr_PropMdl_TablePtr_01;
    if (count_01 == 0 || tableBase_01 == 0)
        return;

    SetItemTableGemStack(count_01, tableBase_01, enableStack);

    g_Flag_GemStackStatus = enableStack;
}

void ToggleChangeGemStackProp()
{
    if (!g_pk_config.enable_gem_stack)
    {
        ShowGameLog("功能未开启");
        return;
    }

    if (g_Flag_GemStackStatus)
    {
        SetGemStackProp(0);
        ShowGameLog("关闭宝石叠加");
        return;
    }

    SetGemStackProp(1);
    ShowGameLog("开启宝石叠加");
}

// ---------------------------------------------------------
// 内存补丁逻辑：修改两个物品信息表中为可堆叠
// ---------------------------------------------------------
void PatchItemStackProp()
{
    DWORD count_02 = *(DWORD *)g_Addr_TableCount_02;
    DWORD tableBase_02 = *(DWORD *)g_Addr_TablePtr_02;
    if (count_02 == 0 || tableBase_02 == 0)
        return;

    SetItemStackProp(count_02, tableBase_02);
    g_Flag_ItemSetStack_02 = 1;

    DWORD count_01 = *(DWORD *)g_Addr_PropMdl_TableCnt_01;
    DWORD tableBase_01 = *(DWORD *)g_Addr_PropMdl_TablePtr_01;
    if (count_01 == 0 || tableBase_01 == 0)
        return;

    SetItemStackProp(count_01, tableBase_01);
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

typedef int(__fastcall *tLoadAllData)(void *pThis, void *_edx);
tLoadAllData fpLoadAllData = NULL;

// 我们的拦截函数
int __fastcall Detour_LoadAllData(void *pThis, void *_edx)
{

    // 1. 执行原逻辑
    int result = fpLoadAllData(pThis, _edx);
    PatchItemStackProp();

    return result;
}

// ---------------------------------------------------------
// Mod 初始化
// ---------------------------------------------------------
void Mod_shop_opt_init(int game_version)
{
    // 默认置空
    g_Addr_TableCount_02 = 0;
    g_Addr_TablePtr_02 = 0;
    g_Addr_ShopHook1 = 0;
    g_Addr_ShopHook2 = 0;
    g_Addr_ShopSortHook = 0;

    void *targetAddr_LoadData = NULL;

    int hook1_len = 0;
    int hook2_len = 0;

    int sort_len = 0;

    if (game_version == 105)
    {
        // --- v1.05 地址 ---

        // 加载函数
        targetAddr_LoadData = (void *)0x00405300;

        g_Addr_TableCount_02 = 0x00548340;
        g_Addr_TablePtr_02 = 0x00548344;

        g_Addr_PropMdl_TableCnt_01 = g_Addr_PropMdl_TableCnt_105_01;
        g_Addr_PropMdl_TablePtr_01 = g_Addr_PropMdl_TablePtr_105_01;

        g_Addr_ShopHook1 = 0x004536BF;
        g_Addr_ShopHook1_Ret = 0x004536D9;
        hook1_len = 5;

        g_Addr_ShopHook2 = 0x00453AE2;
        g_Addr_ShopHook2_Ret = 0x00453AFC;
        hook2_len = 5;

        g_Addr_ShopSortHook = 0x00453A4A;
        sort_len = 7;
    }

    else if (game_version == 201)
    {
        // --- v2.01 地址 (新增) ---

        targetAddr_LoadData = (void *)0x0040C290;

        // 1. 物品表信息
        g_Addr_TableCount_02 = 0x005788D0;
        g_Addr_TablePtr_02 = 0x005788D4;

        g_Addr_PropMdl_TableCnt_01 = g_Addr_PropMdl_TableCnt_201_01;
        g_Addr_PropMdl_TablePtr_01 = g_Addr_PropMdl_TablePtr_201_01;

        // 3. 商店数量随机化 Hook 1
        // 0045F7EF: cmp eax, 14h (3 bytes)
        // 0045F7F2: jl short loc_45F809 (2 bytes) -> Total 5 bytes
        // Return to 0045F809 (push edi)
        g_Addr_ShopHook1 = 0x0045F7EF;
        g_Addr_ShopHook1_Ret = 0x0045F809;
        hook1_len = 5;

        // 4. 商店数量随机化 Hook 2
        g_Addr_ShopHook2 = 0x0045FC12;
        g_Addr_ShopHook2_Ret = 0x0045FC2C;
        hook2_len = 5;

        // 5. 商店排序 Hook
        g_Addr_ShopSortHook = 0x0045FB7A;
        sort_len = 7;
    }
    else
    {
        return; // 不支持的版本
    }

    // 1. 堆叠补丁 (v1.05 & v2.01)
    // 执行 Hook
    if (targetAddr_LoadData != NULL)
    {
        // 1. 检查 Hook 创建结果
        MH_STATUS statusCreate = MH_CreateHook(targetAddr_LoadData, &Detour_LoadAllData, (LPVOID *)&fpLoadAllData);
        if (statusCreate != MH_OK)
        {
            char err[256];
            sprintf_s(err, sizeof(err), "PlugK: Hook Create Failed! Error Code: %d", statusCreate);
            MessageBoxA(NULL, err, "Hook Error", MB_ICONERROR);
            return;
        }

        // 2. 检查 Hook 启用结果
        MH_STATUS statusEnable = MH_EnableHook(targetAddr_LoadData);
        if (statusEnable != MH_OK)
        {
            MessageBoxA(NULL, "PlugK: Hook Enable Failed!", "Hook Error", MB_ICONERROR);
        }
    }

    // 2. 商店数量 Hook 1
    if (g_Addr_ShopHook1 != 0 && g_pk_config.shop_item_count)
    {
        InstallShopItemJmpHook(g_Addr_ShopHook1, (DWORD)ShopQtyHook1_Trampoline, hook1_len);
    }

    // 3. 商店数量 Hook 2
    if (g_Addr_ShopHook2 != 0 && g_pk_config.shop_item_count)
    {
        InstallShopItemJmpHook(g_Addr_ShopHook2, (DWORD)ShopQtyHook2_Trampoline, hook2_len);
    }

    // 4. 商店排序 Hook
    if (g_Addr_ShopSortHook != 0 && g_pk_config.shop_sort)
    {
        InstallShopItemJmpHook(g_Addr_ShopSortHook, (DWORD)ShopSortHook_Trampoline, sort_len);
    }
}