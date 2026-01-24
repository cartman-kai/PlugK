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
    static AppView g_lastView = Home;

    static char g_saveStatus[256] = {0};
    static DWORD g_saveTime = 0;
    static float g_dpiScale = 1.0f;
    static HWND g_hwnd = NULL;

    void SetupStyles(float dpiScale)
    {
        ImGuiStyle &style = ImGui::GetStyle();
        ImGui::StyleColorsDark();

        // Rounded corners - consistent across all elements
        style.WindowRounding = 6.0f;
        style.ChildRounding = 5.0f;
        style.FrameRounding = 4.0f;
        style.PopupRounding = 6.0f;
        style.GrabRounding = 3.0f;
        style.ScrollbarSize = 10.0f;
        style.ScrollbarRounding = 6.0f;

        // Increased vertical spacing for breathing room
        style.ItemSpacing = ImVec2(8 * dpiScale, 6 * dpiScale);
        style.FramePadding = ImVec2(10 * dpiScale, 6 * dpiScale);
        style.WindowPadding = ImVec2(14 * dpiScale, 14 * dpiScale);

        ImVec4 *colors = style.Colors;
        colors[ImGuiCol_WindowBg] = ImVec4(0.09f, 0.09f, 0.11f, 1.00f);
        colors[ImGuiCol_ChildBg] = ImVec4(0.13f, 0.13f, 0.15f, 1.00f);
        colors[ImGuiCol_Header] = ImVec4(0.20f, 0.20f, 0.22f, 1.00f);
        colors[ImGuiCol_HeaderHovered] = ImVec4(0.26f, 0.26f, 0.28f, 1.00f);
        // Tab selected color - slightly muted blue
        colors[ImGuiCol_HeaderActive] = ImVec4(0.18f, 0.50f, 0.75f, 1.00f);

        // Primary Button - Vibrant Blue
        colors[ImGuiCol_Button] = ImVec4(0.20f, 0.58f, 0.88f, 1.00f);
        colors[ImGuiCol_ButtonHovered] = ImVec4(0.26f, 0.64f, 0.92f, 1.00f);
        colors[ImGuiCol_ButtonActive] = ImVec4(0.16f, 0.52f, 0.80f, 1.00f);

        // Checkbox checkmark - match button color
        colors[ImGuiCol_CheckMark] = ImVec4(0.20f, 0.58f, 0.88f, 1.00f);

        // Frame background for inputs/checkboxes
        colors[ImGuiCol_FrameBg] = ImVec4(0.16f, 0.16f, 0.18f, 1.00f);
        colors[ImGuiCol_FrameBgHovered] = ImVec4(0.20f, 0.20f, 0.22f, 1.00f);
        colors[ImGuiCol_FrameBgActive] = ImVec4(0.24f, 0.24f, 0.26f, 1.00f);

        // Border
        colors[ImGuiCol_Border] = ImVec4(0.32f, 0.32f, 0.36f, 0.60f);
        style.FrameBorderSize = 0.0f;
    }

    // --- Key Binding Helpers ---
    std::string KeyCodeToString(int key)
    {
        char buf[64] = {0};
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
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 100 * g_dpiScale);

        std::string btnLabel = KeyCodeToString(*keyVal);
        if (btnLabel.empty())
            btnLabel = "NONE";

        ImGui::PushID(label);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.18f, 0.20f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);

        if (ImGui::Button(btnLabel.c_str(), ImVec2(100 * g_dpiScale, 0)))
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
        ImVec4 color = ok ? ImVec4(0.35f, 0.85f, 0.35f, 1.0f) : ImVec4(0.90f, 0.35f, 0.35f, 1.0f);
        ImGui::TextColored(color, ok ? "[OK]" : "[X]");
        ImGui::SameLine(0, 4 * g_dpiScale);
        ImGui::Text("%s", label);
    }

    void RenderMainPage()
    {
        auto status = ModLoader::CheckStatus();
        float availW = ImGui::GetContentRegionAvail().x;

        // --- Compact Horizontal Status Bar (40px height, no scrollbar) ---
        ImGui::BeginChild("StatusPanel", ImVec2(0, 40 * g_dpiScale), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        // Center vertically in the status bar
        float textH = ImGui::GetTextLineHeight();
        float padY = (40 * g_dpiScale - textH) * 0.5f;
        ImGui::SetCursorPosY(padY);

        RenderStatusIcon("游戏", status.exeExists);
        ImGui::SameLine(0, 16 * g_dpiScale);
        RenderStatusIcon("插件", status.dllExists);
        ImGui::SameLine(0, 16 * g_dpiScale);
        RenderStatusIcon("配置", status.iniExists);

        if (!status.exeExists || !status.dllExists)
        {
            ImGui::SameLine(0, 16 * g_dpiScale);
            ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.35f, 1.0f), "| 部分功能不可用");
        }
        ImGui::EndChild();

        // --- Core Actions Container ---
        ImGui::BeginChild("ActionContainer", ImVec2(0, 0), false);

        // Unified button size
        ImVec2 buttonSize = ImVec2(300 * g_dpiScale, 45 * g_dpiScale);
        float cursorX = (availW - buttonSize.x) * 0.5f;

        // Top spacing for visual balance
        ImGui::Dummy(ImVec2(0, 10 * g_dpiScale));

        // Secondary button colors (neutral gray with hover/active states)
        ImVec4 secondaryBtnColor = ImVec4(0.20f, 0.20f, 0.22f, 1.0f);
        ImVec4 secondaryBtnHovered = ImVec4(0.35f, 0.35f, 0.38f, 1.0f);
        ImVec4 secondaryBtnActive = ImVec4(0.28f, 0.28f, 0.30f, 1.0f);

        // Helper lambda for centered buttons with custom colors
        auto RenderCenteredButton = [&](const char *label, bool enabled, const ImVec4 &btnColor,
                                        const ImVec4 &hoverColor, const ImVec4 &activeColor) -> bool
        {
            ImGui::SetCursorPosX(cursorX);
            if (!enabled)
            {
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);
            }
            ImGui::PushStyleColor(ImGuiCol_Button, btnColor);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hoverColor);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, activeColor);

            bool clicked = ImGui::Button(label, buttonSize);

            ImGui::PopStyleColor(3);
            if (!enabled)
            {
                ImGui::PopStyleVar();
            }
            return clicked && enabled;
        };

        // 1. Launch Original (Green tones)
        ImVec4 greenBtn = ImVec4(0.22f, 0.52f, 0.32f, 1.0f);
        ImVec4 greenHover = ImVec4(0.28f, 0.58f, 0.38f, 1.0f);
        ImVec4 greenActive = ImVec4(0.18f, 0.46f, 0.28f, 1.0f);

        if (RenderCenteredButton("启动原版游戏", status.exeExists, greenBtn, greenHover, greenActive))
        {
            ModLoader::LaunchOriginal();
        }
        ImGui::Dummy(ImVec2(0, 10 * g_dpiScale));

        // 2. Launch MOD (Main action - Vibrant Blue, uses default style)
        ImGui::SetCursorPosX(cursorX);
        bool canLaunchMod = status.exeExists && status.dllExists;
        if (!canLaunchMod)
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);

        if (ImGui::Button("启动 MOD 模式", buttonSize))
        {
            if (canLaunchMod)
                ModLoader::LaunchWithMod();
        }

        if (!canLaunchMod)
            ImGui::PopStyleVar();

        ImGui::Dummy(ImVec2(0, 10 * g_dpiScale));

        // 3. Config (Secondary Style with enhanced hover/active)
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
        if (RenderCenteredButton("配置管理", true, secondaryBtnColor, secondaryBtnHovered, secondaryBtnActive))
        {
            g_currentView = Settings;
        }
        ImGui::PopStyleVar();

        ImGui::Dummy(ImVec2(0, 10 * g_dpiScale));

        // 4. Create Shortcut (Secondary Style)
        bool enableShortcut = status.exeExists && status.dllExists && status.iniExists;

        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
        if (RenderCenteredButton("创建桌面快捷方式", enableShortcut, secondaryBtnColor, secondaryBtnHovered, secondaryBtnActive))
        {
            ImGui::OpenPopup("CreateShortcutPopup");
        }
        ImGui::PopStyleVar();

        // Popup for shortcut creation
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12 * g_dpiScale, 12 * g_dpiScale));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.11f, 0.11f, 0.13f, 1.00f));
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

            float itemH = 30 * g_dpiScale;
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
        float availW = ImGui::GetContentRegionAvail().x;

        // --- Back Button (small, enhanced visibility) ---
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.25f, 0.28f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.35f, 0.35f, 0.38f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.30f, 0.30f, 0.33f, 1.00f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
        if (ImGui::Button("< 返回", ImVec2(75 * g_dpiScale, 26 * g_dpiScale)))
        {
            g_currentView = Home;
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);

        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.50f, 0.50f, 0.55f, 1.0f), "| 详细设置");
        ImGui::Separator();

        // --- Sub-tabs ---
        ImGui::BeginGroup();
        static std::string activeSubTab = "UI";
        const char *subTabs[] = {"UI", "Inventory", "Item&Shop", "Equipment", "Hotkeys"};
        const char *subTabsCN[] = {"界面显示", "背包仓库", "物品商店", "装备合成", "快捷按键"};

        float tabW = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 4) / 5.0f;

        // Tab colors - muted blue for selected
        ImVec4 tabActiveColor = ImVec4(0.18f, 0.50f, 0.75f, 1.00f);
        ImVec4 tabInactiveColor = ImVec4(0.15f, 0.15f, 0.17f, 1.00f);

        for (int i = 0; i < 5; i++)
        {
            if (i > 0)
                ImGui::SameLine();
            bool active = (activeSubTab == subTabs[i]);

            ImGui::PushStyleColor(ImGuiCol_Button, active ? tabActiveColor : tabInactiveColor);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, active ? tabActiveColor : ImVec4(0.22f, 0.22f, 0.24f, 1.00f));

            if (ImGui::Button(subTabsCN[i], ImVec2(tabW, 28 * g_dpiScale)))
            {
                activeSubTab = subTabs[i];
            }
            ImGui::PopStyleColor(2);
        }
        ImGui::EndGroup();

        ImGui::Separator();

        // --- Content Area with scroll (reserve 55px at bottom for save button) ---
        ImGui::BeginChild("SettingsContent", ImVec2(0, -55 * g_dpiScale), true, ImGuiWindowFlags_AlwaysVerticalScrollbar);

        // Compact frame padding for checkboxes
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(3 * g_dpiScale, 3 * g_dpiScale));

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
                    ImGui::SetNextItemWidth(150 * g_dpiScale);                                                                                                                                                                                                           \
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
            ImGui::SetNextItemWidth(100 * g_dpiScale);                                                                                                                                                                                                                   \
            ImGui::InputInt(desc, &g_pk_config.name, 0, 0);                                                                                                                                                                                                              \
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

        ImGui::PopStyleVar(); // FramePadding
        ImGui::EndChild();

        // --- Save Button (bright green, centered) ---
        float saveBtnWidth = 160.0f * g_dpiScale;
        float saveCursorX = (availW - saveBtnWidth) * 0.5f;
        ImGui::SetCursorPosX(saveCursorX);

        // Bright success green for save button
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.70f, 0.40f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.76f, 0.46f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.22f, 0.64f, 0.36f, 1.00f));

        if (ImGui::Button("保存配置", ImVec2(saveBtnWidth, 38 * g_dpiScale)))
        {
            ConfigManager::Save();
            ShowSaveNotification("配置已保存！重启游戏生效。");
        }

        ImGui::PopStyleColor(3);

        if (GetTickCount() - g_saveTime < 3000)
        {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.40f, 1.0f), "%s", g_saveStatus);
        }
    }

    bool GetDesiredWindowSize(int *outWidth, int *outHeight)
    {
        if (g_currentView == g_lastView)
            return false;

        g_lastView = g_currentView;

        if (g_currentView == Home)
        {
            *outWidth = (int)(480 * g_dpiScale);
            *outHeight = (int)(380 * g_dpiScale);
        }
        else
        {
            *outWidth = (int)(750 * g_dpiScale);
            *outHeight = (int)(550 * g_dpiScale);
        }
        return true;
    }

    void Render()
    {
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::Begin("LauncherMain", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

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
        ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - 75 * g_dpiScale, ImGui::GetWindowHeight() - 20 * g_dpiScale));
        ImGui::TextDisabled("v%s", VER_FILE_VERSION_STR);

        ImGui::End();
    }

    void Initialize(HWND hwnd, float dpiScale)
    {
        g_hwnd = hwnd;
        g_dpiScale = dpiScale;
        g_currentView = Home;
        g_lastView = Home;
        SetupStyles(dpiScale);
    }

    void Cleanup() {}
}
