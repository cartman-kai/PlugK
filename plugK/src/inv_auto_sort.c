#include "pch.h"
#include "inv_auto_sort.h"
#include "config.h"
#include <stdio.h>

// ---------------------------------------------------------
// 外部变量引用 (来自 stash_ext.c)
// ---------------------------------------------------------
extern int g_StashPageB[50];
extern int g_InvPageB[50];

// ---------------------------------------------------------
// 全局地址变量 (在 init 中根据版本动态赋值)
// ---------------------------------------------------------
static DWORD g_Addr_GetCharInfo = 0;
static DWORD g_Addr_GlobalVar = 0;
static DWORD g_Addr_DeleteItem = 0;

// ---------------------------------------------------------
// 偏移量定义
// ---------------------------------------------------------
static DWORD g_Offset_PoolPtr = 0xA0; // 物品池指针数组
static DWORD g_Offset_DelSlot = 0x88; // 删除用的临时槽位
static DWORD g_Offset_DelMgr = 0x68;  // 删除用的管理器对象

// [容器偏移]
static DWORD g_Offset_InventoryArr = 0xA4;  // 玩家背包格子数组 (50 int32)
static DWORD g_Offset_QuickSlotArr = 0x184; // [新增] 道具快捷栏数组 (6 int32)
static DWORD g_Offset_StashArr = 0x1FC;     // 储藏箱格子数组 (50 int32)

// 全局线程控制标记
volatile BOOL g_bMonitorThreadRunning = TRUE;

// ---------------------------------------------------------
// 辅助结构与函数
// ---------------------------------------------------------

typedef struct
{
    int Index;       // 物品池中的索引
    ItemObject *Ptr; // 物品对象指针
    DWORD ID;
    DWORD Count;
    DWORD Level;
    DWORD Price;
} SortItemNode;

// 获取角色基址 (通用版)
DWORD GetCharacterBase()
{
    if (g_Addr_GetCharInfo == 0)
        return 0;

    DWORD func = g_Addr_GetCharInfo;
    DWORD param = g_Addr_GlobalVar;
    DWORD result = 0;

    __asm {
        push param
        call func
        add esp, 4 
        mov result, eax
    }
    return result;
}

// 安全删除物品 (通用版)
void SafeDeleteItem(DWORD charBase, ItemObject *itemPtr)
{
    if (charBase == 0 || itemPtr == NULL || g_Addr_DeleteItem == 0)
        return;

    DWORD func_delete = g_Addr_DeleteItem;
    DWORD target_slot = charBase + g_Offset_DelSlot;
    DWORD this_param = charBase + g_Offset_DelMgr;

    __try
    {
        *(DWORD *)target_slot = (DWORD)itemPtr;
        __asm {
            mov ecx, this_param
            call func_delete
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        OutputDebugStringA("PlugK: Exception in SafeDeleteItem");
    }
}

// 检查物品是否可堆叠
BOOL IsStackable(ItemObject *pItem)
{
    if (pItem == NULL)
        return FALSE;

    DWORD id = pItem->ItemID;
    BOOL isIdInScope = FALSE;

    // 1. 初步检查：ID 是否在允许堆叠的逻辑范围内
    if (id >= 4 && id <= 15)
        isIdInScope = TRUE;
    else if (id >= 22 && id <= 23)
        isIdInScope = TRUE;
    else if (id >= 60 && id <= 77)
        isIdInScope = TRUE;

    if (!isIdInScope)
        return FALSE;

    // 2. 深度检查：读取内存模板数据
    __try
    {
        DWORD ptrTemplateRef = *(DWORD *)((DWORD)pItem + 0x14);
        if (ptrTemplateRef == 0 || IsBadReadPtr((void *)ptrTemplateRef, 16))
            return FALSE;

        DWORD ptrTemplateData = *(DWORD *)(ptrTemplateRef + 0x08);
        if (ptrTemplateData == 0 || IsBadReadPtr((void *)ptrTemplateData, 0x20))
            return FALSE;

        int canStackFlag = *(int *)(ptrTemplateData + 0x18);
        if (canStackFlag == 1)
            return TRUE;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return FALSE;
    }

    return FALSE;
}

int CompareItems(const void *a, const void *b)
{
    SortItemNode *itemA = (SortItemNode *)a;
    SortItemNode *itemB = (SortItemNode *)b;

    // 排序优先级: ID > 数量(降序) > 等级 > 价格
    if (itemA->ID != itemB->ID)
        return (int)itemA->ID - (int)itemB->ID;
    if (itemA->Count != itemB->Count)
        return (int)itemB->Count - (int)itemA->Count; // 数量多的排前面，利于合并
    if (itemA->Level != itemB->Level)
        return (int)itemA->Level - (int)itemB->Level;
    if (itemA->Price > itemB->Price)
        return 1;
    if (itemA->Price < itemB->Price)
        return -1;
    return 0;
}

// ---------------------------------------------------------
// 核心整理逻辑 (支持 A+B 面统一整理)
// ---------------------------------------------------------
// targetArrayOffset: 角色身上的 A 面偏移 (如 g_Offset_InventoryArr)
// hiddenPageArray:   对应的 B 面数组指针 (如 g_InvPageB)
void PerformUnifiedOrganize(DWORD targetArrayOffset, int *hiddenPageArray)
{
    DWORD charBase = GetCharacterBase();
    if (charBase == 0)
        return;

    // 获取 A 面指针 (游戏内存)
    int *gameSlotArray = (int *)(charBase + targetArrayOffset);

    // 获取物品池指针
    DWORD poolAddress = *(DWORD *)(charBase + g_Offset_PoolPtr);
    if (poolAddress == 0)
        return;
    DWORD *itemPoolPtr = (DWORD *)poolAddress;

    // 定义容量：A面 50 + B面 50 = 100
    SortItemNode items[100];
    int validCount = 0;

    // -------------------------------------------------
    // 1. 读取有效物品 (合并 A 面和 B 面)
    // -------------------------------------------------
    for (int i = 0; i < 100; i++)
    {
        int itemIndex = -1;

        if (i < 50)
        {
            // 前 50 个来自 A 面 (游戏内存)
            itemIndex = gameSlotArray[i];
        }
        else
        {
            // 后 50 个来自 B 面 (扩展数组)
            if (hiddenPageArray)
            {
                itemIndex = hiddenPageArray[i - 50];
            }
        }

        if (itemIndex != -1)
        {
            ItemObject *obj = (ItemObject *)itemPoolPtr[itemIndex];
            if (obj != NULL && !IsBadReadPtr(obj, sizeof(ItemObject)))
            {
                items[validCount].Index = itemIndex;
                items[validCount].Ptr = obj;
                items[validCount].ID = obj->ItemID;
                items[validCount].Count = obj->Count;
                items[validCount].Level = obj->Level;
                items[validCount].Price = obj->Price;
                validCount++;
            }
        }
    }

    if (validCount == 0)
        return;

    // -------------------------------------------------
    // 2. 预排序 (将相同 ID 的物品排在一起)
    // -------------------------------------------------
    qsort(items, validCount, sizeof(SortItemNode), CompareItems);

    // -------------------------------------------------
    // 3. 堆叠合并逻辑
    // -------------------------------------------------
    for (int i = 0; i < validCount; i++)
    {
        if (items[i].Count == 0)
            continue;
        if (!IsStackable(items[i].Ptr))
            continue;

        for (int j = i + 1; j < validCount; j++)
        {
            if (items[j].ID != items[i].ID)
                break; // ID 不同，无需继续，因为已经排序过
            if (items[j].Count == 0)
                continue;
            if (!IsStackable(items[j].Ptr))
                continue;

            // 计算空间：假设最大堆叠数为 9 (根据原代码逻辑)
            int space = 9 - (int)items[i].Count;
            if (space > 0)
            {
                int take = (items[j].Count >= (DWORD)space) ? space : items[j].Count;
                items[i].Count += take;
                items[j].Count -= take;

                // 更新内存中的数值
                items[i].Ptr->Count = items[i].Count;
                items[j].Ptr->Count = items[j].Count;

                if (items[i].Count == 9)
                    break; // 当前堆已满
            }
        }
    }

    // -------------------------------------------------
    // 4. 清理废弃(数量为0) & 最终排序
    // -------------------------------------------------
    SortItemNode finalItems[100];
    int finalCount = 0;

    for (int i = 0; i < validCount; i++)
    {
        if (items[i].Count > 0)
            finalItems[finalCount++] = items[i];
        else
            SafeDeleteItem(charBase, items[i].Ptr); // 删除合并后数量为0的废弃对象
    }

    // 再次排序，确保整理后的物品紧凑排列
    qsort(finalItems, finalCount, sizeof(SortItemNode), CompareItems);

    // -------------------------------------------------
    // 5. 写回容器 (分布到 A 面和 B 面)
    // -------------------------------------------------

    // 5.1 先清空两边
    for (int i = 0; i < 50; i++)
        gameSlotArray[i] = -1;
    if (hiddenPageArray)
    {
        for (int i = 0; i < 50; i++)
            hiddenPageArray[i] = -1;
    }

    // 5.2 填入数据
    for (int i = 0; i < finalCount; i++)
    {
        if (i < 50)
        {
            // 前 50 个填入 A 面
            gameSlotArray[i] = finalItems[i].Index;
        }
        else
        {
            // 溢出的填入 B 面
            if (hiddenPageArray)
            {
                hiddenPageArray[i - 50] = finalItems[i].Index;
            }
            else
            {
                // 如果没有 B 面但物品多余 50 (理论上不应发生，除非合并失败且原数据>50)，
                // 这里作为保险，只能放在最后
            }
        }
    }
}

// ---------------------------------------------------------
// [修改版] 清理快捷栏逻辑 (支持移入 B 面)
// ---------------------------------------------------------
// 返回值: TRUE 表示有物品移动，需要重新整理背包
BOOL CleanupQuickSlots()
{
    DWORD charBase = GetCharacterBase();
    if (charBase == 0)
        return FALSE;

    // 获取指针
    int *invArrayA = (int *)(charBase + g_Offset_InventoryArr);
    int *quickArray = (int *)(charBase + g_Offset_QuickSlotArr);

    DWORD poolAddress = *(DWORD *)(charBase + g_Offset_PoolPtr);
    if (poolAddress == 0)
        return FALSE;
    DWORD *itemPoolPtr = (DWORD *)poolAddress;

    BOOL hasMoved = FALSE;

    // 遍历 6 个快捷栏
    for (int q = 0; q < 6; q++)
    {
        int qIdx = quickArray[q];
        if (qIdx == -1)
            continue; // 空格子

        ItemObject *obj = (ItemObject *)itemPoolPtr[qIdx];
        if (obj == NULL || IsBadReadPtr(obj, sizeof(ItemObject)))
            continue;

        // 检查类型 (base + 0x28)
        // 保留正常道具类型 (假设 22,23,60-77 为快捷栏可用道具)
        if (obj->ItemID >= 22 && obj->ItemID <= 23)
            continue;
        if (obj->ItemID >= 60 && obj->ItemID <= 77)
            continue;

        // 需要移动。寻找空位：优先 A 面，其次 B 面
        int freeSlotA = -1;
        int freeSlotB = -1;

        // 1. 找 A 面空位
        for (int i = 0; i < 50; i++)
        {
            if (invArrayA[i] == -1)
            {
                freeSlotA = i;
                break;
            }
        }

        // 2. 如果 A 面没空位，找 B 面空位
        if (freeSlotA == -1)
        {
            for (int i = 0; i < 50; i++)
            {
                if (g_InvPageB[i] == -1)
                {
                    freeSlotB = i;
                    break;
                }
            }
        }

        // 3. 执行移动
        if (freeSlotA != -1)
        {
            invArrayA[freeSlotA] = qIdx;
            quickArray[q] = -1;
            hasMoved = TRUE;
        }
        else if (freeSlotB != -1)
        {
            g_InvPageB[freeSlotB] = qIdx;
            quickArray[q] = -1;
            hasMoved = TRUE;
        }
        else
        {
            // A B 两面都满了，无法移动，只能跳过
            break;
        }
    }

    return hasMoved;
}

// ---------------------------------------------------------
// 流程封装
// ---------------------------------------------------------

// 背包整理流程
void ExecuteInventorySortFlow()
{
    // 1. 先进行一次 A+B 统一整理
    // 这会将所有物品优先压缩到 A 面，溢出的去 B 面
    PerformUnifiedOrganize(g_Offset_InventoryArr, g_InvPageB);

    // 2. 检查快捷栏，把非道具物品移入空位 (优先 A，次选 B)
    if (CleanupQuickSlots())
    {
        // 3. 如果有物品从快捷栏移入，再次执行 A+B 统一整理
        PerformUnifiedOrganize(g_Offset_InventoryArr, g_InvPageB);
    }

    MessageBeep(MB_OK);
}

// 储物箱整理流程
void ExecuteStashSortFlow()
{
    // 直接进行 A+B 统一整理
    PerformUnifiedOrganize(g_Offset_StashArr, g_StashPageB);

    MessageBeep(MB_OK);
}

void Mod_inv_auto_sort_init(int game_version)
{
    // 1. 根据版本设置地址
    if (game_version == 105)
    {
        g_Addr_GetCharInfo = 0x004892D0;
        g_Addr_GlobalVar = 0x005585C0;
        g_Addr_DeleteItem = 0x004CF550;
    }
    else if (game_version == 201)
    {
        g_Addr_GetCharInfo = 0x004987F0;
        g_Addr_GlobalVar = 0x00589540;
        g_Addr_DeleteItem = 0x004E4230;
    }
    else
    {
        return;
    }
}

void pk_inventory_cleanup()
{
    g_bMonitorThreadRunning = FALSE;
}