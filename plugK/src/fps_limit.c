// fps_limit.c : 帧率限制（渲染节流）
//
// 背景：2.01 主循环为 PeekMessage 自旋，渲染每轮迭代都执行（门控 dword_5741D4 初始即 1），
// 只有模拟按固定步长 35ms 限速 —— 现代 CPU + ddraw wrapper 下渲染帧率无上限，每帧都是一次
// 无效的 ddraw blt。本模块在每帧函数入口按目标帧间隔 Sleep + spin 补精度，把渲染帧率限制到
// 桌面刷新率（或用户指定值）。Sleep 的时长会被游戏计入自测帧间隔，模拟追赶循环自动补偿，
// 游戏速度不变。调研结论见 docs/reverse_kb/systems/fps_limit_201.md。
#include "pch.h"
#include "fps_limit.h"
#include "config.h"
#include <MinHook.h>
#include <mmsystem.h>

#pragma comment(lib, "winmm.lib")

#define VER_105 105
#define VER_201 201

// 每帧函数（游戏帧）：模拟 35ms 固定步 + 每轮渲染。
// 1.05/2.01 均在该入口节流；对应调用链和地址见 docs/reverse_kb/systems/fps_limit_105.md。
#define ADDR_FRAME_FN_105 0x00404700
#define ADDR_FRAME_FN_201 0x0040B6C0

// 配置语义：0 = 跟随桌面刷新率；-1 = 不限速；其它值 = 指定帧率（clamp 到 FPS_LIMIT_MIN..MAX）
#define FPS_LIMIT_AUTO 0
#define FPS_LIMIT_OFF (-1)
#define FPS_LIMIT_MIN 15
#define FPS_LIMIT_MAX 400

// 使用 __fastcall 模拟 __thiscall：pThis -> ECX, _edx -> EDX (占位)
typedef int(__fastcall *tFrameFn)(void *pThis, void *_edx);

static tFrameFn fpFrameFn = NULL;
static LARGE_INTEGER g_qpc_freq;
static LONGLONG g_next_frame_qpc;
// 0 = 未初始化；1 = 限速生效；-1 = 不限速（解析失败或配置为不限）
static volatile LONG g_limiter_ready = 0;
static volatile LONG g_target_fps = 0;

static LONGLONG QpcNow(void)
{
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return counter.QuadPart;
}

static int GetDesktopRefreshRate(void)
{
    DEVMODEW dm;
    int frequency;

    ZeroMemory(&dm, sizeof(dm));
    dm.dmSize = sizeof(dm);
    if (EnumDisplaySettingsW(NULL, ENUM_CURRENT_SETTINGS, &dm))
        frequency = dm.dmDisplayFrequency;
    else
        frequency = 0;

    return frequency > 0 ? frequency : 60;
}

static int ResolveTargetFps(int configured)
{
    int fps;

    if (configured == FPS_LIMIT_OFF)
        return 0;

    fps = (configured == FPS_LIMIT_AUTO) ? GetDesktopRefreshRate() : configured;
    if (fps < FPS_LIMIT_MIN)
        fps = FPS_LIMIT_MIN;
    if (fps > FPS_LIMIT_MAX)
        fps = FPS_LIMIT_MAX;
    return fps;
}

static int __fastcall Detour_FrameFn(void *pThis, void *_edx)
{
    LONGLONG now;

    // 首次进入时解析目标帧率（避免在 DllMain loader lock 内调用 EnumDisplaySettingsW）
    if (g_limiter_ready == 0)
    {
        int fps = ResolveTargetFps(g_pk_config.fps_limit);
        if (fps > 0)
        {
            InterlockedExchange(&g_target_fps, fps);
            InterlockedExchange(&g_limiter_ready, 1);
        }
        else
        {
            InterlockedExchange(&g_limiter_ready, -1);
        }
    }

    if (g_limiter_ready == 1)
    {
        now = QpcNow();
        if (now < g_next_frame_qpc)
        {
            // 剩余时间（微秒）；Sleep 粗等留 ~1ms，再 spin 补足精度（覆盖 120/144/240Hz）
            LONGLONG remain_us = (g_next_frame_qpc - now) * 1000000 / g_qpc_freq.QuadPart;
            if (remain_us > 2000)
                Sleep((DWORD)((remain_us - 1000) / 1000));
            while (QpcNow() < g_next_frame_qpc)
            {
                YieldProcessor(); // PAUSE 指令，减轻 spin-wait 对核的占用
            }
        }
        // 不累积延迟：本帧已晚于 next 时直接重置，避免渲染超时后追帧产生快速连帧
        g_next_frame_qpc = QpcNow() + g_qpc_freq.QuadPart / g_target_fps;
    }

    return fpFrameFn(pThis, _edx);
}

void Mod_FpsLimit_Init(int game_version)
{
    LPVOID target;

    if (g_pk_config.fps_limit == FPS_LIMIT_OFF)
        return;

    if (game_version == VER_105)
        target = (LPVOID)ADDR_FRAME_FN_105;
    else if (game_version == VER_201)
        target = (LPVOID)ADDR_FRAME_FN_201;
    else
        return;

    if (!QueryPerformanceFrequency(&g_qpc_freq) || g_qpc_freq.QuadPart == 0)
        return;

    // 提高 Sleep 精度到 ~1ms；游戏帧路径本身没有 Sleep，不受影响
    timeBeginPeriod(1);

    if (MH_CreateHook(target, &Detour_FrameFn, (LPVOID *)&fpFrameFn) != MH_OK)
    {
        timeEndPeriod(1);
        return;
    }

    MH_EnableHook(target);
}
