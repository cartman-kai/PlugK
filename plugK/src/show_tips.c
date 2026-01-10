#include "pch.h"
#include "show_tips.h"
#include "config.h"
#include <stdio.h>

// [新增] 游戏文字显示相关地址
static DWORD g_Addr_ShowMsg = 0;
static DWORD g_Addr_MsgObjPtr = 0;

// 封装游戏内文字显示
void ShowGameLog(const char *text)
{
    if (g_Addr_ShowMsg == 0 || g_Addr_MsgObjPtr == 0)
        return;

    DWORD thisPtr = *(DWORD *)g_Addr_MsgObjPtr; // 读取常量地址里的 this 指针
    if (thisPtr == 0)
        return;

    __asm {
        push 0 // 参数 a3
        push text // 参数 String2
        mov ecx, thisPtr // __thiscall: this 指针放入 ecx
        call g_Addr_ShowMsg // 调用函数
    }
    return;
}

void Mod_show_tips_init(int game_version)
{
    // 1. 根据版本设置地址
    if (game_version == 105)
    {
        // [新增] 1.05 消息函数地址
        g_Addr_ShowMsg = 0x004C5EA0;
        g_Addr_MsgObjPtr = 0x0055BCBC;
    }
    else if (game_version == 201)
    {
        // [新增] 2.01 消息函数地址
        g_Addr_ShowMsg = 0x004DA5E0;
        g_Addr_MsgObjPtr = 0x0058D290;
    }
    else
    {
        return;
    }
}