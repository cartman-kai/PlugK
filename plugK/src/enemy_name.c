#include "pch.h"
#include "config.h"
#include "enemy_name.h"
#include <MinHook.h>
#include <intrin.h>
#include <stdio.h>
#include <string.h>

typedef int(__fastcall *tDrawText)(void *pThis, void *_edx, int surface, int x, int y, const char *text, int mode);
typedef int(__fastcall *tEnemyNameDraw)(void *pThis, void *_edx, int surface, int role_id, int x, int y);
typedef void *(__cdecl *tGetRoleObject)(int role_id);
typedef int(__fastcall *tGetMaxHp)(void *pThis, void *_edx);

static tDrawText fpDrawText = NULL;
static tEnemyNameDraw fpEnemyNameDraw = NULL;
static tGetRoleObject fpGetRoleObject = NULL;
static void *g_target_draw_text = NULL;
static void *g_target_enemy_name_draw = NULL;
static void *g_enemy_name_return = NULL;
static void *g_current_enemy_obj = NULL;
static int g_current_hp_offset = 0;

#define ENEMY_NAME_DRAW_TEXT_105 ((void *)0x004B25A0)
#define ENEMY_NAME_DRAW_RETURN_105 ((void *)0x004B0BF1)
#define ENEMY_NAME_DRAW_FUNC_105 ((void *)0x004B0A60)
#define ENEMY_NAME_GET_ROLE_OBJECT_105 ((tGetRoleObject)0x00420D50)
#define ENEMY_NAME_DRAW_TEXT_201 ((void *)0x004C58F0)
#define ENEMY_NAME_DRAW_RETURN_201 ((void *)0x004C3DE1)
#define ENEMY_NAME_DRAW_FUNC_201 ((void *)0x004C3C50)
#define ENEMY_NAME_GET_ROLE_OBJECT_201 ((tGetRoleObject)0x00429340)
#define ENEMY_NAME_HP_TEXT_Y_OFFSET 14
#define ENEMY_NAME_CURRENT_HP_OFFSET_105 0x3A2
#define ENEMY_NAME_CURRENT_HP_OFFSET_201 0x3AE
#define ENEMY_NAME_MAX_HP_VT_OFFSET 0x78

#pragma intrinsic(_ReturnAddress)

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

    vtable = *(void ***)obj;
    if (!vtable)
        return 0;

    get_max_hp = (tGetMaxHp)vtable[ENEMY_NAME_MAX_HP_VT_OFFSET / sizeof(void *)];
    if (!get_max_hp)
        return 0;

    return get_max_hp(obj, NULL);
}

int __fastcall Detour_DrawText(void *pThis, void *_edx, int surface, int x, int y, const char *text, int mode)
{
    void *caller = _ReturnAddress();
    int result = fpDrawText(pThis, _edx, surface, x, y, text, mode);

    if (should_append_probe_line(text, caller))
    {
        char hp_text[64];
        int current_hp = get_enemy_current_hp(g_current_enemy_obj);
        int max_hp = get_enemy_max_hp(g_current_enemy_obj);

        if (max_hp > 0)
        {
            _snprintf_s(hp_text, sizeof(hp_text), _TRUNCATE, "HP: %d/%d", current_hp, max_hp);
            fpDrawText(pThis, _edx, surface, x, y + ENEMY_NAME_HP_TEXT_Y_OFFSET, hp_text, mode);
        }
    }

    return result;
}

int __fastcall Detour_EnemyNameDraw(void *pThis, void *_edx, int surface, int role_id, int x, int y)
{
    void *previous_enemy_obj = g_current_enemy_obj;

    g_current_enemy_obj = fpGetRoleObject ? fpGetRoleObject(role_id) : NULL;
    int result = fpEnemyNameDraw(pThis, _edx, surface, role_id, x, y);
    g_current_enemy_obj = previous_enemy_obj;

    return result;
}

void Mod_Enemy_Name_Init(int game_version)
{
    if (!g_pk_config.show_enemy_hp)
        return;

    if (game_version == 105)
    {
        g_target_draw_text = ENEMY_NAME_DRAW_TEXT_105;
        g_enemy_name_return = ENEMY_NAME_DRAW_RETURN_105;
        g_target_enemy_name_draw = ENEMY_NAME_DRAW_FUNC_105;
        fpGetRoleObject = ENEMY_NAME_GET_ROLE_OBJECT_105;
        g_current_hp_offset = ENEMY_NAME_CURRENT_HP_OFFSET_105;
    }
    else if (game_version == 201)
    {
        g_target_draw_text = ENEMY_NAME_DRAW_TEXT_201;
        g_enemy_name_return = ENEMY_NAME_DRAW_RETURN_201;
        g_target_enemy_name_draw = ENEMY_NAME_DRAW_FUNC_201;
        fpGetRoleObject = ENEMY_NAME_GET_ROLE_OBJECT_201;
        g_current_hp_offset = ENEMY_NAME_CURRENT_HP_OFFSET_201;
    }
    else
    {
        return;
    }

    if (MH_CreateHook(g_target_draw_text, &Detour_DrawText, (LPVOID *)&fpDrawText) != MH_OK)
        return;

    if (MH_CreateHook(g_target_enemy_name_draw, &Detour_EnemyNameDraw, (LPVOID *)&fpEnemyNameDraw) != MH_OK)
        return;

    MH_EnableHook(g_target_draw_text);
    MH_EnableHook(g_target_enemy_name_draw);
}
