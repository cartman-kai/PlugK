#include "pch.h"
#include "item_split.h"
#include "config.h"
#include "inv_auto_sort.h"
#include "show_tips.h"
#include <MinHook.h>

static DWORD g_Addr_ItemIn = 0;
static DWORD g_Addr_FindStackSlot = 0;
static DWORD g_Addr_GetGroundPos = 0;
static DWORD g_Addr_CopyPos = 0;
static DWORD g_Addr_CreateGroundItem = 0;
static DWORD g_Addr_GroundItemMgrPtr = 0;

static volatile LONG g_SuppressStackMerge = 0;

typedef int(__fastcall *tFindStackSlot)(void *thisPtr, void *_edx, int itemId, int count, int *outCount);
static tFindStackSlot fpOriginalFindStackSlot = NULL;

static int __fastcall Detour_FindStackSlot(void *thisPtr, void *_edx, int itemId, int count, int *outCount)
{
    if (g_SuppressStackMerge)
    {
        if (outCount)
            *outCount = 0;
        return -1;
    }

    return fpOriginalFindStackSlot(thisPtr, _edx, itemId, count, outCount);
}

static BOOL HasInventoryFreeSlot(int *slotArray)
{
    if (!slotArray)
        return FALSE;

    for (int i = 0; i < 50; i++)
    {
        if (slotArray[i] == -1)
            return TRUE;
    }

    return FALSE;
}

static ItemObject *GetInventoryItem(DWORD charBase, DWORD *itemPool, int slot)
{
    int index;
    ItemObject *item;

    if (charBase == 0 || itemPool == NULL || slot < 0 || slot >= 50)
        return NULL;

    index = *(int *)(charBase + 0xA4 + slot * sizeof(int));
    if (index == -1)
        return NULL;

    item = (ItemObject *)itemPool[index];
    if (item == NULL || IsBadReadPtr(item, sizeof(ItemObject)))
        return NULL;

    return item;
}

static int FindFirstSplittableSlot(DWORD charBase, DWORD *itemPool)
{
    for (int i = 0; i < 50; i++)
    {
        ItemObject *item = GetInventoryItem(charBase, itemPool, i);
        if (item != NULL && item->Count > 1 && IsStackable(item))
            return i;
    }

    return -1;
}

static ItemObject *CallItemIn(DWORD charBase, DWORD itemId, DWORD count)
{
    ItemObject *result = NULL;
    DWORD func = g_Addr_ItemIn;
    DWORD a6 = 0xFFFFFFFF;
    DWORD a7 = 0xFFFFFFFF;

    if (charBase == 0 || func == 0 || count == 0)
        return NULL;

    __asm {
        push a7
        push a6
        push 0
        push 0
        push count
        push itemId
        mov ecx, charBase
        call func
        mov result, eax
    }

    return result;
}

static void *CreateGroundSplitItem(DWORD charBase, DWORD itemId, DWORD count)
{
    DWORD pos[5] = {0};
    DWORD context = 0;
    DWORD result = 0;
    DWORD groundMgr = 0;

    if (charBase == 0 || itemId == 0 || count == 0)
        return NULL;
    if (g_Addr_GetGroundPos == 0 || g_Addr_CopyPos == 0 ||
        g_Addr_CreateGroundItem == 0 || g_Addr_GroundItemMgrPtr == 0)
        return NULL;

    __asm {
        mov ecx, charBase
        call g_Addr_GetGroundPos
        mov context, eax
    }

    if (context == 0)
        return NULL;

    context += 0x2C;

    __asm {
        push context
        lea eax, pos
        push eax
        call g_Addr_CopyPos
        add esp, 8
    }

    groundMgr = *(DWORD *)g_Addr_GroundItemMgrPtr;
    if (groundMgr == 0)
        return NULL;

    __asm {
        push count
        push 0
        push dword ptr [pos+4]
        push dword ptr [pos]
        push itemId
        mov ecx, groundMgr
        call g_Addr_CreateGroundItem
        mov result, eax
    }

    return (void *)result;
}

void ExecuteFirstInventoryItemSplitFlow(void)
{
    DWORD charBase = GetCharacterBase();
    DWORD poolAddress;
    DWORD *itemPool;
    int *slotArray;
    int slot;
    ItemObject *item;
    DWORD itemId;
    DWORD oldCount;
    DWORD splitCount;
    BOOL hasFreeSlot;
    BOOL created = FALSE;

    if (charBase == 0)
    {
        SendGameTips("[背包] 当前无法拆分物品");
        return;
    }

    if (g_Addr_ItemIn == 0 || g_Addr_FindStackSlot == 0)
    {
        SendGameTips("[背包] 当前版本暂不支持拆分");
        return;
    }

    poolAddress = *(DWORD *)(charBase + 0xA0);
    if (poolAddress == 0)
    {
        SendGameTips("[背包] 当前无法拆分物品");
        return;
    }

    itemPool = (DWORD *)poolAddress;
    slotArray = (int *)(charBase + 0xA4);
    slot = FindFirstSplittableSlot(charBase, itemPool);
    if (slot < 0)
    {
        SendGameTips("[背包] 没有可拆分的叠加物品");
        return;
    }

    item = GetInventoryItem(charBase, itemPool, slot);
    if (item == NULL || item->Count <= 1)
    {
        SendGameTips("[背包] 当前无法拆分物品");
        return;
    }

    itemId = item->ItemID;
    oldCount = item->Count;
    splitCount = 1;

    hasFreeSlot = HasInventoryFreeSlot(slotArray);
    InterlockedIncrement(&g_SuppressStackMerge);
    __try
    {
        if (hasFreeSlot)
        {
            created = CallItemIn(charBase, itemId, splitCount) != NULL;
        }
        else
        {
            created = CreateGroundSplitItem(charBase, itemId, splitCount) != NULL;
        }
    }
    __finally
    {
        InterlockedDecrement(&g_SuppressStackMerge);
    }

    item = GetInventoryItem(charBase, itemPool, slot);
    if (!created || item == NULL || item->ItemID != itemId || item->Count != oldCount)
    {
        SendGameTips("[背包] 拆分失败");
        return;
    }

    item->Count = oldCount - splitCount;

    if (hasFreeSlot)
        SendGameTips("[背包] 已从第一个可拆物品中拆出 1 个");
    else
        SendGameTips("[背包] 已从第一个可拆物品中拆出 1 个，背包已满，物品掉落在地面");
}

void Mod_Item_Split_Init(int game_version)
{
    if (game_version == 105)
    {
        g_Addr_ItemIn = 0x0047EF40;
        g_Addr_FindStackSlot = 0x0047EDF0;
        g_Addr_GetGroundPos = 0x00480560;
        g_Addr_CopyPos = 0x0040B0E0;
        g_Addr_CreateGroundItem = 0x0048B4F0;
        g_Addr_GroundItemMgrPtr = 0x00558E4C;
    }
    else if (game_version == 201)
    {
        g_Addr_ItemIn = 0x0048DD30;
        g_Addr_FindStackSlot = 0x0048DBE0;
        g_Addr_GetGroundPos = 0x0048F360;
        g_Addr_CopyPos = 0x004125B0;
        g_Addr_CreateGroundItem = 0x0049AB70;
        g_Addr_GroundItemMgrPtr = 0x00589E34;
    }
    else
    {
        return;
    }

    if (g_Addr_FindStackSlot != 0 &&
        MH_CreateHook((LPVOID)g_Addr_FindStackSlot,
                      &Detour_FindStackSlot,
                      (LPVOID *)&fpOriginalFindStackSlot) == MH_OK)
    {
        MH_EnableHook((LPVOID)g_Addr_FindStackSlot);
    }
}
