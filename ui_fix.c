#include "pch.h"
#include "ui_fix.h"
#include "config.h"
#include <windows.h>
#include <stdio.h>

void pk_ui_fix_init(int game_version)
{
    // 1. 检查配置，如果未开启则直接返回
    // 默认关闭，需用户手动开启
    if (!g_pk_config.ui_keep_center)
    {
        return;
    }

    DWORD targetAddress = 0;

    // 2. 根据版本选择地址
    if (game_version == 105)
    {
        // 1.05 程序修改点
        targetAddress = 0x0047AD53;
    }
    else if (game_version == 201)
    {
        // 2.01 程序修改点
        targetAddress = 0x004898C3;
    }
    else
    {
        // 不支持的版本
        return;
    }

    // 3. 准备补丁数据
    // 原始指令 (猜测): C1 F8 02 (SAR EAX, 2) -> 3 字节
    // 目标指令:       D1 F8    (SAR EAX, 1) -> 2 字节
    // 填充指令:       90       (NOP)        -> 1 字节
    BYTE patch[] = {0xD1, 0xF8, 0x90};

    // 4. 执行内存修改
    DWORD oldProtect;
    if (VirtualProtect((LPVOID)targetAddress, sizeof(patch), PAGE_EXECUTE_READWRITE, &oldProtect))
    {
        // 写入补丁
        memcpy((void *)targetAddress, patch, sizeof(patch));

        // 恢复内存保护属性
        VirtualProtect((LPVOID)targetAddress, sizeof(patch), oldProtect, &oldProtect);

        // 可选：输出调试信息
        // OutputDebugStringA("PlugK: UI Center Fix Applied.");
    }
}