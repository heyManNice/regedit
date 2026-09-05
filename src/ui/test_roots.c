#include "ui/test_roots.h"

#include <glib.h>
#include <stdlib.h>

static const char *
env_or(const char *key, const char *fallback)
{
    const char *v = getenv(key);
    return (v != NULL && *v != '\0') ? v : fallback;
}

const char *
lr_etc_root(void)
{
    return env_or("LR_TEST_ETC", "/etc");
}

const char *
lr_config_root(void)
{
    static char *buf = NULL;
    if (buf == NULL)
        buf = g_build_filename(g_get_home_dir(), ".config", NULL);
    return env_or("LR_TEST_CONFIG", buf);
}

const char *
lr_boot_root(void)
{
    return env_or("LR_TEST_BOOT", "/boot");
}
