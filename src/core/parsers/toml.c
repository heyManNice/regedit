#include "core/parsers/toml.h"
#include "core/parsers/common.h"

/* TOML 的“表 + 键值对 + # 注释”行模型与 INI 一致：
 *   [table.sub]  -> 节
 *   key = value  -> 配置项（值去引号；行内 # 注释自动剥离）
 * 因此复用共享解析实现；布尔 / 数字由统一类型识别完成。 */
gboolean
lr_parse_toml(const char *content, gsize length, LrConfigFile *file)
{
    return lr_parse_section_kv(content, length, file);
}
