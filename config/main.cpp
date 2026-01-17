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

#include "imgui.h"
#include "imgui_impl_dx9.h"
#include "imgui_impl_win32.h"

// 引入配置
extern "C"
{
#include "../inc/config.h"
}

#pragma comment(lib, "d3d9.lib")

// 全局变量
static LPDIRECT3D9 g_pD3D = NULL;
static LPDIRECT3DDEVICE9 g_pd3dDevice = NULL;
static D3DPRESENT_PARAMETERS g_d3dpp = {};
static char g_iniPath[MAX_PATH] = {0};

// 声明
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void ResetDevice();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// --- 辅助功能 ---

// 获取按键名称
void GetKeyName(int key, char *buffer, int bufSize)
{
    if (key == 0)
    {
        snprintf(buffer, bufSize, "None");
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

// 快捷键按钮组件
void HotkeyButton(const char *label, int *keyVar)
{
    char keyName[64];
    GetKeyName(*keyVar, keyName, sizeof(keyName));
    char btnLabel[128];
    snprintf(btnLabel, sizeof(btnLabel), "%s: [ %s ]###btn_%s", label, keyName, label);

    if (ImGui::Button(btnLabel, ImVec2(-1, 0)))
        ImGui::OpenPopup(label);

    if (ImGui::BeginPopupModal(label, NULL, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("按下键盘按键绑定...");
        ImGui::TextDisabled("(ESC=取消, Backspace=清除)");
        ImGui::Separator();

        // 简单的轮询检测
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
        if (ImGui::Button("取消", ImVec2(120, 0)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

// === 核心：安全保存逻辑 (保留注释) ===
void SaveConfigSafe()
{
    // 1. 读取所有行
    std::ifstream inFile(g_iniPath);
    if (!inFile.is_open())
        return; // 文件不存在则无法保留注释，暂不处理新建情况

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(inFile, line))
        lines.push_back(line);
    inFile.close();

    // 2. 定义更新值的 Lambda
    auto UpdateValueInLines = [&](const char *section, const char *key, int val)
    {
        std::string sSection = std::string("[") + section + "]";
        bool inCorrectSection = false;
        bool keyFound = false;

        for (auto &l : lines)
        {
            // 简单判断 Section (忽略前面的空格)
            if (l.find(sSection) != std::string::npos)
            {
                inCorrectSection = true;
                continue;
            }
            if (inCorrectSection && l.find("[") != std::string::npos && l.find("]") != std::string::npos)
            {
                if (l.find(sSection) == std::string::npos)
                    inCorrectSection = false; // 进入了下一个 Section
            }

            if (inCorrectSection)
            {
                // 正则匹配: ^(空白)Key(空白)=(空白)数字(任意后缀)
                // 这里的正则要小心，确保只匹配 Key，而不是 KeySomething
                std::regex re("^\\s*" + std::string(key) + "\\s*=\\s*(-?\\d+)(.*)");
                std::smatch match;
                if (std::regex_search(l, match, re))
                {
                    // 构建新行： Key = NewVal + 原有的后缀(注释)
                    char buf[256];
                    snprintf(buf, sizeof(buf), "%s=%d%s", key, val, match[2].str().c_str());
                    l = buf;
                    keyFound = true;
                    return; // 找到即停止
                }
            }
        }

        // 如果没找到 Key (可能是新加的配置)，简单地在文件末尾追加 (可选)
        // 为保持简单，这里暂不追加，依赖 pk_config_create_default 的初始化
    };

// 3. 使用 X-Macro 批量更新内存中的行
#define TYPE_BOOL
#define TYPE_INT
#define TYPE_KEY

#define X(type, name, sec, key, val, desc) \
    UpdateValueInLines(sec, key, (int)g_pk_config.name);

#include "../inc/config_def.h"
#undef X

    // 4. 写回文件
    std::ofstream outFile(g_iniPath);
    for (const auto &l : lines)
        outFile << l << "\n";
}

int main(int, char **)
{
    // 1. HiDPI 修复：防止高分屏模糊
    // 注意：Windows 10 1703+ 支持，旧系统可能无效果但不报错
    SetProcessDPIAware();

    // 2. 初始化路径
    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    char *lastSlash = strrchr(exePath, '\\');
    if (lastSlash)
        *(lastSlash + 1) = '\0';
    snprintf(g_iniPath, MAX_PATH, "%sPlugK.ini", exePath);

    // 3. 加载配置 (复用 C 逻辑)
    pk_config_load(g_iniPath);

    // 4. 窗口初始化
    WNDCLASSEX wc = {sizeof(WNDCLASSEX), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(NULL), NULL, NULL, NULL, NULL, _T("PlugK Config"), NULL};
    RegisterClassEx(&wc);
    // 调整初始窗口大小
    HWND hwnd = CreateWindow(wc.lpszClassName, _T("PlugK 配置工具"), WS_OVERLAPPEDWINDOW, 100, 100, 600, 700, NULL, NULL, wc.hInstance, NULL);

    if (!CreateDeviceD3D(hwnd))
    {
        CleanupDeviceD3D();
        UnregisterClass(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    // 5. ImGui 初始化
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    (void)io;
    // 禁止生成 imgui.ini
    io.IniFilename = NULL;

    ImGui::StyleColorsDark();

    // 字体缩放处理 (简单方案：固定放大 1.5 倍或检测 DPI)
    // 这里简单演示：尝试加载系统微软雅黑
    char fontPath[MAX_PATH];
    GetWindowsDirectoryA(fontPath, MAX_PATH);
    strcat(fontPath, "\\Fonts\\msyh.ttc");

    if (GetFileAttributesA(fontPath) != INVALID_FILE_ATTRIBUTES)
    {
        // 18.0f * 1.3f 适应稍微大一点的 DPI
        io.Fonts->AddFontFromFileTTF(fontPath, 24.0f, NULL, io.Fonts->GetGlyphRangesChineseFull());
    }
    else
    {
        io.Fonts->AddFontDefault(); // 英文 fallback
    }

    // 放大 UI 控件尺寸，适配高分屏
    ImGui::GetStyle().ScaleAllSizes(1.3f);

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

        // --- GUI 绘制 ---
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("Main", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);

        ImGui::Text("PlugK 游戏增强配置");
        ImGui::Separator();

        if (ImGui::BeginTabBar("Tabs"))
        {

            // X-Macro 魔法：自动生成界面
            // 我们通过定义宏来决定每个 Tab 显示什么

            // --- Tab 1: 常规设置 (BOOL & INT) ---
            if (ImGui::BeginTabItem("常规设置"))
            {
                ImGui::Spacing();

#define TYPE_BOOL 1
#define TYPE_INT 2
#define TYPE_KEY 3

#define X(type, name, sec, key, val, desc)        \
    if (type == TYPE_BOOL)                        \
    {                                             \
        bool v = (bool)g_pk_config.name;          \
        if (ImGui::Checkbox(desc, &v))            \
            g_pk_config.name = v;                 \
    }                                             \
    else if (type == TYPE_INT)                    \
    {                                             \
        ImGui::InputInt(desc, &g_pk_config.name); \
    }

#include "../inc/config_def.h"
#undef X
#undef TYPE_BOOL
#undef TYPE_INT
#undef TYPE_KEY

                ImGui::EndTabItem();
            }

            // --- Tab 2: 快捷键 (KEY) ---
            if (ImGui::BeginTabItem("快捷键绑定"))
            {
                ImGui::Spacing();
                ImGui::TextDisabled("点击按钮后按键修改，使用时，需要按 Ctrl 生效，例如 Ctrl + ,");
                ImGui::Separator();
                ImGui::Spacing();

#define TYPE_BOOL 1
#define TYPE_INT 2
#define TYPE_KEY 3

#define X(type, name, sec, key, val, desc)     \
    if (type == TYPE_KEY)                      \
    {                                          \
        HotkeyButton(desc, &g_pk_config.name); \
    }

#include "../inc/config_def.h"
#undef X

                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, 10));

        // 居中保存按钮
        float width = ImGui::GetWindowWidth();
        ImGui::SetCursorPosX((width - 200) * 0.5f);
        if (ImGui::Button("保存配置 (Save)", ImVec2(200, 50)))
        {
            SaveConfigSafe();
            ImGui::OpenPopup("Saved");
        }

        if (ImGui::BeginPopup("Saved"))
        {
            ImGui::TextColored(ImVec4(0, 1, 0, 1), "配置已保存成功！");
            ImGui::Text("即时生效 (部分功能需重启游戏)");
            if (ImGui::Button("关闭"))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        ImGui::End();
        // --- End GUI ---

        ImGui::EndFrame();
        g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
        g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        g_pd3dDevice->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
        D3DCOLOR clear_col = D3DCOLOR_RGBA(100, 100, 100, 255);
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

// 这里的 CreateDeviceD3D 等底层函数代码保持不变，请复用之前的代码...
// ... (CreateDeviceD3D, CleanupDeviceD3D, ResetDevice, WndProc) ...
// 必须保留 WndProc 以处理消息
bool CreateDeviceD3D(HWND hWnd)
{ /* 复用之前的代码 */
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