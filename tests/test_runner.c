#include "test_runner.h"

gint g_test_count = 0;
gint g_test_failures = 0;

int main(void)
{
    test_value_types();
    test_parsers();
    test_scanner();
    test_writeback();
    test_writeback_fuzz();
    test_write();

    g_print("\n共 %d 项断言，%d 项失败\n", g_test_count, g_test_failures);
    return g_test_failures == 0 ? 0 : 1;
}
