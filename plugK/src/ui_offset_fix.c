/**
 * @file ui_offset_fix.c
 * @brief 禁用打开背包后画面右移
 * @details 背包，角色，技能窗口都生效
 */

#include "pch.h"
#include "ui_offset_fix.h"
#include "config.h"
#include <MinHook.h>
#include <windows.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#define STATUS_BAR_VISIBLE_WIDTH 660
#define STATUS_BAR_EDGE_MAX_INSET 3
#define STATUS_BAR_EDGE_FEATHER_WIDTH 6

// 平滑单峰岩石边缘：可见高度两端内缩 3px，中部逐渐回到 660px。
// 以状态栏 Y 和屏幕底部为边界，使用整数抛物线，不依赖绘制线程状态。
static int StatusBarRightAtY(int y, int barTop, int screenHeight)
{
    LONGLONG span, position, denominator, numerator;
    int protrusion;
    if (y < barTop)
        return STATUS_BAR_VISIBLE_WIDTH;
    if (screenHeight <= barTop)
        return STATUS_BAR_VISIBLE_WIDTH - STATUS_BAR_EDGE_MAX_INSET;
    span = (LONGLONG)screenHeight - barTop - 1;
    if (span <= 0)
        return STATUS_BAR_VISIBLE_WIDTH - STATUS_BAR_EDGE_MAX_INSET;
    position = y - barTop;
    if (position < 0)
        position = 0;
    if (position > span)
        position = span;
    denominator = span * span;
    numerator = 4 * STATUS_BAR_EDGE_MAX_INSET * position * (span - position);
    protrusion = (int)((numerator + denominator / 2) / denominator);
    if (protrusion > STATUS_BAR_EDGE_MAX_INSET)
        protrusion = STATUS_BAR_EDGE_MAX_INSET;
    return STATUS_BAR_VISIBLE_WIDTH - (STATUS_BAR_EDGE_MAX_INSET - protrusion);
}

// 经两版静态分析核对；绘制和命中逻辑共享，地址按版本选择。
typedef struct StatusBarAddresses
{
    DWORD draw, bitmapDraw, rleDraw, tooltip, interfaceAtPoint, childAtCursor;
    DWORD screenWidth, screenHeight, barPointer, getInterface, templateWidth;
} StatusBarAddresses;

static const StatusBarAddresses k_status105 = {
    0x004C31E0, 0x00501150, 0x005033F0, 0x004C3690, 0x004B4800, 0x004B3380,
    0x005485C0, 0x005485C4, 0x0055BBB0, 0x004B35D0, 0x004B365A
};
static const StatusBarAddresses k_status201 = {
    0x004D77A0, 0x00518AA0, 0x0051AD90, 0x004D7C50, 0x004C7BD0, 0x004C6700,
    0x00578B50, 0x00578B54, 0x0058D164, 0x004C6950, 0x004C69DA
};
static const StatusBarAddresses *g_statusAddresses;


typedef int(__fastcall *fn_StatusBarDraw)(void *, void *, int);
typedef void(__fastcall *fn_StatusBarTooltip)(void *, void *, int);
typedef void *(__fastcall *fn_InterfaceAtPoint)(void *, void *, const POINT *);
typedef void *(__fastcall *fn_ChildAtCursor)(void *, void *);
typedef int(__fastcall *fn_StatusSpriteDraw)(void *, void *, int, int, int,
                                           int, int, int, int, int, int, const int *);
typedef BOOL(WINAPI *fn_StatusTextOut)(HDC, int, int, LPCSTR, int);
typedef int(WINAPI *fn_StatusDrawText)(HDC, LPCSTR, int, LPRECT, UINT);

static fn_StatusBarDraw fpStatusBarDraw;
static fn_StatusBarTooltip fpStatusBarTooltip;
static fn_InterfaceAtPoint fpInterfaceAtPoint;
static fn_ChildAtCursor fpChildAtCursor;
static fn_StatusSpriteDraw fpStatusBitmapDraw;
static fn_StatusSpriteDraw fpStatusRleDraw;
static fn_StatusTextOut fpStatusTextOut;
static fn_StatusDrawText fpStatusDrawText;

// Only the calling thread's status-bar draw is clipped. Tooltips temporarily
// leave this scope; other UI, world rendering and keyboard updates are untouched.
static __declspec(thread) int g_statusDrawSurface;
static __declspec(thread) int g_statusDrawHeight;
static __declspec(thread) int g_statusDrawTop;
static __declspec(thread) HRGN g_statusEdgeRegion;
static __declspec(thread) int g_statusSpriteCalls;
static __declspec(thread) int g_statusTextCalls;
static char g_statusLogPath[MAX_PATH];
static SRWLOCK g_statusLogLock = SRWLOCK_INIT;
static LONG g_statusFrameLogs, g_statusHitLogs;
static LONG g_statusEdgeLogs, g_statusEdgeActiveLogs;
static int g_statusLastWidth, g_statusLastHeight;
static POINT g_statusLastHit = {-1, -1};
static void *g_statusLastHitObject;

static void StatusBarLog(const char *format, ...)
{
    FILE *file;
    va_list args;
    SYSTEMTIME now;
    if (!g_statusLogPath[0])
        return;
    AcquireSRWLockExclusive(&g_statusLogLock);
    if (fopen_s(&file, g_statusLogPath, "a") == 0 && file)
    {
        GetLocalTime(&now);
        fprintf(file, "%02u:%02u:%02u.%03u ", now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);
        va_start(args, format);
        vfprintf(file, format, args);
        va_end(args);
        fclose(file);
    }
    ReleaseSRWLockExclusive(&g_statusLogLock);
}

typedef void *(__fastcall *fn_StatusSurfaceLock)(void *, void *, int);
typedef void (__fastcall *fn_StatusSurfaceUnlock)(void *, void *);

static WORD BlendStatusBarRgb565(WORD foreground, WORD background, int foregroundWeight)
{
    int backgroundWeight = 255 - foregroundWeight;
    int red = (((foreground >> 11) * foregroundWeight +
                (background >> 11) * backgroundWeight + 127) / 255);
    int green = ((((foreground >> 5) & 0x3F) * foregroundWeight +
                  ((background >> 5) & 0x3F) * backgroundWeight + 127) / 255);
    int blue = (((foreground & 0x1F) * foregroundWeight +
                 (background & 0x1F) * backgroundWeight + 127) / 255);
    return (WORD)((red << 11) | (green << 5) | blue);
}

// The game surface wrapper exposes the same lock/unlock pair used by the
// native sprite routines. Blend only inside the already-hidden boundary so
// pixels belonging to another window remain untouched outside the crop.
static void BlendStatusBarEdge(void *surface)
{
    void **vtable;
    BYTE *pixels;
    int surfaceWidth, surfaceHeight, bitsPerPixel, pitchScale, pitch;
    LONGLONG scaledPitch;
    int top, bottom, y;
    if (!surface)
        return;
    surfaceWidth = *(int *)((BYTE *)surface + 0x0C);
    surfaceHeight = *(int *)((BYTE *)surface + 0x10);
    bitsPerPixel = *(BYTE *)((BYTE *)surface + 0x18);
    pitchScale = *(short *)((BYTE *)surface + 0x1F);
    pitch = *(int *)((BYTE *)surface + 0x21);
    if (surfaceWidth <= STATUS_BAR_VISIBLE_WIDTH || surfaceHeight <= 0 ||
        bitsPerPixel != 16 || pitchScale <= 0 || pitch <= 0)
    {
        if (g_statusEdgeLogs < 4)
        {
            ++g_statusEdgeLogs;
            StatusBarLog("edge blend skipped surface=%p size=%dx%d bpp=%d scale=%d pitch=%d\n",
                surface, surfaceWidth, surfaceHeight, bitsPerPixel, pitchScale, pitch);
        }
        return;
    }
    scaledPitch = (LONGLONG)pitch * pitchScale;
    if (scaledPitch < (LONGLONG)surfaceWidth * 2 ||
        scaledPitch > (LONGLONG)surfaceHeight * surfaceWidth * 8)
        return;
    pitch = (int)scaledPitch;
    vtable = *(void ***)surface;
    if (!vtable || !vtable[8] || !vtable[9])
        return;
    pixels = (BYTE *)((fn_StatusSurfaceLock)vtable[8])(surface, NULL, 0);
    if (!pixels)
    {
        if (g_statusEdgeLogs < 4)
        {
            ++g_statusEdgeLogs;
            StatusBarLog("edge blend lock failed surface=%p\n", surface);
        }
        return;
    }
    if (g_statusEdgeActiveLogs < 2)
    {
        ++g_statusEdgeActiveLogs;
        StatusBarLog("edge blend active surface=%p size=%dx%d pitch=%d\n",
            surface, surfaceWidth, surfaceHeight, pitch);
    }
    top = max(0, g_statusDrawTop);
    bottom = min(g_statusDrawHeight, surfaceHeight);
    for (y = top; y < bottom; ++y)
    {
        int edgeRight = StatusBarRightAtY(y, g_statusDrawTop, g_statusDrawHeight);
        int first = max(0, edgeRight - STATUS_BAR_EDGE_FEATHER_WIDTH);
        int x;
        WORD *row;
        if (edgeRight <= first || edgeRight >= surfaceWidth)
            continue;
        row = (WORD *)(pixels + y * pitch);
        for (x = first; x < edgeRight; ++x)
        {
            int distance = edgeRight - x;
            int foregroundWeight = (distance * 255 +
                                     STATUS_BAR_EDGE_FEATHER_WIDTH / 2) /
                                    STATUS_BAR_EDGE_FEATHER_WIDTH;
            if (foregroundWeight > 255)
                foregroundWeight = 255;
            row[x] = BlendStatusBarRgb565(row[x], row[edgeRight], foregroundWeight);
        }
    }
    ((fn_StatusSurfaceUnlock)vtable[9])(surface, NULL);
}

static void InitializeStatusBarLog(void)
{
    HMODULE module;
    char *slash;
    FILE *file;
    DWORD length;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)&InitializeStatusBarLog, &module))
        return;
    length = GetModuleFileNameA(module, g_statusLogPath, MAX_PATH);
    if (!length || length >= MAX_PATH ||
        !(slash = strrchr(g_statusLogPath, '\\')))
    {
        g_statusLogPath[0] = 0;
        return;
    }
    if ((size_t)(slash + 1 - g_statusLogPath) + sizeof("PlugK_StatusBar.log") > MAX_PATH)
    {
        g_statusLogPath[0] = 0;
        return;
    }
    strcpy_s(slash + 1, MAX_PATH - (size_t)(slash + 1 - g_statusLogPath), "PlugK_StatusBar.log");
    if (fopen_s(&file, g_statusLogPath, "w") == 0 && file)
    {
        fputs("PlugK status bar hide-buttons diagnostics (660px)\n", file);
        fclose(file);
    }
}

static void LogStatusBarControls(void)
{
    typedef void *(__stdcall *fn_GetInterface)(int);
    fn_GetInterface getInterface = (fn_GetInterface)g_statusAddresses->getInterface;
    const int ids[] = {9, 10, 11, 12, 13, 14, 15, 16, 38, 39};
    size_t i;
    for (i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i)
    {
        BYTE *control = getInterface(ids[i]);
        if (control)
            StatusBarLog("control id=%d ptr=%p rect=(%d,%d,%d,%d)\n", ids[i], control,
                *(int *)(control + 0x14), *(int *)(control + 0x18),
                *(int *)(control + 0x1C), *(int *)(control + 0x20));
    }
}

static BOOL StatusBarButtonsActive(void)
{
    return *(int *)g_statusAddresses->screenWidth >= 800;
}

static BOOL IsMainStatusBar(void *object)
{
    return object && object == *(void **)g_statusAddresses->barPointer;
}

static int __fastcall Detour_StatusBarDraw(void *object, void *edx, int surface)
{
    int oldSurface = g_statusDrawSurface;
    int oldHeight = g_statusDrawHeight;
    int oldTop = g_statusDrawTop;
    HRGN oldRegion = g_statusEdgeRegion;
    int oldSprites = g_statusSpriteCalls, oldTexts = g_statusTextCalls;
    int screenWidth = *(int *)g_statusAddresses->screenWidth, screenHeight = *(int *)g_statusAddresses->screenHeight;
    BOOL logFrame = g_statusFrameLogs < 8 || screenWidth != g_statusLastWidth ||
                    screenHeight != g_statusLastHeight;
    int result;
    if (logFrame)
    {
        ++g_statusFrameLogs;
        g_statusLastWidth = screenWidth;
        g_statusLastHeight = screenHeight;
        StatusBarLog("draw begin #%ld screen=%dx%d active=%d bar=%p surface=%p rect=(%d,%d,%d,%d)\n",
            g_statusFrameLogs, screenWidth, screenHeight, StatusBarButtonsActive(), object, (void *)surface,
            *(int *)((BYTE *)object + 0x14), *(int *)((BYTE *)object + 0x18),
            *(int *)((BYTE *)object + 0x1C), *(int *)((BYTE *)object + 0x20));
    }
    g_statusSpriteCalls = g_statusTextCalls = 0;
    g_statusEdgeRegion = NULL;
    if (StatusBarButtonsActive() && IsMainStatusBar(object))
    {
        g_statusDrawSurface = surface;
        g_statusDrawHeight = *(int *)g_statusAddresses->screenHeight;
        g_statusDrawTop = *(int *)((BYTE *)object + 0x18);
    }
    result = fpStatusBarDraw(object, edx, surface);
    if (g_statusDrawSurface)
        BlendStatusBarEdge((void *)(ULONG_PTR)(DWORD)surface);
    if (logFrame)
    {
        StatusBarLog("draw end #%ld result=%d sprite_calls=%d text_calls=%d\n",
                     g_statusFrameLogs, result, g_statusSpriteCalls, g_statusTextCalls);
        LogStatusBarControls();
    }
    g_statusSpriteCalls = oldSprites;
    g_statusTextCalls = oldTexts;
    g_statusDrawSurface = oldSurface;
    g_statusDrawHeight = oldHeight;
    g_statusDrawTop = oldTop;
    if (g_statusEdgeRegion)
        DeleteObject(g_statusEdgeRegion);
    g_statusEdgeRegion = oldRegion;
    return result;
}

static void __fastcall Detour_StatusBarTooltip(void *object, void *edx, int surface)
{
    int oldSurface = g_statusDrawSurface;
    g_statusDrawSurface = 0;
    fpStatusBarTooltip(object, edx, surface);
    g_statusDrawSurface = oldSurface;
}

static int DrawStatusSprite(fn_StatusSpriteDraw original, void *object, void *edx,
                            int surface, int x, int y, int width, int height,
                            int sourceX, int sourceY, int mode, int alpha,
                            const int *clip)
{
    // The native clip is x/y/width/height/area, not a Win32 RECT.
    int clipped[5];
    if (g_statusDrawSurface && surface == g_statusDrawSurface)
    {
        ++g_statusSpriteCalls;
        LONGLONG left = max(0, x), top = max(0, y);
        LONGLONG right = min(STATUS_BAR_VISIBLE_WIDTH, (LONGLONG)x + width);
        LONGLONG bottom = min(g_statusDrawHeight, (LONGLONG)y + height);
        if (clip)
        {
            if (clip[2] <= 0 || clip[3] <= 0)
                return 0;
            left = max(left, clip[0]);
            top = max(top, clip[1]);
            right = min(right, (LONGLONG)clip[0] + clip[2]);
            bottom = min(bottom, (LONGLONG)clip[1] + clip[3]);
        }
        if (left >= right || top >= bottom)
            return 0;
        // 完整左侧只提交一次；边缘按连续同宽行合并，避免重叠绘制造成透明度加深。
        // 保留源坐标和原混合模式，抛物线量化后最多形成 7 个连续条带。
        if (right > STATUS_BAR_VISIBLE_WIDTH - STATUS_BAR_EDGE_MAX_INSET)
        {
            int result = 0;
            int edgeLeft = max((int)left, STATUS_BAR_VISIBLE_WIDTH - STATUS_BAR_EDGE_MAX_INSET);
            int row = (int)top;
            if (left < edgeLeft)
            {
                clipped[0] = (int)left; clipped[1] = (int)top;
                clipped[2] = edgeLeft - (int)left; clipped[3] = (int)(bottom - top);
                clipped[4] = clipped[2] * clipped[3];
                result = original(object, edx, surface, x, y, width, height,
                                  sourceX, sourceY, mode, alpha, clipped);
            }
            while (row < bottom)
            {
                int edgeRight = min((int)right,
                    StatusBarRightAtY(row, g_statusDrawTop, g_statusDrawHeight));
                int next = row + 1;
                int partResult;
                while (next < bottom &&
                       min((int)right, StatusBarRightAtY(next, g_statusDrawTop,
                                                         g_statusDrawHeight)) == edgeRight)
                    ++next;
                if (edgeLeft < edgeRight)
                {
                    clipped[0] = edgeLeft; clipped[1] = row;
                    clipped[2] = edgeRight - edgeLeft; clipped[3] = next - row;
                    clipped[4] = clipped[2] * clipped[3];
                    partResult = original(object, edx, surface, x, y, width, height,
                                          sourceX, sourceY, mode, alpha, clipped);
                    if (partResult)
                        result = partResult;
                }
                row = next;
            }
            return result;
        }
        clipped[0] = (int)left;
        clipped[1] = (int)top;
        clipped[2] = (int)(right - left);
        clipped[3] = (int)(bottom - top);
        clipped[4] = clipped[2] * clipped[3];
        clip = clipped;
    }
    return original(object, edx, surface, x, y, width, height,
                    sourceX, sourceY, mode, alpha, clip);
}

static int __fastcall Detour_StatusBitmapDraw(void *object, void *edx,
    int surface, int x, int y, int width, int height,
    int sourceX, int sourceY, int mode, int alpha, const int *clip)
{
    return DrawStatusSprite(fpStatusBitmapDraw, object, edx, surface,
        x, y, width, height, sourceX, sourceY, mode, alpha, clip);
}

static int __fastcall Detour_StatusRleDraw(void *object, void *edx,
    int surface, int x, int y, int width, int height,
    int sourceX, int sourceY, int mode, int alpha, const int *clip)
{
    return DrawStatusSprite(fpStatusRleDraw, object, edx, surface,
        x, y, width, height, sourceX, sourceY, mode, alpha, clip);
}

static BOOL ClipStatusBarText(HDC dc)
{
    if (!g_statusEdgeRegion)
    {
        HRGN region = CreateRectRgn(0, 0, STATUS_BAR_VISIBLE_WIDTH, g_statusDrawHeight);
        HRGN notch = CreateRectRgn(0, 0, 0, 0);
        int row = max(0, g_statusDrawTop);
        if (!region || !notch)
        {
            if (region) DeleteObject(region);
            if (notch) DeleteObject(notch);
            return FALSE;
        }
        while (row < g_statusDrawHeight)
        {
            int edgeRight = StatusBarRightAtY(row, g_statusDrawTop, g_statusDrawHeight);
            int next = row + 1;
            while (next < g_statusDrawHeight &&
                   StatusBarRightAtY(next, g_statusDrawTop, g_statusDrawHeight) == edgeRight)
                ++next;
            if (edgeRight < STATUS_BAR_VISIBLE_WIDTH &&
                (!SetRectRgn(notch, edgeRight, row, STATUS_BAR_VISIBLE_WIDTH, next) ||
                 CombineRgn(region, region, notch, RGN_DIFF) == ERROR))
            {
                DeleteObject(notch);
                DeleteObject(region);
                return FALSE;
            }
            row = next;
        }
        DeleteObject(notch);
        g_statusEdgeRegion = region;
    }
    // 区域在本次主绘制内惰性创建、复用，主绘制返回后释放。
    return ExtSelectClipRgn(dc, g_statusEdgeRegion, RGN_AND) != ERROR;
}

static BOOL WINAPI Detour_StatusTextOut(HDC dc, int x, int y, LPCSTR text, int count)
{
    int saved;
    BOOL result;
    if (!g_statusDrawSurface)
        return fpStatusTextOut(dc, x, y, text, count);
    ++g_statusTextCalls;
    saved = SaveDC(dc);
    if (!saved)
        return FALSE;
    if (!ClipStatusBarText(dc))
    {
        RestoreDC(dc, saved);
        return FALSE;
    }
    result = fpStatusTextOut(dc, x, y, text, count);
    RestoreDC(dc, saved);
    return result;
}

static int WINAPI Detour_StatusDrawText(HDC dc, LPCSTR text, int count, LPRECT rect, UINT format)
{
    int saved;
    int result;
    if (!g_statusDrawSurface || (format & DT_CALCRECT))
        return fpStatusDrawText(dc, text, count, rect, format);
    ++g_statusTextCalls;
    saved = SaveDC(dc);
    if (!saved)
        return 0;
    if (!ClipStatusBarText(dc))
    {
        RestoreDC(dc, saved);
        return 0;
    }
    result = fpStatusDrawText(dc, text, count, rect, format);
    RestoreDC(dc, saved);
    return result;
}

static void *__fastcall Detour_InterfaceAtPoint(void *object, void *edx, const POINT *point)
{
    BYTE *bar = *(BYTE **)g_statusAddresses->barPointer;
    void *result;
    int oldWidth, screenHeight;
    if (!bar || !StatusBarButtonsActive())
        return fpInterfaceAtPoint(object, edx, point);
    // Both the special bottom-right 100x100 test and the ordinary UI rectangle
    // read this width. Narrow it only for hit testing, never for layout/drawing.
    oldWidth = *(int *)(bar + 0x1C);
    screenHeight = *(int *)g_statusAddresses->screenHeight;
    *(int *)(bar + 0x1C) = min(oldWidth, point
        ? StatusBarRightAtY(point->y, *(int *)(bar + 0x18), screenHeight)
        : STATUS_BAR_VISIBLE_WIDTH);
    result = fpInterfaceAtPoint(object, edx, point);
    *(int *)(bar + 0x1C) = oldWidth;
    if (point && point->x >= StatusBarRightAtY(point->y, *(int *)(bar + 0x18), screenHeight) &&
        point->y >= *(int *)(bar + 0x18) && g_statusHitLogs < 20 &&
        (point->x != g_statusLastHit.x || point->y != g_statusLastHit.y ||
         result != g_statusLastHitObject))
    {
        g_statusLastHit = *point;
        g_statusLastHitObject = result;
        ++g_statusHitLogs;
        StatusBarLog("hit #%ld cursor=(%ld,%ld) original_width=%d selected=%p bar=%p\n",
            g_statusHitLogs, point->x, point->y, oldWidth, result, bar);
    }
    return result;
}

static void *__fastcall Detour_ChildAtCursor(void *object, void *edx)
{
    POINT cursor;
    if (IsMainStatusBar(object) && StatusBarButtonsActive() &&
        GetCursorPos(&cursor) &&
        cursor.x >= StatusBarRightAtY(cursor.y, *(int *)((BYTE *)object + 0x18),
                                      *(int *)g_statusAddresses->screenHeight))
    {
        *(void **)((BYTE *)object + 0xA8) = NULL;
        return NULL;
    }
    return fpChildAtCursor(object, edx);
}

static BOOL PatchStatusBarTemplateWidth(int width)
{
    DWORD oldProtect;
    int *immediate = (int *)g_statusAddresses->templateWidth;
    if (!VirtualProtect(immediate, sizeof(*immediate), PAGE_EXECUTE_READWRITE, &oldProtect))
        return FALSE;
    *immediate = width;
    VirtualProtect(immediate, sizeof(*immediate), oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), immediate, sizeof(*immediate));
    return TRUE;
}

void Mod_StatusBar_HideButtons_Init(int game_version)
{
    struct StatusHook
    {
        LPVOID target;
        LPVOID detour;
        LPVOID *original;
    };
    struct StatusHook hooks[] = {
        {NULL, Detour_StatusBarDraw, (LPVOID *)&fpStatusBarDraw},
        {NULL, Detour_StatusBitmapDraw, (LPVOID *)&fpStatusBitmapDraw},
        {NULL, Detour_StatusRleDraw, (LPVOID *)&fpStatusRleDraw},
        {NULL, Detour_StatusBarTooltip, (LPVOID *)&fpStatusBarTooltip},
        {NULL, Detour_InterfaceAtPoint, (LPVOID *)&fpInterfaceAtPoint},
        {NULL, Detour_ChildAtCursor, (LPVOID *)&fpChildAtCursor},
        {NULL, Detour_StatusTextOut, (LPVOID *)&fpStatusTextOut},
        {NULL, Detour_StatusDrawText, (LPVOID *)&fpStatusDrawText}
    };
    const size_t hookCount = sizeof(hooks) / sizeof(hooks[0]);
    size_t created = 0, enabled = 0;
    BOOL patched = FALSE;
    int oldTemplateWidth = 1024;
    MH_STATUS status;
    HMODULE gdi, user;
    if (!g_pk_config.hide_status_bar_buttons)
        return;
    InitializeStatusBarLog();
    StatusBarLog("init version=%d enabled=%d res_enabled=%d configured=%dx%d pid=%lu\n",
        game_version, g_pk_config.hide_status_bar_buttons, g_pk_config.res_enabled,
        g_pk_config.res_width, g_pk_config.res_height, GetCurrentProcessId());
    if (game_version != 105 && game_version != 201)
    {
        StatusBarLog("unsupported version; no patches applied\n");
        return;
    }
    g_statusAddresses = game_version == 105 ? &k_status105 : &k_status201;
    hooks[0].target = (LPVOID)g_statusAddresses->draw;
    hooks[1].target = (LPVOID)g_statusAddresses->bitmapDraw;
    hooks[2].target = (LPVOID)g_statusAddresses->rleDraw;
    hooks[3].target = (LPVOID)g_statusAddresses->tooltip;
    hooks[4].target = (LPVOID)g_statusAddresses->interfaceAtPoint;
    hooks[5].target = (LPVOID)g_statusAddresses->childAtCursor;
    StatusBarLog("addresses width=%p height=%p bar=%p get_interface=%p template=%p\n",
        (void *)g_statusAddresses->screenWidth, (void *)g_statusAddresses->screenHeight,
        (void *)g_statusAddresses->barPointer, (void *)g_statusAddresses->getInterface,
        (void *)g_statusAddresses->templateWidth);
    gdi = GetModuleHandleA("gdi32.dll");
    user = GetModuleHandleA("user32.dll");
    if (!gdi || !user)
    {
        StatusBarLog("missing GDI/User32 module; no patches applied\n");
        return;
    }
    hooks[6].target = (LPVOID)GetProcAddress(gdi, "TextOutA");
    hooks[7].target = (LPVOID)GetProcAddress(user, "DrawTextA");
    for (; created < hookCount; ++created)
    {
        status = hooks[created].target
            ? MH_CreateHook(hooks[created].target, hooks[created].detour, hooks[created].original)
            : MH_ERROR_NOT_EXECUTABLE;
        StatusBarLog("create hook index=%u target=%p status=%d trampoline=%p\n",
            (unsigned)created, hooks[created].target, status, *hooks[created].original);
        if (status != MH_OK)
            goto fail;
    }
    if (g_pk_config.res_enabled && g_pk_config.res_width >= 800 &&
        g_pk_config.res_width != 800 && g_pk_config.res_width != 1024)
    {
        oldTemplateWidth = *(int *)g_statusAddresses->templateWidth;
        if (!PatchStatusBarTemplateWidth(g_pk_config.res_width))
        {
            StatusBarLog("template patch failed error=%lu\n", GetLastError());
            goto fail;
        }
        patched = TRUE;
        StatusBarLog("template width patched %d -> %d\n", oldTemplateWidth, g_pk_config.res_width);
    }
    for (; enabled < hookCount; ++enabled)
    {
        status = MH_EnableHook(hooks[enabled].target);
        StatusBarLog("enable hook index=%u status=%d\n", (unsigned)enabled, status);
        if (status != MH_OK)
            goto fail;
    }
    OutputDebugStringA("PlugK: status bar buttons hidden (660px).\n");
    StatusBarLog("ready; edge=rock-v2 arc max-inset=3 height=screen feather=6px rgb565; draw logs limited to first 8 frames and resolution changes; hit logs limited to 20\n");
    return;

fail:
    StatusBarLog("initialization failed; rolling back created=%u enabled=%u\n",
                 (unsigned)created, (unsigned)enabled);
    while (enabled)
        MH_DisableHook(hooks[--enabled].target);
    while (created)
    {
        --created;
        MH_RemoveHook(hooks[created].target);
        *hooks[created].original = NULL;
    }
    if (patched)
        StatusBarLog("template rollback success=%d\n", PatchStatusBarTemplateWidth(oldTemplateWidth));
    OutputDebugStringA("PlugK: status bar hide-buttons initialization failed; rolled back.\n");
}

void Mod_UI_offset_fix_init(int game_version)
{
    // 1. 检查配置，如果未开启则直接返回
    // 默认关闭，需用户手动开启
    if (!g_pk_config.ui_keep_center)
    {
        return;
    }

    DWORD targetAddress = 0;

    // 2. 根据版本选择地址
    if (game_version == 105)
    {
        // 1.05 程序修改点
        targetAddress = 0x0047AD53;
    }
    else if (game_version == 201)
    {
        // 2.01 程序修改点
        targetAddress = 0x004898C3;
    }
    else
    {
        // 不支持的版本
        return;
    }

    // 3. 准备补丁数据
    // 原始指令 (猜测): C1 F8 02 (SAR EAX, 2) -> 3 字节
    // 目标指令:       D1 F8    (SAR EAX, 1) -> 2 字节
    // 填充指令:       90       (NOP)        -> 1 字节
    BYTE patch[] = {0xD1, 0xF8, 0x90};

    // 4. 执行内存修改
    DWORD oldProtect;
    if (VirtualProtect((LPVOID)targetAddress, sizeof(patch), PAGE_EXECUTE_READWRITE, &oldProtect))
    {
        // 写入补丁
        memcpy((void *)targetAddress, patch, sizeof(patch));

        // 恢复内存保护属性
        VirtualProtect((LPVOID)targetAddress, sizeof(patch), oldProtect, &oldProtect);

        // 可选：输出调试信息
        // OutputDebugStringA("PlugK: UI Center Fix Applied.");
    }
}

void Mod_Screen_shake_effect_init(int game_version)
{
    // 1. 检查配置
    if (!g_pk_config.disable_screen_shake)
    {
        return;
    }

    DWORD targetAddress = 0;

    // 2. 根据版本选择地址
    if (game_version == 105)
    {
        // 00407769 | A1 C4855500 | mov eax, dword ptr ds:[5585C4]
        targetAddress = 0x00407769;
    }
    else if (game_version == 201)
    {
        // 0040E829 | A1 44955800 | mov eax, dword ptr ds:[589544]
        targetAddress = 0x0040E829;
    }
    else
    {
        return;
    }

    // 3. 准备补丁数据
    // 原始指令长度为 5 字节
    // 目标指令: mov eax, 0 -> B8 00 00 00 00 (5 字节)
    BYTE patch[] = {0xB8, 0x00, 0x00, 0x00, 0x00};

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
