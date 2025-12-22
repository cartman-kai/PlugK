# plugK - 《刀剑封魔录》系列游戏功能增强补丁

plugK 是一个基于 C 语言开发的 DLL 补丁，旨在修复原版游戏中的体验问题并强化游戏性。

同时支持《刀剑封魔录》v 1.05 和《上古传说》v 2.01 两个版本的游戏。

由于 Steam 版本的 ComeOn.exe 有特殊修改，所以不支持。

**不支持 Steam 版本**

**不支持 Steam 版本**

**不支持 Steam 版本**



## 🌟 主要功能


- **商店优化**：卖出回复类物品和道具后库存不消失，可重复购买。
- **一键整理**：玩家背包道具自动分类排序。
- **自定义分辨率**：支持现代宽屏显示器分辨率设置。
- **UI 修正**：修复打开功能窗口时视觉中心偏移 1/4 的原版 Bug。

## 🛠 安装说明

1. 下载最新的 [Release](https://github.com/cartman-kai/plugK/releases) 压缩包。
2. 将 `plugK.dll`、`plugKLoader.exe` 和 `plugK.ini` 解压至游戏根目录（与 `Comeon.exe` 同级）。
3. 运行 `plugKLoader.exe` 启动游戏。

## ⚙️ 配置文件 (plugK.ini)
你可以通过修改 `plugK.ini` 来开启或关闭特定功能：

```ini
; PlugK Configuration File
[Inventory]
; 一键整理功能,背包一键整理 (Ctrl+\)  1=启用 0=关闭
EnableSort=1

[Interface]
; 打开背包/技能/属性窗口时，保持画面不左右移动（减少晃动感）
; 1=开启(画面居中) 0=关闭(默认，画面右移)
KeepCenter=1

[Shop]
; 设置为1，商店的回复类与暗器类物品购买后不消失 
InfStock=1

[Resolution]
; 启用分辨率补丁，需要 set.ini 中第一行设置为 ?=6  Enable=1 开启 Enable=0 关闭
Enabled=1
Width=1600
Height=800
```