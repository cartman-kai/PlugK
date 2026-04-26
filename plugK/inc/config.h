#ifndef CONFIG_H
#define CONFIG_H

#include <windows.h>

#ifdef __cplusplus
extern "C"
{
#endif

// --- 1. 定义全局数值标识符 (供逻辑判断使用) ---
#define TYPE_BOOL 1
#define TYPE_INT 2
#define TYPE_KEY 3

// --- 2. 内部类型映射宏 (仅用于结构体生成) ---
#define PK_TYPE_1 BOOL
#define PK_TYPE_2 int
#define PK_TYPE_3 int
// 拼接宏：将数值 ID 转换为对应的 C 类型
#define PK_GET_TYPE(id) PK_TYPE_##id

    typedef struct
    {
// === X-Macro 展开生成结构体成员 ===
#define X(type, name, sec, key, val, desc) PK_GET_TYPE(type) name;
#include "config_def.h"
#undef X
    } PK_CONFIG;

// 清理内部临时映射宏
#undef PK_TYPE_1
#undef PK_TYPE_2
#undef PK_TYPE_3

    extern PK_CONFIG g_pk_config;
    void pk_config_load(const char *optional_path);
    void pk_config_create_default(const char *path);

#ifdef __cplusplus
}
#endif

#endif