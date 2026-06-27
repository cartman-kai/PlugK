# Repository Guidelines

## 项目结构与模块组织
`plugK.sln` 包含两个主要项目。`plugK/` 使用 C 构建补丁 DLL：头文件位于 `plugK/inc`，Hook 实现与功能模块位于 `plugK/src`，`plugK/deps/minhook` 中保存了随仓库提交的 MinHook 依赖。`launcher/` 使用 C++ 和 ImGui 构建 Windows 启动器：源码位于 `launcher/src`，对外头文件位于 `launcher/inc`，界面资源位于 `launcher/res`，ImGui 依赖位于 `launcher/deps/imgui`。构建产物输出到 `bin/Win32/<Configuration>/`，中间文件输出到 `build/<ProjectName>/Win32/<Configuration>/`。`tmp/` 仅作为调研与草稿目录，不应视为正式源码。

## 构建、测试与开发命令
请使用 Visual Studio 2022 和 `v143` 工具集，或在 Developer PowerShell 中执行：

```powershell
msbuild plugK.sln /p:Configuration=Release /p:Platform=x86
msbuild plugK.sln /p:Configuration=Debug /p:Platform=x86
```

`Release|x86` 与现有 GitHub Actions 工作流保持一致，输出 `plugK.dll` 和 `plugKLauncher.exe` 到 `bin/Win32/Release/`。本地验证和提交前检查必须至少构建 `Release|x86`；`Debug|x86` 仅用于调试 Hook 或启动器界面，不能替代 Release 构建。调试时优先直接打开 `plugK.sln`。

## 代码风格与命名约定
遵循现有文件风格，不要做大范围格式化。补丁侧 C 代码普遍使用 include guard、按功能分组的 `#include`，以及类似 `Mod_inv_auto_sort_init` 这样的 PascalCase 初始化函数名。启动器侧 C++ 代码使用 `#pragma once`、小型辅助类，并且多个文件采用 2 空格缩进。新增模块请沿用现有命名模式，例如 `feature_name.c` 与对应的 `feature_name.h`。

## 换行与编码
本仓库面向 Windows 与 Visual Studio，源码、项目文件、Markdown 与配置文件默认使用 CRLF 换行。修改文件时应保持原文件换行风格，新增文本文件优先使用 CRLF，避免因编辑器或脚本把整文件批量转换为 LF。除非任务明确要求，不要提交只包含换行、编码或格式化变化的改动。

## 逆向知识库
`docs/reverse_kb/` 是每次功能开发和逆向分析的长期知识库，不属于 `tmp/` 草稿目录。涉及 Hook、函数地址、返回地址边界、游戏版本差异、数据表、运行时结构或调试验证的改动，应先查阅 `docs/reverse_kb/README.md` 及相关专题文件，再开始实现。

新增或修正逆向结论时，应按知识库规范回写到对应位置：系统逻辑写入 `systems/`，运行时对象与全局变量写入 `runtime/`，版本特定地址写入 `ida/`，数据表与资源格式写入 `data_tables.md`。尚未确认的推测先记录到 `research_queue.md`，确认后再迁移到正式专题。记录 Hook 相关结论时应注明版本号、目标地址或函数名、调用来源/返回地址边界、不应影响的路径以及已完成的手工验证。

## Hook 开发规范
新增或修改 MinHook Hook 时，必须先在源码和 `docs/reverse_kb/` 中搜索目标地址、函数名和相关返回地址，确认该目标是否已经被其它功能 Hook。

同一目标地址不应由多个功能各自 `MH_CreateHook`。若多个功能需要拦截同一个游戏函数，应使用共享 Hook、分发函数或明确的链式设计，并在知识库中记录：
- 目标版本与地址。
- 当前 Hook 的归属模块。
- 共享/分发的调用边界。
- 与其它功能的交互关系。

通用函数尤其要谨慎，例如文字绘制、随机数、物品查询、掉落选择等。修改这类 Hook 前必须检查现有 Hook，避免重复 Hook 导致后初始化功能静默失效。

## 测试指南
当前仓库没有自动化单元测试。一个改动至少应满足以下验证要求：
1. 能够在 `Release|x86` 配置下成功构建；如额外构建 `Debug|x86`，也必须同时确认 Release 构建通过。
2. 能通过 `plugKLauncher.exe` 或目标游戏正常加载，且没有明显回归。
3. 提供受影响功能的手工验证说明，例如背包整理、分辨率修改或继承逻辑。

## 提交与 Pull Request 规范
最近提交历史以简短、直接的主题为主，常见为中文短句或简短英文修复说明，例如 `upgrade version;`、`fix tips & 105 load crash;`。提交应尽量单一、聚焦。发起 Pull Request 时，请附上清晰摘要、影响的游戏版本范围、手工测试步骤；若修改了启动器 UI，还应补充截图。若改动依赖 `tmp/` 中的调研记录，请在描述中一并关联。

## 安全与配置提示
不要提交本地游戏路径、生成的二进制文件或个人 Visual Studio 配置文件。更新版本号时请同步检查 `version_info.h`，涉及打包流程的修改请同时核对 `.github/workflows/build.yml`。
