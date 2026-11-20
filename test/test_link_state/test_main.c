#include <unity.h>
#include "config.h"
#include "link_state.h"

void setUp(void) {}
void tearDown(void) {}

static void test_no_hold_before_first_activity(void)
{
    LinkState s;
    link_state_init(&s);
    TEST_ASSERT_EQUAL_INT(LINK_EVT_NONE, link_state_tick(&s, 100000));
    TEST_ASSERT_EQUAL_UINT16(0, s.loss_count);
}

static void test_hold_starts_at_3000(void)
{
    LinkState s;
    link_state_init(&s);
    link_state_activity(&s, 1000);
    TEST_ASSERT_EQUAL_INT(LINK_EVT_NONE, link_state_tick(&s, 3999));
    TEST_ASSERT_EQUAL_INT(LINK_EVT_HOLD_STARTED, link_state_tick(&s, 4000));
    TEST_ASSERT_EQUAL_INT(LINK_EVT_NONE, link_state_tick(&s, 4001));
    TEST_ASSERT_EQUAL_UINT16(1, s.loss_count);
}

static void test_remaining_counts_down(void)
{
    LinkState s;
    link_state_init(&s);
    link_state_activity(&s, 1000);
    link_state_tick(&s, 4000);
    TEST_ASSERT_EQUAL_UINT16(60, link_state_hold_remaining_s(&s, 4000));
    TEST_ASSERT_EQUAL_UINT16(60, link_state_hold_remaining_s(&s, 4001));
    TEST_ASSERT_EQUAL_UINT16(1, link_state_hold_remaining_s(&s, 63999));
    TEST_ASSERT_EQUAL_INT(LINK_EVT_NONE, link_state_tick(&s, 63999));
    TEST_ASSERT_EQUAL_INT(LINK_EVT_HOLD_EXPIRED, link_state_tick(&s, 64000));
    TEST_ASSERT_EQUAL_UINT16(0, link_state_hold_remaining_s(&s, 64000));
}

static void test_expired_reported_once(void)
{
    LinkState s;
    link_state_init(&s);
    link_state_activity(&s, 1000);
    link_state_tick(&s, 4000);
    link_state_tick(&s, 64000);
    TEST_ASSERT_EQUAL_INT(LINK_EVT_NONE, link_state_tick(&s, 64001));
    TEST_ASSERT_EQUAL_INT(LINK_EVT_NONE, link_state_tick(&s, 200000));
    TEST_ASSERT_EQUAL_UINT16(1, s.loss_count);
}

static void test_activity_ends_hold(void)
{
    LinkState s;
    link_state_init(&s);
    link_state_activity(&s, 1000);
    link_state_tick(&s, 4000);
    link_state_activity(&s, 10000);
    TEST_ASSERT_FALSE(s.in_hold);
    TEST_ASSERT_EQUAL_UINT16(0, link_state_hold_remaining_s(&s, 10000));
    TEST_ASSERT_EQUAL_INT(LINK_EVT_HOLD_STARTED, link_state_tick(&s, 13000));
    TEST_ASSERT_EQUAL_UINT16(2, s.loss_count);
}

static void test_activity_after_expiry_allows_new_hold(void)
{
    LinkState s;
    link_state_init(&s);
    link_state_activity(&s, 1000);
    link_state_tick(&s, 4000);
    link_state_tick(&s, 64000);
    link_state_activity(&s, 70000);
    TEST_ASSERT_EQUAL_INT(LINK_EVT_HOLD_STARTED, link_state_tick(&s, 73000));
    TEST_ASSERT_EQUAL_UINT16(2, s.loss_count);
}

static void test_wraparound(void)
{
    LinkState s;
    uint32_t t0 = 0xFFFFF000U;
    link_state_init(&s);
    link_state_activity(&s, t0);
    TEST_ASSERT_EQUAL_INT(LINK_EVT_NONE, link_state_tick(&s, t0 + 2999U));
    TEST_ASSERT_EQUAL_INT(LINK_EVT_HOLD_STARTED, link_state_tick(&s, t0 + 3000U));
    uint32_t h = t0 + 3000U;
    TEST_ASSERT_EQUAL_UINT16(60, link_state_hold_remaining_s(&s, h));
    TEST_ASSERT_EQUAL_INT(LINK_EVT_NONE, link_state_tick(&s, h + 59999U));
    TEST_ASSERT_EQUAL_INT(LINK_EVT_HOLD_EXPIRED, link_state_tick(&s, h + 60000U));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_no_hold_before_first_activity);
    RUN_TEST(test_hold_starts_at_3000);
    RUN_TEST(test_remaining_counts_down);
    RUN_TEST(test_expired_reported_once);
    RUN_TEST(test_activity_ends_hold);
    RUN_TEST(test_activity_after_expiry_allows_new_hold);
    RUN_TEST(test_wraparound);
    return UNITY_END();
}
