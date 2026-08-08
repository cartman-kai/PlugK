# 1.05 DirectDraw / Direct3D7 窗口化调研（已放弃）

## 原版显示链

`sub_404D30` 通过图形包装对象虚表 `+0x5C` 调用 `sub_4FA3F0`。原版传入的协作级别标志为 `0x11` 或 `0x13`，核心是 `DDSCL_FULLSCREEN | DDSCL_EXCLUSIVE`；随后调用 `IDirectDraw7::SetDisplayMode`。

`sub_4F9910` 创建 caps 为 `0x2200`（`DDSCAPS_PRIMARYSURFACE | DDSCAPS_3DDEVICE`）的可见主表面，保存到 `dword_56D85C`。`sub_441FC0` 取得 `IDirect3D7`，并以该表面调用 `IDirect3D7::CreateDevice`。失败时 `sub_441D40` 在 `0x00441DD2` 显示 `3D Func Error`。

因此，只把协作级别改为 `DDSCL_NORMAL`、并伪造 `SetDisplayMode` 成功是不完整的：窗口模式不能继续把原版全屏主表面模型直接交给 Direct3D7。

## 实验结果

2026-08-01 在 1.05 实机进行了三轮轻量 Hook 验证：

1. 仅改协作级别并跳过显示模式切换：触发 `3D Func Error`。
2. 把原版表面拆为窗口主表面和 3D 离屏合成目标，并在 `EndScene` / 2D `Blt` 后整面提交：游戏可启动且有画面，但出现明显的 16 位内容按桌面 32 位格式解释的异常。
3. 将 3D 离屏目标显式固定为 RGB565（mask `0xF800 / 0x07E0 / 0x001F`），再由 DirectDraw `Blt` 转换到桌面 32 位主表面：显示异常依旧存在。

## 结论

原版同时依赖 16 位显示模式、DirectDraw 主表面和 Direct3D7 渲染目标。在现代 32 位桌面上，仅靠修改协作级别、替换表面和最终 `Blt` 无法可靠地恢复显示。

plugK 已移除该窗口化配置与 Hook，不再继续轻量原生 DirectDraw Hook 路线。如需窗口化，应使用成熟的 DirectDraw wrapper，或实现包含像素格式转换、表面管理和呈现的完整兼容层。
