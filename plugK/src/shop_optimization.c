#include "pch.h"
#include "shop_optimization.h"
#include "config.h"
#include "show_tips.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <MinHook.h>

#define SHOP_SLOT_COUNT 50
#define SHOP_BUNDLE_TIER_COUNT 3

static const int g_ShopBundleTiers[SHOP_BUNDLE_TIER_COUNT] = {1, 5, 10};
static DWORD g_Addr_TableCount_02 = 0;
static DWORD g_Addr_TablePtr_02 = 0;
static DWORD g_Addr_PropMdl_TableCnt_01 = 0;
static DWORD g_Addr_PropMdl_TablePtr_01 = 0;
static DWORD g_Flag_GemStackStatus = 0;

typedef struct
{
    DWORD ItemPtr;
    DWORD ItemID;
    DWORD ItemCount;
    int Group;
} ShopSortNode;

typedef int(__fastcall *tLoadAllData)(void *pThis, void *_edx);
typedef int(__fastcall *tGenerateShop)(void *pThis, void *_edx, void *actor);
typedef DWORD(__fastcall *tCopyShopItem)(void *shop, void *_edx, void *item);
typedef void *(__fastcall *tDeleteItem)(void *item, void *_edx, unsigned int flags);

static tLoadAllData fpLoadAllData = NULL;
static tGenerateShop fpGenerateShop = NULL;
static tCopyShopItem fpCopyShopItem = NULL;

static int GetItemGroup(DWORD itemPtr)
{
    DWORD recordAddress = *(DWORD *)(itemPtr + 0x14);
    DWORD fields;
    int itemType;

    if (recordAddress == 0 || IsBadReadPtr((void *)recordAddress, 16) ||
        *(DWORD *)(recordAddress + 4) <= 2)
        return 2;
    fields = *(DWORD *)(recordAddress + 8);
    if (fields == 0 || IsBadReadPtr((void *)fields, 12))
        return 2;

    itemType = *(int *)(fields + 8);
    if (itemType >= 10 && itemType <= 19)
        return 0;
    if (itemType >= 20 && itemType <= 29)
        return 1;
    return 2;
}

static DWORD FindShopAddress(DWORD shopManagerAddress, DWORD merchantId)
{
    int count;
    DWORD node;

    if (shopManagerAddress == 0 || IsBadReadPtr((void *)shopManagerAddress, 12))
        return 0;

    count = *(int *)shopManagerAddress;
    node = *(DWORD *)(shopManagerAddress + 4);
    for (int i = 0; i < count && node != 0; ++i)
    {
        DWORD shopAddress;
        if (IsBadReadPtr((void *)node, 12))
            return 0;

        shopAddress = *(DWORD *)(node + 8);
        if (shopAddress != 0 && !IsBadReadPtr((void *)shopAddress, 4) &&
            *(DWORD *)shopAddress == merchantId)
            return shopAddress;

        node = *(DWORD *)node;
    }

    return 0;
}

static int CompareShopItems(const void *a, const void *b)
{
    const ShopSortNode *itemA = (const ShopSortNode *)a;
    const ShopSortNode *itemB = (const ShopSortNode *)b;

    if (itemA->Group != itemB->Group)
        return itemA->Group - itemB->Group;
    if (itemA->ItemID != itemB->ItemID)
        return itemA->ItemID < itemB->ItemID ? -1 : 1;
    if (itemA->ItemCount != itemB->ItemCount)
        return itemA->ItemCount < itemB->ItemCount ? -1 : 1;
    return 0;
}

static void PerformShopSort(DWORD shopAddress)
{
    DWORD *slots;
    ShopSortNode items[SHOP_SLOT_COUNT];
    int count = 0;

    if (shopAddress == 0 || IsBadReadPtr((void *)shopAddress, 0xCC))
        return;
    slots = (DWORD *)(shopAddress + 4);
    for (int i = 0; i < SHOP_SLOT_COUNT; ++i)
    {
        DWORD item = slots[i];
        if (item == 0)
            continue;
        if (IsBadReadPtr((void *)item, 0x3C))
            return;
        items[count].ItemPtr = item;
        items[count].ItemID = *(DWORD *)(item + 0x18);
        items[count].ItemCount = *(DWORD *)(item + 0x1C);
        items[count].Group = GetItemGroup(item);
        ++count;
    }
    qsort(items, count, sizeof(ShopSortNode), CompareShopItems);
    for (int i = 0; i < SHOP_SLOT_COUNT; ++i)
        slots[i] = i < count ? items[i].ItemPtr : 0;
}

static void ApplyRecoveryBundles(DWORD shopAddress)
{
    DWORD seeds[SHOP_SLOT_COUNT];
    DWORD ids[SHOP_SLOT_COUNT];
    DWORD duplicates[SHOP_SLOT_COUNT];
    DWORD *slots;
    int recoveryCount = 0;
    int otherCount = 0;
    int duplicateCount = 0;
    int tierCount = SHOP_BUNDLE_TIER_COUNT;

    if (shopAddress == 0 || fpCopyShopItem == NULL ||
        IsBadReadPtr((void *)shopAddress, 0xCC))
        return;
    slots = (DWORD *)(shopAddress + 4);

    // Fixed goods precede random goods. Keep the first instance of each
    // recovery ID, so random duplicates never add another bundle set.
    for (int i = 0; i < SHOP_SLOT_COUNT; ++i)
    {
        DWORD item = slots[i];
        DWORD id;
        int groupIndex;

        if (item == 0)
            continue;
        if (IsBadReadPtr((void *)item, 0x3C))
            return;
        if (GetItemGroup(item) != 0)
        {
            ++otherCount;
            continue;
        }
        id = *(DWORD *)(item + 0x18);
        for (groupIndex = 0; groupIndex < recoveryCount; ++groupIndex)
        {
            if (ids[groupIndex] == id)
                break;
        }
        if (groupIndex == recoveryCount)
        {
            ids[recoveryCount] = id;
            seeds[recoveryCount++] = item;
        }
        else
            duplicates[duplicateCount++] = (DWORD)i;
    }

    // Every recovery ID already occupied a slot, so one tier always fits.
    // Non-recovery objects and their counts are never changed or removed.
    while (tierCount > 1 && otherCount + recoveryCount * tierCount > SHOP_SLOT_COUNT)
        --tierCount;

    for (int i = 0; i < duplicateCount; ++i)
    {
        DWORD slot = duplicates[i];
        DWORD item = slots[slot];
        tDeleteItem deleteItem = (tDeleteItem)**(DWORD **)item;
        slots[slot] = 0;
        // Scalar deleting destructor also releases the game's CString.
        deleteItem((void *)item, NULL, 1);
    }

    for (int i = 0; i < recoveryCount; ++i)
    {
        *(DWORD *)(seeds[i] + 0x1C) = (DWORD)g_ShopBundleTiers[0];
        for (int tier = 1; tier < tierCount; ++tier)
        {
            DWORD copy = fpCopyShopItem((void *)shopAddress, NULL, (void *)seeds[i]);
            if (copy == 0)
                break;
            // Sale quantities do not depend on player stacking configuration.
            *(DWORD *)(copy + 0x1C) = (DWORD)g_ShopBundleTiers[tier];
        }
    }
}

static int __fastcall Detour_GenerateShop(void *pThis, void *_edx, void *actor)
{
    int result;
    DWORD shopAddress;

    (void)_edx;
    // Let the original finish all fixed/random generation before normalizing.
    result = fpGenerateShop(pThis, NULL, actor);
    if (!result || actor == NULL || IsBadReadPtr(actor, 0x18))
        return result;
    shopAddress = FindShopAddress((DWORD)pThis, *(DWORD *)((DWORD)actor + 0x14));
    ApplyRecoveryBundles(shopAddress);
    PerformShopSort(shopAddress);
    return result;
}

static void SetItemStackProp(DWORD count, DWORD tableBaseAddress)
{
    if (count == 0 || tableBaseAddress == 0)
        return;

    for (DWORD i = 0; i < count; ++i)
    {
        DWORD itemDataPtr = *(DWORD *)(tableBaseAddress + i * 16 + 8);
        DWORD itemID;
        DWORD itemType;
        int *canStack;

        if (itemDataPtr == 0 || IsBadReadPtr((void *)itemDataPtr, 0x20))
            continue;

        itemID = *(DWORD *)(itemDataPtr + 4);
        itemType = *(DWORD *)(itemDataPtr + 8);
        canStack = (int *)(itemDataPtr + 0x18);

        if (itemID >= 4 && itemID <= 15 && g_pk_config.enable_consumable_stack)
            *canStack = 1;
        else if (itemType >= 30 && itemType <= 35 && g_pk_config.enable_gem_stack)
        {
            *canStack = 1;
            g_Flag_GemStackStatus = 1;
        }
    }
}

static void SetItemTableGemStack(DWORD count, DWORD tableBaseAddress, DWORD enableStack)
{
    if (count == 0 || tableBaseAddress == 0)
        return;

    for (DWORD i = 0; i < count; ++i)
    {
        DWORD itemDataPtr = *(DWORD *)(tableBaseAddress + i * 16 + 8);
        DWORD itemType;

        if (itemDataPtr == 0 || IsBadReadPtr((void *)itemDataPtr, 0x20))
            continue;

        itemType = *(DWORD *)(itemDataPtr + 8);
        if (itemType >= 30 && itemType <= 35 && g_pk_config.enable_gem_stack)
            *(int *)(itemDataPtr + 0x18) = (int)enableStack;
    }
}

static void SetGemStackProp(DWORD enableStack)
{
    DWORD count_02 = *(DWORD *)g_Addr_TableCount_02;
    DWORD tableBase_02 = *(DWORD *)g_Addr_TablePtr_02;
    DWORD count_01;
    DWORD tableBase_01;

    if (count_02 == 0 || tableBase_02 == 0)
        return;
    SetItemTableGemStack(count_02, tableBase_02, enableStack);

    count_01 = *(DWORD *)g_Addr_PropMdl_TableCnt_01;
    tableBase_01 = *(DWORD *)g_Addr_PropMdl_TablePtr_01;
    if (count_01 == 0 || tableBase_01 == 0)
        return;
    SetItemTableGemStack(count_01, tableBase_01, enableStack);

    g_Flag_GemStackStatus = enableStack;
}

void ToggleChangeGemStackProp()
{
    if (!g_pk_config.enable_gem_stack)
    {
        SendGameTips("功能未开启");
        return;
    }

    if (g_Flag_GemStackStatus)
    {
        SetGemStackProp(0);
        SendGameTips("关闭宝石叠加");
        return;
    }

    SetGemStackProp(1);
    SendGameTips("开启宝石叠加");
}

static void PatchItemStackProp()
{
    DWORD count_02 = *(DWORD *)g_Addr_TableCount_02;
    DWORD tableBase_02 = *(DWORD *)g_Addr_TablePtr_02;
    DWORD count_01;
    DWORD tableBase_01;

    if (count_02 == 0 || tableBase_02 == 0)
        return;
    SetItemStackProp(count_02, tableBase_02);

    count_01 = *(DWORD *)g_Addr_PropMdl_TableCnt_01;
    tableBase_01 = *(DWORD *)g_Addr_PropMdl_TablePtr_01;
    if (count_01 == 0 || tableBase_01 == 0)
        return;
    SetItemStackProp(count_01, tableBase_01);
}

static int __fastcall Detour_LoadAllData(void *pThis, void *_edx)
{
    int result = fpLoadAllData(pThis, _edx);
    PatchItemStackProp();
    return result;
}

static BOOL CreateAndEnableHook(void *target, void *detour, void **original)
{
    MH_STATUS status = MH_CreateHook(target, detour, original);
    if (status != MH_OK)
    {
        char errorMessage[256];
        sprintf_s(errorMessage, sizeof(errorMessage),
                  "PlugK: Hook Create Failed! Error Code: %d", status);
        MessageBoxA(NULL, errorMessage, "Hook Error", MB_ICONERROR);
        return FALSE;
    }

    status = MH_EnableHook(target);
    if (status != MH_OK)
    {
        MessageBoxA(NULL, "PlugK: Hook Enable Failed!", "Hook Error", MB_ICONERROR);
        return FALSE;
    }

    return TRUE;
}

void Mod_shop_opt_init(int game_version)
{
    void *targetAddr_LoadData = NULL;
    void *targetAddr_GenerateShop = NULL;

    if (game_version == 105)
    {
        targetAddr_LoadData = (void *)0x00405300;
        targetAddr_GenerateShop = (void *)0x00453530;
        g_Addr_TableCount_02 = 0x00548340;
        g_Addr_TablePtr_02 = 0x00548344;
        g_Addr_PropMdl_TableCnt_01 = 0x005488BC;
        g_Addr_PropMdl_TablePtr_01 = 0x005488C0;
        fpCopyShopItem = (tCopyShopItem)0x00453210;
    }
    else if (game_version == 201)
    {
        targetAddr_LoadData = (void *)0x0040C290;
        targetAddr_GenerateShop = (void *)0x0045F660;
        g_Addr_TableCount_02 = 0x005788D0;
        g_Addr_TablePtr_02 = 0x005788D4;
        g_Addr_PropMdl_TableCnt_01 = 0x00578E74;
        g_Addr_PropMdl_TablePtr_01 = 0x00578E78;
        fpCopyShopItem = (tCopyShopItem)0x0045F340;
    }
    else
    {
        return;
    }

    if (!CreateAndEnableHook(targetAddr_LoadData, &Detour_LoadAllData,
                             (void **)&fpLoadAllData))
        return;

    if (g_pk_config.shop_optimize)
    {
        CreateAndEnableHook(targetAddr_GenerateShop, &Detour_GenerateShop,
                            (void **)&fpGenerateShop);
    }
}
