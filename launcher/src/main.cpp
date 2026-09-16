#pragma comment(linker, "/subsystem:windows /ENTRY:mainCRTStartup")
#define _CRT_SECURE_NO_WARNINGS

// Windows & System
#include <d3d9.h>
#include <stdio.h>
#include <string>
#include <tchar.h>
#include <windows.h>
#include <wchar.h>


// Third-party (ImGui)
#include "imgui.h"
#include "imgui_impl_dx9.h"
#include "imgui_impl_win32.h"

// Project
#include "../inc/config_manager.h"
#include "../inc/mod_loader.h"
#include "../inc/ui_manager.h"
#include "../inc/utils.h"
#include "../resource.h"


#pragma comment(lib, "d3d9.lib")

// --- Globals ---
static LPDIRECT3D9 g_pD3D = NULL;
static LPDIRECT3DDEVICE9 g_pd3dDevice = NULL;
static D3DPRESENT_PARAMETERS g_d3dpp = {};
static HANDLE g_hInstanceMutex = NULL;
static const UINT kActivateLauncherMessage = WM_APP + 1;

// --- Declarations ---
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void ResetDevice();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static std::wstring GetLauncherDirectoryW() {
  wchar_t modulePath[MAX_PATH] = {};
  DWORD length = GetModuleFileNameW(NULL, modulePath, _countof(modulePath));
  if (length == 0 || length >= _countof(modulePath))
    return L"";

  std::wstring path(modulePath, length);
  size_t slash = path.find_last_of(L"\\/");
  if (slash == std::wstring::npos)
    return L"";

  std::wstring directory = path.substr(0, slash);
  if (directory.size() == 2 && directory[1] == L':')
    directory += L"\\";

  wchar_t fullPath[MAX_PATH] = {};
  DWORD fullLength = GetFullPathNameW(directory.c_str(), _countof(fullPath),
                                      fullPath, NULL);
  if (fullLength > 0 && fullLength < _countof(fullPath))
    directory.assign(fullPath, fullLength);

  if (!directory.empty())
    CharLowerBuffW(&directory[0], (DWORD)directory.size());
  return directory;
}

static unsigned long long HashDirectory(const std::wstring &directory) {
  unsigned long long hash = 1469598103934665603ULL;
  for (size_t i = 0; i < directory.size(); ++i) {
    hash ^= (unsigned long long)(unsigned short)directory[i];
    hash *= 1099511628211ULL;
  }
  return hash;
}

static std::wstring BuildInstanceName(unsigned long long directoryHash) {
  wchar_t name[64] = {};
  swprintf_s(name, _countof(name), L"Local\\PlugKLauncher_%016llX",
             directoryHash);
  return std::wstring(name);
}

static std::wstring BuildWindowClassName(unsigned long long directoryHash) {
  wchar_t name[64] = {};
  swprintf_s(name, _countof(name), L"PlugKLauncher_%016llX", directoryHash);
  return std::wstring(name);
}

static void BringLauncherToFront(HWND hwnd) {
  if (!IsWindow(hwnd))
    return;

  if (IsIconic(hwnd))
    ShowWindow(hwnd, SW_RESTORE);
  else
    ShowWindow(hwnd, SW_SHOW);
  BringWindowToTop(hwnd);
  SetForegroundWindow(hwnd);
}

// Returns 1 when this process owns the instance, 0 when another instance is
// already running, and -1 when the mutex could not be created.
static int AcquireSingleInstance(const std::wstring &mutexName,
                                 const std::wstring &windowClassName) {
  HANDLE mutex = CreateMutexW(NULL, TRUE, mutexName.c_str());
  if (!mutex) {
    MessageBoxW(NULL, L"无法创建启动器单例锁。", L"PlugK", MB_ICONERROR);
    return -1;
  }

  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    HWND existing = NULL;
    for (int i = 0; i < 20 && !existing; ++i) {
      existing = FindWindowW(windowClassName.c_str(), NULL);
      if (!existing)
        Sleep(50);
    }

    if (existing) {
      BringLauncherToFront(existing);
      PostMessageW(existing, kActivateLauncherMessage, 0, 0);
    }
    CloseHandle(mutex);
    return 0;
  }

  g_hInstanceMutex = mutex;
  return 1;
}

static void ReleaseSingleInstance() {
  if (g_hInstanceMutex) {
    CloseHandle(g_hInstanceMutex);
    g_hInstanceMutex = NULL;
  }
}

static std::wstring BuildWindowTitle(int gameVersion) {
  const wchar_t *versionText = L"未知";
  if (gameVersion == 105)
    versionText = L"1.05";
  else if (gameVersion == 201)
    versionText = L"2.01";

  wchar_t title[128] = {};
  swprintf_s(title, _countof(title), L"PlugK 游戏启动器 - 游戏版本 %s",
             versionText);
  return std::wstring(title);
}

static bool LoadChineseFont(ImGuiIO &io, float dpiScale) {
  char windowsDirectory[MAX_PATH] = {};
  UINT length = GetWindowsDirectoryA(windowsDirectory,
                                     _countof(windowsDirectory));
  if (length == 0 || length >= _countof(windowsDirectory))
    return false;

  std::string fontDirectory(windowsDirectory, length);
  if (!fontDirectory.empty() && fontDirectory.back() != '\\')
    fontDirectory += '\\';
  fontDirectory += "Fonts\\";

  const char *fontNames[] = {
      "msyh.ttc",   // Microsoft YaHei
      "simhei.ttf", // SimHei
      "simsun.ttc"  // SimSun
  };

  for (size_t i = 0; i < _countof(fontNames); ++i) {
    std::string fontPath = fontDirectory + fontNames[i];
    DWORD attributes = GetFileAttributesA(fontPath.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & FILE_ATTRIBUTE_DIRECTORY))
      continue;

    if (io.Fonts->AddFontFromFileTTF(
            fontPath.c_str(), 16.0f * dpiScale, NULL,
            io.Fonts->GetGlyphRangesChineseFull()) != NULL)
      return true;
  }

  return false;
}

int main(int argc, char **argv) {
  // Check command line args
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--mod") == 0) {
      ModLoader::LaunchWithMod();
      return 0;
    } else if (strcmp(argv[i], "--original") == 0) {
      ModLoader::LaunchOriginal();
      return 0;
    }
  }

  SetProcessDPIAware();
  float dpiScale = Utils::GetDPIScale();

  std::wstring launcherDirectory = GetLauncherDirectoryW();
  if (launcherDirectory.empty()) {
    MessageBoxW(NULL, L"无法确定启动器所在目录。", L"PlugK", MB_ICONERROR);
    return 1;
  }

  unsigned long long directoryHash = HashDirectory(launcherDirectory);
  std::wstring mutexName = BuildInstanceName(directoryHash);
  std::wstring windowClassName = BuildWindowClassName(directoryHash);
  int instanceStatus =
      AcquireSingleInstance(mutexName, windowClassName);
  if (instanceStatus != 1)
    return instanceStatus < 0 ? 1 : 0;

  int gameVersion = ModLoader::GetGameVersion();

  // Setup paths
  char exePath[MAX_PATH];
  GetModuleFileNameA(NULL, exePath, MAX_PATH);
  char *lastSlash = strrchr(exePath, '\\');
  if (lastSlash)
    *(lastSlash + 1) = '\0';
  std::string iniPath = std::string(exePath) + "PlugK.ini";

  // Initialize Config
  ConfigManager::Initialize(iniPath);
  if (ConfigManager::NeedsGeneration()) {
    ConfigManager::GenerateDefault(iniPath);
    ConfigManager::Initialize(iniPath); // Reload
  }

  // Register class
  HINSTANCE hInstance = GetModuleHandle(NULL);
  HICON hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON1));
  WNDCLASSEXW wc = {};
  wc.cbSize = sizeof(wc);
  wc.style = CS_CLASSDC;
  wc.lpfnWndProc = WndProc;
  wc.hInstance = hInstance;
  wc.hIcon = hIcon;
  wc.lpszClassName = windowClassName.c_str();
  wc.hIconSm = hIcon;
  if (!RegisterClassExW(&wc)) {
    ReleaseSingleInstance();
    return 1;
  }

  int winW = (int)(480 * dpiScale); // Home view size
  int winH = (int)(380 * dpiScale);

  std::wstring windowTitle = BuildWindowTitle(gameVersion);
  HWND hwnd = CreateWindowW(wc.lpszClassName, windowTitle.c_str(),
                            WS_OVERLAPPEDWINDOW, 100, 100, winW, winH, NULL,
                            NULL, wc.hInstance, NULL);

  if (!hwnd) {
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    ReleaseSingleInstance();
    return 1;
  }

  if (!CreateDeviceD3D(hwnd)) {
    CleanupDeviceD3D();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    ReleaseSingleInstance();
    return 1;
  }

  ShowWindow(hwnd, SW_SHOWDEFAULT);
  UpdateWindow(hwnd);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  io.IniFilename = NULL;

  UIManager::Initialize(hwnd, dpiScale, gameVersion);

  // Font loading
  LoadChineseFont(io, dpiScale);

  ImGui_ImplWin32_Init(hwnd);
  ImGui_ImplDX9_Init(g_pd3dDevice);

  bool done = false;
  while (!done) {
    MSG msg;
    while (::PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE)) {
      ::TranslateMessage(&msg);
      ::DispatchMessage(&msg);
      if (msg.message == WM_QUIT)
        done = true;
    }
    if (done)
      break;

    // Dynamic window size based on current view
    int newW, newH;
    if (UIManager::GetDesiredWindowSize(&newW, &newH)) {
      // Get current window rect to calculate border/title size
      RECT clientRect, windowRect;
      GetClientRect(hwnd, &clientRect);
      GetWindowRect(hwnd, &windowRect);
      int borderW = (windowRect.right - windowRect.left) - clientRect.right;
      int borderH = (windowRect.bottom - windowRect.top) - clientRect.bottom;

      // Center the new window position
      int screenW = GetSystemMetrics(SM_CXSCREEN);
      int screenH = GetSystemMetrics(SM_CYSCREEN);
      int posX = (screenW - (newW + borderW)) / 2;
      int posY = (screenH - (newH + borderH)) / 2;

      SetWindowPos(hwnd, NULL, posX, posY, newW + borderW, newH + borderH,
                   SWP_NOZORDER);
    }

    ImGui_ImplDX9_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    UIManager::Render();

    ImGui::EndFrame();

    g_pd3dDevice->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
                        D3DCOLOR_RGBA(30, 30, 35, 255), 1.0f, 0);
    if (g_pd3dDevice->BeginScene() >= 0) {
      ImGui::Render();
      ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
      g_pd3dDevice->EndScene();
    }

    HRESULT result = g_pd3dDevice->Present(NULL, NULL, NULL, NULL);
    if (result == D3DERR_DEVICELOST &&
        g_pd3dDevice->TestCooperativeLevel() == D3DERR_DEVICENOTRESET)
      ResetDevice();
  }

  ImGui_ImplDX9_Shutdown();
  ImGui_ImplWin32_Shutdown();
  ImGui::DestroyContext();
  CleanupDeviceD3D();
  DestroyWindow(hwnd);
  UnregisterClassW(wc.lpszClassName, wc.hInstance);
  ReleaseSingleInstance();

  return 0;
}

// --- D3D Helpers ---
bool CreateDeviceD3D(HWND hWnd) {
  if ((g_pD3D = Direct3DCreate9(D3D_SDK_VERSION)) == NULL)
    return false;
  ZeroMemory(&g_d3dpp, sizeof(g_d3dpp));
  g_d3dpp.Windowed = TRUE;
  g_d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
  g_d3dpp.BackBufferFormat = D3DFMT_UNKNOWN;
  g_d3dpp.EnableAutoDepthStencil = TRUE;
  g_d3dpp.AutoDepthStencilFormat = D3DFMT_D16;
  g_d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
  if (g_pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd,
                           D3DCREATE_HARDWARE_VERTEXPROCESSING, &g_d3dpp,
                           &g_pd3dDevice) < 0)
    return false;
  return true;
}

void CleanupDeviceD3D() {
  if (g_pd3dDevice) {
    g_pd3dDevice->Release();
    g_pd3dDevice = NULL;
  }
  if (g_pD3D) {
    g_pD3D->Release();
    g_pD3D = NULL;
  }
}

void ResetDevice() {
  ImGui_ImplDX9_InvalidateDeviceObjects();
  g_pd3dDevice->Reset(&g_d3dpp);
  ImGui_ImplDX9_CreateDeviceObjects();
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd,
                                                             UINT msg,
                                                             WPARAM wParam,
                                                             LPARAM lParam);
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
    return true;
  switch (msg) {
  case kActivateLauncherMessage:
    BringLauncherToFront(hWnd);
    return 0;
  case WM_SIZE:
    if (g_pd3dDevice != NULL && wParam != SIZE_MINIMIZED) {
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
