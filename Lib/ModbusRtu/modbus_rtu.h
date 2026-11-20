#ifndef MODBUS_RTU_H
#define MODBUS_RTU_H

/* Modbus RTU slave core. Pure C, no HAL: frame in, reply out. */

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define MODBUS_SLAVE_ADDR           1U
#define MODBUS_EX_ILLEGAL_FUNCTION  0x01U
#define MODBUS_EX_ILLEGAL_ADDRESS   0x02U
#define MODBUS_EX_ILLEGAL_VALUE     0x03U
#define MODBUS_EX_SERVER_FAILURE    0x04U

typedef struct {
    uint8_t (*read_input)(void *ctx, uint16_t addr, uint16_t *out);   /* 0 = ok, else exception */
    uint8_t (*read_holding)(void *ctx, uint16_t addr, uint16_t *out);
    uint8_t (*write_holding)(void *ctx, uint16_t addr, uint16_t value);
    void *ctx;
} ModbusHandlers;

uint16_t modbus_crc16(const uint8_t *data, size_t len);

/* The one acceptance rule: at least 4 bytes, good CRC, address MODBUS_SLAVE_ADDR
 * (broadcast excluded), and a function code without bit 0x80. A master never
 * sends 0x80 codes; they are our own exception replies echoed back. Only frames
 * that pass count as link activity. */
bool modbus_frame_is_ours(const uint8_t *req, size_t req_len);

/* Returns -1 (no reply, no handler called) when modbus_frame_is_ours() fails,
 * or when resp_cap < 5, too small even for an exception reply.
 * Otherwise returns the reply length written to resp. */
int modbus_handle_frame(const uint8_t *req, size_t req_len,
                        uint8_t *resp, size_t resp_cap, const ModbusHandlers *h);

/* Builds an exception reply for function code fc (the 0x80 bit is added).
 * Returns the reply length (5), or -1 when resp_cap < 5. Used by the
 * application to replace a reply it has already prepared but cannot honour. */
int modbus_exception(uint8_t fc, uint8_t code, uint8_t *resp, size_t resp_cap);

#endif /* MODBUS_RTU_H */
