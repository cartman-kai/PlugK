# 刀剑封魔录逆向知识库

本目录用于沉淀刀剑封魔录的逆向、数据表、运行时内存结构与补丁开发知识。IDA Pro 中的分析对象会在 `1.05` 与 `2.01` 之间切换；使用函数地址前必须先核对对应版本的地址表，不能跨版本直接复用虚拟地址。

## 阅读入口

- [版本差异](versions.md)：1.05 与 2.01 的整体差异、地址迁移注意事项。
- [数据表与资源](data_tables.md)：游戏模板数据、编码、字段分隔与已确认表。
- [物品系统](systems/items.md)：物品类型、物品池、容器槽位与基础概念。
- [技能与招式系统](systems/skills.md)：技能 ID、招式 ID、必杀技准备/释放链路与运行时字段。
- [文字渲染与内置颜色](systems/text_rendering.md)：通用文字输出、默认颜色字段、控制码与 2.01 字体生命周期。
- [输入与快捷键系统](systems/input.md)：键盘状态刷新、数字键快捷栏/药品槽位与输入消费路径。
- [开场动画](systems/intro_movie.md)：DHP 开场视频、DirectShow 播放链路与跳过 Hook 边界。
- [1.05 DirectDraw / Direct3D7 窗口化调研](systems/ddraw_windowed_105.md)：原版显示链、实验结果与放弃轻量 Hook 方案的原因。
- [1.05 帧率限制调研](systems/fps_limit_105.md)：1.05 主循环、每帧函数、模拟步长与可移植的限速 Hook 点。
- [难度系统](systems/difficulty.md)：难度全局变量、1.05 静态交叉引用与自定义难度设计边界。
- [受击、命中与连续受击保护](systems/combat_damage.md)：角色受击结算、隐藏伤害除数、攻击来源集合与动态验证边界。
- [隐藏减伤常驻显示设计](systems/combat_mitigation_display.md)：常驻 HUD 的比例定义、显示语义、版本范围和共享文字 Hook 边界。
- [1.05 战斗调研工具](systems/combat_probe.md)：受控角色属性修改、有限受击日志、Hook 归属与安全边界。
- [1.05 战斗调研测试案例](testing/combat_probe_105.md)：少量受击即可完成的属性、单来源和双来源验证流程。
- [Steam 1.05 启动链](systems/steam_105_launch_chain.md)：Steam launcher、`ComeOn.dll` Steam 初始化与 launcher hook 边界。
- [Steam 2.01 启动链](systems/steam_201_launch_chain.md)：2.01 外传 Steam launcher、appid 与 launcher hook 复用边界。
- [掉落机制](systems/drop.md)：随机掉落、连招二次掉落、函数链路与 hook 边界。
- [1.05 调试模式](systems/debug_mode_105.md)：隐藏调试口令、调试命令解析与已知快捷键。
- [角色运行时内存](runtime/player.md)：角色数据对象大小、模板/基础属性、战斗对象候选字段、容器与物品字段。
- [IDA 1.05 地址表](ida/1.05_addresses.md)：1.05 版本中已确认函数、全局变量和关键返回地址。
- [IDA 2.01 地址表](ida/2.01_addresses.md)：2.01 版本中已确认函数、全局变量和对应关系。
- [待确认问题](research_queue.md)：尚未完全翻译或需要动态调试确认的点。

## 记录规范

新增知识时优先按“专题”归档，而不是按日期追加：

1. 游戏规则、系统逻辑写入 `systems/`。
2. 对象、全局变量、运行时结构写入 `runtime/`。
3. 版本特定虚拟地址、函数名、返回地址写入 `ida/`。
4. 模板表、资源格式、编码与解包路径写入 `data_tables.md`。
5. 尚未确认的推测写入 `research_queue.md`，确认后再迁移到正式专题。

建议为每条逆向结论标注来源：

- `静态分析`：来自 IDA 反汇编或伪代码。
- `动态调试`：来自断点、内存观察或运行时验证。
- `数据表验证`：来自 `game_file/` 下的资源表统计。
- `推测`：尚未充分验证，不应直接作为 hook 依据。

## Hook 记录原则

同一套游戏逻辑在 1.05 与 2.01 中通常相似，但虚拟地址不同。编写 hook 说明时应同时记录：

- 版本号。
- 目标函数地址或 IDA 临时函数名。
- 调用方或返回地址边界。
- 不应影响的其他路径。
- 已做过的手工验证。

对于可能影响商店、脚本、敌人已有物品吐出等共享逻辑的函数，应优先记录“只处理哪些调用来源”，避免把全局函数 hook 成过宽的行为修改。
