#include "modbus_rtu.h"

#define FC_READ_HOLDING   0x03U
#define FC_READ_INPUT     0x04U
#define FC_WRITE_SINGLE   0x06U
#define FC_WRITE_MULTIPLE 0x10U

#define MAX_READ_QTY      125U
#define MAX_WRITE_QTY     123U
#define MIN_FRAME_LEN     4U   /* addr + fc + crc16 */
#define EXC_REPLY_LEN     5U   /* addr + fc|0x80 + code + crc16 */

uint16_t modbus_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFFU;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 1U) ? (uint16_t)((crc >> 1) ^ 0xA001U) : (uint16_t)(crc >> 1);
        }
    }
    return crc;
}

static int seal(uint8_t *resp, size_t body_len)
{
    uint16_t crc = modbus_crc16(resp, body_len);
    resp[body_len] = (uint8_t)(crc & 0xFFU);
    resp[body_len + 1U] = (uint8_t)(crc >> 8);
    return (int)(body_len + 2U);
}

static int exception_reply(uint8_t *resp, size_t cap, uint8_t fc, uint8_t code)
{
    if (cap < EXC_REPLY_LEN) {
        return -1;
    }
    resp[0] = MODBUS_SLAVE_ADDR;
    resp[1] = (uint8_t)(fc | 0x80U);
    resp[2] = code;
    return seal(resp, 3U);
}

static uint16_t be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

int modbus_handle_frame(const uint8_t *req, size_t req_len,
                        uint8_t *resp, size_t resp_cap, const ModbusHandlers *h)
{
    if (req_len < MIN_FRAME_LEN) {
        return -1;
    }
    size_t body = req_len - 2U;
    uint16_t crc = modbus_crc16(req, body);
    if (req[body] != (uint8_t)(crc & 0xFFU) || req[body + 1U] != (uint8_t)(crc >> 8)) {
        return -1;
    }
    if (req[0] != MODBUS_SLAVE_ADDR) {
        return -1;   /* other slave or broadcast: stay silent */
    }

    uint8_t fc = req[1];
    switch (fc) {
    case FC_READ_HOLDING:
    case FC_READ_INPUT: {
        if (body != 6U) {
            return exception_reply(resp, resp_cap, fc, MODBUS_EX_ILLEGAL_VALUE);
        }
        uint16_t addr = be16(&req[2]);
        uint16_t qty = be16(&req[4]);
        if (qty == 0U || qty > MAX_READ_QTY || (size_t)qty * 2U + 5U > resp_cap) {
            return exception_reply(resp, resp_cap, fc, MODBUS_EX_ILLEGAL_VALUE);
        }
        if ((uint32_t)addr + qty > 0x10000U) {
            return exception_reply(resp, resp_cap, fc, MODBUS_EX_ILLEGAL_ADDRESS);
        }
        uint8_t (*rd)(void *, uint16_t, uint16_t *) =
            (fc == FC_READ_INPUT) ? h->read_input : h->read_holding;
        if (rd == NULL) {
            return exception_reply(resp, resp_cap, fc, MODBUS_EX_ILLEGAL_FUNCTION);
        }
        for (uint16_t i = 0; i < qty; i++) {
            uint16_t v = 0;
            uint8_t ex = rd(h->ctx, (uint16_t)(addr + i), &v);
            if (ex != 0U) {
                return exception_reply(resp, resp_cap, fc, ex);
            }
            resp[3U + 2U * i] = (uint8_t)(v >> 8);
            resp[4U + 2U * i] = (uint8_t)(v & 0xFFU);
        }
        resp[0] = MODBUS_SLAVE_ADDR;
        resp[1] = fc;
        resp[2] = (uint8_t)(qty * 2U);
        return seal(resp, 3U + (size_t)qty * 2U);
    }
    case FC_WRITE_SINGLE: {
        if (body != 6U) {
            return exception_reply(resp, resp_cap, fc, MODBUS_EX_ILLEGAL_VALUE);
        }
        if (h->write_holding == NULL) {
            return exception_reply(resp, resp_cap, fc, MODBUS_EX_ILLEGAL_FUNCTION);
        }
        if (resp_cap < 8U) {
            return exception_reply(resp, resp_cap, fc, MODBUS_EX_ILLEGAL_VALUE);
        }
        uint8_t ex = h->write_holding(h->ctx, be16(&req[2]), be16(&req[4]));
        if (ex != 0U) {
            return exception_reply(resp, resp_cap, fc, ex);
        }
        for (size_t i = 0; i < 6U; i++) {
            resp[i] = req[i];
        }
        return seal(resp, 6U);
    }
    case FC_WRITE_MULTIPLE: {
        if (body < 7U) {
            return exception_reply(resp, resp_cap, fc, MODBUS_EX_ILLEGAL_VALUE);
        }
        uint16_t addr = be16(&req[2]);
        uint16_t qty = be16(&req[4]);
        if (qty == 0U || qty > MAX_WRITE_QTY || req[6] != qty * 2U || body != 7U + (size_t)qty * 2U) {
            return exception_reply(resp, resp_cap, fc, MODBUS_EX_ILLEGAL_VALUE);
        }
        if (h->write_holding == NULL) {
            return exception_reply(resp, resp_cap, fc, MODBUS_EX_ILLEGAL_FUNCTION);
        }
        if ((uint32_t)addr + qty > 0x10000U) {
            return exception_reply(resp, resp_cap, fc, MODBUS_EX_ILLEGAL_ADDRESS);
        }
        if (resp_cap < 8U) {
            return exception_reply(resp, resp_cap, fc, MODBUS_EX_ILLEGAL_VALUE);
        }
        for (uint16_t i = 0; i < qty; i++) {
            uint8_t ex = h->write_holding(h->ctx, (uint16_t)(addr + i), be16(&req[7U + 2U * i]));
            if (ex != 0U) {
                return exception_reply(resp, resp_cap, fc, ex);   /* earlier writes stay applied */
            }
        }
        for (size_t i = 0; i < 6U; i++) {
            resp[i] = req[i];
        }
        return seal(resp, 6U);
    }
    default:
        return exception_reply(resp, resp_cap, fc, MODBUS_EX_ILLEGAL_FUNCTION);
    }
}
