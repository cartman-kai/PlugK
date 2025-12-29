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
            "; PlugK Configuration File\n"
            "[Inventory]\n"
            "; 一键整理功能, 1=启用 0=关闭, 背包整理 Ctrl+\\ 储物箱整理 Ctrl+[ \n"
            "EnableSort=0\n\n"
            "[Interface]\n"
            "; 打开背包/技能/属性窗口时，保持画面不左右移动（减少晃动感）\n"
            "; 1=开启(画面居中) 0=关闭(默认，画面右移)\n"
            "KeepCenter=0\n\n"
            "[Shop]\n"
            "; 设置为1 商店的回复类与暗器类物品购买后不消失 \n"
            "InfStock=0\n\n"
            "; 商店物品数量随机功能 1=开启 0=关闭，开启之后药品可叠加\n"
            "OptimizeItem=0\n\n"
            "; 商店物品排序 1=开启 0=关闭\n"
            "EnableSort=0\n"
            "\n"
            "[Resolution]\n"
            "; 启用分辨率补丁，需要 set.ini 中第一行设置为 ?=6  Enable=1 开启 Enable=0 关闭\n"
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
    g_pk_config.shop_inf_stock = GetPrivateProfileIntA("Shop", "InfStock", 0, ini_path);

    // 分辨率配置
    // 如果没有配置，默认给个 800x600 保底
    g_pk_config.res_enabled = GetPrivateProfileIntA("Resolution", "Enabled", 0, ini_path);
    g_pk_config.res_width = GetPrivateProfileIntA("Resolution", "Width", 800, ini_path);
    g_pk_config.res_height = GetPrivateProfileIntA("Resolution", "Height", 600, ini_path);

    // 读取背包整理开关
    g_pk_config.inventory_sort = GetPrivateProfileIntA("Inventory", "EnableSort", 0, ini_path);

    // 打开背包界面不晃动
    g_pk_config.ui_keep_center = GetPrivateProfileIntA("Interface", "KeepCenter", 0, ini_path);

    // 商店物品数量随机
    g_pk_config.shop_item_count = GetPrivateProfileIntA("Shop", "OptimizeItem", 0, ini_path);
    // 商店物品排序
    g_pk_config.shop_sort = GetPrivateProfileIntA("Shop", "EnableSort", 0, ini_path);
}