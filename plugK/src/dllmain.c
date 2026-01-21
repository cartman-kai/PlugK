// dllmain.c : 定义 DLL 应用程序的入口点。
#include "pch.h"
#include "config.h"
#include "item_stack.h"
#include "shop_inf_stock.h"
#include "resolution.h"
#include "inv_auto_sort.h"
#include "shop_optimization.h"
#include "stash_ext.h"
#include "input_mgr.h"
#include "auto_fill_ext.h"
#include "show_tips.h"
#include "gem_insert.h"
#include "fuse_opt.h"
#include "item.h"
#include <windows.h>
#include <stdio.h>
#include <MinHook.h>

// 版本定义
#define VER_UNKNOWN 0
#define VER_105 105
#define VER_201 201

// 版本检测逻辑 (读取 .text 代码段中的立即数特征)
int DetectGameVersion()
{
    // -------------------------------------------------------------
    // 检测 v1.05
    // 目标指令地址: 0x0046ED12
    // 机器码特征: mov dword ptr ds:[55789C], 3F866666 (1.05 float)
    // 偏移 +6 字节处应该是 66 66 86 3F
    // -------------------------------------------------------------
    void *pVer105 = (void *)(0x0046ED12 + 6);

    // 安全检查：确保地址可读
    if (!IsBadReadPtr(pVer105, 4))
    {
        if (*(unsigned int *)pVer105 == 0x3F866666)
        {
            return VER_105;
        }
    }

    // -------------------------------------------------------------
    // 检测 v2.01
    // 目标指令地址: 0x0047CF22
    // 机器码特征: mov dword ptr ds:[588694], 4000A3D7 (2.01 float)
    // 偏移 +6 字节处应该是 D7 A3 00 40
    // -------------------------------------------------------------
    void *pVer201 = (void *)(0x0047CF22 + 6);

    if (!IsBadReadPtr(pVer201, 4))
    {
        if (*(unsigned int *)pVer201 == 0x4000A3D7)
        {
            return VER_201;
        }
    }

    return VER_UNKNOWN;
}

BOOL APIENTRY DllMain(HMODULE hModule,
                      DWORD ul_reason_for_call,
                      LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);

        // 1. 加载配置
        pk_config_load(NULL);

        // 2. 检测版本 (此时通过读取 .text 代码段来实现)
        int ver = DetectGameVersion();

        // 3. 根据版本应用补丁
        if (ver != VER_UNKNOWN)
        {
            if (MH_Initialize() != MH_OK)
                return;

            // 左侧文字提醒
            Mod_show_tips_init(ver);
            // 商店回复、暗器卖出不消失
            Mod_shop_inf_stock_init(ver);
            // 分辨率修改功能
            Mod_resolution_init(ver);
            // 背包整理
            Mod_inv_auto_sort_init(ver);
            // [新增] 打开道具画面防抖动功能
            Mod_UI_offset_fix_init(ver);
            // 商店物品优化
            Mod_shop_opt_init(ver);

            // 扩展储物箱初始化
            Mod_Stash_Ext_Init(ver);

            // 自动填充扩展页面
            Mod_Auto_Fill_Init(ver);

            // 镶嵌宝石条件修改
            Mod_Gem_Insert_Init(ver);

            // 屏幕震动效果禁用
            Mod_Screen_shake_effect_init(ver);

            // 炼化物品数量优化
            Mod_Fuse_Count_Opt_init(ver);

            // 显示物品名称
            Mod_Show_Item_Name_Init(ver);

            // 2. 启动统一的按键监听线程
            Mod_Input_Mgr_Init();
        }
        else
        {
            // 如果检测不到版本，可以输出调试信息，或者默默退出
            // OutputDebugStringA("PlugK: Unknown Game Version, skipping patches.\n");
        }

        break;
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}
