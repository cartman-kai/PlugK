#include "pch.h"
#include "config.h"
#include "fuse_opt.h"
#include <windows.h>
#include <stdio.h>

// 合成时，仅消耗1个
void Mod_Fuse_Count_Opt_init(int game_version)
{
    // 1. 检查配置
    if (!g_pk_config.enable_fuse_opt)
    {
        return;
    }

    DWORD targetAddress = 0;

    // 2. 根据版本选择地址
    if (game_version == 105)
    {
        // 47FD1E | 6A FF   | push -1
        targetAddress = 0x0047FD1E;
    }
    else if (game_version == 201)
    {
        // 0048EAE8 | 6A FF   | push -1
        targetAddress = 0x0048EAE8;
    }
    else
    {
        return;
    }

    // 3. 准备补丁数据
    // 原始指令长度为 2 字节
    // 目标指令: push 0xFFFFFFFF -> 6A 01 (2 字节)
    BYTE patch[] = {0x6A, 0x01};

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