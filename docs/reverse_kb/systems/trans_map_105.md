# 1.05 传送地图与跨幕限制

## 结论

1.05 原程序允许在传送地图中查看第一幕、第二幕和第三幕，但只允许点击玩家当前幕中的已开放地点。选择其它幕的标签不会修改玩家当前幕。

`Ctrl+Tab` 不是 1.05 原程序提供的跨幕传送快捷键。当前静态分析未发现 `Ctrl+Tab` 专用路径。传送地图点击处理也不读取 Ctrl 或 Tab 状态。因此，cnc-ddraw 或 plugK 的按键冲突不是「可切换标签但不能点击其它幕地点」这一现象的根因。

当前更准确的分类是「原程序的明确限制」或「未完成的跨幕功能」，不是图形 wrapper 引入的回归。仅凭静态代码不能判断设计人员当时的主观意图，因此不把它确定标记为 bug。

来源：IDA 1.05 静态分析、`ActMdl.txt` 与 `JMMDL.txt` 数据表验证。动态点击尚待低频断点验证。

## 数据表

`game_file/105/fol/mb/ActMdl.txt` 使用 GB2312 和 Tab 分隔，包含三条数据记录：

| 数据行 | 物理行 | 幕 | ID | 返回场景 | 坐标 |
| ---: | ---: | --- | ---: | --- | --- |
| 1 | 2 | 第一幕 | 0 | `cunzi1` | `61,47` |
| 2 | 3 | 第二幕 | 1 | `2-0S` | `84,56` |
| 3 | 4 | 第三幕 | 2 | `3-2S*3-1S` | `71,44` |

`game_file/105/fol/mb/JMMDL.txt` 同样使用 GB2312 和 Tab 分隔。与传送地图有关的界面记录如下：

- 根界面 `JM_TransMap` 的界面 ID 是 `103`。
- 三个幕标签 `JM_TransMap_1..3` 的界面 ID 是 `104..106`。
- 关闭按钮 `JM_TransMap_Guan` 的界面 ID 是 `107`。
- 第一幕地点使用 ID `108..118`。
- 第二幕和第三幕地点使用 ID `183..206`。
- 地点记录「特别数据」的第二个值是幕索引 `0/1/2`。

地点数据覆盖三幕，说明界面具备显示三幕地点的资源。数据层没有提供「按住 Ctrl 后允许跨幕」字段。

## 静态调用链

### 幕标签只改变显示状态

`TransMapDialog_on_click / 0x004AEF10` 读取当前点击子控件的界面 ID：

- ID `107`：关闭传送地图。
- ID `104..106`：调用 `TransMapDialog_select_act / 0x004AF0C0`。
- 其它地点按钮：进入地点点击判定。

`TransMapDialog_select_act` 把标签 ID 保存到传送地图对象 `this+0xC4`。函数随后根据地点「特别数据」中的幕索引刷新地点按钮显隐。该函数不调用 `PlayerData_change_act / 0x0047D6A0`，也不修改玩家数据 `+0x374` 的当前幕字段。

### 地点点击明确限制为当前幕

地点点击在 `0x004AEF89..0x004AEF9A` 执行以下比较：

```c
selected_tab_id = trans_map->selected_tab_id;       // this+0xC4
current_tab_id = player_data->current_act + 104;    // player_data+0x374
if (selected_tab_id != current_tab_id)
    return;
```

关键指令为：

```asm
004AEF89  mov eax, [eax+374h]
004AEF8F  mov ecx, [esi+0C4h]
004AEF95  add eax, 68h
004AEF98  cmp ecx, eax
004AEF9A  jnz 004AF070
```

比较失败后直接离开点击处理。游戏不会显示错误，也不会调用 `ChangeAct` 或 `go`。这与「标签能切换，但地点点击没有反应」的现象一致。

比较通过后，函数还会检查打开界面后的短暂点击冷却、地点按钮类型、当前所在地点和场景对象。全部通过时，`0x004AF03D` 执行 `go <scene> -1`。该路径没有先调用 `ChangeAct`。

### `ChangeAct` 是独立操作

脚本命令 `ChangeAct <index>` 最终调用 `PlayerData_change_act / 0x0047D6A0`。该函数写入玩家数据 `this+0x374`，并在幕变化时重载相关状态。

传送地图选择标签与 `ChangeAct` 使用不同字段和不同函数。不能把「查看其它幕」等同于「玩家已经切换到其它幕」。

## `Ctrl+Tab` 路径核对

1.05 键盘状态由 `sub_4CE3D0 / 0x004CE3D0` 每帧刷新。`VK_TAB` 当前状态地址是 `0x005489D1`，`VK_CONTROL` 当前状态地址是 `0x005489D9`。

静态交叉引用结果：

- `0x005489D1` 只有 `sub_4C42B0 / 0x004C42B0` 一个引用。
- `sub_4C42B0` 的 Tab 分支用于切换另一个全局界面对象，不调用传送地图函数、`ChangeAct` 或 `go`。
- 传送地图的虚表更新函数 `TransMapDialog_update / 0x004AF1C0` 只检查 `Esc`，不检查 Tab 或 Ctrl。
- `GetAsyncKeyState` 的调用方中未发现 `VK_TAB` 与 `VK_CONTROL` 组合判定。

plugK 的输入 Hook 会保留 Ctrl、Shift 和 Alt 修饰键。默认 Ctrl 快捷键没有使用 `VK_TAB`。即使用户把某个 plugK 快捷键配置为 Tab，结果也只会屏蔽 Tab，不会生成原程序不存在的跨幕传送路径。

## 动态验证

x32dbg MCP 客户端与当前插件版本不一致，连接返回 `green_pepe != lilac_bonnet`。本次未通过 MCP 修改正在运行的调试会话。

如需用当前游戏状态验证静态结论，优先使用一次点击一次命中的条件断点或日志断点，不要在每帧更新函数上下断：

1. 在 `0x004AEF98` 设置断点。该地址只在传送地图地点点击时经过，不属于主循环每帧路径。
2. 在第三幕打开传送地图，选择第二幕并点击一个地点。
3. 观察 `ECX` 和 `EAX`。预期 `ECX=105`，`EAX=106`。
4. 单步执行 `cmp` 和 `jnz`。预期跳到 `0x004AF070`，且不会到达 `0x004AF03D`。
5. 回到第三幕标签并点击第三幕的其它已开放地点。预期 `ECX=106`、`EAX=106`，比较通过后才可能到达 `0x004AF03D`。

如果断点行为与预期不一致，停止把本专题作为 Hook 依据，并重新核对运行中的 EXE 版本和模块基址。

## 修改边界

不要只把 `0x004AEF9A` 的 `jnz` NOP 掉。这样只能绕过界面检查，后续仍直接执行 `go <scene> -1`，没有切换 `player_data+0x374` 或重载幕相关状态，可能造成对话库、任务标志和场景资源处于错误幕。

若要实现跨幕传送，应在地点点击边界做共享 Hook，并按以下顺序设计：

1. 解析所选幕索引 `selected_tab_id - 104`。
2. 所选幕与当前幕不同时，复用 `PlayerData_change_act` 的完整切幕路径。
3. 在切幕状态重载完成后，再执行目标地点的 `go`。
4. 验证同幕传送、跨幕传送、任务标志、对话库、返回村庄、存档和读档。

切幕重载是否允许在同一个点击调用栈中立即执行 `go` 尚未确认。实现正式 Hook 前，应先动态观察原生 `ChangeAct` 后的场景切换时序；必要时把 `go` 延迟到后续稳定帧执行。

## IDA 标注

本次已在 `ComeOn.exe.i64` 中保存以下确定命名和注释：

- `TransMapDialog_ctor / 0x004AEDC0`
- `TransMapDialog_on_click / 0x004AEF10`
- `TransMapDialog_select_act / 0x004AF0C0`
- `TransMapDialog_update / 0x004AF1C0`
- `TransMapDialog_refresh_locations / 0x004AF3A0`
- `PlayerData_change_act / 0x0047D6A0`
- `g_trans_map_dialog / 0x0055AD60`

运行时子控件 `+0xC0/+0xC4` 的完整结构名尚未确认，因此只在相关指令处记录保守注释，没有创建结构体字段。
