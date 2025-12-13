#include "pch.h" // 确保包含 pch.h 如果项目设置了预编译头
#include "inventory.h"
#include "config.h"
#include <stdio.h>

// ---------------------------------------------------------
// 地址定义 (v1.05)
// ---------------------------------------------------------
#define ADDR_GET_CHAR_INFO 0x004892D0
#define ADDR_GLOBAL_VAR 0x005585C0
#define ADDR_DELETE_ITEM 0x004CF550 // [新增] 游戏原生删除函数

// 全局线程控制标记
volatile BOOL g_bMonitorThreadRunning = TRUE;

// 辅助结构
typedef struct
{
    int Index;
    ItemObject *Ptr;
    DWORD ID;
    DWORD Count;
    DWORD Level;
    DWORD Price;
} SortItemNode;

// 获取角色基址
DWORD GetCharacterBase()
{
    DWORD func = ADDR_GET_CHAR_INFO;
    DWORD param = ADDR_GLOBAL_VAR;
    DWORD result = 0;
    __asm {
        push param
        call func
        add esp, 4 
        mov result, eax
    }
    return result;
}

// [新增] 安全删除物品
// 模拟 sub_47FEE0 中的调用逻辑
void SafeDeleteItem(DWORD charBase, ItemObject *itemPtr)
{
    if (charBase == 0 || itemPtr == NULL)
        return;

    DWORD func_delete = ADDR_DELETE_ITEM;

    // 根据 ASM 分析：
    // 1. 待删除指针放入 [ESI + 0x88]
    // 2. ECX = ESI + 0x68
    // 3. call sub_4CF550

    DWORD target_slot = charBase + 0x88;
    DWORD this_param = charBase + 0x68;

    __try
    {
        // 1. 将指针写入游戏指定的临时槽位
        *(DWORD *)target_slot = (DWORD)itemPtr;

        // 2. 调用游戏删除函数
        __asm {
            mov ecx, this_param
            call func_delete
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        // 防止崩溃
        OutputDebugStringA("PlugK: Exception in SafeDeleteItem");
    }
}

BOOL IsStackable(DWORD id)
{
    if (id >= 4 && id <= 15)
        return TRUE; // 回复药
    if (id >= 22 && id <= 23)
        return TRUE; // 气/怒药
    if (id >= 60 && id <= 77)
        return TRUE; // 暗器/宝石
    return FALSE;
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

void PerformOrganize()
{
    DWORD charBase = GetCharacterBase();
    if (charBase == 0)
        return;

    int *slotArray = (int *)(charBase + 0xA4);
    DWORD *itemPoolPtr = *(DWORD **)(charBase + 0xA0);
    if (itemPoolPtr == NULL)
        return;

    SortItemNode items[50];
    int validCount = 0;

    // 1. 读取
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

    // 3. 合并逻辑
    for (int i = 0; i < validCount; i++)
    {
        if (items[i].Count == 0)
            continue;
        if (!IsStackable(items[i].ID))
            continue;

        for (int j = i + 1; j < validCount; j++)
        {
            if (items[j].ID != items[i].ID)
                break;
            if (items[j].Count == 0)
                continue;

            int space = 9 - (int)items[i].Count;
            if (space > 0)
            {
                int take = (items[j].Count >= (DWORD)space) ? space : items[j].Count;

                items[i].Count += take;
                items[j].Count -= take;

                // 更新内存
                items[i].Ptr->Count = items[i].Count;
                items[j].Ptr->Count = items[j].Count;

                // [重点] 如果物品被掏空 (Count变为0)，标记为需要删除
                // 我们在下一步统一处理删除，避免破坏当前循环的指针

                if (items[i].Count == 9)
                    break;
            }
        }
    }

    // 4. 处理被合并空的物品 & 准备最终排序
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
            // [新增] 安全删除逻辑
            // 数量为 0 的物品，调用游戏原生函数进行析构和链表移除
            SafeDeleteItem(charBase, items[i].Ptr);
            // 此时 items[i].Ptr 指向的内存已无效，不可再访问
        }
    }

    // 5. 最终排序
    qsort(finalItems, finalCount, sizeof(SortItemNode), CompareItems);

    // 6. 写回
    for (int i = 0; i < 50; i++)
    {
        slotArray[i] = -1;
    }
    for (int i = 0; i < finalCount; i++)
    {
        slotArray[i] = finalItems[i].Index;
    }

    // 7. 播放声音提示成功
    MessageBeep(MB_OK);
}

DWORD WINAPI InventoryMonitorThread(LPVOID lpParam)
{
    while (g_bMonitorThreadRunning)
    { // [优化] 使用全局标志控制循环
        Sleep(100);

        // 检查配置开关
        if (g_pk_config.inventory_sort)
        {
            // Ctrl + \ (VK_OEM_5)
            if ((GetAsyncKeyState(VK_CONTROL) & 0x8000) && (GetAsyncKeyState(0xDC) & 0x8000))
            {
                PerformOrganize();
                Sleep(500);
            }
        }
    }
    return 0;
}

void pk_inventory_init(int game_version)
{
    if (game_version != 105)
        return;

    // 创建线程
    CreateThread(NULL, 0, InventoryMonitorThread, NULL, 0, NULL);
}

// [新增] 线程清理函数 (供 dllmain 调用)
void pk_inventory_cleanup()
{
    g_bMonitorThreadRunning = FALSE;
}