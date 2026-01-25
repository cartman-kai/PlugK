#include "pch.h"
#include "show_tips.h"
#include "config.h"
#include <stdio.h>
#include <malloc.h>

// [新增] 游戏文字显示相关地址
static DWORD g_Addr_ShowMsg = 0;
static DWORD g_Addr_MsgObjPtr = 0;

// 封装游戏内文字显示
void SendGameTips(const char *text)
{
    if (g_Addr_ShowMsg == 0 || g_Addr_MsgObjPtr == 0 || text == NULL)
        return;

    DWORD thisPtr = *(DWORD *)g_Addr_MsgObjPtr; // 读取常量地址里的 this 指针
    if (thisPtr == 0)
        return;

    // --- 编码转换: UTF-8 -> GBK ---
    // 1. 获取宽字符长度 (UTF-8 -> UTF-16)
    int wide_len = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    if (wide_len <= 0)
        return;

    // 2. 栈上分配宽字符缓冲区
    wchar_t *wide_str = (wchar_t *)_alloca(wide_len * sizeof(wchar_t));
    MultiByteToWideChar(CP_UTF8, 0, text, -1, wide_str, wide_len);

    // 3. 获取 GBK 长度 (UTF-16 -> GBK/936)
    int gbk_len = WideCharToMultiByte(936, 0, wide_str, -1, NULL, 0, NULL, NULL);
    if (gbk_len <= 0)
        return;

    // 4. 栈上分配 GBK 缓冲区
    char *gbk_text = (char *)_alloca(gbk_len);
    WideCharToMultiByte(936, 0, wide_str, -1, gbk_text, gbk_len, NULL, NULL);

    __asm {
        push 0 // 参数 a3
        push gbk_text // 使用转换后的 GBK 字符串
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