#include "pch.h"
#include "drop_bias.h"
#include "config.h"
#include <MinHook.h>
#include <intrin.h>
#include <stdlib.h>
#include <string.h>

#pragma intrinsic(_ReturnAddress)

extern int g_StashPageB[50];
extern int g_InvPageB[50];

#define VER_105 105
#define VER_201 201

#define ADDR_105_SELECT_DROP_ITEM 0x00452200
#define ADDR_105_CHAR_PTR 0x00558AC4
#define ADDR_105_PROPMDL_TABLE 0x00548340

#define RET_105_DROP_SELECT_1 0x00452630
#define RET_105_DROP_SELECT_2 0x00452641
#define RET_105_DROP_SELECT_3 0x00452652
#define RET_105_DROP_SELECT_4 0x00452663

#define ADDR_201_SELECT_DROP_ITEM 0x0045E390
#define ADDR_201_CHAR_PTR 0x00589A44
#define ADDR_201_PROPMDL_TABLE 0x005788D0

#define RET_201_DROP_SELECT_1 0x0045E7B0
#define RET_201_DROP_SELECT_2 0x0045E7C1
#define RET_201_DROP_SELECT_3 0x0045E7D2
#define RET_201_DROP_SELECT_4 0x0045E7E3

#define OFFSET_ITEM_POOL 0xA0
#define OFFSET_INV 0xA4
#define OFFSET_QUICK 0x184
#define OFFSET_SOCKET 0x1B4
#define OFFSET_CHARM 0x1E4
#define OFFSET_STASH 0x1FC

#define COL_ITEM_ID 1
#define COL_ITEM_TYPE 2
#define COL_ITEM_LEVEL 8

#define MAX_ITEM_ID_TRACK 1024
#define MAX_CANDIDATES 128
#define RECENT_COUNT_CAP 20

typedef int(__cdecl *tSelectDropItem)(int level, int type_hint);

typedef struct DropBiasVersionConfig
{
    DWORD select_drop_item;
    DWORD char_ptr;
    DWORD propmdl_table;
    DWORD returns[4];
} DropBiasVersionConfig;

static tSelectDropItem fpSelectDropItem = NULL;
static DropBiasVersionConfig g_drop_bias_config;
static int g_recent_drop_count[MAX_ITEM_ID_TRACK];
static DWORD g_last_char_base = 0;
static DWORD g_local_rng_state = 0;

typedef struct DropCandidate
{
    int item_id;
    int count;
} DropCandidate;

static int IsDropSelectReturn(void *return_address)
{
    DWORD ret = (DWORD)return_address;
    int i;

    for (i = 0; i < 4; ++i)
    {
        if (g_drop_bias_config.returns[i] == ret)
            return 1;
    }

    return 0;
}

static int IsSupportedItemType(int item_type)
{
    return item_type == 10 ||
           (item_type >= 20 && item_type <= 29) ||
           (item_type >= 30 && item_type <= 35);
}

static int NextLocalRandom(int max_value)
{
    DWORD seed;

    if (max_value <= 1)
        return 0;

    if (g_local_rng_state == 0)
    {
        seed = GetTickCount();
        seed ^= (DWORD)&g_local_rng_state;
        seed ^= (DWORD)GetCurrentThreadId();
        if (seed == 0)
            seed = 0x13579BDF;
        g_local_rng_state = seed;
    }

    g_local_rng_state = g_local_rng_state * 1103515245u + 12345u;
    return (int)((g_local_rng_state >> 16) % (DWORD)max_value);
}

static int ReadRecordInt(DWORD record, int column)
{
    DWORD values;
    int count;

    if (record == 0 || IsBadReadPtr((void *)record, 0x10))
        return 0xFFFF;

    count = *(int *)(record + 4);
    if (column < 0 || column >= count)
        return 0xFFFF;

    values = *(DWORD *)(record + 8);
    if (values == 0 || IsBadReadPtr((void *)values, sizeof(int) * (column + 1)))
        return 0xFFFF;

    return *(int *)(values + column * 4);
}

static DWORD GetPropMdlBase(int *out_count)
{
    int count;
    DWORD base;

    if (g_drop_bias_config.propmdl_table == 0)
        return 0;
    if (IsBadReadPtr((void *)g_drop_bias_config.propmdl_table, 8))
        return 0;

    count = *(int *)g_drop_bias_config.propmdl_table;
    base = *(DWORD *)(g_drop_bias_config.propmdl_table + 4);

    if (count <= 0 || count > 4096 || base == 0)
        return 0;
    if (IsBadReadPtr((void *)base, count * 16))
        return 0;

    if (out_count)
        *out_count = count;
    return base;
}

static DWORD FindPropRecordById(int item_id)
{
    int count;
    DWORD base = GetPropMdlBase(&count);
    int i;

    if (!base)
        return 0;

    for (i = 0; i < count; ++i)
    {
        DWORD record = base + i * 16;
        if (ReadRecordInt(record, COL_ITEM_ID) == item_id)
            return record;
    }

    return 0;
}

static DWORD GetCharBase(void)
{
    DWORD char_base;

    if (g_drop_bias_config.char_ptr == 0)
        return 0;
    if (IsBadReadPtr((void *)g_drop_bias_config.char_ptr, sizeof(DWORD)))
        return 0;

    char_base = *(DWORD *)g_drop_bias_config.char_ptr;
    if (char_base == 0 || IsBadReadPtr((void *)char_base, 0x300))
        return 0;

    if (g_last_char_base != char_base)
    {
        DropBias_ResetRecent();
        g_last_char_base = char_base;
    }

    return char_base;
}

static int GetItemIdFromPoolIndex(DWORD *item_pool, int index, int *out_count)
{
    DWORD item;
    int count = 1;

    if (index < 0 || !item_pool)
        return -1;

    if (IsBadReadPtr(item_pool + index, sizeof(DWORD)))
        return -1;

    item = item_pool[index];
    if (item == 0 || IsBadReadPtr((void *)item, 0x20))
        return -1;

    count = *(int *)(item + 0x1C);
    if (count <= 0)
        count = 1;

    if (out_count)
        *out_count = count;

    return *(int *)(item + 0x18);
}

static int CountItemInSlots(DWORD *item_pool, int *slots, int slot_count, int item_id)
{
    int total = 0;
    int i;

    if (!item_pool || !slots || slot_count <= 0)
        return 0;
    if (IsBadReadPtr(slots, sizeof(int) * slot_count))
        return 0;

    for (i = 0; i < slot_count; ++i)
    {
        int count = 0;
        int id = GetItemIdFromPoolIndex(item_pool, slots[i], &count);
        if (id == item_id)
            total += count;
    }

    return total;
}

static int CountExistingItem(int item_id)
{
    DWORD char_base = GetCharBase();
    DWORD pool_addr;
    DWORD *item_pool;
    int total = 0;

    if (!char_base)
        return 0;

    pool_addr = *(DWORD *)(char_base + OFFSET_ITEM_POOL);
    if (pool_addr == 0 || IsBadReadPtr((void *)pool_addr, sizeof(DWORD)))
        return 0;
    item_pool = (DWORD *)pool_addr;

    total += CountItemInSlots(item_pool, (int *)(char_base + OFFSET_INV), 50, item_id);
    total += CountItemInSlots(item_pool, (int *)(char_base + OFFSET_QUICK), 12, item_id);
    total += CountItemInSlots(item_pool, (int *)(char_base + OFFSET_SOCKET), 12, item_id);
    total += CountItemInSlots(item_pool, (int *)(char_base + OFFSET_CHARM), 6, item_id);
    total += CountItemInSlots(item_pool, (int *)(char_base + OFFSET_STASH), 50, item_id);

    if (g_pk_config.stash_ext_enabled)
    {
        total += CountItemInSlots(item_pool, g_InvPageB, 50, item_id);
        total += CountItemInSlots(item_pool, g_StashPageB, 50, item_id);
    }

    if (item_id >= 0 && item_id < MAX_ITEM_ID_TRACK)
        total += g_recent_drop_count[item_id];

    return total;
}

static int CollectCandidates(int item_type, int item_level, DropCandidate *candidates, int max_candidates)
{
    int count;
    DWORD base = GetPropMdlBase(&count);
    int candidate_count = 0;
    int i;

    if (!base || !candidates || max_candidates <= 0)
        return 0;

    for (i = 0; i < count && candidate_count < max_candidates; ++i)
    {
        DWORD record = base + i * 16;
        int id = ReadRecordInt(record, COL_ITEM_ID);
        int type = ReadRecordInt(record, COL_ITEM_TYPE);
        int level = ReadRecordInt(record, COL_ITEM_LEVEL);

        if (id < 0 || level != item_level)
            continue;
        if (!IsSupportedItemType(type))
            continue;
        if (type != item_type)
            continue;

        candidates[candidate_count].item_id = id;
        candidates[candidate_count].count = CountExistingItem(id);
        ++candidate_count;
    }

    return candidate_count;
}

static int PickBiasedCandidate(DropCandidate *candidates, int candidate_count, int original_item_id)
{
    int i;
    int max_count = 0;
    int total_weight = 0;
    int roll;

    if (!candidates || candidate_count <= 1)
        return original_item_id;

    for (i = 0; i < candidate_count; ++i)
    {
        if (candidates[i].count > max_count)
            max_count = candidates[i].count;
    }

    for (i = 0; i < candidate_count; ++i)
        total_weight += (max_count - candidates[i].count) + 1;

    if (total_weight <= 0)
        return original_item_id;

    roll = NextLocalRandom(total_weight);
    for (i = 0; i < candidate_count; ++i)
    {
        int weight = (max_count - candidates[i].count) + 1;
        if (roll < weight)
            return candidates[i].item_id;
        roll -= weight;
    }

    return original_item_id;
}

static void NoteRecentDrop(int item_id)
{
    if (item_id < 0 || item_id >= MAX_ITEM_ID_TRACK)
        return;
    if (g_recent_drop_count[item_id] < RECENT_COUNT_CAP)
        ++g_recent_drop_count[item_id];
}

static int ApplyDropBias(int original_item_id)
{
    DWORD record;
    int item_type;
    int item_level;
    DropCandidate candidates[MAX_CANDIDATES];
    int candidate_count;
    int selected_item_id;

    record = FindPropRecordById(original_item_id);
    if (!record)
        return original_item_id;

    item_type = ReadRecordInt(record, COL_ITEM_TYPE);
    item_level = ReadRecordInt(record, COL_ITEM_LEVEL);
    if (!IsSupportedItemType(item_type))
        return original_item_id;

    candidate_count = CollectCandidates(item_type, item_level, candidates, MAX_CANDIDATES);
    selected_item_id = PickBiasedCandidate(candidates, candidate_count, original_item_id);
    NoteRecentDrop(selected_item_id);

    return selected_item_id;
}

int __cdecl Detour_SelectDropItem(int level, int type_hint)
{
    void *return_address = _ReturnAddress();
    int original_item_id;

    if (!fpSelectDropItem)
        return -1;

    original_item_id = fpSelectDropItem(level, type_hint);

    if (!g_pk_config.enable_drop_bias)
        return original_item_id;
    if (!IsDropSelectReturn(return_address))
        return original_item_id;
    if (original_item_id < 0 || original_item_id == 0xFFFF)
        return original_item_id;

    return ApplyDropBias(original_item_id);
}

void DropBias_ResetRecent(void)
{
    memset(g_recent_drop_count, 0, sizeof(g_recent_drop_count));
}

static int InitVersionConfig(int game_version, DropBiasVersionConfig *config)
{
    memset(config, 0, sizeof(*config));

    if (game_version == VER_105)
    {
        config->select_drop_item = ADDR_105_SELECT_DROP_ITEM;
        config->char_ptr = ADDR_105_CHAR_PTR;
        config->propmdl_table = ADDR_105_PROPMDL_TABLE;
        config->returns[0] = RET_105_DROP_SELECT_1;
        config->returns[1] = RET_105_DROP_SELECT_2;
        config->returns[2] = RET_105_DROP_SELECT_3;
        config->returns[3] = RET_105_DROP_SELECT_4;
        return 1;
    }

    if (game_version == VER_201)
    {
        config->select_drop_item = ADDR_201_SELECT_DROP_ITEM;
        config->char_ptr = ADDR_201_CHAR_PTR;
        config->propmdl_table = ADDR_201_PROPMDL_TABLE;
        config->returns[0] = RET_201_DROP_SELECT_1;
        config->returns[1] = RET_201_DROP_SELECT_2;
        config->returns[2] = RET_201_DROP_SELECT_3;
        config->returns[3] = RET_201_DROP_SELECT_4;
        return 1;
    }

    return 0;
}

void Mod_Drop_Bias_Init(int game_version)
{
    if (!InitVersionConfig(game_version, &g_drop_bias_config))
        return;
    if (!g_pk_config.enable_drop_bias)
        return;

    MH_CreateHook((LPVOID)g_drop_bias_config.select_drop_item,
                  &Detour_SelectDropItem,
                  (LPVOID *)&fpSelectDropItem);
    MH_EnableHook((LPVOID)g_drop_bias_config.select_drop_item);
}
