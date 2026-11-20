#ifndef MODBUS_RTU_H
#define MODBUS_RTU_H

/* Modbus RTU slave core. Pure C, no HAL: frame in, reply out. */

#include <stdint.h>
#include <stddef.h>

#define MODBUS_SLAVE_ADDR           1U
#define MODBUS_EX_ILLEGAL_FUNCTION  0x01U
#define MODBUS_EX_ILLEGAL_ADDRESS   0x02U
#define MODBUS_EX_ILLEGAL_VALUE     0x03U

typedef struct {
    uint8_t (*read_input)(void *ctx, uint16_t addr, uint16_t *out);   /* 0 = ok, else exception */
    uint8_t (*read_holding)(void *ctx, uint16_t addr, uint16_t *out);
    uint8_t (*write_holding)(void *ctx, uint16_t addr, uint16_t value);
    void *ctx;
} ModbusHandlers;

uint16_t modbus_crc16(const uint8_t *data, size_t len);

/* Returns -1 (no reply, no handler called) when the frame is shorter than 4 bytes,
 * the CRC is bad, or the address is not MODBUS_SLAVE_ADDR (broadcast included).
 * Also -1 when resp_cap < 5, too small even for an exception reply.
 * Otherwise returns the reply length written to resp. */
int modbus_handle_frame(const uint8_t *req, size_t req_len,
                        uint8_t *resp, size_t resp_cap, const ModbusHandlers *h);

#endif /* MODBUS_RTU_H */
