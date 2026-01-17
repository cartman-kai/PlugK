// main.cpp
#define _CRT_SECURE_NO_WARNINGS

// 项目配置头文件
#include "config.h"

#include <windows.h>
#include <tchar.h>
#include <d3d9.h>
#include <stdio.h>
#include <string>

// ImGui 头文件
#include "imgui.h"
#include "imgui_impl_dx9.h"
#include "imgui_impl_win32.h"

#pragma comment(linker, "/subsystem:windows /ENTRY:mainCRTStartup")

// 链接库依赖
#pragma comment(lib, "d3d9.lib")

// 全局变量
static LPDIRECT3D9 g_pD3D = NULL;
static LPDIRECT3DDEVICE9 g_pd3dDevice = NULL;
static D3DPRESENT_PARAMETERS g_d3dpp = {};
static char g_iniPath[MAX_PATH] = {0};

// 声明外部配置对象 (来自 config.c)
extern PK_CONFIG g_pk_config;

// 辅助函数声明
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void ResetDevice();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// 获取按键名称的辅助函数 (将 VK code 转为字符串)
void GetKeyName(int key, char *buffer, int bufSize)
{
    if (key == 0)
    {
        snprintf(buffer, bufSize, "None");
        return;
    }

    // MapVirtualKey + GetKeyNameText 是 Win32 获取按键名的方法
    unsigned int scanCode = MapVirtualKey(key, MAPVK_VK_TO_VSC);

    // 处理扩展键 (如箭头键, Insert, Delete 等)
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
    {
        // 如果转换失败，显示数字
        snprintf(buffer, bufSize, "VK_%d", key);
    }
}

// 保存配置到 INI (由于 config.c 没有保存功能，我们在这里实现)
void SaveConfig()
{
    if (strlen(g_iniPath) == 0)
        return;

    auto WriteInt = [](const char *section, const char *key, int val)
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d", val);
        WritePrivateProfileStringA(section, key, buf, g_iniPath);
    };

    // [Inventory]
    WriteInt("Inventory", "EnableSort", g_pk_config.inventory_sort);

    // [Interface]
    WriteInt("Interface", "KeepCenter", g_pk_config.ui_keep_center);
    WriteInt("Interface", "disable_screen_shake", g_pk_config.disable_screen_shake);

    // [Shop]
    WriteInt("Shop", "InfStock", g_pk_config.shop_inf_stock);
    WriteInt("Shop", "OptimizeItem", g_pk_config.shop_item_count);
    WriteInt("Shop", "EnableSort", g_pk_config.shop_sort);

    // [Resolution]
    WriteInt("Resolution", "Enabled", g_pk_config.res_enabled);
    WriteInt("Resolution", "Width", g_pk_config.res_width);
    WriteInt("Resolution", "Height", g_pk_config.res_height);

    // [Stash]
    WriteInt("Stash", "EnableExt", g_pk_config.stash_ext_enabled);

    // [Experimental]
    WriteInt("Experimental", "AutoFillExt", g_pk_config.enable_autofill_ext);
    WriteInt("Experimental", "EnableGemInsert", g_pk_config.enable_insert_gem);
    WriteInt("Experimental", "EnableFuseOpt", g_pk_config.enable_fuse_opt);

    // [Hotkeys]
    WriteInt("Hotkeys", "StashSwap", g_pk_config.key_stash_swap);
    WriteInt("Hotkeys", "StashSort", g_pk_config.key_stash_sort);
    WriteInt("Hotkeys", "InvPrev", g_pk_config.key_inv_prev);
    WriteInt("Hotkeys", "InvSort", g_pk_config.key_inv_sort);
    WriteInt("Hotkeys", "InvSortCurrent", g_pk_config.key_inv_sort_current);
}

// 快捷键设置组件
void HotkeyButton(const char *label, int *keyVar)
{
    char keyName[64];
    GetKeyName(*keyVar, keyName, sizeof(keyName));

    char btnLabel[128];
    snprintf(btnLabel, sizeof(btnLabel), "%s: [ %s ]###btn_%s", label, keyName, label);

    if (ImGui::Button(btnLabel, ImVec2(-1, 0)))
    { // 宽度填满
        ImGui::OpenPopup(label);
    }

    if (ImGui::BeginPopupModal(label, NULL, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("请按下新的快捷键...");
        ImGui::TextDisabled("(按 ESC 取消, Backspace 清除)");

        ImGui::Separator();

        // 显示当前检测到的按键
        ImGui::Dummy(ImVec2(0, 10));

        // 简单的按键捕获逻辑
        // 遍历常见 Virtual Key Codes (0x08 - 0xFE)
        // 注意：这里是一个简单的轮询，为了更精准可以处理 Windows 消息
        // 但在 ImGui 模态框中，这样足够好用
        int pressedKey = 0;
        for (int k = 0x08; k <= 0xFE; k++)
        {
            // 跳过鼠标按键
            if (k == VK_LBUTTON || k == VK_RBUTTON || k == VK_MBUTTON)
                continue;

            // 检查键是否刚被按下
            if (GetAsyncKeyState(k) & 0x0001)
            {
                pressedKey = k;
                break;
            }
        }

        if (pressedKey != 0)
        {
            if (pressedKey == VK_ESCAPE)
            {
                ImGui::CloseCurrentPopup();
            }
            else if (pressedKey == VK_BACK)
            {
                *keyVar = 0; // 清除
                ImGui::CloseCurrentPopup();
            }
            else
            {
                *keyVar = pressedKey;
                ImGui::CloseCurrentPopup();
            }
        }

        if (ImGui::Button("取消", ImVec2(120, 0)))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// 简单的 HelpMarker
void HelpMarker(const char *desc)
{
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
    {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted(desc);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

// 主程序入口
int main(int, char **)
{
    // 1. 初始化路径配置
    // 获取当前 EXE 所在路径，并拼凑 PlugK.ini
    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    char *lastSlash = strrchr(exePath, '\\');
    if (lastSlash)
        *(lastSlash + 1) = '\0';
    snprintf(g_iniPath, MAX_PATH, "%sPlugK.ini", exePath);

    // 如果文件不存在，先创建一个默认的
    if (GetFileAttributesA(g_iniPath) == INVALID_FILE_ATTRIBUTES)
    {
        pk_config_create_default(g_iniPath);
    }

    // 加载配置
    // 注意：config.c 里的 pk_config_load 可能会去读 DLL 路径，
    // 这里我们手动为了保险，重新实现简单的加载，或者你可以修改 config.c 让它支持传入路径。
    // 鉴于不修改 config.c 的原则，我们这里假设 config.c 里的逻辑在找不到 DLL 时会回退，
    // 或者我们手动再读一遍覆盖，确保 GUI 读的是对的。
    pk_config_load();

    // 强制修正：因为 config.c 可能用了 DLL 句柄导致路径错误，我们手动重读一遍关键数据确保准确
    // 这一步在实际集成时可以优化 config.c 的路径获取逻辑。
    g_pk_config.shop_inf_stock = GetPrivateProfileIntA("Shop", "InfStock", 0, g_iniPath);
    g_pk_config.res_enabled = GetPrivateProfileIntA("Resolution", "Enabled", 0, g_iniPath);
    g_pk_config.res_width = GetPrivateProfileIntA("Resolution", "Width", 800, g_iniPath);
    g_pk_config.res_height = GetPrivateProfileIntA("Resolution", "Height", 600, g_iniPath);
    g_pk_config.inventory_sort = GetPrivateProfileIntA("Inventory", "EnableSort", 0, g_iniPath);
    g_pk_config.ui_keep_center = GetPrivateProfileIntA("Interface", "KeepCenter", 0, g_iniPath);
    g_pk_config.disable_screen_shake = GetPrivateProfileIntA("Interface", "disable_screen_shake", 0, g_iniPath);
    g_pk_config.shop_item_count = GetPrivateProfileIntA("Shop", "OptimizeItem", 0, g_iniPath);
    g_pk_config.shop_sort = GetPrivateProfileIntA("Shop", "EnableSort", 0, g_iniPath);
    g_pk_config.stash_ext_enabled = GetPrivateProfileIntA("Stash", "EnableExt", 1, g_iniPath);
    g_pk_config.enable_autofill_ext = GetPrivateProfileIntA("Experimental", "AutoFillExt", 0, g_iniPath);
    g_pk_config.enable_insert_gem = GetPrivateProfileIntA("Experimental", "EnableGemInsert", 0, g_iniPath);
    g_pk_config.enable_fuse_opt = GetPrivateProfileIntA("Experimental", "EnableFuseOpt", 0, g_iniPath);

    // 快捷键读取
    g_pk_config.key_stash_swap = GetPrivateProfileIntA("Hotkeys", "StashSwap", VK_OEM_COMMA, g_iniPath);
    g_pk_config.key_stash_sort = GetPrivateProfileIntA("Hotkeys", "StashSort", VK_OEM_4, g_iniPath);
    g_pk_config.key_inv_prev = GetPrivateProfileIntA("Hotkeys", "InvPrev", VK_OEM_PERIOD, g_iniPath);
    g_pk_config.key_inv_sort = GetPrivateProfileIntA("Hotkeys", "InvSort", VK_OEM_5, g_iniPath);
    g_pk_config.key_inv_sort_current = GetPrivateProfileIntA("Hotkeys", "InvSortCurrent", VK_OEM_2, g_iniPath);

    // 2. 注册窗口类
    WNDCLASSEX wc = {sizeof(WNDCLASSEX), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(NULL), NULL, NULL, NULL, NULL, _T("PlugK Config Tool"), NULL};
    RegisterClassEx(&wc);
    HWND hwnd = CreateWindow(wc.lpszClassName, _T("PlugK 配置管理工具"), WS_OVERLAPPEDWINDOW, 100, 100, 500, 700, NULL, NULL, wc.hInstance, NULL);

    // 3. 初始化 Direct3D
    if (!CreateDeviceD3D(hwnd))
    {
        CleanupDeviceD3D();
        UnregisterClass(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    // 4. 初始化 Dear ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    (void)io;

    ImFont *pFont = nullptr; // 改名避免与之前的 font 重名

    // 方案 A: 尝试获取系统 Windows 目录 (更安全)
    char fontPath[MAX_PATH];
    GetWindowsDirectoryA(fontPath, MAX_PATH);
    strcat(fontPath, "\\Fonts\\msyh.ttc"); // 微软雅黑

    // 检查文件是否存在
    DWORD dwAttrib = GetFileAttributesA(fontPath);
    bool fileExists = (dwAttrib != INVALID_FILE_ATTRIBUTES && !(dwAttrib & FILE_ATTRIBUTE_DIRECTORY));

    if (fileExists)
    {
        pFont = io.Fonts->AddFontFromFileTTF(fontPath, 18.0f, NULL, io.Fonts->GetGlyphRangesChineseFull());
    }

    // 方案 B: 如果微软雅黑失败，尝试宋体 (Windows 基础字体)
    if (pFont == nullptr)
    {
        GetWindowsDirectoryA(fontPath, MAX_PATH);
        strcat(fontPath, "\\Fonts\\simsun.ttc");
        pFont = io.Fonts->AddFontFromFileTTF(fontPath, 18.0f, NULL, io.Fonts->GetGlyphRangesChineseFull());
    }

    // 方案 C: 如果都失败了，ImGui 会自动退回到默认的像素字体（虽然中文会是问号，但程序不会崩）
    if (pFont == nullptr)
    {
        io.Fonts->AddFontDefault();
    }
    ImGui::StyleColorsDark();

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX9_Init(g_pd3dDevice);

    bool show_demo_window = false;
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    // 5. 主循环
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

        // Start the Dear ImGui frame
        ImGui_ImplDX9_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // --- 界面绘制开始 ---

        // 设置全屏窗口布局
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("MainConfig", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);

        ImGui::Text("PlugK 游戏增强插件配置工具");
        ImGui::Separator();

        if (ImGui::BeginTabBar("ConfigTabs"))
        {
            // --- 标签页 1: 基础设置 ---
            if (ImGui::BeginTabItem("基础 & 界面"))
            {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0, 1, 1, 1), "界面选项");

                bool keepCenter = g_pk_config.ui_keep_center;
                if (ImGui::Checkbox("保持界面居中 (防止晃动)", &keepCenter))
                    g_pk_config.ui_keep_center = keepCenter;
                HelpMarker("打开背包/技能窗口时，保持游戏画面居中，不向右平移。");

                bool noShake = g_pk_config.disable_screen_shake;
                if (ImGui::Checkbox("禁用屏幕震动", &noShake))
                    g_pk_config.disable_screen_shake = noShake;

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0, 1, 1, 1), "分辨率补丁");

                bool resEnable = g_pk_config.res_enabled;
                if (ImGui::Checkbox("启用自定义分辨率", &resEnable))
                    g_pk_config.res_enabled = resEnable;

                if (!resEnable)
                    ImGui::BeginDisabled();
                ImGui::InputInt("宽度 (Width)", &g_pk_config.res_width);
                ImGui::InputInt("高度 (Height)", &g_pk_config.res_height);
                if (!resEnable)
                    ImGui::EndDisabled();

                ImGui::EndTabItem();
            }

            // --- 标签页 2: 物品与商店 ---
            if (ImGui::BeginTabItem("物品 & 商店"))
            {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0, 1, 0, 1), "库存功能");

                bool invSort = g_pk_config.inventory_sort;
                if (ImGui::Checkbox("启用一键整理 (背包)", &invSort))
                    g_pk_config.inventory_sort = invSort;

                bool stashExt = g_pk_config.stash_ext_enabled;
                if (ImGui::Checkbox("启用扩展储物箱 (大箱子)", &stashExt))
                    g_pk_config.stash_ext_enabled = stashExt;

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0, 1, 0, 1), "商店优化");

                bool shopOpt = g_pk_config.shop_item_count;
                if (ImGui::Checkbox("商店物品数量随机 & 堆叠", &shopOpt))
                    g_pk_config.shop_item_count = shopOpt;

                bool shopInf = g_pk_config.shop_inf_stock;
                if (ImGui::Checkbox("无限库存 (药品/暗器不消失)", &shopInf))
                    g_pk_config.shop_inf_stock = shopInf;

                bool shopSort = g_pk_config.shop_sort;
                if (ImGui::Checkbox("商店物品自动排序", &shopSort))
                    g_pk_config.shop_sort = shopSort;

                ImGui::EndTabItem();
            }

            // --- 标签页 3: 快捷键绑定 ---
            if (ImGui::BeginTabItem("快捷键"))
            {
                ImGui::Spacing();
                ImGui::Text("点击按钮后按下键盘按键即可修改。");
                ImGui::TextDisabled("注意：所有快捷键都需要配合 Ctrl 使用 (如 Ctrl + 键)");
                ImGui::Separator();
                ImGui::Spacing();

                HotkeyButton("背包整理 (全部)", &g_pk_config.key_inv_sort);
                HotkeyButton("背包整理 (仅当前页)", &g_pk_config.key_inv_sort_current);
                HotkeyButton("背包上一页", &g_pk_config.key_inv_prev);

                ImGui::Spacing();
                HotkeyButton("储物箱整理", &g_pk_config.key_stash_sort);
                HotkeyButton("储物箱切换 (A/B)", &g_pk_config.key_stash_swap);

                ImGui::EndTabItem();
            }

            // --- 标签页 4: 实验性功能 ---
            if (ImGui::BeginTabItem("实验性"))
            {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(1, 0.5, 0, 1), "警告：以下功能可能不稳定");

                bool autoFill = g_pk_config.enable_autofill_ext;
                if (ImGui::Checkbox("扩展背包自动填充 (免翻页)", &autoFill))
                    g_pk_config.enable_autofill_ext = autoFill;

                bool gemInsert = g_pk_config.enable_insert_gem;
                if (ImGui::Checkbox("修改宝石镶嵌条件", &gemInsert))
                    g_pk_config.enable_insert_gem = gemInsert;

                bool fuseOpt = g_pk_config.enable_fuse_opt;
                if (ImGui::Checkbox("优化炼化消耗 (仅扣除数量)", &fuseOpt))
                    g_pk_config.enable_fuse_opt = fuseOpt;

                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }

        ImGui::Separator();
        ImGui::Spacing();

        // 底部按钮栏
        if (ImGui::Button("保存配置 (Save)", ImVec2(150, 40)))
        {
            SaveConfig();
            ImGui::OpenPopup("SaveSuccess");
        }

        // 保存成功提示
        if (ImGui::BeginPopup("SaveSuccess"))
        {
            ImGui::Text("配置已保存至 PlugK.ini");
            if (ImGui::Button("确定"))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        ImGui::End();
        // --- 界面绘制结束 ---

        // Rendering
        ImGui::EndFrame();
        g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
        g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        g_pd3dDevice->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
        D3DCOLOR clear_col_dx = D3DCOLOR_RGBA((int)(clear_color.x * 255.0f), (int)(clear_color.y * 255.0f), (int)(clear_color.z * 255.0f), (int)(clear_color.w * 255.0f));
        g_pd3dDevice->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, clear_col_dx, 1.0f, 0);
        if (g_pd3dDevice->BeginScene() >= 0)
        {
            ImGui::Render();
            ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
            g_pd3dDevice->EndScene();
        }
        HRESULT result = g_pd3dDevice->Present(NULL, NULL, NULL, NULL);

        // Handle loss of D3D9 device
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

// 辅助函数实现
bool CreateDeviceD3D(HWND hWnd)
{
    if ((g_pD3D = Direct3DCreate9(D3D_SDK_VERSION)) == NULL)
        return false;

    // Create the D3DDevice
    ZeroMemory(&g_d3dpp, sizeof(g_d3dpp));
    g_d3dpp.Windowed = TRUE;
    g_d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    g_d3dpp.BackBufferFormat = D3DFMT_UNKNOWN;
    g_d3dpp.EnableAutoDepthStencil = TRUE;
    g_d3dpp.AutoDepthStencilFormat = D3DFMT_D16;
    g_d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_ONE; // Present with vsync
    // g_d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE; // Present without vsync, maximum unthrottled framerate

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

// Forward declare message handler from imgui_impl_win32.cpp
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
        if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
            return 0;
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProc(hWnd, msg, wParam, lParam);
}