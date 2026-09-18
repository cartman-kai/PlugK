// x86 regression harness: production algorithms with game allocation mocked.
#include <assert.h>
#include "../plugK/src/shop_optimization.c"
#include "../plugK/src/shop_inf_stock.c"
#include "../plugK/src/item_stack.c"

PK_CONFIG g_pk_config = {0};
void SendGameTips(const char *text) { (void)text; }
MH_STATUS WINAPI MH_CreateHook(LPVOID target, LPVOID detour, LPVOID *original)
{
    (void)target; (void)detour; (void)original;
    return MH_OK;
}
MH_STATUS WINAPI MH_EnableHook(LPVOID target) { (void)target; return MH_OK; }

static DWORD records[256][4];
static DWORD fields[256][3];
static int liveItems;
static DWORD fakeVtable[1];

static void *__fastcall DeleteFake(void *item, void *edx, unsigned int flags)
{
    (void)edx;
    assert(flags == 1);
    free(item);
    --liveItems;
    return NULL;
}

static DWORD NewItem(int id, int type, int count)
{
    DWORD *item = (DWORD *)calloc(15, sizeof(DWORD));
    assert(item != NULL);
    fields[id][2] = (DWORD)type;
    records[id][1] = 3;
    records[id][2] = (DWORD)fields[id];
    item[0] = (DWORD)fakeVtable;
    item[5] = (DWORD)records[id];
    item[6] = (DWORD)id;
    item[7] = (DWORD)count;
    ++liveItems;
    return (DWORD)item;
}

static DWORD __fastcall CopyFake(void *shop, void *edx, void *source)
{
    DWORD *slots = (DWORD *)shop + 1;
    (void)edx;
    for (int i = 0; i < 50; ++i)
    {
        if (!slots[i])
        {
            DWORD *copy = (DWORD *)malloc(0x3C);
            assert(copy != NULL);
            memcpy(copy, source, 0x3C);
            slots[i] = (DWORD)copy;
            ++liveItems;
            return (DWORD)copy;
        }
    }
    return 0;
}

static int CountSlots(DWORD *shop)
{
    int count = 0;
    for (int i = 1; i <= 50; ++i)
        count += shop[i] != 0;
    return count;
}

static void ClearShop(DWORD *shop)
{
    for (int i = 1; i <= 50; ++i)
    {
        if (shop[i])
            DeleteFake((void *)shop[i], NULL, 1);
        shop[i] = 0;
    }
    assert(liveItems == 0);
}

static void CheckItem(DWORD *shop, int slot, int id, int count)
{
    DWORD *item = (DWORD *)shop[slot + 1];
    assert(item && item[6] == (DWORD)id && item[7] == (DWORD)count);
}

static void TestMixedSources(void)
{
    DWORD shop[51] = {123};
    DWORD throwing, other;
    DWORD throwingSnapshot[15], otherSnapshot[15];
    for (int i = 1; i <= 10; ++i) shop[i] = NewItem(4, 10, 1);
    for (int i = 11; i <= 20; ++i) shop[i] = NewItem(7, 10, 1);
    shop[21] = NewItem(4, 10, 1); // Random duplicate of a fixed recovery ID.
    shop[22] = NewItem(8, 10, 1); // Random-only recovery ID.
    shop[23] = NewItem(62, 20, 5);
    shop[24] = NewItem(62, 20, 1);
    shop[25] = NewItem(61, 20, 3);
    shop[26] = NewItem(149, 100, 1);
    shop[27] = NewItem(28, 30, 2);
    throwing = shop[23]; other = shop[27];
    memcpy(throwingSnapshot, (void *)throwing, 0x3C);
    memcpy(otherSnapshot, (void *)other, 0x3C);
    ApplyRecoveryBundles((DWORD)shop);
    PerformShopSort((DWORD)shop);
    assert(CountSlots(shop) == 14);
    for (int i = 0; i < 3; ++i)
    {
        CheckItem(shop, i, 4, g_ShopBundleTiers[i]);
        CheckItem(shop, 3 + i, 7, g_ShopBundleTiers[i]);
        CheckItem(shop, 6 + i, 8, g_ShopBundleTiers[i]);
    }
    CheckItem(shop, 9, 61, 3);
    CheckItem(shop, 10, 62, 1);
    CheckItem(shop, 11, 62, 5);
    CheckItem(shop, 12, 28, 2);
    CheckItem(shop, 13, 149, 1);
    assert(memcmp(throwingSnapshot, (void *)throwing, 0x3C) == 0);
    assert(memcmp(otherSnapshot, (void *)other, 0x3C) == 0);
    assert(shop[12] == throwing && shop[13] == other);
    // Normalizing an existing result must not multiply bundle sets.
    ApplyRecoveryBundles((DWORD)shop);
    PerformShopSort((DWORD)shop);
    assert(CountSlots(shop) == 14);
    ClearShop(shop);
}

static void TestCapacity(int otherCount, int recoveryCount, int tiers)
{
    DWORD shop[51] = {1};
    DWORD originals[50];
    for (int i = 0; i < otherCount; ++i)
        shop[i + 1] = originals[i] = NewItem(62, 20, i % 5 + 1);
    for (int i = 0; i < recoveryCount; ++i)
        shop[otherCount + i + 1] = NewItem(4 + i, 10, 1);
    ApplyRecoveryBundles((DWORD)shop);
    PerformShopSort((DWORD)shop);
    assert(CountSlots(shop) == otherCount + recoveryCount * tiers);
    for (int i = 0; i < recoveryCount; ++i)
        for (int j = 0; j < tiers; ++j)
            CheckItem(shop, i * tiers + j, 4 + i, g_ShopBundleTiers[j]);
    for (int i = 0; i < otherCount; ++i)
    {
        int found = 0;
        for (int j = 1; j <= 50; ++j) found += shop[j] == originals[i];
        assert(found == 1);
        assert(((DWORD *)originals[i])[7] == (DWORD)(i % 5 + 1));
    }
    ClearShop(shop);
}

static void TestStockAndLimits(void)
{
    DWORD shop[51] = {1};
    g_pk_config.shop_optimize = TRUE;
    for (int id = 3; id <= 16; ++id)
    {
        shop[1] = NewItem(id, 10, 10);
        assert(ShouldKeepItemPreCall((DWORD)shop, 0) == (id >= 4 && id <= 15));
        ClearShop(shop);
    }
    shop[1] = NewItem(4, 10, 10);
    g_pk_config.shop_optimize = FALSE;
    assert(!ShouldKeepItemPreCall((DWORD)shop, 0));
    assert(!ShouldKeepItemPreCall((DWORD)shop, 50));
    ClearShop(shop);
    g_pk_config.item_stack_limit_enabled = FALSE;
    assert(GetItemStackLimit() == 10);
    g_pk_config.item_stack_limit_enabled = TRUE;
    g_pk_config.item_stack_limit = 99;
    assert(GetItemStackLimit() == 99);
    g_pk_config.item_stack_limit = 5;
    assert(GetItemStackLimit() == 5);
}

int main(void)
{
    fakeVtable[0] = (DWORD)DeleteFake;
    fpCopyShopItem = CopyFake;
    TestMixedSources();
    TestCapacity(40, 4, 2);
    TestCapacity(47, 2, 1);
    TestCapacity(44, 2, 3);
    TestCapacity(49, 1, 1);
    TestCapacity(50, 0, 3);
    TestCapacity(0, 0, 3);
    TestStockAndLimits();
    puts("Shop regression checks passed.");
    return 0;
}
