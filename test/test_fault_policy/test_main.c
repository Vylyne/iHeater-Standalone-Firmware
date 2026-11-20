#include <unity.h>
#include <math.h>
#include "config.h"
#include "fault_policy.h"

void setUp(void) {}
void tearDown(void) {}

#define AIR_EDGE ((float)MAX_AIR_TEMP - LINK_CLEAR_MARGIN)
#define HEAT_EDGE ((float)MAX_TEMP - LINK_CLEAR_MARGIN)

static void test_allow_mask(void) { TEST_ASSERT_EQUAL_HEX16(0x01BA, fault_clear_allow_mask()); }

static void test_clearable_lookup(void)
{
    TEST_ASSERT_FALSE(fault_remote_clearable(0x02));
    TEST_ASSERT_FALSE(fault_remote_clearable(0x06));
    TEST_ASSERT_FALSE(fault_remote_clearable(0xFF));
    TEST_ASSERT_FALSE(fault_remote_clearable(0x00));
    TEST_ASSERT_TRUE(fault_remote_clearable(0x07));
}

static void test_safe_boundaries(void)
{
    TEST_ASSERT_TRUE(fault_readings_safe(AIR_EDGE, 40.0f));
    TEST_ASSERT_FALSE(fault_readings_safe(AIR_EDGE + 0.1f, 40.0f));
    TEST_ASSERT_TRUE(fault_readings_safe(25.0f, HEAT_EDGE));
    TEST_ASSERT_FALSE(fault_readings_safe(25.0f, HEAT_EDGE + 0.1f));
    TEST_ASSERT_FALSE(fault_readings_safe((float)MIN_AIR_TEMP, 40.0f));
    TEST_ASSERT_FALSE(fault_readings_safe(25.0f, (float)MIN_TEMP));
}

static void test_garbage_readings_unsafe(void)
{
    const float bad[] = {NAN, INFINITY, -INFINITY};
    for (unsigned i = 0; i < 3; i++) {
        TEST_ASSERT_FALSE(fault_readings_safe(bad[i], 40.0f));
        TEST_ASSERT_FALSE(fault_readings_safe(25.0f, bad[i]));
    }
}

static void test_tracker_needs_5s(void)
{
    SafeTracker t;
    safe_tracker_init(&t);
    TEST_ASSERT_FALSE(safe_tracker_ok(&t, 1000));
    safe_tracker_update(&t, 25.0f, 40.0f, 1000);
    TEST_ASSERT_FALSE(safe_tracker_ok(&t, 5999));
    TEST_ASSERT_TRUE(safe_tracker_ok(&t, 6000));
    safe_tracker_init(&t);
    safe_tracker_update(&t, 25.0f, 40.0f, 1000);
    safe_tracker_update(&t, (float)MAX_AIR_TEMP, 40.0f, 2999);
    TEST_ASSERT_FALSE(safe_tracker_ok(&t, 7000));
    safe_tracker_update(&t, 25.0f, 40.0f, 3000);
    TEST_ASSERT_FALSE(safe_tracker_ok(&t, 7999));
    TEST_ASSERT_TRUE(safe_tracker_ok(&t, 8000));
}

static void test_tracker_wraps(void)
{
    SafeTracker t;
    safe_tracker_init(&t);
    safe_tracker_update(&t, 25.0f, 40.0f, 0xFFFFF000U);
    TEST_ASSERT_FALSE(safe_tracker_ok(&t, 0xFFFFF000U + 4999U));
    TEST_ASSERT_TRUE(safe_tracker_ok(&t, 0xFFFFF000U + 5000U));
}

static void test_evaluate_order(void)
{
    TEST_ASSERT_EQUAL_INT(CLEAR_NO_FAULT, fault_evaluate_clear(0, 0, 3, true));
    TEST_ASSERT_EQUAL_INT(CLEAR_NO_FAULT, fault_evaluate_clear(0, 7, 0, false));
    TEST_ASSERT_EQUAL_INT(CLEAR_CODE_MISMATCH, fault_evaluate_clear(7, 4, 3, true));
    TEST_ASSERT_EQUAL_INT(CLEAR_CODE_MISMATCH, fault_evaluate_clear(7, 4, 0, false));
    TEST_ASSERT_EQUAL_INT(CLEAR_NOT_ALLOWED, fault_evaluate_clear(2, 2, 0, false));
    TEST_ASSERT_EQUAL_INT(CLEAR_NO_CLEARS_LEFT, fault_evaluate_clear(7, 7, 0, false));
    TEST_ASSERT_EQUAL_INT(CLEAR_UNSAFE, fault_evaluate_clear(7, 7, 1, false));
    TEST_ASSERT_EQUAL_INT(CLEAR_OK, fault_evaluate_clear(7, 7, 1, true));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_allow_mask);
    RUN_TEST(test_clearable_lookup);
    RUN_TEST(test_safe_boundaries);
    RUN_TEST(test_garbage_readings_unsafe);
    RUN_TEST(test_tracker_needs_5s);
    RUN_TEST(test_tracker_wraps);
    RUN_TEST(test_evaluate_order);
    return UNITY_END();
}
