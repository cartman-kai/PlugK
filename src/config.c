#include "pch.h"
#include "config.h"
#include <stdio.h>

PK_CONFIG g_pk_config = {0};

void pk_config_create_default(const char *path)
{
    FILE *f = fopen(path, "w");
    if (f)
    {
        fputs(
            "[General]\n"
            "; PlugK Configuration File\n"
            ";回复物品进入背包叠加 1=启用 0=关闭\n"
            "StackPotion=0\n"
            "\n"
            "[Inventory]\n"
            "; 启用背包一键整理 (Ctrl+\\) 1=启用 0=关闭\n"
            "EnableSort=0\n\n"
            "[Interface]\n"
            "; 打开背包/技能/属性窗口时，保持画面不左右移动（减少晃动感）\n"
            "; 1=开启(画面居中) 0=关闭(默认，画面右移)\n"
            "KeepCenter=0\n\n"
            "[Shop]\n"
            "; 设置为1，商店的回复类与暗器类物品购买后不消失 \n"
            "ShopNoVanish=0\n\n"
            "[Resolution]\n"
            "; 启用分辨率补丁，会跳过读取 set.ini Enable=1 开启\n"
            "Enabled=0\n"
            "Width=800\n"
            "Height=600\n",
            f);
        fclose(f);
    }
}

void pk_config_load()
{
    char ini_path[MAX_PATH];
    char dll_path[MAX_PATH];

    HMODULE hModule = GetModuleHandleA("PlugK.dll");
    if (!hModule)
        hModule = GetModuleHandleA(NULL);

    GetModuleFileNameA(hModule, dll_path, MAX_PATH);
    char *last_slash = strrchr(dll_path, '\\');
    if (last_slash)
        *(last_slash + 1) = '\0';

    snprintf(ini_path, MAX_PATH, "%sPlugK.ini", dll_path);

    if (GetFileAttributesA(ini_path) == INVALID_FILE_ATTRIBUTES)
    {
        pk_config_create_default(ini_path);
    }

    // 商店配置
    g_pk_config.shop_no_vanish = GetPrivateProfileIntA("Shop", "ShopNoVanish", 0, ini_path);

    // [新增] 分辨率配置
    // 如果没有配置，默认给个 800x600 保底
    g_pk_config.res_enabled = GetPrivateProfileIntA("Resolution", "Enabled", 0, ini_path);
    g_pk_config.res_width = GetPrivateProfileIntA("Resolution", "Width", 800, ini_path);
    g_pk_config.res_height = GetPrivateProfileIntA("Resolution", "Height", 600, ini_path);

    // [新增] 堆叠配置
    // 读取 [General] 或 [Item] 下的配置，这里假设放在 [General] 下
    // g_pk_config.stack_potion = GetPrivateProfileIntA("General", "StackPotion", 0, ini_path);

    // [新增] 读取背包整理开关
    g_pk_config.inventory_sort = GetPrivateProfileIntA("Inventory", "EnableSort", 0, ini_path);

    // [新增] 打开背包界面不晃动
    g_pk_config.ui_keep_center = GetPrivateProfileIntA("Interface", "KeepCenter", 0, ini_path);
}