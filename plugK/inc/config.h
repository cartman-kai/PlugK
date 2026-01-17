#ifndef CONFIG_H
#define CONFIG_H

#include <windows.h>

#ifdef __cplusplus
extern "C"
{
#endif

    // 定义类型枚举，用于后续判断（可选，但为了清晰）
    typedef enum
    {
        PK_CFG_BOOL,
        PK_CFG_INT,
        PK_CFG_KEY
    } PkConfigType;

    typedef struct
    {
// === X-Macro 展开生成结构体成员 ===
// 技巧：将 TYPE_BOOL 映射为 BOOL，TYPE_INT 映射为 int
#define TYPE_BOOL BOOL
#define TYPE_INT int
#define TYPE_KEY int

#define X(type, name, sec, key, val, desc) type name;
#include "config_def.h"
#undef X

#undef TYPE_BOOL
#undef TYPE_INT
#undef TYPE_KEY
        // ==================================

    } PK_CONFIG;

    extern PK_CONFIG g_pk_config;

    void pk_config_load(const char *optional_path);
    void pk_config_create_default(const char *path);

#ifdef __cplusplus
}
#endif

#endif