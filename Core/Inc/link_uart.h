#ifndef LINK_UART_H
#define LINK_UART_H

/* USART1 driver for the Modbus link (UART build only). Register-level, no HAL
 * UART. PA9 = TX (USB-C D-), PA10 = RX (USB-C D+), AF1, 115200 8N1.
 * End of frame is the hardware receiver timeout (RTOR, about 1.75 ms of
 * line silence). One frame is buffered; a newer frame replaces one that was
 * never taken, so the caller always gets the most recent request. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* USART1 115200 8N1, PA9/PA10 AF1, RX IRQ, RTO. Safe to call again. */
void link_uart_init(void);

/* True once per completed frame; copies it to buf (room for LINK_RX_BUF bytes)
 * and its length to *len. */
bool link_uart_take_frame(uint8_t *buf, size_t *len);

/* Blocking: returns once the last stop bit has left the wire (TC). */
void link_uart_send(const uint8_t *buf, size_t len);

/* Called from USART1_IRQHandler. */
void link_uart_irq(void);

#endif /* LINK_UART_H */
