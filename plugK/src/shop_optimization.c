#include "pch.h"
#include "shop_optimization.h"
#include "config.h"
#include <windows.h>
#include <stdlib.h> // for rand()

// ---------------------------------------------------------
// 全局地址 (v1.05)
// ---------------------------------------------------------
static DWORD g_Addr_ShopHook1 = 0x004536BF;     // 第一次设置数量逻辑
static DWORD g_Addr_ShopHook1_Ret = 0x004536D9; // 跳回处

static DWORD g_Addr_ShopHook2 = 0x00453AE2;     // 第二次设置数量逻辑
static DWORD g_Addr_ShopHook2_Ret = 0x00453AFC; // 跳回处

static DWORD g_Addr_TemplateHook = 0x004D07FE; // 物品模板加载结束处 (add esp, 2824h)
static DWORD g_Addr_TemplateRet = 0x004D0804;  // retn 10h

// 内存数据表地址
static DWORD g_Addr_TableCount = 0x00548340; // 物品总数指针
static DWORD g_Addr_TablePtr = 0x00548344;   // 物品索引表指针

// ---------------------------------------------------------
// 内存补丁逻辑：修改回复药为可堆叠
// ---------------------------------------------------------
void PatchItemStackability()
{
    // 1. 获取物品表信息
    DWORD count = *(DWORD *)g_Addr_TableCount;
    DWORD tableBase = *(DWORD *)g_Addr_TablePtr;

    if (count == 0 || tableBase == 0)
        return;

    // 2. 遍历所有物品模板
    // 索引表结构: 每个条目 16 字节 (4个 DWORD)
    // +0: ?
    // +4: ?
    // +8: ItemDataPtr (指向实际数据)
    // +12: ?

    for (DWORD i = 0; i < count; i++)
    {
        DWORD entryAddress = tableBase + (i * 16);

        // 读取 ItemDataPtr
        DWORD itemDataPtr = *(DWORD *)(entryAddress + 8);
        if (itemDataPtr == 0 || IsBadReadPtr((void *)itemDataPtr, 0x20))
            continue;

        // 读取 ItemID (偏移 +4)
        DWORD itemID = *(DWORD *)(itemDataPtr + 4);

        // 3. 判断是否为回复类药品 (ID 4 - 15)
        if (itemID >= 4 && itemID <= 15)
        {
            // 修改 CanStack (偏移 +0x18) 为 1
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
        // [执行被覆盖的指令]
        // 004D07FE: add esp, 2824h
        add esp, 0x2824

        // [插入逻辑]
        // 保存寄存器 (虽然 PatchItemStackability 主要是C代码，编译器会处理，但 naked 函数还是小心为妙)
        pushad
        call PatchItemStackability
        popad

                    // [执行返回]
                    // 原指令是 retn 10h，我们不能直接 jmp 回去，因为 retn 会直接结束函数
                    // 这里直接模拟 retn 10h
        ret 0x10
    }
}

// ---------------------------------------------------------
// Shop Quantity Hook Logic (通用逻辑)
// 输入: EAX (Item Type)
// 输出: EDI (Calculated Quantity)
// ---------------------------------------------------------
int CalculateShopQuantity(int itemType)
{
    // 逻辑：
    // Type 10 (回复药) 或 Type 20-29 (暗器) -> 随机 1-9
    if (itemType == 10 || (itemType >= 20 && itemType <= 29))
    {
        // rand() 返回 0..RAND_MAX
        return (rand() % 9) + 1;
    }
    return 1;
}

// ---------------------------------------------------------
// Shop Hook 1 Trampoline
// ---------------------------------------------------------
__declspec(naked) void ShopQtyHook1_Trampoline()
{
    __asm {
        // 此时 EAX = Item Type

        // 保存上下文 (CalculateShopQuantity 会修改寄存器)
        push eax
        push ecx
        push edx
                    // EDI 是我们要修改的目标，不需要保存值，但需要作为结果被修改

                    // 调用 C 函数计算数量
        push eax // 参数 itemType
        call CalculateShopQuantity
        add esp, 4 // 平栈

        // EAX 现在是返回值 (Quantity)
        mov edi, eax // 将结果存入 EDI

            // 恢复上下文
        pop edx
        pop ecx
        pop eax

                        // 跳过原程序的判断逻辑，直接去应用数量的地方
        jmp [g_Addr_ShopHook1_Ret]
    }
}

// ---------------------------------------------------------
// Shop Hook 2 Trampoline
// ---------------------------------------------------------
__declspec(naked) void ShopQtyHook2_Trampoline()
{
    __asm {
        // 逻辑同上
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
// Mod 商店物品优化
// ---------------------------------------------------------
void Mod_shop_opt_init(int game_version)
{
    if (game_version != 105)
        return;
    if (!g_pk_config.optimize_shop)
        return;

    // 1. Hook 物品模板加载 (使其可堆叠)
    // Hook 点: 004D07FE (add esp, 2824h) -> 长度 6 字节
    InstallShopItemJmpHook(g_Addr_TemplateHook, (DWORD)TemplateLoad_Trampoline, 6);

    // 2. Hook 商店数量逻辑 1
    // Hook 点: 004536BF (cmp eax, 14h ...) -> 长度 5 字节
    InstallShopItemJmpHook(g_Addr_ShopHook1, (DWORD)ShopQtyHook1_Trampoline, 5);

    // 3. Hook 商店数量逻辑 2
    // Hook 点: 00453AE2 (cmp eax, 14h ...) -> 长度 5 字节
    InstallShopItemJmpHook(g_Addr_ShopHook2, (DWORD)ShopQtyHook2_Trampoline, 5);
}