/* * config_def.h
 * 格式: X(类型, 变量名, Section, Key, 默认值, 中文描述)
 * 类型支持: TYPE_BOOL, TYPE_INT, TYPE_KEY
 */

// --- 界面设置 ---
X(TYPE_BOOL, ui_keep_center, "Interface", "KeepCenter", 1, "保持界面居中 (防晃动)")
X(TYPE_BOOL, disable_screen_shake, "Interface", "disable_screen_shake", 1, "禁用屏幕震动")

// --- 分辨率 ---
X(TYPE_BOOL, res_enabled, "Resolution", "Enabled", 1, "启用自定义分辨率")
X(TYPE_INT, res_width, "Resolution", "Width", 1280, "宽度 (Width)")
X(TYPE_INT, res_height, "Resolution", "Height", 720, "高度 (Height)")

// --- 物品与背包 ---
X(TYPE_BOOL, inventory_sort, "Inventory", "EnableSort", 1, "启用背包一键整理")
X(TYPE_BOOL, stash_ext_enabled, "Stash", "EnableExt", 1, "启用扩展储物箱 (大箱子)")

// 物品属性
X(TYPE_BOOL, enable_gem_stack, "Item", "EnableGemStack", 1, "启用宝石叠加")

// --- 商店功能 ---
X(TYPE_BOOL, shop_inf_stock, "Shop", "InfStock", 0, "无限库存 (购买不消失)")
X(TYPE_BOOL, shop_item_count, "Shop", "OptimizeItem", 1, "商店物品堆叠/随机数量")
X(TYPE_BOOL, shop_sort, "Shop", "EnableSort", 1, "商店物品自动排序")

// --- 实验性功能 ---
X(TYPE_BOOL, enable_autofill_ext, "Experimental", "AutoFillExt", 1, "扩展背包自动填充")
X(TYPE_BOOL, enable_insert_gem, "Experimental", "EnableGemInsert", 1, "修改宝石镶嵌条件")
X(TYPE_BOOL, enable_fuse_opt, "Experimental", "EnableFuseOpt", 1, "炼化仅扣除数量")

// --- 快捷键配置 (注意：默认值使用 Windows VK 宏) ---
X(TYPE_KEY, key_stash_swap, "Hotkeys", "StashSwap", VK_OEM_COMMA, "储物箱切换 (A/B)")
X(TYPE_KEY, key_stash_sort, "Hotkeys", "StashSort", VK_OEM_4, "储物箱整理")
X(TYPE_KEY, key_inv_swap, "Hotkeys", "InvPrev", VK_OEM_PERIOD, "背包切换(A/B)")
X(TYPE_KEY, key_inv_sort, "Hotkeys", "InvSort", VK_OEM_5, "背包整理 (全部)")
X(TYPE_KEY, key_inv_sort_current, "Hotkeys", "InvSortCurrent", VK_OEM_2, "背包整理 (当前页)")
X(TYPE_KEY, key_switch_gem_stack, "Hotkeys", "switch_gem_stack", VK_OEM_7, "切换宝石叠加开关")