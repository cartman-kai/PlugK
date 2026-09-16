#include "pch.h"
#include "config.h"
#include "enemy_name.h"
#include "item.h"
#include <MinHook.h>
#include <stdio.h>
#include <string.h>

typedef int(__fastcall *tEnemyNameDraw)(void *pThis, void *_edx, int surface, int role_id, int x, int y);
typedef void *(__cdecl *tGetRoleObject)(int role_id);
typedef int(__fastcall *tGetMaxHp)(void *pThis, void *_edx);
// 游戏原函数是 thiscall；C 项目用 fastcall 的 ECX/EDX 形式保留 this 与栈参数布局。
typedef int(__fastcall *tEnemyHealthSegments)(void *pThis, void *_edx, int *percent, int *segments);
typedef void *(__fastcall *tEnemyResourceEntry)(void *pThis, void *_edx, int index);
typedef void *(__fastcall *tEnemyResourceResolve)(void *pThis, void *_edx);
typedef const char *(__fastcall *tEnemyNameGet)(void *pThis, void *_edx, int index);
typedef int(__fastcall *tEnemyProject)(void *pThis, void *_edx);
typedef int(__fastcall *tEnemySpriteDraw)(void *pThis, void *_edx, int surface, int x, int y, int width, int height, int source_x, int source_y, int clip_x, int clip_y, int flags);
typedef void *(__fastcall *tEnemyResolveThis)(void *pThis, void *_edx);

static tEnemyNameDraw fpEnemyNameDraw = NULL;
static tGetRoleObject fpGetRoleObject = NULL;
static tEnemyHealthSegments fpEnemyHealthSegments = NULL;
static tEnemyResourceEntry fpEnemyResourceEntry = NULL;
static tEnemyResourceResolve fpEnemyResourceResolve = NULL;
static tEnemyNameGet fpEnemyNameGet = NULL;
static tEnemyResolveThis fpEnemyPlayerResolve = NULL;
static tEnemyResolveThis fpEnemyLockResolve = NULL;
static void *g_target_enemy_name_draw = NULL;
static void *g_enemy_name_return = NULL;
static void *g_enemy_game_global = NULL;
static void *g_enemy_mouse_global = NULL;
static void *g_current_enemy_obj = NULL;
static int g_current_hp_offset = 0;

#define ENEMY_NAME_DRAW_RETURN_105 ((void *)0x004B0BF1)
#define ENEMY_NAME_DRAW_FUNC_105 ((void *)0x004B0A60)
#define ENEMY_NAME_GET_ROLE_OBJECT_105 ((tGetRoleObject)0x00420D50)
#define ENEMY_NAME_DRAW_RETURN_201 ((void *)0x004C3DE1)
#define ENEMY_NAME_DRAW_FUNC_201 ((void *)0x004C3C50)
#define ENEMY_NAME_GET_ROLE_OBJECT_201 ((tGetRoleObject)0x00429340)
#define ENEMY_NAME_HP_TEXT_Y_OFFSET 14
#define ENEMY_NAME_CURRENT_HP_OFFSET_105 0x3A2
#define ENEMY_NAME_CURRENT_HP_OFFSET_201 0x3AE
#define ENEMY_NAME_MAX_HP_VT_OFFSET 0x78
#define ENEMY_NAME_RECORD_OFFSET 0x18B
#define ENEMY_NAME_OBJECT_SCREEN_X_OFFSET 0x48
#define ENEMY_NAME_OBJECT_SCREEN_Y_OFFSET 0x4C
#define ENEMY_NAME_OBJECT_BOUNDS_LEFT_OFFSET 0x34
#define ENEMY_NAME_OBJECT_BOUNDS_TOP_OFFSET 0x38
#define ENEMY_NAME_OBJECT_BOUNDS_WIDTH_OFFSET 0x3C
#define ENEMY_NAME_DRAW_CONTEXT_BAR_RESOURCE_INDEX_OFFSET 0x110
#define ENEMY_NAME_DRAW_CONTEXT_BAR_RESOURCE_BASE_OFFSET 0x118
#define ENEMY_NAME_DRAW_CONTEXT_CLIP_X_OFFSET 0x58
#define ENEMY_NAME_DRAW_CONTEXT_CLIP_Y_OFFSET 0x5C
#define ENEMY_NAME_PROJECT_VT_OFFSET 0x1C
#define ENEMY_NAME_GAME_PLAYER_OFFSET 0x30
#define ENEMY_NAME_MOUSE_TARGET_OFFSET 0x3C
#define ENEMY_NAME_TOP_TEXT_HEIGHT 14
#define ENEMY_NAME_TOP_TEXT_GAP 2
#define ENEMY_NAME_TOP_TEXT_FALLBACK_OFFSET 24
#define ENEMY_NAME_BOTTOM_BAR_GAP 4
#define ENEMY_NAME_BOTTOM_TEXT_GAP 1

#define ENEMY_NAME_GAME_GLOBAL_105 ((void *)0x005585C4)
#define ENEMY_NAME_MOUSE_GLOBAL_105 ((void *)0x00557D1C)
#define ENEMY_NAME_GET_PLAYER_105 ((tEnemyResolveThis)0x00477790)
#define ENEMY_NAME_GET_LOCK_105 ((tEnemyResolveThis)0x0042B280)
#define ENEMY_NAME_GAME_GLOBAL_201 ((void *)0x00589544)
#define ENEMY_NAME_MOUSE_GLOBAL_201 ((void *)0x00588BA8)
#define ENEMY_NAME_GET_PLAYER_201 ((tEnemyResolveThis)0x004861A0)
#define ENEMY_NAME_GET_LOCK_201 ((tEnemyResolveThis)0x00434350)

static int can_read(void *address, size_t size)
{
    return address && !IsBadReadPtr(address, size);
}

static int read_enemy_int(void *base, int offset, int *value)
{
    BYTE *address;

    if (!base || !value)
        return 0;

    address = (BYTE *)base + offset;
    if (!can_read(address, sizeof(*value)))
        return 0;

    *value = *(int *)address;
    return 1;
}

static int read_enemy_pointer(void *base, int offset, void **value)
{
    BYTE *address;

    if (!base || !value)
        return 0;

    address = (BYTE *)base + offset;
    if (!can_read(address, sizeof(*value)))
        return 0;

    *value = *(void **)address;
    return 1;
}

static int is_enemy_name_draw_call(void *caller)
{
    return caller == g_enemy_name_return;
}

static int should_append_probe_line(const char *text, void *caller)
{
    if (!text || !text[0])
        return 0;

    if (strchr(text, '\n') || strchr(text, '\r'))
        return 0;

    return is_enemy_name_draw_call(caller);
}

static int get_enemy_current_hp(void *obj)
{
    float hp;

    if (!obj)
        return 0;

    if (g_current_hp_offset <= 0)
        return 0;

    if (!can_read((BYTE *)obj + g_current_hp_offset, sizeof(hp)))
        return 0;

    hp = *(float *)((char *)obj + g_current_hp_offset);
    if (hp < 0.0f)
        hp = 0.0f;

    return (int)(hp + 0.5f);
}

static int get_enemy_max_hp(void *obj)
{
    void **vtable;
    tGetMaxHp get_max_hp;

    if (!obj)
        return 0;

    if (!can_read(obj, sizeof(void *)))
        return 0;

    vtable = *(void ***)obj;
    if (!vtable || !can_read(vtable + ENEMY_NAME_MAX_HP_VT_OFFSET / sizeof(void *), sizeof(void *)))
        return 0;

    get_max_hp = (tGetMaxHp)vtable[ENEMY_NAME_MAX_HP_VT_OFFSET / sizeof(void *)];
    if (!get_max_hp)
        return 0;

    return get_max_hp(obj, NULL);
}

static void *get_enemy_hover_target(void)
{
    void *mouse_manager = NULL;
    void *target = NULL;

    if (!g_enemy_mouse_global)
        return NULL;

    if (!read_enemy_pointer(g_enemy_mouse_global, 0, &mouse_manager) || !mouse_manager)
        return NULL;

    if (!read_enemy_pointer(mouse_manager, ENEMY_NAME_MOUSE_TARGET_OFFSET, &target))
        return NULL;

    return target;
}

static void *get_enemy_locked_target(void)
{
    void *game_manager = NULL;
    void *player_manager = NULL;
    void *player_obj;

    if (!g_enemy_game_global || !fpEnemyPlayerResolve || !fpEnemyLockResolve)
        return NULL;

    if (!read_enemy_pointer(g_enemy_game_global, 0, &game_manager) || !game_manager)
        return NULL;

    if (!read_enemy_pointer(game_manager, ENEMY_NAME_GAME_PLAYER_OFFSET, &player_manager) || !player_manager)
        return NULL;

    player_obj = fpEnemyPlayerResolve(player_manager, NULL);
    if (!player_obj || !can_read((BYTE *)player_obj + 323, sizeof(DWORD)))
        return NULL;

    return fpEnemyLockResolve(player_obj, NULL);
}

static int should_draw_enemy_name(void *enemy_obj)
{
    if (!enemy_obj)
        return 0;

    return enemy_obj == get_enemy_hover_target() || enemy_obj == get_enemy_locked_target();
}

static void *get_enemy_resource_sprite(void *draw_context, int slot)
{
    void *resource_base = NULL;
    void *resource_entry;
    int resource_index;

    if (!draw_context || !fpEnemyResourceEntry || !fpEnemyResourceResolve)
        return NULL;

    if (!read_enemy_pointer(draw_context, ENEMY_NAME_DRAW_CONTEXT_BAR_RESOURCE_BASE_OFFSET, &resource_base))
        return NULL;

    if (!read_enemy_int(draw_context, ENEMY_NAME_DRAW_CONTEXT_BAR_RESOURCE_INDEX_OFFSET, &resource_index))
        return NULL;

    if (!resource_base || resource_index < 0 || resource_index > 0x100000)
        return NULL;

    resource_base = (BYTE *)resource_base + 32 * resource_index;
    if (!can_read(resource_base, sizeof(DWORD) * 4))
        return NULL;

    resource_entry = fpEnemyResourceEntry(resource_base, NULL, slot);
    if (!resource_entry)
        return NULL;

    return fpEnemyResourceResolve(resource_entry, NULL);
}

static const char *get_enemy_name(void *enemy_obj)
{
    void *record = NULL;

    if (!enemy_obj || !fpEnemyNameGet)
        return NULL;

    if (!read_enemy_pointer(enemy_obj, ENEMY_NAME_RECORD_OFFSET, &record) || !record)
        return NULL;

    return fpEnemyNameGet(record, NULL, 0);
}

static int get_enemy_screen_position(void *enemy_obj, int *screen_x, int *screen_y, int *name_x, int *name_y)
{
    void **vtable;
    tEnemyProject project;
    int bounds_left;
    int bounds_top;
    int bounds_width;

    if (!enemy_obj || !screen_x || !screen_y || !name_x || !name_y || !can_read(enemy_obj, sizeof(void *)))
        return 0;

    vtable = *(void ***)enemy_obj;
    if (vtable && can_read(vtable + ENEMY_NAME_PROJECT_VT_OFFSET / sizeof(void *), sizeof(void *)))
    {
        project = (tEnemyProject)vtable[ENEMY_NAME_PROJECT_VT_OFFSET / sizeof(void *)];
        if (project)
            project(enemy_obj, NULL);
    }

    if (!read_enemy_int(enemy_obj, ENEMY_NAME_OBJECT_SCREEN_X_OFFSET, screen_x))
        return 0;

    if (!read_enemy_int(enemy_obj, ENEMY_NAME_OBJECT_SCREEN_Y_OFFSET, screen_y))
        return 0;

    *name_x = *screen_x;
    *name_y = *screen_y - ENEMY_NAME_TOP_TEXT_FALLBACK_OFFSET;
    if (read_enemy_int(enemy_obj, ENEMY_NAME_OBJECT_BOUNDS_LEFT_OFFSET, &bounds_left)
        && read_enemy_int(enemy_obj, ENEMY_NAME_OBJECT_BOUNDS_TOP_OFFSET, &bounds_top)
        && read_enemy_int(enemy_obj, ENEMY_NAME_OBJECT_BOUNDS_WIDTH_OFFSET, &bounds_width)
        && bounds_width > 0)
    {
        *name_x = bounds_left + bounds_width / 2;
        *name_y = bounds_top - ENEMY_NAME_TOP_TEXT_HEIGHT - ENEMY_NAME_TOP_TEXT_GAP;
    }

    return 1;
}

static int get_enemy_sprite_dimension(void *sprite, int offset, int fallback)
{
    int value;

    if (!read_enemy_int(sprite, offset, &value) || value <= 0)
        return fallback;

    return value;
}

static void draw_enemy_sprite(void *draw_context, void *sprite, int surface, int x, int y, int width, int height, int source_x)
{
    void **vtable;
    tEnemySpriteDraw draw;
    int clip_x = 0;
    int clip_y = 0;

    if (!draw_context || !sprite || width <= 0 || height <= 0 || !can_read(sprite, sizeof(void *)))
        return;

    vtable = *(void ***)sprite;
    if (!vtable || !can_read(vtable + 0x0C / sizeof(void *), sizeof(void *)))
        return;

    draw = (tEnemySpriteDraw)vtable[0x0C / sizeof(void *)];
    if (!draw)
        return;

    read_enemy_int(draw_context, ENEMY_NAME_DRAW_CONTEXT_CLIP_X_OFFSET, &clip_x);
    read_enemy_int(draw_context, ENEMY_NAME_DRAW_CONTEXT_CLIP_Y_OFFSET, &clip_y);
    draw(sprite, NULL, surface, x, y, width, height, source_x, 0, clip_x, clip_y, 0);
}

static void draw_enemy_bottom_bar(void *draw_context, int surface, void *enemy_obj, int screen_x, int screen_y, int *bar_height)
{
    void *filled_sprite;
    void *empty_sprite;
    int percent = 100;
    int segments = 1;
    int filled_width;
    int filled_sprite_width;
    int empty_sprite_width;
    int bar_width;
    int narrow_width;
    int filled_height;
    int empty_height;
    int height;
    int bar_x;
    int bar_y;

    if (!bar_height)
        return;

    *bar_height = 0;
    if (!enemy_obj || !fpEnemyHealthSegments)
        return;

    fpEnemyHealthSegments(enemy_obj, NULL, &percent, &segments);
    if (percent < 0)
        percent = 0;
    if (percent > 100)
        percent = 100;

    // 原版允许 segments=0（空血或第一段血量），对应资源槽 1/0。
    if (segments < 0)
        segments = 0;
    if (segments > 5)
        segments = 5;

    // 保留原版按分段选择颜色和按段内百分比绘制的逻辑。
    filled_sprite = get_enemy_resource_sprite(draw_context, segments + 1);
    empty_sprite = get_enemy_resource_sprite(draw_context, segments);
    if (!filled_sprite || !empty_sprite)
        return;

    filled_sprite_width = get_enemy_sprite_dimension(filled_sprite, 0x0C, 0);
    empty_sprite_width = get_enemy_sprite_dimension(empty_sprite, 0x0C, 0);
    filled_height = get_enemy_sprite_dimension(filled_sprite, 0x10, 0);
    empty_height = get_enemy_sprite_dimension(empty_sprite, 0x10, 0);

    bar_width = empty_sprite_width > 0 ? empty_sprite_width : filled_sprite_width;
    narrow_width = bar_width / 2;
    if (narrow_width < 1)
        narrow_width = 1;

    height = filled_height > 0 ? filled_height : empty_height;
    if (height <= 0)
        return;

    height /= 2;
    if (height < 1)
        height = 1;

    filled_width = narrow_width * percent / 100;
    bar_x = screen_x - narrow_width / 2;
    bar_y = screen_y + ENEMY_NAME_BOTTOM_BAR_GAP;

    if (filled_width > 0)
        draw_enemy_sprite(draw_context, filled_sprite, surface, bar_x, bar_y, filled_width, height, 0);

    if (filled_width < narrow_width)
        draw_enemy_sprite(draw_context, empty_sprite, surface, bar_x + filled_width, bar_y, narrow_width - filled_width, height, filled_width);

    *bar_height = height;
}

static int draw_enemy_bottom(void *draw_context, int surface, void *enemy_obj)
{
    const char *name;
    char hp_text[64];
    int screen_x;
    int screen_y;
    int name_x;
    int name_y;
    int bar_height = 0;
    int text_x;
    int bar_y;
    int hp_y;
    int max_hp;
    int current_hp;

    if (!draw_context || !enemy_obj)
        return 0;

    if (!get_enemy_screen_position(enemy_obj, &screen_x, &screen_y, &name_x, &name_y))
        return 0;

    draw_enemy_bottom_bar(draw_context, surface, enemy_obj, screen_x, screen_y, &bar_height);

    name = get_enemy_name(enemy_obj);
    text_x = screen_x;
    bar_y = screen_y + ENEMY_NAME_BOTTOM_BAR_GAP;
    hp_y = bar_height > 0 ? bar_y + bar_height + ENEMY_NAME_BOTTOM_TEXT_GAP : bar_y + ENEMY_NAME_HP_TEXT_Y_OFFSET;
    if (should_draw_enemy_name(enemy_obj) && name && can_read((void *)name, 1) && name[0])
        Item_DrawText(draw_context, NULL, surface, name_x, name_y, name, 1);

    if (g_pk_config.show_enemy_hp)
    {
        current_hp = get_enemy_current_hp(enemy_obj);
        max_hp = get_enemy_max_hp(enemy_obj);
        if (max_hp > 0)
        {
            _snprintf_s(hp_text, sizeof(hp_text), _TRUNCATE, "HP: %d/%d", current_hp, max_hp);
            Item_DrawText(draw_context, NULL, surface, text_x, hp_y, hp_text, 1);
        }
    }

    return 1;
}

void EnemyName_AfterDrawText(void *caller, PK_DrawTextFn draw_text, void *pThis, void *_edx, int surface, int x, int y, const char *text, int mode)
{
    char hp_text[64];
    int current_hp;
    int max_hp;

    if (!draw_text)
        return;

    if (!g_pk_config.show_enemy_hp)
        return;

    if (!should_append_probe_line(text, caller))
        return;

    current_hp = get_enemy_current_hp(g_current_enemy_obj);
    max_hp = get_enemy_max_hp(g_current_enemy_obj);

    if (max_hp > 0)
    {
        _snprintf_s(hp_text, sizeof(hp_text), _TRUNCATE, "HP: %d/%d", current_hp, max_hp);
        draw_text(pThis, _edx, surface, x, y + ENEMY_NAME_HP_TEXT_Y_OFFSET, hp_text, mode);
    }
}

int __fastcall Detour_EnemyNameDraw(void *pThis, void *_edx, int surface, int role_id, int x, int y)
{
    void *previous_enemy_obj = g_current_enemy_obj;
    int result;

    g_current_enemy_obj = fpGetRoleObject ? fpGetRoleObject(role_id) : NULL;
    if (g_pk_config.move_enemy_hp_bottom)
        result = draw_enemy_bottom(pThis, surface, g_current_enemy_obj);
    else
        result = fpEnemyNameDraw(pThis, _edx, surface, role_id, x, y);

    g_current_enemy_obj = previous_enemy_obj;

    return result;
}

void Mod_Enemy_Name_Init(int game_version)
{
    if (!g_pk_config.show_enemy_hp && !g_pk_config.move_enemy_hp_bottom)
        return;

    if (game_version == 105)
    {
        g_enemy_name_return = ENEMY_NAME_DRAW_RETURN_105;
        g_target_enemy_name_draw = ENEMY_NAME_DRAW_FUNC_105;
        fpGetRoleObject = ENEMY_NAME_GET_ROLE_OBJECT_105;
        fpEnemyHealthSegments = (tEnemyHealthSegments)0x00424AF0;
        fpEnemyResourceEntry = (tEnemyResourceEntry)0x004F4D70;
        fpEnemyResourceResolve = (tEnemyResourceResolve)0x004F5CF0;
        fpEnemyNameGet = (tEnemyNameGet)0x004D00B0;
        fpEnemyPlayerResolve = ENEMY_NAME_GET_PLAYER_105;
        fpEnemyLockResolve = ENEMY_NAME_GET_LOCK_105;
        g_enemy_game_global = ENEMY_NAME_GAME_GLOBAL_105;
        g_enemy_mouse_global = ENEMY_NAME_MOUSE_GLOBAL_105;
        g_current_hp_offset = ENEMY_NAME_CURRENT_HP_OFFSET_105;
    }
    else if (game_version == 201)
    {
        g_enemy_name_return = ENEMY_NAME_DRAW_RETURN_201;
        g_target_enemy_name_draw = ENEMY_NAME_DRAW_FUNC_201;
        fpGetRoleObject = ENEMY_NAME_GET_ROLE_OBJECT_201;
        fpEnemyHealthSegments = (tEnemyHealthSegments)0x0042D7A0;
        fpEnemyResourceEntry = (tEnemyResourceEntry)0x0050BC50;
        fpEnemyResourceResolve = (tEnemyResourceResolve)0x0050CD10;
        fpEnemyNameGet = (tEnemyNameGet)0x004E4D90;
        fpEnemyPlayerResolve = ENEMY_NAME_GET_PLAYER_201;
        fpEnemyLockResolve = ENEMY_NAME_GET_LOCK_201;
        g_enemy_game_global = ENEMY_NAME_GAME_GLOBAL_201;
        g_enemy_mouse_global = ENEMY_NAME_MOUSE_GLOBAL_201;
        g_current_hp_offset = ENEMY_NAME_CURRENT_HP_OFFSET_201;
    }
    else
    {
        return;
    }

    if (MH_CreateHook(g_target_enemy_name_draw, &Detour_EnemyNameDraw, (LPVOID *)&fpEnemyNameDraw) != MH_OK)
        return;

    MH_EnableHook(g_target_enemy_name_draw);
}
