# plugK

《刀剑封魔录》系列游戏增强补丁。

`plugK` 由一个注入式补丁 DLL 和一个可视化启动器组成，目标是修复原版中的体验问题，并提供可按需启用的增强功能。

## 兼容性

- 支持《刀剑封魔录》`v1.05`
- 支持《上古传说》`v2.01`
- v0.7.2 开始支持 Steam 版本

## 主要功能

补丁通过 `PlugK.ini` 配置文件控制，功能大致分为以下几类：

- 界面与显示
  - 自定义分辨率
  - UI 居中修正
  - 禁用部分屏幕震动
  - 存档继承优化
  - 敌人血量显示
  - 掉落物品名称颜色优化
- 背包与储物箱
  - 一键整理
  - 扩展存储
  - 自动填充扩展区域
  - 自定义单格物品数量
  - 拆分背包中第一个可叠加物品；背包已满时拆出的物品会掉落到地面
- 物品与商店
  - 宝石堆叠
  - 商店无限库存
  - 商店物品堆叠/随机数量
  - 商店内排序
  - 地面物品显名，默认按住 ` 键临时显示
  - 自动拾取地面物品，支持按范围过滤
  - 敌人随机掉落倾向优化，偏向玩家持有较少的同类型、同掉落档位物品
- 装备与合成
  - 自主镶嵌
  - 合成逻辑优化，每次仅消耗一个宝石
- 角色
  - 一键重置技能点，保留角色初始技能，返还普通技能消耗，并回退必杀技怒气上限
  - 支持 Alt+1-4 快捷释放已学习的四个必杀技，可在配置中关闭
- 快捷键
  - 支持通过 `Ctrl + 自定义按键` 触发常用功能
  - 支持单独配置“长按显示物品名称”的快捷键
  - 自动拾取使用固定快捷键：`Ctrl+Z` 开关，`Shift+Z` 切换范围
  - 必杀技使用固定快捷键：`Alt+1` 到 `Alt+4`

## 安装与使用

1. 从 [Releases](https://github.com/cartman-kai/plugK/releases) 下载压缩包。
2. 将以下文件解压到游戏根目录，也就是 `ComeOn.exe` 所在目录：
   - `plugK.dll`
   - `plugKLauncher.exe`
   - `PlugKLauncherHook.dll`
3. 运行 `plugKLauncher.exe`。

首次启动时，启动器会在游戏根目录自动生成默认配置文件 `PlugK.ini`。发布包不依赖预置 INI 文件。

启动器提供：

- 自动检测游戏文件、补丁 DLL、配置文件是否齐全
- 一键启动原版游戏或 MOD 模式
- 图形化修改补丁配置
- 创建原版和 MOD 模式桌面快捷方式

## 配置文件

虽然推荐使用启动器，但也可以手动修改 `PlugK.ini`。下面示例包含启动器可见配置，以及少数仅用于手动调整或排查问题的 `Hidden`、`Debug` 配置。

```ini
[UI]
KeepCenter=1             ; 1=画面居中(防晃动) 0=关
disable_screen_shake=1   ; 1=禁用震动 0=开
enable_fix_inheritance=1 ; 1=存档继承优化 0=关
ShowEnemyHp=1            ; 1=显示敌人血量 0=关
OptimizeDropItemNameColor=1 ; 1=优化掉落物品名称颜色 0=关
Enabled=1                ; 1=启用自定义分辨率
Width=1280               ; 宽度
Height=720               ; 高度

[Player]
EnableSort=1             ; 1=启用一键整理
EnableExt=1              ; 1=启用大箱子
AutoFillExt=1            ; 1=扩展背包自动填充
ItemStackLimitEnabled=1  ; 1=启用单格物品数量上限
ItemStackLimit=99        ; 单格物品数量上限 (1-127)
EnableSkillRespec=1      ; 1=启用一键洗技能
EnableUltimateHotkey=1   ; 1=启用 Alt+1-4 快捷释放必杀技

[Item&Shop]
EnableGemStack=1         ; 1=宝石堆叠
InfStock=0               ; 1=商店无限库存
OptimizeItem=1           ; 1=商店堆叠/随机数量
EnableSort=1             ; 1=商店内排序
EnableHoldShowItemName=1 ; 1=启用长按快捷键显示地面物品名

[Equipment]
EnableGemInsert=1        ; 1=自己镶嵌宝石
EnableFuseOpt=1          ; 1=优化合成逻辑

[Hidden]
EnableDropBias=1         ; 1=启用掉落倾向优化
EnableShowItemName=0     ; 1=长期显示地面物品名称，通常保持关闭

[Hotkeys]
; 使用 Windows VK 键值，默认搭配 Ctrl 组合键
; HoldShowItemName 为单独长按键，不需要 Ctrl
; 自动拾取固定使用 Ctrl+Z / Shift+Z，不在配置文件中自定义
; 必杀技固定使用 Alt+1 到 Alt+4，不在配置文件中自定义
StashSwap=188            ; 储物箱切换 (A/B) - [ , ]
StashSort=219            ; 储物箱整理 - [ [ ]
InvPrev=190              ; 背包切换 (A/B) - [ . ]
InvSort=220              ; 全背包整理 - [ \ ]
InvSortCurrent=191       ; 当前页整理 - [ / ]
switch_gem_stack=222     ; 切换宝石叠加开关 - [ ' ]
key_switch_show_item_name=221 ; 保留项，当前切换逻辑未启用
HoldShowItemName=192     ; 长按显示物品名 - [ ` ]
SkillRespec=8            ; 洗技能 - [ Backspace ]
SplitStack=88            ; 拆分第一个叠加物品 - [ X ]

[Debug]
DropBiasDebug=0          ; 1=显示掉落倾向调试提示
DropBiasTrace=0          ; 1=输出掉落倾向详细调试日志
```

## 第三方组件

- Dear ImGui: `launcher/deps/imgui/`, MIT，许可证见 `launcher/deps/imgui/LICENSE.txt`
- MinHook: `plugK/deps/minhook/`, BSD-2-Clause，许可证见 `plugK/deps/minhook/LICENSE.txt`

更完整的归属说明见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。

## 说明

本项目主要使用 C 实现补丁逻辑，启动器使用 C++ 和 ImGui 实现。项目仅用于技术学习与单机游戏体验增强交流。
