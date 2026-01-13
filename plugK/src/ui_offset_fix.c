/**
 * @file ui_offset_fix.c
 * @brief 禁用打开背包后画面右移
 * @details 背包，角色，技能窗口都生效
 */

#include "pch.h"
#include "ui_offset_fix.h"
#include "config.h"
#include <windows.h>
#include <stdio.h>

void Mod_UI_offset_fix_init(int game_version)
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

void Mod_Screen_shake_effect_init(int game_version)
{
    // 1. 检查配置
    if (!g_pk_config.disable_screen_shake)
    {
        return;
    }

    DWORD targetAddress = 0;

    // 2. 根据版本选择地址
    if (game_version == 105)
    {
        // 00407769 | A1 C4855500 | mov eax, dword ptr ds:[5585C4]
        targetAddress = 0x00407769;
    }
    else if (game_version == 201)
    {
        // 0040E829 | A1 44955800 | mov eax, dword ptr ds:[589544]
        targetAddress = 0x0040E829;
    }
    else
    {
        return;
    }

    // 3. 准备补丁数据
    // 原始指令长度为 5 字节
    // 目标指令: mov eax, 0 -> B8 00 00 00 00 (5 字节)
    BYTE patch[] = {0xB8, 0x00, 0x00, 0x00, 0x00};

    // 4. 执行内存写入
    DWORD oldProtect;
    // 修改内存页属性为可写
    if (VirtualProtect((LPVOID)targetAddress, sizeof(patch), PAGE_EXECUTE_READWRITE, &oldProtect))
    {
        // 写入指令
        memcpy((void *)targetAddress, patch, sizeof(patch));

        // 恢复原始内存属性
        VirtualProtect((LPVOID)targetAddress, sizeof(patch), oldProtect, &oldProtect);
    }
}