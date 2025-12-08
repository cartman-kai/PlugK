#include "pch.h"
#include "resolution.h"
#include "config.h"
#include <windows.h>
#include <stdio.h>

// ---------------------------------------------------------
// 全局变量，用于 Hook 中跳转
// ---------------------------------------------------------
static DWORD g_InitHook_Ret = 0;
static DWORD g_MetricsHook1_Ret = 0;
static DWORD g_MetricsHook2_CallTarget = 0; // 这是一个 Call 的目标地址
static DWORD g_MetricsHook2_Ret = 0;

// ---------------------------------------------------------
// 1. Init Hook Trampoline
// ---------------------------------------------------------
// V1.05 Hook点: 0x404DC5 (push 0; push 0; push 1)
// 此时 ESI 指向对象。 [ESI+228h]=W, [ESI+22Ch]=H
__declspec(naked) void InitHook_Trampoline()
{
    __asm {
        // [覆盖逻辑]
        // 强制写入 PlugK.ini 的分辨率到内存对象中
        push eax // 借用 EAX
        mov eax, g_pk_config.res_width
        mov [esi + 0x228], eax
        mov eax, g_pk_config.res_height
        mov [esi + 0x22C], eax
        pop eax

                // [执行被覆盖的原指令]
                // push 0; push 0; push 1
        push 0
        push 0
        push 1

        // [跳回]
        jmp [g_InitHook_Ret]
    }
}

// ---------------------------------------------------------
// 2. Metrics Hook 1 Trampoline
// ---------------------------------------------------------
// V1.05 Hook点: 0x470C72
// 此时 ESI=Width, EDI=Height (准备入栈)
__declspec(naked) void MetricsHook1_Trampoline()
{
    __asm {
        // [覆盖逻辑]
        // 直接修改寄存器值
        mov esi, g_pk_config.res_width
        mov edi, g_pk_config.res_height

            // [执行被覆盖的原指令] (部分)
            // 0x470C72: mov ecx, [ebx+34h] (3 bytes)
            // 0x470C75: push edi           (1 byte)
            // 0x470C76: push esi           (1 byte)
            // 我们 Hook 了 5 字节，刚好覆盖 mov ecx... + push edi + push esi
        
        mov ecx, [ebx + 0x34]
        push edi
        push esi

                // [跳回]
                // 跳回 push 0 (0x470C77)
        jmp [g_MetricsHook1_Ret]
    }
}

// ---------------------------------------------------------
// 3. Metrics Hook 2 Trampoline (修改堆栈参数)
// ---------------------------------------------------------
// V1.05 Hook点: 0x47B0E2
// 原指令: mov ecx, esi (2 bytes) + call sub_478D50 (5 bytes)
// 此时栈顶参数已经是 [W] [H] (通过之前的 push 压入)
// 这里的汇编比较特殊，我们需要手动 call 目标函数
__declspec(naked) void MetricsHook2_Trampoline()
{
    __asm {
        // [修改堆栈上的参数]
        // 当前 ESP 指向返回地址? 不，我们是 JMP 过来的。
        // 当前 ESP 指向栈顶 (Width)
        // ESP+4 是 Height
        
        mov eax, g_pk_config.res_width
        mov [esp], eax // 修改栈顶 (Width)
        
        mov eax, g_pk_config.res_height
        mov [esp + 4], eax // 修改栈+4 (Height)

            // [恢复被覆盖的指令 & 手动 Call]
        mov ecx, esi // 恢复 mov ecx, esi
        
        call [g_MetricsHook2_CallTarget] // 手动调用原版函数

        // [跳回]
        jmp [g_MetricsHook2_Ret]
    }
}

// ---------------------------------------------------------
// 工具: 获取相对 Call 的绝对地址
// ---------------------------------------------------------
DWORD GetCallTarget(DWORD callInstructionAddr)
{
    // call 指令: E8 [Offset]
    // Target = Addr + 5 + Offset
    DWORD offset = *(DWORD *)(callInstructionAddr + 1);
    return callInstructionAddr + 5 + offset;
}

// ---------------------------------------------------------
// 初始化逻辑
// ---------------------------------------------------------
void pk_resolution_init(int game_version)
{
    if (!g_pk_config.res_enabled)
        return;

    // 根据版本设置地址
    DWORD addr_init_hook = 0;
    DWORD addr_metrics1_hook = 0;
    DWORD addr_metrics2_hook = 0;

    // 我们需要知道 Metrics2 处 Call 指令的位置来计算目标
    DWORD addr_metrics2_call = 0;

    if (game_version == 105)
    {
        // --- V1.05 ---
        // 1. Init Hook: 0x404DC5
        // 指令: 6A 00 (push 0) / 6A 00 (push 0) / 6A 01 (push 1) = 6 bytes
        addr_init_hook = 0x404DC5;
        g_InitHook_Ret = 0x404DCB; // 0x404DC5 + 6

        // 2. Metrics Hook 1: 0x470C72
        // 指令: 8B 4B 34 (mov) / 57 (push) / 56 (push) = 5 bytes
        addr_metrics1_hook = 0x470C72;
        g_MetricsHook1_Ret = 0x470C77; // 0x470C72 + 5

        // 3. Metrics Hook 2: 0x47B0E2
        // 指令: 8B CE (mov ecx, esi) / E8 ... (call) = 7 bytes
        addr_metrics2_hook = 0x47B0E2;
        addr_metrics2_call = 0x47B0E4; // call指令的地址
        g_MetricsHook2_Ret = 0x47B0E9; // 0x47B0E2 + 7
    }
    else if (game_version == 201)
    {
        // --- V2.01 ---
        // 1. Init Hook: 0x40BD55
        // 指令: 6A 00 / 6A 00 / 6A 01 = 6 bytes
        addr_init_hook = 0x40BD55;
        g_InitHook_Ret = 0x40BD5B;

        // 2. Metrics Hook 1: 0x47F0D2
        // 指令: 8B 4B 34 / 57 / 56 = 5 bytes
        addr_metrics1_hook = 0x47F0D2;
        g_MetricsHook1_Ret = 0x47F0D7;

        // 3. Metrics Hook 2: 0x489C52
        // 指令: 8B CE / E8 ... = 7 bytes
        addr_metrics2_hook = 0x489C52;
        addr_metrics2_call = 0x489C54;
        g_MetricsHook2_Ret = 0x489C59;
    }
    else
    {
        return;
    }

    // --- 应用 Hooks ---

    // 1. Hook Init
    // 覆盖 6 字节 (3个push)
    InstallJmpHook(addr_init_hook, (DWORD)InitHook_Trampoline, 6);

    // 2. Hook Metrics 1
    // 覆盖 5 字节 (mov + 2 push)
    InstallJmpHook(addr_metrics1_hook, (DWORD)MetricsHook1_Trampoline, 5);

    // 3. Hook Metrics 2
    // 计算原 Call 的目标地址
    g_MetricsHook2_CallTarget = GetCallTarget(addr_metrics2_call);
    // 覆盖 7 字节 (mov + call)
    InstallJmpHook(addr_metrics2_hook, (DWORD)MetricsHook2_Trampoline, 7);
}