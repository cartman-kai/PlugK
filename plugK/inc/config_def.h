/* * config_def.h
 * 格式: X(类型, 变量名, Section, Key, 默认值, 中文描述)
 * 类型支持: TYPE_BOOL, TYPE_INT, TYPE_KEY
 */

// --- 界面设置 ---
X(TYPE_BOOL, ui_keep_center, "UI", "KeepCenter", 1, "保持界面居中 (防晃动)")
X(TYPE_BOOL, disable_screen_shake, "UI", "disable_screen_shake", 1, "禁用屏幕震动")
X(TYPE_BOOL, enable_fix_inheritance, "UI", "enable_fix_inheritance", 1, "通关后存档继承优化")
X(TYPE_BOOL, show_enemy_hp, "UI", "ShowEnemyHp", 1, "显示敌人血量")
X(TYPE_BOOL, optimize_drop_item_name_color, "UI", "OptimizeDropItemNameColor", 1, "掉落物品显示颜色优化")
X(TYPE_BOOL, res_enabled, "UI", "Enabled", 1, "启用自定义分辨率")
X(TYPE_INT, res_width, "UI", "Width", 1280, "宽度 (Width)")
X(TYPE_INT, res_height, "UI", "Height", 720, "高度 (Height)")

// --- 背包与储物箱 ---
X(TYPE_BOOL, inventory_sort, "Player", "EnableSort", 1, "启用一键整理")
X(TYPE_BOOL, stash_ext_enabled, "Player", "EnableExt", 1, "启用扩展存储 (大箱子)")
X(TYPE_BOOL, enable_autofill_ext, "Player", "AutoFillExt", 1, "扩展背包自动填充")
X(TYPE_BOOL, enable_drop_bias, "Hidden", "EnableDropBias", 1, "启用掉落倾向优化") // 设置为隐藏，无该配置项目
X(TYPE_BOOL, item_stack_limit_enabled, "Player", "ItemStackLimitEnabled", 1, "启用单格物品数量上限")
X(TYPE_INT, item_stack_limit, "Player", "ItemStackLimit", 99, "单格物品数量上限 (1-127)")
X(TYPE_BOOL, enable_skill_respec, "Player", "EnableSkillRespec", 1, "启用重置技能")
X(TYPE_BOOL, enable_ultimate_hotkey, "Player", "EnableUltimateHotkey", 1, "启用 Alt+1-4 快捷释放必杀技")

// 物品属性与商店
X(TYPE_BOOL, enable_gem_stack, "Item&Shop", "EnableGemStack", 1, "启用宝石叠加")
X(TYPE_BOOL, shop_inf_stock, "Item&Shop", "InfStock", 0, "商店无限库存 (购买不消失)")
X(TYPE_BOOL, shop_item_count, "Item&Shop", "OptimizeItem", 1, "商店物品堆叠/随机数量")
X(TYPE_BOOL, shop_sort, "Item&Shop", "EnableSort", 1, "商店物品自动排序")
X(TYPE_BOOL, show_item_name, "Hidden", "EnableShowItemName", 0, "长期显示地面上物品的名称") // 设置为隐藏，无该配置项目
X(TYPE_BOOL, hold_show_item_name, "Item&Shop", "EnableHoldShowItemName", 1, "启用长按快捷键显示地面物品名称")

// --- 合成与装备 ---
X(TYPE_BOOL, enable_insert_gem, "Equipment", "EnableGemInsert", 1, "自己镶嵌宝石")
X(TYPE_BOOL, enable_fuse_opt, "Equipment", "EnableFuseOpt", 1, "优化叠加物品合成")

// --- 快捷键配置 (注意：默认值使用 Windows VK 宏) ---
X(TYPE_KEY, key_stash_swap, "Hotkeys", "StashSwap", VK_OEM_COMMA, "储物箱切换 (A/B)")
X(TYPE_KEY, key_stash_sort, "Hotkeys", "StashSort", VK_OEM_4, "储物箱整理")
X(TYPE_KEY, key_inv_swap, "Hotkeys", "InvPrev", VK_OEM_PERIOD, "背包切换(A/B)")
X(TYPE_KEY, key_inv_sort, "Hotkeys", "InvSort", VK_OEM_5, "背包整理 (全部)")
X(TYPE_KEY, key_inv_sort_current, "Hotkeys", "InvSortCurrent", VK_OEM_2, "背包整理 (当前页)")
X(TYPE_KEY, key_switch_gem_stack, "Hotkeys", "switch_gem_stack", VK_OEM_7, "切换宝石叠加开关")
X(TYPE_KEY, key_switch_show_item_name, "Hotkeys", "key_switch_show_item_name", VK_OEM_6, "显示物品名称")
X(TYPE_KEY, key_hold_show_item_name, "Hotkeys", "HoldShowItemName", VK_OEM_3, "长按显示物品名称")
X(TYPE_KEY, key_skill_respec, "Hotkeys", "SkillRespec", VK_BACK, "重置技能")
