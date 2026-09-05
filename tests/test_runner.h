/* 极简测试断言框架 */
#ifndef LR_TEST_RUNNER_H
#define LR_TEST_RUNNER_H

#include <glib.h>

extern gint g_test_count;
extern gint g_test_failures;

#define TEST_ASSERT(cond)                                           \
    do                                                              \
    {                                                               \
        g_test_count++;                                             \
        if (!(cond))                                                \
        {                                                           \
            g_test_failures++;                                      \
            g_print("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                           \
    } while (0)

#define TEST_ASSERT_STR_EQ(a, b)                                             \
    do                                                                       \
    {                                                                        \
        const char *_a = (a);                                                \
        const char *_b = (b);                                                \
        g_test_count++;                                                      \
        if (g_strcmp0(_a, _b) != 0)                                          \
        {                                                                    \
            g_test_failures++;                                               \
            g_print("FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__,    \
                    _a != NULL ? _a : "(null)", _b != NULL ? _b : "(null)"); \
        }                                                                    \
    } while (0)

void test_value_types(void);
void test_parsers(void);
void test_scanner(void);
void test_writeback(void);
void test_write(void);

#endif /* LR_TEST_RUNNER_H */
