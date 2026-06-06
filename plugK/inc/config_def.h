/* * config_def.h
 * 格式: X(类型, 变量名, Section, Key, 默认值, 中文描述)
 * 类型支持: TYPE_BOOL, TYPE_INT, TYPE_KEY
 */

// --- 界面设置 ---
X(TYPE_BOOL, ui_keep_center, "UI", "KeepCenter", 1, "保持界面居中 (防晃动)")
X(TYPE_BOOL, disable_screen_shake, "UI", "disable_screen_shake", 1, "禁用屏幕震动")
X(TYPE_BOOL, enable_fix_inheritance, "UI", "enable_fix_inheritance", 1, "通关后存档继承优化")
X(TYPE_BOOL, res_enabled, "UI", "Enabled", 1, "启用自定义分辨率")
X(TYPE_INT, res_width, "UI", "Width", 1280, "宽度 (Width)")
X(TYPE_INT, res_height, "UI", "Height", 720, "高度 (Height)")

// --- 背包与储物箱 ---
X(TYPE_BOOL, inventory_sort, "Inventory", "EnableSort", 1, "启用一键整理")
X(TYPE_BOOL, stash_ext_enabled, "Inventory", "EnableExt", 1, "启用扩展存储 (大箱子)")
X(TYPE_BOOL, enable_autofill_ext, "Inventory", "AutoFillExt", 1, "扩展背包自动填充")
X(TYPE_BOOL, item_stack_limit_enabled, "Inventory", "ItemStackLimitEnabled", 1, "启用单格物品数量上限")
X(TYPE_INT, item_stack_limit, "Inventory", "ItemStackLimit", 99, "单格物品数量上限 (1-127)")

// 物品属性与商店
X(TYPE_BOOL, enable_gem_stack, "Item&Shop", "EnableGemStack", 1, "启用宝石叠加")
X(TYPE_BOOL, shop_inf_stock, "Item&Shop", "InfStock", 0, "商店无限库存 (购买不消失)")
X(TYPE_BOOL, shop_item_count, "Item&Shop", "OptimizeItem", 1, "商店物品堆叠/随机数量")
X(TYPE_BOOL, shop_sort, "Item&Shop", "EnableSort", 1, "商店物品自动排序")
X(TYPE_BOOL, show_item_name, "Item&Shop", "EnableShowItemName", 1, "显示地面上物品的名称")

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
