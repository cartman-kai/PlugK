#include "pch.h"
#include "resolution.h"
#include "config.h"
#include <windows.h>
#include <stdio.h>

// ---------------------------------------------------------
// 辅助函数: 直接修改内存中的 4字节 整数 (立即数)
// ---------------------------------------------------------
void PatchMemoryInt(DWORD address, int value)
{
    if (address == 0)
        return;

    DWORD oldProtect;
    // 解除内存保护
    if (VirtualProtect((LPVOID)address, 4, PAGE_EXECUTE_READWRITE, &oldProtect))
    {
        // 写入新的数值
        *(int *)address = value;

        // 恢复内存保护
        VirtualProtect((LPVOID)address, 4, oldProtect, &oldProtect);
    }
}

// ---------------------------------------------------------
// 修改分辨率的 hooking  初始化逻辑
// ---------------------------------------------------------
void Mod_resolution_init(int game_version)
{
    if (!g_pk_config.res_enabled)
        return;

    int w = g_pk_config.res_width;
    int h = g_pk_config.res_height;

    // =====================================================
    // VERSION 1.05
    // =====================================================
    if (game_version == 105)
    {
        // 1. 函数 404D30 (Init)
        // .text:00404D85 | mov dword ptr [esi+228h], 400h
        // .text:00404D8F | mov dword ptr [esi+22Ch], 300h
        // 指令是 C7 86 ... [Imm32]，偏移为 +6
        PatchMemoryInt(0x00404D85 + 6, w); // 修改 1024 -> Width
        PatchMemoryInt(0x00404D8F + 6, h); // 修改 768  -> Height

        // 2. 函数 470BE0 (GetSystemMetrics Switch Case)
        // .text:00470C4C | cmp eax, 400h  (偏移+1)
        // .text:00470C55 | mov edi, 300h  (偏移+1)
        // 注意：这里我们同时修改 cmp 的比较值和 mov 的赋值
        // 这样当 eax == ConfigWidth 时，esi 自动就是 Width，我们只需修正 edi (Height)
        PatchMemoryInt(0x00470C4C + 1, w); // 修改 cmp eax, 1024
        PatchMemoryInt(0x00470C55 + 1, h); // 修改 mov edi, 768

        // 3. 函数 47B0A0 (Reset/Metrics 2)
        // .text:0047B0B9 | cmp eax, 400h  (偏移+1)
        // .text:0047B0CC | push 300h      (偏移+1)
        // .text:0047B0D1 | push 400h      (偏移+1)
        PatchMemoryInt(0x0047B0B9 + 1, w); // 修改 cmp eax, 1024
        PatchMemoryInt(0x0047B0CC + 1, h); // 修改 push 768
        PatchMemoryInt(0x0047B0D1 + 1, w); // 修改 push 1024
    }
    // =====================================================
    // VERSION 2.01 (需要您在 IDA 中确认地址)
    // =====================================================
    else if (game_version == 201)
    {
        // 提示：以下地址需要您使用 IDA 在 2.01 程序中查找对应指令
        // 查找思路：搜索常量 0x400 (1024) 和 0x300 (768)

        // 1. Init 函数 (对应 1.05 的 404D30，2.01 入口约在 40BCC0)
        // 寻找: mov [esi+228h], 400h 和 mov [esi+22Ch], 300h
        DWORD addr_init_w = 0x0040BD15 + 6; // 填入指令地址 + 6
        DWORD addr_init_h = 0x0040BD1F + 6; // 填入指令地址 + 6

        PatchMemoryInt(addr_init_w, w);
        PatchMemoryInt(addr_init_h, h);

        // 2. Metrics1 函数 (对应 1.05 的 470BE0，2.01 入口约在 47F040)
        // 寻找: cmp eax, 400h ... mov edi, 300h
        DWORD addr_met1_cmp = 0x0047F0AC + 1; // 填入指令地址 + 1
        DWORD addr_met1_mov = 0x0047F0B5 + 1; // 填入指令地址 + 1

        PatchMemoryInt(addr_met1_cmp, w);
        PatchMemoryInt(addr_met1_mov, h);

        // 3. Metrics2 函数 (对应 1.05 的 47B0A0，2.01 入口约在 489C10)
        // 寻找: cmp eax, 400h ... push 300h ... push 400h
        DWORD addr_met2_cmp = 0x00489C29 + 1;    // 填入指令地址 + 1
        DWORD addr_met2_push_h = 0x00489C3C + 1; // 填入指令地址 + 1
        DWORD addr_met2_push_w = 0x00489C41 + 1; // 填入指令地址 + 1

        PatchMemoryInt(addr_met2_cmp, w);
        PatchMemoryInt(addr_met2_push_h, h);
        PatchMemoryInt(addr_met2_push_w, w);
    }
}