# 1.05 主循环与帧率限制调研（fps_limit）

> 2026-08-02 于 IDA（1.05 `ComeOn.exe`）静态分析确认。本文记录 1.05 的地址和调用边界；
> plugK 已按本文结论接入 1.05 Hook。

## 结论摘要

- **主消息循环是 `PeekMessageA` 自旋**：`sub_401090` 处理完待处理消息后调用
  `sub_4043E0`，正常游戏帧路径没有 `Sleep` 或其它阻塞等待。
- **每帧函数是 `sub_404700`（`0x00404700`）**：`sub_4043E0` 的状态 case `1/2/3/5`
  唯一调用它；它的唯一直接调用者是 `sub_4043E0`。
- **模拟仍按固定 35ms 追赶**：`dword_544D9C` 的值为 `0x23`，`sub_404700` 用
  `dword_5487DC` 作为模拟时间轴，在落后至少一个步长时循环更新 `this[137]`。
- **渲染和模拟位于同一个每帧函数**：模拟追赶后，满足 `dword_544DA8`、
  `dword_547F50` 和 `sub_408A50(this)` 时进入渲染分支；`sub_407320` 在渲染分支内维护
  调试 HUD 的 FPS 计数。
- **可直接复用 2.01 的入口节流方案**：在 `sub_404700` 入口按目标帧间隔等待，等待时间
  会被 `timeGetTime` 计入 `v3`，原有 35ms 模拟追赶逻辑会自动消化这段等待，渲染帧率被限制而
  模拟步长不变。

## 调用链

```text
sub_401090   主消息循环：while (PeekMessageA) { GetMessageA/Translate/Dispatch }
  └─ sub_4043E0   状态机：case 1/2/3/5 → sub_404700；case 6 片头/过场 Sleep(50ms)
       └─ sub_404700   每帧函数（游戏帧）
            ├─ timeGetTime + 35ms 模拟追赶
            └─ 渲染分支 → sub_407320（帧计数 + 调试 HUD/FPS）
```

`sub_401090` 的反编译逻辑是：

```c
do {
    while (PeekMessageA(&msg, 0, 0, 0, 0)) {
        if (!GetMessageA(&msg, 0, 0, 0))
            return msg.wParam;
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
} while (sub_4043E0(&hdc));
```

`sub_4043E0` 的状态分支中，case `1/2/3/5` 在 `0x0040448B` 调用 `sub_404700`，随后
固定返回 `1`；因此 `sub_404700` 内部的卡顿重同步返回值不会终止主循环。

## `sub_404700` 的时间与模拟逻辑

入口首先以 `timeGetTime()` 得到当前相对时间：

```c
now = timeGetTime() - dword_55C3C8;
delta = now - dword_5487DC;
```

`sub_407060(this)` 先递增 `dword_548DA0`，再根据 `abs(this[153] - dword_548DA0)` 返回：

- 差值不超过 20：`200ms`；
- 否则：`1500ms`。

当 `abs(delta)` 未超过该阈值且 `delta >= dword_544D9C` 时执行模拟追赶循环。每次循环：

1. `++this[137]`；
2. 在游戏运行状态执行输入、逻辑和状态更新；
3. `dword_5487DC += dword_544D9C`；
4. 若 `dword_544DA8 == 0` 则置为 `1`；
5. 只要没有 `this[169]` 且仍落后一个 35ms 步长，就继续循环。

当 `abs(delta)` 超过卡顿阈值时，函数只执行：

```c
dword_5487DC = now;
dword_544DA8 = 0;
return 0;
```

这与 2.01 的重同步边界一致：它跳过本轮渲染，但状态机仍返回继续运行。

## 渲染与 FPS 统计

模拟部分之后，`sub_404700` 只有在以下条件同时满足时进入渲染路径：

```c
if (dword_544DA8 && dword_547F50 && sub_408A50(this)) {
    // state 3 的额外场景准备
    if (!sub_406F80(this)) {
        sub_408510(this);
        sub_4421D0(dword_548234);
        sub_4EDA30(dword_548234);
        sub_407320(this);
        sub_403260(...);
    }
}
```

`sub_407320`（`0x00407320`）每次被调用时递增 `dword_548DA4`，重新读取
`timeGetTime()`，并在经过约 1000ms 时执行：

```c
dword_548DAC = elapsed;
dword_548DB0 = dword_548DA4;
dword_548DA4 = 0;
```

调试 HUD 格式串为：

```text
"%d  %i/%i//player:%i/%i,%i/%i//fps:%i,bld:%i,hour: %i,DeadCount: %d"
```

所以 `dword_548DB0` 是可用于手工确认限速结果的“上一采样周期渲染帧数”，不是模拟步数。

## 关键地址（1.05）

| 地址 | 角色 |
| --- | --- |
| `0x00401090` | 主消息循环，`PeekMessageA` 自旋 |
| `0x004043E0` | 状态机；case `1/2/3/5` 调用每帧函数 |
| `0x00404700` | 每帧函数；1.05 帧率限制 Hook 点 |
| `0x00407060` | 卡顿阈值计算，返回 `200/1500ms` |
| `0x00407320` | 渲染帧计数与调试 HUD/FPS |
| `0x00544D9C` | 固定模拟步长，值 `0x23`（35ms） |
| `0x00544DA8` | 模拟完成后的渲染门控 |
| `0x005487DC` | 模拟时间轴 |
| `0x00548DA4` | 渲染帧计数器 |
| `0x00548DAC` | FPS 上次采样时刻 |
| `0x00548DB0` | FPS 采样值，HUD 的 `fps:%i` 来源 |

## 1.05 的实现建议

当前 `plugK/src/fps_limit.c` 的 Detour 签名已经适合这个目标：原函数是 `__thiscall`，
继续使用 `__fastcall` 将 `this` 映射到 ECX、占位参数映射到 EDX 即可。移植只需：

1. 增加 `ADDR_FRAME_FN_105 0x00404700`；
2. 在 `Mod_FpsLimit_Init` 的版本选择中，将 1.05 映射到该地址；
3. 保留现有 QPC、Sleep 粗等和 spin 精等逻辑，不改游戏内部 `dword_544D9C`；
4. 构建并分别验证 `Release|x86` 的 1.05 与 2.01 加载。

Hook `sub_4043E0` 也能覆盖状态机，但它还负责显示模式切换、状态转换和片头分支，
边界更宽；因此不建议作为限速入口。不要 Hook `sub_407320`，那只会改变 HUD/计数而不会
阻止渲染提交。

## 已确认与未确认

- 已确认：地址、调用者/被调用者关系、35ms 常量、模拟追赶结构、渲染门控、HUD FPS 采样。
- 已确认：`sub_404700` 的唯一直接调用点为 `0x0040448B`；没有发现其它路径需要单独安装同一
  Hook。
- 尚未动态验证：1.05 实机在 60/30 FPS 下的 HUD 数值、后台/失焦行为，以及等待期间是否
  存在特定输入时序依赖。这些应在实际启用 1.05 Hook 后手工验证。
