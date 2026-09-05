#include "core/value.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

/* 判断是否为布尔值字面量：true/false、yes/no、on/off、1/0（大小写不敏感） */
static gboolean
is_bool_literal(const char *s)
{
    if (s == NULL || *s == '\0')
        return FALSE;

    if (g_ascii_strcasecmp(s, "true") == 0 ||
        g_ascii_strcasecmp(s, "false") == 0 ||
        g_ascii_strcasecmp(s, "yes") == 0 ||
        g_ascii_strcasecmp(s, "no") == 0 ||
        g_ascii_strcasecmp(s, "on") == 0 ||
        g_ascii_strcasecmp(s, "off") == 0 ||
        g_ascii_strcasecmp(s, "1") == 0 ||
        g_ascii_strcasecmp(s, "0") == 0)
        return TRUE;
    return FALSE;
}

/* 判断是否为数字字面量：十进制整数/浮点、负数、0x 十六进制、科学计数法 */
static gboolean
is_number_literal(const char *s)
{
    char *end = NULL;

    if (s == NULL || *s == '\0')
        return FALSE;

    /* 十六进制 */
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
    {
        if (s[2] == '\0')
            return FALSE;
        errno = 0;
        g_ascii_strtoull(s + 2, &end, 16);
        return errno == 0 && end != NULL && *end == '\0';
    }

    /* 十进制整数 / 浮点 / 科学计数法 */
    errno = 0;
    g_ascii_strtod(s, &end);
    if (errno != 0)
        return FALSE;
    return end != NULL && *end == '\0';
}

LrValueType
lr_value_detect_type(const char *value)
{
    char *trimmed;
    LrValueType result = LR_VALUE_STRING;

    if (value == NULL)
        return LR_VALUE_STRING;

    trimmed = g_strstrip(g_strdup(value));

    if (is_bool_literal(trimmed))
        result = LR_VALUE_BOOL;
    else if (is_number_literal(trimmed))
        result = LR_VALUE_NUMBER;

    g_free(trimmed);
    return result;
}

const char *
lr_value_type_name(LrValueType type)
{
    switch (type)
    {
    case LR_VALUE_NUMBER:
        return "Number";
    case LR_VALUE_BOOL:
        return "Boolean";
    case LR_VALUE_SECTION:
        return "Section";
    case LR_VALUE_STRING:
    default:
        return "String";
    }
}

LrValueType
lr_value_type_from_name(const char *name)
{
    if (name == NULL)
        return LR_VALUE_STRING;
    if (strcmp(name, "Number") == 0)
        return LR_VALUE_NUMBER;
    if (strcmp(name, "Boolean") == 0)
        return LR_VALUE_BOOL;
    if (strcmp(name, "Section") == 0)
        return LR_VALUE_SECTION;
    return LR_VALUE_STRING;
}

const char *const *
lr_value_type_names(void)
{
    static const char *const names[] = {
        "Section", "String", "Boolean", "Number", NULL,
    };
    return names;
}

LrConfigItem *
lr_config_item_new(const char *key, const char *data,
                   LrValueType type, const char *section,
                   const char *comment)
{
    LrConfigItem *item = g_new0(LrConfigItem, 1);
    item->key = g_strdup(key != NULL ? key : "");
    item->data = g_strdup(data != NULL ? data : "");
    item->type = type;
    item->section = g_strdup(section);
    item->comment = g_strdup(comment);
    item->enabled = TRUE;
    return item;
}

void lr_config_item_free(LrConfigItem *item)
{
    if (item == NULL)
        return;
    g_free(item->key);
    g_free(item->data);
    g_free(item->section);
    g_free(item->comment);
    g_free(item);
}

LrConfigFile *
lr_config_file_new(const char *path)
{
    LrConfigFile *file = g_new0(LrConfigFile, 1);
    file->path = g_strdup(path != NULL ? path : "");
    file->items = g_ptr_array_new_with_free_func(
        (GDestroyNotify)lr_config_item_free);
    return file;
}

void lr_config_file_free(LrConfigFile *file)
{
    if (file == NULL)
        return;
    g_free(file->path);
    g_free(file->error);
    g_ptr_array_unref(file->items);
    g_free(file);
}
