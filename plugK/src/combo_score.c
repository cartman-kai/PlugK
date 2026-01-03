#include "pch.h"
#include "config.h"

// 跳转返回地址：指向原程序 Hook 点之后的下一条指令
static DWORD g_ComboHook_Ret = 0x00424018;

int __stdcall CalculateComboScoreFix(int score)
{
    float result = (float)score;

    if (score < 10)
    {
        // 1. 低于 10 分不补正，认为是无效或平庸连招
        return score;
    }
    else if (score < 30)
    {
        // 2. 10-30 分区间：强力挽救并扩容 (20分跨度映射到45分跨度)
        // 映射起点 10->20, 终点 30->65
        result = (score - 10) * 2.25f + 20.0f;
    }
    else if (score < 60)
    {
        // 3. 30-60 分区间：高手进阶 (30分跨度映射到35分跨度)
        // 映射起点 30->65, 终点 60->100
        result = (score - 30) * 1.166f + 65.0f;
    }
    else
    {
        // 4. 60 分以上直接满分
        result = 100.0f;
    }

    // 四舍五入取整，保证数值分布自然（奇偶交错）
    int finalScore = (int)(result + 0.5f);

    if (finalScore > 100)
        finalScore = 100;

    return finalScore;
}

// ---------------------------------------------------------
// 极简汇编逻辑：只负责计算和补偿
// ---------------------------------------------------------
__declspec(naked) void Hook_ComboScore_Fix_Trampoline()
{
    __asm {
        // 1. 进入时 EAX 是原始分数
        pushad // 保存所有寄存器 (虽然这里 EAX 会被返回值覆盖，但 pushad 更稳妥)
        
        push eax // 压入参数：原始分数
        call CalculateComboScoreFix // 调用 C 函数 (stdcall 自动清理参数)

                    // C 函数执行完后，结果在 EAX 中
                    // 但我们要把结果存入 pushad 压入的栈结构中，以便 popad 后 EAX 是新值
        mov [esp + 28], eax // 28 是 pushad 结构中 EAX 的偏移
        
        popad // 恢复寄存器，此时 EAX = 新分数

                // 2. 补偿原指令
        mov ebp, eax // 原指令: mov ebp, eax
        cmp ebp, 0x14 // 原指令: cmp ebp, 14h

        // 3. 返回
        jmp g_ComboHook_Ret
    }
}

// ---------------------------------------------------------
// C 语言初始化：负责判断配置与安装
// ---------------------------------------------------------
void Mod_Combo_Score_Init(int ver)
{
    // 只有 1.05 版本支持此 Hook 点
    if (ver != 105)
        return;

    // 在 C 语言层面判断，如果未开启则不安装 Hook，不修改内存
    if (!g_pk_config.combo_score_fix)
    {
        return;
    }

    DWORD addr = 0x00424013;
    DWORD oldProtect;

    // 修改内存页属性
    if (VirtualProtect((LPVOID)addr, 5, PAGE_EXECUTE_READWRITE, &oldProtect))
    {
        // 写入 JMP 指令 (E9)
        *(BYTE *)addr = 0xE9;

        // 计算相对偏移
        *(DWORD *)(addr + 1) = (DWORD)Hook_ComboScore_Fix_Trampoline - addr - 5;

        // 恢复内存页属性
        VirtualProtect((LPVOID)addr, 5, oldProtect, &oldProtect);
    }
}