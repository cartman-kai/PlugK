#include "pch.h"
#include "auto_pickup.h"
#include "show_tips.h"
#include <MinHook.h>
#include <intrin.h>
#include <stdio.h>

#pragma intrinsic(_ReturnAddress)

#define VER_105 105
#define VER_201 201

#define ADDR_105_ACTION_ENTRY 0x0041C270
#define ADDR_105_PICKUP_ENTRY 0x00452730
#define ADDR_105_GROUND_MGR_PTR 0x00558E4C
#define ADDR_105_ACTION_SLOT_TABLE_PTR 0x00558E50
#define VTABLE_105_Z_ACTION 0x00528CD4
#define VTABLE_105_GROUND_ITEM 0x005288F8
#define RET_105_Z_PICKUP_CALL 0x0041C892

#define ADDR_201_ACTION_ENTRY 0x004247F0
#define ADDR_201_PICKUP_ENTRY 0x0045E8B0
#define ADDR_201_GROUND_MGR_PTR 0x00589E34
#define ADDR_201_ACTION_SLOT_TABLE_PTR 0x00589E38
#define VTABLE_201_Z_ACTION 0x00551E2C
#define VTABLE_201_GROUND_ITEM 0x005519FC
#define RET_201_Z_PICKUP_CALL 0x00424E64

#define GROUND_ITEM_TYPE_OFFSET 0x67
#define GROUND_ITEM_RECORD_OFFSET 0x81
#define ACTION_GROUND_MGR_OFFSET 0x04
#define ACTION_SLOT_SIZE 6
#define ACTION_SLOT_MAX_COUNT 0x8000
#define AUTO_PICKUP_INTERVAL_MS 100

#define COL_ITEM_ID 1
#define COL_ITEM_TYPE 2

#define MAX_AUTO_PICKUP_ITEM_NAME 128

typedef int(__fastcall *tActionEntry)(void *pThis, void *_edx, int action, int a3, int a4, int a5, int a6, int a7);
typedef int(__cdecl *tPickupEntry)(int ground_item, int action_this);

typedef enum AutoPickupMode
{
    AUTO_PICKUP_MONEY = 0,
    AUTO_PICKUP_MONEY_GEMS = 1,
    AUTO_PICKUP_MONEY_GEMS_CHARMS_RECOVERY = 2,
    AUTO_PICKUP_ALL = 3,
    AUTO_PICKUP_MODE_COUNT = 4
} AutoPickupMode;

typedef struct AutoPickupConfig
{
    DWORD action_entry;
    DWORD pickup_entry;
    DWORD ground_mgr_ptr;
    DWORD action_slot_table_ptr;
    DWORD z_action_vtable;
    DWORD ground_item_vtable;
    DWORD z_pickup_return;
} AutoPickupConfig;

static AutoPickupConfig g_auto_pickup_config;
static tActionEntry fpActionEntry = NULL;
static tPickupEntry fpPickupEntry = NULL;
static int g_auto_pickup_supported = 0;
static int g_auto_pickup_enabled = 0;
static AutoPickupMode g_auto_pickup_mode = AUTO_PICKUP_ALL;
static DWORD g_last_pickup_tick = 0;
static DWORD g_auto_pickup_thread_id = 0;
static DWORD g_cached_z_action = 0;
static int g_auto_pickup_context_depth = 0;

static const char *GetModeName(void)
{
    switch (g_auto_pickup_mode)
    {
    case AUTO_PICKUP_MONEY:
        return "仅金钱";
    case AUTO_PICKUP_MONEY_GEMS:
        return "金钱+宝石";
    case AUTO_PICKUP_MONEY_GEMS_CHARMS_RECOVERY:
        return "金钱+宝石+护身石+回复道具";
    case AUTO_PICKUP_ALL:
    default:
        return "全部物品";
    }
}

static void SendAutoPickupStateTip(const char *state)
{
    char text[128];
    snprintf(text, sizeof(text), "[自动拾取] %s：%s", state, GetModeName());
    SendGameTips(text);
}

static BOOL GbkToUtf8(const char *gbk_text, char *out_utf8, int out_size)
{
    wchar_t wide_text[MAX_AUTO_PICKUP_ITEM_NAME];
    int wide_len;

    if (gbk_text == NULL || out_utf8 == NULL || out_size <= 0)
        return FALSE;

    out_utf8[0] = '\0';
    wide_len = MultiByteToWideChar(936, 0, gbk_text, -1, wide_text, MAX_AUTO_PICKUP_ITEM_NAME);
    if (wide_len <= 0)
        return FALSE;

    return WideCharToMultiByte(CP_UTF8, 0, wide_text, -1, out_utf8, out_size, NULL, NULL) > 0;
}

static int ReadRecordInt(DWORD record, int column)
{
    int count;
    DWORD values;

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

static int IsGemType(int item_type)
{
    return item_type == 30 || item_type == 35;
}

static int IsCharmType(int item_type)
{
    return item_type == 50;
}

static int ShouldPickupItemType(int item_type)
{
    if (g_auto_pickup_mode == AUTO_PICKUP_ALL)
        return 1;

    if (item_type == 0)
        return 1;

    if (g_auto_pickup_mode >= AUTO_PICKUP_MONEY_GEMS && IsGemType(item_type))
        return 1;

    if (g_auto_pickup_mode >= AUTO_PICKUP_MONEY_GEMS_CHARMS_RECOVERY &&
        (IsCharmType(item_type) || item_type == 10))
    {
        return 1;
    }

    return 0;
}

static int ShouldPickupGroundItem(int ground_item)
{
    DWORD record;
    int item_id;
    int item_type;

    if (g_auto_pickup_mode == AUTO_PICKUP_ALL)
        return 1;

    if (ground_item == 0 || IsBadReadPtr((void *)ground_item, GROUND_ITEM_RECORD_OFFSET + sizeof(DWORD)))
        return 0;

    if (*(DWORD *)ground_item != g_auto_pickup_config.ground_item_vtable)
        return 0;
    if (*(DWORD *)(ground_item + GROUND_ITEM_TYPE_OFFSET) != 0x17)
        return 0;

    record = *(DWORD *)(ground_item + GROUND_ITEM_RECORD_OFFSET);
    item_id = ReadRecordInt(record, COL_ITEM_ID);
    item_type = ReadRecordInt(record, COL_ITEM_TYPE);
    if (item_id == 0xFFFF || item_type == 0xFFFF)
        return 0;

    return ShouldPickupItemType(item_type);
}

static BOOL GetGroundItemPickupInfo(int ground_item, char *out_name, int out_name_size, DWORD *out_count)
{
    DWORD record;
    DWORD text_ptrs;
    const char *gbk_name;

    if (out_name == NULL || out_name_size <= 0 || out_count == NULL)
        return FALSE;

    out_name[0] = '\0';
    *out_count = 0;

    if (ground_item == 0 || IsBadReadPtr((void *)ground_item, GROUND_ITEM_RECORD_OFFSET + sizeof(DWORD)))
        return FALSE;

    if (*(DWORD *)ground_item != g_auto_pickup_config.ground_item_vtable)
        return FALSE;
    if (*(DWORD *)(ground_item + GROUND_ITEM_TYPE_OFFSET) != 0x17)
        return FALSE;

    __try
    {
        record = *(DWORD *)(ground_item + GROUND_ITEM_RECORD_OFFSET);
        if (record == 0 || IsBadReadPtr((void *)record, 0x10))
            return FALSE;

        text_ptrs = *(DWORD *)(record + 0x0C);
        if (text_ptrs == 0 || IsBadReadPtr((void *)text_ptrs, sizeof(DWORD)))
            return FALSE;

        gbk_name = *(const char **)text_ptrs;
        if (gbk_name == NULL || IsBadReadPtr(gbk_name, 1) || gbk_name[0] == '\0')
            return FALSE;

        *out_count = *(DWORD *)(ground_item + 0x85);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        out_name[0] = '\0';
        *out_count = 0;
        return FALSE;
    }

    return GbkToUtf8(gbk_name, out_name, out_name_size);
}

static void SendAutoPickupGotTip(const char *item_name, DWORD count)
{
    char text[192];

    if (item_name == NULL || item_name[0] == '\0')
        return;

    snprintf(text, sizeof(text), "[自动拾取] 获得：%s 数量 %lu", item_name, (unsigned long)count);
    SendGameTips(text);
}

static DWORD ReadGlobalDword(DWORD address)
{
    if (address == 0 || IsBadReadPtr((void *)address, sizeof(DWORD)))
        return 0;

    return *(DWORD *)address;
}

static int GetReadableSlotCount(DWORD table)
{
    MEMORY_BASIC_INFORMATION mbi;
    DWORD region_end;
    DWORD readable_bytes;
    int slot_count;

    if (!VirtualQuery((void *)table, &mbi, sizeof(mbi)))
        return 0;
    if (mbi.State != MEM_COMMIT)
        return 0;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))
        return 0;

    region_end = (DWORD)mbi.BaseAddress + (DWORD)mbi.RegionSize;
    if (region_end <= table)
        return 0;

    readable_bytes = region_end - table;
    slot_count = (int)(readable_bytes / ACTION_SLOT_SIZE);
    if (slot_count > ACTION_SLOT_MAX_COUNT)
        slot_count = ACTION_SLOT_MAX_COUNT;
    return slot_count;
}

static void *FindZActionObject(void)
{
    DWORD ground_mgr;
    DWORD table;
    void *found = NULL;
    int found_count = 0;
    int slot_count;
    int i;

    ground_mgr = ReadGlobalDword(g_auto_pickup_config.ground_mgr_ptr);
    table = ReadGlobalDword(g_auto_pickup_config.action_slot_table_ptr);
    if (ground_mgr == 0 || table == 0)
        return NULL;

    if (g_cached_z_action != 0 &&
        !IsBadReadPtr((void *)g_cached_z_action, ACTION_GROUND_MGR_OFFSET + sizeof(DWORD)) &&
        *(DWORD *)g_cached_z_action == g_auto_pickup_config.z_action_vtable &&
        *(DWORD *)(g_cached_z_action + ACTION_GROUND_MGR_OFFSET) == ground_mgr)
    {
        return (void *)g_cached_z_action;
    }

    g_cached_z_action = 0;
    slot_count = GetReadableSlotCount(table);
    if (slot_count <= 0)
        return NULL;

    for (i = 0; i < slot_count; ++i)
    {
        DWORD slot = table + i * ACTION_SLOT_SIZE;
        DWORD candidate = *(DWORD *)(slot + 2);

        if (candidate == 0)
            continue;
        if (IsBadReadPtr((void *)candidate, ACTION_GROUND_MGR_OFFSET + sizeof(DWORD)))
            continue;
        if (*(DWORD *)candidate != g_auto_pickup_config.z_action_vtable)
            continue;
        if (*(DWORD *)(candidate + ACTION_GROUND_MGR_OFFSET) != ground_mgr)
            continue;

        found = (void *)candidate;
        ++found_count;
        if (found_count > 1)
            return NULL;
    }

    g_cached_z_action = (DWORD)found;
    return found;
}

static void ExecuteAutoPickup(void)
{
    void *z_action;

    if (!g_auto_pickup_supported || !g_auto_pickup_enabled || !fpActionEntry)
        return;

    z_action = FindZActionObject();
    if (!z_action)
        return;

    ++g_auto_pickup_context_depth;
    g_auto_pickup_thread_id = GetCurrentThreadId();
    fpActionEntry(z_action, NULL, 22, 0, 0, 0, 1, 1);
    --g_auto_pickup_context_depth;
    if (g_auto_pickup_context_depth <= 0)
    {
        g_auto_pickup_context_depth = 0;
        g_auto_pickup_thread_id = 0;
    }
}

static int IsAutoPickupContext(void)
{
    return g_auto_pickup_context_depth > 0 &&
           g_auto_pickup_thread_id == GetCurrentThreadId();
}

static int LoadAutoPickupConfig(int game_version, AutoPickupConfig *config)
{
    memset(config, 0, sizeof(*config));

    if (game_version == VER_105)
    {
        config->action_entry = ADDR_105_ACTION_ENTRY;
        config->pickup_entry = ADDR_105_PICKUP_ENTRY;
        config->ground_mgr_ptr = ADDR_105_GROUND_MGR_PTR;
        config->action_slot_table_ptr = ADDR_105_ACTION_SLOT_TABLE_PTR;
        config->z_action_vtable = VTABLE_105_Z_ACTION;
        config->ground_item_vtable = VTABLE_105_GROUND_ITEM;
        config->z_pickup_return = RET_105_Z_PICKUP_CALL;
        return 1;
    }

    if (game_version == VER_201)
    {
        config->action_entry = ADDR_201_ACTION_ENTRY;
        config->pickup_entry = ADDR_201_PICKUP_ENTRY;
        config->ground_mgr_ptr = ADDR_201_GROUND_MGR_PTR;
        config->action_slot_table_ptr = ADDR_201_ACTION_SLOT_TABLE_PTR;
        config->z_action_vtable = VTABLE_201_Z_ACTION;
        config->ground_item_vtable = VTABLE_201_GROUND_ITEM;
        config->z_pickup_return = RET_201_Z_PICKUP_CALL;
        return 1;
    }

    return 0;
}

static int __cdecl Detour_PickupEntry(int ground_item, int action_this)
{
    void *return_address = _ReturnAddress();
    int is_auto_z_pickup;
    char item_name[MAX_AUTO_PICKUP_ITEM_NAME];
    DWORD item_count = 0;
    BOOL has_pickup_info = FALSE;
    int result;

    if (!fpPickupEntry)
        return 1;

    is_auto_z_pickup = IsAutoPickupContext() &&
                       (DWORD)return_address == g_auto_pickup_config.z_pickup_return;

    if (is_auto_z_pickup && !ShouldPickupGroundItem(ground_item))
    {
        return 1;
    }

    if (is_auto_z_pickup)
        has_pickup_info = GetGroundItemPickupInfo(ground_item, item_name, sizeof(item_name), &item_count);

    result = fpPickupEntry(ground_item, action_this);
    if (is_auto_z_pickup && has_pickup_info)
        SendAutoPickupGotTip(item_name, item_count);

    return result;
}

void AutoPickup_OnInputFrame(void)
{
    DWORD now;

    if (!g_auto_pickup_supported || !g_auto_pickup_enabled)
        return;

    now = GetTickCount();
    if (g_last_pickup_tick != 0 && now - g_last_pickup_tick < AUTO_PICKUP_INTERVAL_MS)
        return;

    g_last_pickup_tick = now;
    ExecuteAutoPickup();
}

void AutoPickup_Toggle(void)
{
    if (!g_auto_pickup_supported)
    {
        SendGameTips("[自动拾取] 当前版本暂不支持");
        return;
    }

    g_auto_pickup_enabled = !g_auto_pickup_enabled;
    g_last_pickup_tick = 0;
    g_cached_z_action = 0;
    SendAutoPickupStateTip(g_auto_pickup_enabled ? "已开启" : "已关闭");
}

void AutoPickup_CycleMode(void)
{
    if (!g_auto_pickup_supported)
    {
        SendGameTips("[自动拾取] 当前版本暂不支持");
        return;
    }

    g_auto_pickup_mode = (AutoPickupMode)((g_auto_pickup_mode + 1) % AUTO_PICKUP_MODE_COUNT);
    SendAutoPickupStateTip("已切换");
}

BOOL AutoPickup_ShouldBlockZHotkey(void)
{
    return (GetAsyncKeyState('Z') & 0x8000) &&
           ((GetAsyncKeyState(VK_CONTROL) & 0x8000) ||
            (GetAsyncKeyState(VK_SHIFT) & 0x8000));
}

void Mod_Auto_Pickup_Init(int game_version)
{
    MH_STATUS status;

    memset(&g_auto_pickup_config, 0, sizeof(g_auto_pickup_config));
    g_auto_pickup_enabled = 0;
    g_auto_pickup_mode = AUTO_PICKUP_ALL;
    g_last_pickup_tick = 0;
    g_cached_z_action = 0;
    g_auto_pickup_supported = 0;

    if (!LoadAutoPickupConfig(game_version, &g_auto_pickup_config))
        return;

    fpActionEntry = (tActionEntry)g_auto_pickup_config.action_entry;
    status = MH_CreateHook((LPVOID)g_auto_pickup_config.pickup_entry,
                           &Detour_PickupEntry,
                           (LPVOID *)&fpPickupEntry);
    if (status != MH_OK)
        return;

    if (MH_EnableHook((LPVOID)g_auto_pickup_config.pickup_entry) != MH_OK)
        return;

    g_auto_pickup_supported = 1;
}
