#include <unity.h>
#include "config.h"

void setUp(void) {}
void tearDown(void) {}

static void test_input_mode_uart(void) { TEST_ASSERT_EQUAL_INT(2, INPUT_MODE_UART); }
static void test_link_max_target(void) { TEST_ASSERT_EQUAL_INT(MAX_AIR_TEMP - 5, LINK_MAX_TARGET); }
static void test_fault_table_rows(void)
{
    int n = 0;
#define X(code, remote_clear) n++;
    FAULT_TABLE(X)
#undef X
    TEST_ASSERT_EQUAL_INT(9, n);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_input_mode_uart);
    RUN_TEST(test_link_max_target);
    RUN_TEST(test_fault_table_rows);
    return UNITY_END();
}
