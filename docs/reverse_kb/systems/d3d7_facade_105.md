# 1.05 D3D7 CPU facade

## 目的

1.05 的 `sub_441FC0` 会从 DirectDraw 表面取得 `IDirect3D7`，并调用 `CreateDevice`。在部分现代环境中，cnc-ddraw 的 D3D7 wrapper 返回链不稳定，随后由 `sub_441D40` 在 `0x00441DD2` 显示 `3D Func Error`。

plugK 提供一个默认关闭的 CPU D3D7 facade，在初始化边界直接创建游戏可调用的 D3D7 device COM 兼容对象。DirectDraw 表面仍由 cnc-ddraw 提供，CPU facade 只通过标准 `IDirectDrawSurface7` 方法访问它，不依赖 cnc-ddraw 的私有实现结构。

## 版本边界与 Hook

| 版本 | 初始化 Hook | D3D root 全局 | device 全局 | render target 全局 | 状态 |
| --- | --- | --- | --- | --- | --- |
| 1.05 | `0x00441FC0` (`sub_441FC0`) | `0x0054B6C8` (`dword_54B6C8`) | `0x0054B6CC` (`dword_54B6CC`) | `0x0056D85C` (`dword_56D85C`) | 已接入，配置默认关闭 |
| 2.01 | 未确认 | 未确认 | 未确认 | 未确认 | 仅保留 adapter 入口，禁止启用 |

IDA 确认 1.05 边界为单参数 `__stdcall`：调用者在 `0x00441DC4` 压入一个参数，`sub_441FC0` 在 `0x004420D8` 以 `retn 4` 返回。参数在原函数中未使用，但 Hook 必须接收并清理该参数，否则会令调用者 ESP 错位并破坏 `sub_441D40` 的 SEH 恢复链。安装 Hook 前还会校验入口字节 `A1 58 D8 56 00`，防止错误版本命中相同绝对地址。

启用时先读取 `dword_56D85C`，通过标准 surface 的 `GetSurfaceDesc`、`GetPixelFormat`、`Lock`、`Unlock` 做一次能力探测。探测对象必须经完整的 surface view 绑定流程设置 COM 指针和 valid 状态后才能调用 `Lock`。探测失败返回 `E_FAIL`；不能返回 `0`，因为 `sub_441D40` 使用 `FAILED(result)` 判断失败，零会被误判为成功并继续解引用空 device。

成功后把自有 `IDirect3D7` root 和 `IDirect3DDevice7` facade 分别写入 `dword_54B6C8`、`dword_54B6CC`，返回原版成功值 `1`，使 `sub_441D40` 不再进入 `3D Func Error` 分支。root/device 引用关系与 `sub_4420E0` 的先 root、后 device 释放顺序匹配。Hook 归属为 `d3d7_facade.c`，当前没有其它模块拦截 `0x00441FC0`。

facade 创建后必须复现原版 `0x00442008..0x004420D2` 的 render state 与 texture-stage state 初始化，尤其是 additive `DESTBLEND=ONE`、`ALPHAOP=SELECTARG1` 和线性 MIN/MAG filter；仅使用 facade 自身默认值会造成透明与叠加特效差异。

## 实际实现的 D3D7 子集

- COM：`QueryInterface`、`AddRef`、`Release`。
- 初始化与能力：`GetCaps`、`EnumTextureFormats`、`GetDirect3D`。
- 场景：`SetRenderTarget`、`GetRenderTarget`、`BeginScene`、`EndScene`、`Clear`。
- 状态：`SetRenderState`、`GetRenderState`、`SetTextureStageState`、`GetTextureStageState`、`SetTexture`、`GetTexture`。
- 绘制：TL vertex 的 `DrawIndexedPrimitive`，以及同一 FVF 的 `DrawPrimitive` 三角形列表。
- CPU 后端支持 16/32 位 RGB surface，按 pixel-format mask 解码和写回；纹理在场景内锁定，目标 surface 在首次绘制时锁定，`EndScene` 统一解锁。

第一阶段只承诺游戏已观测到的 TL triangle-list 调用。未覆盖的 primitive/FVF 会返回 `S_OK`、跳过绘制并写入有限次数的诊断日志，避免破坏游戏调用链。

## 配置与失败行为

配置项为 `Hidden/D3D7CpuFacade`，默认值为 `0`。只有显式设置为 `1` 才安装 Hook。

以下任一条件失败时 facade 不接管：版本 adapter 未确认、全局地址不可读、surface COM vtable 不可读、surface 描述/格式不支持、初始化 Lock 探测失败、内存分配失败或 device 创建失败。运行期目标 surface Lock 失败后停止 CPU 绘制并记录原因，不访问私有 surface 布局。

启用功能后会在游戏 EXE 所在目录创建 `plugK-d3d7.log`。文件只记录 Hook 安装、初始化阶段、首次关键 COM 调用，以及有限次数的 surface/primitive 错误；总行数上限为 160。逐帧性能统计仍只写 `OutputDebugString`，不会持续扩大日志文件。

## 性能统计

每个 D3D scene 记录 draw 次数、三角形数、像素数和 CPU 光栅化耗时；每 120 个 scene 输出最近窗口的平均值、P95 和最大值到调试输出。评估目标是 60 FPS 的 16.67 ms 帧预算内，重点在 i5-4xxx 笔记本上进行实际战斗和特效场景验证。

## 2.01 扩展规则

CPU rasterizer、COM facade 和 surface 适配不包含版本地址。2.01 只能新增自己的 adapter（初始化入口、device 全局、render target 全局、调用约定和验证特征），确认动态/静态调用链后才允许把 `supported` 改为启用；不得直接套用 1.05 的绝对地址。
