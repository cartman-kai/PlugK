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
            "EnableSort=1\n\n"
            "[Interface]\n"
            "; 打开背包/技能/属性窗口时，保持画面不左右移动（减少晃动感）\n"
            "; 1=开启(画面居中) 0=关闭(默认，画面右移)\n"
            "KeepCenter=1\n\n"
            "[Shop]\n"
            "; 设置为1 商店的回复类与暗器类物品购买后不消失 \n"
            "InfStock=0\n\n"
            "; 商店物品数量随机功能 1=开启 0=关闭，开启之后药品可叠加\n"
            "OptimizeItem=1\n\n"
            "; 商店物品排序 1=开启 0=关闭\n"
            "EnableSort=1\n"
            "\n"
            "[Resolution]\n"
            "; 启用分辨率补丁，需要 set.ini 中第一行设置为 ?=6  Enable=1 开启 Enable=0 关闭\n"
            "Enabled=1\n"
            "Width=960\n"
            "Height=720\n"
            "\n[Stash]\n"
            "; 启用扩展储物箱与背包功能 Ctrl + < 键切换储物箱 A/B 面  Ctrl + > 切换背包 \n"
            "EnableExt=1\n"
            "\n"
            "[Experimental]\n"
            "; 实验性功能 扩展背包免翻页填充物品 1=开启 0=关闭 \n"
            "AutoFillExt=1\n"
            "; 实验性功能 宝石插入条件修改 1=开启 0=关闭 \n"
            "EnableGemInsert=1\n"
            "\n",
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

    // [新增] 读取扩展储物箱配置
    g_pk_config.stash_ext_enabled = GetPrivateProfileIntA("Stash", "EnableExt", 1, ini_path);

    // 实验性功能，扩展背包自动填充
    g_pk_config.enable_autofill_ext = GetPrivateProfileIntA("Experimental", "AutoFillExt", 0, ini_path);

    // 宝石插入条件修改
    g_pk_config.enable_insert_gem = GetPrivateProfileIntA("Experimental", "EnableGemInsert", 0, ini_path);

    // 快捷键
    g_pk_config.key_stash_swap = GetPrivateProfileIntA("Hotkeys", "StashSwap", VK_OEM_COMMA, ini_path); // 储物箱切换  改为 Ctrl+<;
    g_pk_config.key_stash_sort = GetPrivateProfileIntA("Hotkeys", "StashSort", VK_OEM_4, ini_path);     // 储物箱整理  改为 Ctrl+[
    g_pk_config.key_inv_prev = GetPrivateProfileIntA("Hotkeys", "InvPrev", VK_OEM_PERIOD, ini_path);    // 背包切换 ctrl + >
    g_pk_config.key_inv_sort = GetPrivateProfileIntA("Hotkeys", "InvSort", VK_OEM_5, ini_path);         // 背包整理 ctrl+\

    // g_pk_config.key_stack_toggle = GetPrivateProfileIntA("Hotkeys", "StackToggle", VK_OEM_7, ini_path);

    // 1.05版程序，连击得分 *2
    // g_pk_config.combo_score_fix = GetPrivateProfileIntA("ComboScore", "EnableFix", 0, ini_path);

    // 存在无法扔出 宝石的 bug，暂时删除
    // g_pk_config.enable_item_stack = GetPrivateProfileIntA("Item", "EnableStack", 0, ini_path);
}