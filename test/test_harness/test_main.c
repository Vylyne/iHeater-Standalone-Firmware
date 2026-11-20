#include <unity.h>
#include "config.h"

void setUp(void) {}
void tearDown(void) {}

static void test_input_mode_uart(void) { TEST_ASSERT_EQUAL_INT(2, INPUT_MODE_UART); }
static void test_link_max_target(void) { TEST_ASSERT_EQUAL_INT(MAX_AIR_TEMP - 5, LINK_MAX_TARGET); }
static void test_fault_table_rows(void)
{
    static const struct { int code; int remote_clear; } t[] = {
#define X(code, remote_clear) {code, remote_clear},
        FAULT_TABLE(X)
#undef X
    };
    static const struct { int code; int remote_clear; } e[] = {
        {0x01, 1}, {0x02, 0}, {0x03, 1}, {0x04, 1}, {0x05, 1},
        {0x06, 0}, {0x07, 1}, {0x08, 1}, {0xFF, 0},
    };
    TEST_ASSERT_EQUAL_INT(9, (int)(sizeof t / sizeof t[0]));
    for (unsigned i = 0; i < sizeof e / sizeof e[0]; i++) {
        TEST_ASSERT_EQUAL_INT(e[i].code, t[i].code);
        TEST_ASSERT_EQUAL_INT(e[i].remote_clear, t[i].remote_clear);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_input_mode_uart);
    RUN_TEST(test_link_max_target);
    RUN_TEST(test_fault_table_rows);
    return UNITY_END();
}
