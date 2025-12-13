// dllmain.c : 定义 DLL 应用程序的入口点。
#include "pch.h"
#include "config.h"
#include "shop.h"
#include "resolution.h"
#include "item_stack.h"
#include "inventory.h"
#include <windows.h>
#include <stdio.h>

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
        pk_config_load();

        // 2. 检测版本 (此时通过读取 .text 代码段来实现)
        int ver = DetectGameVersion();

        // 3. 根据版本应用补丁
        if (ver != VER_UNKNOWN)
        {
            pk_shop_init(ver);
            pk_resolution_init(ver);
            pk_item_stack_init(ver);
            pk_inventory_init(ver);
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
