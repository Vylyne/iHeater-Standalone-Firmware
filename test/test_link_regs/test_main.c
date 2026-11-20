#include <unity.h>
#include <math.h>
#include <string.h>
#include <stdint.h>
#include "config.h"
#include "version.h"
#include "modbus_rtu.h"
#include "fault_policy.h"
#include "link_regs.h"

void setUp(void) {}
void tearDown(void) {}

static LinkRegs R;

static uint16_t rd_in(uint16_t a)
{
    uint16_t v = 0xFFFF;
    TEST_ASSERT_EQUAL_UINT8(0, link_regs_read_input(&R, a, &v));
    return v;
}
static uint16_t rd_hold(uint16_t a)
{
    uint16_t v = 0xFFFF;
    TEST_ASSERT_EQUAL_UINT8(0, link_regs_read_holding(&R, a, &v));
    return v;
}

static void fill(void)
{
    memset(&R, 0, sizeof R);
    R.snap.state = LINK_DEV_HEATING;
    R.snap.active_fault = 0x07;
    R.snap.last_fault = 0x04;
    R.snap.clears_left = 2;
    R.snap.last_clear_result = 5;
    R.snap.reset_cause = LINK_RESET_WATCHDOG;
    R.snap.safe_ok = true;
    R.snap.aux_valid = true;
    R.snap.fan_on = true;
    R.snap.air_c = 54.24f;
    R.snap.heater_c = 80.0f;
    R.snap.aux_c = -3.25f;
    R.snap.air_target_c = 60.0f;
    R.snap.heater_setpoint_c = 75.5f;
    R.snap.pwm_pct = 42.5f;
    R.snap.uptime_s = 0x00012345u;
    R.snap.link_loss_count = 9;
    R.snap.hold_remaining_s = 33;
}

static void test_input_map(void)
{
    fill();
    TEST_ASSERT_EQUAL_HEX16(1, rd_in(0x00));
    TEST_ASSERT_EQUAL_HEX16((VERSION_MAJOR << 8) | VERSION_MINOR, rd_in(0x01));
    TEST_ASSERT_EQUAL_HEX16(VERSION_PATCH, rd_in(0x02));
    TEST_ASSERT_EQUAL_HEX16(LINK_DEV_HEATING, rd_in(0x03));
    TEST_ASSERT_EQUAL_HEX16(0x07, rd_in(0x04));
    TEST_ASSERT_EQUAL_HEX16(1, rd_in(0x05));
    TEST_ASSERT_EQUAL_HEX16(0x01BA, rd_in(0x06));
    TEST_ASSERT_EQUAL_HEX16(2, rd_in(0x07));
    TEST_ASSERT_EQUAL_HEX16(5, rd_in(0x08));
    TEST_ASSERT_EQUAL_HEX16(542, rd_in(0x09));
    TEST_ASSERT_EQUAL_HEX16(800, rd_in(0x0A));
    TEST_ASSERT_EQUAL_HEX16((uint16_t)-33, rd_in(0x0B));
    TEST_ASSERT_EQUAL_HEX16(600, rd_in(0x0C));
    TEST_ASSERT_EQUAL_HEX16(755, rd_in(0x0D));
    TEST_ASSERT_EQUAL_HEX16(425, rd_in(0x0E));
    TEST_ASSERT_EQUAL_HEX16(1, rd_in(0x0F));
    TEST_ASSERT_EQUAL_HEX16(0x2345, rd_in(0x10));
    TEST_ASSERT_EQUAL_HEX16(0x0001, rd_in(0x11));
    TEST_ASSERT_EQUAL_HEX16(9, rd_in(0x12));
    TEST_ASSERT_EQUAL_HEX16(33, rd_in(0x13));
    TEST_ASSERT_EQUAL_HEX16(LINK_RESET_WATCHDOG, rd_in(0x14));
    TEST_ASSERT_EQUAL_HEX16(0x04, rd_in(0x15));

    R.snap.aux_valid = false;
    TEST_ASSERT_EQUAL_HEX16(0x8000, rd_in(0x0B));
    R.snap.safe_ok = false;
    TEST_ASSERT_EQUAL_HEX16(0, rd_in(0x05));
    R.snap.pwm_pct = 150.0f;
    TEST_ASSERT_EQUAL_HEX16(1000, rd_in(0x0E));
    R.snap.pwm_pct = -5.0f;
    TEST_ASSERT_EQUAL_HEX16(0, rd_in(0x0E));

    uint16_t v;
    TEST_ASSERT_EQUAL_UINT8(MODBUS_EX_ILLEGAL_ADDRESS, link_regs_read_input(&R, 0x16, &v));
}

static void test_air_heater_invalid(void)
{
    fill();
    /* Open circuit converts to about -273.15 C: finite but impossible. */
    R.snap.air_c = -273.15f;
    R.snap.heater_c = -273.15f;
    TEST_ASSERT_EQUAL_HEX16(0x8000, rd_in(0x09));
    TEST_ASSERT_EQUAL_HEX16(0x8000, rd_in(0x0A));
    /* Not finite, and exactly at the open-circuit limits (main.c uses <=). */
    R.snap.air_c = NAN;
    R.snap.heater_c = INFINITY;
    TEST_ASSERT_EQUAL_HEX16(0x8000, rd_in(0x09));
    TEST_ASSERT_EQUAL_HEX16(0x8000, rd_in(0x0A));
    R.snap.air_c = (float)MIN_AIR_TEMP;
    R.snap.heater_c = (float)MIN_TEMP;
    TEST_ASSERT_EQUAL_HEX16(0x8000, rd_in(0x09));
    TEST_ASSERT_EQUAL_HEX16(0x8000, rd_in(0x0A));
    /* Just above the limit reads normally. */
    R.snap.air_c = (float)MIN_AIR_TEMP + 0.5f;
    R.snap.heater_c = (float)MIN_TEMP + 0.5f;
    TEST_ASSERT_EQUAL_HEX16(MIN_AIR_TEMP * 10 + 5, rd_in(0x09));
    TEST_ASSERT_EQUAL_HEX16(MIN_TEMP * 10 + 5, rd_in(0x0A));
}

static void test_temp_c10(void)
{
    TEST_ASSERT_EQUAL_INT16(542, link_temp_c10(54.24f));
    TEST_ASSERT_EQUAL_INT16(543, link_temp_c10(54.25f));
    TEST_ASSERT_EQUAL_INT16(-33, link_temp_c10(-3.25f));
    TEST_ASSERT_EQUAL_INT16(INT16_MIN, link_temp_c10(NAN));
    TEST_ASSERT_EQUAL_INT16(INT16_MIN, link_temp_c10(INFINITY));
}

static void test_settings_block(void)
{
    fill();
    TEST_ASSERT_EQUAL_UINT16(MAX_TEMP * 10, rd_hold(0x0200));
    TEST_ASSERT_EQUAL_UINT16(MAX_AIR_TEMP * 10, rd_hold(0x0201));
    TEST_ASSERT_EQUAL_UINT16(LINK_MIN_TARGET * 10, rd_hold(0x0202));
    TEST_ASSERT_EQUAL_UINT16(LINK_MAX_TARGET * 10, rd_hold(0x0203));
    TEST_ASSERT_EQUAL_UINT16(LINK_HOLD_S, rd_hold(0x0204));
    TEST_ASSERT_EQUAL_UINT16(DELTA * 10, rd_hold(0x0205));
    TEST_ASSERT_EQUAL_UINT16((uint16_t)(FAN_ON_TEMP * 10), rd_hold(0x0206));
    TEST_ASSERT_EQUAL_UINT16((uint16_t)(FAN_OFF_TEMP * 10), rd_hold(0x0207));
    TEST_ASSERT_EQUAL_UINT16((uint16_t)(FAN_ON_ACTIVE_MODE_TEMP * 10), rd_hold(0x0208));
    TEST_ASSERT_EQUAL_UINT16((uint16_t)(LINK_CLEAR_MARGIN * 10), rd_hold(0x0209));
    for (uint16_t a = 0x0200; a <= 0x0209; a++)
        TEST_ASSERT_EQUAL_UINT8(MODBUS_EX_ILLEGAL_ADDRESS, link_regs_write_holding(&R, a, 1));
    uint16_t v;
    TEST_ASSERT_EQUAL_UINT8(MODBUS_EX_ILLEGAL_ADDRESS, link_regs_read_holding(&R, 0x020A, &v));
    TEST_ASSERT_EQUAL_UINT8(MODBUS_EX_ILLEGAL_ADDRESS, link_regs_write_holding(&R, 0x020A, 1));
}

static void test_target_write(void)
{
    memset(&R, 0, sizeof R);
    TEST_ASSERT_EQUAL_UINT8(0, link_regs_write_holding(&R, 0x0100, 0));
    TEST_ASSERT_TRUE(R.cmd.target_written);
    TEST_ASSERT_EQUAL_UINT16(0, R.cmd.target_c10);
    memset(&R.cmd, 0, sizeof R.cmd);

    TEST_ASSERT_EQUAL_UINT8(0, link_regs_write_holding(&R, 0x0100, LINK_MIN_TARGET * 10));
    TEST_ASSERT_EQUAL_UINT16(LINK_MIN_TARGET * 10, R.cmd.target_c10);
    TEST_ASSERT_EQUAL_UINT16(LINK_MIN_TARGET * 10, rd_hold(0x0100));
    TEST_ASSERT_EQUAL_UINT16(LINK_MIN_TARGET * 10, rd_in(0x0C));
    memset(&R.cmd, 0, sizeof R.cmd);

    TEST_ASSERT_EQUAL_UINT8(0, link_regs_write_holding(&R, 0x0100, LINK_MAX_TARGET * 10));
    TEST_ASSERT_EQUAL_UINT16(LINK_MAX_TARGET * 10, R.cmd.target_c10);
    TEST_ASSERT_EQUAL_UINT16(LINK_MAX_TARGET * 10, rd_hold(0x0100));
    memset(&R.cmd, 0, sizeof R.cmd);

    TEST_ASSERT_EQUAL_UINT8(MODBUS_EX_ILLEGAL_VALUE, link_regs_write_holding(&R, 0x0100, LINK_MIN_TARGET * 10 - 1));
    TEST_ASSERT_FALSE(R.cmd.target_written);
    TEST_ASSERT_EQUAL_UINT8(MODBUS_EX_ILLEGAL_VALUE, link_regs_write_holding(&R, 0x0100, LINK_MAX_TARGET * 10 + 1));
    TEST_ASSERT_FALSE(R.cmd.target_written);
}

static void test_target_refused_in_fault(void)
{
    memset(&R, 0, sizeof R);
    R.snap.active_fault = 0x07;
    TEST_ASSERT_EQUAL_UINT8(MODBUS_EX_ILLEGAL_ADDRESS, link_regs_write_holding(&R, 0x0100, LINK_MIN_TARGET * 10));
    TEST_ASSERT_FALSE(R.cmd.target_written);
}

static void test_clear_write(void)
{
    memset(&R, 0, sizeof R);
    R.snap.active_fault = 0x07;
    R.snap.clears_left = 3;
    R.snap.safe_ok = true;
    TEST_ASSERT_EQUAL_UINT8(0, link_regs_write_holding(&R, 0x0101, 7));
    TEST_ASSERT_TRUE(R.cmd.clear_approved);
    TEST_ASSERT_EQUAL_UINT8(1, R.snap.last_clear_result);
    TEST_ASSERT_EQUAL_UINT8(3, R.snap.clears_left);

    memset(&R.cmd, 0, sizeof R.cmd);
    R.snap.safe_ok = false;
    TEST_ASSERT_EQUAL_UINT8(MODBUS_EX_ILLEGAL_VALUE, link_regs_write_holding(&R, 0x0101, 7));
    TEST_ASSERT_FALSE(R.cmd.clear_approved);
    TEST_ASSERT_EQUAL_UINT8(3, R.snap.last_clear_result);
    TEST_ASSERT_EQUAL_UINT16(0, rd_hold(0x0101));
}

static void test_bootloader_reserved(void)
{
    memset(&R, 0, sizeof R);
    TEST_ASSERT_EQUAL_UINT8(MODBUS_EX_ILLEGAL_ADDRESS, link_regs_write_holding(&R, 0x0102, LINK_BOOTLOADER_MAGIC));
    TEST_ASSERT_EQUAL_UINT16(0, rd_hold(0x0102));
}

static void test_reset_cause(void)
{
    const uint32_t POR = 1u << 27, PIN = 1u << 26, SFT = 1u << 28, IWDG = 1u << 29;
    TEST_ASSERT_EQUAL_UINT8(LINK_RESET_POWER_ON, link_reset_cause(POR | PIN));
    TEST_ASSERT_EQUAL_UINT8(LINK_RESET_WATCHDOG, link_reset_cause(IWDG | PIN));
    TEST_ASSERT_EQUAL_UINT8(LINK_RESET_SOFTWARE, link_reset_cause(SFT));
    TEST_ASSERT_EQUAL_UINT8(LINK_RESET_PIN, link_reset_cause(PIN));
    TEST_ASSERT_EQUAL_UINT8(LINK_RESET_UNKNOWN, link_reset_cause(0));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_input_map);
    RUN_TEST(test_air_heater_invalid);
    RUN_TEST(test_temp_c10);
    RUN_TEST(test_settings_block);
    RUN_TEST(test_target_write);
    RUN_TEST(test_target_refused_in_fault);
    RUN_TEST(test_clear_write);
    RUN_TEST(test_bootloader_reserved);
    RUN_TEST(test_reset_cause);
    return UNITY_END();
}
