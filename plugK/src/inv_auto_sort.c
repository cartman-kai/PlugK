#include "pch.h"
#include "inv_auto_sort.h"
#include "stash_ext.h" // 必须引用，获取MAX_PAGES和GetPtr
#include "config.h"
#include "show_tips.h"
#include <stdio.h>
#include <stdlib.h> // for qsort

// ---------------------------------------------------------
// 全局地址变量
// ---------------------------------------------------------
static DWORD g_Addr_GetCharInfo = 0;
static DWORD g_Addr_GlobalVar = 0;
static DWORD g_Addr_DeleteItem = 0;

// ---------------------------------------------------------
// 偏移量
// ---------------------------------------------------------
static DWORD g_Offset_PoolPtr = 0xA0;
static DWORD g_Offset_DelSlot = 0x88;
static DWORD g_Offset_DelMgr = 0x68;
static DWORD g_Offset_InventoryArr = 0xA4;
static DWORD g_Offset_QuickSlotArr = 0x184;
static DWORD g_Offset_StashArr = 0x1FC;

volatile BOOL g_bMonitorThreadRunning = TRUE;

// ---------------------------------------------------------
// 结构定义
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

// ---------------------------------------------------------
// 基础辅助函数
// ---------------------------------------------------------

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
    }
}

BOOL IsStackable(ItemObject *pItem)
{
    if (pItem == NULL)
        return FALSE;
    DWORD id = pItem->ItemID;
    // 简化的 ID 检查
    if (!((id >= 4 && id <= 15) || (id >= 22 && id <= 23) || (id >= 60 && id <= 77)))
        return FALSE;

    __try
    {
        DWORD ptrRef = *(DWORD *)((DWORD)pItem + 0x14);
        if (!ptrRef || IsBadReadPtr((void *)ptrRef, 16))
            return FALSE;
        DWORD ptrData = *(DWORD *)(ptrRef + 0x08);
        if (!ptrData || IsBadReadPtr((void *)ptrData, 0x20))
            return FALSE;
        return (*(int *)(ptrData + 0x18) == 1);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return FALSE;
    }
}

int CompareItems(const void *a, const void *b)
{
    SortItemNode *itemA = (SortItemNode *)a;
    SortItemNode *itemB = (SortItemNode *)b;
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
// 核心整理逻辑: 全页大一统 (支持 10 页)
// ---------------------------------------------------------
// type: 0 = Inventory, 1 = Stash
void PerformUnifiedOrganize(int type)
{
    DWORD charBase = GetCharacterBase();
    if (charBase == 0)
        return;

    DWORD poolAddress = *(DWORD *)(charBase + g_Offset_PoolPtr);
    if (poolAddress == 0)
        return;
    DWORD *itemPoolPtr = (DWORD *)poolAddress;

    // 1. 准备大容量数组 (10页 * 50格 = 500)
    int totalSlots = MAX_PAGES * 50;
    // 使用 static 避免栈溢出，或者 malloc
    // 注意：VC6 栈较小，建议 malloc 或 static。此处用 malloc 确保线程安全
    SortItemNode *items = (SortItemNode *)malloc(totalSlots * sizeof(SortItemNode));
    if (!items)
        return;

    int validCount = 0;

    // 2. 遍历所有 10 个页面收集物品
    for (int pageIdx = 0; pageIdx < MAX_PAGES; pageIdx++)
    {
        // 根据类型获取数据指针 (自动处理内存/缓存)
        int *pageData = (type == 0) ? GetInvPagePtr(pageIdx) : GetStashPagePtr(pageIdx);

        if (!pageData)
            continue;

        for (int slot = 0; slot < 50; slot++)
        {
            int itemIndex = pageData[slot];
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
    }

    if (validCount == 0)
    {
        free(items);
        return;
    }

    // 3. 排序与堆叠
    qsort(items, validCount, sizeof(SortItemNode), CompareItems);

    for (int i = 0; i < validCount; i++)
    {
        if (items[i].Count == 0 || !IsStackable(items[i].Ptr))
            continue;

        for (int j = i + 1; j < validCount; j++)
        {
            if (items[j].ID != items[i].ID)
                break;
            if (items[j].Count == 0 || !IsStackable(items[j].Ptr))
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

    // 4. 清理无效物品并重新压缩
    SortItemNode *finalItems = (SortItemNode *)malloc(totalSlots * sizeof(SortItemNode));
    int finalCount = 0;

    if (finalItems)
    {
        for (int i = 0; i < validCount; i++)
        {
            if (items[i].Count > 0)
                finalItems[finalCount++] = items[i];
            else
                SafeDeleteItem(charBase, items[i].Ptr);
        }
        qsort(finalItems, finalCount, sizeof(SortItemNode), CompareItems);

        // 5. 写回所有页面
        // 先全部清空
        for (int pageIdx = 0; pageIdx < MAX_PAGES; pageIdx++)
        {
            int *pageData = (type == 0) ? GetInvPagePtr(pageIdx) : GetStashPagePtr(pageIdx);
            if (pageData)
            {
                for (int k = 0; k < 50; k++)
                    pageData[k] = -1;
            }
        }

        // 再填充
        for (int i = 0; i < finalCount; i++)
        {
            int targetPage = i / 50;
            int targetSlot = i % 50;

            if (targetPage < MAX_PAGES)
            {
                int *pageData = (type == 0) ? GetInvPagePtr(targetPage) : GetStashPagePtr(targetPage);
                if (pageData)
                {
                    pageData[targetSlot] = finalItems[i].Index;
                }
            }
        }
        free(finalItems);
    }

    free(items);
}

// ---------------------------------------------------------
// 快捷栏清理逻辑 (全局搜索空位)
// ---------------------------------------------------------
BOOL CleanupQuickSlots()
{
    DWORD charBase = GetCharacterBase();
    if (charBase == 0)
        return FALSE;

    int *quickArray = (int *)(charBase + g_Offset_QuickSlotArr);
    DWORD poolAddress = *(DWORD *)(charBase + g_Offset_PoolPtr);
    if (!poolAddress)
        return FALSE;
    DWORD *itemPoolPtr = (DWORD *)poolAddress;

    BOOL hasMoved = FALSE;

    for (int q = 0; q < 6; q++)
    {
        int qIdx = quickArray[q];
        if (qIdx == -1)
            continue;

        ItemObject *obj = (ItemObject *)itemPoolPtr[qIdx];
        if (!obj || IsBadReadPtr(obj, sizeof(ItemObject)))
            continue;

        // 判断是否为需要清理的道具
        if ((obj->ItemID >= 22 && obj->ItemID <= 23) || (obj->ItemID >= 60 && obj->ItemID <= 77))
            continue; // 保留

        // 寻找空位：遍历 0-9 页
        int foundPage = -1;
        int foundSlot = -1;

        for (int page = 0; page < MAX_PAGES; page++)
        {
            int *pageData = GetInvPagePtr(page);
            if (!pageData)
                continue;

            for (int slot = 0; slot < 50; slot++)
            {
                if (pageData[slot] == -1)
                {
                    foundPage = page;
                    foundSlot = slot;
                    break;
                }
            }
            if (foundPage != -1)
                break;
        }

        // 移动
        if (foundPage != -1)
        {
            int *pageData = GetInvPagePtr(foundPage);
            pageData[foundSlot] = qIdx;
            quickArray[q] = -1;
            hasMoved = TRUE;
        }
    }

    return hasMoved;
}

// ---------------------------------------------------------
// 流程封装
// ---------------------------------------------------------

void ExecuteInventorySortFlow()
{
    // 1. 10页全量整理
    PerformUnifiedOrganize(0);

    // 2. 检查快捷栏 (移动到任意空页)
    if (CleanupQuickSlots())
    {
        // 3. 再次全量整理
        PerformUnifiedOrganize(0);
    }

    ShowGameLog("[背包] 整理完成 (10页)");
}

void ExecuteStashSortFlow()
{
    // 储物箱也使用全量整理，因为现在数据结构已统一为 10 页
    // 若只整理一页，其他 9 页的数据将无法被利用或整理
    PerformUnifiedOrganize(1);
    ShowGameLog("[储物箱] 整理完成 (10页)");
}

void Mod_inv_auto_sort_init(int game_version)
{
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
}

void pk_inventory_cleanup()
{
    g_bMonitorThreadRunning = FALSE;
}