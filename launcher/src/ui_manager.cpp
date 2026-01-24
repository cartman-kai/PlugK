// Windows & System
#include <windows.h>
#include <string>
#include <vector>

// Third-party (ImGui)
#include "imgui.h"

// Project
#include "../inc/ui_manager.h"
#include "../inc/mod_loader.h"
#include "../inc/config_manager.h"
#include "../inc/utils.h"
#include "version_info.h"

namespace UIManager
{
    // Navigation State
    enum AppView
    {
        Home,
        Settings
    };
    static AppView g_currentView = Home;

    static char g_saveStatus[256] = {0};
    static DWORD g_saveTime = 0;
    static float g_dpiScale = 1.0f;

    void SetupStyles(float dpiScale)
    {
        ImGuiStyle &style = ImGui::GetStyle();
        ImGui::StyleColorsDark();

        style.WindowRounding = 8.0f;
        style.ChildRounding = 6.0f;
        style.FrameRounding = 6.0f; // Slightly more rounded
        style.PopupRounding = 6.0f;
        style.GrabRounding = 4.0f;
        style.ScrollbarSize = 12.0f;
        style.ScrollbarRounding = 9.0f;

        style.ItemSpacing = ImVec2(10 * dpiScale, 8 * dpiScale);
        // Request B: Increase FramePadding
        style.FramePadding = ImVec2(20 * dpiScale, 12 * dpiScale);
        style.WindowPadding = ImVec2(16 * dpiScale, 16 * dpiScale);

        ImVec4 *colors = style.Colors;
        colors[ImGuiCol_WindowBg] = ImVec4(0.12f, 0.12f, 0.14f, 1.00f);
        colors[ImGuiCol_ChildBg] = ImVec4(0.16f, 0.16f, 0.18f, 1.00f);
        colors[ImGuiCol_Header] = ImVec4(0.24f, 0.24f, 0.26f, 1.00f);
        colors[ImGuiCol_HeaderHovered] = ImVec4(0.30f, 0.30f, 0.33f, 1.00f);
        colors[ImGuiCol_HeaderActive] = ImVec4(0.20f, 0.60f, 0.86f, 1.00f); // Vibrant Blue

        // Request B: Vibrant Blue Button #3498DB (RGB: 0.20, 0.60, 0.86)
        colors[ImGuiCol_Button] = ImVec4(0.20f, 0.60f, 0.86f, 1.00f);
        colors[ImGuiCol_ButtonHovered] = ImVec4(0.25f, 0.65f, 0.90f, 1.00f);
        colors[ImGuiCol_ButtonActive] = ImVec4(0.15f, 0.55f, 0.80f, 1.00f);

        // Request B: Checkbox color same as button
        colors[ImGuiCol_CheckMark] = ImVec4(0.20f, 0.60f, 0.86f, 1.00f);

        // Border for secondary buttons (subtle)
        colors[ImGuiCol_Border] = ImVec4(0.40f, 0.40f, 0.42f, 0.50f);
        style.FrameBorderSize = 0.0f; // Default 0, override for secondary
    }

    // --- Key Binding Helpers ---
    std::string KeyCodeToString(int key)
    {
        char buf[64] = {0};
        // Handle common keys manually for better names
        if (key >= '0' && key <= '9')
        {
            buf[0] = (char)key;
            return std::string(buf);
        }
        if (key >= 'A' && key <= 'Z')
        {
            buf[0] = (char)key;
            return std::string(buf);
        }

        switch (key)
        {
        case VK_ESCAPE:
            return "ESC";
        case VK_RETURN:
            return "Enter";
        case VK_TAB:
            return "Tab";
        case VK_SPACE:
            return "Space";
        case VK_BACK:
            return "Backspace";
        case VK_DELETE:
            return "Delete";
        case VK_SHIFT:
            return "Shift";
        case VK_CONTROL:
            return "Ctrl";
        case VK_MENU:
            return "Alt";
        // F-Keys
        case VK_F1:
            return "F1";
        case VK_F2:
            return "F2";
        case VK_F3:
            return "F3";
        case VK_F4:
            return "F4";
        case VK_F5:
            return "F5";
        case VK_F6:
            return "F6";
        case VK_F7:
            return "F7";
        case VK_F8:
            return "F8";
        case VK_F9:
            return "F9";
        case VK_F10:
            return "F10";
        case VK_F11:
            return "F11";
        case VK_F12:
            return "F12";
        // OEM
        case VK_OEM_COMMA:
            return ",";
        case VK_OEM_PERIOD:
            return ".";
        case VK_OEM_PLUS:
            return "+";
        case VK_OEM_MINUS:
            return "-";
        case VK_OEM_1:
            return ";";
        case VK_OEM_2:
            return "/";
        case VK_OEM_3:
            return "`";
        case VK_OEM_4:
            return "[";
        case VK_OEM_5:
            return "\\";
        case VK_OEM_6:
            return "]";
        case VK_OEM_7:
            return "'";
        }

        int scanCode = MapVirtualKeyA(key, MAPVK_VK_TO_VSC);
        if (scanCode != 0)
        {
            if (GetKeyNameTextA(scanCode << 16, buf, sizeof(buf)) > 0)
                return std::string(buf);
        }

        sprintf_s(buf, "VK_%02X", key);
        return std::string(buf);
    }

    void RenderKeyBind(const char *label, int *keyVal)
    {
        ImGui::BeginGroup();
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%s", label);
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 120 * g_dpiScale);

        std::string btnLabel = KeyCodeToString(*keyVal);
        if (btnLabel.empty())
            btnLabel = "NONE";

        ImGui::PushID(label);
        // Use a slightly different style for keybind buttons (secondary)
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.22f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);

        if (ImGui::Button(btnLabel.c_str(), ImVec2(120 * g_dpiScale, 0)))
        {
            ImGui::OpenPopup("BindKeyPopup");
        }

        ImGui::PopStyleVar();
        ImGui::PopStyleColor();

        if (ImGui::BeginPopup("BindKeyPopup"))
        {
            ImGui::Text("请按键...");
            for (int k = 8; k <= 255; k++)
            {
                if (k == VK_LBUTTON || k == VK_RBUTTON || k == VK_MBUTTON)
                    continue;
                if (GetAsyncKeyState(k) & 0x8000)
                {
                    *keyVal = k;
                    ImGui::CloseCurrentPopup();
                    break;
                }
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
        ImGui::EndGroup();
    }

    void ShowSaveNotification(const char *text)
    {
        strcpy_s(g_saveStatus, text);
        g_saveTime = GetTickCount();
    }

    void RenderStatusIcon(const char *label, bool ok)
    {
        ImVec4 color = ok ? ImVec4(0.2f, 0.8f, 0.2f, 1.0f) : ImVec4(0.8f, 0.2f, 0.2f, 1.0f);
        ImGui::TextColored(color, ok ? "[ OK ]" : "[MISS]");
        ImGui::SameLine();
        ImGui::Text("%s", label);
    }

    void RenderMainPage()
    {
        auto status = ModLoader::CheckStatus();
        float availW = ImGui::GetContentRegionAvail().x;
        float availH = ImGui::GetContentRegionAvail().y;

        // --- Environmental Check ---
        ImGui::BeginChild("StatusPanel", ImVec2(0, 120 * g_dpiScale), true);
        // ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "环境监测 (Environmental Check)");
        // ImGui::Separator();
        // ImGui::Spacing();

        RenderStatusIcon("游戏程序 (ComeOn.exe)", status.exeExists);
        RenderStatusIcon("插件动态库 (PlugK.dll)", status.dllExists);
        RenderStatusIcon("配置文件 (PlugK.ini)", status.iniExists);

        if (!status.exeExists || !status.dllExists)
        {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "提示：部分功能不可用，请检查文件完整性。");
        }
        ImGui::EndChild();

        // Request B: Breathing room
        ImGui::Dummy(ImVec2(0, 20 * g_dpiScale));

        // --- Core Actions ---
        // Center vertically in the remaining space roughly
        // But we have fixed flow. Let's just center the buttons.

        ImGui::BeginChild("ActionContainer", ImVec2(0, 0), false); // Transparent container for layout

        float btnWidth = availW * 0.6f; // 60% width for main buttons
        if (btnWidth < 300 * g_dpiScale)
            btnWidth = 300 * g_dpiScale;
        float btnHeight = 60 * g_dpiScale;

        float cursorX = (availW - btnWidth) * 0.5f;

        auto CenteredButton = [&](const char *label, bool enabled, const ImVec4 &colorOverride = ImVec4(0, 0, 0, 0))
        {
            ImGui::SetCursorPosX(cursorX);
            if (!enabled)
            {
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
            }
            else if (colorOverride.w > 0.0f)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, colorOverride);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(colorOverride.x + 0.1f, colorOverride.y + 0.1f, colorOverride.z + 0.1f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(colorOverride.x - 0.1f, colorOverride.y - 0.1f, colorOverride.z - 0.1f, 1.0f));
            }

            bool clicked = ImGui::Button(label, ImVec2(btnWidth, btnHeight));

            if (!enabled)
            {
                ImGui::PopStyleColor();
                ImGui::PopStyleVar();
            }
            else if (colorOverride.w > 0.0f)
            {
                ImGui::PopStyleColor(3);
            }

            return clicked && enabled;
        };

        // 1. Launch Original (Greenish - distinct from main blue)
        if (CenteredButton("启动原版游戏", status.exeExists, ImVec4(0.25f, 0.6f, 0.35f, 1.0f)))
        {
            ModLoader::LaunchOriginal();
        }
        ImGui::Spacing();

        // 2. Launch Modded (Reddish/Highlighted - distinct)
        // Or keep it Vibrant Blue (Main Action) per User Request B?
        // User said "Launch button to ... vibrant blue". Let's use the default style (which we set to vibrant blue)
        // OR override if we want it to pop more. The default is now Blue #3498DB.
        // Let's use default style for the "Main" action, which effectively is the Mod Launch for this tool.

        ImGui::SetCursorPosX(cursorX);
        if (!status.exeExists || !status.dllExists)
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);

        // This uses the default Vibrant Blue we set in SetupStyles
        if (ImGui::Button("启动MOD模式", ImVec2(btnWidth, btnHeight)))
        {
            if (status.exeExists && status.dllExists)
                ModLoader::LaunchWithMod();
        }

        if (!status.exeExists || !status.dllExists)
            ImGui::PopStyleVar();

        ImGui::Spacing();

        // 3. Config (Secondary Style)
        ImGui::SetCursorPosX(cursorX);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.22f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f); // Secondary border
        if (ImGui::Button("配置管理", ImVec2(btnWidth, btnHeight)))
        {
            g_currentView = Settings;
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();

        ImGui::Spacing();

        // 4. Shortcut (Secondary Style)
        ImGui::SetCursorPosX(cursorX);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.22f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);

        bool enableShortcut = status.exeExists && status.dllExists && status.iniExists;
        if (!enableShortcut)
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);

        if (ImGui::Button("创建启动快捷方式", ImVec2(btnWidth, btnHeight)))
        {
            if (enableShortcut)
                ImGui::OpenPopup("CreateShortcutPopup");
        }

        if (!enableShortcut)
            ImGui::PopStyleVar();

        ImGui::PopStyleVar();
        ImGui::PopStyleColor();

        // Request C: Popup Styling
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(15 * g_dpiScale, 15 * g_dpiScale));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.15f, 0.15f, 0.17f, 1.00f));
        if (ImGui::BeginPopup("CreateShortcutPopup"))
        {
            char target[MAX_PATH];
            GetModuleFileNameA(NULL, target, MAX_PATH);

            int ver = ModLoader::GetGameVersion();
            std::string nameOriginal = "comeon";
            std::string nameMod = "comeon_mod_loader";

            if (ver == 105)
            {
                nameOriginal = "刀剑正传";
                nameMod = "刀剑正传-MOD";
            }
            else if (ver == 201)
            {
                nameOriginal = "上古传说";
                nameMod = "上古传说-MOD";
            }

            // Request C: Use Selectable instead of Button
            float itemH = 35 * g_dpiScale;
            if (ImGui::Selectable("创建 [ 原版 ] 快捷方式", false, 0, ImVec2(0, itemH)))
            {
                if (Utils::CreateDesktopShortcut(target, nameOriginal, "--original"))
                    ShowSaveNotification("原版快捷方式已创建！");
            }
            if (ImGui::Selectable("创建 [ MOD模式 ] 快捷方式", false, 0, ImVec2(0, itemH)))
            {
                if (Utils::CreateDesktopShortcut(target, nameMod, "--mod"))
                    ShowSaveNotification("MOD快捷方式已创建！");
            }
            ImGui::EndPopup();
        }
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();

        ImGui::EndChild(); // ActionContainer
    }

    void RenderSettingsPage()
    {
        // Navigation: Back Button (Request A)
        if (ImGui::Button("<- 返回", ImVec2(150 * g_dpiScale, 40 * g_dpiScale)))
        {
            g_currentView = Home;
            // Optionally save on exit? Or just rely on explicit save.
        }
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "|  详细设置");
        ImGui::Separator();

        ImGui::BeginGroup();
        static std::string activeSubTab = "UI";
        const char *subTabs[] = {"UI", "Inventory", "Item&Shop", "Equipment", "Hotkeys"};
        const char *subTabsCN[] = {"界面显示", "背包与仓库", "物品与商店", "装备与合成", "快捷按键"};

        float tabW = ImGui::GetContentRegionAvail().x / 5.0f - ImGui::GetStyle().ItemSpacing.x;

        for (int i = 0; i < 5; i++)
        {
            if (i > 0)
                ImGui::SameLine();
            bool active = (activeSubTab == subTabs[i]);

            // Subtabs styling
            if (active)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.60f, 0.86f, 1.00f));
            }
            else
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.18f, 0.20f, 1.00f));
            }

            if (ImGui::Button(subTabsCN[i], ImVec2(tabW, 40 * g_dpiScale)))
            {
                activeSubTab = subTabs[i];
            }
            ImGui::PopStyleColor();
        }
        ImGui::EndGroup();

        ImGui::Separator();

        // 2. Content Area
        ImGui::BeginChild("SettingsContent", ImVec2(0, -60 * g_dpiScale), true);
        ImGui::Spacing();

#define TYPE_KEY 3
#define X(type, name, sec, key, val, desc)                                                                                                                                                                                                                               \
    if (activeSubTab == sec)                                                                                                                                                                                                                                             \
    {                                                                                                                                                                                                                                                                    \
        if (type == TYPE_BOOL)                                                                                                                                                                                                                                           \
        {                                                                                                                                                                                                                                                                \
            bool v = (bool)g_pk_config.name;                                                                                                                                                                                                                             \
            if ((strcmp("res_enabled", #name) == 0))                                                                                                                                                                                                                     \
            {                                                                                                                                                                                                                                                            \
                if (ImGui::Checkbox(desc, &v))                                                                                                                                                                                                                           \
                    g_pk_config.name = v;                                                                                                                                                                                                                                \
                if (v)                                                                                                                                                                                                                                                   \
                {                                                                                                                                                                                                                                                        \
                    static const char *items[] = {"800x600 (4:3)", "1024x768 (4:3)", "1280x720 (16:9)", "1280x800 (16:10)", "1366x768 (16:9)", "1440x900 (16:10)", "1600x900 (16:9)", "1680x1050 (16:10)", "1920x1080 (16:9)", "1920x1200 (16:10)", "2560x1440 (16:9)"}; \
                    static int currentRes = -1;                                                                                                                                                                                                                          \
                    if (currentRes == -1)                                                                                                                                                                                                                                \
                    {                                                                                                                                                                                                                                                    \
                        for (int k = 0; k < 11; k++)                                                                                                                                                                                                                     \
                        {                                                                                                                                                                                                                                                \
                            int w, h;                                                                                                                                                                                                                                    \
                            sscanf_s(items[k], "%dx%d", &w, &h);                                                                                                                                                                                                         \
                            if (w == g_pk_config.res_width && h == g_pk_config.res_height)                                                                                                                                                                               \
                                currentRes = k;                                                                                                                                                                                                                          \
                        }                                                                                                                                                                                                                                                \
                    }                                                                                                                                                                                                                                                    \
                    if (ImGui::Combo("预设分辨率", &currentRes, items, 11))                                                                                                                                                                                              \
                    {                                                                                                                                                                                                                                                    \
                        int w, h;                                                                                                                                                                                                                                        \
                        sscanf_s(items[currentRes], "%dx%d", &w, &h);                                                                                                                                                                                                    \
                        g_pk_config.res_width = w;                                                                                                                                                                                                                       \
                        g_pk_config.res_height = h;                                                                                                                                                                                                                      \
                    }                                                                                                                                                                                                                                                    \
                }                                                                                                                                                                                                                                                        \
            }                                                                                                                                                                                                                                                            \
            else                                                                                                                                                                                                                                                         \
            {                                                                                                                                                                                                                                                            \
                if (ImGui::Checkbox(desc, &v))                                                                                                                                                                                                                           \
                    g_pk_config.name = v;                                                                                                                                                                                                                                \
            }                                                                                                                                                                                                                                                            \
        }                                                                                                                                                                                                                                                                \
        else if (type == TYPE_INT)                                                                                                                                                                                                                                       \
        {                                                                                                                                                                                                                                                                \
            ImGui::InputInt(desc, &g_pk_config.name);                                                                                                                                                                                                                    \
        }                                                                                                                                                                                                                                                                \
        else if (type == TYPE_KEY)                                                                                                                                                                                                                                       \
        {                                                                                                                                                                                                                                                                \
            int k = g_pk_config.name;                                                                                                                                                                                                                                    \
            RenderKeyBind(desc, &k);                                                                                                                                                                                                                                     \
            if (g_pk_config.name != k)                                                                                                                                                                                                                                   \
                g_pk_config.name = k;                                                                                                                                                                                                                                    \
            if (ImGui::IsItemHovered())                                                                                                                                                                                                                                  \
                ImGui::SetTooltip("组合键生效：Ctrl + %s", KeyCodeToString(k).c_str());                                                                                                                                                                                  \
        }                                                                                                                                                                                                                                                                \
        ImGui::Separator();                                                                                                                                                                                                                                              \
    }
#include "config_def.h"
#undef X

        ImGui::EndChild();

        if (ImGui::Button("保存配置", ImVec2(200 * g_dpiScale, 50 * g_dpiScale)))
        {
            ConfigManager::Save();
            ShowSaveNotification("配置已成功保存！请重启游戏生效。");
        }

        if (GetTickCount() - g_saveTime < 3000)
        {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "  %s", g_saveStatus);
        }
    }

    void Render()
    {
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::Begin("LauncherMain", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

        // Sidebar Removed (Request A)
        // Main view switcher

        // Header / Version info could go top right?

        switch (g_currentView)
        {
        case Home:
            RenderMainPage();
            break;
        case Settings:
            RenderSettingsPage();
            break;
        }

        // Footer version info
        ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - 100 * g_dpiScale, ImGui::GetWindowHeight() - 25 * g_dpiScale));
        ImGui::TextDisabled("v%s", VER_FILE_VERSION_STR);

        ImGui::End();
    }

    void Initialize(HWND hwnd, float dpiScale)
    {
        g_dpiScale = dpiScale;
        SetupStyles(dpiScale);
    }

    void Cleanup() {}
}
