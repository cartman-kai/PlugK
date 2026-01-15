#include "pch.h"
#include "gem_stack.h"
#include "config.h"
#include <MinHook.h>
#include <stdio.h>

// 全局变量，用于存储 Hook 后的跳转目标（虽然这里我们用不到 Trampoline，但 MinHook 需要）
void *g_pOriginalDropCall = NULL;

// -------------------------------------------------------------------------
// 辅助函数：内存读写与游戏函数调用
// -------------------------------------------------------------------------

/**
 * 获取物品信息内存块指针
 * 修正依据：
 * 1. 0x578E74 是结构体基址，Table指针位于 +4 偏移处。
 * 2. 索引表本身是结构体数组，步长为 16 (4*4字节)，物品指针在 +8 偏移处。
 */
void *GetItemDataPtr(int itemID)
{
    // 1. 获取全局对象地址 (0x00578E74)
    DWORD globalObjAddr = DROP_ADDR_ITEM_TABLE_201;

    // 2. 获取索引表(Index Table)的指针
    // 指针位于全局对象的 +4 偏移处 (对应内存 A4 BA 6E 02)
    // 此时 tableBase 应该等于 0x026EBAA4
    DWORD tableBase = *(DWORD *)(globalObjAddr + 4);

    if (tableBase == 0)
        return NULL;

    // 3. 计算特定 ItemID 的 Entry 地址
    // 调研文档显示："每一行是 4 个 4 字节的数据" -> 步长为 16 字节
    // Row = TableBase + (ID * 16)
    DWORD entryAddr = tableBase + (itemID * 16);

    // 4. 获取具体物品数据的指针
    // 调研文档显示："第三个 4 字节...是一个指向物品信息的指针" -> 偏移为 8
    DWORD *pItemDataPtr = (DWORD *)(entryAddr + 8);

    return (void *)*pItemDataPtr;
}

/**
 * 切换物品的可叠加状态
 * pItemData: 物品信息指针
 * enable: 1 开启叠加, 0 关闭叠加
 * 返回: 修改前的原始值
 */
int SetItemStackable(void *pItemData, int enable)
{
    if (!pItemData)
        return 0;

    // 偏移 0x18 (24) 处是 Stackable 属性 (根据之前的分析: 第7个DWORD, 6*4=24)
    DWORD *pStackable = (DWORD *)((char *)pItemData + 24);
    int original = *pStackable;

    // 修改内存需要修改保护属性，防止崩溃
    DWORD oldProtect;
    VirtualProtect(pStackable, sizeof(DWORD), PAGE_EXECUTE_READWRITE, &oldProtect);

    *pStackable = enable;

    VirtualProtect(pStackable, sizeof(DWORD), oldProtect, &oldProtect);

    return original;
}

/**
 * 包装调用游戏原函数 B: 创建掉落物
 * 这是一个 __thiscall 调用，需要用汇编处理 ECX
 */
void *CallGame_CreateItem(void *pThis, int itemID, int arg2, int arg3, int arg4, int count)
{
    void *result = NULL;
    DWORD funcAddr = DROP_ADDR_FUNC_CREATE_201;

    __asm {
        push count // Arg5
        push arg4 // Arg4
        push arg3 // Arg3
        push arg2 // Arg2
        push itemID // Arg1
        mov ecx, pThis // this 指针
        call funcAddr
        mov result, eax
    }
    return result;
}

/**
 * 包装调用游戏原函数 C: 添加属性
 * 这是一个 __thiscall 调用
 */
void CallGame_AddProp(void *pNewItem, void *pPropData, int arg2, int arg3)
{
    if (!pNewItem || !pPropData)
        return;

    DWORD funcAddr = DROP_ADDR_FUN_ADDPROP_201;

    __asm {
        push arg3
        push arg2
        push pPropData
        mov ecx, pNewItem
        call funcAddr
    }
}

// -------------------------------------------------------------------------
// 核心逻辑处理函数 (C 语言层)
// -------------------------------------------------------------------------

/**
 * 处理宝石的 N-1 次循环掉落
 * 返回值: 1 表示执行了循环逻辑，0 表示未执行
 */
int ProcessGemPreDrop(void *pGlobalMgr, void *pContextESI, int itemID, int arg2, int arg3, int arg4, int totalCount)
{
    // 1. 基本检查
    if (totalCount <= 1)
        return 0;

    // 2. 检查物品类型是否为宝石
    void *pItemData = GetItemDataPtr(itemID);
    if (!pItemData)
        return 0;

    // 偏移 0x08 (8) 是 Type 字段
    int itemType = *(int *)((char *)pItemData + 8);

    // 假设宝石类型是 30 或 35 (根据之前的调研)
    if (itemType != ITEM_TYPE_GEM_A && itemType != ITEM_TYPE_GEM_B)
    {
        return 0;
    }

    // 3. 准备上下文数据 (从 ESI 获取)
    // 根据 sub_48E480:
    // ESI+0x24 (36) -> 属性数据指针 (v7)
    // ESI+0x2C (44) -> AddProp Arg2 (v5[11])
    // ESI+0x30 (48) -> AddProp Arg3 (v5[12])
    DWORD *pESI = (DWORD *)pContextESI;
    void *pPropData = (void *)pESI[9]; // 9 * 4 = 36 = 0x24
    int propArg2 = pESI[11];           // 11 * 4 = 44 = 0x2C
    int propArg3 = pESI[12];           // 12 * 4 = 48 = 0x30

    // 4. 临时修改 Stackable = 0
    SetItemStackable(pItemData, 0);

    // 5. 循环执行 (Total - 1) 次
    // 为什么减1？因为最后一个留给游戏原来的逻辑执行
    int loopCount = totalCount - 1;
    for (int i = 0; i < loopCount; i++)
    {
        // 调用创建函数，强制数量为 1
        void *pNewItem = CallGame_CreateItem(pGlobalMgr, itemID, arg2, arg3, arg4, 1);

        // 如果创建成功，且有属性数据，则添加属性
        // 判断 *((_DWORD *)v7 - 2) 是否存在
        if (pNewItem && pPropData)
        {
            // 检查属性标志位 (pPropData - 8 字节)
            if (*((DWORD *)pPropData - 2) != 0)
            {
                CallGame_AddProp(pNewItem, pPropData, propArg2, propArg3);
            }
        }
    }

    // 6. 恢复 Stackable = 1 (必须恢复，否则后续无法叠加)
    SetItemStackable(pItemData, 1);

    return 1; // 告诉汇编层，我们处理过了
}

// -------------------------------------------------------------------------
// Naked Hook (汇编层)
// -------------------------------------------------------------------------

void __declspec(naked) Hooked_Drop_CallSite()
{
    __asm {
        // 保存环境
        pushad
        pushfd

                // ---------------------------------------------------------
                // 参数获取 & 逻辑判断
                // ---------------------------------------------------------
                // 获取 Count (Arg5) -> [ESP + 0x34]
        mov eax, [esp + 0x34]

        // 优化：先判断数量，如果 <= 1 直接跳过复杂逻辑，提升性能
        cmp eax, 1
        jle _Label_Normal_Execution

            // 准备调用参数 (ProcessGemPreDrop)
        push eax // totalCount
        
        mov eax, [esp + 0x34 + 4] // Arg4 (注意每次 push esp 都会变，这里为了简便，建议用 ebp 寻址，或者如下计算)
        // 此时栈顶多了1个push(totalCount)，所以偏移+4。
        // 原ESP偏移是 0x34。
        // 为了避免烧脑的 ESP 计算，我们重新利用 pushad 的特性：
        // [esp + 4(刚push的count) + 36(pushad/fd) + 16(arg5偏移)] = 错的，太乱了。

        // --- 修正参数传递逻辑 ---
        // 为了安全，我们还是老老实实读原始栈位置。
        // 此时 ESP 已经 push 了 totalCount (4字节)
        // 原始 Base = ESP + 4
        // Count = [Base + 0x34]

        // 重新来，为了代码整洁，直接从 pushad 保存的堆栈里取值最稳。
        // 也可以直接用 ebp 建立栈帧，但 naked 函数里不建议乱动 ebp。

        // 简单粗暴法：利用 EAX 暂存
        // 此时栈顶是 totalCount。
        // 我们需要 Arg4 (0), Arg3 (Y), Arg2 (X), Arg1 (ID)

        // Arg4 (0) 位于 [ESP + 4 + 0x24 + 0x0C] = [ESP + 0x34]
        mov eax, [esp + 0x34]
        push eax

            // Arg3 (Y) 位于 [ESP + 8 + 0x24 + 0x08] = [ESP + 0x34] ...
            // 发现规律了吗？因为我们每 Push 一次，ESP-4，目标地址相对 ESP 就 +4。
            // 所以偏移量恒定是 0x34。
        mov eax, [esp + 0x34]
        push eax // Push Arg3

        mov eax, [esp + 0x34]
        push eax // Push Arg2

        mov eax, [esp + 0x34]
        push eax // Push ID

            // ESI, ECX
            // ESI 在 pushad 里的位置。
            // [ESP + 16(上面4个push) + 4(totalCount) + 4(pushfd) + 4(EDI) + 4(ESI)]
            // 太麻烦。直接利用当前寄存器值，因为我们还没破坏 ESI/ECX
        push esi 
        push ecx 

        call ProcessGemPreDrop
        add esp, 28 // 平衡 ProcessGemPreDrop 的参数

        // 检查返回值
        test eax, eax
        jz _Label_Normal_Execution

                // 如果处理了循环，修改栈上的 Count 为 1
                // 此时 ESP 恢复到了 pushad+pushfd 后的状态
                // Count 在 [esp + 0x24 + 0x10] = [esp + 0x34]
        mov dword ptr [esp + 0x34], 1 

    _Label_Normal_Execution:
        // 恢复环境
        popfd
        popad

                // -------------------------------------------------------
                // 关键修正：模拟 CALL 并跳转回原流程
                // -------------------------------------------------------

                // 1. 准备返回地址 (让 49AB70 执行完 ret 时跳回 0048E506)
        push DROP_ADDR_RET_201

                    // 2. 准备目标函数地址
        mov eax, DROP_ADDR_FUNC_CREATE_201

            // 3. 跳过去 (去而不返，由 49AB70 的 ret 负责跳回 0048E506)
        jmp eax
    }
}

// ---------------------------------------------------------
// 安装 Hook
// ---------------------------------------------------------
void Mod_Gem_SafeDrop_Init(int ver)
{

    if (!g_pk_config.gem_safe_drop)
        return;

    if (ver == 105)
    {
        return; // 1.05 版本不支持该功能
    }
    else if (ver == 201)
    {
        // Hook 地址
        void *pTarget = (void *)DROP_ADDR_HOOK_POINT_201;

        char msg[256];

        // 1. 初始化 MinHook
        MH_STATUS status = MH_Initialize();
        if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED)
        {
            sprintf_s(msg, "MinHook 始化失败! 代码: %d", (int)status);
            MessageBoxA(NULL, msg, "MOD 调试", MB_OK | MB_ICONERROR);
            return;
        }

        // 创建 Hook
        // 注意：这里我们 Hook 的是一个 CALL 指令的位置。
        // MinHook 会在 0x48E501 写入一个 JMP。
        // 原本指令是 E8 xx xx xx xx (5字节)，刚好够放 JMP。
        if (MH_CreateHook(pTarget, &Hooked_Drop_CallSite, &g_pOriginalDropCall) == MH_OK)
        {
            MH_EnableHook(pTarget);
        }
        else
        {
            MessageBoxA(NULL, "HOOK 失败", "MOD 调试", MB_OK | MB_ICONERROR);
        }
    }
    else
    {
        return; // 不支持的版本
    }
}