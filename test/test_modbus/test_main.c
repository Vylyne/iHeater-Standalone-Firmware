#include <unity.h>
#include <string.h>
#include "modbus_rtu.h"

static uint16_t fake[0x300];
static int write_count;
static int call_count;
static uint16_t last_write_addr;

void setUp(void)
{
    for (unsigned i = 0; i < 0x300; i++) fake[i] = (uint16_t)(i * 3U);
    write_count = 0;
    call_count = 0;
    last_write_addr = 0;
}
void tearDown(void) {}

static uint8_t f_read(void *ctx, uint16_t addr, uint16_t *out)
{
    (void)ctx;
    call_count++;
    if (addr == 0x0200U) return MODBUS_EX_ILLEGAL_ADDRESS;
    *out = fake[addr];
    return 0;
}
static uint8_t f_write(void *ctx, uint16_t addr, uint16_t value)
{
    (void)ctx;
    call_count++;
    if (addr == 0x0200U) return MODBUS_EX_ILLEGAL_ADDRESS;
    if (value == 0xDEADU) return MODBUS_EX_ILLEGAL_VALUE;
    fake[addr] = value;
    last_write_addr = addr;
    write_count++;
    return 0;
}
static const ModbusHandlers H = { f_read, f_read, f_write, NULL };

/* Append CRC (low byte first) to the first n bytes; returns n + 2. */
static size_t seal(uint8_t *f, size_t n)
{
    uint16_t c = modbus_crc16(f, n);
    f[n] = (uint8_t)(c & 0xFFU);
    f[n + 1] = (uint8_t)(c >> 8);
    return n + 2;
}

static void expect_frame(const uint8_t *body, size_t n, const uint8_t *got, size_t got_len)
{
    uint8_t exp[64];
    memcpy(exp, body, n);
    size_t el = seal(exp, n);
    TEST_ASSERT_EQUAL_UINT32(el, got_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp, got, el);
}

static void test_crc_known_vector(void)
{
    const uint8_t d[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x0A};
    TEST_ASSERT_EQUAL_HEX16(0xCDC5, modbus_crc16(d, sizeof d));
    uint8_t f[8];
    memcpy(f, d, 6);
    seal(f, 6);
    TEST_ASSERT_EQUAL_HEX8(0xC5, f[6]);
    TEST_ASSERT_EQUAL_HEX8(0xCD, f[7]);
}

static void test_fc04_reads_two(void)
{
    uint8_t rq[8] = {1, 4, 0, 9, 0, 2}, rs[32];
    size_t n = seal(rq, 6);
    int r = modbus_handle_frame(rq, n, rs, sizeof rs, &H);
    const uint8_t b[] = {1, 4, 4, 0x00, 0x1B, 0x00, 0x1E};
    TEST_ASSERT_EQUAL_INT(9, r);
    expect_frame(b, sizeof b, rs, (size_t)r);
}

static void test_fc03_reads_one(void)
{
    uint8_t rq[8] = {1, 3, 1, 0, 0, 1}, rs[32];
    size_t n = seal(rq, 6);
    int r = modbus_handle_frame(rq, n, rs, sizeof rs, &H);
    const uint8_t b[] = {1, 3, 2, 0x03, 0x00};
    TEST_ASSERT_GREATER_THAN_INT(0, r);
    expect_frame(b, sizeof b, rs, (size_t)r);
}

static void test_fc06_echoes(void)
{
    uint8_t rq[8] = {1, 6, 1, 0, 0x02, 0x26}, rs[32];
    size_t n = seal(rq, 6);
    int r = modbus_handle_frame(rq, n, rs, sizeof rs, &H);
    TEST_ASSERT_EQUAL_INT(8, r);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(rq, rs, 8);
    TEST_ASSERT_EQUAL_HEX16(0x0226, fake[0x0100]);
}

static void test_fc10_writes_two(void)
{
    uint8_t rq[16] = {1, 0x10, 1, 0, 0, 2, 4, 0, 1, 0, 2}, rs[32];
    size_t n = seal(rq, 11);
    int r = modbus_handle_frame(rq, n, rs, sizeof rs, &H);
    const uint8_t b[] = {1, 0x10, 1, 0, 0, 2};
    TEST_ASSERT_GREATER_THAN_INT(0, r);
    expect_frame(b, sizeof b, rs, (size_t)r);
    TEST_ASSERT_EQUAL_HEX16(1, fake[0x0100]);
    TEST_ASSERT_EQUAL_HEX16(2, fake[0x0101]);
}

static void test_fc10_stops_at_refused(void)
{
    uint8_t rq[16] = {1, 0x10, 1, 0xFF, 0, 2, 4, 0, 7, 0, 8}, rs[32];
    size_t n = seal(rq, 11);
    int r = modbus_handle_frame(rq, n, rs, sizeof rs, &H);
    const uint8_t b[] = {1, 0x90, 2};
    TEST_ASSERT_GREATER_THAN_INT(0, r);
    expect_frame(b, sizeof b, rs, (size_t)r);
    TEST_ASSERT_EQUAL_HEX16(7, fake[0x01FF]);
    TEST_ASSERT_EQUAL_INT(1, write_count);
}

static void test_fc10_bad_byte_count(void)
{
    uint8_t rq[16] = {1, 0x10, 1, 0, 0, 2, 2, 0, 1, 0, 2}, rs[32];
    size_t n = seal(rq, 11);
    int r = modbus_handle_frame(rq, n, rs, sizeof rs, &H);
    const uint8_t b[] = {1, 0x90, 3};
    TEST_ASSERT_GREATER_THAN_INT(0, r);
    expect_frame(b, sizeof b, rs, (size_t)r);
    TEST_ASSERT_EQUAL_INT(0, write_count);
}

static void test_unknown_function(void)
{
    uint8_t rq[8] = {1, 0x2B, 0, 0, 0, 0}, rs[32];
    size_t n = seal(rq, 6);
    int r = modbus_handle_frame(rq, n, rs, sizeof rs, &H);
    const uint8_t b[] = {1, 0xAB, 1};
    TEST_ASSERT_GREATER_THAN_INT(0, r);
    expect_frame(b, sizeof b, rs, (size_t)r);
}

static void test_handler_exception(void)
{
    uint8_t rq[8] = {1, 4, 2, 0, 0, 1}, rs[32];
    size_t n = seal(rq, 6);
    int r = modbus_handle_frame(rq, n, rs, sizeof rs, &H);
    const uint8_t b[] = {1, 0x84, 2};
    TEST_ASSERT_GREATER_THAN_INT(0, r);
    expect_frame(b, sizeof b, rs, (size_t)r);
}

static void test_value_refused(void)
{
    uint8_t rq[8] = {1, 6, 1, 0, 0xDE, 0xAD}, rs[32];
    size_t n = seal(rq, 6);
    int r = modbus_handle_frame(rq, n, rs, sizeof rs, &H);
    const uint8_t b[] = {1, 0x86, 3};
    TEST_ASSERT_GREATER_THAN_INT(0, r);
    expect_frame(b, sizeof b, rs, (size_t)r);
}

static void test_quantity_zero_or_over_125(void)
{
    const uint8_t q[2] = {0, 126};
    for (int i = 0; i < 2; i++) {
        uint8_t rq[8] = {1, 4, 0, 0, 0, q[i]}, rs[300];
        size_t n = seal(rq, 6);
        int r = modbus_handle_frame(rq, n, rs, sizeof rs, &H);
        const uint8_t b[] = {1, 0x84, 3};
        TEST_ASSERT_GREATER_THAN_INT(0, r);
        expect_frame(b, sizeof b, rs, (size_t)r);
    }
}

static void test_reply_would_overflow(void)
{
    uint8_t rq[8] = {1, 4, 0, 0, 0, 10}, rs[16];
    memset(rs, 0xEE, sizeof rs);
    size_t n = seal(rq, 6);
    int r = modbus_handle_frame(rq, n, rs, 16, &H);
    const uint8_t b[] = {1, 0x84, 3};
    TEST_ASSERT_GREATER_THAN_INT(0, r);
    expect_frame(b, sizeof b, rs, (size_t)r);
    for (int i = 5; i < 16; i++) TEST_ASSERT_EQUAL_HEX8(0xEE, rs[i]);
}

static void test_other_address_ignored(void)
{
    uint8_t rs[32];
    for (uint8_t a = 0; a < 3; a += 2) {
        uint8_t rq[8] = {a, 4, 0, 9, 0, 2};
        size_t n = seal(rq, 6);
        TEST_ASSERT_EQUAL_INT(-1, modbus_handle_frame(rq, n, rs, sizeof rs, &H));
    }
    TEST_ASSERT_EQUAL_INT(0, call_count);
}

static void test_bad_crc_ignored(void)
{
    uint8_t rq[8] = {1, 4, 0, 9, 0, 2}, rs[32];
    size_t n = seal(rq, 6);
    rq[n - 1] ^= 0xFFU;
    TEST_ASSERT_EQUAL_INT(-1, modbus_handle_frame(rq, n, rs, sizeof rs, &H));
    TEST_ASSERT_EQUAL_INT(0, call_count);
}

static void test_leading_noise_ignored(void)
{
    uint8_t good[8] = {1, 4, 0, 9, 0, 2}, buf[9], rs[32];
    size_t n = seal(good, 6);
    buf[0] = 0xFF;
    memcpy(buf + 1, good, n);
    TEST_ASSERT_EQUAL_INT(-1, modbus_handle_frame(buf, n + 1, rs, sizeof rs, &H));
    TEST_ASSERT_EQUAL_INT(9, modbus_handle_frame(good, n, rs, sizeof rs, &H));
}

static void test_short_frame_ignored(void)
{
    uint8_t rq[4] = {1, 4, 0, 0}, rs[32];
    for (size_t l = 0; l <= 3; l++)
        TEST_ASSERT_EQUAL_INT(-1, modbus_handle_frame(rq, l, rs, sizeof rs, &H));
}

static void test_address_wrap_refused(void)
{
    uint8_t rq[16] = {1, 0x10, 0xFF, 0xFF, 0, 2, 4, 0, 1, 0, 2}, rs[32];
    size_t n = seal(rq, 11);
    int r = modbus_handle_frame(rq, n, rs, sizeof rs, &H);
    const uint8_t b[] = {1, 0x90, 2};
    TEST_ASSERT_GREATER_THAN_INT(0, r);
    expect_frame(b, sizeof b, rs, (size_t)r);
    uint8_t rd[8] = {1, 3, 0xFF, 0xFF, 0, 2};
    n = seal(rd, 6);
    r = modbus_handle_frame(rd, n, rs, sizeof rs, &H);
    const uint8_t b2[] = {1, 0x83, 2};
    TEST_ASSERT_GREATER_THAN_INT(0, r);
    expect_frame(b2, sizeof b2, rs, (size_t)r);
    TEST_ASSERT_EQUAL_INT(0, call_count);
}

static void test_write_small_cap_no_write(void)
{
    uint8_t rq[8] = {1, 6, 1, 0, 0x02, 0x26}, rs[6];
    size_t n = seal(rq, 6);
    int r = modbus_handle_frame(rq, n, rs, 6, &H);
    const uint8_t b[] = {1, 0x86, 3};
    TEST_ASSERT_GREATER_THAN_INT(0, r);
    expect_frame(b, sizeof b, rs, (size_t)r);
    TEST_ASSERT_EQUAL_INT(0, write_count);
    uint8_t rq10[16] = {1, 0x10, 1, 0, 0, 1, 2, 0, 5};
    n = seal(rq10, 9);
    r = modbus_handle_frame(rq10, n, rs, 4, &H);
    TEST_ASSERT_EQUAL_INT(-1, r);
    TEST_ASSERT_EQUAL_INT(0, write_count);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_crc_known_vector);
    RUN_TEST(test_fc04_reads_two);
    RUN_TEST(test_fc03_reads_one);
    RUN_TEST(test_fc06_echoes);
    RUN_TEST(test_fc10_writes_two);
    RUN_TEST(test_fc10_stops_at_refused);
    RUN_TEST(test_fc10_bad_byte_count);
    RUN_TEST(test_unknown_function);
    RUN_TEST(test_handler_exception);
    RUN_TEST(test_value_refused);
    RUN_TEST(test_quantity_zero_or_over_125);
    RUN_TEST(test_reply_would_overflow);
    RUN_TEST(test_other_address_ignored);
    RUN_TEST(test_bad_crc_ignored);
    RUN_TEST(test_leading_noise_ignored);
    RUN_TEST(test_short_frame_ignored);
    RUN_TEST(test_address_wrap_refused);
    RUN_TEST(test_write_small_cap_no_write);
    return UNITY_END();
}
