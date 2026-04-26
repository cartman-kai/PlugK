#include "pch.h"
#include "inheritance.h"
#include "config.h"
#include <windows.h>

// 辅助函数：修改内存中的字节
static void PatchByte(DWORD address, BYTE value)
{
    DWORD oldProtect;
    if (VirtualProtect((LPVOID)address, 1, PAGE_EXECUTE_READWRITE, &oldProtect))
    {
        *((BYTE *)address) = value;
        VirtualProtect((LPVOID)address, 1, oldProtect, &oldProtect);
    }
}

// 继承优化功能
void Mod_SaveInheritance_Init(int game_version)
{
    // 1. 检查配置项（假设你在 config 中定义了 enable_save_inheritance）
    if (!g_pk_config.enable_fix_inheritance)
    {
        return;
    }

    // ---------------------------------------------------------
    // 正传 v1.05
    // ---------------------------------------------------------
    if (game_version == 105)
    {
        // 修改：cmp dword ptr ds:[esi+C8], 3 -> 修改为 5
        // 地址：0049E39F 指令长度 7 字节 (83 BE C8 00 00 00 03)
        // 立即数 03 位于偏移 +6 的位置
        PatchByte(0x0049E39F + 6, 0x05);
    }
    // ---------------------------------------------------------
    // 外传 v2.01
    // ---------------------------------------------------------
    else if (game_version == 201)
    {
        // 修改点 1：结束游戏时的继承判定
        // 地址：004AEE3D 指令长度 7 字节 (83 BE C8 00 00 00 03)
        // 把 03 修改为 05
        PatchByte(0x004AEE3D + 6, 0x05);

        // 修改点 2：新角色创建时的继承读取
        // 逻辑：将 "jne" (如果不等于3就跳过) 修改为 "jl" 小于 3 就不执行继承了。
        // 或者是根据你的调研直接改跳转条件指令码
        // 004A9971 位置原始是 75 (JNE)，修改为 7C (JL)
        PatchByte(0x0048D5FD, 0x7C);

        // 如果你也想把 004A996E 的 cmp eax, 3 改成 5，可以开启下面这行
        // PatchByte(0x004A996E + 2, 0x05);
    }
}