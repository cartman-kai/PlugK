// 设置为 Windows 子系统，隐藏控制台
#pragma comment(linker, "/subsystem:windows /ENTRY:mainCRTStartup")

#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <d3d9.h>
#include <tchar.h>
#include <stdio.h>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <regex>
#include <map>

#include "imgui.h"
#include "imgui_impl_dx9.h"
#include "imgui_impl_win32.h"

// 引入配置结构
extern "C"
{
#include "../inc/config.h"
}

#pragma comment(lib, "d3d9.lib")

// --- 全局变量 ---
static LPDIRECT3D9 g_pD3D = NULL;
static LPDIRECT3DDEVICE9 g_pd3dDevice = NULL;
static D3DPRESENT_PARAMETERS g_d3dpp = {};
static char g_iniPath[MAX_PATH] = {0};

// --- 状态变量 ---
// 用于记录当前左侧选中的分类
static std::string g_currentTab = "UI";

// --- 函数声明 ---
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void ResetDevice();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// --- 辅助功能 ---

float GetDPIScale()
{
    HDC hdc = GetDC(NULL);
    int dpi = GetDeviceCaps(hdc, LOGPIXELSY);
    ReleaseDC(NULL, hdc);
    // 基础缩放，如果觉得太大可以改为 / 96.0f 后再乘一个系数
    float scale = (float)dpi / 96.0f;
    if (scale < 1.0f)
        scale = 1.0f;
    return scale;
}

void GetKeyName(int key, char *buffer, int bufSize)
{
    if (key == 0)
    {
        snprintf(buffer, bufSize, "未设置 (None)");
        return;
    }
    unsigned int scanCode = MapVirtualKey(key, MAPVK_VK_TO_VSC);
    switch (key)
    {
    case VK_LEFT:
    case VK_UP:
    case VK_RIGHT:
    case VK_DOWN:
    case VK_PRIOR:
    case VK_NEXT:
    case VK_END:
    case VK_HOME:
    case VK_INSERT:
    case VK_DELETE:
    case VK_DIVIDE:
    case VK_NUMLOCK:
        scanCode |= 0x100;
        break;
    }
    if (GetKeyNameTextA(scanCode << 16, buffer, bufSize) == 0)
        snprintf(buffer, bufSize, "VK_%d", key);
}

// Section 转中文显示，用于侧边栏
const char *GetSectionTitleCN(const char *sec)
{
    if (strcmp(sec, "UI") == 0)
        return "  界面显示  ";
    if (strcmp(sec, "Inventory") == 0)
        return "  背包与仓库  ";
    if (strcmp(sec, "Item&Shop") == 0)
        return "  物品与商店  ";
    if (strcmp(sec, "Equipment") == 0)
        return "  装备与合成  ";
    if (strcmp(sec, "Hotkeys") == 0)
        return "  快捷按键  ";
    return sec;
}

// 图标辅助（如果有图标字体可以使用，这里用文字代替）
const char *GetSectionIcon(const char *sec)
{
    // 简单的视觉装饰
    return "::";
}

// --- 配置保存逻辑 ---
void SaveConfigSafe()
{
    std::ifstream inFile(g_iniPath);
    std::vector<std::string> lines;
    std::string line;

    // 如果文件存在，先读取内容
    if (inFile.is_open())
    {
        while (std::getline(inFile, line))
            lines.push_back(line);
        inFile.close();
    }

    auto UpdateValueInLines = [&](const char *section, const char *key, int val)
    {
        std::string sSection = std::string("[") + section + "]";
        bool inCorrectSection = false;
        bool keyFound = false;

        for (auto &l : lines)
        {
            if (l.find(sSection) != std::string::npos)
            {
                inCorrectSection = true;
                continue;
            }
            if (inCorrectSection && l.find("[") != std::string::npos && l.find("]") != std::string::npos)
            {
                if (l.find(sSection) == std::string::npos)
                    inCorrectSection = false;
            }

            if (inCorrectSection)
            {
                // 正则匹配 Key=Value
                std::regex re("^\\s*" + std::string(key) + "\\s*=\\s*(-?\\d+)(.*)");
                std::smatch match;
                if (std::regex_search(l, match, re))
                {
                    char buf[256];
                    // 保留原有注释
                    snprintf(buf, sizeof(buf), "%s=%d%s", key, val, match[2].str().c_str());
                    l = buf;
                    keyFound = true;
                    return; // 找到并更新后返回
                }
            }
        }

        // 如果整个循环结束都没找到该 Key（可能是新 Key），此处简化处理：
        // 实际应用中可能需要追加到 Section 末尾，这里暂略，假设 config.c 已补全 Key。
    };

#define X(type, name, sec, key, val, desc) UpdateValueInLines(sec, key, (int)g_pk_config.name);
#include "../inc/config_def.h"
#undef X

    std::ofstream outFile(g_iniPath);
    for (const auto &l : lines)
        outFile << l << "\n";
}

// --- UI 渲染相关函数 ---

void SetupUIStyle(float dpiScale)
{
    ImGuiStyle &style = ImGui::GetStyle();
    ImGui::StyleColorsDark();

    style.WindowRounding = 8.0f;
    style.ChildRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 6.0f;
    style.GrabRounding = 4.0f;
    style.ScrollbarSize = 12.0f;
    style.ScrollbarRounding = 9.0f;

    // 缩放间距
    style.ItemSpacing = ImVec2(10 * dpiScale, 8 * dpiScale);
    style.FramePadding = ImVec2(8 * dpiScale, 6 * dpiScale);
    style.WindowPadding = ImVec2(10 * dpiScale, 10 * dpiScale);

    ImVec4 *colors = style.Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0.12f, 0.12f, 0.14f, 1.00f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.16f, 0.16f, 0.18f, 1.00f); // 稍微亮一点的子区域背景
    colors[ImGuiCol_Header] = ImVec4(0.24f, 0.24f, 0.26f, 1.00f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.30f, 0.30f, 0.33f, 1.00f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.22f, 0.45f, 0.65f, 1.00f); // 蓝色系按钮
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.28f, 0.55f, 0.78f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.15f, 0.35f, 0.55f, 1.00f);
    colors[ImGuiCol_CheckMark] = ImVec4(0.26f, 0.98f, 0.45f, 1.00f);
}

// 渲染按键绑定按钮
void RenderHotkeyButton(const char *label, int *keyVar)
{
    char keyName[64];
    GetKeyName(*keyVar, keyName, sizeof(keyName));

    ImGui::PushID(label);

    // 使用 Child 作为一个小的 Row 来承载这一行，增加美观度
    ImGui::BeginGroup();

    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s", label);

    ImGui::SameLine();

    // 将按钮推到最右侧
    float availX = ImGui::GetContentRegionAvail().x;
    float btnWidth = 140.0f;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + availX - btnWidth);

    if (*keyVar != 0)
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.9f, 0.2f, 1.0f));

    if (ImGui::Button(keyName, ImVec2(btnWidth, 0)))
        ImGui::OpenPopup("BindKeyPopup");

    if (*keyVar != 0)
        ImGui::PopStyleColor();

    ImGui::EndGroup();

    // 悬停效果：画一个淡淡的背景框（可选优化）
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("点击修改按键");
    }

    if (ImGui::BeginPopupModal("BindKeyPopup", NULL, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("\n请按下一个按键...\n\n");
        ImGui::Separator();
        ImGui::TextDisabled("ESC = 取消 | Backspace = 清除");

        ImGui::Dummy(ImVec2(0, 10));

        for (int k = 0x08; k <= 0xFE; k++)
        {
            if (k == VK_LBUTTON || k == VK_RBUTTON || k == VK_MBUTTON)
                continue;
            if (GetAsyncKeyState(k) & 0x0001)
            {
                if (k == VK_ESCAPE)
                    ImGui::CloseCurrentPopup();
                else if (k == VK_BACK)
                {
                    *keyVar = 0;
                    ImGui::CloseCurrentPopup();
                }
                else
                {
                    *keyVar = k;
                    ImGui::CloseCurrentPopup();
                }
                break;
            }
        }

        // 底部关闭
        if (ImGui::Button("取消", ImVec2(120, 0)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::PopID();
}

// 分辨率预设
struct ResPreset
{
    int w;
    int h;
    const char *label;
};
static const ResPreset g_resPresets[] = {
    {1280, 720, "1280 x 720 (HD)"},
    {1366, 768, "1366 x 768"},
    {1440, 900, "1440 x 900"},
    {1600, 900, "1600 x 900"},
    {1920, 1080, "1920 x 1080 (FHD)"},
    {2560, 1440, "2560 x 1440 (2K)"},
    {3840, 2160, "3840 x 2160 (4K)"},
};

// 渲染右侧的具体设置内容
// 参数 filterSection: 只渲染属于该 Section 的配置项
void RenderContent(const char *filterSection)
{
    // 如果是“快捷键”页面，单独处理
    if (strcmp(filterSection, "Hotkeys") == 0)
    {
        ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "快捷键配置");
        ImGui::Separator();
        ImGui::TextDisabled("提示：组合键需在游戏中按 Ctrl + 键位 生效");
        ImGui::Spacing();
        ImGui::Spacing();

// #define TYPE_KEY 3
#define X(type, name, sec, key, val, desc)           \
    if (type == TYPE_KEY)                            \
    {                                                \
        RenderHotkeyButton(desc, &g_pk_config.name); \
        ImGui::Separator();                          \
    }
#include "../inc/config_def.h"
#undef X
        return;
    }

    // 常规设置页面
    ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "%s 设置", GetSectionTitleCN(filterSection));
    ImGui::Separator();
    ImGui::Spacing();

#define X(type, name, sec, key, val, desc)                                                            \
    if (strcmp(sec, filterSection) == 0 && type != TYPE_KEY)                                          \
    {                                                                                                 \
        /* 跳过分辨率宽高，单独绘制 */                                                                \
        if (strcmp(key, "Width") == 0 || strcmp(key, "Height") == 0)                                  \
            goto skip_##name;                                                                         \
                                                                                                      \
        if (type == TYPE_BOOL)                                                                        \
        {                                                                                             \
            bool v = (bool)g_pk_config.name;                                                          \
            if (ImGui::Checkbox(desc, &v))                                                            \
                g_pk_config.name = v;                                                                 \
                                                                                                      \
            /* 分辨率特殊逻辑 */                                                                      \
            if (strcmp(sec, "UI") == 0 && strcmp(key, "Enabled") == 0 && v)                           \
            {                                                                                         \
                ImGui::Indent(24.0f);                                                                 \
                ImGui::Spacing();                                                                     \
                ImGui::Text("分辨率设置:");                                                           \
                const char *comboPreview = "自定义分辨率";                                            \
                for (const auto &p : g_resPresets)                                                    \
                    if (g_pk_config.res_width == p.w && g_pk_config.res_height == p.h)                \
                        comboPreview = p.label;                                                       \
                                                                                                      \
                ImGui::SetNextItemWidth(300);                                                         \
                if (ImGui::BeginCombo("##Res", comboPreview))                                         \
                {                                                                                     \
                    for (const auto &p : g_resPresets)                                                \
                    {                                                                                 \
                        bool isSel = (g_pk_config.res_width == p.w && g_pk_config.res_height == p.h); \
                        if (ImGui::Selectable(p.label, isSel))                                        \
                        {                                                                             \
                            g_pk_config.res_width = p.w;                                              \
                            g_pk_config.res_height = p.h;                                             \
                        }                                                                             \
                        if (isSel)                                                                    \
                            ImGui::SetItemDefaultFocus();                                             \
                    }                                                                                 \
                    ImGui::EndCombo();                                                                \
                }                                                                                     \
                ImGui::SameLine();                                                                    \
                ImGui::TextDisabled("  或手动输入:");                                                 \
                                                                                                      \
                ImGui::SetNextItemWidth(100);                                                         \
                ImGui::InputInt("*", &g_pk_config.res_width, 0);                                      \
                ImGui::SameLine();                                                                    \
                ImGui::SetNextItemWidth(100);                                                         \
                ImGui::InputInt("", &g_pk_config.res_height, 0);                                      \
                ImGui::Unindent(24.0f);                                                               \
                ImGui::Spacing();                                                                     \
            }                                                                                         \
        }                                                                                             \
        else if (type == TYPE_INT)                                                                    \
        {                                                                                             \
            ImGui::InputInt(desc, &g_pk_config.name);                                                 \
        }                                                                                             \
        ImGui::Dummy(ImVec2(0, 5));                                                                   \
        skip_##name :;                                                                                \
    }

#include "../inc/config_def.h"
#undef X
}

void RenderMainUI(ImGuiIO &io)
{
    // 获取当前 DPI 缩放比例
    float dpiScale = GetDPIScale();

    // --- 动态布局参数 ---
    float sidebarWidth = 180.0f * dpiScale; // 增加基础宽度并随 DPI 缩放
    float footerHeight = 70.0f * dpiScale;  // 底部栏高度随 DPI 缩放
    float sidebarBtnHeight = 45.0f * dpiScale;
    float saveBtnW = 220.0f * dpiScale;
    float saveBtnH = 40.0f * dpiScale;

    // 全屏主窗口
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("MainPanel", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus);

    // --- 上半部分：工作区 ---
    if (ImGui::BeginChild("WorkArea", ImVec2(0, -footerHeight), false))
    {
        // 1. 左侧：侧边栏
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.10f, 0.10f, 0.11f, 1.00f));
        ImGui::BeginChild("Sidebar", ImVec2(sidebarWidth, 0), true);

        //ImGui::Spacing();
        //ImGui::TextDisabled("  配置工具  ");
        //ImGui::Separator();
        //ImGui::Spacing();

        auto SidebarItem = [&](const char *secID)
        {
            bool isSelected = (g_currentTab == secID);
            if (isSelected)
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.26f, 0.59f, 0.98f, 1.00f));

            // 使用动态高度，并确保内容不溢出
            if (ImGui::Button(GetSectionTitleCN(secID), ImVec2(ImGui::GetContentRegionAvail().x, sidebarBtnHeight)))
            {
                g_currentTab = secID;
            }

            if (isSelected)
                ImGui::PopStyleColor();
        };

        // 修正：确保这里的 ID 与 config_def.h 中的 Section 名称完全一致
        SidebarItem("UI");
        SidebarItem("Inventory"); // 对应 config_def.h 中的 "Inventory"
        SidebarItem("Item&Shop");
        SidebarItem("Equipment"); // 对应 config_def.h 中的 "Equipment"
        SidebarItem("Hotkeys");

        ImGui::EndChild();
        ImGui::PopStyleColor();

        ImGui::SameLine();

        // 2. 右侧：内容区
        ImGui::BeginChild("Content", ImVec2(0, 0), true);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 10 * dpiScale));

        RenderContent(g_currentTab.c_str());

        ImGui::PopStyleVar();
        ImGui::EndChild();
    }
    ImGui::EndChild();

    // --- 底部：操作栏 ---
    ImGui::Separator();

    // 居中计算：使用动态缩放后的尺寸
    float availW = ImGui::GetWindowWidth();
    float cursorX = (availW - saveBtnW) * 0.5f;
    // 在 footer 区域内垂直居中
    float cursorY = ImGui::GetWindowHeight() - (footerHeight + saveBtnH) * 0.5f;

    ImGui::SetCursorPosX(cursorX);
    ImGui::SetCursorPosY(cursorY);

    if (ImGui::Button("保存配置 (Save)", ImVec2(saveBtnW, saveBtnH)))
    {
        SaveConfigSafe();
        ImGui::OpenPopup("SaveSuccess");
    }

    // (SaveSuccess 弹窗代码保持不变...)
    ImGui::End();
}

// --- Main 入口 ---

int main(int, char **)
{
    SetProcessDPIAware();
    float dpiScale = GetDPIScale();

    // 初始化路径
    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    char *lastSlash = strrchr(exePath, '\\');
    if (lastSlash)
        *(lastSlash + 1) = '\0';
    snprintf(g_iniPath, MAX_PATH, "%sPlugK.ini", exePath);

    pk_config_load(g_iniPath);

    WNDCLASSEX wc = {sizeof(WNDCLASSEX), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(NULL), NULL, NULL, NULL, NULL, _T("PlugK Config"), NULL};
    RegisterClassEx(&wc);

    // 窗口尺寸 (调整为可变大小窗口)
    int baseW = 680;
    int baseH = 500;
    int winW = (int)(baseW * dpiScale);
    int winH = (int)(baseH * dpiScale);

    // 修改：使用 WS_OVERLAPPEDWINDOW 允许调整大小
    HWND hwnd = CreateWindow(wc.lpszClassName, _T("PlugK 配置工具"),
                             WS_OVERLAPPEDWINDOW,
                             100, 100, winW, winH, NULL, NULL, wc.hInstance, NULL);

    if (!CreateDeviceD3D(hwnd))
    {
        CleanupDeviceD3D();
        UnregisterClass(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    (void)io;
    io.IniFilename = NULL; // 不保存 UI 布局文件

    SetupUIStyle(dpiScale);

    // 字体加载
    char fontPath[MAX_PATH];
    GetWindowsDirectoryA(fontPath, MAX_PATH);
    float fontSize = 16.0f * dpiScale;
    strcat(fontPath, "\\Fonts\\msyh.ttc");
    if (GetFileAttributesA(fontPath) != INVALID_FILE_ATTRIBUTES)
        io.Fonts->AddFontFromFileTTF(fontPath, fontSize, NULL, io.Fonts->GetGlyphRangesChineseFull());
    else
    {
        ImFontConfig cfg;
        cfg.SizePixels = 13.0f * dpiScale;
        io.Fonts->AddFontDefault(&cfg);
    }

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX9_Init(g_pd3dDevice);

    bool done = false;
    while (!done)
    {
        MSG msg;
        while (::PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                done = true;
        }
        if (done)
            break;

        ImGui_ImplDX9_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        RenderMainUI(io);

        ImGui::EndFrame();

        g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
        g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        g_pd3dDevice->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
        // 背景色设置得深一点，防止闪烁时刺眼
        D3DCOLOR clear_col = D3DCOLOR_RGBA(30, 30, 35, 255);
        g_pd3dDevice->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, clear_col, 1.0f, 0);

        if (g_pd3dDevice->BeginScene() >= 0)
        {
            ImGui::Render();
            ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
            g_pd3dDevice->EndScene();
        }

        HRESULT result = g_pd3dDevice->Present(NULL, NULL, NULL, NULL);
        if (result == D3DERR_DEVICELOST && g_pd3dDevice->TestCooperativeLevel() == D3DERR_DEVICENOTRESET)
            ResetDevice();
    }

    ImGui_ImplDX9_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    DestroyWindow(hwnd);
    UnregisterClass(wc.lpszClassName, wc.hInstance);

    return 0;
}

// --- D3D 底层函数 (保持不变) ---
bool CreateDeviceD3D(HWND hWnd)
{
    if ((g_pD3D = Direct3DCreate9(D3D_SDK_VERSION)) == NULL)
        return false;
    ZeroMemory(&g_d3dpp, sizeof(g_d3dpp));
    g_d3dpp.Windowed = TRUE;
    g_d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    g_d3dpp.BackBufferFormat = D3DFMT_UNKNOWN;
    g_d3dpp.EnableAutoDepthStencil = TRUE;
    g_d3dpp.AutoDepthStencilFormat = D3DFMT_D16;
    g_d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
    if (g_pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd, D3DCREATE_HARDWARE_VERTEXPROCESSING, &g_d3dpp, &g_pd3dDevice) < 0)
        return false;
    return true;
}
void CleanupDeviceD3D()
{
    if (g_pd3dDevice)
    {
        g_pd3dDevice->Release();
        g_pd3dDevice = NULL;
    }
    if (g_pD3D)
    {
        g_pD3D->Release();
        g_pD3D = NULL;
    }
}
void ResetDevice()
{
    ImGui_ImplDX9_InvalidateDeviceObjects();
    HRESULT hr = g_pd3dDevice->Reset(&g_d3dpp);
    if (hr == D3DERR_INVALIDCALL)
        IM_ASSERT(0);
    ImGui_ImplDX9_CreateDeviceObjects();
}
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;
    switch (msg)
    {
    case WM_SIZE:
        if (g_pd3dDevice != NULL && wParam != SIZE_MINIMIZED)
        {
            g_d3dpp.BackBufferWidth = LOWORD(lParam);
            g_d3dpp.BackBufferHeight = HIWORD(lParam);
            ResetDevice();
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU)
            return 0;
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProc(hWnd, msg, wParam, lParam);
}