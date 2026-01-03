#include "pch.h"
#include "inv_auto_sort.h"
#include "config.h"
#include <stdio.h>

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
    int Index;
    ItemObject *Ptr;
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

    // ID > Count > Level > Price
    if (itemA->ID != itemB->ID)
        return (int)itemA->ID - (int)itemB->ID;
    if (itemA->Count != itemB->Count)
        return (int)itemB->Count - (int)itemA->Count;
    if (itemA->Level != itemB->Level)
        return (int)itemA->Level - (int)itemB->Level;
    if (itemA->Price > itemB->Price)
        return 1;
    if (itemA->Price < itemB->Price)
        return -1;
    return 0;
}

// ---------------------------------------------------------
// 核心整理逻辑
// ---------------------------------------------------------
void PerformOrganize(DWORD targetArrayOffset)
{
    DWORD charBase = GetCharacterBase();
    if (charBase == 0)
        return;

    int *slotArray = (int *)(charBase + targetArrayOffset);
    DWORD poolAddress = *(DWORD *)(charBase + g_Offset_PoolPtr);
    if (poolAddress == 0)
        return;
    DWORD *itemPoolPtr = (DWORD *)poolAddress;

    SortItemNode items[50];
    int validCount = 0;

    // 1. 读取有效物品
    for (int i = 0; i < 50; i++)
    {
        int idx = slotArray[i];
        if (idx != -1)
        {
            ItemObject *obj = (ItemObject *)itemPoolPtr[idx];
            if (obj != NULL && !IsBadReadPtr(obj, sizeof(ItemObject)))
            {
                items[validCount].Index = idx;
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

    // 2. 预排序
    qsort(items, validCount, sizeof(SortItemNode), CompareItems);

    // 3. 合并逻辑
    for (int i = 0; i < validCount; i++)
    {
        if (items[i].Count == 0)
            continue;
        if (!IsStackable(items[i].Ptr))
            continue;

        for (int j = i + 1; j < validCount; j++)
        {
            if (items[j].ID != items[i].ID)
                break;
            if (items[j].Count == 0)
                continue;
            if (!IsStackable(items[j].Ptr))
                continue;

            int space = 9 - (int)items[i].Count;
            if (space > 0)
            {
                int take = (items[j].Count >= (DWORD)space) ? space : items[j].Count;
                items[i].Count += take;
                items[j].Count -= take;
                items[i].Ptr->Count = items[i].Count;
                items[j].Ptr->Count = items[j].Count;
                if (items[i].Count == 9)
                    break;
            }
        }
    }

    // 4. 清理废弃 & 最终排序
    SortItemNode finalItems[50];
    int finalCount = 0;
    for (int i = 0; i < validCount; i++)
    {
        if (items[i].Count > 0)
            finalItems[finalCount++] = items[i];
        else
            SafeDeleteItem(charBase, items[i].Ptr);
    }
    qsort(finalItems, finalCount, sizeof(SortItemNode), CompareItems);

    // 5. 写回容器
    for (int i = 0; i < 50; i++)
        slotArray[i] = -1;
    for (int i = 0; i < finalCount; i++)
        slotArray[i] = finalItems[i].Index;
}

// ---------------------------------------------------------
// [新增] 清理快捷栏逻辑
// ---------------------------------------------------------
// 返回值: TRUE 表示有物品移动，需要重新整理背包
BOOL CleanupQuickSlots()
{
    DWORD charBase = GetCharacterBase();
    if (charBase == 0)
        return FALSE;

    // 获取指针
    int *invArray = (int *)(charBase + g_Offset_InventoryArr);
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
        // 20 为正常道具类型，如果不等于 20，则尝试移动
        if (obj->ItemID >= 22 && obj->ItemID <= 23)
            continue;

        if (obj->ItemID >= 60 && obj->ItemID <= 77)
            continue;

        // 寻找背包中的空位
        // 注意：我们假设在调用此函数前已经执行过 PerformOrganize
        // 所以背包是紧凑的，直接找第一个 -1 即可
        int freeSlot = -1;
        for (int i = 0; i < 50; i++)
        {
            if (invArray[i] == -1)
            {
                freeSlot = i;
                break;
            }
        }

        // 如果背包满了，直接退出循环，不再尝试移动
        if (freeSlot == -1)
        {
            break;
        }

        // 执行移动
        invArray[freeSlot] = qIdx; // 放入背包
        quickArray[q] = -1;        // 清空快捷栏
        hasMoved = TRUE;
    }

    return hasMoved;
}

// [新增] 封装背包整理的完整流程
void ExecuteInventorySortFlow()
{
    // 1. 先进行一次标准整理
    PerformOrganize(g_Offset_InventoryArr);

    // 2. 检查快捷栏，把非道具物品移入刚腾出的空位
    if (CleanupQuickSlots())
    {
        // 3. 如果有物品移入，再次整理
        PerformOrganize(g_Offset_InventoryArr);
    }

    MessageBeep(MB_OK);
}

void ExecuteStashSortFlow()
{
    // 1. 先进行一次标准整理
    PerformOrganize(g_Offset_StashArr);

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