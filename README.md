# plugK

《刀剑封魔录》系列游戏增强补丁。

`plugK` 由一个注入式补丁 DLL 和一个可视化启动器组成，目标是修复原版中的体验问题，并提供可按需启用的增强功能。

## 兼容性

- 支持《刀剑封魔录》`v1.05`
- 支持《上古传说》`v2.01`
- 不支持 Steam 版本的 `ComeOn.exe`
- 当前发布与 CI 构建基线为 `Release|x86`

## 最近更新

### v0.6.5.7

- 暂时关闭自动拾取成功后的逐物品获得提示，减少大量拾取时的游戏内文字输出开销
- 自动拾取开关状态和拾取范围切换提示保留

### v0.6.5.5

- 自动拾取获得物品时新增游戏内提示，例如 `[自动拾取] 获得：地煞宝石 数量 1`
- 提示支持 `v1.05` 与 `v2.01`，会显示物品名和数量/金额

### v0.6.5.4

- 新增自动拾取地面物品，支持 `v1.05` 与 `v2.01`
- 自动拾取提供四种范围：仅金钱、金钱+宝石、金钱+宝石+护身石+回复道具、全部物品
- 自动拾取快捷键固定为 `Ctrl+Z` 开关、`Shift+Z` 切换范围
- 自动拾取复用原版 `Z` 拾取逻辑，避免绕过原版地面对象清理流程

### v0.6.5

- 支持 1.05 与 2.01 掉落宝石、护身石等高价值物品名称颜色优化
- 地面物品名称改为按住快捷键临时显示，移除长期显示配置项
- 新增 `Ctrl+X` 拆分背包中第一个可叠加物品
- 修复敌人血量显示异常
- 修复九转和武神丹等高级回复物品误进入快捷栏的问题
- 修正组合快捷键和游戏原生按键逻辑重复生效的问题
- 移除启动器中的掉落倾向优化入口，功能默认关闭

## 主要功能

补丁通过 `PlugK.ini` 配置文件控制，功能大致分为以下几类：

- 界面与显示
  - 自定义分辨率
  - UI 居中修正
  - 禁用部分屏幕震动
  - 存档继承优化
  - 敌人血量显示
- 背包与储物箱
  - 一键整理
  - 扩展存储
  - 自动填充扩展区域
  - 自定义单格物品数量
  - `Ctrl+X` 拆分背包中第一个可叠加物品
- 物品与商店
  - 宝石堆叠
  - 商店无限库存
  - 商店物品堆叠/随机数量
  - 商店内排序
  - 地面物品显名，默认按住 ` 键临时显示
  - 掉落物品名称颜色优化
  - 自动拾取地面物品，支持按范围过滤
- 装备与合成
  - 自主镶嵌
  - 合成逻辑优化
- 角色
  - 一键重置技能点，保留角色初始技能，返还普通技能消耗，并回退必杀技怒气上限
  - 支持 Alt+1-4 快捷释放已学习的四个必杀技，可在配置中关闭
- 快捷键
  - 支持通过 `Ctrl + 自定义按键` 触发常用功能
  - 支持单独配置“长按显示物品名称”的快捷键
  - 自动拾取使用固定快捷键：`Ctrl+Z` 开关，`Shift+Z` 切换范围

## 安装与使用

1. 从 [Releases](https://github.com/cartman-kai/plugK/releases) 下载压缩包。
2. 将以下文件解压到游戏根目录，也就是 `ComeOn.exe` 所在目录：
   - `plugK.dll`
   - `plugKLauncher.exe`
3. 运行 `plugKLauncher.exe`。

首次启动时，启动器会在游戏根目录自动生成默认配置文件 `PlugK.ini`。发布包不依赖预置 INI 文件。

启动器提供：

- 自动检测游戏文件、补丁 DLL、配置文件是否齐全
- 一键启动原版游戏或 MOD 模式
- 图形化修改补丁配置
- 创建原版和 MOD 模式桌面快捷方式

## 配置文件

虽然推荐使用启动器，但也可以手动修改 `PlugK.ini`。

```ini
[UI]
KeepCenter=1             ; 1=画面居中(防晃动) 0=关
disable_screen_shake=1   ; 1=禁用震动 0=开
enable_fix_inheritance=1 ; 1=存档继承优化 0=关
ShowEnemyHp=1            ; 1=显示敌人血量 0=关
Enabled=1                ; 1=启用自定义分辨率
Width=1280               ; 宽度
Height=720               ; 高度

[Player]
EnableSort=1             ; 1=启用一键整理
EnableExt=1              ; 1=启用大箱子
AutoFillExt=1            ; 1=扩展箱子自动填充
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

[Hotkeys]
; 使用 Windows VK 键值，默认搭配 Ctrl 组合键
; 自动拾取固定使用 Ctrl+Z / Shift+Z，不在配置文件中自定义
StashSwap=188            ; 储物箱切换 (A/B) - [ , ]
StashSort=219            ; 储物箱整理 - [ [ ]
InvPrev=190              ; 背包切换 (A/B) - [ . ]
InvSort=220              ; 全背包整理 - [ \ ]
InvSortCurrent=191       ; 当前页整理 - [ / ]
switch_gem_stack=222     ; 切换宝石叠加开关 - [ ' ]
HoldShowItemName=192     ; 长按显示物品名 - [ ` ]
SkillRespec=8            ; 洗技能 - [ Backspace ]
```

## 第三方组件

- Dear ImGui: `launcher/deps/imgui/`, MIT，许可证见 `launcher/deps/imgui/LICENSE.txt`
- MinHook: `plugK/deps/minhook/`, BSD-2-Clause，许可证见 `plugK/deps/minhook/LICENSE.txt`

更完整的归属说明见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。

## 说明

本项目主要使用 C 实现补丁逻辑，启动器使用 C++ 和 ImGui 实现。项目仅用于技术学习与单机游戏体验增强交流。
