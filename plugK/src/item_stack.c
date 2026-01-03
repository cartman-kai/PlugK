#include "pch.h"
#include "item_stack.h"
#include "config.h"
#include <stdio.h>

// --------------------------------------------------------
// 内存 Patch 工具函数
// --------------------------------------------------------
// targetAddr: 目标指令地址
// offset: 立即数（要修改的字节）相对于指令开始的偏移量
// newValue: 要写入的新字节值 (09h)
void MemoryPatchByte(DWORD targetAddr, int offset, BYTE newValue)
{
    DWORD address = targetAddr + offset;
    DWORD oldProtect;

    // 1. 修改内存保护属性为可写
    if (!VirtualProtect((LPVOID)address, 1, PAGE_EXECUTE_READWRITE, &oldProtect))
    {
        // 错误处理，可忽略或打印日志
        return;
    }

    // 2. 写入新的字节值
    *(BYTE *)address = newValue;

    // 3. 恢复内存保护属性
    VirtualProtect((LPVOID)address, 1, oldProtect, &oldProtect);
}

// --------------------------------------------------------
// 初始化函数
// --------------------------------------------------------
void Mod_item_stack_init(int game_version)
{
    if (!g_pk_config.enable_item_stack)
    {
        return; // 配置未开启，不执行补丁
    }

    DWORD item_type_min_cmp_addr = 0;
    DWORD item_type_max_cmp_addr = 0;

    // **注意：我们假设要修改的立即数位于 cmp 指令地址 + 2 的位置。**
    // 常见的 cmp reg, imm8 指令长度为 3 字节，立即数在第 3 字节 (偏移 2)。
    const int cmp_offset = 2;
    const BYTE item_type_min_value = 0x09; // 09 药品为 10 ，小于 10
    const BYTE item_type_max_value = 0x24; // 36 比 外传 猫眼等 大1

    if (game_version == 105)
    {
        // 目标指令: 0047F013 cmp eax, 14h
        item_type_min_cmp_addr = 0x0047F013;
        // cmp eax, 1Dh
        item_type_max_cmp_addr = 0x0047F018;
    }
    else if (game_version == 201)
    {
        // 目标指令: 0048DE26 cmp eax, 14h
        item_type_min_cmp_addr = 0x0048DE26;
        // 目标指令: 0048DE2B cmp eax, 1Dh
        item_type_max_cmp_addr = 0x0048DE2B;
    }
    else
    {
        return;
    }

    // 执行内存补丁
    MemoryPatchByte(item_type_min_cmp_addr, cmp_offset, item_type_min_value);
    MemoryPatchByte(item_type_max_cmp_addr, cmp_offset, item_type_max_value);
}