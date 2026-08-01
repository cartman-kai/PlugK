#include "pch.h"
#include "resolution.h"
#include "config.h"
#include <MinHook.h>
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
// set.ini 第一行（分辨率枚举）setter 的 Hook
// sub_4A5970 (0x004A5970, __stdcall, 单 int 参数) 是分辨率枚举的唯一入口：
//   读取 set.ini 第一行、游戏内设置菜单、应用设置时都会走到这里，
//   内部执行 dword_548618 = a1（全局分辨率枚举），随后经显示控制对象
//   (+8) 由主循环 sub_4043E0 触发 sub_404D30 切换显示模式。
// 开启自定义分辨率时把枚举强制为 6 (1024x768)，使游戏必然进入
// sub_404D30 的 1024 分支（下方已把该分支 patch 为自定义宽高），
// 这样无论 set.ini 第一行写 4/5/6，自定义分辨率都会生效。
// 1.05 地址 0x004A5970；2.01 对应 0x004B8140（sub_4B8140，同构）。
// ---------------------------------------------------------
typedef void(__stdcall *fn_SetResEnum)(int mode);
static fn_SetResEnum fpOriginalSetResEnum = NULL;

static void __stdcall Detour_SetResEnum(int mode)
{
    if (g_pk_config.res_enabled)
        mode = 6; // 强制 1024x768 枚举
    fpOriginalSetResEnum(mode);
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
        // 0. Hook set.ini 分辨率枚举 setter (0x004A5970)
        // 覆盖 ini 加载、游戏内设置菜单、应用设置三条路径；
        // 开启自定义分辨率时强制枚举为 6，确保进入下面的 1024 分支
        if (MH_CreateHook((LPVOID)0x004A5970, &Detour_SetResEnum,
                          (LPVOID *)&fpOriginalSetResEnum) == MH_OK)
        {
            MH_EnableHook((LPVOID)0x004A5970);
        }

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
    // VERSION 2.01 (地址已于 2026-08-01 在 IDA 中逐一验证)
    // =====================================================
    else if (game_version == 201)
    {
        // 0. Hook set.ini 分辨率枚举 setter (0x004B8140)
        // 与 1.05 的 0x004A5970 同构：dword_578BA8 = 枚举值
        // 开启自定义分辨率时强制枚举为 6，确保进入下面的 1024 分支
        if (MH_CreateHook((LPVOID)0x004B8140, &Detour_SetResEnum,
                          (LPVOID *)&fpOriginalSetResEnum) == MH_OK)
        {
            MH_EnableHook((LPVOID)0x004B8140);
        }

        // 1. Init 函数 sub_40BCC0 (对应 1.05 的 404D30)
        // .text:0040BD15 | mov dword ptr [esi+228h], 400h
        // .text:0040BD1F | mov dword ptr [esi+22Ch], 300h
        DWORD addr_init_w = 0x0040BD15 + 6; // 已通过 IDA 验证
        DWORD addr_init_h = 0x0040BD1F + 6; // 已通过 IDA 验证

        PatchMemoryInt(addr_init_w, w);
        PatchMemoryInt(addr_init_h, h);

        // 2. Metrics1 函数 sub_47F040 (对应 1.05 的 470BE0)
        // .text:0047F0AC | cmp eax, 400h
        // .text:0047F0B5 | mov edi, 300h
        DWORD addr_met1_cmp = 0x0047F0AC + 1; // 已通过 IDA 验证
        DWORD addr_met1_mov = 0x0047F0B5 + 1; // 已通过 IDA 验证

        PatchMemoryInt(addr_met1_cmp, w);
        PatchMemoryInt(addr_met1_mov, h);

        // 3. Metrics2 函数 sub_489C10 (对应 1.05 的 47B0A0)
        // .text:00489C29 | cmp eax, 400h
        // .text:00489C3C | push 300h
        // .text:00489C41 | push 400h
        DWORD addr_met2_cmp = 0x00489C29 + 1;    // 已通过 IDA 验证
        DWORD addr_met2_push_h = 0x00489C3C + 1; // 已通过 IDA 验证
        DWORD addr_met2_push_w = 0x00489C41 + 1; // 已通过 IDA 验证

        PatchMemoryInt(addr_met2_cmp, w);
        PatchMemoryInt(addr_met2_push_h, h);
        PatchMemoryInt(addr_met2_push_w, w);
    }
}