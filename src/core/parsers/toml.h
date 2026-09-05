/* TOML 解析器：目前按“节 + 键值对”的行模型解析（与 INI 同构）。
 * 数组 / 内联表等复合值暂以原始文本展示，后续可按需扩展。 */
#ifndef LR_CORE_PARSERS_TOML_H
#define LR_CORE_PARSERS_TOML_H

#include <glib.h>
#include "core/value.h"

gboolean lr_parse_toml(const char *content, gsize length,
                       LrConfigFile *file);

#endif /* LR_CORE_PARSERS_TOML_H */
