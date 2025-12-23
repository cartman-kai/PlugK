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

// 偏移量  (两个版本一致，但为了扩展性也定义为变量)
static DWORD g_Offset_PoolPtr = 0xA0;   // 物品池指针数组
static DWORD g_Offset_SlotArray = 0xA4; // 背包格子 Index 数组
static DWORD g_Offset_DelSlot = 0x88;   // 删除用的临时槽位
static DWORD g_Offset_DelMgr = 0x68;    // 删除用的管理器对象

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

// [更新] 检查物品是否可堆叠
// 结合了硬编码 ID 范围和内存模板数据的双重检查
BOOL IsStackable(ItemObject *pItem)
{
    if (pItem == NULL)
        return FALSE;

    DWORD id = pItem->ItemID;
    BOOL isIdInScope = FALSE;

    // 1. 初步检查：ID 是否在允许堆叠的逻辑范围内
    if (id >= 4 && id <= 15)
        isIdInScope = TRUE; // 回复类药品
    else if (id >= 22 && id <= 23)
        isIdInScope = TRUE; // 召唤天兵
    else if (id >= 60 && id <= 77)
        isIdInScope = TRUE; // 暗器

    // 如果 ID 不在范围内，直接不允许堆叠
    if (!isIdInScope)
        return FALSE;

    // 2. 深度检查：读取内存模板数据，确认 CanStack 属性
    // 指针链: pItem -> +0x14 (TemplateRef) -> +0x08 (TemplateData) -> +0x18 (CanStack)

    __try
    {
        // Step 1: 获取 TemplateRef
        DWORD ptrTemplateRef = *(DWORD *)((DWORD)pItem + 0x14);
        if (ptrTemplateRef == 0 || IsBadReadPtr((void *)ptrTemplateRef, 16))
            return FALSE;

        // Step 2: 获取 TemplateData
        // TemplateRef + 0x08 指向实际数据结构
        DWORD ptrTemplateData = *(DWORD *)(ptrTemplateRef + 0x08);
        if (ptrTemplateData == 0 || IsBadReadPtr((void *)ptrTemplateData, 0x20))
            return FALSE;

        // Step 3: 读取 CanStack 属性
        // TemplateData + 0x18 是堆叠标记 (1=Yes, 0=No)
        int canStackFlag = *(int *)(ptrTemplateData + 0x18);

        if (canStackFlag == 1)
        {
            return TRUE;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        // 读取指针链发生异常，安全起见返回不可堆叠
        return FALSE;
    }

    return FALSE;
}

int CompareItems(const void *a, const void *b)
{
    SortItemNode *itemA = (SortItemNode *)a;
    SortItemNode *itemB = (SortItemNode *)b;

    // 1. ID 小 -> 大
    if (itemA->ID != itemB->ID)
        return (int)itemA->ID - (int)itemB->ID;
    // 2. 数量 大 -> 小
    if (itemA->Count != itemB->Count)
        return (int)itemB->Count - (int)itemA->Count;
    // 3. 等级 小 -> 大
    if (itemA->Level != itemB->Level)
        return (int)itemA->Level - (int)itemB->Level;
    // 4. 价格 低 -> 高
    if (itemA->Price > itemB->Price)
        return 1;
    if (itemA->Price < itemB->Price)
        return -1;

    return 0;
}

// ---------------------------------------------------------
// 核心整理逻辑
// ---------------------------------------------------------
void PerformOrganize()
{
    DWORD charBase = GetCharacterBase();
    if (charBase == 0)
        return;

    // 获取关键指针
    int *slotArray = (int *)(charBase + g_Offset_SlotArray);

    // charBase + 0xA0 存放的是指向 "指针数组" 的指针
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

    // 2. 预排序 (为了合并)
    qsort(items, validCount, sizeof(SortItemNode), CompareItems);

    // 3. 合并逻辑 (Stacking)
    for (int i = 0; i < validCount; i++)
    {
        if (items[i].Count == 0)
            continue;

        // [修改] 传入物品对象指针进行检查
        if (!IsStackable(items[i].Ptr))
            continue;

        for (int j = i + 1; j < validCount; j++)
        {
            if (items[j].ID != items[i].ID)
                break;
            if (items[j].Count == 0)
                continue;

            // 确保第二个物品也是可堆叠的 (理论上同ID应该属性相同，但双重检查更安全)
            if (!IsStackable(items[j].Ptr))
                continue;

            int space = 9 - (int)items[i].Count;
            if (space > 0)
            {
                int take = (items[j].Count >= (DWORD)space) ? space : items[j].Count;

                items[i].Count += take;
                items[j].Count -= take;

                // 立即更新内存
                items[i].Ptr->Count = items[i].Count;
                items[j].Ptr->Count = items[j].Count;

                if (items[i].Count == 9)
                    break;
            }
        }
    }

    // 4. 清理废弃物品 & 准备最终排序
    SortItemNode finalItems[50];
    int finalCount = 0;

    for (int i = 0; i < validCount; i++)
    {
        if (items[i].Count > 0)
        {
            finalItems[finalCount++] = items[i];
        }
        else
        {
            // Count == 0, 调用游戏删除函数
            SafeDeleteItem(charBase, items[i].Ptr);
        }
    }

    // 5. 最终排序
    qsort(finalItems, finalCount, sizeof(SortItemNode), CompareItems);

    // 6. 写回背包
    for (int i = 0; i < 50; i++)
    {
        slotArray[i] = -1;
    }
    for (int i = 0; i < finalCount; i++)
    {
        slotArray[i] = finalItems[i].Index;
    }

    // 7. 提示音效
    MessageBeep(MB_OK);
}

// ---------------------------------------------------------
// 线程与初始化
// ---------------------------------------------------------

DWORD WINAPI InventoryMonitorThread(LPVOID lpParam)
{
    while (g_bMonitorThreadRunning)
    {
        Sleep(100);

        if (g_pk_config.inventory_sort)
        {
            // 快捷键: Ctrl + \ (VK_OEM_5)
            if ((GetAsyncKeyState(VK_CONTROL) & 0x8000) && (GetAsyncKeyState(0xDC) & 0x8000))
            {
                PerformOrganize();
                Sleep(500); // 防抖
            }
        }
    }
    return 0;
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

    // 2. 启动线程
    CreateThread(NULL, 0, InventoryMonitorThread, NULL, 0, NULL);
}

void pk_inventory_cleanup()
{
    g_bMonitorThreadRunning = FALSE;
}