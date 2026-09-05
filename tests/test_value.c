#include "test_runner.h"
#include "core/value.h"

void test_value_types(void)
{
    /* 数字 */
    TEST_ASSERT(lr_value_detect_type("123") == LR_VALUE_NUMBER);
    TEST_ASSERT(lr_value_detect_type("-42") == LR_VALUE_NUMBER);
    TEST_ASSERT(lr_value_detect_type("+7") == LR_VALUE_NUMBER);
    TEST_ASSERT(lr_value_detect_type("3.14") == LR_VALUE_NUMBER);
    TEST_ASSERT(lr_value_detect_type("0.5") == LR_VALUE_NUMBER);
    TEST_ASSERT(lr_value_detect_type("0x1F") == LR_VALUE_NUMBER);
    TEST_ASSERT(lr_value_detect_type("1e3") == LR_VALUE_NUMBER);

    /* 布尔值 */
    TEST_ASSERT(lr_value_detect_type("true") == LR_VALUE_BOOL);
    TEST_ASSERT(lr_value_detect_type("FALSE") == LR_VALUE_BOOL);
    TEST_ASSERT(lr_value_detect_type("Yes") == LR_VALUE_BOOL);
    TEST_ASSERT(lr_value_detect_type("no") == LR_VALUE_BOOL);
    TEST_ASSERT(lr_value_detect_type("on") == LR_VALUE_BOOL);
    TEST_ASSERT(lr_value_detect_type("off") == LR_VALUE_BOOL);
    TEST_ASSERT(lr_value_detect_type("1") == LR_VALUE_BOOL);
    TEST_ASSERT(lr_value_detect_type("0") == LR_VALUE_BOOL);

    /* 字符串 */
    TEST_ASSERT(lr_value_detect_type("hello") == LR_VALUE_STRING);
    TEST_ASSERT(lr_value_detect_type("") == LR_VALUE_STRING);
    TEST_ASSERT(lr_value_detect_type("abc123") == LR_VALUE_STRING);
    TEST_ASSERT(lr_value_detect_type("on-failure") == LR_VALUE_STRING);
    TEST_ASSERT(lr_value_detect_type("192.168.1.1") == LR_VALUE_STRING);

    /* 类型显示名 */
    TEST_ASSERT_STR_EQ(lr_value_type_name(LR_VALUE_NUMBER), "Number");
    TEST_ASSERT_STR_EQ(lr_value_type_name(LR_VALUE_BOOL), "Boolean");
    TEST_ASSERT_STR_EQ(lr_value_type_name(LR_VALUE_STRING), "String");
    TEST_ASSERT_STR_EQ(lr_value_type_name(LR_VALUE_SECTION), "Section");

    /* 规范名反查：未知一律回退 String */
    TEST_ASSERT(lr_value_type_from_name("Number") == LR_VALUE_NUMBER);
    TEST_ASSERT(lr_value_type_from_name("Boolean") == LR_VALUE_BOOL);
    TEST_ASSERT(lr_value_type_from_name("String") == LR_VALUE_STRING);
    TEST_ASSERT(lr_value_type_from_name("Section") == LR_VALUE_SECTION);
    TEST_ASSERT(lr_value_type_from_name(NULL) == LR_VALUE_STRING);
    TEST_ASSERT(lr_value_type_from_name("number") == LR_VALUE_STRING);

    /* 表格“类型”列的固定取值序列 */
    {
        const char *const *names = lr_value_type_names();
        const gchar *expected[] = {"Section", "String", "Boolean", "Number",
                                   NULL};
        gint i;

        for (i = 0; expected[i] != NULL; i++)
        {
            TEST_ASSERT(names != NULL);
            TEST_ASSERT_STR_EQ(names[i], expected[i]);
        }
        TEST_ASSERT(names[i] == NULL);
    }
}
